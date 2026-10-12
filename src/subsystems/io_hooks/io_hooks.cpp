#include "io_hooks.h"

#include <algorithm>
#include <cctype>

#include "../../core/log.h"

#include "../../game/addresses.h"
#include "../../game/structs.h"

#include "../patch_silent_compat/qpc_timer.h"

namespace nfs5
{
    namespace
    {
        // Free space is only used for save checks, a short cache is safe.
        constexpr std::uint64_t DISK_SPACE_TTL_MS = 2000;
    }

    IoHooks & IoHooks::GetInstance()
    {
        static IoHooks instance;
        return instance;
    }

    int __cdecl IoHooks::FileOpenHook(
        const char * file_name,
        std::uint32_t flags,
        std::uint32_t block_size,
        int * out_handle
    )
    {
        IoHooks & self = GetInstance();
        namespace ff = game::file_flag;

        // Unbuffered handles need sector sized transfers, those are left untouched.
        const bool read_only = (flags & ff::READ) != 0 && (flags & ff::WRITE) == 0 && (flags & ff::NO_BUFFERING) == 0;
        const int result = self.m_OpenHook->ccall< int >( file_name, flags, block_size, out_handle );

        if (result == 0 || out_handle == nullptr || !read_only)
        {
            return result;
        }

        // Handles are the bitwise complement of the table index.
        const int index = ~*out_handle;
        const int count = hook::Ref<int>( game::var::FILE_TABLE_COUNT );
        auto * table = hook::Ref<game::FileEntry *>( game::var::FILE_TABLE );

        if (table == nullptr || index < 0 || index >= count)
        {
            return result;
        }

        // The block size only bounds the 'ReadFile' and 'memcpy' chunk in 'sub_591DF0',
        // the async reader queries it for the file size alone, raising it is safe.
        game::FileEntry & entry = table[index];
        const auto chunk = static_cast<std::uint32_t>( self.m_Settings.m_ReadChunkKB ) * 1024u;

        if (entry.m_InUse != 0 && entry.m_BlockSize < chunk)
        {
            entry.m_BlockSize = chunk;
        }

        return result;
    }


    int IoHooks::CachedCpuSpeed(
        safetyhook::InlineHook & hook,
        std::optional< int > & cache
    )
    {
        std::scoped_lock lock( m_CpuSpeedLock );

        if (cache.has_value())
        {
            return *cache;
        }

        const std::int64_t start = QpcNow();
        cache = hook.ccall<int>();

        LOG_INFO( "CPU speed measured once in {} ms, value {:08X} cached", (QpcNow() - start) * 1000 / QpcFrequency(),
            static_cast<std::uint32_t>(*cache) );

        return *cache;
    }


    int __cdecl IoHooks::CpuSpeedRdtscHook()
    {
        IoHooks & self = GetInstance();

        if (!self.m_CpuSpeedRdtsc.has_value())
        {
            return self.CachedCpuSpeed( *self.m_CpuSpeedRdtscHook, self.m_CpuSpeedRdtsc );
        }

        return *self.m_CpuSpeedRdtsc;
    }


    int __cdecl IoHooks::CpuSpeedLegacyHook()
    {
        IoHooks & self = GetInstance();
        return self.CachedCpuSpeed( *self.m_CpuSpeedLegacyHook, self.m_CpuSpeedLegacy );
    }


    BOOL WINAPI IoHooks::GetDiskFreeSpaceHook(
        LPCSTR root_path,
        LPDWORD sectors_per_cluster,
        LPDWORD bytes_per_sector,
        LPDWORD free_clusters,
        LPDWORD total_clusters
    )
    {
        IoHooks & self = GetInstance();
        auto original = self.m_DiskSpacePatch.GetOriginal< decltype(&::GetDiskFreeSpaceA) >();

        if (root_path == nullptr)
        {
            return original( root_path, sectors_per_cluster, bytes_per_sector, free_clusters, total_clusters );
        }

        std::string key( root_path );
        std::transform( key.begin(), key.end(), key.begin(), [] ( char ch )
        {
            return static_cast<char>(std::toupper( static_cast<unsigned char>(ch) ));
        } );

        const std::uint64_t now = ::GetTickCount64();

        DiskSpaceEntry entry;
        bool cached = false;
        {
            std::scoped_lock lock( self.m_DiskSpaceLock );
            const auto it = self.m_DiskSpaceCache.find( key );

            if (it != self.m_DiskSpaceCache.end() && now - it->second.m_Stamp < DISK_SPACE_TTL_MS)
            {
                entry = it->second;
                cached = true;
            }
        }

        if (!cached)
        {
            entry.m_Result = original( root_path, &entry.m_SectorsPerCluster, &entry.m_BytesPerSector, &entry.m_FreeClusters, &entry.m_TotalClusters );
            entry.m_LastError = ::GetLastError();
            entry.m_Stamp = now;

            std::scoped_lock lock( self.m_DiskSpaceLock );
            self.m_DiskSpaceCache[key] = entry;
        }

        if (sectors_per_cluster != nullptr)
        {
            *sectors_per_cluster = entry.m_SectorsPerCluster;
        }

        if (bytes_per_sector != nullptr)
        {
            *bytes_per_sector = entry.m_BytesPerSector;
        }

        if (free_clusters != nullptr)
        {
            *free_clusters = entry.m_FreeClusters;
        }

        if (total_clusters != nullptr)
        {
            *total_clusters = entry.m_TotalClusters;
        }

        ::SetLastError( entry.m_Result != FALSE ? 0 : entry.m_LastError );
        return entry.m_Result;
    }

    bool IoHooks::Install( const IoSettings & settings )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        // Not an IO change, so it does not depend on the IO switch.
        if (settings.m_FastCpuSpeedCheck)
        {
            namespace fn = game::fn;

            // The result is a signed clock in Hz, any CPU above 2.1 GHz wraps it around and the
            // texture detail detection 'sub_44E370' then rates the machine below 235 MHz and loads
            // every texture at its lowest quality. The measurement is replaced by the constant the
            // Enhanced executable returns, 2130 MHz, the highest value that stays positive.
            m_CpuSpeedRdtsc = static_cast<int>(fn::CPU_SPEED_CONSTANT);
            m_CpuSpeedLegacy = static_cast<int>(fn::CPU_SPEED_CONSTANT);
            LOG_INFO( "CPU speed measurement skipped, reported as {:08X}", fn::CPU_SPEED_CONSTANT );

            m_CpuSpeedRdtscHook = hook::CreateInline( "CpuSpeedRdtsc", fn::CPU_SPEED_RDTSC.m_Address, fn::CPU_SPEED_RDTSC.m_Signature,
                reinterpret_cast<void *>(&CpuSpeedRdtscHook) );
            m_CpuSpeedLegacyHook = hook::CreateInline( "CpuSpeedLegacy", fn::CPU_SPEED_LEGACY.m_Address, fn::CPU_SPEED_LEGACY.m_Signature,
                reinterpret_cast<void *>(&CpuSpeedLegacyHook) );

            LOG_INFO( "CPU speed check hooks ok {}", m_CpuSpeedRdtscHook != nullptr && m_CpuSpeedLegacyHook != nullptr );
        }


        if (!settings.m_Enabled)
        {
            LOG_INFO( "IO subsystem disabled by config" );
            m_Installed = true;
            return true;
        }

        bool ok = true;
        m_OpenHook = hook::CreateInline( "FileOpen", game::fn::FILE_LAYER_OPEN.m_Address, game::fn::FILE_LAYER_OPEN.m_Signature,
            reinterpret_cast<void *>(&FileOpenHook) );

        ok &= m_OpenHook != nullptr;

        if (settings.m_CacheDiskFreeSpace)
        {
            ok &= m_DiskSpacePatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "GetDiskFreeSpaceA",
                reinterpret_cast<void *>(&GetDiskFreeSpaceHook) );
        }

        m_Installed = true;

        LOG_INFO( "IO installed, read chunk {} KB, all hooks ok {}", settings.m_ReadChunkKB, ok );
        return ok;
    }

    void IoHooks::Uninstall()
    {
        m_CpuSpeedLegacyHook.reset();
        m_CpuSpeedRdtscHook.reset();
        m_DiskSpacePatch.Restore();
        m_OpenHook.reset();
        m_Installed = false;
    }
}