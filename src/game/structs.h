#pragma once

// Game structure layouts.

#include <cstddef>
#include <cstdint>

namespace nfs5::game
{
    #pragma pack( push, 1 )

    // EAGL camera, 548 bytes, constructed by 'sub_538D10'.
    // 'SetPerspective' writes the projection at +0x148 and the cull planes at +0xF8.
    struct EaglCamera
    {
        std::uint8_t m_Pad000[0xCC];
        float m_Near; // +0xCC, 204
        float m_Far; // +0xD0, 208
        float m_FovDegrees; // +0xD4, 212, horizontal FOV as passed in.
        float m_Aspect; // +0xD8, 216, vertical scale factor as passed in.
        float m_DepthScale; // +0xDC, 220
        float m_DepthOffset; // +0xE0, 224
        std::uint8_t m_Pad0E4[0xF8 - 0xE4];
        float m_TanHalfHorizontal; // +0xF8, 248
        float m_CosHalfHorizontal; // +0xFC, 252
        float m_TanHalfVertical; // +0x100, 256
        float m_CosHalfVertical; // +0x104, 260
        std::uint8_t m_Pad108[0x148 - 0x108];
        float m_Projection[16]; // +0x148, 328
        std::uint8_t m_Pad188[0x1D0 - 0x188];
        std::int32_t m_ProjectionSerial; // +0x1D0, 464
        std::uint8_t m_Pad1D4[0x200 - 0x1D4];
        float m_ViewportScaleX; // +0x200, 512
        float m_ViewportScaleY; // +0x204, 516
        float m_ViewportOffsetX; // +0x208, 520
        float m_ViewportOffsetY; // +0x20C, 524
        std::uint8_t m_Pad210[0x220 - 0x210];
        std::int32_t m_ProjectionType; // +0x220, 544, '0' = perspective, '1' = ortho.
    };

    // One slot of the EA file layer table, 32 bytes, see sub_5919A0
    struct FileEntry
    {
        std::uint8_t m_InUse; // +0x00
        std::uint8_t m_DriveIndex; // +0x01
        std::uint8_t m_Busy; // +0x02, cleared by another thread to abort a read.
        std::uint8_t m_Pad03;
        void * m_Handle; // +0x04, Win32 file handle
        std::uint32_t m_Flags; // +0x08, open flags, 0x800 means mapped
        std::uint32_t m_BlockSize; // +0x0C, chunk size for 'ReadFile' and 'WriteFile' loops.
        void * m_Mapping; // +0x10
        void * m_View; // +0x14
        std::uint32_t m_Position; // +0x18
        std::uint32_t m_Size; // +0x1C
    };

    // EA file open flags as decoded from 'sub_5919A0'.
    namespace file_flag
    {
        inline constexpr std::uint32_t READ = 0x001;
        inline constexpr std::uint32_t WRITE = 0x002;
        inline constexpr std::uint32_t SHARE_READ = 0x004;
        inline constexpr std::uint32_t SHARE_WRITE = 0x008;
        inline constexpr std::uint32_t NO_BUFFERING = 0x100;
        inline constexpr std::uint32_t SEQUENTIAL_SCAN = 0x200;
        inline constexpr std::uint32_t WRITE_THROUGH = 0x400;
        inline constexpr std::uint32_t MEMORY_MAPPED = 0x800;
    }

    // Display mode record, 40 bytes, array at 'mode_object + 8'.
    struct DisplayMode
    {
        std::int32_t m_Width; // +0x00
        std::int32_t m_Height; // +0x04
        std::int32_t m_Bpp; // +0x08
        std::uint8_t m_Pad0C[0x18 - 0x0C];
        std::int32_t m_Capability; // +0x18, must be at least 2.
        std::uint8_t m_Pad1C[0x28 - 0x1C];
    };

    // World transform shared by cars and race cameras, position then a 3x3 rotation.
    struct Transform
    {
        float m_Position[3];
        float m_Rotation[9];
    };

    // Car object, only the transform is mapped.
    // 'R3DObject' draw methods [sub_4432B0 and sub_443490] and the camera code [sub_464F80]
    // read both fields at render time, so a value written before the frame is what is drawn.
    struct CarObject
    {
        std::uint8_t m_Pad000[0x7F];
        std::uint8_t m_Active; // +0x7F, 127
        std::uint8_t m_Pad080[0x330 - 0x80];
        float m_Position[3]; // +0x330, 816
        std::uint8_t m_Pad33C[0x364 - 0x33C];
        float m_Rotation[9]; // +0x364, 868
    };

    // Race camera block, 268 bytes, array of 'CAMERA_BLOCK_COUNT' at 'CAMERA_BLOCKS'.
    // Position is written by the camera directors every simulation step, the rotation every
    // second step and again by the race renderer on every frame.
    struct CameraBlock
    {
        CarObject * m_Target; // +0x00
        std::uint8_t m_Pad004[0x24 - 0x04];
        float m_Position[3]; // +0x24
        std::uint8_t m_Pad030[0x7C - 0x30];
        float m_Rotation[9]; // +0x7C
        std::uint8_t m_PadA0[268 - 0xA0];
    };

    // Font texture object of the THRASH font driver, 156 bytes [sub_571B90].
    // The glyph sheet is split into square power of two tiles stacked vertically,
    // glyph UVs are texels and are normalised by the tile size when drawn.
    struct FontTexture
    {
        std::int32_t m_Width; // +0x00, glyph sheet width.
        std::int32_t m_Height; // +0x04, glyph sheet height.
        std::int32_t m_Format; // +0x08, EA texture format, 1 is 4 bit palettized.
        std::int32_t m_Bits; // +0x0C
        std::int32_t m_TileSize; // +0x10, tile width and height.
        std::int32_t m_TileCount; // +0x14

        struct
        {
            std::int32_t m_Top;
            std::int32_t m_Height;
        } m_Tiles[10]; // +0x18

        void * m_Textures[10]; // +0x68, dtexture entry per tile.
        std::int32_t m_Generation; // +0x90, dtexture generation the tiles belong to.
        std::uint8_t m_Pad094[0x9C - 0x94];
    };

    // THRASH vertex as filled by the font code, 32 bytes.
    struct ThrashVertex
    {
        float m_X;
        float m_Y;
        float m_Z;
        float m_W;
        std::uint32_t m_Color;
        std::uint32_t m_Specular;
        float m_U;
        float m_V;
    };

    #pragma pack( pop )
}