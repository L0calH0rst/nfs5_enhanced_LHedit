#pragma once

// DirectInput 5 joystick and force feedback effect backed by the SDL3 gamepad state.
// The game only ever sees this one controller for every SDL pad. It is listed at a fixed
// position, always attached, so bindings saved against it stay valid across restarts and
// while pads come and go.

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0500
#endif

#include <dinput.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace nfs5::input
{
    // Pad inputs after dead zones, neutral while no pad is connected or the game is inactive.
    struct PadSnapshot
    {
        bool m_Connected = false;

        // Sticks '-1' to '1', down and right positive.
        float m_LeftX = 0.0f;
        float m_LeftY = 0.0f;
        float m_RightX = 0.0f;
        float m_RightY = 0.0f;

        // Triggers '0' to '1'.
        float m_LeftTrigger = 0.0f;
        float m_RightTrigger = 0.0f;

        // One bit per 'SDL_GamepadButton'.
        std::uint32_t m_Buttons = 0;

        char m_Name[96] = {};
    };

    // Provides the current pad state to the device.
    class PadSource
    {
    public:

        virtual ~PadSource() = default;

        virtual void ReadPad(
            PadSnapshot & out
        ) = 0;
    };

    class VirtualGamepad;

    class VirtualEffect final : public IDirectInputEffect
    {
    private:

        enum class Kind : std::uint8_t
        {
            NONE,
            CONSTANT,
            RAMP,
            PERIODIC,
            CONDITION
        };

        std::atomic<ULONG> m_References = 1;
        VirtualGamepad * m_Device = nullptr;
        GUID m_Guid = {};

        // Parameters, guarded by the device lock.
        Kind m_Kind = Kind::NONE;
        DWORD m_Duration = INFINITE;
        DWORD m_Gain = 10000;
        float m_Magnitude = 0.0f;
        DWORD m_Period = 0;
        bool m_Playing = false;
        std::int64_t m_Start = 0;
        std::int64_t m_End = 0;

        void ApplyParameters(
            LPCDIEFFECT effect,
            DWORD flags
        );

        void Play(
            DWORD iterations
        );

    public:

        VirtualEffect(
            VirtualGamepad * device,
            REFGUID guid
        );

        // Adds this effect to the rumble motors, returns 'false' once it finished.
        bool Contribute(
            std::int64_t now,
            float & low,
            float & high
        ) const;

        void StopLocked()
        {
            m_Playing = false;
        }

        // IUnknown.
        STDMETHOD( QueryInterface )(
            REFIID riid,
            void ** object
            ) override;

        STDMETHOD_( ULONG, AddRef )() override;
        STDMETHOD_( ULONG, Release )() override;

        // IDirectInputEffect.
        STDMETHOD( Initialize )(
            HINSTANCE instance,
            DWORD version,
            REFGUID guid
            ) override;

        STDMETHOD( GetEffectGuid )(
            LPGUID guid
            ) override;

        STDMETHOD( GetParameters )(
            LPDIEFFECT effect,
            DWORD flags
            ) override;

        STDMETHOD( SetParameters )(
            LPCDIEFFECT effect,
            DWORD flags
            ) override;

        STDMETHOD( Start )(
            DWORD iterations,
            DWORD flags
            ) override;

        STDMETHOD( Stop )() override;

        STDMETHOD( GetEffectStatus )(
            LPDWORD status
            ) override;

        STDMETHOD( Download )() override;
        STDMETHOD( Unload )() override;

        STDMETHOD( Escape )(
            LPDIEFFESCAPE escape
            ) override;
    };

    class VirtualGamepad final : public IDirectInputDevice2A
    {
    private:

        friend class VirtualEffect;

        // Axis order of the state, offsets into 'DIJOYSTATE'.
        enum Axis
        {
            AXIS_X = 0,
            AXIS_Y,
            AXIS_Z,
            AXIS_RX,
            AXIS_RY,
            AXIS_RZ,
            AXIS_SLIDER,
            AXIS_COUNT
        };

        struct AxisRange
        {
            LONG m_Min = 0;
            LONG m_Max = 65535;
        };

        PadSource * m_Source = nullptr;
        bool m_ForceFeedback = true;
        std::array<AxisRange, AXIS_COUNT> m_Ranges = {};

        // Effects and force feedback state, read by the SDL thread for rumble.
        mutable std::mutex m_EffectLock;
        std::vector<VirtualEffect *> m_Effects;
        DWORD m_FeedbackGain = 10000;
        bool m_ActuatorsOn = true;
        bool m_Paused = false;

        [[nodiscard]] static int AxisFromOffset(
            DWORD offset
        );

        [[nodiscard]] LONG ToRange(
            int axis,
            float value
        ) const;

        void FillObject(
            DIDEVICEOBJECTINSTANCEA & object,
            DWORD offset
        ) const;

        void RemoveEffect(
            VirtualEffect * effect
        );

    public:

        // Device type as reported, a HID gamepad.
        static constexpr DWORD DEVICE_TYPE = DIDEVTYPE_JOYSTICK | (DIDEVTYPEJOYSTICK_GAMEPAD << 8) | DIDEVTYPE_HID;
        static constexpr int BUTTON_COUNT = 24;

        VirtualGamepad() = default;

        VirtualGamepad( const VirtualGamepad & ) = delete;
        VirtualGamepad & operator=( const VirtualGamepad & ) = delete;

        void Configure(
            PadSource * source,
            bool force_feedback
        );

        // Motor strengths '0' to '1' from the playing effects.
        void ComputeRumble(
            std::int64_t now,
            float & low,
            float & high
        ) const;

        // Stops every effect, used when the game shuts its input down.
        void StopAllEffects();

        // Fills a 'DIJOYSTATE' or 'DIJOYSTATE2' from the pad.
        void BuildState(
            const PadSnapshot & pad,
            DWORD size,
            void * out
        );

        // IUnknown.
        STDMETHOD( QueryInterface )(
            REFIID riid,
            void ** object
            ) override;

        STDMETHOD_( ULONG, AddRef )() override;
        STDMETHOD_( ULONG, Release )() override;

        // IDirectInputDeviceA.
        STDMETHOD( GetCapabilities )(
            LPDIDEVCAPS caps
            ) override;

        STDMETHOD( EnumObjects )(
            LPDIENUMDEVICEOBJECTSCALLBACKA callback,
            LPVOID reference,
            DWORD flags
            ) override;

        STDMETHOD( GetProperty )(
            REFGUID property,
            LPDIPROPHEADER header
            ) override;

        STDMETHOD( SetProperty )(
            REFGUID property,
            LPCDIPROPHEADER header
            ) override;

        STDMETHOD( Acquire )() override;
        STDMETHOD( Unacquire )() override;

        STDMETHOD( GetDeviceState )(
            DWORD size,
            LPVOID data
            ) override;

        STDMETHOD( GetDeviceData )(
            DWORD object_size,
            LPDIDEVICEOBJECTDATA data,
            LPDWORD count,
            DWORD flags
            ) override;

        STDMETHOD( SetDataFormat )(
            LPCDIDATAFORMAT format
            ) override;

        STDMETHOD( SetEventNotification )(
            HANDLE event
            ) override;

        STDMETHOD( SetCooperativeLevel )(
            HWND window,
            DWORD flags
            ) override;

        STDMETHOD( GetObjectInfo )(
            LPDIDEVICEOBJECTINSTANCEA object,
            DWORD id,
            DWORD how
            ) override;

        STDMETHOD( GetDeviceInfo )(
            LPDIDEVICEINSTANCEA instance
            ) override;

        STDMETHOD( RunControlPanel )(
            HWND owner,
            DWORD flags
            ) override;

        STDMETHOD( Initialize )(
            HINSTANCE instance,
            DWORD version,
            REFGUID guid
            ) override;

        // IDirectInputDevice2A.
        STDMETHOD( CreateEffect )(
            REFGUID guid,
            LPCDIEFFECT effect,
            LPDIRECTINPUTEFFECT * out,
            LPUNKNOWN outer
            ) override;

        STDMETHOD( EnumEffects )(
            LPDIENUMEFFECTSCALLBACKA callback,
            LPVOID reference,
            DWORD type
            ) override;

        STDMETHOD( GetEffectInfo )(
            LPDIEFFECTINFOA info,
            REFGUID guid
            ) override;

        STDMETHOD( GetForceFeedbackState )(
            LPDWORD state
            ) override;

        STDMETHOD( SendForceFeedbackCommand )(
            DWORD flags
            ) override;

        STDMETHOD( EnumCreatedEffectObjects )(
            LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,
            LPVOID reference,
            DWORD flags
            ) override;

        STDMETHOD( Escape )(
            LPDIEFFESCAPE escape
            ) override;

        STDMETHOD( Poll )() override;

        STDMETHOD( SendDeviceData )(
            DWORD object_size,
            LPCDIDEVICEOBJECTDATA data,
            LPDWORD count,
            DWORD flags
            ) override;
    };
}