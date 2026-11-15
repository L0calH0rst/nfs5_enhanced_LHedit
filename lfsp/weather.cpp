// weather.cpp – LFSP: Wetter im Rennen ([Weather] in lfsp.ini, doc/re/13-VIDEO.md §8, FORMATE.md §4.12).
// 1. Menüpunkt „Wetter“ (Original/Regen/Schnee) in der Streckenwahl: eigener Accessor Track1.Weather im
//    Datenbereich FEdaTrack, eigene Text-IDs, Eintrag wird beim Laden in location.lay/QuickRace.lay eingefügt.
// 2. Regen- und Schneestärke: Sky_InterpLerp18/20 (Wetterspur der Sky/<Strecke>.bin) ersetzt; Wahl aus dem Menü
//    und Abdeckung (Brücke, Tunnel: Fläche des Kollisionsbaums über der Kamera) gehen ein.
// 3. Scheibentropfen: nach R_WindshieldDropsTick wandern die Tropfen mit dem Fahrtwind über die Scheibe.
// 4. Sturm und Gewitter (Menü): Regen voll, Himmel und Welt dunkler und kühler, mehr Nebel, stärkerer Fahrtwind; beim
//    Gewitter Blitze (Helligkeit in dx11z über LfspWeatherFx) und Donner aus dem Spiel (Snd_QueueThunder).
// 5. Schnee-Weiß: bei Schnee mischt dx11z die Streckenflächen Richtung Weiß (Werte ebenfalls über LfspWeatherFx).
// 6. Haftung bei Nässe und Schnee (Grip…, ab Werk aus): das Spiel kennt keine, Faktor nach Sim_UpdateWheelGrip auf die
//    Haftgrenze aller vier Räder; KI-Soll-Tempo (AI_PlanRaceSpeed) × √Faktor, Verkehr über die stillgelegten
//    Wetter-Schalter des Spiels (WEATHER ×0,9 in AI_GetMaxCornerSpeed, „aiwther“ in AI_SetCruiseSpeed).
// 7. Schnee und Nässe auf dem Auto (CarSnow…, CarDrops…): Anteil über die Zeit, an dx11z über LfspWeatherCar.

#include <windows.h>
#include <mmsystem.h>

#include <cctype>
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
    bool NightGradeActive(float * rgb);
    bool NightGradeEx(float * out9);
    bool NightSynthetic();
    bool WeatherFx(float * out16);
    bool WeatherCar(float * out8);
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
    constexpr std::uintptr_t kWheelGrip     = 0x4A1900; // Sim_UpdateWheelGrip, cdecl(auto): Haftgrenze +0xAC je Rad
    constexpr std::uintptr_t kPlanRaceSpeed = 0x40B650; // AI_PlanRaceSpeed, cdecl(auto): Soll-Tempo +0xEB8 der Gegner
    constexpr std::uintptr_t kWeatherVar    = 0x657A24; // WEATHER: > 0 → Kurventempo der einfachen KI ×0,9 (Sim_RaceInit setzt 0)
    constexpr std::uintptr_t kAiWeather     = 0x5CC358; // „aiwther“: Faktor auf das Reisetempo der Polizei (AI_LoadTuning setzt 1)
    constexpr std::uintptr_t kQueueThunder  = 0x4A8340; // Snd_QueueThunder, cdecl(abzug, panorama): Probe 0x3DF + Zufall 0…8
                                                        // (thunder_991…999, track.viv), Wartezeit 0x87 − abzug Takte (64 je s)

    // Eigene Text-IDs (die .loc-Tabellen kennen nur 16 Bit, das Spiel nutzt bis etwa 23000)
    constexpr unsigned kStrLabel    = 64000;
    constexpr unsigned kStrOriginal = 64001;
    constexpr unsigned kStrRain     = 64002;
    constexpr unsigned kStrSnow     = 64003;
    constexpr unsigned kStrHelp     = 64004;
    constexpr unsigned kStrRandom   = 64005;
    constexpr unsigned kStrTimeLabel = 64006;
    constexpr unsigned kStrDay      = 64007;
    constexpr unsigned kStrNight    = 64008;
    constexpr unsigned kStrTimeHelp = 64009;
    constexpr unsigned kStrStorm    = 64010;
    constexpr unsigned kStrThunder  = 64011;
    constexpr std::uintptr_t kSelectSpec = 0x475D70; // Trk_SelectTrackSpec, cdecl(weather, time), aus R_InitRace
    constexpr std::uintptr_t kTimeVar    = 0x657A30; // TIME (Nachtflag), wird in Trk_DefaultTrackSpec ins Spec kopiert
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
        { "Choose the weather: as on the track, rain, snow, storm, thunderstorm or random.",
          "Wetter w\xE4hlen: wie auf der Strecke, Regen, Schnee, Sturm, Gewitter oder Zufall." }, // Texte der FE in Windows-1252
        { "random", "Zufall" },
        { "time of day", "Tageszeit" },
        { "day", "Tag" },
        { "dusk/dawn", "D\xE4mmerung" },
        { "Choose the time of day: as on the track, day, dusk/dawn or random.",
          "Tageszeit w\xE4hlen: wie auf der Strecke, Tag, D\xE4mmerung oder Zufall." },
        { "storm", "Sturm" },
        { "thunderstorm", "Gewitter" },
    };

    // ---- Einstellungen ----
    int g_mode = 0;              // Menüwahl: 0 Original, 1 Regen, 2 Schnee, 3 Zufall, 4 Sturm, 5 Gewitter
    int g_effMode = 0;           // gilt im Rennen: 0, 1, 2, 4, 5 (bei Zufall je Rennstart gewürfelt)
    // Sturm und Gewitter ([Weather] Storm…, Lightning…, Thunder…)
    bool g_stormOn = true;         // Sturm und Gewitter im Menü
    bool g_randomStorm = true;     // Zufall würfelt auch Sturm und Gewitter
    float g_stormRain = 1.0f;
    float g_stormWorld[3] = { 0.74f, 0.77f, 0.82f };
    float g_stormSky[3] = { 0.62f, 0.66f, 0.74f };
    float g_stormFog = 2.0f;       // Exponent auf die Sicht durch den Nebel (1 = wie das Spiel)
    float g_stormDropWind = 1.8f;  // Faktor auf DropWind
    float g_lightMin = 6.0f;       // Sekunden zwischen Blitzen
    float g_lightMax = 20.0f;
    float g_lightWorld = 1.6f;     // Welt hell × (1 + Wert · Blitz)
    float g_lightSky = 2.5f;
    bool g_thunderOn = true;
    float g_thunderMin = 0.4f;     // Sekunden vom Blitz bis zum Donner
    float g_thunderMax = 2.8f;
    // Schnee-Weiß ([Weather] SnowWhite…)
    float g_snowWhite = 0.25f;     // 0 = aus
    float g_snowDesat = 0.3f;
    float g_snowSlope = 0.0f;      // 0 = gleichmäßig, 1 = nur nach oben zeigende Flächen
    float g_snowColour[3] = { 0.94f, 0.96f, 1.0f };
    int g_snowDebug = 0;
    float g_rawSnow = 0.0f;        // Schnee der Strecke (Wetterspur), ohne Menü und Abdeckung
    float g_rawRain = 0.0f;        // Regen der Strecke (Wetterspur), ohne Menü und Abdeckung
    // Haftung ([Weather] Grip…)
    bool g_gripOn = false;
    float g_gripRain = 0.85f;
    float g_gripSnow = 0.7f;
    float g_gripStorm = 0.8f;
    bool g_gripAi = true;
    float g_gripNow = 1.0f;        // gilt im laufenden Rennen
    // Schnee und Nässe auf dem Auto ([Weather] CarSnow…, CarDrops…)
    float g_carSnowMax = 0.5f;     // 0 = aus
    float g_carSnowTime = 180.0f;  // Sekunden bis zum Höchstwert
    float g_carSnowMelt = 240.0f;  // Sekunden vom Höchstwert bis 0 (bei Regen doppelt so schnell)
    float g_carDrops = 0.6f;       // Dichte 0…1, 0 = aus
    float g_carWetTime = 15.0f;    // Sekunden bis ganz nass
    float g_carDryTime = 90.0f;    // Sekunden bis trocken
    float g_carSnow = 0.0f;
    float g_carWet = 0.0f;
    DWORD g_carTick = 0;
    int g_nightForce = -1;         // -1 nichts, 0 Tag, 1 Nacht (Wahl dieses Rennens)
    float g_skyDarken = 0.6f;      // 0…1: Anteil des Welt-Faktors, der auf den Himmel wirkt
    float g_lightBoost = 1.6f;     // Faktor auf Leuchtsprites (additive Draws) bei erzwungener Nacht
    bool g_nightGrade = true;      // Welt abdunkeln, wenn „Nacht“ auf einer Tagstrecke gewählt ist
    bool g_nightActive = false;    // Abdunkeln gilt für das laufende Rennen
    bool g_nightSynthetic = false; // „Nacht“ auf einer Strecke ohne eigene Nacht: dort gibt es keine Nachtschichten im Spiel
    bool g_flagLogPending = false;
    int g_timeMode = 0;          // Menüwahl: 0 Original, 1 Tag, 2 Nacht, 3 Zufall
    int g_menu = 1;
    std::string g_posLocation = "377,71"; // unter „records…“
    std::string g_posQuick;      // leer = kein Eintrag im Schnellen Rennen
    std::string g_posTimeLocation = "428,95"; // unter „Wetter“
    std::string g_posTimeQuick;
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
                g_effMode, g_lastRain, g_lastSnow, g_cover, probe.valid ? (probe.covered ? "bedeckt" : "offen") : "-",
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

    bool StormMode(int mode)
    {
        return mode == 4 || mode == 5;
    }

    void UpdateGrip(); // Haftung, unten

    float __fastcall SkyRainHook(void * sky)
    {
        float v = SkyValue(sky, kSkyLerpRain);
        g_rawRain = v;
        UpdateCover();
        UpdateGrip();
        if (g_flagLogPending)
        {
            g_flagLogPending = false;
            Log("Wetter: Nachtflag im Rennen %u, Nacht gewaehlt %d, synthetisch %d, Abdunkeln %d",
                static_cast<unsigned>(*reinterpret_cast<const unsigned short *>(0x628A28)), g_nightForce, g_nightSynthetic ? 1 : 0,
                g_nightActive ? 1 : 0);
        }
        if (g_effMode == 1)
        {
            v = g_rainAmount;
        }
        else if (StormMode(g_effMode))
        {
            v = g_stormRain;
        }
        else if (g_effMode == 2)
        {
            v = 0.0f;
        }
        g_lastRain = v * (1.0f - g_cover);
        return g_lastRain;
    }

    float __fastcall SkySnowHook(void * sky)
    {
        float v = SkyValue(sky, kSkyLerpSnow);
        g_rawSnow = v;
        UpdateCover();
        if (g_effMode == 1 || StormMode(g_effMode))
        {
            v = 0.0f;
        }
        else if (g_effMode == 2)
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
            const float blow = g_dropWind * air * (StormMode(g_effMode) ? g_stormDropWind : 1.0f);
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

    // ---- Haftung bei Nässe und Schnee ----
    // Faktor des laufenden Rennens aus der Wetterwahl; „Original“ nach der Wetterspur der Strecke (Regen/Schnee ≥ 0,2).
    float GripFactor()
    {
        if (!g_gripOn)
        {
            return 1.0f;
        }
        if (StormMode(g_effMode))
        {
            return g_gripStorm;
        }
        if (g_effMode == 1)
        {
            return g_gripRain;
        }
        if (g_effMode == 2)
        {
            return g_gripSnow;
        }
        if (g_rawSnow >= 0.2f)
        {
            return g_gripSnow;
        }
        if (g_rawRain >= 0.2f)
        {
            return g_gripRain;
        }
        return 1.0f;
    }

    using CarFn = void (__cdecl *)(void * car);
    CarFn g_origWheelGrip = nullptr;
    CarFn g_origPlanSpeed = nullptr;

    void __cdecl WheelGripHook(void * car)
    {
        g_origWheelGrip(car);
        if (g_gripNow < 1.0f)
        {
            static const std::size_t kLimit[4] = { 0x864, 0x928, 0x9EC, 0xAB0 }; // Rad-Blöcke +0x7B8 + i·0xC4, Haftgrenze +0xAC
            for (std::size_t offset : kLimit)
            {
                At<float>(car, offset) *= g_gripNow;
            }
        }
    }

    void __cdecl PlanSpeedHook(void * car)
    {
        g_origPlanSpeed(car);
        if (g_gripNow < 1.0f && g_gripAi)
        {
            // Kurventempo ∝ √Haftung; auf Geraden ist das Soll-Tempo ohnehin über dem Höchsttempo
            At<float>(car, 0xEB8) *= std::sqrt(g_gripNow);
        }
    }

    // Je Bild im Rennen (SkyRainHook): Faktor und die Wetter-Schalter der einfachen KI
    void UpdateGrip()
    {
        g_gripNow = GripFactor();
        if (g_gripOn && g_gripAi)
        {
            const bool wet = g_gripNow < 1.0f;
            *reinterpret_cast<int *>(kWeatherVar) = wet ? 1 : 0;
            *reinterpret_cast<float *>(kAiWeather) = wet ? std::sqrt(g_gripNow) : 1.0f;
        }
    }

    // ---- Menü: Accessoren Track1.Weather und Track1.TimeOfDay ----
    // Wie Track1.Traffic in FEdaTrack__Initialize: FELocalizedTextAccessor mit nullterminierter Liste von Text-IDs,
    // aktueller Eintrag +0x18, Rückruf +0x10 (cdecl(accessor)) bei jeder Wahl (FEPositionableAccessor__SetCurrent).
    // Liste des Menüs (Text-IDs) und je Eintrag der Wert für [Weather] Mode; „Zufall“ bleibt der letzte Eintrag,
    // der Wert 3 bleibt Zufall wie vor Sturm und Gewitter. Ohne [Weather] Storm fehlen die beiden Einträge.
    int g_weatherStrings[8] = { static_cast<int>(kStrOriginal), static_cast<int>(kStrRain), static_cast<int>(kStrSnow),
                                static_cast<int>(kStrStorm), static_cast<int>(kStrThunder), static_cast<int>(kStrRandom), 0 };
    int g_weatherModes[8] = { 0, 1, 2, 4, 5, 3 };
    int g_weatherCount = 6;

    void BuildWeatherList()
    {
        if (g_stormOn)
        {
            return;
        }
        const int strings[] = { static_cast<int>(kStrOriginal), static_cast<int>(kStrRain), static_cast<int>(kStrSnow),
                                static_cast<int>(kStrRandom), 0 };
        const int modes[] = { 0, 1, 2, 3 };
        std::memcpy(g_weatherStrings, strings, sizeof(strings));
        std::memcpy(g_weatherModes, modes, sizeof(modes));
        g_weatherCount = 4;
    }

    int WeatherIndex(int mode)
    {
        for (int i = 0; i < g_weatherCount; ++i)
        {
            if (g_weatherModes[i] == mode)
            {
                return i;
            }
        }
        return 0;
    }
    int g_timeStrings[] = { static_cast<int>(kStrOriginal), static_cast<int>(kStrDay),
                            static_cast<int>(kStrNight), static_cast<int>(kStrRandom), 0 };
    void * g_acc = nullptr;
    void * g_accTime = nullptr;
    bool g_menuReady = false;

    // Blitze beim Gewitter (LfspWeatherFx, einmal je Bild aus dx11z): bis zu 3 Lichtstöße je Blitz, abklingend.
    DWORD g_flashNext = 0;   // timeGetTime des nächsten Blitzes, 0 = noch nicht geplant
    DWORD g_flashStart = 0;
    struct Pulse
    {
        float at;   // Sekunden nach dem Beginn
        float amp;
    };
    Pulse g_pulses[3] = {};
    int g_pulseCount = 0;

    // Wahl 3 = Zufall: je Rennstart neu würfeln (SelectSpecHook). Sonst gilt die Wahl selbst.
    unsigned g_rng = 0;
    unsigned Roll(unsigned n)
    {
        if (!g_rng)
        {
            g_rng = timeGetTime() | 1u;
        }
        g_rng = g_rng * 1664525u + 1013904223u;
        return (g_rng >> 16) % n;
    }

    void __cdecl OnWeatherChanged(void * acc)
    {
        const int index = At<int>(acc, 0x18);
        if (index < 0 || index >= g_weatherCount || g_weatherModes[index] == g_mode)
        {
            return;
        }
        const int mode = g_weatherModes[index];
        g_mode = mode;
        g_effMode = mode == 3 ? 0 : mode;
        char text[8] = {};
        std::snprintf(text, sizeof(text), "%d", mode);
        WritePrivateProfileStringA("Weather", "Mode", text, g_ini.c_str());
        Log("Wetter: Menue Modus %d", mode);
    }

    void __cdecl OnTimeChanged(void * acc)
    {
        const int mode = At<int>(acc, 0x18);
        if (mode == g_timeMode || mode < 0 || mode > 3)
        {
            return;
        }
        g_timeMode = mode;
        char text[8] = {};
        std::snprintf(text, sizeof(text), "%d", mode);
        WritePrivateProfileStringA("Weather", "TimeMode", text, g_ini.c_str());
        Log("Wetter: Menue Tageszeit %d", mode);
    }

    // Trk_SelectTrackSpec(weather, time) läuft einmal je Rennstart (R_InitRace) vor Sky_Load/R_WeatherCreate.
    // Die Strecken-Spec-Datei fehlt im Spiel, Trk_DefaultTrackSpec übernimmt TIME aus 0x657A30 (Nachtflag).
    using SelectSpecFn = void (__cdecl *)(int weather, int time);
    SelectSpecFn g_origSelectSpec = nullptr;

    // Trk_BWorldInit löscht das Nachtflag (Trk_ClearNightFlag 0x4B68E7: mov byte [0x628A28],0), danach setzen
    // Streckenschichten 0x8002/0x8003/0x8011 es wieder. Bei „Nacht“ bleibt es nach dem Löschen an.
    float g_nightMul[3] = { 0.22f, 0.27f, 0.44f };
    std::string g_nightTracks = "monaco3"; // Strecken, die das Spiel selbst bei Nacht zeigt (kein Abdunkeln)
    constexpr std::uintptr_t kTrackName = 0x657444; // Game_gTrackName
    constexpr std::uintptr_t kFrontEnd = 0x65B928;  // != 0 im Frontend

    bool NativeNight()
    {
        const char * name = reinterpret_cast<const char *>(kTrackName);
        std::string lower;
        for (int i = 0; i < 32 && name[i]; ++i)
        {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
        }
        std::string list = "," + g_nightTracks + ",";
        for (char & c : list)
        {
            c = c == ' ' ? ',' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return !lower.empty() && list.find("," + lower + ",") != std::string::npos;
    }
    void __cdecl ClearNightHook()
    {
        *reinterpret_cast<unsigned char *>(0x628A28) = g_nightForce == 1 ? 1 : 0;
    }

    void __cdecl SelectSpecHook(int weather, int time)
    {
        g_nightForce = -1;
        g_nightActive = false;
        g_nightSynthetic = false;
        if (g_mode == 3)
        {
            static const int kAll[] = { 0, 1, 2, 4, 5 };
            g_effMode = kAll[Roll(g_stormOn && g_randomStorm ? 5u : 3u)];
            Log("Wetter: Zufall -> Modus %d", g_effMode);
        }
        g_flashNext = 0; // erster Blitz erst einige Sekunden nach dem Start
        g_carSnow = 0.0f; // jedes Rennen mit sauberem, trockenem Auto
        g_carWet = 0.0f;
        g_carTick = 0;
        g_gripNow = 1.0f;
        if (g_gripOn)
        {
            *reinterpret_cast<int *>(kWeatherVar) = 0; // nicht ins nächste Rennen (Regenton, Streckendatensatz) tragen
        }
        if (g_timeMode != 0)
        {
            const int night = g_timeMode == 3 ? static_cast<int>(Roll(2)) : g_timeMode - 1;
            Log("Wetter: Tageszeit %s (Wahl %d, Spiel %d)", night ? "Nacht" : "Tag", g_timeMode, time);
            g_nightForce = night;
            g_nightSynthetic = night == 1 && !NativeNight();
            g_nightActive = g_nightGrade && g_nightSynthetic;
            g_flagLogPending = true;
            Log("Wetter: Abdunkeln %s (Strecke '%.16s')", g_nightActive ? "an" : "aus (dx11z)", reinterpret_cast<const char *>(kTrackName));
            time = night;
            *reinterpret_cast<int *>(kTimeVar) = night;
        }
        g_origSelectSpec(weather, time);
    }

    void * MakeAccessor(const char * name, int * strings, void (__cdecl * changed)(void *), int value)
    {
        using AllocFn = void * (__cdecl *)(unsigned size, unsigned pool);
        using CtorFn = void * (__thiscall *)(void * mem, const char * name, unsigned key, int * table,
                                            const char * group, void (__cdecl * changed)(void *), int extra);
        void * mem = reinterpret_cast<AllocFn>(kMemAllocIn)(0x2C, *reinterpret_cast<unsigned *>(kFePool));
        if (!mem)
        {
            return nullptr;
        }
        void * acc = reinterpret_cast<CtorFn>(kAccCtorCount)(mem, name, 0, strings, nullptr, changed, 0);
        if (acc)
        {
            At<int>(acc, 0x18) = value;
        }
        return acc;
    }

    void __fastcall TrackInitHook(void * self)
    {
        using InitFn = void (__thiscall *)(void * self);
        reinterpret_cast<InitFn>(kTrackInit)(self);
        if (g_acc)
        {
            return;
        }
        g_acc = MakeAccessor("Track1.Weather", g_weatherStrings, &OnWeatherChanged, WeatherIndex(g_mode));
        g_accTime = MakeAccessor("Track1.TimeOfDay", g_timeStrings, &OnTimeChanged, g_timeMode);
    }

    void DestroyAccessor(void *& acc)
    {
        if (acc)
        {
            using DtorFn = void (__thiscall *)(void * self, int flags);
            reinterpret_cast<DtorFn>((*reinterpret_cast<std::uintptr_t **>(acc))[0])(acc, 1);
            acc = nullptr;
        }
    }

    void __fastcall TrackCleanupHook(void * self)
    {
        DestroyAccessor(g_acc);
        DestroyAccessor(g_accTime);
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

    bool BuildLayout(std::string & text, const char * templ, const std::string & pos, const char * accessor,
                     unsigned labelId, unsigned helpId)
    {
        if (text.find(std::string("accessor=") + accessor) != std::string::npos)
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
                l = std::string("accessor=") + accessor;
            }
            else if (!LineValue(l, "stringID").empty())
            {
                l = "stringID=" + std::to_string(labelId);
            }
            else if (!LineValue(l, "helptext").empty())
            {
                l = "helptext=" + std::to_string(helpId);
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
        const char * templTime = nullptr;
        const std::string * pos = nullptr;
        const std::string * posTime = nullptr;
        if (EndsWith(path, "location.lay"))
        {
            // Treppe: je Zeile weiter unten um 24 nach außen (links x -51, y +24: Track 152,47 · Direction 101,71 · Traffic 50,95 ·
            // Laps 9,119). Rechts records 326,47 · Wetter 377,71 · Tageszeit 428,95; die Verbindungslinie (stemleftheight) hat
            // in der Zeile die Länge des linken Eintrags: Direction 17, Traffic 13.
            templ = "Track1.Direction(SHORT)"; // gleiche Zeile auf der linken Seite
            templTime = "Track1.Traffic";
            pos = &g_posLocation;
            posTime = &g_posTimeLocation;
        }
        else if (EndsWith(path, "QuickRace.lay"))
        {
            templ = "Game.NumOpponents";
            templTime = templ;
            pos = &g_posQuick;
            posTime = &g_posTimeQuick;
        }
        if (!templ || (pos->empty() && posTime->empty()))
        {
            return block;
        }
        const int size = At<int>(block, static_cast<std::size_t>(-0xC));
        std::string text(static_cast<const char *>(block), static_cast<std::size_t>(size > 0 ? size : 0));
        bool changed = false;
        if (!pos->empty())
        {
            changed = BuildLayout(text, templ, *pos, "Track1.Weather", kStrLabel, kStrHelp) || changed;
        }
        if (!posTime->empty())
        {
            changed = BuildLayout(text, templTime, *posTime, "Track1.TimeOfDay", kStrTimeLabel, kStrTimeHelp) || changed;
        }
        if (!changed)
        {
            Log("Wetter: %s: Vorbild %s nicht gefunden oder Eintraege schon da", path, templ);
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

    void ReadColour(const char * ini, const char * key, const char * fallback, float * rgb)
    {
        char text[32] = {};
        GetPrivateProfileStringA("Weather", key, fallback, text, sizeof(text), ini);
        const unsigned long colour = std::strtoul(text, nullptr, 16);
        rgb[0] = ((colour >> 16) & 0xFF) / 255.0f;
        rgb[1] = ((colour >> 8) & 0xFF) / 255.0f;
        rgb[2] = (colour & 0xFF) / 255.0f;
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
    g_stormOn = GetPrivateProfileIntA("Weather", "Storm", 1, ini) != 0;
    g_mode = GetPrivateProfileIntA("Weather", "Mode", 0, ini);
    g_mode = g_mode < 0 || g_mode > (g_stormOn ? 5 : 3) ? 0 : g_mode;
    BuildWeatherList();
    g_randomStorm = GetPrivateProfileIntA("Weather", "RandomStorm", 1, ini) != 0;
    g_stormRain = ReadFloat(ini, "StormRain", "1.0", 0.0f, 1.0f);
    ReadColour(ini, "StormLight", "BDC4D1", g_stormWorld);
    ReadColour(ini, "StormSky", "9EA8BD", g_stormSky);
    g_stormFog = ReadFloat(ini, "StormFog", "2.0", 1.0f, 8.0f);
    g_stormDropWind = ReadFloat(ini, "StormDropWind", "1.8", 0.0f, 5.0f);
    g_lightMin = ReadFloat(ini, "LightningMin", "6", 1.0f, 300.0f);
    g_lightMax = std::fmax(g_lightMin, ReadFloat(ini, "LightningMax", "20", 1.0f, 600.0f));
    g_lightWorld = ReadFloat(ini, "LightningWorld", "1.6", 0.0f, 10.0f);
    g_lightSky = ReadFloat(ini, "LightningSky", "2.5", 0.0f, 10.0f);
    g_thunderOn = GetPrivateProfileIntA("Weather", "Thunder", 1, ini) != 0;
    g_thunderMin = ReadFloat(ini, "ThunderDelayMin", "0.4", 0.0f, 10.0f);
    g_thunderMax = std::fmax(g_thunderMin, ReadFloat(ini, "ThunderDelayMax", "2.8", 0.0f, 10.0f));
    g_snowWhite = ReadFloat(ini, "SnowWhite", "0.25", 0.0f, 1.0f);
    g_snowDesat = ReadFloat(ini, "SnowDesaturate", "0.3", 0.0f, 1.0f);
    g_snowSlope = ReadFloat(ini, "SnowSlope", "0", 0.0f, 1.0f);
    ReadColour(ini, "SnowColour", "F0F5FF", g_snowColour);
    g_snowDebug = GetPrivateProfileIntA("Weather", "SnowDebug", 0, ini);
    g_gripOn = GetPrivateProfileIntA("Weather", "Grip", 0, ini) != 0;
    g_gripRain = ReadFloat(ini, "GripRain", "0.85", 0.3f, 1.0f);
    g_gripSnow = ReadFloat(ini, "GripSnow", "0.7", 0.3f, 1.0f);
    g_gripStorm = ReadFloat(ini, "GripStorm", "0.8", 0.3f, 1.0f);
    g_gripAi = GetPrivateProfileIntA("Weather", "GripAi", 1, ini) != 0;
    g_carSnowMax = ReadFloat(ini, "CarSnow", "0.5", 0.0f, 1.0f);
    g_carSnowTime = ReadFloat(ini, "CarSnowTime", "180", 1.0f, 3600.0f);
    g_carSnowMelt = ReadFloat(ini, "CarSnowMelt", "240", 1.0f, 3600.0f);
    g_carDrops = ReadFloat(ini, "CarDrops", "0.6", 0.0f, 1.0f);
    g_carWetTime = ReadFloat(ini, "CarWetTime", "15", 0.5f, 600.0f);
    g_carDryTime = ReadFloat(ini, "CarDryTime", "90", 0.5f, 3600.0f);
    g_effMode = g_mode == 3 ? 0 : g_mode;
    g_nightGrade = GetPrivateProfileIntA("Weather", "NightGrade", 1, ini) != 0;
    {
        const unsigned long colour = std::strtoul(ReadString(ini, "NightLight", "38466F").c_str(), nullptr, 16);
        g_nightMul[0] = ((colour >> 16) & 0xFF) / 255.0f;
        g_nightMul[1] = ((colour >> 8) & 0xFF) / 255.0f;
        g_nightMul[2] = (colour & 0xFF) / 255.0f;
        g_nightTracks = ReadString(ini, "NightTracks", "monaco3");
        g_skyDarken = ReadFloat(ini, "SkyDarken", "0.6", 0.0f, 1.0f);
        g_lightBoost = ReadFloat(ini, "NightLightBoost", "1.6", 0.5f, 8.0f);
    }
    g_timeMode = GetPrivateProfileIntA("Weather", "TimeMode", 0, ini);
    g_timeMode = g_timeMode < 0 || g_timeMode > 3 ? 0 : g_timeMode;
    g_menu = GetPrivateProfileIntA("Weather", "Menu", 1, ini);
    g_posLocation = ReadString(ini, "MenuPosLocation", "377,71");
    g_posQuick = ReadString(ini, "MenuPosQuickRace", "");
    g_posTimeLocation = ReadString(ini, "MenuPosTimeLocation", "428,95");
    g_posTimeQuick = ReadString(ini, "MenuPosTimeQuickRace", "");
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
    Log("Wetter: Modus %d, Tageszeit %d, Menue %d, Regen %.2f, Schnee %.2f, Abdeckung %d (%.1f s), Tropfen %d (Wind %.2f, %.0f-%.0f km/h, "
        "Rutschen %.2f)",
        g_mode, g_timeMode, g_menu, g_rainAmount, g_snowAmount, g_coverOn, g_coverFade, g_dropsOn, g_dropWind, g_windStart,
        g_windFull, g_dropSlide);
    Log("Wetter: Sturm %d (Regen %.2f, Licht %.2f %.2f %.2f, Himmel %.2f %.2f %.2f, Nebel %.2f, Fahrtwind x%.2f), Blitze %.0f-%.0f s "
        "(Welt %.2f, Himmel %.2f), Donner %d (%.1f-%.1f s), Schnee-Weiss %.2f (Entsaettigen %.2f, Neigung %.2f, Test %d)",
        g_stormOn ? 1 : 0, g_stormRain, g_stormWorld[0], g_stormWorld[1], g_stormWorld[2], g_stormSky[0], g_stormSky[1], g_stormSky[2],
        g_stormFog, g_stormDropWind, g_lightMin, g_lightMax, g_lightWorld, g_lightSky, g_thunderOn ? 1 : 0, g_thunderMin, g_thunderMax,
        g_snowWhite, g_snowDesat, g_snowSlope, g_snowDebug);
    Log("Wetter: Haftung %d (Regen %.2f, Schnee %.2f, Sturm %.2f, KI %d), Auto: Schnee %.2f (%.0f s, Abbau %.0f s), Tropfen %.2f "
        "(nass %.0f s, trocken %.0f s)",
        g_gripOn ? 1 : 0, g_gripRain, g_gripSnow, g_gripStorm, g_gripAi ? 1 : 0, g_carSnowMax, g_carSnowTime, g_carSnowMelt,
        g_carDrops, g_carWetTime, g_carDryTime);

    static const unsigned char kSkyBytes[] = { 0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x50 };
    PatchJump("Regen (Sky_InterpLerp18)", kSkyRain, kSkyBytes, sizeof(kSkyBytes), reinterpret_cast<void *>(&SkyRainHook));
    PatchJump("Schnee (Sky_InterpLerp20)", kSkySnow, kSkyBytes, sizeof(kSkyBytes), reinterpret_cast<void *>(&SkySnowHook));
    if (g_dropsOn)
    {
        PatchCall("Scheibentropfen", kDropsTickCall, kDropsTick, reinterpret_cast<void *>(&DropsTickHook));
    }
    if (g_gripOn)
    {
        static const unsigned char kGripBytes[] = { 0x83, 0xEC, 0x08, 0x53, 0x55 };
        static const unsigned char kPlanBytes[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C };
        InstallHook("Haftung (Sim_UpdateWheelGrip)", kWheelGrip, kGripBytes, sizeof(kGripBytes),
                    reinterpret_cast<void *>(&WheelGripHook), reinterpret_cast<void **>(&g_origWheelGrip));
        if (g_gripAi)
        {
            InstallHook("KI-Tempo (AI_PlanRaceSpeed)", kPlanRaceSpeed, kPlanBytes, sizeof(kPlanBytes),
                        reinterpret_cast<void *>(&PlanSpeedHook), reinterpret_cast<void **>(&g_origPlanSpeed));
        }
    }
    if (true) // immer: die Wahl kann im Menü wechseln
    {
        static const unsigned char kSelectBytes[] = { 0x83, 0xEC, 0x10, 0x8B, 0x44, 0x24, 0x14 };
        InstallHook("Rennstart-Spec (Zufall, Tageszeit)", kSelectSpec, kSelectBytes, sizeof(kSelectBytes),
                    reinterpret_cast<void *>(&SelectSpecHook), reinterpret_cast<void **>(&g_origSelectSpec));
    }
    {
        static const unsigned char kClearBytes[] = { 0xC6, 0x05, 0x28, 0x8A, 0x62, 0x00, 0x00 };
        static void * dummy = nullptr;
        InstallHook("Nachtflag loeschen", 0x4B68E7, kClearBytes, sizeof(kClearBytes),
                    reinterpret_cast<void *>(&ClearNightHook), &dummy);
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

// Nacht-Abdunkeln für dx11z (doc/PLAN.md 15.1): Faktor je Farbkanal für die Welt. 1 = aktiv (Rennen, „Nacht“ auf
// einer Strecke, die das Spiel bei Tag zeigt), 0 = nichts tun.
extern "C" __declspec(dllexport) int LfspNightGrade(float * rgb)
{
    if (!lfsp_weather::NightGradeActive(rgb))
    {
        return 0;
    }
    return 1;
}

bool lfsp_weather::NightGradeActive(float * rgb)
{
    if (!g_nightActive || *reinterpret_cast<const int *>(kFrontEnd) != 0)
    {
        return false;
    }
    std::memcpy(rgb, g_nightMul, sizeof(g_nightMul));
    return true;
}

// dx11z: 1, wenn im Rennen „Nacht“ auf einer Strecke gilt, die das Spiel selbst bei Tag zeigt. Das Nachtflag ist dort
// gesetzt, aber die Streckenschichten für das Scheinwerferlicht (LayerVertexLight) fehlen; dx11z nimmt dann den Tag-Pfad.
extern "C" __declspec(dllexport) int LfspNightSynthetic()
{
    return lfsp_weather::NightSynthetic() ? 1 : 0;
}

bool lfsp_weather::NightSynthetic()
{
    return g_nightSynthetic && *reinterpret_cast<const int *>(kFrontEnd) == 0;
}

// dx11z (Build 9e): je 3 Werte Welt, Himmel, Leuchtsprites. 1 = Abdunkeln aktiv.
extern "C" __declspec(dllexport) int LfspNightGradeEx(float * out)
{
    return lfsp_weather::NightGradeEx(out) ? 1 : 0;
}

bool lfsp_weather::NightGradeEx(float * out)
{
    float world[3] = {};
    if (!NightGradeActive(world))
    {
        return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        out[i] = world[i];
        out[3 + i] = 1.0f + (world[i] - 1.0f) * g_skyDarken; // Himmel: nur ein Teil des Welt-Faktors, bleibt bläulich
        out[6 + i] = g_lightBoost;
    }
    return true;
}

// dx11z (Wetter w1): einmal je Bild. out[0…2] Faktor Welt, [3…5] Faktor Himmel (Sturm, Blitz), [6] Nebel-Exponent,
// [7] Schnee-Weiß (0 = aus), [8] Entsättigen, [9] Neigung (0 gleichmäßig … 1 nur Flächen nach oben), [10] Testbild,
// [11…13] Schneefarbe, [14] Blitz 0…1, [15] Modus. 1 = etwas davon aktiv.
extern "C" __declspec(dllexport) int LfspWeatherFx(float * out)
{
    return lfsp_weather::WeatherFx(out) ? 1 : 0;
}

bool lfsp_weather::WeatherFx(float * out)
{
    for (int i = 0; i < 16; ++i)
    {
        out[i] = i < 6 ? 1.0f : 0.0f;
    }
    out[6] = 1.0f;
    out[15] = static_cast<float>(g_effMode);
    if (Log == nullptr || *reinterpret_cast<const int *>(kFrontEnd) != 0)
    {
        g_flashNext = 0;
        return false;
    }
    bool active = false;
    if (StormMode(g_effMode))
    {
        active = true;
        for (int i = 0; i < 3; ++i)
        {
            out[i] = g_stormWorld[i];
            out[3 + i] = g_stormSky[i];
        }
        out[6] = g_stormFog;
    }
    if (g_effMode == 5 && (g_lightWorld > 0.0f || g_lightSky > 0.0f))
    {
        const DWORD now = timeGetTime();
        if (g_flashNext == 0)
        {
            g_flashNext = now + static_cast<DWORD>(1000.0f * (g_lightMin + (g_lightMax - g_lightMin) * Roll(1000) / 999.0f));
        }
        else if (static_cast<int>(now - g_flashNext) >= 0)
        {
            // Neuer Blitz: 1 bis 3 Stöße, der erste am hellsten
            g_flashStart = now;
            g_pulseCount = 1 + static_cast<int>(Roll(3));
            float at = 0.0f;
            for (int i = 0; i < g_pulseCount; ++i)
            {
                g_pulses[i].at = at;
                g_pulses[i].amp = i == 0 ? 1.0f : 0.45f + 0.45f * Roll(100) / 99.0f;
                at += 0.07f + 0.12f * Roll(100) / 99.0f;
            }
            g_flashNext = now + static_cast<DWORD>(1000.0f * (g_lightMin + (g_lightMax - g_lightMin) * Roll(1000) / 999.0f));
            if (g_thunderOn)
            {
                // Takte bis zum Donner (64 je Sekunde): Snd_QueueThunder wartet 0x87 − abzug Takte, Snd_PlayQueued
                // (Snd_RaceTick) lädt die Probe in der Wartezeit vor und spielt sie dann.
                const float delay = g_thunderMin + (g_thunderMax - g_thunderMin) * Roll(1000) / 999.0f;
                const int ticks = static_cast<int>(delay * 64.0f) + 2;
                using ThunderFn = void (__cdecl *)(int reduction, int pan);
                reinterpret_cast<ThunderFn>(kQueueThunder)(0x87 - ticks, static_cast<int>(Roll(65536)));
            }
            if (g_logOn)
            {
                Log("Wetter: Blitz (%d Stoesse), naechster in %.1f s", g_pulseCount, (g_flashNext - now) / 1000.0f);
            }
        }
        const float t = (now - g_flashStart) / 1000.0f;
        float flash = 0.0f;
        for (int i = 0; i < g_pulseCount; ++i)
        {
            const float d = t - g_pulses[i].at;
            if (d >= 0.0f && d < 0.6f)
            {
                flash = std::fmax(flash, g_pulses[i].amp * std::exp(-d / 0.07f));
            }
        }
        if (flash > 0.002f)
        {
            out[14] = flash;
            for (int i = 0; i < 3; ++i)
            {
                out[i] *= 1.0f + g_lightWorld * flash;
                out[3 + i] *= 1.0f + g_lightSky * flash;
            }
        }
    }
    // Schnee-Weiß bei Schnee aus dem Menü oder auf einer Schneestrecke (Wetterspur), nicht bei Regen oder Sturm
    const float snow = g_effMode == 2 ? 1.0f : g_effMode == 0 ? std::fmin(g_rawSnow * 2.0f, 1.0f) : 0.0f;
    if (g_snowWhite > 0.0f && snow > 0.0f)
    {
        active = true;
        out[7] = g_snowWhite * snow;
        out[8] = g_snowDesat * snow;
        out[9] = g_snowSlope;
        out[10] = static_cast<float>(g_snowDebug);
        out[11] = g_snowColour[0];
        out[12] = g_snowColour[1];
        out[13] = g_snowColour[2];
    }
    return active;
}

// dx11z (Wetter w2): einmal je Bild, für den Reflexionsdurchgang der Autos. out[0] Schnee auf dem Auto 0…1, [1] Tropfen
// (Dichte × Nässe) 0…1, [2] Zeit in Sekunden (Tropfen erscheinen und vergehen), [3…7] frei. 1 = etwas davon aktiv.
extern "C" __declspec(dllexport) int LfspWeatherCar(float * out)
{
    return lfsp_weather::WeatherCar(out) ? 1 : 0;
}

bool lfsp_weather::WeatherCar(float * out)
{
    for (int i = 0; i < 8; ++i)
    {
        out[i] = 0.0f;
    }
    if (Log == nullptr || *reinterpret_cast<const int *>(kFrontEnd) != 0)
    {
        g_carTick = 0;
        return false;
    }
    const DWORD now = timeGetTime();
    const float dt = g_carTick ? std::fmin((now - g_carTick) / 1000.0f, 0.5f) : 0.0f;
    g_carTick = now;
    // Schnee fällt (Menü oder Schneestrecke): steigt bis CarSnow; sonst Abbau, bei Regen doppelt so schnell
    const bool snowing = g_lastSnow > 0.05f;
    const bool raining = g_lastRain > 0.05f;
    if (g_carSnowMax > 0.0f)
    {
        if (snowing)
        {
            g_carSnow = std::fmin(g_carSnow + dt * g_carSnowMax / g_carSnowTime * std::fmin(g_lastSnow / 0.5f, 1.5f), g_carSnowMax);
        }
        else
        {
            g_carSnow = std::fmax(g_carSnow - dt * g_carSnowMax / g_carSnowMelt * (raining ? 2.0f : 1.0f), 0.0f);
        }
    }
    if (g_carDrops > 0.0f)
    {
        g_carWet = raining ? std::fmin(g_carWet + dt / g_carWetTime, 1.0f) : std::fmax(g_carWet - dt / g_carDryTime, 0.0f);
    }
    out[0] = g_carSnow;
    out[1] = g_carDrops * g_carWet;
    out[2] = static_cast<float>(now % 3600000u) / 1000.0f;
    return out[0] > 0.0f || out[1] > 0.0f;
}
