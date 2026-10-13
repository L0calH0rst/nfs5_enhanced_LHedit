#pragma once

// EA texture formats as the game hands them to 'talloc' and 'tupdate'.

#include <dxgiformat.h>

#include <cstddef>
#include <cstdint>

namespace dx11z::formats
{
    inline constexpr int P4 = 1;
    inline constexpr int P8 = 2;
    inline constexpr int ARGB1555 = 3;
    inline constexpr int RGB565 = 4;
    inline constexpr int RGB888 = 5;
    inline constexpr int ARGB8888 = 6;
    inline constexpr int ARGB4444 = 7;
    inline constexpr int DXT1 = 12;
    inline constexpr int DXT3 = 13;

    [[nodiscard]] bool IsBlockCompressed(
        int format
    );

    [[nodiscard]] DXGI_FORMAT ToDxgi(
        int format
    );

    // Size in bytes of one mip level as packed in the game data.
    [[nodiscard]] std::size_t LevelSize(
        int format,
        int width,
        int height
    );

    // Row pitch of one level as uploaded to D3D11.
    [[nodiscard]] std::size_t UploadPitch(
        int format,
        int width
    );

    // Converts one level to the upload layout, B8G8R8A8 or the block data as is.
    // The palette holds 256 A8R8G8B8 entries, alpha is ignored as in dx7z.
    void ConvertLevel(
        int format,
        int width,
        int height,
        const std::uint8_t * source,
        const std::uint32_t * palette,
        std::uint8_t * destination
    );
}