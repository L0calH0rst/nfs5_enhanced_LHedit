#include "formats.h"

#include <algorithm>
#include <cstring>

namespace dx11z::formats
{
    namespace
    {
        [[nodiscard]] std::uint32_t Expand(
            std::uint32_t value,
            int bits
        )
        {
            const std::uint32_t max = (1u << bits) - 1u;
            return (value * 255u + max / 2u) / max;
        }


        [[nodiscard]] std::uint32_t Pack(
            std::uint32_t a,
            std::uint32_t r,
            std::uint32_t g,
            std::uint32_t b
        )
        {
            return (a << 24) | (r << 16) | (g << 8) | b;
        }
    }


    bool IsBlockCompressed(
        int format
    )
    {
        return format == DXT1 || format == DXT3;
    }


    DXGI_FORMAT ToDxgi(
        int format
    )
    {
        if (format == DXT1)
        {
            return DXGI_FORMAT_BC1_UNORM;
        }

        if (format == DXT3)
        {
            return DXGI_FORMAT_BC2_UNORM;
        }

        return DXGI_FORMAT_B8G8R8A8_UNORM;
    }


    std::size_t LevelSize(
        int format,
        int width,
        int height
    )
    {
        const std::size_t w = static_cast<std::size_t>(std::max( width, 1 ));
        const std::size_t h = static_cast<std::size_t>(std::max( height, 1 ));

        switch (format)
        {
            case P4:
                return (w * h + 1) / 2;

            case P8:
                return w * h;

            case ARGB1555:
            case RGB565:
            case ARGB4444:
                return w * h * 2;

            case RGB888:
                return w * h * 3;

            case DXT1:
                return ((w + 3) / 4) * ((h + 3) / 4) * 8;

            case DXT3:
                return ((w + 3) / 4) * ((h + 3) / 4) * 16;

            default:
                return w * h * 4;
        }
    }


    std::size_t UploadPitch(
        int format,
        int width
    )
    {
        const std::size_t w = static_cast<std::size_t>(std::max( width, 1 ));

        if (format == DXT1)
        {
            return ((w + 3) / 4) * 8;
        }

        if (format == DXT3)
        {
            return ((w + 3) / 4) * 16;
        }

        return w * 4;
    }


    void ConvertLevel(
        int format,
        int width,
        int height,
        const std::uint8_t * source,
        const std::uint32_t * palette,
        std::uint8_t * destination
    )
    {
        const std::size_t count = static_cast<std::size_t>(std::max( width, 1 )) * static_cast<std::size_t>(std::max( height, 1 ));
        auto * out = reinterpret_cast<std::uint32_t *>(destination);

        switch (format)
        {
            case DXT1:
            case DXT3:
            case ARGB8888:
                std::memcpy( destination, source, LevelSize( format, width, height ) );
                return;

            case P4:
                // The dtexture layer swaps the nibbles before upload, the low nibble is the left pixel.
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint8_t packed = source[i / 2];
                    const std::uint32_t index = (i & 1) != 0 ? (packed >> 4) : (packed & 0x0F);
                    out[i] = palette != nullptr ? (palette[index] | 0xFF000000u) : Pack( 255, index * 17, index * 17, index * 17 );
                }
                return;

            case P8:
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::uint32_t index = source[i];
                    out[i] = palette != nullptr ? (palette[index] | 0xFF000000u) : Pack( 255, index, index, index );
                }
                return;

            case ARGB1555:
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::uint16_t v;
                    std::memcpy( &v, source + i * 2, 2 );
                    out[i] = Pack( (v & 0x8000) != 0 ? 255 : 0, Expand( (v >> 10) & 31, 5 ), Expand( (v >> 5) & 31, 5 ), Expand( v & 31, 5 ) );
                }
                return;

            case RGB565:
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::uint16_t v;
                    std::memcpy( &v, source + i * 2, 2 );
                    out[i] = Pack( 255, Expand( (v >> 11) & 31, 5 ), Expand( (v >> 5) & 63, 6 ), Expand( v & 31, 5 ) );
                }
                return;

            case ARGB4444:
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::uint16_t v;
                    std::memcpy( &v, source + i * 2, 2 );
                    out[i] = Pack( Expand( (v >> 12) & 15, 4 ), Expand( (v >> 8) & 15, 4 ), Expand( (v >> 4) & 15, 4 ), Expand( v & 15, 4 ) );
                }
                return;

            case RGB888:
                for (std::size_t i = 0; i < count; ++i)
                {
                    out[i] = Pack( 255, source[i * 3 + 2], source[i * 3 + 1], source[i * 3] );
                }
                return;

            default:
                std::memset( destination, 0xFF, count * 4 );
                return;
        }
    }
}