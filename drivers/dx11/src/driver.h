#pragma once

// THRASH driver on Direct3D 11.
// Keeps the D3D7 state dx7z.dll would have set, batches the pre transformed vertices
// and draws them with a fixed function emulation shader into an off screen target at
// the game resolution, which is scaled into a flip model swap chain on pageflip.

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "d3d7_state.h"

namespace dx11z
{
    template <typename T>
    class ComPtr
    {
    private:

        T * m_Pointer = nullptr;

    public:

        ComPtr() = default;

        ComPtr( const ComPtr & other ) : m_Pointer( other.m_Pointer )
        {
            if (m_Pointer)
            {
                m_Pointer->AddRef();
            }
        }

        ComPtr & operator=( const ComPtr & other )
        {
            if (this != &other)
            {
                Reset();
                m_Pointer = other.m_Pointer;

                if (m_Pointer)
                {
                    m_Pointer->AddRef();
                }
            }

            return *this;
        }

        ~ComPtr()
        {
            Reset();
        }

        void Reset()
        {
            if (m_Pointer)
            {
                m_Pointer->Release();
                m_Pointer = nullptr;
            }
        }

        T * Get() const
        {
            return m_Pointer;
        }

        T ** Put()
        {
            Reset();
            return &m_Pointer;
        }

        T * operator->() const
        {
            return m_Pointer;
        }

        explicit operator bool() const
        {
            return m_Pointer != nullptr;
        }
    };

    // Lock description returned by 'lockwindow', layout fixed by the game.
    struct LockInfo
    {
        void * m_Bits;
        std::int32_t m_Pitch;
        std::int32_t m_Format;
        std::int32_t m_Width;
        std::int32_t m_Height;
    };

    // Mode list entry, 40 bytes, layout fixed by the game.
    struct ModeRecord
    {
        std::int32_t m_Width;
        std::int32_t m_Height;
        std::int32_t m_Bpp;
        std::int32_t m_Format;
        std::int32_t m_Valid;
        std::int32_t m_Buffers;
        std::int32_t m_Capability;
        std::int32_t m_Reserved[3];
    };

    // About block, 208 bytes, layout fixed by the game.
    struct AboutBlock
    {
        std::uint32_t m_Signature; // +00
        std::uint32_t m_Size; // +04
        std::uint32_t m_Version; // +08
        std::uint32_t m_Flags; // +0C
        std::int32_t m_TextureWidthMin; // +10
        std::int32_t m_TextureWidthMax; // +14
        std::int32_t m_TextureWidthMultiple;
        std::int32_t m_TextureHeightMin; // +1C
        std::int32_t m_TextureHeightMax; // +20
        std::int32_t m_TextureHeightMultiple;
        std::int32_t m_ClipAlign; // +28
        std::int32_t m_FormatCount; // +2C
        const std::int32_t * m_Formats; // +30
        std::int32_t m_PaletteFormatCount; // +34
        const std::int32_t * m_PaletteFormats;
        std::int32_t m_ModeCount; // +3C
        ModeRecord * m_Modes; // +40
        std::int32_t m_Stages; // +44
        std::int32_t m_Unknown48;
        std::uint32_t m_Constants[3]; // +4C
        std::uint32_t m_Unknown58[5];
        std::int32_t m_Subtype; // +6C
        std::uint32_t m_TextureMemory; // +70
        std::int32_t m_TextureMemoryType; // +74
        const char * m_Author; // +78
        std::int32_t m_DxVersion; // +7C
        char m_Name[80]; // +80
    };

    // Vertex as the game passes it, the second UV pair only with state 2A.
    struct TlVertex
    {
        float m_X;
        float m_Y;
        float m_Z;
        float m_Rhw;
        std::uint32_t m_Diffuse;
        std::uint32_t m_Specular;
        float m_U0;
        float m_V0;
        float m_U1;
        float m_V1;
    };

    // Constant buffer of the scene shader, layout matches 'shaders::SCENE'.
    struct DrawConstants
    {
        float m_Target[4];
        std::int32_t m_ColorOp[2][4];
        std::int32_t m_AlphaOp[2][4];
        float m_Factor[4];
        float m_FogColor[4];
        float m_FogParams[4];
        std::int32_t m_Flags[4];
        float m_AlphaRef[4];
        float m_Debug[4];
        float m_Head[4];
        float m_HeadColour[4];
        float m_HeadProjection[4];
        float m_ViewToLight[16];
        // Other racers' headlights (PLAN.md 9.11): x count.
        std::int32_t m_Extra[4];
        // High beam of the player's light (PLAN.md 9.10): factors on the z, x and y scales.
        float m_HeadHigh[4];
        // Tail light glow of all racers (PLAN.md 11.5): x count; shape: rear offset, reach, lamp x, lamp spread.
        std::int32_t m_Rear[4];
        float m_RearShape[4];
        // Test switches (BUGS.md B21): x vertex alpha forced to 1.
        float m_Test[4];
        float m_Clip[4]; // clip rectangle of the game in game pixels: left, top, right, bottom
        float m_Night[4]; // rgb: factor of the world at night (LFSP LfspNightGrade), w: 1 = on
        // Weather (LFSP LfspWeatherFx): x snow white amount (0 = off), y desaturation, z slope (0 uniform, 1 only faces up), w test view;
        // m_Weather2: x fog exponent (storm, 1 = as the game), yzw snow colour. m_SnowToWorld: screen side (xc, yc, wc, 1) to track space.
        float m_Weather[4];
        float m_Weather2[4];
        float m_SnowToWorld[16];
        // The light arrays come last: a draw uploads only the head of the block and the first m_Extra[0] / m_Rear[0] entries of each array.
        float m_ExtraColour[16][4];
        float m_ExtraViewToLight[16][16];
        float m_RearLeft[16][4];
        float m_RearRight[16][4];
        float m_RearViewToLight[16][16];
    };

    static_assert( offsetof( DrawConstants, m_ExtraColour ) == 496 && offsetof( DrawConstants, m_ExtraViewToLight ) == 752 &&
        offsetof( DrawConstants, m_RearLeft ) == 1776 && offsetof( DrawConstants, m_RearRight ) == 2032 &&
        offsetof( DrawConstants, m_RearViewToLight ) == 2288 && sizeof( DrawConstants ) == 3312, "DrawConstants must match the fxc listing of shaders::SCENE" );

    struct Texture
    {
        std::uint32_t m_Magic = 0x58544433;
        std::int32_t m_Width = 0;
        std::int32_t m_Height = 0;
        std::int32_t m_Format = 0;
        std::int32_t m_Levels = 1;
        std::int32_t m_Stage = 0;
        std::int32_t m_Updates = 0;
        // Top level holds texels with alpha below 255, set on upload.
        bool m_Translucent = false;
        // Share of the top level texels with a half transparent alpha (32 to 224): cut outs (leaves, fences) have almost none,
        // gradients (water edges, shadows, smoke, glass) many.
        float m_AlphaMid = 0.0f;
        // Smallest alpha of the top level (0 when unknown, as DXT1 with punch through): a texture with a high minimum never loses a
        // pixel to the alpha test.
        std::uint8_t m_AlphaMin = 255;
        // B34: the car's glass texture: at least 45 % of the top level at about half alpha (96 to 176) and under 2 % fully clear.
        // Over all 44 car models only the texture 'glas' has this; paint, lamps, handles and grilles do not.
        bool m_GlassPage = false;
        // Size of all levels as uploaded.
        std::uint64_t m_Bytes = 0;
        // FNV-1a over the converted top level, names the texture in the measure log.
        std::uint32_t m_Hash = 0;
        ComPtr<ID3D11Texture2D> m_Texture;
        ComPtr<ID3D11ShaderResourceView> m_View;
    };

    class Driver
    {
    private:

        static constexpr std::size_t BATCH_VERTICES = 16384;
        static constexpr std::size_t BATCH_INDICES = BATCH_VERTICES * 3;
        static constexpr int STATE_SLOTS = 8;

        enum WindowMode
        {
            WINDOW_BORDERLESS = 0,
            WINDOW_FULLSCREEN = 1,
            WINDOW_WINDOWED = 2
        };

        enum class Primitive
        {
            NONE,
            TRIANGLES,
            LINES,
            POINTS
        };

        // Window and swap chain.
        HWND m_Window = nullptr;
        bool m_Windowed = false;
        HMODULE m_Compiler = nullptr;

        ComPtr<ID3D11Device> m_Device;
        ComPtr<ID3D11DeviceContext> m_Context;
        ComPtr<IDXGISwapChain1> m_SwapChain;
        ComPtr<ID3D11RenderTargetView> m_BackBufferView;
        std::int32_t m_BackBufferWidth = 0;
        std::int32_t m_BackBufferHeight = 0;

        // Off screen frame, rendered at a scaled resolution and with multisampling. The
        // game sees 'm_Width' by 'm_Height', locks and rectangle copies go through game sized
        // copies of the frame.
        ComPtr<ID3D11Texture2D> m_Frame;
        ComPtr<ID3D11RenderTargetView> m_FrameView;
        ComPtr<ID3D11Texture2D> m_Resolved;
        ComPtr<ID3D11ShaderResourceView> m_ResolvedView;
        ComPtr<ID3D11Texture2D> m_Depth;
        ComPtr<ID3D11DepthStencilView> m_DepthView;
        // Stencil and depth of m_Depth as textures, where the device allows it (typeless depth): the water
        // reflections read them without a copy. Empty: the copy in CopyStencil.
        ComPtr<ID3D11ShaderResourceView> m_DepthStencilSrv;
        ComPtr<ID3D11ShaderResourceView> m_DepthSrv;

        // Front buffer, window 1. It holds the last presented frame, the movie player
        // writes into it through locks and expects the picture to show at once.
        ComPtr<ID3D11Texture2D> m_Front;
        ComPtr<ID3D11RenderTargetView> m_FrontTarget;
        ComPtr<ID3D11ShaderResourceView> m_FrontView;
        ComPtr<ID3D11Texture2D> m_GameFrame;
        ComPtr<ID3D11RenderTargetView> m_GameFrameView;
        ComPtr<ID3D11Texture2D> m_Upload;
        ComPtr<ID3D11ShaderResourceView> m_UploadView;
        ComPtr<ID3D11Texture2D> m_Staging;
        std::int32_t m_Width = 0;
        std::int32_t m_Height = 0;
        std::int32_t m_RenderWidth = 0;
        std::int32_t m_RenderHeight = 0;
        float m_ScaleX = 1.0f;
        float m_ScaleY = 1.0f;
        UINT m_Samples = 1;
        bool m_Resolve = false;

        // Settings from [Dx11] in 'nfs5_enhanced.ini'.
        bool m_ConfigLoaded = false;
        int m_ConfigScale = 0;
        int m_ConfigSamples = 4;
        int m_ConfigAnisotropy = 0;
        int m_ConfigWindowMode = WINDOW_BORDERLESS;
        int m_ConfigWindowWidth = 0;
        int m_ConfigWindowHeight = 0;
        bool m_WindowPlaced = false;
        bool m_ConfigVsync = true;
        bool m_ConfigMinimize = true;
        bool m_ConfigFogEnabled = true;
        bool m_AllowTearing = false;
        UINT m_SwapChainFlags = 0;
        WNDPROC m_OriginalWndProc = nullptr;

        // Set while another program is active, the game then may not take the focus back.
        volatile bool m_Inactive = false;

        // Set on activation, exclusive fullscreen is entered again on the next page flip.
        volatile bool m_ReenterFullscreen = false;
        bool m_FocusPatched = false;
        HWND m_SubclassedWindow = nullptr;
        int m_TraceLines = 0;
        std::uint32_t m_FlipCount = 0;


        // '[Dx11] Headlights': 0 game vertex light, 1 per pixel instead, 2 both (comparison).
        int m_ConfigHeadlights = 0;
        // '[Dx11] HeadlightsDay': 0 off, 1 when the car's light switch is on, 2 always.
        int m_ConfigHeadlightsDay = 0;
        std::uint32_t m_DayLightFlip = 0xFFFFFFFFu;
        // A track chunk is being drawn with the day light, OnMeshDrawn keeps the flag until it ends.
        bool m_DayMesh = false;
        // '[Dx11] HeadlightsDayStrength', 'HeadlightsDayColour', 'HeadlightsDayRange'.
        float m_ConfigDayStrength = 1.0f;
        bool m_ConfigNightEdgeLight = true; // '[Weather] NightEdgeLight' of lfsp.ini: light fades out on the faint alpha edge
        std::uint32_t m_ConfigDayColour = 0x808080u;
        float m_ConfigDayRange = 1.0f;


        // One time log lines of the day path (doc/LICHT.md 7.10).
        std::uint32_t m_DayLogged = 0;
        // Which step of PrepareHeadMesh failed last: 1 renderer or camera, 2 inverse, 3 viewport, 4 map.
        int m_HeadFailStep = 0;
        // A world draw of a day track chunk has started, the first driver draw inside it prepares the light.
        bool m_DayPending = false;
        int m_DayFailDumps = 0;
        // Column of the camera projection that gives w: 3 at night, 2 when column 3 is (0, 0, 0, 1) (doc/LICHT.md 7.12).
        int m_HeadWColumn = 3;
        std::uint32_t m_HeadColumnLogged = 0;

        std::vector<std::uint8_t> m_DayMap;
        // '[Dx11] HeadlightsMap': own light map as BMP instead of the game's, by day and night (PLAN.md 9.7).
        std::string m_ConfigHeadMapFile;
        std::vector<std::uint8_t> m_CustomMap;
        int m_CustomMapWidth = 0;
        int m_CustomMapHeight = 0;
        bool m_CustomMapTried = false;
        // Model year of the last LFSP tint (lfsp.asi, PLAN.md 9.9), 0 = none, -1 = not asked yet.
        int m_HeadTintYear = -1;

        // Headlights of the other racers (PLAN.md 9.11, doc/LICHT.md 8.4a): '[Dx11] HeadlightsOpponents' and
        // 'HeadlightsOpponentsStrength'. Collected once per frame, object to light space like 0x0061BCCC.
        static constexpr int MAX_CAR_LIGHTS = 16;
        struct CarLight
        {
            std::uintptr_t m_Car = 0;
            std::array<float, 16> m_ObjectToLight = {};
            std::array<float, 4> m_Colour = {};
        };
        int m_ConfigOpponents = 1;
        float m_ConfigOpponentsStrength = 1.0f;
        float m_ConfigTrafficGlow = 0.35f; // [Lights] TrafficHeadlightGlow: share of the opponents' light for traffic cars, 0 = off
        float m_ConfigTrafficRange = 0.5f; // [Lights] TrafficHeadlightRange: reach of the traffic cars' light as a share of the opponents' (0.1-2)
        // '[Lights] HeadlightsOpponentsRange': reach of the opponents' light as a share of the game's map (1 = as the player's).
        float m_ConfigOpponentsRange = 1.5f;
        // '[Lights] CarLightRadius': cars this close to the camera can light the road (was fixed 100).
        float m_ConfigCarLightRadius = 250.0f;
        int m_ConfigMaxCarLights = 16; // [Lights] MaxCarLights: active car lights (opponents and traffic), 1 to MAX_CAR_LIGHTS
        int m_ConfigNightGradeBlended = 1; // [Lights] NightGradeBlended: normal blended draws without depth write (foliage fringe) are darkened too
        std::map<std::uint32_t, std::array<std::uint64_t, 3>> m_NightTextures;
        float m_ConfigCoverageMid = 0.12f; // [Lights] AlphaCoverageMid: textures with a larger half transparent share keep the two pass fix
        int m_ConfigEarlyZ = 2; // [Lights] EarlyZ: forced early depth test for draws that cannot discard (feature level 11)
        ComPtr<ID3D11PixelShader> m_ScenePsEarly;
        int m_ConfigLightCulling = 1; // [Lights] LightCulling: lights out of reach of a batch stay out of its constants (no visible change)

        int m_ConfigCarReflInterval = 1; // [CarReflections] UpdateInterval: the frame copy is renewed every N frames
        float m_ConfigCarReflMinSize = 0.0f; // [CarReflections] MinSize: parts smaller than this many pixels (of 1440 lines) get no reflection
        std::set<std::uint64_t> m_CarReflCellsDone; // parts whose triangles have marked their texture cells already
        bool BatchViewSphere( float * centre, float & radius ) const;
        bool CannotDiscard( const DrawConstants & constants ) const;
        std::int64_t m_CarLightsTicks = 0;
        std::uint64_t m_CarLightsActive = 0;
        int m_CarLightsFrames = 0;
        std::vector<CarLight> m_CarLights;
        std::uint32_t m_CarLightsFlip = 0xFFFFFFFFu;
        // Row layout of the rotation at car + 0x364: 0 not checked, 1 rows as stored, 2 transposed (checked at night
        // against the game's player matrix 0x0061BCCC).
        int m_CarMatrixLayout = 0;
        int m_CarLightsLogged = -1;
        int m_CarMatrixTries = 0;
        // Tail light glow (PLAN.md 11.5): '[Lights] TailGlow…' in lfsp.ini.
        // Rear glow per car plus a front glow while a front turn signal is lit (11.5d).
        static constexpr int MAX_REAR_LIGHTS = 16;
        struct RearLight
        {
            std::uintptr_t m_Car = 0;
            std::array<float, 16> m_ObjectToLight = {};
            std::array<float, 4> m_Left = {}; // w: offset of the glow in front of the light space origin, 0 = TailGlowOffset
            std::array<float, 4> m_Right = {};
        };
        int m_LampsLogged = 0; // bit per racer: lamps logged once
        // Turn signal lamps per racer (11.5e): frames in a row a lamp stayed dark in its own blink phase (3 = broken).
        std::array<std::uintptr_t, 9> m_SignalCar = {};
        std::array<std::array<std::uint8_t, 16>, 9> m_SignalMiss = {};
        int m_ConfigTailGlow = 1;
        std::uint32_t m_ConfigTailColour = 0xFF1A0Au;
        float m_ConfigTailStrength = 0.0f;
        float m_ConfigTailBrake = 0.41f;
        float m_ConfigTailSignals = 0.3f;
        float m_ConfigTailLights = 0.12f; // added to the tail light while the light switch is on (11.5f)
        float m_ConfigTailReverse = 0.35f; // reversing light, lamp 8, in the colour of the lamp (11.5g)
        float m_ConfigTailOffset = 2.1f;
        float m_ConfigTailReach = 1.5f;
        float m_ConfigTailWidth = 0.6f;
        float m_ConfigTailSpread = 0.5f;
        std::vector<RearLight> m_RearLights;
        std::array<float, 16 * MAX_REAR_LIGHTS> m_HeadRearViewToLight = {};
        int m_HeadRearCount = 0;
        int m_RearLightsLogged = -1;
        // Tail light measurement (PLAN.md 11.5): last logged lamp table of the player's car.
        std::uintptr_t m_LampCar = 0;
        std::array<std::uint8_t, 16> m_LampBytes = {};
        int m_LampLines = 0;
        // Car reflections, stage 2 (PLAN.md 9.12b, '[CarReflections] Reflections'): R3DCarLOD::Draw 0x43C830 (slot 2 of
        // 0x5B36AC) is hooked, its pass mask says which parts it draws (0x50 body, 0x20 wheels, 0x30 interior). The body
        // draws are drawn once more with the frame in the reflected direction over them (CarReflBatch).
        struct CarReflVertex
        {
            float m_Position[4]; // as the game's vertex: x, y in game pixels, z, 1 / w
            float m_View[3];
            float m_Normal[3];
            float m_Uv[3]; // u, v, alpha of the vertex colour
            float m_Object[3]; // position in the space of the part (rain drops and snow, w3: fixed on the car, not mirrored like the UV)
        };
        int m_ConfigCarRefl = 0;
        float m_ConfigCarReflStrength = 0.8f;
        float m_ConfigCarReflF0 = 0.15f;
        float m_ConfigCarReflBlur = 2.0f;
        float m_ConfigCarReflFresnel = 3.0f; // '[CarReflections] Fresnel': exponent, 5 = Schlick, smaller = softer
        int m_ConfigCarReflDebug = 0;
        std::vector<std::uint32_t> m_ConfigCarReflSkip;
        // '[CarReflections] MatteParts': part << 8 | variant (0xFF = any variant) of the .crp, e.g. cloth tops 37 and 41.
        std::vector<std::uint32_t> m_ConfigCarReflMatte;
        float m_ConfigCarReflGround = 0.35f; // '[CarReflections] Ground': strength of reflections below the horizon
        bool m_CarPartMatte = false; // the part being drawn is matte (CarPartEntryHook)
        bool m_CarPartBody = false; // the part being drawn is the body (part 1)
        bool m_CarPartOwn = false; // glass or 'SkipParts': the part keeps a batch of its own (B34)
        std::size_t m_CarReflGlassCount = 0; // last glass draw of this call: the game draws each window twice (B34)
        float m_CarReflGlassFirst[3] = {};
        // Textures of the body (part 1), the paint pages, and where on its textures any glossy part is drawn: 64 x 64 cells
        // of the texture, one bit each. In matte parts the triangles on cells of a paint page stay glossy. The tops of the
        // 356 (37, 41) hold the cloth and the painted rear deck together, on the paint page; the deck shares its cells with
        // the lids (16, 34), not with the body.
        std::set<std::uint32_t> m_CarReflBodyTextures;
        std::unordered_set<std::uint32_t> m_CarReflInteriorPages; // B34: textures that hold the interior of a car
        std::map<std::uint32_t, std::array<std::uint64_t, 64>> m_CarReflPaintCells;
        // Mean colour (0x00RRGGBB) of the opaque texels in each of the 64 x 64 cells of a paint page, read back once; empty
        // when it cannot be read. On the 356 the top holds the rear quarters too, on cells no glossy part covers: there
        // the colour tells paint from cloth.
        std::map<std::uint32_t, std::vector<std::uint32_t>> m_CarReflCellColours;
        // Colours of the first page seen per size, the undamaged state (damage repaints the page under a new hash).
        struct CarReflBaseline { std::uint32_t m_Width, m_Height; std::vector<std::uint32_t> m_Colours; };
        std::vector<CarReflBaseline> m_CarReflBaselines;
        // B28: which triangles of a matte part reflect as paint, decided once per car object, part and geometry (triangle
        // count and texture coordinates) and kept: damage repaints the page under a new hash (with HD textures the undamaged
        // page cannot be read at all, so there is no baseline), and scratched cloth would read as paint.
        // '[CarReflections] MatteKeep' (1 = on).
        struct CarReflMatteKeep { std::uint32_t m_Hash; bool m_Logged; std::vector<std::uint8_t> m_Keep; };
        std::map<std::uint64_t, CarReflMatteKeep> m_CarReflMatteKeep;
        int m_ConfigCarReflMatteKeep = 1;
        // '[CarReflections] MatteGrey' (B28): 0 = grey, unsaturated colours of the paint cells count as paint only when they
        // are the main colour (black, grey, silver, white cars); 1 = all colours as before. Dark trim and shading of a
        // coloured car otherwise let the dark cloth read as paint (356 opponents without HD textures, part 41).
        int m_ConfigCarReflMatteGrey = 0;
        // '[CarReflections] MatteSolid' (B28): matte parts without any paint in them, matte all over (no split by cells or
        // colour). Default 41, the rigid top of the cabrios: opponents keep it (LFSP swaps it for the animated top 37 on the
        // player's car only), and on their own 256 pages all its triangles read as paint.
        std::vector<int> m_ConfigCarReflMatteSolid;
        // '[CarReflections] SkipAlphaMid' (B34): textures whose share of half transparent texels (alpha 32..224) is at least
        // this get no reflection: grilles and meshes (GT1: part 27, 64 x 64, 58 %); their half covered texels reflected like
        // paint, a small texture blown up into a bright block pattern. 0 = off.
        float m_ConfigCarReflSkipAlphaMid = 0.3f;
        std::set<std::uint32_t> m_CarReflAlphaSkipped;
        // '[CarReflections] GlassParts' (B34): glass parts (35/47 side windows outside/inside, 48 windscreen inside) reflect as
        // a whole like glass: 'GlassStrength' at a flat angle, 'GlassReflectance' head on, Schlick exponent 5, no highlight
        // boost, no clearcoat, no paint split. Glass used to be told apart per pixel by alpha; where a window lies on opaque
        // texels of the paint page, half of it reflected like paint (GT1 rear quarter window).
        std::vector<int> m_ConfigCarReflGlassParts;
        std::vector<int> m_ConfigCarReflSkipParts; // '[CarReflections] SkipParts' (B34): no reflection at all
        float m_ConfigCarReflGlassSkyBoost = 2.0f; // '[CarReflections] GlassSkyBoost' (B34)
        std::set<int> m_CarReflGlassLogged;
        float m_ConfigCarReflGlassStrength = 0.5f;
        float m_ConfigCarReflGlassF0 = 0.12f;
        std::set<std::uint32_t> m_CarReflPaletteLogged;
        // 9.15e: paint colours of a texture, kept until the cells glossy parts cover change (they were counted over all 4096
        // cells with a map and a text for the log on every matte part draw, every frame).
        struct CarReflPalette
        {
            std::uint64_t m_Signature = 0;
            int m_Grey = -1;
            std::vector<std::array<int, 3>> m_Colours;
        };
        std::unordered_map<std::uint32_t, CarReflPalette> m_CarReflPalettes;
        const void * m_CarLodObject = nullptr; // R3DCarLOD of the call being drawn
        int m_CarPart = -1; // part << 8 | variant being drawn
        std::vector<std::uint16_t> m_CarReflIndices;
        std::set<std::uint64_t> m_CarReflMatteSeen;
        std::set<std::uint32_t> m_CarReflPartsSeen;
        std::uint32_t m_CarLodMask = 0; // pass mask of the running R3DCarLOD::Draw, 0 = outside
        bool m_CarReflCamera = false;
        std::array<float, 16> m_CarReflClipOfView = {};
        std::array<float, 16> m_CarReflViewOfClip = {};
        std::array<float, 4> m_CarReflViewport = {};
        std::uint32_t m_CarReflCopyFlip = 0xFFFFFFFFu;
        bool m_CarReflSourceValid = false;
        // '[CarReflections] SourceScale' 0.5 (9.13d): the frame resolved into m_CarReflResolve, then drawn half size into the copy.
        float m_ConfigCarReflSourceScale = 1.0f;
        float m_CarReflLevelOffset = 0.0f; // mip levels the copy is smaller than the frame (1 at half size)
        // Shared blur chain (REFLEXION-WETTER.md 9.16a, '[CarReflections] BlurChain'): m_BlurDown holds the scene filtered down from
        // half size on (BLUR DownPS), m_BlurChain level k is level k + 1 of it drawn back up to the size of level k (UpPS).
        static constexpr int BLUR_DOWN_LEVELS = 6;
        int m_ConfigCarReflBlurChain = 0;
        float m_ConfigCarReflHighlightBoost = 0.0f; // 9.15b '[CarReflections] HighlightBoost' (0 = off)
        float m_ConfigCarReflHighlightThreshold = 0.75f; // 9.15b '[CarReflections] HighlightThreshold'
        float m_ConfigCarReflCreaseCos = -2.0f; // 9.15b cos of '[CarReflections] CreaseAngle' (< -1 = off, all faces of a point)
        int m_ConfigCarReflClearCoat = 0; // 9.15b '[CarReflections] ClearCoat'
        int m_ConfigCarReflObjectNormals = 0; // 9.15e '[CarReflections] ObjectNormals': fixed orientation, normals kept per part
        int m_ConfigCarReflSeamFix = 0; // 9.15e '[CarReflections] SeamFix': corners on the middle plane x = 0 get no sideways normal
        float m_ConfigCarReflBaseCoat = 0.2f; // 9.15b '[CarReflections] BaseCoat': strength of the soft coloured base reflection
        float m_ConfigCarReflBaseBlur = 2.5f; // 9.15b '[CarReflections] BaseCoatBlur': its extra blur levels
        float m_ConfigCarReflBaseTint = 0.7f; // 9.15b '[CarReflections] BaseCoatTint': how much it takes the paint colour
        int m_ConfigCarReflSky = 0; // '[CarReflections] Sky' (9.15a): sky image of the water reflection above the frame instead of the fog colour
        std::uint32_t m_SkyImageFlip = 0xFFFFFFFFu; // flip in which the sky image was last drawn (main view)
        // '[CarReflections] SkyHold' (B36): seconds the last sky image stays in use after the water left the screen, then it
        // fades to the fog colour (and back in) instead of switching in one frame; 0 = switch at once as before, -1 (default)
        // = keep the last image until the next race (menu) or a new one.
        float m_ConfigCarReflSkyHold = -1.0f;
        int m_ConfigCarReflSkyPanorama = 1; // '[CarReflections] SkyByDirection'
        ComPtr<ID3D11Texture2D> m_CarSkyPano;
        ComPtr<ID3D11RenderTargetView> m_CarSkyPanoRtv;
        ComPtr<ID3D11ShaderResourceView> m_CarSkyPanoView;
        ComPtr<ID3D11VertexShader> m_CarSkyPanoVs;
        ComPtr<ID3D11PixelShader> m_CarSkyPanoPs;
        ComPtr<ID3D11InputLayout> m_CarSkyPanoLayout;
        ComPtr<ID3D11Buffer> m_CarSkyPanoVertices;
        ComPtr<ID3D11BlendState> m_CarSkyPanoBlend;
        ComPtr<ID3D11DepthStencilState> m_CarSkyPanoDepth;
        ComPtr<ID3D11RasterizerState> m_CarSkyPanoRaster;
        ComPtr<ID3D11SamplerState> m_CarSkyPanoSampler;
        ComPtr<ID3D11ShaderResourceView> m_CarSkyBandView;
        std::uintptr_t m_CarSkyHorizon = 0; // the game's horizon object the band below was read from
        std::array<std::array<float, 4>, 64> m_CarSkyBandPositions = {};
        std::array<std::array<float, 2>, 64> m_CarSkyBandUv = {};
        std::array<std::uint32_t, 64> m_CarSkyBandColours = {};
        std::uint32_t m_CarSkyBandHash = 0;
        std::uint32_t m_CarSkyFlatColour = 0;
        std::uint64_t m_CarSkyFlatFlip = ~0ull;
        std::uint32_t m_CarSkyBuiltHash = 0;
        std::uint32_t m_CarSkyBuiltZenith = 0;
        std::uintptr_t m_CarSkyBuiltHorizon = 0;
        bool m_CarSkyPanoValid = false;
        bool m_CarSkyRowsValid = false;
        float m_CarSkyRows[9] = {};
        float m_CarSkyBlend = 0.0f; // share of the sky image against the fog colour, once per flip
        std::uint32_t m_CarSkyBlendFlip = 0xFFFFFFFFu;
        double m_CarSkyBlendTime = 0.0;
        double m_CarSkyLastFresh = -1e9;
        int m_CarSkyFreshLogged = -1;
        int m_CarSkyLogLines = 0;
        float CarSkyImageBlend();
        // B36: the sky for the car reflections by direction in the world (horizon band of the game as a panorama).
        void CarSkyNoteBatch();
        void CarSkyBuildPanorama();
        bool CarSkyPanoramaUsable() const;
        bool CarSkyImageUsable() const;
        ComPtr<ID3D11Texture2D> m_BlurDown;
        ComPtr<ID3D11Texture2D> m_BlurChain;
        std::array<ComPtr<ID3D11RenderTargetView>, BLUR_DOWN_LEVELS> m_BlurDownRtv;
        std::array<ComPtr<ID3D11ShaderResourceView>, BLUR_DOWN_LEVELS> m_BlurDownSrv;
        std::array<ComPtr<ID3D11RenderTargetView>, BLUR_DOWN_LEVELS> m_BlurChainRtv;
        ComPtr<ID3D11ShaderResourceView> m_BlurChainView;
        ComPtr<ID3D11PixelShader> m_BlurDownPs;
        ComPtr<ID3D11PixelShader> m_BlurUpPs;
        UINT m_BlurDownLevels = 0; // levels of m_BlurDown; m_BlurChain has one less
        bool m_BlurChainValid = false;
        bool BuildBlurChain( ID3D11ShaderResourceView * source );
        ComPtr<ID3D11Texture2D> m_CarReflResolve;
        ComPtr<ID3D11ShaderResourceView> m_CarReflResolveView;
        ComPtr<ID3D11RenderTargetView> m_CarReflSourceRtv;
        ComPtr<ID3D11PixelShader> m_CarReflDownPs;
        ComPtr<ID3D11Texture2D> m_CarReflSource;
        ComPtr<ID3D11ShaderResourceView> m_CarReflSourceView;
        ComPtr<ID3D11VertexShader> m_CarReflVs;
        ComPtr<ID3D11PixelShader> m_CarReflPs;
        ComPtr<ID3D11InputLayout> m_CarReflLayout;
        ComPtr<ID3D11Buffer> m_CarReflConstants;
        ComPtr<ID3D11BlendState> m_CarReflBlend;
        ComPtr<ID3D11DepthStencilState> m_CarReflDepth;
        std::vector<CarReflVertex> m_CarReflVertices;
        // Own ring for the reflection draws (9.13b), so the ring of the game's batches is never started anew for them.
        ComPtr<ID3D11Buffer> m_CarReflVertexBuffer;
        ComPtr<ID3D11Buffer> m_CarReflIndexBuffer;
        std::size_t m_CarReflVertexCursor = 0;
        std::size_t m_CarReflIndexCursor = 0;
        // Groups of corners at the same point per part (9.13c), kept over frames: the triangles of a part do not change.
        // Checked each frame on a few pairs of corners in the same group (still equal) and in different groups (still apart).
        struct CarReflWeld
        {
            std::vector<std::uint32_t> m_Group;
            std::uint32_t m_Groups = 0;
            std::array<std::uint32_t, 16> m_Same = {}; // pairs a, b of corners of one group
            std::array<std::uint32_t, 16> m_Apart = {}; // pairs a, b of corners of different groups
            int m_SameCount = 0, m_ApartCount = 0;
            // 9.15e: the normal of each corner in the space of the part (raw, mirror not taken out), kept while the part
            // keeps its shape; empty = not built. Checked on a few corners (raw object position), the crease and seam
            // settings and the size on screen it was built at (rebuilt when the part is drawn more than twice as large).
            std::vector<std::array<float, 3>> m_ObjNormal;
            std::array<std::array<float, 3>, 8> m_ObjCheck = {};
            std::array<std::uint32_t, 8> m_ObjCheckIndex = {};
            int m_ObjCheckCount = 0;
            float m_ObjCrease = -3.0f;
            int m_ObjSeam = -1;
            float m_ObjExtent = 0.0f;
            std::uint32_t m_LastUse = 0; // m_FlipCount when last used (old entries go first when the cache is full)
        };
        std::unordered_map<std::uint64_t, CarReflWeld> m_CarReflWeldCache;
        std::uint64_t m_CarReflWeldHits = 0, m_CarReflWeldMisses = 0;
        // Why the groups were built anew: no entry yet, an entry whose test corners no longer fit, whole cache emptied (full).
        std::uint64_t m_CarReflWeldNew = 0, m_CarReflWeldFailed = 0, m_CarReflWeldClears = 0, m_CarReflWeldEvicted = 0;
        // 9.15e counters for the report: normals taken from / built into the part cache, how batches were oriented
        // (by the faces the GPU draws, by the centre of the part, mixed winding = turned per face as before), drawn
        // triangles against the batch's sign, corners on the middle seam set straight.
        std::uint64_t m_CarReflObjCached = 0, m_CarReflObjBuilt = 0;
        std::uint64_t m_CarReflOrientCull = 0, m_CarReflOrientCentre = 0, m_CarReflOrientMixed = 0;
        std::uint64_t m_CarReflOrientVisible = 0, m_CarReflOrientAgainst = 0, m_CarReflSeamCorners = 0;
        // Reflection draws of one R3DCarLOD::Draw collected per state and drawn at its end (9.13c).
        struct CarReflPending
        {
            ID3D11ShaderResourceView * m_View = nullptr;
            ID3D11SamplerState * m_Sampler = nullptr;
            std::uint32_t m_Cull = 0;
            std::uint32_t m_Fog = 0;
            float m_AlphaRef = -1.0f;
            float m_GlassRef = -1.0f;
            float m_Up[3] = {};
            int m_CarFlow = -1; // track of the car (w5: rain drops flow with its speed), -1 = none
            std::vector<CarReflVertex> m_Vertices;
            std::vector<std::uint16_t> m_Indices;
        };
        std::vector<CarReflPending> m_CarReflPending;
        std::size_t m_CarReflPendingCount = 0;
        std::uint64_t m_CarReflPendingDraws = 0, m_CarReflPendingParts = 0;
        void CarReflDraw( CarReflPending & pending );
        void CarReflDrawPending();
        std::vector<std::uint32_t> m_CarReflOrder;
        std::vector<std::array<double, 3>> m_CarReflView;
        std::vector<std::array<double, 3>> m_CarReflNormal;
        // Per pass mask: draws reflected, skipped as blended, skipped by texture; textures seen (logged once each).
        struct CarReflStats
        {
            std::uint64_t m_Drawn = 0, m_Blended = 0, m_Skipped = 0, m_Matte = 0, m_Triangles = 0, m_MatteTriangles = 0;
        };
        std::map<std::uint32_t, CarReflStats> m_CarReflStats;
        std::unordered_map<std::uint64_t, int> m_CarReflSeen;
        std::uint64_t m_CarReflLodCalls = 0, m_CarReflNoCamera = 0, m_CarReflFrames = 0;
        ULONGLONG m_CarReflNextReport = 0;
        int m_CarReflLines = 0;
        bool m_CarReflRoomLogged = false;
        void InstallCarReflHook();
        // Precipitation of the game (PLAN.md 16.0a measure build p1): a mark around the two calls of R_DrawPrecipitation in
        // R_WeatherUpdateDraw (rain, snow); draws arriving inside are logged every 10 s.
        void InstallPrecipHooks();
    public:
        // true: drawn by dx11z (16.4 / 16.5), the game's draw is left out.
        bool OnPrecipDraw( int kind, const void * precipitation, bool begin );
    private:
        bool PrecipDrawOwn( int kind, const void * precipitation );
        int m_ConfigPrecip = 1; // '[Weather] Precip' of lfsp.ini: 1 = own rain and snow (16.4 / 16.5), 0 = the game's
        float m_ConfigPrecipRain = 7000.0f; // '[Weather] PrecipRain': rain streaks at full rain
        float m_ConfigPrecipSnow = 5000.0f; // '[Weather] PrecipSnow': flakes at full snow
        float m_ConfigPrecipBright = 1.0f; // '[Weather] PrecipBright': brightness and opacity
        ComPtr<ID3D11VertexShader> m_PrecipVs;
        ComPtr<ID3D11PixelShader> m_PrecipPs;
        ComPtr<ID3D11Buffer> m_PrecipConstants;
        ComPtr<ID3D11BlendState> m_PrecipBlend;
        ComPtr<ID3D11DepthStencilState> m_PrecipDepth;
        ComPtr<ID3D11RasterizerState> m_PrecipRaster;
        float m_PrecipCamPos[3] = {};
        float m_PrecipCamVel[3] = {};
        int m_PrecipCamStep = 0;
        bool m_PrecipCamHave = false;
        std::uint32_t m_PrecipCamFlip = 0xFFFFFFFFu;
        std::uint64_t m_PrecipOwn[2] = {}, m_PrecipOwnInstances[2] = {};
        struct PrecipStats
        {
            std::uint64_t m_Marks = 0, m_MarksWithParticles = 0, m_Draws = 0, m_Vertices = 0, m_Indices = 0;
            int m_ParticlesMax = 0;
            std::uint32_t m_Hashes[4] = {};
            int m_HashCount = 0;
            std::uint32_t m_Src = 0, m_Dst = 0, m_Blend = 0, m_ZEnable = 0, m_ZWrite = 0, m_ZFunc = 0, m_Cull = 0, m_Fog = 0;
            int m_QueueList = -1, m_Primitive = -1;
            float m_Box[4] = { 1e30f, 1e30f, -1e30f, -1e30f }; // x, y min; x, y max (game pixels)
            float m_Rhw[2] = { 1e30f, -1e30f };
            float m_Z[2] = { 1e30f, -1e30f };
            std::uint32_t m_Diffuse = 0;
        };
        PrecipStats m_PrecipStats[2];
        int m_PrecipMark = 0; // 1 rain, 2 snow: inside R_DrawPrecipitation
        ULONGLONG m_PrecipNextReport = 0;
        void PrecipMeasureBatch();
        bool CopyCarReflSource();
        void CarReflBatch();
        const std::vector<std::uint32_t> & CarReflCellColours(
            const Texture * texture
        );
        void CarReflReport();
        // Frame in which the day path built the player's matrix 0x0061BCCC (layout check by day, 9.11c).
        std::uint32_t m_GameMatrixFlip = 0xFFFFFFFEu;
        // High beam (PLAN.md 9.10): 'HeadlightsHighRange', 'HeadlightsHighWidth', 'HeadlightsHighStrength', 'HeadlightsHighHeight'.
        float m_ConfigHighRange = 1.0f;
        float m_ConfigHighWidth = 1.0f;
        float m_ConfigHighStrength = 1.2f;
        float m_ConfigHighHeight = 0.4f; // inverse of the INI value
        float m_High = 0.0f;
        bool m_HighKey = false;
        std::uint32_t m_HighFlip = 0xFFFFFFFFu;
        std::array<float, 4> m_HeadHigh = { 1.0f, 1.0f, 1.0f, 0.0f };
        // The player's own light is off (or broken), only the other cars light the mesh.
        bool m_HeadPlayerOff = false;
        std::array<float, 16 * MAX_CAR_LIGHTS> m_HeadExtraViewToLight = {};
        int m_HeadExtraCount = 0;
        // The draws of the current mesh get the per pixel light, constants below are for it.
        bool m_HeadMesh = false;
        // '[Lights] HeadlightsObjects' (PLAN.md 11.4): 1 = every depth tested 3D draw of the frame gets the light of the last
        // track mesh (cars, signs, objects), 0 = track only.
        int m_ConfigHeadlightsObjects = 1;
        // A track mesh prepared the light in this frame: screen to light space (m_HeadViewToLight) is the same for every draw
        // with that camera, so other draws can use it.
        bool m_HeadFrameValid = false;
        struct ObjectLightStats
        {
            std::uint64_t m_Track = 0;   // flushes of track meshes (lit as before)
            std::uint64_t m_Lit = 0;     // flushes of other draws lit by HeadlightsObjects
            std::uint64_t m_NoState = 0; // no track mesh prepared the light yet in this frame
            std::uint64_t m_NoDepth = 0; // depth test off (HUD, 2D)
            std::uint64_t m_Blend = 0;   // additive or other blending (flares, shadows, smoke)
        };
        ObjectLightStats m_ObjectStats;
        int m_HeadCount = 0;
        std::array<float, 16> m_HeadViewToLight = {}; // screen side (xc, yc, wc, 1) to headlight space
        std::array<float, 16> m_HeadClipToObject = {}; // screen side (xc, yc, wc, 1) to the space of the track mesh (snow slope)
        std::array<float, 16> m_HeadModelView = {};
        std::array<float, 16> m_HeadProjectionMatrix = {};
        std::array<float, 4> m_HeadProjection = {}; // fx, fy, cx, cy
        std::array<float, 4> m_HeadScale = {}; // x, z, y, unused
        std::array<float, 4> m_HeadColour = {};
        std::uintptr_t m_HeadMapPointer = 0;
        std::uint32_t m_HeadMapFlip = 0xFFFFFFFFu;
        std::uint32_t m_HeadMapSum = 0;
        ComPtr<ID3D11Texture2D> m_HeadMap;
        ComPtr<ID3D11ShaderResourceView> m_HeadMapView;
        ComPtr<ID3D11SamplerState> m_HeadSampler;

        void InstallHeadlightHooks();
        void InstallSkidHook();
        bool UpdateHeadMap(
            bool day
        );

        void ApplyLfspTint();

        void CollectCarLights();

        void CollectRearLights(
            std::uint32_t racers
        );

        float UpdateHighBeam();

        void LogLampTable();

        void ApplyHighBeam();

        bool CarObjectToLight(
            std::uintptr_t car,
            int layout,
            float * out
        );

        bool PrepareHeadMesh(
            bool day
        );




        // B4 fix, PLAN.md 3T.2, '[Dx11] AlphaEdgeFix'. The game blends most of the world with
        // depth writes on and an alpha reference near zero, so faint edge texels write depth
        // and hide alpha geometry drawn later. Texels below 'AlphaEdgeRef' are drawn in a
        // second pass without depth writes.
        bool m_ConfigAlphaEdgeFix = false;
        bool m_ConfigAlphaToCoverage = false; // [Dx11] AlphaEdgeFix = 3
        int m_ConfigAlphaEdgeRef = 128;
        float m_ConfigAlphaMipScale = 0.0f; // '[Dx11] AlphaMipScale' (BUGS.md B21)
        bool m_ConfigTestNoMips = false; // '[Dx11] TestNoMips' (BUGS.md B21): top mip level only
        bool m_ConfigTestVertexAlpha = false; // '[Dx11] TestVertexAlpha' (BUGS.md B21): vertex alpha forced to 1
        bool m_ConfigTestNoZBias = false; // '[Dx11] TestNoZBias' (BUGS.md B21): the game's depth bias ignored
        std::set< std::int32_t > m_LoggedDepthBias; // depth bias values seen, logged once each
        // '[Dx11] TestDraw' (BUGS.md B21): 1 = opaque triangles always depth tested and written,
        // 2 = everything as wireframe. Both log the opaque draws without full depth once per texture.
        int m_ConfigTestDraw = 0;
        std::set< std::uint64_t > m_LoggedNoDepth;

        // '[Dx11] SkyEdgeFill' (BUGS.md B32): the sky dome is a band, above and below it the flat clear colour shows. While the
        // sky is drawn the alpha of the frame keeps the share of the clear colour left in each pixel; at the first draw after
        // the sky each column gets the colour of the sky at its upper and lower edge there instead of the clear colour.
        int m_ConfigSkyEdgeFill = 1;
        float m_ConfigSkyEdgeBlend = 12.0f; // '[Dx11] SkyEdgeBlend': soft seam into the band, pixels at 1440 lines
        int m_SkyEdgeState = 0; // 0 idle, 1 after a full clear, 2 sky draws running, 3 done for this frame
        bool m_SkyEdgeDraw = false; // the draw being applied is part of the sky
        bool m_SkyEdgeFlatDraw = false; // first sky draw is a flat untextured opaque colour: it counts as the clear colour
        float m_SkyEdgeClear[4] = {};
        ComPtr<ID3D11Texture2D> m_SkyEdgeCopy;
        ComPtr<ID3D11ShaderResourceView> m_SkyEdgeCopyView;
        ComPtr<ID3D11Texture2D> m_SkyEdgeTable; // render width x 2: colour and row of the upper / lower sky edge per column
        ComPtr<ID3D11RenderTargetView> m_SkyEdgeTableRtv;
        ComPtr<ID3D11ShaderResourceView> m_SkyEdgeTableView;
        ComPtr<ID3D11Texture2D> m_SkyEdgeSmooth; // the same with the colours averaged over the columns around
        ComPtr<ID3D11RenderTargetView> m_SkyEdgeSmoothRtv;
        ComPtr<ID3D11ShaderResourceView> m_SkyEdgeSmoothView;
        ComPtr<ID3D11PixelShader> m_SkyEdgeSmoothPs;
        ComPtr<ID3D11PixelShader> m_SkyEdgeColumnPs;
        ComPtr<ID3D11PixelShader> m_SkyEdgeFillPs;
        ComPtr<ID3D11Buffer> m_SkyEdgeConstants;
        ComPtr<ID3D11RasterizerState> m_SkyEdgeRaster;
        bool BatchIsSky() const;
        void SkyEdgeFill();

        // 0 normal draw, 1 opaque part only, 2 faint part only without depth writes.
        int m_EdgePass = 0;

        // B8, PLAN.md 3C, '[Dx11] ForceTwoSided'. Adds the faces the game culls in a second pass
        // pushed slightly back in depth, so a face the model already has on the other side
        // stays in front and does not flicker against it (B11).
        bool m_ConfigForceTwoSided = false;
        bool m_BackPass = false;

        [[nodiscard]] bool BatchTranslucent() const;



        // Pipeline objects.
        ComPtr<ID3D11VertexShader> m_SceneVs;
        ComPtr<ID3D11PixelShader> m_ScenePs;
        ComPtr<ID3D11InputLayout> m_SceneLayout;
        ComPtr<ID3D11VertexShader> m_PresentVs;
        ComPtr<ID3D11PixelShader> m_PresentPs;
        ComPtr<ID3D11PixelShader> m_MoviePs;
        ComPtr<ID3D11VertexShader> m_ClearVs;
        ComPtr<ID3D11PixelShader> m_ClearPs;
        ComPtr<ID3D11Buffer> m_DrawConstants;
        ComPtr<ID3D11Buffer> m_PresentConstants;
        ComPtr<ID3D11Buffer> m_ClearConstants;
        ComPtr<ID3D11Buffer> m_VertexBuffer;
        ComPtr<ID3D11Buffer> m_IndexBuffer;
        // Appends a batch to the vertex and index buffer ring (m_VertexCursor, m_IndexCursor); false when it does not fit.
        bool UploadRing(
            const TlVertex * vertices,
            std::size_t vertex_count,
            const std::uint16_t * indices,
            std::size_t index_count,
            UINT & base_vertex,
            UINT & first_index
        );
        // Uploads the draw constants unless they equal the last upload (m_LastConstants, m_ConstantsValid).
        void UploadDrawConstants(
            const void * data,
            std::size_t size
        );
        ComPtr<ID3D11SamplerState> m_PresentSampler;
        ComPtr<ID3D11SamplerState> m_PointSampler;
        ComPtr<ID3D11BlendState> m_OpaqueBlend;
        ComPtr<ID3D11RasterizerState> m_PlainRaster;
        ComPtr<ID3D11DepthStencilState> m_ClearDepth;
        ComPtr<ID3D11DepthStencilState> m_ClearColorOnly;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11SamplerState>> m_Samplers;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11BlendState>> m_Blends;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11DepthStencilState>> m_DepthStates;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11RasterizerState>> m_Rasters;

        // Batch of converted vertices and 16 bit indices.
        std::vector<TlVertex> m_BatchVertices;
        std::vector<std::uint16_t> m_BatchIndices;
        Primitive m_BatchPrimitive = Primitive::NONE;

        // Write positions in the dynamic vertex and index buffers. Batches are appended with
        // 'NO_OVERWRITE' and the buffers are only discarded when they are full, a discard per
        // draw made the driver rename megabytes of memory thousands of times per frame.
        std::size_t m_VertexCursor = 0;
        std::size_t m_IndexCursor = 0;

        // Pipeline objects and values bound last, nothing is set again while it is unchanged.
        // Cleared by 'InvalidatePipeline' whenever another path rebinds the context.
        bool m_SceneBound = false;
        bool m_ConstantsValid = false;
        DrawConstants m_LastConstants = {};
        ID3D11BlendState * m_BoundBlend = nullptr;
        ID3D11DepthStencilState * m_BoundDepth = nullptr;
        UINT m_BoundStencilRef = 0;
        ID3D11RasterizerState * m_BoundRaster = nullptr;
        D3D11_RECT m_BoundScissor = {};
        bool m_ScissorValid = false;
        D3D11_PRIMITIVE_TOPOLOGY m_BoundTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::array<ID3D11ShaderResourceView *, MAX_STAGES> m_BoundViews = {};
        std::array<ID3D11SamplerState *, MAX_STAGES> m_BoundSamplers = {};
        std::array<std::uint64_t, MAX_STAGES> m_SamplerKeys = {};
        std::array<ID3D11SamplerState *, MAX_STAGES> m_SamplerCache = {};
        std::uint64_t m_BlendKey = ~0ull;
        ID3D11BlendState * m_BlendCache = nullptr;
        std::uint64_t m_DepthKey = ~0ull;
        ID3D11DepthStencilState * m_DepthCache = nullptr;
        std::uint64_t m_RasterKey = ~0ull;
        ID3D11RasterizerState * m_RasterCache = nullptr;
        ComPtr<ID3D11BlendState> m_ClearColorBlend;
        ComPtr<ID3D11BlendState> m_ClearDepthOnlyBlend;

        // Values of pipeline states as last applied, a repeated identical call is skipped
        // without ending the batch. The generation is advanced whenever 'm_D3D' is reset.
        static constexpr std::size_t STATE_CACHE_STATES = 0x200;
        std::array<std::uint32_t, STATE_CACHE_STATES * 8> m_StateCacheValue = {};
        std::array<std::uint32_t, STATE_CACHE_STATES * 8> m_StateCacheGeneration = {};
        std::uint32_t m_StateGeneration = 1;

        // Small rectangle reads, the sun glare probe, copy only the pixels they need from the
        // render sized frame instead of scaling the whole frame down to the game size first.
        struct ProbeRead
        {
            int m_X0 = 0;
            int m_Y0 = 0;
            int m_X1 = 0;
            int m_Y1 = 0;
            int m_X = 0;
            int m_Y = 0;
            int m_Width = 0;
            int m_RenderX0 = 0;
            int m_RenderY0 = 0;
            int m_RenderX1 = 0;
            int m_RenderY1 = 0;
            float m_ScaleX = 1.0f;
            float m_ScaleY = 1.0f;
            int m_FrameBits = 32;
            std::chrono::steady_clock::time_point m_Time = {};
            bool m_Valid = false;
        };

        static constexpr int PROBE_SIZE = 256;
        static constexpr std::chrono::milliseconds PROBE_REUSE_WINDOW { 1000 };
        std::array<ComPtr<ID3D11Texture2D>, 2> m_Probes;
        std::array<ProbeRead, 2> m_ProbeReads = {};
        std::size_t m_ProbeNext = 0;
        int m_SlowProbeLogs = 0;

        // THRASH and D3D7 state.
        D3D7State m_D3D;
        std::array<Texture *, MAX_STAGES> m_Bound = {};
        std::unordered_map<std::uint32_t, std::array<std::uint32_t, STATE_SLOTS>> m_StateStore;
        std::uint32_t m_BackgroundColor = 0;
        float m_ClearDepthValue = 1.0f;
        float m_DepthBias = 0.0f;
        bool m_SkidDraw = false; // inside the skid mark draw 0x451420 (BUGS.md 19.1)
        bool m_WhiteDiffuse = false;
        bool m_TwoUvSets = false;
        bool m_FogTableActive = false;
        std::array<std::uint8_t, 256> m_FogTable = {};
        bool m_WaitVblank = true;
        float m_Brightness = 1.0f;

        // Movie frame enlarged on the graphics card, see 'MovieBlit'.
        ComPtr<ID3D11Texture2D> m_MovieTexture;
        ComPtr<ID3D11ShaderResourceView> m_MovieView;
        std::vector<std::uint8_t> m_MoviePixels;
        std::int32_t m_MovieWidth = 0;
        std::int32_t m_MovieHeight = 0;
        float m_MovieSharpness = 0.33f;
        bool m_ConfigMovie = true;

        // Movies that span the screen width fill the window, cutting the black bars of the frame
        // and keeping its aspect ratio. The bars are measured on the frames, the smallest
        // seen in a movie counts, a pause of half a second starts a new movie.
        bool m_ConfigMovieFill = true;
        std::int32_t m_MovieBarTop = -1;
        std::int32_t m_MovieBarBottom = -1;
        DWORD m_MovieTick = 0;
        bool m_MovieLogged = false;
        D3D11_RECT m_Clip = {};
        int m_SelectedWindow = 0;

        // Lock emulation.
        std::vector<std::uint8_t> m_LockSnapshot;
        std::vector<std::uint8_t> m_LockPixels;
        void * m_LockMapped = nullptr;
        std::uint32_t m_LockMappedPitch = 0;

        // Color depth the game sees through locks and rectangle copies, 16 is RGB565 like
        // dx7z in 16 bit modes, the software movie player needs it.
        int m_FrameBits = 32;
        LockInfo m_Lock = {};
        LockInfo m_FrameInfo = {};
        bool m_Locked = false;

        // Display information.
        AboutBlock m_About = {};
        std::vector<ModeRecord> m_Modes;
        std::array<std::int32_t, 16> m_FormatFlags = {};
        std::array<std::int32_t, 4> m_PaletteFlags = {};
        std::array<std::uint32_t, 8> m_Callbacks = {};
        std::string m_LogPath;

        // Live textures by handle, the game may hand back a freed handle, it is looked up
        // here instead of reading the freed object.
        std::unordered_map<Texture *, std::unique_ptr<Texture>> m_Textures;

        Driver() = default;

        void LoadConfig();

        void MakeDpiAware();

        void ReleaseFrame();

        void ReleaseAll();

        // Returns 'true' when the frame matches the game resolution and is single sampled.
        [[nodiscard]] bool IsDirect() const
        {
            return m_RenderWidth == m_Width && m_RenderHeight == m_Height && m_Samples == 1 && !IsFrontSelected();
        }

        void ComputeRenderSize();

        void ResolveFrame();

        // Scales the frame or the front buffer down into 'm_GameFrame'.
        void DownsampleFrame(
            bool front
        );

        // Shows the front buffer.
        void PresentFront();

        // Distance probe, 'DistanceKey' in '[World]' of lfsp.ini (doc/BUGS.md B20): the key toggles a cross at the
        // mouse with the depth of the nearest depth tested triangle under it (1 / rhw, the game's metres).
        int m_ProbeKey = 0;
        bool m_ProbeOn = false;
        bool m_ProbeKeyDown = false;
        float m_ProbeX = -1.0f; // game pixels, this frame
        float m_ProbeY = -1.0f;
        float m_ProbeBest = 0.0f; // largest rhw this frame
        // Every draw under the probe this frame (texture, blend, depth write, rhw), logged once a second (9.14b: shore textures).
        struct ProbeHit
        {
            float m_Rhw = 0.0f;
            std::uint32_t m_Hash = 0;
            float m_AlphaMid = 0.0f;
            std::uint8_t m_Blend = 0, m_Source = 0, m_Destination = 0, m_DepthWrite = 0, m_Water = 0, m_Shore = 0;
            // B32: what kind of draw it is (sky draw by BatchIsSky, depth, colour and fog at the probe, sky edge state).
            std::uint8_t m_Sky = 0, m_ZEnable = 0, m_ZFunc = 0, m_Fog = 0, m_FogFactor = 0, m_SkyEdge = 0;
            float m_Z = 0.0f;
            std::uint32_t m_Diffuse = 0, m_FogColour = 0, m_Vertices = 0;
            // B36: car pass of the draw (0 = none), part, lowest alpha of the texture, what the car reflections made of it.
            std::uint32_t m_CarMask = 0;
            int m_CarPart = -1;
            std::uint8_t m_AlphaMin = 255, m_Translucent = 0;
            const char * m_CarOutcome = nullptr;
            float m_CarGlassRef = 0.0f;
            char m_CarDetail[256] = {}; // B36: normal, up, reflected direction and frame lookup at the probe
        };
        std::vector<ProbeHit> m_ProbeHits;
        static constexpr float CAR_REFL_GLASS_PAGE = 0.9f; // glass reference of a glass texture on a body part (B34)
        const char * m_CarReflOutcome = nullptr; // B36: why CarReflBatch kept or left out the batch (for the probe)
        float m_CarReflGlassRefLast = 0.0f;
        std::size_t m_ProbeFirst = 0; // index of the probe hit of the batch being flushed
        std::uint64_t m_ProbeLogTick = 0;
        float m_ProbeShown = 0.0f;
        int m_ProbeCursorX = 0; // back buffer pixels
        int m_ProbeCursorY = 0;
        void ProbeBatch();
        void ProbeFlip();
        void DrawProbe();
        // Shows what is in the back buffer.
        void PresentBackBuffer();

        [[nodiscard]] bool IsFrontSelected() const
        {
            return m_SelectedWindow == 1;
        }

        // 'pixel_shader' replaces the plain copy, 'shape' goes to the fourth parameter.
        void Blit(
            ID3D11ShaderResourceView * source,
            ID3D11RenderTargetView * target,
            int target_width,
            int target_height,
            const float * destination_rect,
            const float * source_rect,
            float brightness,
            ID3D11SamplerState * sampler,
            ID3D11PixelShader * pixel_shader = nullptr,
            float shape = 0.0f
        );

        [[nodiscard]] D3D11_RECT ScaledClip() const;

        bool CreateDevice();

        bool CreateShaders();

        bool CreateSwapChain();

        bool CreateFrame(
            int width,
            int height
        );

        void BuildModeList();

        void FitWindow();

        // Exclusive fullscreen at the desktop resolution, also called to return after alt tab.
        void EnterFullscreen();

        // Watches activation of the game window for alt tab.
        void SubclassWindow();

        void RestoreWindowProc();

        // The game window code calls 'SetForegroundWindow' on 'WM_PAINT', after input errors and
        // in its message loop, which pulls it back in front right after Alt+Tab.
        void PatchFocusImports();

        static BOOL WINAPI SetForegroundWindowFilter(
            HWND window
        );

        static HWND WINAPI SetActiveWindowFilter(
            HWND window
        );

        static LRESULT CALLBACK WindowProc(
            HWND window,
            UINT message,
            WPARAM wparam,
            LPARAM lparam
        );

        // Water reflections, '[Water]' in lfsp.ini (doc/BUGS.md B15 §12, way E): the game's baked reflections
        // (queue list 1, alpha blended) are left out; every world draw of the render queue is kept and drawn again,
        // mirrored at the water plane, into an own target, which is laid over the water pixels (stencil bit 1)
        // when the queue ends. The queue list comes from LFSP (lfsp.asi v7f, LfspQueueList).
        struct MirrorBatch
        {
            std::vector<TlVertex> m_Vertices;
            std::vector<std::uint16_t> m_Indices;
            std::vector<std::uint8_t> m_Constants; // DrawConstants of the original draw
            std::uint64_t m_BlendKey = 0; // 0 opaque, 1 alpha blended, 2/3 additive light SrcAlpha/One + One (9.14a)
            bool m_DepthWrite = true;
            std::uint32_t m_Cull = 1;
            std::array<ID3D11ShaderResourceView *, 2> m_Views = {};
            std::array<ID3D11SamplerState *, 2> m_Samplers = {};
            std::uint32_t m_Hash = 0; // texture of stage 0
        };

        int m_ConfigReflections = 0;
        float m_ConfigReflectionStrength = 0.6f;
        std::uint32_t m_ConfigReflectionTint = 0xFFFFFF;
        int m_ConfigRemoveBaked = 1;
        bool m_ConfigGuardBand = true;
        float m_ConfigSkidBias = 0.003f; // '[World] SkidDepthBias' (BUGS.md B22): skid marks moved towards the camera, share of the distance
        float m_ConfigTextureLodBias = 0.0f; // '[World] TextureLodBias' (HD-TEXTUREN.md): added to the game's mip bias, > 0 = blurrier, less shimmer
        bool m_ConfigForceMips = false; // '[World] ForceMips': sample mip levels even where the game turns the mip filter off
        int m_ConfigReflectionDebug = 0;
        std::vector<std::uint32_t> m_ConfigWaterTextures;
        std::vector<std::uint32_t> m_ConfigShoreTextures; // water part (low alpha) joins the water mask
        bool m_ShorePass = false;
        ComPtr<ID3D11BlendState> m_ReflNoColour;
        ComPtr<ID3D11BlendState> m_ReflShoreDebug;
        unsigned m_ReflShoreDraws = 0;
        std::vector<std::uint32_t> m_ConfigBakedTextures;

        int m_QueueList = 0; // list of the batch being collected, 0 = outside the render queue
        bool m_ReflCapture = false; // ApplyPipeline keeps its constants in 'm_ReflConstants'
        std::vector<std::uint8_t> m_ReflConstants;
        std::vector<MirrorBatch> m_ReflBatches;
        std::size_t m_ReflUsed = 0;
        std::size_t m_ReflSkyCount = 0; // batches before the first queue run (sky), kept for every view
        bool m_ReflSkyPhase = true;
        std::vector<std::array<float, 3>> m_ReflWater; // water triangle corners: screen x, y and w
        std::vector<std::uint32_t> m_ReflWaterHash; // texture of each water triangle (measurement)
        int m_ReflLogged = 0;
        D3D11_RECT m_ReflClip = {};
        bool m_ReflCameraValid = false;
        std::array<float, 16> m_ReflClipOfView = {};
        std::array<float, 16> m_ReflViewOfClip = {};
        std::array<float, 4> m_ReflViewport = {};
        std::uint64_t m_ReflFrames = 0;

        ComPtr<ID3D11Texture2D> m_ReflTarget;
        ComPtr<ID3D11RenderTargetView> m_ReflTargetView;
        ComPtr<ID3D11ShaderResourceView> m_ReflTargetSrv;
        // The sky alone, drawn again from the draws of the sky phase; where the mirrored scene has nothing, the
        // composite looks up the reflected sky there (B15, geometry the game never drew).
        ComPtr<ID3D11Texture2D> m_ReflSky;
        ComPtr<ID3D11ShaderResourceView> m_ReflSkyView;
        ComPtr<ID3D11RenderTargetView> m_ReflSkyRtv;
        // How much the water mask mirrors (1 = water, shore pixels 1 - alpha), B15 soft shore.
        ComPtr<ID3D11Texture2D> m_ReflWeight;
        ComPtr<ID3D11RenderTargetView> m_ReflWeightRtv;
        ComPtr<ID3D11ShaderResourceView> m_ReflWeightSrv;
        ComPtr<ID3D11BlendState> m_ReflWeightBlend;
        bool m_ReflSkyRun = false; // the current queue run is the one right after the sky
        int m_ConfigReflectionSky = 1;
        // Cheaper reflections (REFLEXION-WETTER.md 9.13d).
        int m_ConfigReflectionSimpleShader = 0; // '[Water] ReflectionSimpleShader': mirrored draws without the light loops
        float m_ConfigReflectionMinSize = 0.0f; // '[Water] ReflectionMinSize': draws smaller than this (pixels of 1440 lines) are not mirrored
        int m_ConfigReflectionMirrorView = 1; // '[Water] ReflectionMirrorView': 0 = no reflection in the rear view mirror
        float m_ConfigReflectionSkyScale = 1.0f; // '[Water] ReflectionSkyScale': size of the sky image, 1 or 0.5
        // Lights in the water (9.14a): additive draws (lamp glows, light sprites) are mirrored too, into m_ReflLight; the composite
        // brightens them and draws streaks.
        int m_ConfigReflectionLights = 0; // '[Water] ReflectionLights'
        float m_ConfigReflectionLightBoost = 3.0f; // '[Water] ReflectionLightBoost': factor on their colour
        float m_ConfigReflectionWaveSlope = 0.06f; // '[Water] ReflectionWaveSlope': slope of the waves fixed on the water (9.14b)
        float m_ConfigReflectionSunGlint = 0.0f; // '[Water] ReflectionSunGlint': strength of the sun glint on the water (0 = off)
        float m_ConfigReflectionStrengthNight = 0.6f; // '[Water] ReflectionStrengthNight': strength at night (native or LFSP NightGrade)
        int m_ConfigReflectionWorldWaves = 0; // '[Water] ReflectionWorldWaves': waves fixed on the water instead of on the screen
        bool m_NightGradeSeen = false; // LFSP NightGrade reported active for the last draw
        // Weather (LFSP LfspWeatherFx, PLAN.md 15.2): world rgb, sky rgb, fog exponent, snow white, desaturation, slope, test, snow rgb,
        // lightning, mode; read once per frame.
        float m_WeatherFx[16] = {};
        bool m_WeatherFxOn = false; // LFSP reported weather effects for this frame
        bool m_WeatherGrade = false; // storm or lightning: factors of world and sky differ from 1
        std::uint32_t m_WeatherFxFlip = 0xFFFFFFFFu;
        bool m_ConfigSnowWhite = false; // '[Weather] SnowWhite' of lfsp.ini above 0: the track chunk draw is hooked for it
        int m_TrackDrawDepth = 0; // inside the game's track chunk draw (FUN_00450ca0)
        bool m_SnowDraw = false; // the batch being drawn gets snow white
        // snow draws, left out outside the track draw, left out inside (car, water), with slope matrix; car reflection draws with snow, with drops only
        std::uint64_t m_WeatherStats[6] = {};
        // Snow and drops on the cars (LFSP LfspWeatherCar, PLAN.md 15.3): snow, drops, time; light of the snow (world factor of the last frame).
        float m_CarWeather[8] = {};
        bool m_CarWeatherOn = false;
        float m_CarWeatherLight = 1.0f;
        float m_CarWeatherLightNext = 1.0f;
        // Wetness of the track (LFSP LfspWeatherState, PLAN.md 16.1): rain, snow, wetness, snow cover, roof, wind, -, time s.
        // Only read and logged so far (16.2 uses it).
        float m_WeatherState[8] = {};
        bool m_WeatherStateOn = false;
        ULONGLONG m_WeatherStateNextReport = 0;
        float m_CarUpVote = 0.0f; // > 0: "up" of the car (row 1 of object x view) points to the camera side, as expected
        std::uint64_t m_CarUpVotes[2] = {}; // draws with the camera on the "up" side, on the other side
        std::uint64_t m_CarMirrorParts[2] = {}; // car reflection parts with object x view of positive, negative determinant (w4)
        // Rain drops flowing on the cars (w5): per car its place in the world between game steps, the speed from it and the
        // distance the drops have crept, as a phase in the space of the part (no jump when the speed changes).
        struct CarFlowTrack
        {
            float m_Position[3] = {};
            float m_Velocity[3] = {}; // world, m/s, smoothed
            float m_Flow[3] = {};     // space of the part, m
            float m_DirObject[3] = {};
            float m_DirView[3] = {};
            float m_Speed = 0.0f;
            int m_Step = 0;
            std::uint32_t m_Flip = 0xFFFFFFFFu;
            bool m_Used = false;
        };
        CarFlowTrack m_CarFlowTracks[16];
        std::uint64_t m_CarFlowStats[2] = {}; // parts with a track, without (no camera, no matrix)
        float m_CarFlowMaxSpeed = 0.0f;
        int CarFlowTrackFor( const float model_view[16], bool mirrored );
        float m_ConfigCarDropCells = 110.0f; // '[Weather] CarDropCells' of lfsp.ini: drop cells per metre on the car (larger = smaller drops)
        float m_ConfigCarDropShine = 1.0f; // '[Weather] CarDropShine' of lfsp.ini: brightness of glint and bead (w7)
        float m_ConfigCarSnowSoft = 1.0f; // '[Weather] CarSnowSoft' of lfsp.ini: evenness of the car snow (w9; w8: width of its edge)
        float m_ConfigCarSnowBright = 0.5f; // '[Weather] CarSnowBright' of lfsp.ini: brightness of the car snow (w8)
        float m_ConfigCarDropFlow = 0.005f; // '[Weather] CarDropFlow' of lfsp.ini: creep of the drops, m/s per m/s above 8 m/s (w6)
        ULONGLONG m_WeatherNextReport = 0;
        float m_ConfigReflectionWaveDirection = 35.0f; // '[Water] ReflectionWaveDirection': main direction of the waves (degrees in the world)
        float m_ConfigReflectionWaveSpread = 1.0f; // '[Water] ReflectionWaveSpread': spread of the waves about it (0 = all parallel)
        float m_ConfigReflectionWaveHeight = 0.0f; // '[Water] ReflectionWaveHeight': relief height of the waves (m, 0 = flat)
        float m_ConfigReflectionWaveSize = 5.0f; // '[Water] ReflectionWaveSize': wavelengths x this (1 = 0.65 ... 3.5 m)
        // World camera of the queue run (read at its start like the clip; the last good one if the read fails).
        bool m_ReflWorldValid = false;
        float m_ReflWorldRows[9] = {};
        float m_ReflWorldPosition[3] = {};
        std::uint64_t m_ReflWorldReads = 0, m_ReflWorldFails = 0;
        bool ReadWorldCamera( float rows[9], float position[3] );
        bool ReadSunDirection( float direction[3] );
        float m_ConfigReflectionLightStreak = 6.0f; // '[Water] ReflectionLightStreak': step of the streak lookups (pixels at 1080 lines, 0 = none)
        std::uint64_t m_ReflLightsKept = 0;
        ComPtr<ID3D11Texture2D> m_ReflLight;
        ComPtr<ID3D11RenderTargetView> m_ReflLightView;
        ComPtr<ID3D11ShaderResourceView> m_ReflLightSrv;
        bool m_ReflViewIsMain = false; // the current queue run follows the sky phase (main view, not the rear view mirror)
        std::uint64_t m_ReflCulledBelow = 0, m_ReflCulledSmall = 0, m_ReflKept = 0, m_ReflMirrorViewSkipped = 0;
        ComPtr<ID3D11PixelShader> m_MirrorPs;
        int m_ConfigReflectionTwoSided = 1;
        float m_ConfigReflectionWaves = 3.0f;
        float m_ConfigReflectionBlur = 2.0f;
        float m_ConfigReflectionFresnel = 0.3f;
        float m_ConfigShoreFade = 0.27f;
        // '[Water] ShoreWaterLight' (B33): the see-through part of a shore texture takes the light colour of the water (the
        // running colour of WaterEvenColour) instead of the shore's own vertex colour; 0 = off, 1 = fully by transparency.
        float m_ConfigShoreWaterLight = 1.0f;
        float m_ConfigWaterFarFade = 0.3f;
        float m_ReflWaterFar = 0.0f;
        // '[Water] EdgeFade': where the water surface ends against open sky (stencil 0: nothing of the render queue
        // drawn there), the water fades into that sky. Needs a copy of the stencil (bit 2 = solid queue draw).
        float m_ConfigWaterEdgeFade = 40.0f;
        int m_ConfigWaterEvenColour = 1; // '[Water] WaterEvenColour', one vertex colour for all water
        std::array<float, 3> m_WaterColour = { -1.0f, -1.0f, -1.0f };
        float m_ConfigReflectionScale = 0.5f; // '[Water] ReflectionScale', size of the reflection target
        float m_ConfigReflectionGrazing = 1.0f; // '[Water] ReflectionGrazing', strength looking along the water
        std::vector<std::uint32_t> m_ConfigReflectionSkip; // '[Water] ReflectionSkip', textures left out of the mirror
        std::vector<std::uint32_t> m_ConfigFarWaterTextures; // '[Water] FarWaterTextures', water mask but not in the plane fit
        ComPtr<ID3D11Texture2D> m_ReflFrameCopy; // the frame before the composite, for holes of the mirrored scene
        ComPtr<ID3D11ShaderResourceView> m_ReflFrameCopyView;
        ComPtr<ID3D11ShaderResourceView> m_ReflDepthCopyView; // depth of the stencil copy
        ComPtr<ID3D11Texture2D> m_ReflStencil;
        ComPtr<ID3D11ShaderResourceView> m_ReflStencilView;
        // Stencil and depth, sample 0, single sampled for the composite (PackStencil). It reads the stencil up to
        // a hundred times per water pixel; from the multisampled buffer that was most of its cost.
        ComPtr<ID3D11Texture2D> m_ReflMask;
        ComPtr<ID3D11RenderTargetView> m_ReflMaskRtv;
        ComPtr<ID3D11ShaderResourceView> m_ReflMaskSrv;
        ComPtr<ID3D11Texture2D> m_ReflMaskDepth;
        ComPtr<ID3D11RenderTargetView> m_ReflMaskDepthRtv;
        ComPtr<ID3D11ShaderResourceView> m_ReflMaskDepthSrv;
        ComPtr<ID3D11PixelShader> m_ReflPackPs;
        ComPtr<ID3D11PixelShader> m_ReflPackMsPs; // the same, depth buffer multisampled

        ComPtr<ID3D11Texture2D> m_ReflDepth;
        ComPtr<ID3D11DepthStencilView> m_ReflDepthView;
        int m_ReflWidth = 0;
        int m_ReflHeight = 0;
        ComPtr<ID3D11VertexShader> m_MirrorVs;
        ComPtr<ID3D11VertexShader> m_ReflCompositeVs;
        ComPtr<ID3D11PixelShader> m_ReflCompositePs;
        ComPtr<ID3D11Buffer> m_MirrorConstants;
        // Ambient occlusion ('[AmbientOcclusion]' in lfsp.ini, horizon based with a visibility bitmask; shaders::AO).
        int m_ConfigAo = 0;
        int m_ConfigAoQuality = 1; // 'Quality' 0 low, 1 medium, 2 high (slices x samples per side 2x4, 2x10, 4x14)
        int m_ConfigAoShadingRate = 1; // 'ShadingRate' 0 all pixels, 1 checkerboard per frame
        float m_ConfigAoRadius = 1.893f; // 'SampleRadius'
        int m_ConfigAoGrow = 1; // 'IncreaseRadiusWithDistance'
        float m_ConfigAoAmount = 1.0f; // 'Amount'
        // 'FadeDistance': metres along the view axis where the occlusion is down to half; < 0 = the old 'FadeDepth' below.
        float m_ConfigAoFadeDistance = 250.0f;
        float m_ConfigAoFade = 0.333f; // old 'FadeDepth': the same point on the normalised depth z / (F - z (F - 1))
        int m_ConfigAoFilter = 2; // 'FilterQuality' 0 ... 2
        int m_ConfigAoDebug = 0; // 'Debug' 1 = raw occlusion
        float m_ConfigAoFarPlane = 1000.0f; // F of the normalised depth (fade, filter); old key 'FadeFarPlane'
        int m_ConfigAoMethod = 0; // 'Method' 0 standard (approximate weighting), 1 exact weighting of the slices and back face along the sample's ray (GT-VBAO)
        float m_ConfigAoThickness = 1.0f; // 'Thickness': scale of the thickness given to the objects
        float m_ConfigAoFill = 0.35f; // 'Fill' (B35): near occluders thicker with 'Method' 1, 0 = off
        float m_ConfigAoAngleBias = 10.0f; // 'AngleBias' (B35): degrees above the face a rise has to reach to occlude
        int m_ConfigAoDepthMips = 0; // 'DepthMips' 1: far samples from a pyramid of the linear depth
        int m_ConfigAoHalfRes = 0; // 'HalfRes' 1: raw occlusion for a quarter of the pixels (half width and height), then upsampled by depth
        int m_ConfigAoTemporal = 0; // 'Temporal' 1: accumulation over the frames, reprojected with the world camera
        float m_ConfigAoTemporalBlend = 0.85f; // 'TemporalBlend': share of the history
        int m_ConfigAoMultiBounce = 0; // 'MultiBounce' 1: light thrown back by the surroundings (Jimenez et al. 2016)
        int m_ConfigAoFogFade = 0; // 'FogFade' 1: no occlusion where the fog of the game covers the scene
        ComPtr<ID3D11PixelShader> m_AoPs, m_AoFilterPs, m_AoApplyPs, m_AoDepthHalfPs, m_AoDepthMipPs;
        ComPtr<ID3D11PixelShader> m_AoUpsamplePs; // 'HalfRes': half size raw occlusion back to full size
        ComPtr<ID3D11Texture2D> m_AoHalf; // 'HalfRes': raw occlusion at half width and height
        ComPtr<ID3D11RenderTargetView> m_AoHalfRtv;
        ComPtr<ID3D11ShaderResourceView> m_AoHalfSrv;
        // m_AoTemp / m_AoHistory: FilterPS's target alternates between the two, the other is the last frame ('Temporal').
        ComPtr<ID3D11Texture2D> m_AoRaw, m_AoTemp, m_AoHistory;
        ComPtr<ID3D11RenderTargetView> m_AoRawRtv, m_AoTempRtv, m_AoHistoryRtv;
        ComPtr<ID3D11ShaderResourceView> m_AoRawSrv, m_AoTempSrv, m_AoHistorySrv;
        bool m_AoHistoryFlip = false;
        static constexpr int AO_DEPTH_LEVELS = 4; // half ... 1/16 of the render size
        ComPtr<ID3D11Texture2D> m_AoDepthMips;
        ComPtr<ID3D11ShaderResourceView> m_AoDepthMipsSrv; // all levels
        ComPtr<ID3D11ShaderResourceView> m_AoDepthLevelSrv[AO_DEPTH_LEVELS];
        ComPtr<ID3D11RenderTargetView> m_AoDepthLevelRtv[AO_DEPTH_LEVELS];
        // Camera of the last AO frame for the reprojection ('Temporal').
        bool m_AoPrevValid = false;
        std::uint32_t m_AoPrevFlip = 0;
        float m_AoPrevClipOfView[16] = {};
        float m_AoPrevViewport[4] = {};
        float m_AoPrevRows[9] = {};
        float m_AoPrevPosition[3] = {};
        bool m_ReflWorldFresh = false; // the world camera was read at the start of this queue run
        // Fog of the main view's world draws ('FogFade'): -1 none seen, 0 table over z, 1 exp, 2 exp2, 3 linear over z.
        int m_AoFogMode = -1;
        float m_AoFogParams[4] = {}; // start, end, density, storm exponent
        std::array<std::uint8_t, 256> m_AoFogTable = {}; // the game's table at that draw (visibility over z)
        ComPtr<ID3D11Buffer> m_AoConstants;
        ComPtr<ID3D11BlendState> m_AoMultiply;
        ComPtr<ID3D11RasterizerState> m_AoRaster;
        float m_ReflNear = 0.5f; // near plane of the view (R_View + 0x18), n of z = 1 - n / w
        std::uint32_t m_AoFrame = 0;
        std::uint64_t m_AoRuns = 0;
        void DrawAmbientOcclusion();

        std::string m_LfspIni; // path of lfsp.ini
        float m_ConfigCarReflCreaseDeg = 0.0f; // '[CarReflections] CreaseAngle' in degrees (the cosine follows)
        std::unordered_map<std::uint64_t, ComPtr<ID3D11BlendState>> m_ReflBlends;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11DepthStencilState>> m_ReflDepthStates;
        std::unordered_map<std::uint64_t, ComPtr<ID3D11RasterizerState>> m_ReflRasters;

        // Current list of the game's render queue, -1 without LFSP.
        int QueryQueueList();

        // Called before a batch starts: on a change of the queue list the pending batch is flushed, at the end of
        // the queue the reflections are drawn.
        void NoteQueueList();

        bool PrepareSkyImage();
        bool CopyStencil();
        bool PackStencil();
        bool CopyFrame();
        void PrepareReflWeight();
        bool IsWaterTexture() const;
        bool IsFarWaterTexture() const;
        bool IsBakedTexture() const;

        // The game camera: clip (xc, yc, wc) of view and back, viewport fx, fy, cx, cy (as PrepareHeadMesh).
        bool ReadCamera(
            float clip_of_view[16],
            float view_of_clip[16],
            float viewport[4]
        );

        void CaptureMirrorBatch();

        void DrawReflections();

        void ApplyPipeline();

        // Forgets every binding cached by 'ApplyPipeline'.
        void InvalidatePipeline();

        [[nodiscard]] bool IsTextureHandle(
            std::uintptr_t handle
        ) const;

        // Pipeline state whose repeated identical value can be skipped.
        [[nodiscard]] static bool IsCachedState(
            std::uint32_t state
        );

        // Texture stage state, the stage bits of the id select the stage.
        [[nodiscard]] static bool IsStageState(
            std::uint32_t state
        );

        void ResetStateCache();

        // Drops the cached values of states that write the same values as 'state'.
        void ForgetRelatedStates(
            std::uint32_t state
        );

        // Reads a small rectangle straight from the render sized frame, 'false' when it cannot.
        bool ReadRectProbe(
            int x0,
            int y0,
            int x1,
            int y1,
            int x,
            int y,
            int width,
            bool allow_previous,
            std::uint8_t * out
        );

        void ResetProbes();

        ID3D11SamplerState * GetSampler(
            int stage
        );

        void Flush();

        void WidenLines();

        void BeginBatch(
            Primitive primitive,
            std::size_t vertices,
            std::size_t indices
        );

        void PushVertex(
            const std::uint8_t * source
        );

        void SetTextureStageMode(
            int stage,
            std::uint32_t mode
        );

        void StoreState(
            std::uint32_t id,
            std::uint32_t value
        );

    public:

        Driver( const Driver & ) = delete;
        Driver & operator=( const Driver & ) = delete;

        static Driver & GetInstance();




        // Per pixel headlights, PLAN.md 9.6, '[Dx11] Headlights'. Called after the game computed the
        // headlight factors of a scenery mesh, and after each mesh draw.
        void OnHeadlightFactor(
            int count
        );

        void OnMeshDrawn(
            void * renderer
        );

        // Called around the game's track chunk draw FUN_00450ca0 (begin, end). By day the game has no
        // headlights, '[Dx11] HeadlightsDay' lights these meshes per pixel (PLAN.md 9.6c, doc/LICHT.md 7.9).
        void OnDayTrackDraw(
            bool begin
        );

        // Around the track chunk draw for both: snow white marks the track draws (m_TrackDrawDepth), then OnDayTrackDraw.
        void OnTrackChunk(
            bool begin
        );

        // LFSP LfspWeatherFx once per frame into m_WeatherFx (storm, lightning, snow white). True when LFSP reports any.
        bool WeatherFxFrame();

        // Called around the game's skid mark draw 0x451420 (begin, end), '[World] SkidDepthBias' (BUGS.md 19.1).
        void OnSkidDraw(
            bool begin
        )
        {
            // Skid marks in batches of their own: the water reflections leave them out.
            if (m_SkidDraw != begin)
                Flush();
            m_SkidDraw = begin;
        }

        // Called around the game's car part draw R3DCarLOD::Draw 0x43C830 with its pass mask (PLAN.md 9.12b);
        // End gets back what Begin returned. 'room': a 3D room of the menus, drawn without reflections (BUGS.md 21).
        std::uint32_t BeginCarLodDraw(
            std::uint32_t mask,
            bool room,
            const void * object
        );
        void EndCarLodDraw(
            std::uint32_t previous
        );
        // Before each part of R3DCarLOD::Draw with its .crp part and variant (-1: unknown).
        void OnCarPart(
            int part,
            int variant
        );

        // First driver draw inside a day world draw: the game has updated the camera matrices by now.
        void PrepareDayPending();

        // Right before the world draw FUN_00543ae0: by day the camera matrices of the chunk are only
        // final here (FUN_005393a0 runs inside the node chain), so the constants are taken again.
        void OnBeforeMeshDraw();


        void Log(
            const char * format,
            ...
        );

        // Call trace for diagnosing.
        void Trace(
            const char * format,
            ...
        );

        // THRASH entry points.
        AboutBlock * About();

        int Init();

        int Is();

        int SelectDisplay(
            int display
        );

        int SetVideoMode(
            int mode,
            int buffers,
            int depth
        );

        int Restore();

        int Window(
            int window
        );

        void ClearWindow();

        int Clip(
            int x0,
            int y0,
            int x1,
            int y1
        );

        int FlushWindow();

        void PageFlip();

        int Sync(
            int mode
        );

        LockInfo * LockWindow();

        // Draws a frame of the movie player, packed 4:2:2 with 7 bit components, into the front
        // buffer at 'scale' times its size and shows it. Returns 'false' when the game has to draw the frame itself.
        bool MovieBlit(
            const void * pixels,
            int pitch,
            int width,
            int height,
            int x,
            int y,
            int scale
        );

        int UnlockWindow(
            LockInfo * lock
        );

        int ReadRect(
            int x,
            int y,
            int width,
            int height,
            void * out
        );

        int WriteRect(
            int x,
            int y,
            int width,
            int height,
            const void * in
        );

        Texture * TextureAlloc(
            int width,
            int height,
            int format,
            int palette_format,
            int mips
        );

        int TextureUpdate(
            Texture * texture,
            const void * pixels,
            const std::uint32_t * palette
        );

        int TextureFree(
            Texture * texture
        );

        int TextureReset();

        int SetTexture(
            std::uintptr_t handle
        );

        std::intptr_t SetState(
            std::uint32_t id,
            std::uintptr_t value
        );

        std::intptr_t GetState(
            std::uint32_t id
        );

        void DrawTriangles(
            const std::uint8_t * vertices,
            const int * indices,
            int count,
            int stride_indices
        );

        // Single triangle or quad from separate vertex pointers, 'drawtri' and 'drawquad'.
        void DrawPolygon(
            const std::uint8_t * const * corners,
            int count
        );

        void DrawQuads(
            const std::uint8_t * vertices,
            const int * indices,
            int count
        );

        void DrawStrip(
            const std::uint8_t * vertices,
            const int * indices,
            int triangles,
            bool fan
        );

        void DrawLines(
            const std::uint8_t * vertices,
            const int * indices,
            int count,
            bool strip
        );

        void DrawPoints(
            const std::uint8_t * vertices,
            const int * indices,
            int count
        );

        void DrawRaw(
            const std::uint32_t * description
        );

        [[nodiscard]] std::uint32_t VertexStride() const
        {
            return m_TwoUvSets ? 40u : 32u;
        }
    };

    // Puts back what the driver changed in the game and removes its exception handler (DllMain, unload).
    void RestoreGamePatches();
}