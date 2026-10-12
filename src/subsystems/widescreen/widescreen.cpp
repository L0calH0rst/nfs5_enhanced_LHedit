#include "widescreen.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#if defined( _MSC_VER )
#include <intrin.h>
#define NFS5_RETURN_ADDRESS() reinterpret_cast< std::uintptr_t >( _ReturnAddress() )
#else
#define NFS5_RETURN_ADDRESS() reinterpret_cast< std::uintptr_t >( __builtin_return_address( 0 ) )
#endif

#include "../../core/log.h"
#include "../../game/addresses.h"

namespace nfs5
{
    namespace
    {
        constexpr float BASE_ASPECT = 4.0f / 3.0f;
        constexpr float DEG_TO_RAD = std::numbers::pi_v< float > / 180.0f;
        constexpr float RAD_TO_DEG = 180.0f / std::numbers::pi_v<float>;
        constexpr float MAX_FOV_DEGREES = 170.0f;
        constexpr float MAX_CULL_HALF_ANGLE = 1.53f;

        // Operand target of fmul in the flare builder, starts as the original '1 / 640'.
        float s_FlareWidthScale = 1.0f / 640.0f;

        // Replaces the 640.0 the car LOD size is normalised with, larger keeps detail further out.
        float s_LodSizeScale = 640.0f;

        // 'THRASH_about' returns the driver info block, +3Ch mode count, +40h mode records.
        // Records are 40 bytes and 1 based, width height bpp first.
        // 'dx7z.dll' keeps only about 30 modes, every mode the display reports beyond
        // that is dropped, so 1920x1080 and up can be missing entirely.
        bool FindThrashMode(
            int width,
            int height,
            int bpp
        )
        {
            using AboutFn = std::uint8_t * (__stdcall *)();
            const auto about = hook::Ref< AboutFn >( game::var::THRASH_ABOUT );

            if (about == nullptr)
            {
                return false;
            }

            const std::uint8_t * info = about();

            if (info == nullptr)
            {
                return false;
            }

            const int count = *reinterpret_cast<const int *>(info + 0x3C);
            const auto * records = *reinterpret_cast<const std::uint8_t * const *>(info + 0x40);

            if (records == nullptr || count <= 0 || count > 256)
            {
                return false;
            }

            for (int i = 1; i <= count; ++i)
            {
                const auto * record = reinterpret_cast<const int *>(records + 40 * i);

                if (record[0] == width && record[1] == height && (bpp == 0 || record[2] == bpp))
                {
                    return true;
                }
            }

            return false;
        }
    }

    Widescreen & Widescreen::GetInstance()
    {
        static Widescreen instance;
        return instance;
    }

    float Widescreen::GetAspectCorrection()
    {
        const int width = hook::Ref< int >( game::var::SCREEN_WIDTH );
        const int height = hook::Ref< int >( game::var::SCREEN_HEIGHT );

        if (width <= 0 || height <= 0)
        {
            return 1.0f;
        }

        return (static_cast<float>(width) / static_cast<float>(height)) / BASE_ASPECT;
    }


    void Widescreen::UpdateSpriteScale(
        int width,
        int height
    )
    {
        if (!m_FlareScalePatch.IsApplied() || width <= 0 || height <= 0)
            return;

        // Flare width was 'screen_width / 640' and height 'screen_height / 480',
        // use the height based scale for both so flares stay round.
        s_FlareWidthScale = static_cast<float>(height) / (480.0f * static_cast<float>(width));
    }


    int __fastcall Widescreen::SetPerspectiveHook(
        game::EaglCamera * camera,
        void *,
        float fov_degrees,
        float aspect,
        float near_z,
        float far_z
    )
    {
        Widescreen & self = GetInstance();

        const int width = hook::Ref< int >( game::var::SCREEN_WIDTH );
        const int height = hook::Ref< int >( game::var::SCREEN_HEIGHT );
        const float correction = GetAspectCorrection();

        float new_fov = fov_degrees;
        float new_aspect = aspect;

        if (self.m_Settings.m_HorPlus && aspect > 0.0f && fov_degrees > 0.0f)
        {
            const float tan_half = std::tan( fov_degrees * 0.5f * DEG_TO_RAD ) * self.m_Settings.m_FovMultiplier;

            if (correction >= 1.0f)
            {
                // Wider than 4:3, keep the vertical extent and open up the horizontal one
                new_fov = 2.0f * std::atan( tan_half * correction ) * RAD_TO_DEG;
            }
            else
            {
                // Taller than 4:3, keep the horizontal extent and open up the vertical one
                new_fov = 2.0f * std::atan( tan_half ) * RAD_TO_DEG;
            }

            new_fov = std::min( new_fov, MAX_FOV_DEGREES );
            new_aspect = aspect * correction;
        }

        const int result = self.m_PerspectiveHook->thiscall< int >( camera, new_fov, new_aspect, near_z, far_z );

        if (new_fov != fov_degrees || new_aspect != aspect)
        {
            // 'SetActive' re-applies the stored inputs every time a camera is selected,
            // keeping the caller values makes that path idempotent.
            camera->m_FovDegrees = fov_degrees;
            camera->m_Aspect = aspect;

            // The original vertical cull angle is 'half_fov' times aspect, a generous bound.
            // Keep it, but never smaller than the true vertical half angle of the new projection.
            const float original_vertical = fov_degrees * 0.5f * DEG_TO_RAD * aspect;
            const float true_vertical = std::atan( std::tan( new_fov * 0.5f * DEG_TO_RAD ) / new_aspect );
            const float cull_vertical = std::clamp( std::max( original_vertical, true_vertical * 1.1f ), 0.0f, MAX_CULL_HALF_ANGLE );

            camera->m_TanHalfVertical = std::tan( cull_vertical );
            camera->m_CosHalfVertical = std::cos( cull_vertical );
        }

        self.UpdateSpriteScale( width, height );

        if (self.m_LodSizePatch.IsApplied())
        {
            // Undo the size loss of the wider horizontal fov, then apply the extra distance.
            const float fov_compensation = self.m_Settings.m_HorPlus ? std::max( correction, 1.0f ) * self.m_Settings.m_FovMultiplier : 1.0f;
            s_LodSizeScale = 640.0f * fov_compensation * self.m_Settings.m_LodDistanceScale;
        }

        if (width != self.m_LoggedWidth.load( std::memory_order_relaxed ) || height != self.m_LoggedHeight.load( std::memory_order_relaxed ))
        {
            self.m_LoggedWidth.store( width, std::memory_order_relaxed );
            self.m_LoggedHeight.store( height, std::memory_order_relaxed );

            LOG_INFO( "Resolution {}x{} aspect correction {:.4f}, sample camera fov {:.2f} -> {:.2f} aspect {:.4f} -> {:.4f}, car LOD scale {:.1f}",
                width, height, correction, fov_degrees, new_fov, aspect, new_aspect, s_LodSizeScale / 640.0f );
        }

        return result;
    }


    bool __cdecl Widescreen::IsVideoModeUsableHook(
        int mode_index
    )
    {
        Widescreen & self = GetInstance();

        if (self.m_ModeFilterHook->ccall< bool >( mode_index ))
            return true;

        const auto context = hook::Ref< std::uintptr_t >( game::var::RENDER_CONTEXT );

        if (context == 0 || hook::Ref< std::uint8_t >( context + 0x60 ) != 0)
            return false;

        const auto mode_object = hook::Ref< std::uintptr_t >( context + 0x70 );

        if (mode_object == 0)
            return false;

        // The original also rejected modes by a VRAM estimate and by card class,
        // both are meaningless on current hardware behind DDrawCompat.
        const auto * modes = hook::Ref< const game::DisplayMode * >( mode_object + 8 );
        const game::DisplayMode & mode = modes[mode_index];

        return mode.m_Width >= 640 && mode.m_Height >= 480 && mode.m_Capability >= 2 && mode.m_Bpp >= 15;
    }


    int __fastcall Widescreen::SetVideoModeHook(
        void * context,
        void *,
        int width,
        int height,
        int bpp,
        int flags
    )
    {
        Widescreen & self = GetInstance();

        const std::uintptr_t caller = NFS5_RETURN_ADDRESS();
        const bool race_switch = caller == game::fn::SET_VIDEO_MODE_RACE_START_RETURN || caller == game::fn::SET_VIDEO_MODE_RACE_OPTIONS_RETURN;

        if (race_switch)
        {
            int new_width = width;
            int new_height = height;

            if (!self.GetRaceResolutionOverride( new_width, new_height ))
            {
                new_width = width;
                new_height = height;
            }

            const int new_bpp = self.m_Settings.m_RaceColorDepth > 0 ? self.m_Settings.m_RaceColorDepth : bpp;
            const bool changed = new_width != width || new_height != height || new_bpp != bpp;

            if (changed && !FindThrashMode( new_width, new_height, new_bpp ))
            {
                // The driver would fall back to its closest entry and the display mode change fails,
                // which crashed 'dx7z.dll' in the window thread, keep the game choice instead.
                LOG_WARN( "Race video mode {}x{}x{} is not in the driver mode list, keeping {}x{}x{}. Limit 'SupportedResolutions' in 'DDrawCompat.ini'",
                    new_width, new_height, new_bpp, width, height, bpp );
            }
            else if (new_width >= 640 && new_height >= 480 && changed)
            {
                LOG_INFO( "Race video mode {}x{}x{} overridden to {}x{}x{}", width, height, bpp, new_width, new_height, new_bpp );

                width = new_width;
                height = new_height;
                bpp = new_bpp;

                // Keep the option globals in sync, later code compares them with the active mode.
                hook::Ref< int >( game::var::RACE_WIDTH ) = width;
                hook::Ref< int >( game::var::RACE_HEIGHT ) = height;
                hook::Ref< int >( game::var::RACE_BPP ) = bpp;
            }
        }

        return self.m_SetVideoModeHook->thiscall< int >( context, width, height, bpp, flags );
    }


    bool Widescreen::GetRaceResolutionOverride(
        int & width,
        int & height
    ) const
    {
        if (!m_Installed || m_Settings.m_RaceWidth == 0)
            return false;

        if (m_Settings.m_RaceWidth > 0)
        {
            width = m_Settings.m_RaceWidth;
            height = m_Settings.m_RaceHeight;
            return true;
        }

        // The registry mode is the real desktop, the current mode may be the
        // 640x480 front end mode DDrawCompat is emulating.
        DEVMODEW mode = {};
        mode.dmSize = sizeof( mode );

        if (::EnumDisplaySettingsW( nullptr, ENUM_REGISTRY_SETTINGS, &mode ) != FALSE && mode.dmPelsWidth >= 640)
        {
            width = static_cast<int>(mode.dmPelsWidth);
            height = static_cast<int>(mode.dmPelsHeight);
        }
        else
        {
            width = ::GetSystemMetrics( SM_CXSCREEN );
            height = ::GetSystemMetrics( SM_CYSCREEN );
        }

        return width >= 640 && height >= 480;
    }


    bool Widescreen::Install( const WidescreenSettings & settings )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "Widescreen disabled by config" );
            return true;
        }

        using namespace game;
        bool ok = true;

        if (settings.m_FixSpriteAspect)
        {
            ok &= m_FlareScalePatch.Apply(
                "FlareWidthScale",
                site::FLARE_WIDTH_SCALE.m_Address,
                site::FLARE_WIDTH_SCALE.m_Signature,
                site::FLARE_WIDTH_SCALE_OPERAND,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&s_FlareWidthScale)) );
        }

        if (settings.m_LodDistanceScale > 0.0f)
        {
            if (hook::MatchBytes( site::CAR_LOD_SIZE_BLOCK.m_Address, site::CAR_LOD_SIZE_BLOCK.m_Signature ))
            {
                s_LodSizeScale = 640.0f * settings.m_LodDistanceScale;
                ok &= m_LodSizePatch.Apply( "CarLodSize", site::CAR_LOD_SIZE_DIVIDE.m_Address, site::CAR_LOD_SIZE_DIVIDE.m_Signature,
                    site::CAR_LOD_SIZE_DIVIDE_OPERAND, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&s_LodSizeScale)) );
            }
            else
            {
                LOG_ERROR( "Car LOD size block does not match, car detail distance unchanged" );
                ok = false;
            }
        }

        m_PerspectiveHook = hook::CreateInline( "CameraSetPerspective", fn::CAMERA_SET_PERSPECTIVE.m_Address, fn::CAMERA_SET_PERSPECTIVE.m_Signature,
            reinterpret_cast<void *>(&SetPerspectiveHook) );

        ok &= m_PerspectiveHook != nullptr;

        if (settings.m_UnlockResolutions)
        {
            m_ModeFilterHook = hook::CreateInline( "IsVideoModeUsable", fn::IS_VIDEO_MODE_USABLE.m_Address, fn::IS_VIDEO_MODE_USABLE.m_Signature,
                reinterpret_cast<void *>(&IsVideoModeUsableHook) );
            ok &= m_ModeFilterHook != nullptr;
        }

        if (settings.m_RaceWidth != 0 || settings.m_RaceColorDepth != 0)
        {
            m_SetVideoModeHook = hook::CreateInline( "SetVideoMode", fn::SET_VIDEO_MODE.m_Address, fn::SET_VIDEO_MODE.m_Signature,
                reinterpret_cast<void *>(&SetVideoModeHook) );
            ok &= m_SetVideoModeHook != nullptr;
        }

        m_Installed = true;
        LOG_INFO( "Widescreen installed, all hooks ok {}", ok );
        return ok;
    }


    void Widescreen::Uninstall()
    {
        m_SetVideoModeHook.reset();
        m_ModeFilterHook.reset();
        m_PerspectiveHook.reset();
        m_FlareScalePatch.Restore();
        m_LodSizePatch.Restore();
        m_Installed = false;
    }
}