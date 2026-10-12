#pragma once

// Thread affinity management.

#include <atomic>
#include <cstdint>

#include "../../core/config.h"
#include "../../core/hook_util.h"

namespace nfs5
{
    class AffinityManager
    {
    private:

        hook::IatPatch m_CreateThreadPatch;
        AffinitySettings m_Settings;
        DWORD_PTR m_GameMask = 0;
        DWORD_PTR m_ProcessMask = 0;
        std::atomic<std::uint32_t> m_PinnedThreads = 0;
        bool m_Installed = false;

        AffinityManager() = default;

        [[nodiscard]] DWORD_PTR SelectGameCore() const;

        static HANDLE WINAPI CreateThreadHook(
            LPSECURITY_ATTRIBUTES attributes,
            SIZE_T stack_size,
            LPTHREAD_START_ROUTINE start_address,
            LPVOID parameter,
            DWORD creation_flags,
            LPDWORD thread_id
        );

    public:

        AffinityManager( const AffinityManager & ) = delete;
        AffinityManager & operator=( const AffinityManager & ) = delete;

        static AffinityManager & GetInstance();

        bool Install(
            const AffinitySettings & settings
        );

        void Uninstall();

        // Applies the game core mask to the calling thread when enabled.
        void PinCurrentThread() const;

        [[nodiscard]] DWORD_PTR GetGameMask() const
        {
            return m_GameMask;
        }
    };
}