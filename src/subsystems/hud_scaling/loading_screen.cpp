#include "loading_screen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../core/log.h"

#include "../../game/addresses.h"

namespace nfs5
{
    namespace
    {
        constexpr std::size_t SHAPE_HEADER_SIZE = 16;

        using ShapeBlitFn = void( __cdecl * )(
            const std::uint8_t * shape,
            std::int32_t x,
            std::int32_t y
            );

        struct Field
        {
            std::uint32_t m_Shift;
            std::uint32_t m_Bits;
        };

        struct PixelLayout
        {
            std::uint8_t m_Type;
            std::int32_t m_Bytes;
            Field m_Fields[4];
            std::int32_t m_FieldCount;
        };

        // Shape types with a known channel layout, others are not scaled.
        constexpr PixelLayout LAYOUTS[] =
        {
            { 0x78, 2, { { 11, 5 }, { 5, 6 }, { 0, 5 }, { 0, 0 } }, 3 }, // 565
            { 0x7E, 2, { { 15, 1 }, { 10, 5 }, { 5, 5 }, { 0, 5 } }, 4 }, // 1555
            { 0x6D, 2, { { 12, 4 }, { 8, 4 }, { 4, 4 }, { 0, 4 } }, 4 }, // 4444
            { 0x7D, 4, { { 24, 8 }, { 16, 8 }, { 8, 8 }, { 0, 8 } }, 4 }, // 8888
        };

        const PixelLayout * FindLayout(
            std::uint8_t type
        )
        {
            for (const PixelLayout & layout : LAYOUTS)
            {
                if (layout.m_Type == type)
                {
                    return &layout;
                }
            }
            return nullptr;
        }


        std::uint32_t Load(
            const std::uint8_t * pixel,
            std::int32_t bytes
        )
        {
            if (bytes == 2)
            {
                std::uint16_t value = 0;
                std::memcpy( &value, pixel, sizeof( value ) );
                return value;
            }

            std::uint32_t value = 0;
            std::memcpy( &value, pixel, sizeof( value ) );
            return value;
        }


        void Store(
            std::uint8_t * pixel,
            std::int32_t bytes,
            std::uint32_t value
        )
        {
            if (bytes == 2)
            {
                const auto narrow = static_cast<std::uint16_t>(value);
                std::memcpy( pixel, &narrow, sizeof( narrow ) );
                return;
            }

            std::memcpy( pixel, &value, sizeof( value ) );
        }


        // Blends two packed pixels channel by channel, weight of 'b' from 0 to 256.
        std::uint32_t Lerp(
            const PixelLayout & layout,
            std::uint32_t a,
            std::uint32_t b,
            std::uint32_t weight
        )
        {
            if (weight == 0 || a == b)
            {
                return a;
            }

            if (weight >= 256)
            {
                return b;
            }

            std::uint32_t result = 0;

            for (std::int32_t i = 0; i < layout.m_FieldCount; ++i)
            {
                const Field & field = layout.m_Fields[i];

                const std::uint32_t mask = (1u << field.m_Bits) - 1u;
                const std::uint32_t channel_a = (a >> field.m_Shift) & mask;
                const std::uint32_t channel_b = (b >> field.m_Shift) & mask;
                const std::uint32_t channel = (channel_a * (256u - weight) + channel_b * weight + 128u) >> 8;

                result |= (channel & mask) << field.m_Shift;
            }

            return result;
        }
    }

    LoadingScreen & LoadingScreen::GetInstance()
    {
        static LoadingScreen instance;
        return instance;
    }

    void LoadingScreen::Prepare(
        const std::uint8_t * shape,
        std::int32_t target_width,
        std::int32_t target_height,
        std::int32_t bytes_per_pixel
    )
    {
        m_Type = shape[0];

        m_SourceWidth = *reinterpret_cast<const std::int16_t *>(shape + 4);
        m_SourceHeight = *reinterpret_cast<const std::int16_t *>(shape + 6);
        m_TargetWidth = target_width;
        m_TargetHeight = target_height;

        m_Scaled.assign( SHAPE_HEADER_SIZE + static_cast<std::size_t>(target_width) * target_height * bytes_per_pixel, 0 );
        std::memcpy( m_Scaled.data(), shape, SHAPE_HEADER_SIZE );

        const auto width16 = static_cast<std::int16_t>(target_width);
        const auto height16 = static_cast<std::int16_t>(target_height);

        std::memcpy( m_Scaled.data() + 4, &width16, sizeof( width16 ) );
        std::memcpy( m_Scaled.data() + 6, &height16, sizeof( height16 ) );

        // The source is a window with no center or position offsets, keep it that way.
        std::memset( m_Scaled.data() + 8, 0, SHAPE_HEADER_SIZE - 8 );

        // Sample positions use pixel centres so the edges are not shifted.
        const auto build_taps = [this] ( std::vector< Tap > & taps, std::int32_t source, std::int32_t target )
        {
            taps.resize( static_cast<std::size_t>(target) );
            const double ratio = static_cast<double>(source) / static_cast<double>(target);
            for (std::int32_t i = 0; i < target; ++i)
            {
                Tap & tap = taps[static_cast<std::size_t>( i )];

                if (!m_Settings.m_SmoothLoadingScreen)
                {
                    tap.m_Index0 = std::clamp( static_cast<std::int32_t>( (i + 0.5) * ratio ), 0, source - 1 );
                    tap.m_Index1 = tap.m_Index0;
                    tap.m_Weight = 0;
                    continue;
                }

                const double position = std::max( 0.0, (i + 0.5) * ratio - 0.5 );
                const auto index = static_cast<std::int32_t>(position);
                tap.m_Index0 = std::min( index, source - 1 );
                tap.m_Index1 = std::min( index + 1, source - 1 );
                tap.m_Weight = static_cast<std::uint32_t>(std::lround( (position - index) * 256.0 ));
            }
        };

        build_taps( m_ColumnTaps, m_SourceWidth, target_width );
        build_taps( m_RowTaps, m_SourceHeight, target_height );

        // Force a full rescale on the first use.
        m_PreviousSource.clear();
        m_DirtyRows.assign( static_cast<std::size_t>(m_SourceHeight), 1 );

        LOG_INFO( "Loading screen {}x{} type {:02X} shown as {}x{}", m_SourceWidth, m_SourceHeight, m_Type, target_width, target_height );
    }


    void LoadingScreen::ScaleRow(
        const std::uint8_t * source,
        std::int32_t source_pitch,
        std::int32_t row,
        std::int32_t bytes_per_pixel
    )
    {
        const PixelLayout & layout = *FindLayout( m_Type );

        const Tap & row_tap = m_RowTaps[static_cast<std::size_t>(row)];
        const std::uint8_t * row0 = source + static_cast<std::ptrdiff_t>(row_tap.m_Index0) * source_pitch;
        const std::uint8_t * row1 = source + static_cast<std::ptrdiff_t>(row_tap.m_Index1) * source_pitch;

        std::uint8_t * out = m_Scaled.data() + SHAPE_HEADER_SIZE + static_cast<std::size_t>(row) * m_TargetWidth * bytes_per_pixel;

        for (std::int32_t column = 0; column < m_TargetWidth; ++column)
        {
            const Tap & column_tap = m_ColumnTaps[static_cast<std::size_t>( column )];
            const std::ptrdiff_t offset0 = static_cast<std::ptrdiff_t>( column_tap.m_Index0 ) * bytes_per_pixel;
            const std::ptrdiff_t offset1 = static_cast<std::ptrdiff_t>( column_tap.m_Index1 ) * bytes_per_pixel;
            const std::uint32_t top = Lerp( layout, Load( row0 + offset0, bytes_per_pixel ), Load( row0 + offset1, bytes_per_pixel ), column_tap.m_Weight );
            std::uint32_t value = top;

            if (row_tap.m_Weight != 0)
            {
                const std::uint32_t bottom =
                    Lerp( layout, Load( row1 + offset0, bytes_per_pixel ), Load( row1 + offset1, bytes_per_pixel ), column_tap.m_Weight );
                value = Lerp( layout, top, bottom, row_tap.m_Weight );
            }

            Store( out + static_cast<std::ptrdiff_t>(column) * bytes_per_pixel, bytes_per_pixel, value );
        }
    }


    const std::uint8_t * LoadingScreen::Scale(
        const std::uint8_t * shape,
        std::int32_t screen_width,
        std::int32_t screen_height
    )
    {
        const std::uint8_t type = shape[0];
        const PixelLayout * layout = FindLayout( type );

        if (layout == nullptr)
            return nullptr;

        const auto * bits_table = hook::Ptr<const std::uint8_t>( game::hud::SHAPE_BITS_TABLE );
        const std::int32_t bits = bits_table[type & 0x7F];
        const std::int32_t bytes_per_pixel = (bits == 15 ? 16 : bits) / 8;

        if (bytes_per_pixel != layout->m_Bytes)
            return nullptr;

        const std::int32_t source_width = *reinterpret_cast<const std::int16_t *>(shape + 4);
        const std::int32_t source_height = *reinterpret_cast<const std::int16_t *>(shape + 6);

        if (source_width <= 0 || source_height <= 0 || screen_width <= source_width || screen_height <= source_height)
            return nullptr;

        // Fit inside the display at the source aspect, the width stays a multiple of 8
        // like every window the game creates.
        const double scale = std::min( static_cast<double>(screen_width) / source_width, static_cast<double>(screen_height) / source_height );
        const std::int32_t target_width = std::min( screen_width, static_cast<std::int32_t>(std::lround( source_width * scale )) ) & ~7;
        const std::int32_t target_height = std::min( screen_height, static_cast<std::int32_t>(std::lround( source_height * scale )) );

        if (target_width > 0x7FFF || target_height > 0x7FFF)
            return nullptr;

        if (type != m_Type || source_width != m_SourceWidth || source_height != m_SourceHeight || target_width != m_TargetWidth ||
            target_height != m_TargetHeight)
        {
            Prepare( shape, target_width, target_height, bytes_per_pixel );
        }

        // Only rows that changed since the last blit are scaled again.
        const std::int32_t source_pitch = source_width * bytes_per_pixel;
        const std::uint8_t * pixels = shape + SHAPE_HEADER_SIZE;
        const std::size_t source_size = static_cast<std::size_t>(source_pitch) * source_height;
        const bool first = m_PreviousSource.size() != source_size;

        if (first)
        {
            m_PreviousSource.assign( pixels, pixels + source_size );
            std::fill( m_DirtyRows.begin(), m_DirtyRows.end(), 1 );
        }
        else
        {
            for (std::int32_t row = 0; row < source_height; ++row)
            {
                const std::size_t offset = static_cast<std::size_t>( row ) * source_pitch;

                if (std::memcmp( pixels + offset, m_PreviousSource.data() + offset, static_cast<std::size_t>( source_pitch ) ) != 0)
                {
                    std::memcpy( m_PreviousSource.data() + offset, pixels + offset, static_cast<std::size_t>( source_pitch ) );
                    m_DirtyRows[static_cast<std::size_t>( row )] = 1;
                }
            }
        }

        for (std::int32_t row = 0; row < m_TargetHeight; ++row)
        {
            const Tap & tap = m_RowTaps[static_cast<std::size_t>( row )];

            if (m_DirtyRows[static_cast<std::size_t>( tap.m_Index0 )] != 0 || m_DirtyRows[static_cast<std::size_t>( tap.m_Index1 )] != 0)
            {
                ScaleRow( pixels, source_pitch, row, bytes_per_pixel );
            }
        }

        std::fill( m_DirtyRows.begin(), m_DirtyRows.end(), 0 );
        return m_Scaled.data();
    }


    void __cdecl LoadingScreen::BlitHook(
        const std::uint8_t * shape,
        std::int32_t x,
        std::int32_t y
    )
    {
        LoadingScreen & self = GetInstance();
        const auto original = reinterpret_cast<ShapeBlitFn>(self.m_BlitPatch.GetOriginalTarget());

        if (shape == nullptr)
        {
            original( shape, x, y );
            return;
        }

        const std::int32_t screen_width = hook::Ref<std::int32_t>( game::hud::DISPLAY_WIDTH );
        const std::int32_t screen_height = hook::Ref<std::int32_t>( game::hud::DISPLAY_HEIGHT );
        const std::uint8_t * scaled = self.Scale( shape, screen_width, screen_height );

        if (scaled == nullptr)
        {
            original( shape, x, y );
            return;
        }

        original( scaled, (screen_width - self.m_TargetWidth) / 2, (screen_height - self.m_TargetHeight) / 2 );
    }


    bool LoadingScreen::Install(
        const HudSettings & settings
    )
    {
        if (m_Installed)
            return true;

        m_Settings = settings;

        if (!settings.m_Enabled || !settings.m_ScaleLoadingScreen)
            return true;

        namespace hud = game::hud;
        if (!hook::MatchBytes( hud::LOADING_BLIT_CALL.m_Address, hud::LOADING_BLIT_CALL.m_Signature ) ||
            !hook::MatchBytes( hud::SHAPE_BLIT.m_Address, hud::SHAPE_BLIT.m_Signature ))
        {
            LOG_ERROR( "Loading screen blit does not match, the loading screen keeps its size" );
            return false;
        }

        if (!m_BlitPatch.Apply( "LoadingScreenBlit", hud::LOADING_BLIT_CALL.m_Address, reinterpret_cast<void *>(&BlitHook) ))
            return false;

        m_Installed = true;

        LOG_INFO( "Loading screen scaling installed, smoothing {}", settings.m_SmoothLoadingScreen );
        return true;
    }

    void LoadingScreen::Uninstall()
    {
        m_BlitPatch.Restore();
        m_Installed = false;
    }
}