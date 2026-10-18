#include "sdl3_api.h"

#include "../../core/log.h"

namespace nfs5::sdl
{
    Api::~Api()
    {
        Unload();
    }

    template <typename T>
    bool Api::Resolve(
        T & target,
        const char * name
    )
    {
        target = reinterpret_cast<T>(reinterpret_cast<void *>(::GetProcAddress( m_Module, name )));

        if (target == nullptr)
        {
            LOG_ERROR( "'SDL3.dll' has no export {}", name );
            return false;
        }

        return true;
    }


    bool Api::Load(
        const std::filesystem::path & folder
    )
    {
        if (m_Module != nullptr)
            return true;

        // The copy next to the ASI wins over any other 'SDL3.dll' on the search path.
        const std::filesystem::path local = folder / L"SDL3.dll";
        m_Module = ::LoadLibraryExW( local.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );

        if (m_Module == nullptr)
        {
            m_Module = ::LoadLibraryW( L"SDL3.dll" );
        }

        if (m_Module == nullptr)
        {
            LOG_WARN( "'SDL3.dll' not found next to the ASI or on the search path, controller support is off" );
            return false;
        }

        bool ok = true;
        ok &= Resolve( GetVersion, "SDL_GetVersion" );
        ok &= Resolve( SetHint, "SDL_SetHint" );
        ok &= Resolve( Init, "SDL_Init" );
        ok &= Resolve( Quit, "SDL_Quit" );
        ok &= Resolve( GetError, "SDL_GetError" );
        ok &= Resolve( Free, "SDL_free" );
        ok &= Resolve( PollEvent, "SDL_PollEvent" );
        ok &= Resolve( WaitEventTimeout, "SDL_WaitEventTimeout" );
        ok &= Resolve( PumpEvents, "SDL_PumpEvents" );
        ok &= Resolve( GetGamepads, "SDL_GetGamepads" );
        ok &= Resolve( OpenGamepad, "SDL_OpenGamepad" );
        ok &= Resolve( CloseGamepad, "SDL_CloseGamepad" );
        ok &= Resolve( GetGamepadID, "SDL_GetGamepadID" );
        ok &= Resolve( GetGamepadName, "SDL_GetGamepadName" );
        ok &= Resolve( GetGamepadAxis, "SDL_GetGamepadAxis" );
        ok &= Resolve( GetGamepadButton, "SDL_GetGamepadButton" );
        ok &= Resolve( GetGamepadVendor, "SDL_GetGamepadVendor" );
        ok &= Resolve( GetGamepadProduct, "SDL_GetGamepadProduct" );
        ok &= Resolve( RumbleGamepad, "SDL_RumbleGamepad" );
        ok &= Resolve( AddGamepadMappingsFromFile, "SDL_AddGamepadMappingsFromFile" );

        if (!ok)
        {
            LOG_ERROR( "'SDL3.dll' is not a usable SDL 3 library, controller support is off" );
            Unload();
            return false;
        }

        const int version = GetVersion();
        LOG_INFO( "SDL {}.{}.{} loaded", SDL_VERSIONNUM_MAJOR( version ), SDL_VERSIONNUM_MINOR( version ), SDL_VERSIONNUM_MICRO( version ) );
        return true;
    }


    void Api::Unload()
    {
        if (m_Module != nullptr)
        {
            ::FreeLibrary( m_Module );
            m_Module = nullptr;
        }
    }
}