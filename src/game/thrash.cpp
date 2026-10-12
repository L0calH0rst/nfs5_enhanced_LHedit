#include "thrash.h"

#include <cstring>

#include "../core/hook_util.h"
#include "../core/log.h"
#include "addresses.h"

namespace nfs5::game
{
    ThrashDriver & ThrashDriver::GetInstance()
    {
        static ThrashDriver instance;
        return instance;
    }

    int __cdecl ThrashDriver::OpenDllHook(
        const char * driver_name
    )
    {
        ThrashDriver & self = GetInstance();

        // A missing driver file makes 'THRASH_opendll' show its DirectX message box before it
        // returns, so the file is checked first and the fallback used straight away.
        std::string substitute;

        if (driver_name != nullptr && !self.m_FallbackDriver.empty())
        {
            std::string file = driver_name;
            const std::size_t name_start = file.find_last_of( "\\/" );

            if (file.find( '.', name_start == std::string::npos ? 0 : name_start + 1 ) == std::string::npos)
            {
                file += ".dll";
            }

            if (::GetFileAttributesA( file.c_str() ) == INVALID_FILE_ATTRIBUTES)
            {
                const std::string folder = name_start != std::string::npos ? std::string( driver_name ).substr( 0, name_start + 1 ) : std::string( ".\\drivers\\" );
                substitute = folder + self.m_FallbackDriver;

                if (_stricmp( substitute.c_str(), driver_name ) != 0)
                {
                    LOG_WARN( "THRASH driver file {} not found, loading {} instead", file, substitute );
                    driver_name = substitute.c_str();
                }
            }
        }

        int result = self.m_OpenHook->ccall< int >( driver_name );

        if (result == 0)
        {
            const std::string requested = driver_name != nullptr ? driver_name : "";
            LOG_WARN( "'THRASH_opendll' failed for {}", requested.empty() ? "<null>" : requested );

            // The driver name comes from the registry value Thrash Driver, which other mods
            // and old installs may change. Retry with the stock driver in the same folder so the
            // game starts instead of reporting a missing DirectX.
            if (self.m_FallbackDriver.empty())
                return result;

            const std::size_t slash = requested.find_last_of( "\\/" );
            const std::string folder = slash != std::string::npos ? requested.substr( 0, slash + 1 ) : std::string( ".\\drivers\\" );
            const std::string fallback = folder + self.m_FallbackDriver;

            if (_stricmp( fallback.c_str(), requested.c_str() ) == 0)
                return result;

            result = self.m_OpenHook->ccall< int >( fallback.c_str() );

            if (result == 0)
            {
                LOG_ERROR( "'THRASH_opendll' fallback {} failed too", fallback );
                return result;
            }

            LOG_WARN( "THRASH driver {} could not be loaded, using {} instead", requested, fallback );
            driver_name = nullptr;
        }

        // Resolve the module from a function pointer the loader just stored.
        HMODULE module = nullptr;
        const void * about = hook::Ref<void *>( var::THRASH_ABOUT );

        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(about),
            &module );

        std::vector<std::function<void( HMODULE )>> listeners;
        {
            std::scoped_lock lock( self.m_Lock );
            self.m_Module = module;
            listeners = self.m_Listeners;
        }

        LOG_INFO( "THRASH driver {} loaded at {:08X}", driver_name != nullptr ? driver_name : "fallback", reinterpret_cast<std::uintptr_t>(module) );

        for (const auto & listener : listeners)
        {
            listener( module );
        }

        return result;
    }


    bool ThrashDriver::CanLoadDriver(
        const std::string & registry_name
    )
    {
        // 'sub_467470' builds drivers folder and name, dx becomes dx7, then z is appended.
        // 'THRASH_opendll' tries that name and then the name with another z.
        const char * folder = hook::Ref<const char *>( var::DRIVER_FOLDER );
        std::string path = folder != nullptr ? folder : ".\\drivers\\";
        path += registry_name;

        if (_stricmp( registry_name.c_str(), "dx" ) == 0)
        {
            path += "7";
        }

        path += "z";

        for (const std::string & candidate : { path, path + "z" })
        {
            // The module stays loaded, the game loads the same file right after and
            // DllMain then runs only once
            if (::LoadLibraryA( candidate.c_str() ) != nullptr)
                return true;
        }

        LOG_WARN( "THRASH driver {} from the registry does not load, error {}", path, ::GetLastError() );
        return false;
    }


    LSTATUS WINAPI ThrashDriver::RegQueryValueHook(
        HKEY key,
        LPCSTR value_name,
        LPDWORD reserved,
        LPDWORD type,
        LPBYTE data,
        LPDWORD size
    )
    {
        ThrashDriver & self = GetInstance();

        const auto original = self.m_RegQueryPatch.GetOriginal< decltype(&::RegQueryValueExA) >();
        LSTATUS result = original( key, value_name, reserved, type, data, size );

        if (value_name == nullptr || _stricmp( value_name, "Thrash Driver" ) != 0 || data == nullptr || size == nullptr)
            return result;

        std::string stored;
        if (result == ERROR_SUCCESS && *size > 0)
            stored.assign( reinterpret_cast<const char *>(data), strnlen( reinterpret_cast<const char *>(data), *size ) );

        // Absent values are left alone, the game then writes its default dx and reads it back.
        std::string reported = stored;

        if (self.m_DriverOverride.empty() || _stricmp( self.m_DriverOverride.c_str(), "auto" ) == 0)
        {
            if (!stored.empty() && _stricmp( stored.c_str(), "dx" ) != 0 && !CanLoadDriver( stored ))
            {
                reported = "dx";
            }
        }
        else if (result == ERROR_SUCCESS)
        {
            reported = self.m_DriverOverride;
        }

        if (reported != stored && reported.size() + 1 <= 255)
        {
            std::memcpy( data, reported.c_str(), reported.size() + 1 );
            *size = static_cast<DWORD>(reported.size() + 1);

            if (type != nullptr)
                *type = REG_SZ;

            if (!self.m_RegistryLogged)
            {
                self.m_RegistryLogged = true;
                LOG_INFO( "Registry Thrash Driver {} reported to the game as {}", stored, reported );
            }
        }

        return result;
    }


    bool ThrashDriver::Install(
        const std::string & fallback_driver,
        const std::string & driver_override
    )
    {
        m_FallbackDriver = fallback_driver;
        m_DriverOverride = driver_override;

        if (_stricmp( driver_override.c_str(), "registry" ) != 0)
            m_RegQueryPatch.Apply( ::GetModuleHandleW( nullptr ), "ADVAPI32.dll", "RegQueryValueExA", reinterpret_cast<void *>(&RegQueryValueHook) );

        if (m_OpenHook)
            return true;

        m_OpenHook = hook::CreateInline(
            "THRASH_opendll",
            fn::THRASH_OPEN_DLL.m_Address,
            fn::THRASH_OPEN_DLL.m_Signature,
            reinterpret_cast<void *>(&OpenDllHook) );

        return m_OpenHook != nullptr;
    }


    void ThrashDriver::Uninstall()
    {
        m_RegQueryPatch.Restore();
        m_OpenHook.reset();
        std::scoped_lock lock( m_Lock );
        m_Listeners.clear();
    }


    void ThrashDriver::AddLoadListener(
        std::function< void( HMODULE ) > listener
    )
    {
        std::scoped_lock lock( m_Lock );
        m_Listeners.push_back( std::move( listener ) );
    }
}