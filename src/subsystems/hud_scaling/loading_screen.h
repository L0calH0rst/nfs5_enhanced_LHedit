#pragma once

// Race loading screen scaling.
// The loading screen is composed into a 640x480 window [sub_4A4A70] and blitted 1:1 in
// the middle of the display by the shape blit 'sub_53D570', so at high
// resolutions it is a small box. The blit call is redirected to a copy of the shape
// scaled to fit the display height at 4:3, rows are only rescaled when they changed.

#include <cstdint>
#include <vector>

#include "../../core/config.h"
#include "../../core/hook_util.h"

namespace nfs5
{
    class LoadingScreen
    {
    private:

        struct Tap
        {
            std::int32_t m_Index0 = 0;
            std::int32_t m_Index1 = 0;
            std::uint32_t m_Weight = 0; // Weight of index1, 0 to 256.
        };

        HudSettings m_Settings;
        hook::CallSitePatch m_BlitPatch;
        std::vector< std::uint8_t > m_Scaled;
        std::vector< std::uint8_t > m_PreviousSource;
        std::vector< std::uint8_t > m_DirtyRows;
        std::vector< Tap > m_ColumnTaps;
        std::vector< Tap > m_RowTaps;
        std::int32_t m_SourceWidth = 0;
        std::int32_t m_SourceHeight = 0;
        std::int32_t m_TargetWidth = 0;
        std::int32_t m_TargetHeight = 0;
        std::uint8_t m_Type = 0;
        bool m_Installed = false;

        LoadingScreen() = default;

        static void __cdecl BlitHook(
            const std::uint8_t * shape,
            std::int32_t x,
            std::int32_t y
        );

        // Returns the scaled shape, or nullptr when the shape is kept as it is.
        const std::uint8_t * Scale(
            const std::uint8_t * shape,
            std::int32_t screen_width,
            std::int32_t screen_height
        );

        void Prepare(
            const std::uint8_t * shape,
            std::int32_t target_width,
            std::int32_t target_height,
            std::int32_t bytes_per_pixel
        );

        void ScaleRow(
            const std::uint8_t * source,
            std::int32_t source_pitch,
            std::int32_t row,
            std::int32_t bytes_per_pixel
        );

    public:

        LoadingScreen( const LoadingScreen & ) = delete;
        LoadingScreen & operator=( const LoadingScreen & ) = delete;

        static LoadingScreen & GetInstance();

        bool Install(
            const HudSettings & settings
        );

        void Uninstall();
    };
}