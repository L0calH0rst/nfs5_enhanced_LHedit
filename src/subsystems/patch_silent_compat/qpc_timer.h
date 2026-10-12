#pragma once

// Replacement for the EA TIMER module [inittmr.c and lowtimer.c].

#include <atomic>
#include <cstdint>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/win_handle.h"

namespace nfs5
{
    class QpcTimer
    {
    private:

        std::unique_ptr<safetyhook::InlineHook> m_InitHook;
        std::unique_ptr<safetyhook::InlineHook> m_ShutdownHook;
        UniqueHandle m_WaitTimer;
        UniqueHandle m_StopEvent;
        UniqueHandle m_ExitedEvent;
        UniqueHandle m_FirstTickEvent;
        TimingSettings m_Settings;
        std::atomic<bool> m_Running = false;
        std::atomic<DWORD> m_ThreadId = 0;
        std::int64_t m_QpcFrequency = 1;
        int m_TickFrequency = 0;
        bool m_HighResolutionTimer = false;
        bool m_PeriodRequested = false;
        bool m_Installed = false;

        QpcTimer() = default;

        static void __cdecl InitHook(
            int frequency
        );

        static void __cdecl ShutdownHook();

        static void __cdecl ThreadProc();

        void Run();

        void WaitUntil(
            std::int64_t due_qpc
        );

        void Stop();

    public:

        QpcTimer( const QpcTimer & ) = delete;
        QpcTimer & operator=( const QpcTimer & ) = delete;

        static QpcTimer & GetInstance();

        bool Install(
            const TimingSettings & settings
        );

        void Uninstall();

        [[nodiscard]] int GetTickFrequency() const
        {
            return m_TickFrequency;
        }
    };

    // Returns the current 'QueryPerformanceCounter' value.
    [[nodiscard]] std::int64_t QpcNow();

    // Returns the 'QueryPerformanceCounter' frequency.
    [[nodiscard]] std::int64_t QpcFrequency();
}