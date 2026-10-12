#pragma once

// Tracks the THRASH rasterizer driver [dx7z.dll and friends].

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "../core/hook_util.h"
#include "../core/win_handle.h"

namespace nfs5::game
{
    class ThrashDriver
    {
    private:

        std::unique_ptr< safetyhook::InlineHook > m_OpenHook;
        std::vector< std::function< void( HMODULE ) > > m_Listeners;
        std::mutex m_Lock;
        HMODULE m_Module = nullptr;

        // Driver tried when the configured one does not load, empty disables the retry.
        std::string m_FallbackDriver;

        // Registry value Thrash Driver as the game should see it, auto checks the stored one.
        std::string m_DriverOverride;
        hook::IatPatch m_RegQueryPatch;
        bool m_RegistryLogged = false;

        ThrashDriver() = default;

        static int __cdecl OpenDllHook(
            const char * driver_name
        );

        static LSTATUS WINAPI RegQueryValueHook(
            HKEY key,
            LPCSTR value_name,
            LPDWORD reserved,
            LPDWORD type,
            LPBYTE data,
            LPDWORD size
        );

        // Returns 'true' when the driver the registry names loads, the way 'THRASH_opendll' loads it.
        [[nodiscard]] static bool CanLoadDriver(
            const std::string & registry_name
        );

    public:

        ThrashDriver( const ThrashDriver & ) = delete;
        ThrashDriver & operator=( const ThrashDriver & ) = delete;

        static ThrashDriver & GetInstance();

        bool Install(
            const std::string & fallback_driver,
            const std::string & driver_override
        );

        void Uninstall();

        // Listener runs after every successful driver load, including reloads.
        void AddLoadListener(
            std::function< void( HMODULE ) > listener
        );

        [[nodiscard]] HMODULE GetModule() const
        {
            return m_Module;
        }
    };
}