#pragma once

// SDL3 entry points resolved at run time from 'SDL3.dll'.
// The ASI never links against SDL, without the library the controller support stays off and
// the game keeps its own DirectInput handling.

#include <filesystem>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_version.h>

#include "../../core/win_handle.h"

namespace nfs5::sdl
{
    class Api
    {
    private:

        HMODULE m_Module = nullptr;

        template <typename T>
        bool Resolve(
            T & target,
            const char * name
        );

    public:

        decltype(&::SDL_GetVersion) GetVersion = nullptr;
        decltype(&::SDL_SetHint) SetHint = nullptr;
        decltype(&::SDL_Init) Init = nullptr;
        decltype(&::SDL_Quit) Quit = nullptr;
        decltype(&::SDL_GetError) GetError = nullptr;
        decltype(&::SDL_free) Free = nullptr;
        decltype(&::SDL_PollEvent) PollEvent = nullptr;
        decltype(&::SDL_WaitEventTimeout) WaitEventTimeout = nullptr;
        decltype(&::SDL_PumpEvents) PumpEvents = nullptr;
        decltype(&::SDL_GetGamepads) GetGamepads = nullptr;
        decltype(&::SDL_OpenGamepad) OpenGamepad = nullptr;
        decltype(&::SDL_CloseGamepad) CloseGamepad = nullptr;
        decltype(&::SDL_GetGamepadID) GetGamepadID = nullptr;
        decltype(&::SDL_GetGamepadName) GetGamepadName = nullptr;
        decltype(&::SDL_GetGamepadAxis) GetGamepadAxis = nullptr;
        decltype(&::SDL_GetGamepadButton) GetGamepadButton = nullptr;
        decltype(&::SDL_GetGamepadVendor) GetGamepadVendor = nullptr;
        decltype(&::SDL_GetGamepadProduct) GetGamepadProduct = nullptr;
        decltype(&::SDL_RumbleGamepad) RumbleGamepad = nullptr;
        decltype(&::SDL_AddGamepadMappingsFromFile) AddGamepadMappingsFromFile = nullptr;

        Api() = default;
        ~Api();

        Api( const Api & ) = delete;
        Api & operator=( const Api & ) = delete;

        // Loads 'SDL3.dll' from the given folder first, then from the default search path.
        bool Load(
            const std::filesystem::path & folder
        );

        void Unload();

        [[nodiscard]] bool IsLoaded() const
        {
            return m_Module != nullptr;
        }
    };
}