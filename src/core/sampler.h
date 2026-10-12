#pragma once

// Sampling profiler for the game's main thread
// A separate thread suspends the main thread about once per millisecond,
// records where it is, and writes the hottest locations of every window to
// the log. When the main thread sits in a system wait the first return
// address back into Porsche.exe is recorded, so waits show their caller

#include <atomic>
#include <cstdint>
#include <unordered_map>

#include "config.h"
#include "win_handle.h"

namespace nfs5
{
    class Sampler
    {
    private:
        UniqueHandle m_TargetThread;
        UniqueHandle m_SamplerThread;
        UniqueHandle m_StopEvent;
        ProfilerSettings m_Settings;
        std::unordered_map< std::uint32_t, std::uint32_t > m_ExeHits;
        std::unordered_map< std::uint32_t, std::uint32_t > m_WaitCallers;
        std::unordered_map< std::uint32_t, std::uint32_t > m_ModuleHits;
        std::uint32_t m_TotalSamples = 0;
        std::uint32_t m_WaitSamples = 0;
        std::uintptr_t m_ExeBegin = 0;
        std::uintptr_t m_ExeEnd = 0;
        bool m_Installed = false;

        Sampler() = default;

        static DWORD WINAPI ThreadProc( LPVOID parameter );

        void Run();

        void TakeSample();

        void FlushWindow( double window_seconds );

        [[nodiscard]] std::uint32_t FindExeCaller(
            std::uint32_t stack_pointer
        ) const;

    public:
        Sampler( const Sampler & ) = delete;
        Sampler & operator=( const Sampler & ) = delete;

        static Sampler & GetInstance();

        void Install( const ProfilerSettings & settings );

        void Uninstall();
    };
}
