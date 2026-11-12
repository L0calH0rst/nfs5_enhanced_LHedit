// coverage.cpp – LFSP: Funktionsabdeckung ([Coverage] in lfsp.ini, doc/re/OFFEN.md, Messung für die Funktionen mit ◐).
// Setzt nach einer Wartezeit an den Anfang jeder Funktion aus der Liste einen Einmal-Haltepunkt (INT3). Der erste
// Aufruf schreibt Funktion, Aufrufer (Rücksprungadresse), Faden und Spielzustand in eine Tabelle; danach steht das
// Originalbyte wieder da und die Funktion läuft ohne Kosten weiter. Stellen, deren Bytes nicht mehr der
// Porsche.exe auf der Platte entsprechen (Hooks von LFSP, dx11z/safetyhook, GameplayFixes), bleiben aus.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace lfsp_coverage
{
    using LogFn = void (*)(const char *, ...);
    void Install(const char * ini, const char * dir, LogFn log);
}

namespace
{
    lfsp_coverage::LogFn Log = nullptr;

    // ---- Spielzustand (Porsche.exe v3.5, doc/re/07-SPIELABLAUF.md, 08-FRONTEND.md) ----
    constexpr std::uintptr_t kFeGlobal   = 0x65B928; // != 0 im Frontend
    constexpr std::uintptr_t kGameMode   = 0x5E99F4; // < 2 Rennen, >= 2 Wiederholung
    constexpr std::uintptr_t kFeGameType = 0x6573E8; // FEGAME_TYPE
    constexpr std::uintptr_t kRaceType   = 0x6573EC; // RACE_TYPE
    constexpr std::uintptr_t kCops       = 0x657414; // COPS
    constexpr std::uintptr_t kReplay     = 0x657424; // Game_gReplayState (REPLAY)

    struct Entry
    {
        std::uint32_t addr;
        unsigned char orig;
        volatile LONG hit;
        std::string name;
    };

    struct Record
    {
        std::uint32_t index;
        std::uint32_t caller;
        DWORD tick;
        DWORD thread;
        int fe, mode, fegame, racetype, cops, replay;
        volatile LONG ready;
    };

    std::vector<Entry> g_entries;     // nach Adresse sortiert
    std::vector<Record> g_records;    // je Funktion höchstens ein Treffer
    volatile LONG g_recordCount = 0;
    std::string g_outPath;
    int g_delay = 15;

    int ReadInt(std::uintptr_t a)
    {
        return *reinterpret_cast<const int *>(a);
    }

    Entry * Find(std::uint32_t addr)
    {
        auto it = std::lower_bound(g_entries.begin(), g_entries.end(), addr,
                                   [](const Entry & e, std::uint32_t a) { return e.addr < a; });
        return it != g_entries.end() && it->addr == addr ? &*it : nullptr;
    }

    LONG CALLBACK CoverageVeh(EXCEPTION_POINTERS * info)
    {
        if (info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        CONTEXT & c = *info->ContextRecord;
        // Windows meldet bei INT3 die Adresse des Haltepunkts selbst (Eip steht schon wieder darauf)
        const std::uint32_t addr = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress));
        Entry * e = Find(addr);
        if (!e)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // Mehrere Fäden können den Haltepunkt gleichzeitig treffen: zurückschreiben ist harmlos, gezählt wird einmal
        *reinterpret_cast<volatile unsigned char *>(addr) = e->orig;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(addr), 1);
        c.Eip = addr;
        if (InterlockedExchange(&e->hit, 1) == 0)
        {
            const LONG slot = InterlockedIncrement(&g_recordCount) - 1;
            if (slot < static_cast<LONG>(g_records.size()))
            {
                Record & r = g_records[slot];
                r.index = static_cast<std::uint32_t>(e - g_entries.data());
                r.caller = *reinterpret_cast<const std::uint32_t *>(c.Esp);
                r.tick = GetTickCount();
                r.thread = GetCurrentThreadId();
                r.fe = ReadInt(kFeGlobal) != 0;
                r.mode = ReadInt(kGameMode);
                r.fegame = ReadInt(kFeGameType);
                r.racetype = ReadInt(kRaceType);
                r.cops = ReadInt(kCops);
                r.replay = ReadInt(kReplay);
                InterlockedExchange(&r.ready, 1);
            }
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Liste: je Zeile "ADRESSE NAME" (hex), # = Kommentar
    bool LoadList(const std::string & path)
    {
        FILE * f = std::fopen(path.c_str(), "r");
        if (!f)
        {
            return false;
        }
        char line[512];
        while (std::fgets(line, sizeof(line), f))
        {
            if (line[0] == '#')
            {
                continue;
            }
            char * end = nullptr;
            const unsigned long a = std::strtoul(line, &end, 16);
            if (!a || end == line)
            {
                continue;
            }
            while (*end == ' ' || *end == '\t')
            {
                ++end;
            }
            std::string name = end;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            {
                name.pop_back();
            }
            Entry e{};
            e.addr = static_cast<std::uint32_t>(a);
            e.name = name;
            g_entries.push_back(e);
        }
        std::fclose(f);
        std::sort(g_entries.begin(), g_entries.end(), [](const Entry & x, const Entry & y) { return x.addr < y.addr; });
        g_entries.erase(std::unique(g_entries.begin(), g_entries.end(),
                                    [](const Entry & x, const Entry & y) { return x.addr == y.addr; }),
                        g_entries.end());
        return true;
    }

    void WriteRecords(FILE * out, LONG & written)
    {
        const LONG count = g_recordCount;
        const LONG n = std::min<LONG>(count, static_cast<LONG>(g_records.size()));
        while (written < n && g_records[written].ready)
        {
            const Record & r = g_records[written];
            const Entry & e = g_entries[r.index];
            std::fprintf(out, "%lu;%08X;%s;%08X;%lu;%d;%d;%d;%d;%d;%d\n", r.tick, e.addr, e.name.c_str(), r.caller,
                         r.thread, r.fe, r.mode, r.fegame, r.racetype, r.cops, r.replay);
            ++written;
        }
        std::fflush(out);
    }

    DWORD WINAPI CoverageThread(void *)
    {
        Sleep(static_cast<DWORD>(g_delay) * 1000);

        auto * exe = reinterpret_cast<unsigned char *>(GetModuleHandleA(nullptr));
        const auto * nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(exe + reinterpret_cast<IMAGE_DOS_HEADER *>(exe)->e_lfanew);
        const auto * sec = IMAGE_FIRST_SECTION(nt);
        std::uint32_t textLo = 0, textHi = 0;
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            {
                textLo = 0x400000 + sec[i].VirtualAddress;
                textHi = textLo + sec[i].Misc.VirtualSize;
                break;
            }
        }

        // Originalbytes direkt aus der Datei (LoadLibraryEx gäbe das laufende Modul zurück)
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::vector<unsigned char> fileBytes;
        if (FILE * f = std::fopen(exePath, "rb"))
        {
            std::fseek(f, 0, SEEK_END);
            fileBytes.resize(static_cast<std::size_t>(std::ftell(f)));
            std::fseek(f, 0, SEEK_SET);
            fileBytes.resize(std::fread(fileBytes.data(), 1, fileBytes.size(), f));
            std::fclose(f);
        }
        const IMAGE_SECTION_HEADER * textSec = nullptr;
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            {
                textSec = &sec[i];
                break;
            }
        }
        if (fileBytes.empty() || !textSec || !textLo)
        {
            Log("Coverage: %s nicht lesbar, Abbruch", exePath);
            return 0;
        }
        // Adresse in .text -> Byte in der Datei
        const auto diskAt = [&](std::uint32_t a) -> const unsigned char * {
            const std::size_t off = textSec->PointerToRawData + (a - textLo);
            return off + 16 <= fileBytes.size() ? fileBytes.data() + off : nullptr;
        };

        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void *>(textLo), textHi - textLo, PAGE_EXECUTE_READWRITE, &old);
        int armed = 0, changed = 0, outside = 0;
        std::vector<Entry> kept;
        kept.reserve(g_entries.size());
        for (Entry & e : g_entries)
        {
            if (e.addr < textLo + 8 || e.addr + 8 > textHi || !diskAt(e.addr - 8))
            {
                ++outside;
                continue;
            }
            const auto * mem = reinterpret_cast<const unsigned char *>(e.addr);
            if (std::memcmp(mem - 8, diskAt(e.addr - 8), 16) != 0)
            {
                ++changed;
                continue;
            }
            kept.push_back(e);
        }
        // Tabelle vor dem ersten Haltepunkt festlegen (der Handler sucht darin)
        g_entries.swap(kept);
        g_records.assign(g_entries.size(), Record{});
        AddVectoredExceptionHandler(1, &CoverageVeh);
        for (Entry & e : g_entries)
        {
            auto * p = reinterpret_cast<volatile unsigned char *>(e.addr);
            e.orig = *p;
            *p = 0xCC;
            ++armed;
        }
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(textLo), textHi - textLo);
        Log("Coverage: %d Haltepunkte gesetzt, %d ausgelassen (Code geändert/Hook), %d ausserhalb .text; Ausgabe %s",
            armed, changed, outside, g_outPath.c_str());

        FILE * out = std::fopen(g_outPath.c_str(), "w");
        if (!out)
        {
            Log("Coverage: %s nicht beschreibbar", g_outPath.c_str());
            return 0;
        }
        std::fprintf(out, "# tick;adresse;name;aufrufer;faden;frontend;gamemode;fegame_type;race_type;cops;replay\n");
        std::fprintf(out, "# gesetzt %d, ausgelassen %d, ausserhalb %d, Wartezeit %d s\n", armed, changed, outside, g_delay);
        LONG written = 0;
        for (;;)
        {
            Sleep(2000);
            WriteRecords(out, written);
        }
    }
}

namespace lfsp_coverage
{
    void Install(const char * ini, const char * dir, LogFn log)
    {
        Log = log;
        if (!GetPrivateProfileIntA("Coverage", "Enabled", 0, ini))
        {
            return;
        }
        g_delay = GetPrivateProfileIntA("Coverage", "Delay", 15, ini);
        char name[MAX_PATH] = {};
        GetPrivateProfileStringA("Coverage", "List", "lfsp_coverage.txt", name, MAX_PATH, ini);
        const std::string list = std::string(dir) + name;
        GetPrivateProfileStringA("Coverage", "Output", "lfsp_coverage.log", name, MAX_PATH, ini);
        g_outPath = std::string(dir) + name;
        if (!LoadList(list))
        {
            Log("Coverage: Liste %s fehlt", list.c_str());
            return;
        }
        HANDLE t = CreateThread(nullptr, 0, &CoverageThread, nullptr, 0, nullptr);
        Log("Coverage: %u Funktionen in %s, Start in %d s, Thread %s", static_cast<unsigned>(g_entries.size()),
            list.c_str(), g_delay, t ? "gestartet" : "fehlgeschlagen");
        if (t)
        {
            CloseHandle(t);
        }
    }
}
