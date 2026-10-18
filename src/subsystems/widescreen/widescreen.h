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
        std::unique_ptr<safetyhook::InlineHook> m_ResolutionCtorHook;
        std::unique_ptr<safetyhook::InlineHook> m_PositionSaveHook;
        std::unique_ptr<safetyhook::InlineHook> m_PositionLoadHook;
        hook::CallSitePatch m_ResolutionCountPatch;
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

        static int __cdecl ResolutionListCountHook();

        static void * __fastcall ResolutionAccessorCtorHook(
            void * accessor,
            void * unused_edx,
            const char * name
        );

        static void * __fastcall PositionSaveHook(
            void * accessor,
            void * unused_edx,
            std::uint8_t ** cursor
        );

        static int __fastcall PositionLoadHook(
            void * accessor,
            void * unused_edx,
            std::uint8_t ** cursor
        );

        [[nodiscard]] static bool IsResolutionAccessor(
            const void * accessor
        );

        // Position of a mode in the screen size list, '-1' when it is not listed.
        [[nodiscard]] static int FindListedMode(
            const void * accessor,
            int width,
            int height,
            int bpp
        );

        // The desktop size in 32 bit, else in 16 bit, else the largest 32 bit mode.
        [[nodiscard]] static int FindDefaultMode(
            const void * accessor
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