#pragma once

// SDL3 controller support.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/win_handle.h"

#include "sdl3_api.h"
#include "virtual_gamepad.h"

namespace nfs5
{
    class Gamepad final : public input::PadSource
    {
    private:

        struct OpenPad
        {
            SDL_Gamepad * m_Handle = nullptr;
            SDL_JoystickID m_Id = 0;
            std::uint16_t m_Vendor = 0;
            std::uint16_t m_Product = 0;
            std::int64_t m_LastActivity = 0;
            std::uint32_t m_LastButtons = 0;
            float m_LastAxes[6] = {};

            // Read this pass, sticks '-1' to '1' and triggers '0' to '1' before dead zones.
            std::uint32_t m_Buttons = 0;
            float m_Axes[6] = {};
        };

        ControllerSettings m_Settings;
        std::filesystem::path m_Folder;
        sdl::Api m_Sdl;
        input::VirtualGamepad m_Device;

        std::unique_ptr<safetyhook::InlineHook> m_InputInitHook;
        std::unique_ptr<safetyhook::InlineHook> m_EnumCallbackHook;
        std::unique_ptr<safetyhook::InlineHook> m_InputShutdownHook;
        std::unique_ptr<safetyhook::InlineHook> m_DefaultBindingsHook;

        // SDL thread.
        UniqueHandle m_Thread;
        std::atomic<bool> m_Stop = false;
        UniqueHandle m_ReadyEvent;
        std::atomic<bool> m_SdlReady = false;

        // State shared with the game thread.
        mutable std::mutex m_StateLock;
        input::PadSnapshot m_Pad;
        std::vector<std::uint32_t> m_HandledIds;

        // Pads open on the SDL thread, touched only there.
        std::vector<OpenPad> m_Pads;
        SDL_JoystickID m_ActiveId = 0;
        SDL_JoystickID m_RumbleId = 0;
        float m_LastLow = 0.0f;
        float m_LastHigh = 0.0f;
        std::int64_t m_LastRumbleSend = 0;
        std::atomic<std::uint32_t> m_RumbleAck = 0;
        std::atomic<std::uint32_t> m_RumbleRequest = 0;

        // Index of the virtual device in the game list, '-1' while not inserted.
        int m_VirtualIndex = -1;
        bool m_Installed = false;

        Gamepad() = default;

        static int __fastcall InputInitHook(
            void * self,
            void * unused_edx
        );

        static BOOL __stdcall EnumCallbackHook(
            const DIDEVICEINSTANCEA * instance,
            void * reference
        );

        static void __cdecl InputShutdownHook();

        static char __cdecl DefaultBindingsHook(
            int device
        );

        // Binds driving, gears, camera and reset to the gamepad at the given table index.
        void ApplyGamepadDefaults(
            int device
        ) const;

        [[nodiscard]] bool IsVirtualDevice(
            int device
        ) const;

        static DWORD WINAPI ThreadProc(
            LPVOID parameter
        );

        void ThreadMain();

        void OpenPadById(
            SDL_JoystickID id
        );

        void ClosePadById(
            SDL_JoystickID id
        );

        void PublishHandledIds();

        void UpdatePads(
            std::int64_t now
        );

        void UpdateRumble(
            std::int64_t now
        );

        [[nodiscard]] bool IsGameActive() const;

        // Puts the virtual device into the game controller table at the given slot.
        void InsertDevice(
            int & count
        );

        // Returns 'true' when SDL reads the pad behind this DirectInput device.
        [[nodiscard]] bool IsHandledBySdl(
            const DIDEVICEINSTANCEA * instance
        ) const;

    public:

        Gamepad( const Gamepad & ) = delete;
        Gamepad & operator=( const Gamepad & ) = delete;

        static Gamepad & GetInstance();

        bool Install(
            const ControllerSettings & settings,
            const std::filesystem::path & folder
        );

        void Uninstall();

        // 'input::PadSource', called by the game thread through the virtual device.
        void ReadPad(
            input::PadSnapshot & out
        ) override;
    };
}