#pragma once

// Widescreen support.
// Every 3D view derives its aspect from a viewport rectangle scaled by 640x480,
// so the projection is corrected once inside EAGL camera 'SetPerspective'. This
// covers the chase, bumper, cockpit, replay and rear view mirror cameras alike.

#include <atomic>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"

#include "../../game/structs.h"

namespace nfs5
{
    class Widescreen
    {
    private:

        std::unique_ptr<safetyhook::InlineHook> m_PerspectiveHook;
        std::unique_ptr<safetyhook::InlineHook> m_ModeFilterHook;
        std::unique_ptr<safetyhook::InlineHook> m_SetVideoModeHook;
        hook::OperandPatch m_FlareScalePatch;
        hook::OperandPatch m_LodSizePatch;
        WidescreenSettings m_Settings;
        std::atomic<int> m_LoggedWidth = 0;
        std::atomic<int> m_LoggedHeight = 0;
        bool m_Installed = false;

        Widescreen() = default;

        static int __fastcall SetPerspectiveHook(
            game::EaglCamera * camera,
            void * unused_edx,
            float fov_degrees,
            float aspect,
            float near_z,
            float far_z
        );

        static bool __cdecl IsVideoModeUsableHook(
            int mode_index
        );

        static int __fastcall SetVideoModeHook(
            void * context,
            void * unused_edx,
            int width,
            int height,
            int bpp,
            int flags
        );

        void UpdateSpriteScale(
            int width,
            int height
        );

    public:

        Widescreen( const Widescreen & ) = delete;
        Widescreen & operator=( const Widescreen & ) = delete;

        static Widescreen & GetInstance();

        bool Install(
            const WidescreenSettings & settings
        );

        void Uninstall();

        // Ratio of the display aspect to the 4:3 the engine assumes.
        [[nodiscard]] static float GetAspectCorrection();

        // Resolution races will use when 'RaceResolution' overrides the game setting.
        [[nodiscard]] bool GetRaceResolutionOverride(
            int & width,
            int & height
        ) const;
    };
}