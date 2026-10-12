#include "detail.h"

#include <algorithm>
#include <cstring>

#include "../../core/log.h"
#include "../../game/addresses.h"
#include "../../core/win_handle.h"

namespace nfs5
{
    namespace
    {
        // Wheel LOD per detail level and body LOD, 3 rows of 7, LOD 5 and 6 map to themselves.
        constexpr std::array< std::int32_t, 21 > STOCK_WHEEL_TABLE =
        {
            0, 0, 1, 1, 1, 5, 6,
            0, 0, 0, 0, 1, 5, 6,
            0, 0, 0, 0, 1, 5, 6
        };

        constexpr float STOCK_FAR_CLIP = 700.0f;

        // Depth precision with the 0.15 near plane gets poor beyond this.
        constexpr float MAX_FAR_CLIP = 20000.0f;
    }

    Detail & Detail::GetInstance()
    {
        static Detail instance;
        return instance;
    }

    double __cdecl Detail::DrawDistanceHook(
        float distance
    )
    {
        Detail & self = GetInstance();

        // The original multiplies by the view distance option and keeps at least 50.
        return self.m_DrawDistanceHook->ccall< double >( distance * self.m_Settings.m_DrawDistanceScale );
    }


    int __cdecl Detail::TextureDetailHook()
    {
        Detail & self = GetInstance();
        const int detected = self.m_TextureDetailHook->ccall<int>();

        LOG_INFO( "Texture detail detected {} used {}", detected, self.m_Settings.m_TextureDetail );
        return self.m_Settings.m_TextureDetail;
    }


    void Detail::CarLodOffsetHook(
        safetyhook::Context & context
    )
    {
        // First usable LOD, the distance based choice still picks simpler models further away.
        context.ebx = 0;
    }


    bool Detail::PatchWheelTable()
    {
        auto * table = hook::Ptr<std::int32_t>( game::detail::WHEEL_LOD_TABLE );

        if (std::memcmp( table, STOCK_WHEEL_TABLE.data(), sizeof( STOCK_WHEEL_TABLE ) ) != 0)
        {
            LOG_ERROR( "Wheel LOD table does not match, distant cars keep the game wheels" );
            return false;
        }

        ScopedPageProtection protection( table, sizeof( STOCK_WHEEL_TABLE ) );
        std::memcpy( m_OriginalWheelTable.data(), table, sizeof( STOCK_WHEEL_TABLE ) );

        // The two farthest body LODs take the wheels of LOD 1, wheels are placed per wheel
        // transform and not by the body mesh.
        for (std::size_t i = 0; i < STOCK_WHEEL_TABLE.size(); ++i)
        {
            if (table[i] >= 5)
            {
                table[i] = 1;
            }
        }

        m_WheelTablePatched = true;
        return true;
    }


    void Detail::RestoreWheelTable()
    {
        if (!m_WheelTablePatched)
            return;

        auto * table = hook::Ptr<std::int32_t>( game::detail::WHEEL_LOD_TABLE );

        ScopedPageProtection protection( table, sizeof( STOCK_WHEEL_TABLE ) );
        std::memcpy( table, m_OriginalWheelTable.data(), sizeof( STOCK_WHEEL_TABLE ) );

        m_WheelTablePatched = false;
    }


    bool Detail::Install(
        const DetailSettings & settings
    )
    {
        if (m_Installed)
        {
            return true;
        }

        m_Settings = settings;

        if (!settings.m_Enabled)
        {
            LOG_INFO( "Detail subsystem disabled by config" );
            return true;
        }

        namespace site = game::detail;
        bool ok = true;

        if (settings.m_DrawDistanceScale > 1.0f)
        {
            m_DrawDistanceHook = hook::CreateInline( "DrawDistance", site::DRAW_DISTANCE.m_Address, site::DRAW_DISTANCE.m_Signature,
                reinterpret_cast<void *>(&DrawDistanceHook) );
            ok &= m_DrawDistanceHook != nullptr;
        }

        // The far plane has to cover the longer draw distance, otherwise cars and objects
        // past 700 are cut by the frustum test even though the track is drawn.
        const float far_clip = std::clamp( STOCK_FAR_CLIP * std::max( 1.0f, settings.m_DrawDistanceScale ) * settings.m_FarClipMultiplier,
            STOCK_FAR_CLIP, MAX_FAR_CLIP );

        if (far_clip > STOCK_FAR_CLIP)
        {
            std::uint32_t bits = 0;
            std::memcpy( &bits, &far_clip, sizeof( bits ) );

            ok &= m_FarClipPatch.Apply( "FarClip", site::FAR_CLIP.m_Address, site::FAR_CLIP.m_Signature, site::FAR_CLIP_OPERAND, bits );
            ok &= m_FarClipResetPatch.Apply( "FarClipReset", site::FAR_CLIP_RESET.m_Address, site::FAR_CLIP_RESET.m_Signature,
                site::FAR_CLIP_RESET_OPERAND, bits );
        }

        if (settings.m_KeepDistantWheels)
        {
            ok &= PatchWheelTable();
        }

        if (settings.m_TextureDetail >= 0)
        {
            m_TextureDetailHook = hook::CreateInline( "TextureDetail", site::TEXTURE_DETAIL_DETECT.m_Address,
                site::TEXTURE_DETAIL_DETECT.m_Signature, reinterpret_cast<void *>(&TextureDetailHook) );
            ok &= m_TextureDetailHook != nullptr;
        }

        if (settings.m_FullCarDetail)
        {
            m_CarLodOffsetHook = hook::CreateMid( "CarLodOffset", site::CAR_LOD_OFFSET.m_Address, site::CAR_LOD_OFFSET.m_Signature,
                &CarLodOffsetHook );
            ok &= m_CarLodOffsetHook != nullptr;
        }

        m_Installed = true;

        LOG_INFO( "Detail installed, draw distance x{:.2f} far clip {:.0f} distant wheels {} all ok {}", settings.m_DrawDistanceScale, far_clip,
            m_WheelTablePatched, ok );
        return ok;
    }


    void Detail::Uninstall()
    {
        RestoreWheelTable();

        m_CarLodOffsetHook.reset();
        m_TextureDetailHook.reset();
        m_FarClipResetPatch.Restore();
        m_FarClipPatch.Restore();
        m_DrawDistanceHook.reset();
        m_Installed = false;
    }
}