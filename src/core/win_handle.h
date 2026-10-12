#pragma once

// Wrappers for Win32 kernel objects.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <utility>

namespace nfs5
{
    // Traits decide what counts as an invalid value and how to release it
    struct KernelHandleTraits
    {
        static HANDLE Invalid() noexcept
        {
            return nullptr;
        }

        static bool IsValid(
            HANDLE handle
        ) noexcept
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        static void Close(
            HANDLE handle
        ) noexcept
        {
            ::CloseHandle( handle );
        }
    };

    struct FileHandleTraits
    {
        static HANDLE Invalid() noexcept
        {
            return INVALID_HANDLE_VALUE;
        }

        static bool IsValid(
            HANDLE handle
        ) noexcept
        {
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        }

        static void Close(
            HANDLE handle
        ) noexcept
        {
            ::CloseHandle( handle );
        }
    };

    template <typename Traits>
    class UniqueHandleBase
    {
    private:

        HANDLE m_Handle = Traits::Invalid();

    public:

        UniqueHandleBase() noexcept = default;

        explicit UniqueHandleBase( HANDLE handle ) noexcept
            : m_Handle( handle )
        {}

        ~UniqueHandleBase()
        {
            Reset();
        }

        UniqueHandleBase( const UniqueHandleBase & ) = delete;
        UniqueHandleBase & operator=( const UniqueHandleBase & ) = delete;

        UniqueHandleBase( UniqueHandleBase && other ) noexcept
            : m_Handle( std::exchange( other.m_Handle, Traits::Invalid() ) )
        {}

        UniqueHandleBase & operator=( UniqueHandleBase && other ) noexcept
        {
            if (this != &other)
                Reset( std::exchange( other.m_Handle, Traits::Invalid() ) );

            return *this;
        }

        void Reset( 
            HANDLE handle = Traits::Invalid()
        ) noexcept
        {
            HANDLE old_handle = std::exchange( m_Handle, handle );

            if (Traits::IsValid( old_handle ))
            {
                Traits::Close( old_handle );
            }
        }

        [[nodiscard]] HANDLE Release() noexcept
        {
            return std::exchange( m_Handle, Traits::Invalid() );
        }

        [[nodiscard]] HANDLE Get() const noexcept
        {
            return m_Handle;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Traits::IsValid( m_Handle );
        }

        explicit operator bool() const noexcept
        {
            return IsValid();
        }
    };

    using UniqueHandle = UniqueHandleBase<KernelHandleTraits>;
    using UniqueFileHandle = UniqueHandleBase<FileHandleTraits>;

    // Restores a page protection when leaving scope.
    class ScopedPageProtection
    {
    private:

        void * m_Address = nullptr;
        SIZE_T m_Size = 0;
        DWORD m_OldProtection = 0;
        bool m_Changed = false;

    public:

        ScopedPageProtection(
            void * address,
            SIZE_T size,
            DWORD protection = PAGE_EXECUTE_READWRITE
        ) noexcept
            : m_Address( address ),
            m_Size( size )
        {
            m_Changed = ::VirtualProtect( m_Address, m_Size, protection, &m_OldProtection ) != FALSE;
        }

        ~ScopedPageProtection()
        {
            if (m_Changed)
            {
                DWORD unused = 0;
                ::VirtualProtect( m_Address, m_Size, m_OldProtection, &unused );
                ::FlushInstructionCache( ::GetCurrentProcess(), m_Address, m_Size );
            }
        }

        ScopedPageProtection( const ScopedPageProtection & ) = delete;
        ScopedPageProtection & operator=( const ScopedPageProtection & ) = delete;

        [[nodiscard]] bool IsChanged() const noexcept
        {
            return m_Changed;
        }
    };
}