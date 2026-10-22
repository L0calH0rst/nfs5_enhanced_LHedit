#pragma once

// Sharper text.

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <safetyhook.hpp>

#include "../../core/config.h"
#include "../../core/hook_util.h"

#include "../../game/structs.h"

namespace nfs5
{
    class TextQuality
    {
    private:

        static constexpr std::size_t MAX_TILES = 10;

        // Font colors kept as textures per glyph sheet, the least recently used is dropped.
        static constexpr std::size_t MAX_COLOR_SETS = 8;

        using Palette = std::array<std::uint32_t, 16>;
        using TreSetFn = int( __stdcall * )();
        using SetVideoModeFn = int( __stdcall * )(int, int, int);

        struct ColorSet
        {
            Palette m_Palette = {};
            std::array< int, MAX_TILES > m_Handles = {};
            std::uint64_t m_LastUse = 0;
        };

        // Glyph metrics of a prerendered sheet, the quads are set at the TrueType advances.
        struct LayoutGlyph
        {
            float m_GameAdvance = 0.0f; // texels
            float m_GameOffset = 0.0f; // rectangle left from the pen, texels
            float m_Advance = 0.0f;
            float m_Offset = 0.0f;
        };

        struct Sheet
        {
            const std::uint8_t * m_Pixels = nullptr;
            std::int32_t m_Width = 0;
            std::int32_t m_Height = 0;
            std::int32_t m_TileSize = 0;
            std::int32_t m_TileCount = 0;
            std::array<std::int32_t, MAX_TILES> m_TileTops = {};
            std::uint32_t m_Checksum = 0;

            // Palette ramps, 1 for plain fonts, 2 for fonts with a two stage outline ramp.
            std::int32_t m_Segments = 1;

            // Enlarged palette index per tile, 17 per index step.
            std::vector<std::vector<std::uint8_t>> m_Levels;
            Palette m_Palette = {};
            bool m_HavePalette = false;
            bool m_Failed = false;
            std::vector< ColorSet > m_Sets;

            // Keyed by rectangle 'x << 16 | y' in the sheet.
            std::unordered_map< std::uint32_t, LayoutGlyph > m_Layout;
            float m_GameSpace = 0.0f;
            float m_Space = 0.0f;
        };

        TextSettings m_Settings;
        std::unique_ptr<safetyhook::InlineHook> m_UploadHook;
        std::unique_ptr<safetyhook::InlineHook> m_DrawHook;
        std::unique_ptr<safetyhook::InlineHook> m_FreeHook;
        hook::CallSitePatch m_SelectPatch;
        TreSetFn m_OriginalTreset = nullptr;
        SetVideoModeFn m_OriginalSetVideoMode = nullptr;
        std::unordered_map< const game::FontTexture *, Sheet > m_Sheets;
        std::vector<std::uint32_t> m_Staging;
        std::int32_t m_Upscale = 4;
        float m_EdgeSlope = 2.4f;
        std::int32_t m_TextureFormat = 0;
        std::int32_t m_DesktopHeight = 0;
        float m_Magnification = 0.0f;
        std::uint64_t m_UseCounter = 0;
        bool m_Installed = false;

        TextQuality() = default;

        static int __cdecl UploadHook(
            void * font
        );

        static int __cdecl DrawQuadsHook(
            int quad_count,
            game::ThrashVertex * vertices,
            void * indices
        );

        static int __cdecl SelectTileHook(
            void * texture
        );

        static int __cdecl FreeHook(
            game::FontTexture * texture
        );

        static int __stdcall TresetHook();

        static int __stdcall SetVideoModeHook(
            int width,
            int height,
            int bpp
        );

        void OnThrashDriverLoaded();

        void OnUpload(
            const std::uint8_t * font
        );

        void OnSelectTile(
            void * texture
        );

        [[nodiscard]] float DisplayFactor() const;

        [[nodiscard]] bool PickTextureFormat();

        void BuildLevels(
            Sheet & sheet
        );

        // Layout table of the prerendered sheet, read when the font is uploaded.
        void LoadLayout(
            Sheet & sheet
        );

        // Moves glyph quads to the TrueType advances, each run keeps its original width.
        void Relayout(
            const Sheet & sheet,
            game::ThrashVertex * vertices,
            int quad_count,
            float magnification
        ) const;

        // Levels from a prerendered sheet 'fonts\<checksum>_<factor>.lvl' next to the game, see ffn_hd.py.
        [[nodiscard]] bool LoadLevels(
            Sheet & sheet
        );

        ColorSet * FindOrCreateSet(
            Sheet & sheet
        );

        [[nodiscard]] bool UploadSet(
            Sheet & sheet,
            ColorSet & set
        );

        void FreeSet(
            ColorSet & set
        );

        void ReleaseSheet(
            Sheet & sheet,
            bool free_handles
        );

        void ReleaseAll(
            bool free_handles
        );

    public:

        TextQuality( const TextQuality & ) = delete;
        TextQuality & operator=( const TextQuality & ) = delete;

        static TextQuality & GetInstance();

        bool Install(
            const TextSettings & settings
        );

        void Uninstall();
    };
}