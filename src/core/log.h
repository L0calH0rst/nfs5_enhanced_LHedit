#pragma once

// Minimal thread safe file logger.

#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

#include "win_handle.h"

namespace nfs5
{
    enum class LogLevel : std::uint8_t
    {
        TRACE = 0,
        INFO = 1,
        WARN = 2,
        ERR = 3,
        OFF = 4
    };

    class Log
    {
    private:

        std::mutex m_Lock;
        UniqueFileHandle m_File;
        LogLevel m_Level = LogLevel::INFO;
        std::uint64_t m_StartTicks = 0;
        std::uint64_t m_TickFrequency = 1;

        Log() = default;

        void WriteLine(
            std::string_view line
        );

    public:

        Log( const Log & ) = delete;
        Log & operator=( const Log & ) = delete;

        static Log & GetInstance();

        void Open(
            const std::wstring & path,
            LogLevel level
        );

        void Close();

        [[nodiscard]] bool IsEnabled(
            LogLevel level
        ) const
        {
            return m_File.IsValid() && level >= m_Level;
        }

        void Write(
            LogLevel level,
            std::string_view message
        );
    };


    template <typename... Args>
    void LogFormat(
        LogLevel level,
        std::format_string< Args... > fmt,
        Args &&... args
    )
    {
        Log & log = Log::GetInstance();

        if (!log.IsEnabled( level ))
            return;

        log.Write( level, std::format( fmt, std::forward< Args >( args )... ) );
    }
}

#define LOG_TRACE( ... ) ::nfs5::LogFormat( ::nfs5::LogLevel::TRACE, __VA_ARGS__ )
#define LOG_INFO( ... ) ::nfs5::LogFormat( ::nfs5::LogLevel::INFO, __VA_ARGS__ )
#define LOG_WARN( ... ) ::nfs5::LogFormat( ::nfs5::LogLevel::WARN, __VA_ARGS__ )
#define LOG_ERROR( ... ) ::nfs5::LogFormat( ::nfs5::LogLevel::ERR, __VA_ARGS__ )