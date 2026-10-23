// nfsp_texpool.asi – vergrößert den Texturpool „DTEX“ von Need for Speed Porsche (HD-Texturen, Aufgabe 2.1).
// Ziel: Porsche.exe v3.5 (TimeDateStamp 0x393C2E8F). Herleitung: doc/knowledge.md (Texturpool DTEX).
// Im Rennen legt das Spiel den Pool mit FUN_00553fc0(0x1800000) = 24 MB an. Eine 1024²-Textur 8888
// mit Mips braucht 5,6 MB; läuft der Pool über, stürzt FUN_00554ad0 ab (0x554AE6).
// Die ASI ersetzt die Größe an beiden Aufrufstellen (und im Log-Text daneben) durch TexturePoolMB.

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
    constexpr std::uint32_t kExpectedTimestamp = 0x393C2E8F;
    constexpr std::uint32_t kOriginalSize = 0x1800000;

    // Adresse des imm32 von „push 0x1800000“: je Aufruf FUN_00553fc0 und das Argument der Log-Ausgabe danach
    constexpr std::uintptr_t kSites[] = {0x4679DB, 0x4679EA, 0x467F46, 0x467F55};

    std::string g_logPath;

    void Log(const char* format, ...)
    {
        FILE* file = std::fopen(g_logPath.c_str(), "a");
        if (!file)
            return;
        va_list args;
        va_start(args, format);
        std::vfprintf(file, format, args);
        va_end(args);
        std::fputc('\n', file);
        std::fclose(file);
    }

    void Install(HMODULE self)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string base(path);
        base = base.substr(0, base.find_last_of('.'));
        g_logPath = base + ".log";
        std::remove(g_logPath.c_str());

        auto* exe = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);
        if (reinterpret_cast<std::uintptr_t>(exe) != 0x400000 || nt->FileHeader.TimeDateStamp != kExpectedTimestamp)
        {
            Log("Falsche Porsche.exe (TimeDateStamp 0x%08X), nichts geändert", nt->FileHeader.TimeDateStamp);
            return;
        }

        const std::string ini = base + ".ini";
        int megabytes = GetPrivateProfileIntA("General", "TexturePoolMB", 256, ini.c_str());
        if (megabytes < 24)
            megabytes = 24;
        if (megabytes > 1536)
            megabytes = 1536;
        const std::uint32_t size = static_cast<std::uint32_t>(megabytes) << 20;

        for (std::uintptr_t site : kSites)
        {
            auto* value = reinterpret_cast<std::uint32_t*>(site);
            if (*reinterpret_cast<std::uint8_t*>(site - 1) != 0x68 || *value != kOriginalSize)
            {
                Log("Stelle %08X hat unerwartete Bytes, nichts geändert", static_cast<unsigned>(site));
                return;
            }
        }
        for (std::uintptr_t site : kSites)
        {
            DWORD old;
            VirtualProtect(reinterpret_cast<void*>(site), 4, PAGE_EXECUTE_READWRITE, &old);
            *reinterpret_cast<std::uint32_t*>(site) = size;
            VirtualProtect(reinterpret_cast<void*>(site), 4, old, &old);
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        Log("Texturpool im Rennen: %d MB statt 24 MB", megabytes);
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        Install(module);
    }
    return TRUE;
}
