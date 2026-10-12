#include "framerate.h"

#include "../../core/hook_util.h"
#include "../../core/log.h"

#include "../../game/addresses.h"
#include "../../game/thrash.h"

#include "../patch_silent_compat/qpc_timer.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace nfs5
{
    namespace
    {
        // A race frame within this window keeps the race limit active.
        constexpr std::int64_t RACE_WINDOW_MS = 250;
    }

    Framerate & Framerate::GetInstance()
    {
        static Framerate instance;
        return instance;
    }

    bool Framerate::IsInRace() const
    {
        const std::int64_t last = m_LastRaceFrame.load( std::memory_order_relaxed );

        if (last == 0)
            return false;

        return (QpcNow() - last) * 1000 < RACE_WINDOW_MS * QpcFrequency();
    }


    void Framerate::RaceFrameMidHook(
        safetyhook::Context &
    )
    {
        GetInstance().m_LastRaceFrame.store( QpcNow(), std::memory_order_relaxed );
    }


    void Framerate::Limit(
        int fps_limit
    )
    {
        if (fps_limit <= 0)
        {
            m_NextDeadline = 0;
            return;
        }

        const std::int64_t frequency = QpcFrequency();
        const std::int64_t interval = frequency / fps_limit;
        std::int64_t now = QpcNow();

        if (m_NextDeadline == 0 || now - m_NextDeadline > interval * 4)
        {
            // First frame or a long stall, restart the grid instead of bursting to catch up.
            m_NextDeadline = now + interval;
            return;
        }

        while (now < m_NextDeadline)
        {
            const std::int64_t remaining_100ns = (m_NextDeadline - now) * 10000000 / frequency;

            if (remaining_100ns > 2000 && m_WaitTimer.IsValid())
            {
                LARGE_INTEGER due = {};
                due.QuadPart = -(remaining_100ns - 1000);

                if (::SetWaitableTimer( m_WaitTimer.Get(), &due, 0, nullptr, nullptr, FALSE ) != FALSE)
                {
                    ::WaitForSingleObject( m_WaitTimer.Get(), INFINITE );
                }
            }
            else if (remaining_100ns > 20000)
            {
                ::Sleep( 1 );
            }
            else
            {
                ::SwitchToThread();
            }

            now = QpcNow();
        }

        // Fixed grid keeps the cadence even, it never accumulates debt from slow frames.
        m_NextDeadline += interval;

        if (m_NextDeadline < now)
        {
            m_NextDeadline = now + interval;
        }
    }


    int __stdcall Framerate::PageflipHook()
    {
        Framerate & self = GetInstance();

        self.Limit( self.IsInRace() ? self.m_Settings.m_FpsLimit : self.m_Settings.m_FrontendFpsLimit );

        const PageflipFn original = self.m_OriginalPageflip.load( std::memory_order_acquire );
        return original != nullptr ? original() : 0;
    }


    void Framerate::OnThrashDriverLoaded()
    {
        auto & slot = hook::Ref<PageflipFn>( game::var::THRASH_PAGEFLIP );
        const PageflipFn current = slot;

        if (current == nullptr || current == &PageflipHook)
            return;

        m_OriginalPageflip.store( current, std::memory_order_release );
        slot = &PageflipHook;
        m_NextDeadline = 0;

        LOG_INFO( "'THRASH_pageflip' wrapped, original {:08X}", reinterpret_cast<std::uintptr_t>(current) );
    }


    bool Framerate::Install(
        const FramerateSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "Framerate subsystem disabled by config" );
            return true;
        }

        m_WaitTimer.Reset( ::CreateWaitableTimerExW( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS ) );

        if (!m_WaitTimer)
        {
            m_WaitTimer.Reset( ::CreateWaitableTimerW( nullptr, FALSE, nullptr ) );
        }

        m_RaceFrameHook = hook::CreateMid( "RaceRenderFrame", game::fn::RACE_RENDER_FRAME.m_Address, game::fn::RACE_RENDER_FRAME.m_Signature,
            &RaceFrameMidHook );

        game::ThrashDriver::GetInstance().AddLoadListener( [this] ( HMODULE )
        {
            OnThrashDriverLoaded();
        } );

        m_Installed = true;

        LOG_INFO( "Framerate installed, race limit {} frontend limit {}", settings.m_FpsLimit, settings.m_FrontendFpsLimit );
        return m_RaceFrameHook != nullptr;
    }


    void Framerate::Uninstall()
    {
        auto & slot = hook::Ref<PageflipFn>( game::var::THRASH_PAGEFLIP );
        const PageflipFn original = m_OriginalPageflip.exchange( nullptr );

        if (slot == &PageflipHook && original != nullptr)
        {
            slot = original;
        }

        m_RaceFrameHook.reset();
        m_Installed = false;
    }
}