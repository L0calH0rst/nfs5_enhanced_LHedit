#pragma once

// Draw distance and level of detail.
// The race draw distance is the per track position visibility from 'sub_44FCB0' times the
// view distance option [0.5 to 1.0], computed in 'sub_44F1A0' and stored at 6197CC. The
// track renderer [sub_47F550] draws sections up to that distance and objects use it for
// culling. Every view is created with a far clip plane of 700 from the render context,
// cars beyond it are frustum culled. Both are raised together here.
// Distant cars switch to LOD 5 and 6 which have no wheel parts, the wheel LOD table
// 5CDB44 can point those at the LOD 1 wheels instead.

#include <array>
#include <cstdint>
#include <memory>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"

namespace nfs5
{
    class Detail
    {
    private:

        static constexpr std::size_t WHEEL_TABLE_SIZE = 21;

        DetailSettings m_Settings;
        std::unique_ptr<safetyhook::InlineHook> m_DrawDistanceHook;
        hook::OperandPatch m_FarClipPatch;
        hook::OperandPatch m_FarClipResetPatch;
        std::unique_ptr<safetyhook::MidHook> m_CarLodOffsetHook;
        std::unique_ptr<safetyhook::InlineHook> m_TextureDetailHook;
        std::array<std::int32_t, WHEEL_TABLE_SIZE> m_OriginalWheelTable = {};
        bool m_WheelTablePatched = false;
        bool m_Installed = false;

        Detail() = default;

        static double __cdecl DrawDistanceHook(
            float distance
        );

        static int __cdecl TextureDetailHook();

        static void CarLodOffsetHook(
            safetyhook::Context & context
        );

        bool PatchWheelTable();

        void RestoreWheelTable();

    public:

        Detail( const Detail & ) = delete;
        Detail & operator=( const Detail & ) = delete;

        static Detail & GetInstance();

        bool Install(
            const DetailSettings & settings
        );

        void Uninstall();
    };
}