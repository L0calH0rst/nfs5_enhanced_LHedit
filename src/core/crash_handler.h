#pragma once

// Unhandled exception reporter.

#include <string>

#include "hook_util.h"

namespace nfs5
{
    class CrashHandler
    {
    private:

        hook::IatPatch m_FilterPatch;
        std::wstring m_DumpPath;
        LPTOP_LEVEL_EXCEPTION_FILTER m_GameFilter = nullptr;
        bool m_Installed = false;

        CrashHandler() = default;

        static LONG WINAPI Filter(
            EXCEPTION_POINTERS * info
        );

        static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetFilterHook(
            LPTOP_LEVEL_EXCEPTION_FILTER filter
        );

        void WriteReport(
            EXCEPTION_POINTERS * info
        );

        void WriteDump(
            EXCEPTION_POINTERS * info
        );

    public:

        CrashHandler( const CrashHandler & ) = delete;
        CrashHandler & operator=( const CrashHandler & ) = delete;

        static CrashHandler & GetInstance();

        void Install(
            const std::wstring & dump_path
        );
    };
}