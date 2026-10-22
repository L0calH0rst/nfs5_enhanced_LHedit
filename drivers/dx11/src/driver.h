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
        float m_ExtraColour[7][4];
        float m_ExtraViewToLight[7][16];
        // High beam of the player's light (PLAN.md 9.10): factors on the z, x and y scales.
        float m_HeadHigh[4];
        // Tail light glow of all racers (PLAN.md 11.5): x count; shape: rear offset, reach, lamp x, lamp spread.
        std::int32_t m_Rear[4];
        float m_RearShape[4];
        float m_RearLeft[16][4];
        float m_RearRight[16][4];
        float m_RearViewToLight[16][16];
        // Test switches (BUGS.md B21): x vertex alpha forced to 1.
        float m_Test[4];
    };

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
        // Size of all levels as uploaded, for the memory report of '[Dx11] Measure'.
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

        // Measuring build for PLAN.md 3T.1, '[Dx11] Measure'. 1 logs the state of textured
        // draws and the texture formats, 2 also tints draws that blend and write depth.
        struct MeasureDraw
        {
            int m_Id = 0;
            std::uint64_t m_Draws = 0;
            std::uint64_t m_Primitives = 0;
        };

        // Texture coordinate range per translucent texture, PLAN.md 3C (B7).
        struct MeasureUv
        {
            float m_MinU = 1e9f, m_MaxU = -1e9f, m_MinV = 1e9f, m_MaxV = -1e9f;
            std::uint64_t m_Draws = 0;
        };

        int m_ConfigMeasure = 0;
        std::unordered_map<const Texture *, MeasureUv> m_MeasureUv;

        void MeasureUvRange();

        // Light passes, PLAN.md 8 (headlights). Draws with a second texture stage and blends that
        // multiply or add onto the frame, per texture pair and state. Their textures are dumped
        // once as BMP next to the log, and the mean vertex colour of opaque world draws shows
        // whether the game lights the vertices instead.
        struct MeasurePass
        {
            int m_Id = 0;
            std::uint64_t m_Draws = 0;
            std::uint64_t m_Primitives = 0;
            float m_MinU0 = 1e9f, m_MaxU0 = -1e9f, m_MinV0 = 1e9f, m_MaxV0 = -1e9f;
            float m_MinU1 = 1e9f, m_MaxU1 = -1e9f, m_MinV1 = 1e9f, m_MaxV1 = -1e9f;
        };

        std::unordered_map<std::string, MeasurePass> m_MeasurePasses;
        std::unordered_map<std::uint32_t, bool> m_MeasureDumps;
        std::array<double, 6> m_MeasureColour = {};
        std::uint64_t m_MeasureColourVertices = 0;

        // Seconds since the driver started, the periodic report runs every 10 s so test drives can
        // be matched to the clock (PLAN.md 9.2).
        ULONGLONG m_MeasureStart = ::GetTickCount64();
        ULONGLONG m_MeasureNextReport = 0;

        [[nodiscard]] double MeasureSeconds() const
        {
            return static_cast<double>(::GetTickCount64() - m_MeasureStart) / 1000.0;
        }

        void MeasurePassCall();

        // One-frame snapshot on Scroll Lock (PLAN.md 9.2c): every draw of the next frame per texture and
        // blend, with mean vertex colour weighted by covered screen area, to compare lights off and on.
        struct MeasureSnap
        {
            const Texture * m_Texture = nullptr;
            bool m_Blend = false;
            std::uint32_t m_Src = 0;
            std::uint32_t m_Dst = 0;
            std::uint64_t m_Draws = 0;
            std::uint64_t m_Vertices = 0;
            double m_Area = 0.0;
            std::array<double, 3> m_Colour = {};
            // B15: game cull mode, screen rectangle and area by winding, so a mirrored copy of an object
            // (flipped winding, below the original) can be told apart from the original (BUGS.md §12).
            std::uint32_t m_Cull = 0;
            int m_List = -1;
            double m_AreaCw = 0.0;
            float m_MinX = 1e9f;
            float m_MinY = 1e9f;
            float m_MaxX = -1e9f;
            float m_MaxY = -1e9f;
        };

        std::unordered_map<std::string, MeasureSnap> m_MeasureSnap;
        bool m_MeasureSnapActive = false;
        bool m_MeasureSnapKey = false;
        int m_MeasureSnapCount = 0;

        // Draw calls of the snapshot frame per call stack and texture. The game has no frame
        // pointers, so the stack is scanned for return addresses into its code (a call right before).
        struct MeasureCaller
        {
            std::array<void *, 6> m_Stack = {};
            std::uint32_t m_Hash = 0;
            bool m_Blend = false;
            std::uint32_t m_Src = 0;
            std::uint32_t m_Dst = 0;
            bool m_Lit = false;
            std::uint64_t m_Calls = 0;
        };

        std::unordered_map<std::string, MeasureCaller> m_MeasureCallers;

        // Light lists seen by the lighting node in the snapshot frame, by content.
        struct MeasureLights
        {
            int m_Count = 0;
            std::array<int, 8> m_Types = {};
            std::array<std::array<float, 4>, 8> m_Vectors = {};
            std::array<std::array<float, 4>, 8> m_Colours = {};
            std::uint64_t m_Calls = 0;
        };

        std::unordered_map<std::string, MeasureLights> m_MeasureLights;
        std::uint64_t m_MeasureLightCalls = 0;
        bool m_LitSinceDraw = false;

        void InstallLightHook();

        // Hardware write watch on the road colour, armed from the snapshot frame, reported once it ran out.
        bool m_WatchArmed = false;
        bool m_WatchDoneThisSnap = false;
        bool m_HeadlightLogged = false;
        bool m_HeadlightMapDumped = false;

        // '[Dx11] Headlights': 0 game vertex light, 1 per pixel instead, 2 both (comparison).
        int m_ConfigHeadlights = 0;
        // '[Dx11] HeadlightsDay': 0 off, 1 when the car's light switch is on, 2 always.
        int m_ConfigHeadlightsDay = 0;
        std::uint32_t m_DayLightFlip = 0xFFFFFFFFu;
        // A track chunk is being drawn with the day light, OnMeshDrawn keeps the flag until it ends.
        bool m_DayMesh = false;
        // '[Dx11] HeadlightsDayStrength', 'HeadlightsDayColour', 'HeadlightsDayRange'.
        float m_ConfigDayStrength = 1.0f;
        std::uint32_t m_ConfigDayColour = 0x808080u;
        float m_ConfigDayRange = 1.0f;

        // Why the day path ran or stopped, reported every 10 s with '[Dx11] Measure' (PLAN.md 9.6c).
        struct DayStats
        {
            std::uint64_t m_Calls = 0;
            std::uint64_t m_Night = 0;
            std::uint64_t m_NoCar = 0;
            std::uint64_t m_SwitchOff = 0;
            std::uint64_t m_Prepared = 0;
            std::uint64_t m_Failed = 0;
            std::uint64_t m_Draws = 0;
            std::uint64_t m_LitDraws = 0;
            std::uint8_t m_Lights[2] = {};
            std::uint16_t m_NightFlag = 0;
            std::uint32_t m_Index = 0;
        };

        // One time log lines of the day path, written without '[Dx11] Measure' (doc/LICHT.md 7.10).
        std::uint32_t m_DayLogged = 0;
        // Which step of PrepareHeadMesh failed last: 1 renderer or camera, 2 inverse, 3 viewport, 4 map.
        int m_HeadFailStep = 0;
        // A world draw of a day track chunk has started, the first driver draw inside it prepares the light.
        bool m_DayPending = false;
        int m_DayFailDumps = 0;
        // Column of the camera projection that gives w: 3 at night, 2 when column 3 is (0, 0, 0, 1) (doc/LICHT.md 7.12).
        int m_HeadWColumn = 3;
        std::uint32_t m_HeadColumnLogged = 0;

        DayStats m_DayStats;
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
        static constexpr int MAX_CAR_LIGHTS = 7;
        struct CarLight
        {
            std::uintptr_t m_Car = 0;
            std::array<float, 16> m_ObjectToLight = {};
            std::array<float, 4> m_Colour = {};
        };
        int m_ConfigOpponents = 1;
        float m_ConfigOpponentsStrength = 1.0f;
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
        // track mesh (cars, signs, objects), 2 = the same with 4 x colour on those draws to see which ones they are, 0 = track only.
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
        bool m_HeadValidated = false;
        std::array<float, 16> m_HeadViewToLight = {}; // screen side (xc, yc, wc, 1) to headlight space
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

        void CheckWatch();

        void MeasureSnapBatch();
        void MeasureSnapReport();

        void MeasureDump(
            const Texture * texture
        );

        // B4 fix, PLAN.md 3T.2, '[Dx11] AlphaEdgeFix'. The game blends most of the world with
        // depth writes on and an alpha reference near zero, so faint edge texels write depth
        // and hide alpha geometry drawn later. Texels below 'AlphaEdgeRef' are drawn in a
        // second pass without depth writes.
        bool m_ConfigAlphaEdgeFix = false;
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

        // 0 normal draw, 1 opaque part only, 2 faint part only without depth writes.
        int m_EdgePass = 0;

        // B8, PLAN.md 3C, '[Dx11] ForceTwoSided'. Adds the faces the game culls in a second pass
        // pushed slightly back in depth, so a face the model already has on the other side
        // stays in front and does not flicker against it (B11).
        bool m_ConfigForceTwoSided = false;
        bool m_BackPass = false;

        [[nodiscard]] bool BatchTranslucent() const;
        bool m_MeasureSuspect = false;
        std::unordered_map<std::uint64_t, MeasureDraw> m_MeasureDraws;
        std::unordered_map<std::uint64_t, std::uint64_t> m_MeasureAllocs;
        int m_MeasureDynamic = 0;
        std::uint64_t m_MeasureTextureBytes = 0;

        void MeasureDrawCall();

        void MeasureReport(
            const char * reason
        );

        // Pipeline objects.
        ComPtr<ID3D11VertexShader> m_SceneVs;
        ComPtr<ID3D11PixelShader> m_ScenePs;
        ComPtr<ID3D11InputLayout> m_SceneLayout;
        ComPtr<ID3D11VertexShader> m_PresentVs;
        ComPtr<ID3D11PixelShader> m_PresentPs;
        ComPtr<ID3D11VertexShader> m_ClearVs;
        ComPtr<ID3D11PixelShader> m_ClearPs;
        ComPtr<ID3D11Buffer> m_DrawConstants;
        ComPtr<ID3D11Buffer> m_PresentConstants;
        ComPtr<ID3D11Buffer> m_ClearConstants;
        ComPtr<ID3D11Buffer> m_VertexBuffer;
        ComPtr<ID3D11Buffer> m_IndexBuffer;
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
        float m_ProbeShown = 0.0f;
        int m_ProbeCursorX = 0; // back buffer pixels
        int m_ProbeCursorY = 0;
        void ProbeBatch();
        void ProbeFlip();
        void DrawProbe();

        [[nodiscard]] bool IsFrontSelected() const
        {
            return m_SelectedWindow == 1;
        }

        void Blit(
            ID3D11ShaderResourceView * source,
            ID3D11RenderTargetView * target,
            int target_width,
            int target_height,
            const float * destination_rect,
            const float * source_rect,
            float brightness,
            ID3D11SamplerState * sampler
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
            std::uint64_t m_BlendKey = 0;
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
        int m_ReflTrace = 0;
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
        int m_ConfigReflectionTwoSided = 1;
        float m_ConfigReflectionWaves = 3.0f;
        float m_ConfigReflectionBlur = 2.0f;
        float m_ConfigReflectionFresnel = 0.3f;
        float m_ConfigShoreFade = 0.27f;
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
        ComPtr<ID3D11PixelShader> m_ReflCompositeMsPs; // the same, stencil copy multisampled
        ComPtr<ID3D11Texture2D> m_ReflDepth;
        ComPtr<ID3D11DepthStencilView> m_ReflDepthView;
        int m_ReflWidth = 0;
        int m_ReflHeight = 0;
        ComPtr<ID3D11VertexShader> m_MirrorVs;
        ComPtr<ID3D11VertexShader> m_ReflCompositeVs;
        ComPtr<ID3D11PixelShader> m_ReflCompositePs;
        ComPtr<ID3D11Buffer> m_MirrorConstants;
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

        // Return address of the game's draw call, for the F11 snapshot (PLAN.md 9.4).
        void NoteCaller(
            void * address
        );

        // Called from the hooked vertex lighting node of porsche.exe (0x00574440, PLAN.md 9.4c).
        void NoteLightNode(
            void * node
        );

        // Called after the game's world draw FUN_00543ae0 (PLAN.md 9.4d): on the road texture of the
        // snapshot frame, finds the vertex colour in the source streams and watches it for writes.
        // Called after the game's headlight factor FUN_00452910 (PLAN.md 9.5): logs its inputs, the
        // light space coordinates and factors of the first vertices, and dumps the light map once.
        void NoteHeadlight(
            int count,
            const float * positions,
            const float * matrix
        );

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

        // Called around the game's skid mark draw 0x451420 (begin, end), '[World] SkidDepthBias' (BUGS.md 19.1).
        void OnSkidDraw(
            bool begin
        )
        {
            m_SkidDraw = begin;
        }

        // First driver draw inside a day world draw: the game has updated the camera matrices by now.
        void PrepareDayPending();

        // Right before the world draw FUN_00543ae0: by day the camera matrices of the chunk are only
        // final here (FUN_005393a0 runs inside the node chain), so the constants are taken again.
        void OnBeforeMeshDraw();

        void NoteRoadDraw(
            void * renderer,
            const void * positions,
            const void * second,
            const void * third,
            std::uint32_t count
        );

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
}