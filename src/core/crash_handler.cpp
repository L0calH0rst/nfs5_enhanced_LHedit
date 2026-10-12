#include "crash_handler.h"

#include <dbghelp.h>

#include <atomic>

#include "log.h"

namespace nfs5
{
    namespace
    {
        std::atomic<bool> s_Reported = false;

        std::string DescribeAddress(
            std::uintptr_t address
        )
        {
            HMODULE module = nullptr;
            if (::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address),
                &module ) == FALSE || module == nullptr)
            {
                return std::format( "{:08X}", address );
            }

            char path[MAX_PATH] = {};
            ::GetModuleFileNameA( module, path, MAX_PATH );
            const char * name = path;

            for (const char * p = path; *p != '\0'; ++p)
            {
                if (*p == '\\' || *p == '/')
                {
                    name = p + 1;
                }
            }

            return std::format( "{:08X} {}+{:X}", address, name, address - reinterpret_cast<std::uintptr_t>(module) );
        }
    }

    CrashHandler & CrashHandler::GetInstance()
    {
        static CrashHandler instance;
        return instance;
    }

    void CrashHandler::WriteReport(
        EXCEPTION_POINTERS * info
    )
    {
        const EXCEPTION_RECORD & record = *info->ExceptionRecord;
        const CONTEXT & context = *info->ContextRecord;

        LOG_ERROR( "Unhandled exception {:08X} at {}", static_cast<std::uint32_t>(record.ExceptionCode),
            DescribeAddress( reinterpret_cast<std::uintptr_t>(record.ExceptionAddress) ) );

        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            LOG_ERROR( "  {} of address {:08X}", record.ExceptionInformation[0] == 0 ? "read" : (record.ExceptionInformation[0] == 1 ? "write" : "execute"),
                static_cast<std::uint32_t>(record.ExceptionInformation[1]) );
        }

        LOG_ERROR( "  eax {:08X} ebx {:08X} ecx {:08X} edx {:08X} esi {:08X} edi {:08X}",
            context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi, context.Edi );
        LOG_ERROR( "  ebp {:08X} esp {:08X} eip {:08X}", context.Ebp, context.Esp, context.Eip );

        // Raw scan of the stack, every value that lands in a module is a candidate return address.
        const auto * stack = reinterpret_cast<const std::uint32_t *>(context.Esp);
        int printed = 0;

        for (int i = 0; i < 256 && printed < 24; ++i)
        {
            MEMORY_BASIC_INFORMATION memory = {};
            if (::VirtualQuery( stack + i, &memory, sizeof( memory ) ) == 0 || memory.State != MEM_COMMIT)
            {
                break;
            }

            const std::uint32_t value = stack[i];
            HMODULE module = nullptr;
            if (value > 0x10000 && ::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(static_cast<std::uintptr_t>(value)),
                &module ) != FALSE)
            {
                LOG_ERROR( "  [esp+{:03X}] {}", i * 4, DescribeAddress( value ) );
                ++printed;
            }
        }
    }


    void CrashHandler::WriteDump(
        EXCEPTION_POINTERS * info
    )
    {
        using MiniDumpWriteDumpFn = BOOL( WINAPI * )(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
            PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

        HMODULE dbghelp = ::LoadLibraryW( L"dbghelp.dll" );

        if (dbghelp == nullptr)
            return;

        auto write_dump = reinterpret_cast<MiniDumpWriteDumpFn>(::GetProcAddress( dbghelp, "MiniDumpWriteDump" ));

        if (write_dump == nullptr)
            return;

        UniqueFileHandle file( ::CreateFileW( m_DumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr ) );

        if (!file)
            return;

        MINIDUMP_EXCEPTION_INFORMATION exception = {};
        exception.ThreadId = ::GetCurrentThreadId();
        exception.ExceptionPointers = info;
        exception.ClientPointers = FALSE;

        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);

        if (write_dump( ::GetCurrentProcess(), ::GetCurrentProcessId(), file.Get(), type, &exception, nullptr, nullptr ) != FALSE)
        {
            LOG_ERROR( "Minidump written to 'nfs5_enhanced_crashdump.dmp'" );
        }
    }


    LONG WINAPI CrashHandler::Filter(
        EXCEPTION_POINTERS * info
    )
    {
        CrashHandler & self = GetInstance();

        if (!s_Reported.exchange( true ))
        {
            self.WriteReport( info );
            self.WriteDump( info );
        }

        if (self.m_GameFilter != nullptr)
        {
            return self.m_GameFilter( info );
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }


    LPTOP_LEVEL_EXCEPTION_FILTER WINAPI CrashHandler::SetFilterHook(
        LPTOP_LEVEL_EXCEPTION_FILTER filter
    )
    {
        // The game installs its own filter, keep ours in front and chain to it.
        CrashHandler & self = GetInstance();
        return std::exchange( self.m_GameFilter, filter );
    }


    void CrashHandler::Install(
        const std::wstring & dump_path
    )
    {
        if (m_Installed)
            return;

        m_DumpPath = dump_path;
        m_GameFilter = ::SetUnhandledExceptionFilter( &Filter );
        m_FilterPatch.Apply( ::GetModuleHandleW( nullptr ), "KERNEL32.dll", "SetUnhandledExceptionFilter", reinterpret_cast<void *>(&SetFilterHook) );
        m_Installed = true;
    }
}