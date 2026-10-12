#pragma once

// Storage I/O acceleration for the EA file layer [nfile.c].

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"

namespace nfs5
{
    class IoHooks
    {
    private:

        struct DiskSpaceEntry
        {
            DWORD m_SectorsPerCluster = 0;
            DWORD m_BytesPerSector = 0;
            DWORD m_FreeClusters = 0;
            DWORD m_TotalClusters = 0;
            BOOL m_Result = FALSE;
            DWORD m_LastError = 0;
            std::uint64_t m_Stamp = 0;
        };

        std::unique_ptr<safetyhook::InlineHook> m_OpenHook;
        std::unique_ptr<safetyhook::InlineHook> m_CpuSpeedRdtscHook;
        std::unique_ptr<safetyhook::InlineHook> m_CpuSpeedLegacyHook;
        std::mutex m_CpuSpeedLock;
        std::optional<int> m_CpuSpeedRdtsc;
        std::optional<int> m_CpuSpeedLegacy;
        hook::IatPatch m_DiskSpacePatch;
        std::mutex m_DiskSpaceLock;
        std::map<std::string, DiskSpaceEntry> m_DiskSpaceCache;
        IoSettings m_Settings;
        bool m_Installed = false;

        IoHooks() = default;

        static int __cdecl FileOpenHook(
            const char * file_name,
            std::uint32_t flags,
            std::uint32_t block_size,
            int * out_handle
        );

        static int __cdecl CpuSpeedRdtscHook();

        static int __cdecl CpuSpeedLegacyHook();

        // Runs the measurement the first time only, later calls return the cached value.
        int CachedCpuSpeed(
            safetyhook::InlineHook & hook,
            std::optional< int > & cache
        );

        static BOOL WINAPI GetDiskFreeSpaceHook(
            LPCSTR root_path,
            LPDWORD sectors_per_cluster,
            LPDWORD bytes_per_sector,
            LPDWORD free_clusters,
            LPDWORD total_clusters
        );

    public:

        IoHooks( const IoHooks & ) = delete;
        IoHooks & operator=( const IoHooks & ) = delete;

        static IoHooks & GetInstance();

        bool Install(
            const IoSettings & settings
        );

        void Uninstall();
    };
}