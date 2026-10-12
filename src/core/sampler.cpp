#include "sampler.h"

#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <vector>

#include "log.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace nfs5
{
    namespace
    {
        // Module bucket key, the upper half of the module base
        std::uint32_t ModuleKey( std::uint32_t address )
        {
            HMODULE module = nullptr;
            if ( ::GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast< LPCWSTR >( static_cast< std::uintptr_t >( address ) ), &module ) == FALSE )
            {
                return 0;
            }
            return static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( module ) );
        }

        std::string ModuleName( std::uint32_t base )
        {
            if ( base == 0 )
            {
                return "<no module>";
            }
            char path[MAX_PATH] = {};
            ::GetModuleFileNameA( reinterpret_cast< HMODULE >( static_cast< std::uintptr_t >( base ) ), path, MAX_PATH );
            const char * name = path;
            for ( const char * p = path; *p != '\0'; ++p )
            {
                if ( *p == '\\' )
                {
                    name = p + 1;
                }
            }
            return name;
        }

        template < typename Map >
        std::vector< std::pair< std::uint32_t, std::uint32_t > > TopEntries(
            const Map & map,
            std::size_t count
        )
        {
            std::vector< std::pair< std::uint32_t, std::uint32_t > > entries( map.begin(), map.end() );
            std::sort( entries.begin(), entries.end(), []( const auto & a, const auto & b ) { return a.second > b.second; } );
            if ( entries.size() > count )
            {
                entries.resize( count );
            }
            return entries;
        }
    }

    Sampler & Sampler::GetInstance()
    {
        static Sampler instance;
        return instance;
    }

    std::uint32_t Sampler::FindExeCaller( std::uint32_t stack_pointer ) const
    {
        // Copy the top of the stack without risking a fault
        std::array< std::uint32_t, 512 > stack = {};
        SIZE_T read = 0;
        ::ReadProcessMemory( ::GetCurrentProcess(), reinterpret_cast< LPCVOID >( static_cast< std::uintptr_t >( stack_pointer ) ),
            stack.data(), sizeof( stack ), &read );
        if ( read == 0 )
        {
            // Partial page at the stack top, retry with less
            ::ReadProcessMemory( ::GetCurrentProcess(), reinterpret_cast< LPCVOID >( static_cast< std::uintptr_t >( stack_pointer ) ),
                stack.data(), 256, &read );
        }

        const std::size_t count = read / sizeof( std::uint32_t );
        for ( std::size_t i = 0; i < count; ++i )
        {
            const std::uint32_t value = stack[i];
            if ( value < m_ExeBegin + 5 || value >= m_ExeEnd )
            {
                continue;
            }
            // A return address follows a call, check for E8 rel32 or FF /2 forms
            const auto * code = reinterpret_cast< const std::uint8_t * >( static_cast< std::uintptr_t >( value ) );
            if ( code[-5] == 0xE8 || code[-6] == 0xFF || code[-2] == 0xFF || code[-3] == 0xFF )
            {
                return value;
            }
        }
        return 0;
    }

    void Sampler::TakeSample()
    {
        if ( ::SuspendThread( m_TargetThread.Get() ) == static_cast< DWORD >( -1 ) )
        {
            return;
        }

        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL;
        const BOOL have_context = ::GetThreadContext( m_TargetThread.Get(), &context );
        // Only the stack copy happens while suspended, no heap use until resumed
        std::uint32_t caller = 0;
        const bool in_exe = have_context != FALSE && context.Eip >= m_ExeBegin && context.Eip < m_ExeEnd;
        if ( have_context != FALSE && !in_exe )
        {
            caller = FindExeCaller( context.Esp );
        }
        ::ResumeThread( m_TargetThread.Get() );

        if ( have_context == FALSE )
        {
            return;
        }

        ++m_TotalSamples;
        if ( in_exe )
        {
            ++m_ExeHits[context.Eip];
            return;
        }

        ++m_ModuleHits[ModuleKey( context.Eip )];
        ++m_WaitCallers[caller];
        ++m_WaitSamples;
    }

    void Sampler::FlushWindow( double window_seconds )
    {
        if ( m_TotalSamples == 0 )
        {
            return;
        }

        std::uint32_t exe_samples = 0;
        for ( const auto & entry : m_ExeHits )
        {
            exe_samples += entry.second;
        }

        LOG_INFO( "Profile {:.1f}s main thread samples {} in Porsche.exe {:.0f}% elsewhere {:.0f}%",
            window_seconds, m_TotalSamples, 100.0 * exe_samples / m_TotalSamples, 100.0 * m_WaitSamples / m_TotalSamples );

        for ( const auto & [address, hits] : TopEntries( m_ExeHits, m_Settings.m_TopEntries ) )
        {
            LOG_INFO( "  exe   {:08X} {:5.1f}%", address, 100.0 * hits / m_TotalSamples );
        }
        for ( const auto & [module, hits] : TopEntries( m_ModuleHits, 6 ) )
        {
            LOG_INFO( "  mod   {:<20} {:5.1f}%", ModuleName( module ), 100.0 * hits / m_TotalSamples );
        }
        for ( const auto & [caller, hits] : TopEntries( m_WaitCallers, m_Settings.m_TopEntries ) )
        {
            LOG_INFO( "  from  {:08X} {:5.1f}%", caller, 100.0 * hits / m_TotalSamples );
        }

        m_ExeHits.clear();
        m_ModuleHits.clear();
        m_WaitCallers.clear();
        m_TotalSamples = 0;
        m_WaitSamples = 0;
    }

    void Sampler::Run()
    {
        ::timeBeginPeriod( 1 );
        UniqueHandle timer( ::CreateWaitableTimerExW( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS ) );

        LARGE_INTEGER frequency = {};
        LARGE_INTEGER window_start = {};
        ::QueryPerformanceFrequency( &frequency );
        ::QueryPerformanceCounter( &window_start );

        for ( ;; )
        {
            if ( timer )
            {
                LARGE_INTEGER due = {};
                due.QuadPart = -static_cast< LONGLONG >( m_Settings.m_IntervalUs ) * 10;
                ::SetWaitableTimer( timer.Get(), &due, 0, nullptr, nullptr, FALSE );
                const HANDLE handles[2] = { m_StopEvent.Get(), timer.Get() };
                if ( ::WaitForMultipleObjects( 2, handles, FALSE, INFINITE ) == WAIT_OBJECT_0 )
                {
                    break;
                }
            }
            else if ( ::WaitForSingleObject( m_StopEvent.Get(), 1 ) == WAIT_OBJECT_0 )
            {
                break;
            }

            TakeSample();

            LARGE_INTEGER now = {};
            ::QueryPerformanceCounter( &now );
            const double elapsed = static_cast< double >( now.QuadPart - window_start.QuadPart ) / static_cast< double >( frequency.QuadPart );
            if ( elapsed * 1000.0 >= m_Settings.m_WindowMs )
            {
                FlushWindow( elapsed );
                window_start = now;
            }
        }
        ::timeEndPeriod( 1 );
    }

    DWORD WINAPI Sampler::ThreadProc( LPVOID parameter )
    {
        static_cast< Sampler * >( parameter )->Run();
        return 0;
    }

    void Sampler::Install( const ProfilerSettings & settings )
    {
        m_Settings = settings;
        if ( !settings.m_Enabled || m_Installed )
        {
            return;
        }

        const auto base = reinterpret_cast< std::uintptr_t >( ::GetModuleHandleW( nullptr ) );
        const auto * dos = reinterpret_cast< const IMAGE_DOS_HEADER * >( base );
        const auto * nt = reinterpret_cast< const IMAGE_NT_HEADERS * >( base + dos->e_lfanew );
        m_ExeBegin = base;
        m_ExeEnd = base + nt->OptionalHeader.SizeOfImage;

        m_TargetThread.Reset( ::OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ::GetCurrentThreadId() ) );
        m_StopEvent.Reset( ::CreateEventW( nullptr, TRUE, FALSE, nullptr ) );
        if ( !m_TargetThread || !m_StopEvent )
        {
            LOG_ERROR( "Profiler could not open the main thread" );
            return;
        }

        // Created through kernel32 directly, not through the game import, so it is never pinned
        m_SamplerThread.Reset( ::CreateThread( nullptr, 0, &ThreadProc, this, 0, nullptr ) );
        if ( m_SamplerThread )
        {
            ::SetThreadPriority( m_SamplerThread.Get(), THREAD_PRIORITY_TIME_CRITICAL );
            m_Installed = true;
            LOG_INFO( "Profiler sampling main thread every {} us, window {} ms", settings.m_IntervalUs, settings.m_WindowMs );
        }
    }

    void Sampler::Uninstall()
    {
        if ( !m_Installed )
        {
            return;
        }
        ::SetEvent( m_StopEvent.Get() );
        ::WaitForSingleObject( m_SamplerThread.Get(), 2000 );
        m_Installed = false;
    }
}
