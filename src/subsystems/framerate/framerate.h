#pragma once

// Frame pacing.
// Wraps 'THRASH_pageflip' so every presented frame passes through one place.

#include <atomic>
#include <cstdint>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/win_handle.h"

namespace nfs5
{
    class Framerate
    {
    private:

        using PageflipFn = int( __stdcall * )();

        std::unique_ptr<safetyhook::MidHook> m_RaceFrameHook;
        UniqueHandle m_WaitTimer;
        FramerateSettings m_Settings;
        std::atomic<PageflipFn> m_OriginalPageflip = nullptr;
        std::atomic<std::int64_t> m_LastRaceFrame = 0;
        std::int64_t m_NextDeadline = 0;
        bool m_Installed = false;

        Framerate() = default;

        static int __stdcall PageflipHook();

        static void RaceFrameMidHook(
            safetyhook::Context & context
        );

        void OnThrashDriverLoaded();

        void Limit(
            int fps_limit
        );

    public:

        Framerate( const Framerate & ) = delete;
        Framerate & operator=( const Framerate & ) = delete;

        static Framerate & GetInstance();

        bool Install(
            const FramerateSettings & settings
        );

        void Uninstall();

        // Returns 'true' while the race renderer produced a frame recently.
        [[nodiscard]] bool IsInRace() const;
    };
}