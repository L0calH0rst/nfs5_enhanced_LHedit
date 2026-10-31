// weather.cpp – LFSP: Wetter im Rennen ([Weather] in lfsp.ini, doc/re/13-VIDEO.md §8, FORMATE.md §4.12).
// 1. Menüpunkt „Wetter“ (Original/Regen/Schnee) in der Streckenwahl: eigener Accessor Track1.Weather im
//    Datenbereich FEdaTrack, eigene Text-IDs, Eintrag wird beim Laden in location.lay/QuickRace.lay eingefügt.
// 2. Regen- und Schneestärke: Sky_InterpLerp18/20 (Wetterspur der Sky/<Strecke>.bin) ersetzt; Wahl aus dem Menü
//    und Abdeckung (Brücke, Tunnel: Fläche des Kollisionsbaums über der Kamera) gehen ein.
// 3. Scheibentropfen: nach R_WindshieldDropsTick wandern die Tropfen mit dem Fahrtwind über die Scheibe.

#include <windows.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace lfsp_weather
{
    using LogFn = void (*)(const char *, ...);
    void Install(const char * ini, LogFn log);
}

namespace
{
    lfsp_weather::LogFn Log = nullptr;
    std::string g_ini;

    // ---- Adressen (Porsche.exe v3.5) ----
    constexpr std::uintptr_t kSkyRain       = 0x45D000; // Sky_InterpLerp18, thiscall(sky) → float Regen
    constexpr std::uintptr_t kSkySnow       = 0x45CFD0; // Sky_InterpLerp20, thiscall(sky) → float Schnee
    constexpr std::uintptr_t kSkyKeyInterp  = 0x45A560; // Sky_ColourKeyInterp, thiscall(spur, abschnitt) → Gewicht
    constexpr std::uintptr_t kSkyLerpRain   = 0x45A8E0; // Sky_ColourTrack__Lerp18, thiscall(spur, gewicht)
    constexpr std::uintptr_t kSkyLerpSnow   = 0x45A940; // Sky_ColourTrack__Lerp20
    constexpr std::uintptr_t kDropsTickCall = 0x459112; // call R_WindshieldDropsTick in R_WeatherUpdateDraw
    constexpr std::uintptr_t kDropsTick     = 0x4582C0; // thiscall(tropfen, takte, regen, schnee)
    constexpr std::uintptr_t kDropsBuild    = 0x457FD0; // fastcall(tropfen): Ecken aller Tropfen neu
    constexpr std::uintptr_t kRenderContext = 0x628130; // +0x10 Ansicht, dort +0x10C/+0x110/+0x114 Lage
    constexpr std::uintptr_t kWorld         = 0x628960; // Trk_gWorld, +0x24 → Kollisionsbaum, *Baum = Wurzel
    constexpr std::uintptr_t kFindLeaf      = 0x484500; // WTree__FindLeaf, thiscall(knoten, punkt)
    constexpr std::uintptr_t kVtQuad        = 0x5B4880; // WTreeElem_Quad (Ecken +0xC…+0x18)
    constexpr std::uintptr_t kVtTri         = 0x5B48B0; // WTreeElem_Tri (Ecken +0xC…+0x14)
    constexpr std::uintptr_t kTrackVtable   = 0x5B7DFC; // FEdaTrack: Slot 0 Initialize, Slot 2 Cleanup
    constexpr std::uintptr_t kTrackInit     = 0x4F9790;
    constexpr std::uintptr_t kTrackCleanup  = 0x4F9F10;
    constexpr std::uintptr_t kAccCtorCount  = 0x4BB680; // FEPositionableAccessor_CtorCount (FELocalizedTextAccessor)
    constexpr std::uintptr_t kMemAllocIn    = 0x59EFC0; // cdecl(größe, pool)
    constexpr std::uintptr_t kFePool        = 0x5D9B48; // Mem_GetFePool liest diesen Wert
    constexpr std::uintptr_t kGetString     = 0x4E06D0; // FE-Text zu ID, cdecl(id) → char*
    constexpr std::uintptr_t kLanguage      = 0x690710; // 0 Englisch, 2 Deutsch (Spalten der FEStrings.csv)
    constexpr std::uintptr_t kGameLoad      = 0x59D8E0; // File_GameLoadTagged, cdecl(pfad, pool) → Block
    constexpr std::uintptr_t kMemAllocTag   = 0x531CA0; // Mem_AllocTagged, cdecl(name, größe, pool)
    constexpr std::uintptr_t kMemFree       = 0x531F90; // Mem_Free, cdecl(block)

    // Eigene Text-IDs (die .loc-Tabellen kennen nur 16 Bit, das Spiel nutzt bis etwa 23000)
    constexpr unsigned kStrLabel    = 64000;
    constexpr unsigned kStrOriginal = 64001;
    constexpr unsigned kStrRain     = 64002;
    constexpr unsigned kStrSnow     = 64003;
    constexpr unsigned kStrHelp     = 64004;
    struct Text
    {
        const char * en;
        const char * de;
    };
    const Text kTexts[] = {
        { "weather", "Wetter" },
        { "original", "Original" },
        { "rain", "Regen" },
        { "snow", "Schnee" },
        { "Choose the weather: as on the track, rain or snow.",
          "Wetter w\xE4hlen: wie auf der Strecke, Regen oder Schnee." }, // Texte der FE in Windows-1252
    };

    // ---- Einstellungen ----
    int g_mode = 0;              // 0 Original, 1 Regen, 2 Schnee
    int g_menu = 1;
    std::string g_posLocation = "377,71"; // unter „records…“
    std::string g_posQuick;      // leer = kein Eintrag im Schnellen Rennen
    float g_rainAmount = 0.8f;
    float g_snowAmount = 0.8f;
    int g_coverOn = 1;
    float g_coverFade = 0.7f;    // Sekunden für ganz an/aus
    float g_coverMin = 0.3f;     // Fläche mindestens so hoch über der Kamera (m)
    float g_coverMax = 60.0f;    // … und höchstens so hoch
    float g_coverAhead = 0.8f;   // Sekunden voraus (Einfahrt)
    int g_dropsOn = 1;
    float g_dropWind = 0.5f;     // Scheibenhöhen je Sekunde bei vollem Fahrtwind
    float g_dropSlide = 0.04f;   // Scheibenhöhen je Sekunde, große Tropfen im Stand (nur Regen)
    float g_dropSpread = 0.25f;  // Anteil nach außen beim Fahrtwind (0 = gerade nach oben)
    float g_windStart = 30.0f;   // km/h
    float g_windFull = 150.0f;   // km/h
    int g_logOn = 0;

    template <typename T>
    T & At(void * base, std::size_t offset)
    {
        return *reinterpret_cast<T *>(static_cast<unsigned char *>(base) + offset);
    }

    // ---- Patch-Helfer (wie in lfsp.cpp: nur wenn die Originalbytes passen) ----
    bool Write(std::uintptr_t target, const void * data, std::size_t size)
    {
        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<void *>(target), size, PAGE_EXECUTE_READWRITE, &old))
        {
            return false;
        }
        std::memcpy(reinterpret_cast<void *>(target), data, size);
        VirtualProtect(reinterpret_cast<void *>(target), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(target), size);
        return true;
    }

    bool PatchJump(const char * name, std::uintptr_t target, const unsigned char * expected, std::size_t size,
                   void * dest)
    {
        if (size < 5 || std::memcmp(reinterpret_cast<void *>(target), expected, size) != 0)
        {
            Log("Wetter: Patch %s @%08X: Bytes passen nicht, uebersprungen", name, static_cast<unsigned>(target));
            return false;
        }
        unsigned char code[16] = {};
        code[0] = 0xE9;
        *reinterpret_cast<std::int32_t *>(code + 1) =
            static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(dest) - (target + 5));
        for (std::size_t i = 5; i < size; ++i)
        {
            code[i] = 0x90;
        }
        Write(target, code, size);
        Log("Wetter: Patch %s @%08X installiert", name, static_cast<unsigned>(target));
        return true;
    }

    bool PatchCall(const char * name, std::uintptr_t site, std::uintptr_t oldDest, void * dest)
    {
        const auto * p = reinterpret_cast<const unsigned char *>(site);
        if (p[0] != 0xE8 || site + 5 + *reinterpret_cast<const std::int32_t *>(p + 1) != oldDest)
        {
            Log("Wetter: Aufruf %s @%08X: Bytes passen nicht, uebersprungen", name, static_cast<unsigned>(site));
            return false;
        }
        const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(dest) - (site + 5));
        Write(site + 1, &rel, 4);
        Log("Wetter: Aufruf %s @%08X umgelenkt", name, static_cast<unsigned>(site));
        return true;
    }

    bool InstallHook(const char * name, std::uintptr_t target, const unsigned char * expected, std::size_t size,
                     void * detour, void ** original)
    {
        if (std::memcmp(reinterpret_cast<void *>(target), expected, size) != 0)
        {
            Log("Wetter: Hook %s @%08X: Bytes passen nicht, uebersprungen", name, static_cast<unsigned>(target));
            return false;
        }
        auto * tramp = static_cast<unsigned char *>(
            VirtualAlloc(nullptr, size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!tramp)
        {
            return false;
        }
        std::memcpy(tramp, expected, size);
        tramp[size] = 0xE9;
        *reinterpret_cast<std::int32_t *>(tramp + size + 1) =
            static_cast<std::int32_t>(target + size - (reinterpret_cast<std::uintptr_t>(tramp) + size + 5));
        *original = tramp;
        unsigned char code[16] = {};
        code[0] = 0xE9;
        *reinterpret_cast<std::int32_t *>(code + 1) =
            static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(detour) - (target + 5));
        for (std::size_t i = 5; i < size; ++i)
        {
            code[i] = 0x90;
        }
        Write(target, code, size);
        Log("Wetter: Hook %s @%08X installiert", name, static_cast<unsigned>(target));
        return true;
    }

    bool PatchSlot(const char * name, std::uintptr_t slot, std::uintptr_t expected, void * dest)
    {
        if (*reinterpret_cast<std::uintptr_t *>(slot) != expected)
        {
            Log("Wetter: vtable %s @%08X: Eintrag passt nicht, uebersprungen", name, static_cast<unsigned>(slot));
            return false;
        }
        const auto value = reinterpret_cast<std::uintptr_t>(dest);
        Write(slot, &value, 4);
        Log("Wetter: vtable %s @%08X umgelenkt", name, static_cast<unsigned>(slot));
        return true;
    }

    // ---- Abdeckung: Fläche des Kollisionsbaums über der Kamera ----
    // Der Kollisionsbaum (Trk_BuildCollisionTree) enthält Fahrbahn, Brückendecks und weitere Flächen der LOD-0-
    // Geometrie übereinander. TrkPos::FindSurface wählt daraus die nächste zur Höhe; hier zählt jede Fläche, die
    // den Punkt in X/Z enthält und darüber liegt.
    struct CoverProbe
    {
        bool valid = false;
        bool covered = false;
        int surfaces = 0;      // Flächen am Punkt (X/Z)
        float above = 0.0f;    // nächste Fläche über der Kamera (m), 0 = keine
        float pos[3] = {};
    };

    bool ReadCamera(float * pos)
    {
        const auto * ctx = *reinterpret_cast<unsigned char * const *>(kRenderContext);
        const auto * view = ctx ? *reinterpret_cast<unsigned char * const *>(ctx + 0x10) : nullptr;
        if (!view)
        {
            return false;
        }
        std::memcpy(pos, view + 0x10C, 12);
        // R_WeatherUpdateDraw setzt die Ansicht zum Zeichnen des Niederschlags kurz auf den Ursprung
        return pos[0] != 0.0f || pos[1] != 0.0f || pos[2] != 0.0f;
    }

    // Fläche über probe.pos suchen
    void ProbeCover(CoverProbe & probe)
    {
        __try
        {
            auto * world = *reinterpret_cast<unsigned char **>(kWorld);
            auto ** tree = world ? *reinterpret_cast<void ***>(world + 0x24) : nullptr;
            if (!tree || !*tree)
            {
                return;
            }
            using FindLeafFn = unsigned char * (__thiscall *)(void * node, const float * point);
            auto * leaf = reinterpret_cast<FindLeafFn>(kFindLeaf)(*tree, probe.pos);
            auto ** list = leaf ? *reinterpret_cast<unsigned char ***>(leaf + 0x14) : nullptr;
            probe.valid = true;
            if (!list || !list[0])
            {
                return;
            }
            const float * p = probe.pos;
            for (unsigned char * e = list[0]; e != list[1]; e += 8)
            {
                auto * elem = *reinterpret_cast<unsigned char **>(e + 4);
                const auto vt = elem ? *reinterpret_cast<std::uintptr_t *>(elem) : 0;
                if (vt != kVtQuad && vt != kVtTri)
                {
                    continue;
                }
                using ContainsFn = bool (__thiscall *)(void * elem, const float * point);
                const auto contains = reinterpret_cast<ContainsFn>(reinterpret_cast<std::uintptr_t *>(vt)[3]);
                if (!contains(elem, p))
                {
                    continue;
                }
                ++probe.surfaces;
                const float * v0 = *reinterpret_cast<float **>(elem + 0xC);
                const float * v1 = *reinterpret_cast<float **>(elem + 0x10);
                const float * v2 = *reinterpret_cast<float **>(elem + 0x14);
                const float a[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
                const float b[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
                const float n[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
                if (std::fabs(n[1]) < 1e-4f)
                {
                    continue; // senkrechte Fläche
                }
                const float h = v0[1] - (n[0] * (p[0] - v0[0]) + n[2] * (p[2] - v0[2])) / n[1];
                const float up = h - p[1];
                if (up > g_coverMin && up < g_coverMax)
                {
                    probe.covered = true;
                    if (probe.above == 0.0f || up < probe.above)
                    {
                        probe.above = up;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            probe.valid = false;
            probe.covered = false;
        }
    }

    float g_cover = 0.0f;        // 0 = offen, 1 = ganz abgedeckt (geglättet)
    DWORD g_coverTick = 0;
    float g_coverPrev[3] = {};
    float g_coverVel[3] = {};    // m/s, geglättet
    bool g_coverHave = false;
    DWORD g_logTick = 0;
    float g_lastRain = 0.0f, g_lastSnow = 0.0f;

    void UpdateCover()
    {
        const DWORD now = timeGetTime();
        if (g_coverTick && now - g_coverTick < 30)
        {
            return;
        }
        const float dt = g_coverTick ? std::fmin((now - g_coverTick) / 1000.0f, 0.25f) : 1.0f;
        g_coverTick = now;
        CoverProbe probe;
        CoverProbe ahead;
        if (g_coverOn && ReadCamera(probe.pos))
        {
            // Kamera-Tempo für den Blick nach vorn (Kamerawechsel > 120 m/s übergehen)
            if (g_coverHave && dt > 0.0f)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const float v = (probe.pos[i] - g_coverPrev[i]) / dt;
                    g_coverVel[i] = std::fabs(v) > 120.0f ? g_coverVel[i] : g_coverVel[i] + (v - g_coverVel[i]) * std::fmin(1.0f, dt * 4.0f);
                }
            }
            std::memcpy(g_coverPrev, probe.pos, 12);
            g_coverHave = true;
            ProbeCover(probe);
            // Einfahrt: schon abdecken, wenn die Kamera in CoverAhead Sekunden darunter ist. Ausfahrt: nur die Kamera.
            if (probe.valid && !probe.covered && g_coverAhead > 0.0f)
            {
                for (int i = 0; i < 3; ++i)
                {
                    ahead.pos[i] = probe.pos[i] + g_coverVel[i] * g_coverAhead;
                }
                ahead.pos[1] = probe.pos[1];
                ProbeCover(ahead);
                probe.covered = ahead.valid && ahead.covered;
                probe.above = ahead.above;
            }
        }
        if (probe.valid || !g_coverOn)
        {
            const float target = probe.covered ? 1.0f : 0.0f;
            const float step = g_coverFade > 0.0f ? dt / g_coverFade : 1.0f;
            g_cover += std::fmax(-step, std::fmin(target - g_cover, step));
        }
        if (g_logOn && now - g_logTick >= 1000)
        {
            g_logTick = now;
            Log("Wetter: Modus %d, Regen %.2f, Schnee %.2f, Abdeckung %.2f (%s%s, %d Flaechen, darueber %.1f m), "
                "Kamera %.1f %.1f %.1f, %.0f km/h",
                g_mode, g_lastRain, g_lastSnow, g_cover, probe.valid ? (probe.covered ? "bedeckt" : "offen") : "-",
                ahead.covered ? " voraus" : "", probe.surfaces, probe.above, probe.pos[0], probe.pos[1], probe.pos[2],
                std::sqrt(g_coverVel[0] * g_coverVel[0] + g_coverVel[2] * g_coverVel[2]) * 3.6f);
        }
    }

    // ---- Regen und Schnee je Bild ----
    float SkyValue(void * sky, std::uintptr_t lerp)
    {
        using InterpFn = float (__thiscall *)(void * track, int section);
        using LerpFn = float (__thiscall *)(void * track, float weight);
        void * track = At<void *>(sky, 0x48);
        const float w = reinterpret_cast<InterpFn>(kSkyKeyInterp)(track, At<int>(sky, 0x50));
        return reinterpret_cast<LerpFn>(lerp)(track, w);
    }

    float __fastcall SkyRainHook(void * sky)
    {
        float v = SkyValue(sky, kSkyLerpRain);
        UpdateCover();
        if (g_mode == 1)
        {
            v = g_rainAmount;
        }
        else if (g_mode == 2)
        {
            v = 0.0f;
        }
        g_lastRain = v * (1.0f - g_cover);
        return g_lastRain;
    }

    float __fastcall SkySnowHook(void * sky)
    {
        float v = SkyValue(sky, kSkyLerpSnow);
        UpdateCover();
        if (g_mode == 1)
        {
            v = 0.0f;
        }
        else if (g_mode == 2)
        {
            v = g_snowAmount;
        }
        g_lastSnow = v * (1.0f - g_cover);
        return g_lastSnow;
    }

    // ---- Scheibentropfen (Tropfenobjekt, doc/re/13-VIDEO.md §8) ----
    // Je Tropfen i: aktiv +0x4200+i, Lage +0xC+i·0x30 (Spitze des Dreiecks), Richtung +0x445C+i·0xC (läuft dem
    // Ziel +0x4DC8+i·0xC mit 1/60 je Bild nach). Scheibe: Höhe +0x676C, „oben“ längs der Scheibe +0x6770,
    // Mitte unten +0x6794 (Ziel der Richtung im schnellen Zustand: von dort weg), Merker +0x66F9 (oben/unten getauscht).
    // Verlässt ein Tropfen das Feld +0x67A0…+0x67AC, setzt ihn das Spiel im nächsten Takt neu.
    float g_camPrev[3] = {};
    bool g_camHave = false;
    float g_speed = 0.0f; // m/s, geglättet

    void Normalize(float * v, const float * fallback)
    {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 1e-6f)
        {
            std::memcpy(v, fallback, 12);
            return;
        }
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }

    void MoveDrops(unsigned char * d, unsigned ticks)
    {
        const int count = At<int>(d, 4);
        if (count <= 0 || ticks == 0)
        {
            return;
        }
        const float dt = ticks / 64.0f; // 64 Takte je Sekunde (Tabelle 0x5CEDEC = 64/n)
        float cam[3] = {};
        if (ReadCamera(cam))
        {
            if (g_camHave)
            {
                const float dx = cam[0] - g_camPrev[0], dz = cam[2] - g_camPrev[2];
                float v = std::sqrt(dx * dx + dz * dz) / dt;
                if (v > 120.0f)
                {
                    v = g_speed; // Kamerawechsel
                }
                g_speed += (v - g_speed) * std::fmin(1.0f, dt * 3.0f);
            }
            std::memcpy(g_camPrev, cam, 12);
            g_camHave = true;
        }
        const float kmh = g_speed * 3.6f;
        const float air = g_windFull > g_windStart
                              ? std::fmax(0.0f, std::fmin((kmh - g_windStart) / (g_windFull - g_windStart), 1.0f))
                              : (kmh >= g_windStart ? 1.0f : 0.0f);
        const bool snow = At<unsigned char>(d, 0x66FA) != 0;
        const float height = At<float>(d, 0x676C);
        float up[3] = { At<float>(d, 0x6770), At<float>(d, 0x6774), At<float>(d, 0x6778) };
        static const float kY[3] = { 0.0f, 1.0f, 0.0f };
        Normalize(up, kY);
        const float g = At<unsigned char>(d, 0x66F9) ? 1.0f : -1.0f;
        const float * focus = &At<float>(d, 0x6794);
        for (int i = 0; i < count && i < 0xC9; ++i)
        {
            if (At<unsigned char>(d, 0x4200 + i) != 1)
            {
                continue;
            }
            float * p = &At<float>(d, 0xC + i * 0x30);
            // Fahrtwind: längs der Scheibe nach oben, seitlich der Mitte etwas nach außen (DropSpread)
            float side[3] = { p[0] - focus[0], p[1] - focus[1], p[2] - focus[2] };
            Normalize(side, up);
            const float along = side[0] * up[0] + side[1] * up[1] + side[2] * up[2];
            float wind[3] = { up[0] + (side[0] - along * up[0]) * g_dropSpread,
                              up[1] + (side[1] - along * up[1]) * g_dropSpread,
                              up[2] + (side[2] - along * up[2]) * g_dropSpread };
            Normalize(wind, up);
            // je Tropfen etwas anders schnell (0,6…1,4), damit sie nicht im Gleichschritt laufen
            const float k = 0.6f + 0.8f * ((static_cast<unsigned>(i) * 2654435761u >> 16) & 0xFFFF) / 65535.0f;
            // Rutschen (nur große Regentropfen, nur ohne Fahrtwind) und Fahrtwind getrennt, jeweils mit eigenem Tempo
            const float slide = (!snow && k > 1.1f) ? g_dropSlide * (1.0f - air) : 0.0f;
            const float blow = g_dropWind * air;
            float move[3] = {};
            for (int a = 0; a < 3; ++a)
            {
                move[a] = (g * up[a] * slide + wind[a] * blow) * height * k;
                p[a] += move[a] * dt;
            }
            // Ausrichtung folgt der Bewegung (das Spiel schaltet sonst erst über 80 km/h auf „vom Fahrtwind“ um)
            float dir[3] = { move[0], move[1], move[2] };
            const float gravity[3] = { g * up[0], g * up[1], g * up[2] };
            Normalize(dir, gravity);
            float * target = &At<float>(d, 0x4DC8 + i * 0xC);
            target[0] = dir[0];
            target[1] = dir[1];
            target[2] = dir[2];
        }
        using BuildFn = void (__fastcall *)(void * drops);
        reinterpret_cast<BuildFn>(kDropsBuild)(d);
    }

    void __fastcall DropsTickHook(unsigned char * d, void *, unsigned ticks, float rain, float snow)
    {
        using TickFn = void (__thiscall *)(void * drops, unsigned ticks, float rain, float snow);
        reinterpret_cast<TickFn>(kDropsTick)(d, ticks, rain, snow);
        __try
        {
            MoveDrops(d, ticks);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // ---- Menü: Accessor Track1.Weather ----
    // Wie Track1.Traffic in FEdaTrack__Initialize: FELocalizedTextAccessor mit nullterminierter Liste von Text-IDs,
    // aktueller Eintrag +0x18, Rückruf +0x10 (cdecl(accessor)) bei jeder Wahl (FEPositionableAccessor__SetCurrent).
    int g_weatherStrings[] = { static_cast<int>(kStrOriginal), static_cast<int>(kStrRain),
                               static_cast<int>(kStrSnow), 0 };
    void * g_acc = nullptr;
    bool g_menuReady = false;

    void __cdecl OnWeatherChanged(void * acc)
    {
        const int mode = At<int>(acc, 0x18);
        if (mode == g_mode || mode < 0 || mode > 2)
        {
            return;
        }
        g_mode = mode;
        char text[8] = {};
        std::snprintf(text, sizeof(text), "%d", mode);
        WritePrivateProfileStringA("Weather", "Mode", text, g_ini.c_str());
        Log("Wetter: Menue Modus %d", mode);
    }

    void __fastcall TrackInitHook(void * self)
    {
        using InitFn = void (__thiscall *)(void * self);
        reinterpret_cast<InitFn>(kTrackInit)(self);
        if (g_acc)
        {
            return;
        }
        using AllocFn = void * (__cdecl *)(unsigned size, unsigned pool);
        using CtorFn = void * (__thiscall *)(void * mem, const char * name, unsigned key, int * table,
                                            const char * group, void (__cdecl * changed)(void *), int extra);
        void * mem = reinterpret_cast<AllocFn>(kMemAllocIn)(0x2C, *reinterpret_cast<unsigned *>(kFePool));
        if (!mem)
        {
            return;
        }
        g_acc = reinterpret_cast<CtorFn>(kAccCtorCount)(mem, "Track1.Weather", 0, g_weatherStrings, nullptr,
                                                         &OnWeatherChanged, 0);
        At<int>(g_acc, 0x18) = g_mode;
    }

    void __fastcall TrackCleanupHook(void * self)
    {
        if (g_acc)
        {
            using DtorFn = void (__thiscall *)(void * self, int flags);
            reinterpret_cast<DtorFn>((*reinterpret_cast<std::uintptr_t **>(g_acc))[0])(g_acc, 1);
            g_acc = nullptr;
        }
        using CleanupFn = void (__thiscall *)(void * self);
        reinterpret_cast<CleanupFn>(kTrackCleanup)(self);
    }

    // ---- Texte zu den eigenen IDs ----
    using GetStringFn = const char * (__cdecl *)(unsigned id);
    GetStringFn g_origGetString = nullptr;

    const char * __cdecl GetStringHook(unsigned id)
    {
        if (id >= kStrLabel && id < kStrLabel + sizeof(kTexts) / sizeof(kTexts[0]))
        {
            const Text & t = kTexts[id - kStrLabel];
            return *reinterpret_cast<int *>(kLanguage) == 2 ? t.de : t.en;
        }
        return g_origGetString(id);
    }

    // ---- Eintrag in die Layouts einfügen ----
    // Jede .lay liegt nach dem Laden ganz im Speicher (FE_InitLayoutFileMap → File_GameLoadTagged, Block mit
    // Größe bei −0xC) und wird von dort geparst. LFSP hängt einen Abschnitt an, kopiert vom Menüpunkt darüber.
    using GameLoadFn = void * (__cdecl *)(const char * path, unsigned pool);
    GameLoadFn g_origGameLoad = nullptr;

    bool EndsWith(const char * path, const char * name)
    {
        const std::size_t n = std::strlen(path), m = std::strlen(name);
        return n > m && _stricmp(path + n - m, name) == 0 && (path[n - m - 1] == '\\' || path[n - m - 1] == '/');
    }

    std::string LineValue(const std::string & line, const char * key)
    {
        const std::size_t k = std::strlen(key);
        return line.size() > k && _strnicmp(line.c_str(), key, k) == 0 && line[k] == '=' ? line.substr(k + 1) : "";
    }

    bool BuildLayout(std::string & text, const char * templ, const std::string & pos)
    {
        if (text.find("accessor=Track1.Weather") != std::string::npos)
        {
            return false;
        }
        const std::string key = std::string("accessor=") + templ + "\r";
        const std::size_t at = text.find(key);
        const std::size_t begin = at == std::string::npos ? at : text.rfind("[section.", at);
        if (begin == std::string::npos)
        {
            return false;
        }
        std::size_t end = text.find("\n[", at);
        end = end == std::string::npos ? text.size() : end + 1;
        int last = -1;
        for (std::size_t s = text.find("[section."); s != std::string::npos; s = text.find("[section.", s + 1))
        {
            const int n = std::atoi(text.c_str() + s + 9);
            last = n > last ? n : last;
        }
        // Zeilen des Vorbilds übernehmen, eigene Werte einsetzen. Liegt der Eintrag auf der anderen Bildhälfte als
        // das Vorbild, werden Einflugrichtung (transon) und Verbindungslinie (stemleft/stemright) gespiegelt.
        const bool right = std::atoi(pos.c_str()) >= 320;
        const std::size_t trans = text.find("transon=", begin);
        const bool templRight = trans < end && _strnicmp(text.c_str() + trans + 8, "RIGHT", 5) == 0;
        const bool mirror = right != templRight;
        std::string section = "[section." + std::to_string(last + 1) + "]\r\n";
        std::size_t line = text.find('\n', begin) + 1;
        while (line < end)
        {
            std::size_t next = text.find('\n', line);
            next = next == std::string::npos || next >= end ? end : next + 1;
            std::string l = text.substr(line, next - line);
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n'))
            {
                l.pop_back();
            }
            line = next;
            if (l.empty())
            {
                continue;
            }
            if (!LineValue(l, "pos").empty())
            {
                l = "pos=" + pos;
            }
            else if (!LineValue(l, "accessor").empty())
            {
                l = "accessor=Track1.Weather";
            }
            else if (!LineValue(l, "stringID").empty())
            {
                l = "stringID=" + std::to_string(kStrLabel);
            }
            else if (!LineValue(l, "helptext").empty())
            {
                l = "helptext=" + std::to_string(kStrHelp);
            }
            else if (!LineValue(l, "name").empty())
            {
                l = "name=ID_COMBOBOX";
            }
            else if (mirror && _strnicmp(l.c_str(), "stemleftheight=", 15) == 0)
            {
                l = "stemrightheight=" + l.substr(15);
            }
            else if (mirror && _strnicmp(l.c_str(), "stemrightheight=", 16) == 0)
            {
                l = "stemleftheight=" + l.substr(16);
            }
            else if (mirror && _strnicmp(l.c_str(), "transon=", 8) == 0)
            {
                l = right ? "transon=RIGHT" : "transon=LEFT";
            }
            section += l + "\r\n";
        }
        if (!text.empty() && text.back() != '\n')
        {
            text += "\r\n";
        }
        text += "\r\n" + section;
        return true;
    }

    void * __cdecl GameLoadHook(const char * path, unsigned pool)
    {
        void * block = g_origGameLoad(path, pool);
        if (!block || !path || !g_menuReady)
        {
            return block;
        }
        const char * templ = nullptr;
        const std::string * pos = nullptr;
        if (EndsWith(path, "location.lay"))
        {
            templ = "Track1.Direction(SHORT)"; // gleiche Zeile auf der linken Seite
            pos = &g_posLocation;
        }
        else if (EndsWith(path, "QuickRace.lay"))
        {
            templ = "Game.NumOpponents";
            pos = &g_posQuick;
        }
        if (!templ || pos->empty())
        {
            return block;
        }
        const int size = At<int>(block, static_cast<std::size_t>(-0xC));
        std::string text(static_cast<const char *>(block), static_cast<std::size_t>(size > 0 ? size : 0));
        if (!BuildLayout(text, templ, *pos))
        {
            Log("Wetter: %s: Vorbild %s nicht gefunden oder Eintrag schon da", path, templ);
            return block;
        }
        using AllocFn = void * (__cdecl *)(const char * name, int size, unsigned pool);
        using FreeFn = void (__cdecl *)(void * block);
        void * fresh = reinterpret_cast<AllocFn>(kMemAllocTag)(path, static_cast<int>(text.size()), pool);
        if (!fresh)
        {
            return block;
        }
        std::memcpy(fresh, text.data(), text.size());
        reinterpret_cast<FreeFn>(kMemFree)(block);
        Log("Wetter: Menuepunkt in %s eingefuegt (%d -> %u Byte)", path, size, static_cast<unsigned>(text.size()));
        return fresh;
    }

    float ReadFloat(const char * ini, const char * key, const char * fallback, float lo, float hi)
    {
        char text[32] = {};
        GetPrivateProfileStringA("Weather", key, fallback, text, sizeof(text), ini);
        return std::fmax(lo, std::fmin(static_cast<float>(std::atof(text)), hi));
    }

    std::string ReadString(const char * ini, const char * key, const char * fallback)
    {
        char text[64] = {};
        GetPrivateProfileStringA("Weather", key, fallback, text, sizeof(text), ini);
        return text;
    }
}

void lfsp_weather::Install(const char * ini, LogFn log)
{
    Log = log;
    g_ini = ini;
    if (!GetPrivateProfileIntA("Weather", "Enabled", 1, ini))
    {
        Log("Wetter: aus");
        return;
    }
    g_mode = GetPrivateProfileIntA("Weather", "Mode", 0, ini);
    g_mode = g_mode < 0 || g_mode > 2 ? 0 : g_mode;
    g_menu = GetPrivateProfileIntA("Weather", "Menu", 1, ini);
    g_posLocation = ReadString(ini, "MenuPosLocation", "377,71");
    g_posQuick = ReadString(ini, "MenuPosQuickRace", "");
    g_rainAmount = ReadFloat(ini, "RainAmount", "0.8", 0.0f, 1.0f);
    g_snowAmount = ReadFloat(ini, "SnowAmount", "0.8", 0.0f, 1.0f);
    g_coverOn = GetPrivateProfileIntA("Weather", "Cover", 1, ini);
    g_coverFade = ReadFloat(ini, "CoverFade", "0.7", 0.0f, 10.0f);
    g_coverMin = ReadFloat(ini, "CoverMin", "0.3", 0.0f, 20.0f);
    g_coverMax = ReadFloat(ini, "CoverMax", "60", 1.0f, 500.0f);
    g_coverAhead = ReadFloat(ini, "CoverAhead", "0.8", 0.0f, 5.0f);
    g_dropsOn = GetPrivateProfileIntA("Weather", "Drops", 1, ini);
    g_dropWind = ReadFloat(ini, "DropWind", "0.5", -5.0f, 5.0f);
    g_dropSlide = ReadFloat(ini, "DropSlide", "0.04", 0.0f, 2.0f);
    g_dropSpread = ReadFloat(ini, "DropSpread", "0.25", 0.0f, 2.0f);
    g_windStart = ReadFloat(ini, "DropWindStart", "30", 0.0f, 400.0f);
    g_windFull = ReadFloat(ini, "DropWindFull", "150", 0.0f, 400.0f);
    g_logOn = GetPrivateProfileIntA("Weather", "Log", 0, ini);
    Log("Wetter: Modus %d, Menue %d, Regen %.2f, Schnee %.2f, Abdeckung %d (%.1f s), Tropfen %d (Wind %.2f, %.0f-%.0f km/h, "
        "Rutschen %.2f)",
        g_mode, g_menu, g_rainAmount, g_snowAmount, g_coverOn, g_coverFade, g_dropsOn, g_dropWind, g_windStart,
        g_windFull, g_dropSlide);

    static const unsigned char kSkyBytes[] = { 0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x50 };
    PatchJump("Regen (Sky_InterpLerp18)", kSkyRain, kSkyBytes, sizeof(kSkyBytes), reinterpret_cast<void *>(&SkyRainHook));
    PatchJump("Schnee (Sky_InterpLerp20)", kSkySnow, kSkyBytes, sizeof(kSkyBytes), reinterpret_cast<void *>(&SkySnowHook));
    if (g_dropsOn)
    {
        PatchCall("Scheibentropfen", kDropsTickCall, kDropsTick, reinterpret_cast<void *>(&DropsTickHook));
    }
    if (g_menu)
    {
        static const unsigned char kGetStringBytes[] = { 0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x57 };
        static const unsigned char kGameLoadBytes[] = { 0xA0, 0x70, 0xF3, 0x6A, 0x00 };
        const bool init = PatchSlot("FEdaTrack Initialize", kTrackVtable, kTrackInit,
                                    reinterpret_cast<void *>(&TrackInitHook));
        const bool cleanup = init && PatchSlot("FEdaTrack Cleanup", kTrackVtable + 8, kTrackCleanup,
                                               reinterpret_cast<void *>(&TrackCleanupHook));
        const bool texts = cleanup && InstallHook("FE-Texte", kGetString, kGetStringBytes, sizeof(kGetStringBytes),
                                                  reinterpret_cast<void *>(&GetStringHook),
                                                  reinterpret_cast<void **>(&g_origGetString));
        g_menuReady = texts && InstallHook("Layout laden", kGameLoad, kGameLoadBytes, sizeof(kGameLoadBytes),
                                           reinterpret_cast<void *>(&GameLoadHook),
                                           reinterpret_cast<void **>(&g_origGameLoad));
        if (cleanup && !g_menuReady)
        {
            Log("Wetter: Menuepunkt fehlt (Hook fehlgeschlagen), Accessor wird trotzdem angelegt");
        }
    }
}
