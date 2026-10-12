#include "affinity.h"

#include <bit>
#include <memory>
#include <vector>

#include "../../core/log.h"

namespace nfs5
{
    namespace
    {
        struct CoreInfo
        {
            DWORD_PTR m_FirstLogicalMask = 0;
            BYTE m_EfficiencyClass = 0;
        };

        // Enumerates physical cores, each entry carries the mask of its first logical processor.
        std::vector<CoreInfo> QueryPhysicalCores()
        {
            std::vector< CoreInfo > cores;
            DWORD length = 0;
            ::GetLogicalProcessorInformationEx( RelationProcessorCore, nullptr, &length );
            if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0)
            {
                return cores;
            }

            auto buffer = std::make_unique< std::uint8_t[] >( length );
            auto * info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer.get());
            if (::GetLogicalProcessorInformationEx( RelationProcessorCore, info, &length ) == FALSE)
            {
                return cores;
            }

            std::uint8_t * cursor = buffer.get();
            std::uint8_t * const end = buffer.get() + length;

            while (cursor < end)
            {
                const auto * entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>( cursor );

                if (entry->Relationship == RelationProcessorCore && entry->Processor.GroupCount > 0 && entry->Processor.GroupMask[0].Group == 0)
                {
                    const auto mask = static_cast<DWORD_PTR>(entry->Processor.GroupMask[0].Mask);

                    if (mask != 0)
                    {
                        CoreInfo core;
                        core.m_FirstLogicalMask = mask & (~mask + 1);

                        // EfficiencyClass follows Flags, older SDK headers name it 'Reserved'.
                        core.m_EfficiencyClass = reinterpret_cast<const BYTE *>(&entry->Processor.Flags)[1];
                        cores.push_back( core );
                    }
                }

                cursor += entry->Size;
            }

            return cores;
        }
    }

    AffinityManager & AffinityManager::GetInstance()
    {
        static AffinityManager instance;
        return instance;
    }

    DWORD_PTR AffinityManager::SelectGameCore() const
    {
        if (m_Settings.m_GameCore >= 0 && m_Settings.m_GameCore < static_cast<int>( sizeof( DWORD_PTR ) * 8 ))
        {
            const DWORD_PTR requested = static_cast<DWORD_PTR>( 1 ) << m_Settings.m_GameCore;

            if ((requested & m_ProcessMask) != 0)
            {
                return requested;
            }

            LOG_WARN( "Affinity GameCore {} is outside the process mask {:X}, using automatic selection", m_Settings.m_GameCore, m_ProcessMask );
        }

        // Candidate cores, physical cores first so the game never shares a core with its SMT sibling choice.
        std::vector< DWORD_PTR > candidates;
        const std::vector< CoreInfo > cores = QueryPhysicalCores();

        if (!cores.empty())
        {
            BYTE best_class = 0;
            if (m_Settings.m_PreferPerformanceCores)
            {
                for (const CoreInfo & core : cores)
                {
                    if ((core.m_FirstLogicalMask & m_ProcessMask) != 0 && core.m_EfficiencyClass > best_class)
                    {
                        best_class = core.m_EfficiencyClass;
                    }
                }
            }

            for (const CoreInfo & core : cores)
            {
                if ((core.m_FirstLogicalMask & m_ProcessMask) != 0 && core.m_EfficiencyClass >= best_class)
                {
                    candidates.push_back( core.m_FirstLogicalMask );
                }
            }
        }

        if (candidates.empty())
        {
            for (std::size_t bit = 0; bit < sizeof( DWORD_PTR ) * 8; ++bit)
            {
                const DWORD_PTR mask = static_cast<DWORD_PTR>( 1 ) << bit;

                if ((mask & m_ProcessMask) != 0)
                {
                    candidates.push_back( mask );
                }
            }
        }

        if (candidates.empty())
        {
            return 0;
        }

        // Core 0 services most interrupts and DPCs, skip it when there is a choice.
        if (candidates.size() >= 2 && candidates.front() == 1)
        {
            return candidates[1];
        }

        return candidates.front();
    }


    HANDLE WINAPI AffinityManager::CreateThreadHook(
        LPSECURITY_ATTRIBUTES attributes,
        SIZE_T stack_size,
        LPTHREAD_START_ROUTINE start_address,
        LPVOID parameter,
        DWORD creation_flags,
        LPDWORD thread_id
    )
    {
        AffinityManager & self = GetInstance();
        auto original = self.m_CreateThreadPatch.GetOriginal< decltype(&::CreateThread) >();

        HANDLE thread = original( attributes, stack_size, start_address, parameter, creation_flags | CREATE_SUSPENDED, thread_id );

        if (thread == nullptr)
            return thread;

        if (::SetThreadAffinityMask( thread, self.m_GameMask ) == 0)
        {
            LOG_WARN( "SetThreadAffinityMask failed for new game thread, error {}", ::GetLastError() );
        }
        else
        {
            self.m_PinnedThreads.fetch_add( 1, std::memory_order_relaxed );
        }

        if ((creation_flags & CREATE_SUSPENDED) == 0)
        {
            ::ResumeThread( thread );
        }

        return thread;
    }


    bool AffinityManager::Install(
        const AffinitySettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (settings.m_Mode == AffinityMode::OFF)
        {
            LOG_INFO( "Affinity management disabled by config" );
            return true;
        }

        DWORD_PTR system_mask = 0;
        if (::GetProcessAffinityMask( ::GetCurrentProcess(), &m_ProcessMask, &system_mask ) == FALSE)
        {
            LOG_ERROR( "GetProcessAffinityMask failed, error {}", ::GetLastError() );
            return false;
        }

        if (std::popcount( m_ProcessMask ) <= 1)
        {
            LOG_WARN( "Process is already limited to mask {:X}, set 'CpuAffinity = app' in 'DDrawCompat.ini' to let worker threads use other cores", m_ProcessMask );
        }

        m_GameMask = SelectGameCore();

        if (m_GameMask == 0)
        {
            LOG_ERROR( "No usable core for the game threads" );
            return false;
        }

        if (settings.m_Mode == AffinityMode::WHOLE_PROCESS)
        {
            if (::SetProcessAffinityMask( ::GetCurrentProcess(), m_GameMask ) == FALSE)
            {
                LOG_ERROR( "'SetProcessAffinityMask' failed, error {}", ::GetLastError() );
                return false;
            }
        }
        else
        {
            PinCurrentThread();

            if (!m_CreateThreadPatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "CreateThread", reinterpret_cast<void *>(&CreateThreadHook) ))
            {
                LOG_ERROR( "'CreateThread' import could not be redirected, game threads stay unpinned" );
                return false;
            }
        }

        m_Installed = true;

        LOG_INFO( "Affinity mode {} game mask {:X} process mask {:X}", static_cast<int>(settings.m_Mode), m_GameMask, m_ProcessMask );
        return true;
    }


    void AffinityManager::Uninstall()
    {
        m_CreateThreadPatch.Restore();
        m_Installed = false;
    }


    void AffinityManager::PinCurrentThread() const
    {
        if (m_GameMask != 0 && m_Settings.m_Mode != AffinityMode::OFF)
        {
            ::SetThreadAffinityMask( ::GetCurrentThread(), m_GameMask );
        }
    }
}