#pragma once

// D3D7 render state and texture stage state values the THRASH interface is defined in.
// 'dx7z.dll' translates every THRASH state into these, the D3D11 driver keeps the same
// values and emulates them, so the mapping in 'setstate' stays a direct port.

#include <array>
#include <cstdint>

namespace dx11z
{
    namespace rs
    {
        inline constexpr int ANTIALIAS = 2;
        inline constexpr int ZENABLE = 7;
        inline constexpr int FILLMODE = 8;
        inline constexpr int SHADEMODE = 9;
        inline constexpr int ZWRITEENABLE = 14;
        inline constexpr int ALPHATESTENABLE = 15;
        inline constexpr int SRCBLEND = 19;
        inline constexpr int DESTBLEND = 20;
        inline constexpr int CULLMODE = 22;
        inline constexpr int ZFUNC = 23;
        inline constexpr int ALPHAREF = 24;
        inline constexpr int ALPHAFUNC = 25;
        inline constexpr int DITHERENABLE = 26;
        inline constexpr int ALPHABLENDENABLE = 27;
        inline constexpr int FOGENABLE = 28;
        inline constexpr int SPECULARENABLE = 29;
        inline constexpr int FOGCOLOR = 34;
        inline constexpr int FOGTABLEMODE = 35;
        inline constexpr int FOGSTART = 36;
        inline constexpr int FOGEND = 37;
        inline constexpr int FOGDENSITY = 38;
        inline constexpr int COLORKEYENABLE = 41;
        inline constexpr int ZBIAS = 47;
        inline constexpr int STENCILENABLE = 52;
        inline constexpr int STENCILFAIL = 53;
        inline constexpr int STENCILZFAIL = 54;
        inline constexpr int STENCILPASS = 55;
        inline constexpr int STENCILFUNC = 56;
        inline constexpr int STENCILREF = 57;
        inline constexpr int STENCILMASK = 58;
        inline constexpr int STENCILWRITEMASK = 59;
        inline constexpr int TEXTUREFACTOR = 60;
        inline constexpr int FOGVERTEXMODE = 140;
        inline constexpr int COUNT = 256;
    }

    namespace tss
    {
        inline constexpr int COLOROP = 1;
        inline constexpr int COLORARG1 = 2;
        inline constexpr int COLORARG2 = 3;
        inline constexpr int ALPHAOP = 4;
        inline constexpr int ALPHAARG1 = 5;
        inline constexpr int ALPHAARG2 = 6;
        inline constexpr int TEXCOORDINDEX = 11;
        inline constexpr int ADDRESS = 12;
        inline constexpr int ADDRESSU = 13;
        inline constexpr int ADDRESSV = 14;
        inline constexpr int MAGFILTER = 16;
        inline constexpr int MINFILTER = 17;
        inline constexpr int MIPFILTER = 18;
        inline constexpr int MIPMAPLODBIAS = 19;
        inline constexpr int MAXANISOTROPY = 21;
        inline constexpr int COUNT = 32;
    }

    // D3DTEXTUREOP.
    namespace top
    {
        inline constexpr int DISABLE = 1;
        inline constexpr int SELECTARG1 = 2;
        inline constexpr int SELECTARG2 = 3;
        inline constexpr int MODULATE = 4;
        inline constexpr int MODULATE2X = 5;
        inline constexpr int MODULATE4X = 6;
        inline constexpr int ADD = 7;
        inline constexpr int ADDSIGNED = 8;
        inline constexpr int ADDSIGNED2X = 9;
        inline constexpr int SUBTRACT = 10;
        inline constexpr int ADDSMOOTH = 11;
        inline constexpr int BLENDDIFFUSEALPHA = 12;
        inline constexpr int BLENDTEXTUREALPHA = 13;
        inline constexpr int BLENDFACTORALPHA = 14;
        inline constexpr int BLENDTEXTUREALPHAPM = 15;
        inline constexpr int BLENDCURRENTALPHA = 16;
    }

    // D3DTA.
    namespace ta
    {
        inline constexpr int DIFFUSE = 0;
        inline constexpr int CURRENT = 1;
        inline constexpr int TEXTURE = 2;
        inline constexpr int TFACTOR = 3;
        inline constexpr int SPECULAR = 4;
        inline constexpr int COMPLEMENT = 0x10;
        inline constexpr int ALPHAREPLICATE = 0x20;
    }

    inline constexpr int MAX_STAGES = 2;

    struct D3D7State
    {
        std::array<std::uint32_t, rs::COUNT> m_Render = {};
        std::array<std::array<std::uint32_t, tss::COUNT>, MAX_STAGES> m_Stage = {};

        void Reset()
        {
            m_Render.fill( 0 );
            m_Render[rs::ZENABLE] = 1;
            m_Render[rs::FILLMODE] = 3;
            m_Render[rs::SHADEMODE] = 2;
            m_Render[rs::ZWRITEENABLE] = 1;
            m_Render[rs::SRCBLEND] = 2;
            m_Render[rs::DESTBLEND] = 1;
            m_Render[rs::CULLMODE] = 3;
            m_Render[rs::ZFUNC] = 4;
            m_Render[rs::ALPHAFUNC] = 8;
            m_Render[rs::FOGEND] = 0x3F800000;
            m_Render[rs::FOGDENSITY] = 0x3F800000;
            m_Render[rs::STENCILFAIL] = 1;
            m_Render[rs::STENCILZFAIL] = 1;
            m_Render[rs::STENCILPASS] = 1;
            m_Render[rs::STENCILFUNC] = 8;
            m_Render[rs::STENCILMASK] = 0xFFFFFFFF;
            m_Render[rs::STENCILWRITEMASK] = 0xFFFFFFFF;
            m_Render[rs::TEXTUREFACTOR] = 0xFFFFFFFF;

            for (std::size_t stage = 0; stage < m_Stage.size(); ++stage)
            {
                auto & s = m_Stage[stage];

                s.fill( 0 );
                s[tss::COLOROP] = stage == 0 ? top::MODULATE : top::DISABLE;
                s[tss::COLORARG1] = ta::TEXTURE;
                s[tss::COLORARG2] = ta::CURRENT;
                s[tss::ALPHAOP] = stage == 0 ? top::SELECTARG1 : top::DISABLE;
                s[tss::ALPHAARG1] = ta::TEXTURE;
                s[tss::ALPHAARG2] = ta::CURRENT;
                s[tss::TEXCOORDINDEX] = static_cast<std::uint32_t>( stage );
                s[tss::ADDRESSU] = 1;
                s[tss::ADDRESSV] = 1;
                s[tss::MAGFILTER] = 1;
                s[tss::MINFILTER] = 1;
                s[tss::MIPFILTER] = 1;
                s[tss::MAXANISOTROPY] = 1;
            }
        }
    };
}