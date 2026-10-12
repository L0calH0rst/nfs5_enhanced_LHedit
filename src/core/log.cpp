#include "log.h"

#include <array>

namespace nfs5
{
    namespace
    {
        constexpr std::array< std::string_view, 5 > LEVEL_NAMES =
        {
            "TRACE",
            "INFO ",
            "WARN ",
            "ERROR",
            "OFF  "
        };
    }

    Log & Log::GetInstance()
    {
        static Log instance;
        return instance;
    }


    void Log::Open(
        const std::wstring & path,
        LogLevel level
    )
    {
        std::scoped_lock lock( m_Lock );

        m_Level = level;

        if (level == LogLevel::OFF)
        {
            m_File.Reset();
            return;
        }

        m_File.Reset( ::CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr ) );

        LARGE_INTEGER frequency = {};
        LARGE_INTEGER now = {};

        ::QueryPerformanceFrequency( &frequency );
        ::QueryPerformanceCounter( &now );

        m_TickFrequency = static_cast<std::uint64_t>(frequency.QuadPart);
        m_StartTicks = static_cast<std::uint64_t>(now.QuadPart);
    }


    void Log::Close()
    {
        std::scoped_lock lock( m_Lock );
        m_File.Reset();
    }


    void Log::WriteLine(
        std::string_view line
    )
    {
        DWORD written = 0;
        ::WriteFile( m_File.Get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr );
    }


    void Log::Write(
        LogLevel level,
        std::string_view message
    )
    {
        LARGE_INTEGER now = {};
        ::QueryPerformanceCounter( &now );

        const std::uint64_t elapsed_ticks = static_cast<std::uint64_t>(now.QuadPart) - m_StartTicks;
        const double elapsed_seconds = static_cast<double>(elapsed_ticks) / static_cast<double>(m_TickFrequency);
        const auto level_index = static_cast<std::size_t>(level);

        std::string line = std::format(
            "[{:10.4f}] [{}] [T{:5}] {}\r\n",
            elapsed_seconds,
            LEVEL_NAMES[level_index < LEVEL_NAMES.size() ? level_index : 0],
            ::GetCurrentThreadId(),
            message );

        std::scoped_lock lock( m_Lock );

        if (m_File.IsValid())
        {
            WriteLine( line );
        }
    }
}