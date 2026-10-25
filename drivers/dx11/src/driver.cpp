#include "driver.h"

#include <d3dcompiler.h>
#include <intrin.h>

#ifndef _MSC_VER
#ifndef _ReturnAddress
#define _ReturnAddress() __builtin_return_address(0)
#endif
#ifndef _AddressOfReturnAddress
#define _AddressOfReturnAddress() __builtin_frame_address(0)
#endif
#endif
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "formats.h"
#include "shaders.h"

extern HMODULE g_Module;

namespace dx11z
{
    namespace
    {
        using CompileFn = HRESULT( WINAPI * )(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
            ID3DBlob **, ID3DBlob **);

        constexpr std::uint32_t ABOUT_SIGNATURE = 0x44334437;
        constexpr std::uint32_t DRIVER_VERSION = 0x6B;
        constexpr std::uint32_t TEXTURE_MEMORY = 512u * 1024u * 1024u;

        // Vendor class reported to the game, 0x10 is what dx7z returns for current cards.
        constexpr std::int32_t SUBTYPE = 0x10;
        constexpr std::int32_t DX_VERSION = 11;
        constexpr float DEPTH_BIAS_UNIT = 1.0f / 32768.0f;
        constexpr std::size_t RAW_VERTICES = 65536;
        constexpr std::size_t RAW_INDICES = RAW_VERTICES * 4;

        const char AUTHOR[] = "NFS5 Enhanced D3D11 THRASH driver by iArtorias";

        const char * FormatName(
            int format
        )
        {
            switch (format)
            {
            case formats::P4: return "P4";
            case formats::P8: return "P8";
            case formats::ARGB1555: return "ARGB1555";
            case formats::RGB565: return "RGB565";
            case formats::RGB888: return "RGB888";
            case formats::ARGB8888: return "ARGB8888";
            case formats::ARGB4444: return "ARGB4444";
            case formats::DXT1: return "DXT1";
            case formats::DXT3: return "DXT3";
            default: return "?";
            }
        }

        struct PresentConstants
        {
            float m_Rect[4];
            float m_Params[4];
            float m_Source[4];
        };

        struct ClearConstants
        {
            float m_Rect[4];
            float m_Target[4];
            float m_Color[4];
        };

        [[nodiscard]] float AsFloat(
            std::uint32_t bits
        )
        {
            float value;
            std::memcpy( &value, &bits, sizeof( value ) );
            return value;
        }

        [[nodiscard]] std::uint32_t AsBits(
            float value
        )
        {
            std::uint32_t bits;
            std::memcpy( &bits, &value, sizeof( bits ) );
            return bits;
        }

        void ColorToFloat4(
            std::uint32_t color,
            float * out
        )
        {
            out[0] = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
            out[1] = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
            out[2] = static_cast<float>(color & 0xFF) / 255.0f;
            out[3] = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
        }

        // D3DBLEND to D3D11, the alpha channel version maps colour factors to alpha ones.
        [[nodiscard]] D3D11_BLEND ToBlend(
            std::uint32_t blend,
            bool alpha
        )
        {
            switch (blend)
            {
                case 1:
                    return D3D11_BLEND_ZERO;

                case 2:
                    return D3D11_BLEND_ONE;

                case 3:
                    return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;

                case 4:
                    return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;

                case 5:
                    return D3D11_BLEND_SRC_ALPHA;

                case 6:
                    return D3D11_BLEND_INV_SRC_ALPHA;

                case 7:
                    return D3D11_BLEND_DEST_ALPHA;

                case 8:
                    return D3D11_BLEND_INV_DEST_ALPHA;

                case 9:
                    return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;

                case 10:
                    return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;

                case 11:
                    return D3D11_BLEND_SRC_ALPHA_SAT;

                default:
                    return D3D11_BLEND_ONE;
            }
        }

        [[nodiscard]] D3D11_TEXTURE_ADDRESS_MODE ToAddress(
            std::uint32_t mode
        )
        {
            switch (mode)
            {
                case 2:
                    return D3D11_TEXTURE_ADDRESS_MIRROR;

                case 3:
                    return D3D11_TEXTURE_ADDRESS_CLAMP;
                case 4:
                    return D3D11_TEXTURE_ADDRESS_BORDER;

                default:
                    return D3D11_TEXTURE_ADDRESS_WRAP;
            }
        }

        [[nodiscard]] D3D11_COMPARISON_FUNC ToCompare(
            std::uint32_t func
        )
        {
            if (func < 1 || func > 8)
            {
                return D3D11_COMPARISON_ALWAYS;
            }

            return static_cast<D3D11_COMPARISON_FUNC>(func);
        }

        [[nodiscard]] D3D11_STENCIL_OP ToStencilOp(
            std::uint32_t op
        )
        {
            if (op < 1 || op > 8)
            {
                return D3D11_STENCIL_OP_KEEP;
            }

            return static_cast<D3D11_STENCIL_OP>(op);
        }
    }

    namespace
    {
        void ToRgb565(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            std::uint8_t * destination,
            std::uint32_t destination_pitch,
            int width,
            int height
        )
        {
            for (int y = 0; y < height; ++y)
            {
                const auto * in = reinterpret_cast<const std::uint32_t *>( source + static_cast<std::size_t>( y ) * source_pitch );
                auto * out = reinterpret_cast<std::uint16_t *>( destination + static_cast<std::size_t>( y ) * destination_pitch );
                for (int x = 0; x < width; ++x)
                {
                    const std::uint32_t c = in[x];
                    out[x] = static_cast<std::uint16_t>( ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F) );
                }
            }
        }

        void FromRgb565(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            std::uint8_t * destination,
            std::uint32_t destination_pitch,
            int width,
            int height
        )
        {
            for (int y = 0; y < height; ++y)
            {
                const auto * in = reinterpret_cast<const std::uint16_t *>( source + static_cast<std::size_t>( y ) * source_pitch );
                auto * out = reinterpret_cast<std::uint32_t *>( destination + static_cast<std::size_t>( y ) * destination_pitch );
                for (int x = 0; x < width; ++x)
                {
                    const std::uint32_t v = in[x];
                    const std::uint32_t r = (v >> 11) & 31;
                    const std::uint32_t g = (v >> 5) & 63;
                    const std::uint32_t b = v & 31;
                    out[x] = 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
                }
            }
        }

        // Same spellings as the booleans of the ASI ('1', 'true', 'on', 'yes' and the opposites).
        bool ReadIniBool(
            const char * key,
            bool fallback,
            const std::string & ini
        )
        {
            char buffer[16] = {};
            ::GetPrivateProfileStringA( "Dx11", key, "", buffer, sizeof( buffer ), ini.c_str() );

            std::string value = buffer;
            value.erase( std::remove_if( value.begin(), value.end(), [] ( unsigned char ch ) { return std::isspace( ch ) != 0; } ), value.end() );
            std::transform( value.begin(), value.end(), value.begin(), [] ( unsigned char ch ) { return static_cast<char>(std::tolower( ch )); } );

            if (value == "1" || value == "true" || value == "on" || value == "yes")
                return true;

            if (value == "0" || value == "false" || value == "off" || value == "no")
                return false;

            return fallback;
        }
    }

    namespace
    {
        using SetForegroundWindowFn = BOOL( WINAPI * )(HWND);
        using SetActiveWindowFn = HWND( WINAPI * )(HWND);

        SetForegroundWindowFn g_SetForegroundWindow = nullptr;
        SetActiveWindowFn g_SetActiveWindow = nullptr;

        // Replaces one import of a module, returns the previous target.
        void * PatchImport(
            HMODULE module,
            const char * dll,
            const char * name,
            void * replacement
        )
        {
            auto * base = reinterpret_cast<std::uint8_t *>(module);
            const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
            const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
            const IMAGE_DATA_DIRECTORY & directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

            if (directory.VirtualAddress == 0)
            {
                return nullptr;
            }

            for (auto * entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress); entry->Name != 0; ++entry)
            {
                if (_stricmp( reinterpret_cast<const char *>(base + entry->Name), dll ) != 0 || entry->OriginalFirstThunk == 0)
                    continue;

                auto * names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->OriginalFirstThunk);
                auto * slots = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->FirstThunk);

                for (; names->u1.AddressOfData != 0; ++names, ++slots)
                {
                    if (IMAGE_SNAP_BY_ORDINAL( names->u1.Ordinal ))
                        continue;

                    const auto * by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);

                    if (std::strcmp( reinterpret_cast<const char *>(by_name->Name), name ) != 0)
                        continue;

                    DWORD protection = 0;
                    ::VirtualProtect( &slots->u1.Function, sizeof( slots->u1.Function ), PAGE_READWRITE, &protection );
                    void * previous = reinterpret_cast<void *>(slots->u1.Function);
                    slots->u1.Function = reinterpret_cast<std::uintptr_t>(replacement);
                    ::VirtualProtect( &slots->u1.Function, sizeof( slots->u1.Function ), protection, &protection );
                    return previous;
                }
            }

            return nullptr;
        }
    }

    Driver & Driver::GetInstance()
    {
        static Driver * instance = new Driver();
        return *instance;
    }

    void Driver::Log(
        const char * format,
        ...
    )
    {
        if (m_LogPath.empty())
        {
            char path[MAX_PATH] = {};
            ::GetModuleFileNameA( g_Module, path, MAX_PATH );

            m_LogPath = path;

            const std::size_t dot = m_LogPath.find_last_of( '.' );

            if (dot != std::string::npos)
            {
                m_LogPath.resize( dot );
            }

            m_LogPath += ".log";

            if (FILE * file = std::fopen( m_LogPath.c_str(), "w" ))
            {
                std::fclose( file );
            }
        }

        char buffer[1024];
        va_list args;
        va_start( args, format );
        std::vsnprintf( buffer, sizeof( buffer ), format, args );
        va_end( args );

        if (FILE * file = std::fopen( m_LogPath.c_str(), "a" ))
        {
            std::fprintf( file, "%s\n", buffer );
            std::fclose( file );
        }
    }


    void Driver::Trace(
        const char * format,
        ...
    )
    {
        if (m_TraceLines <= 0)
            return;

        --m_TraceLines;
        char buffer[512];
        va_list args;
        va_start( args, format );
        std::vsnprintf( buffer, sizeof( buffer ), format, args );
        va_end( args );
        Log( "[%u] %s", m_FlipCount, buffer );
    }

    // Device and resources.

    bool Driver::CreateDevice()
    {
        if (m_Device)
            return true;

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained = {};
        HRESULT hr = ::D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
            static_cast<UINT>(std::size( levels )), D3D11_SDK_VERSION, m_Device.Put(), &obtained, m_Context.Put() );

        if (FAILED( hr ))
        {
            Log( "D3D11CreateDevice failed %08lX", static_cast<unsigned long>(hr) );
            return false;
        }

        Log( "D3D11 device created, feature level %04X", static_cast<unsigned>(obtained) );

        if (!CreateShaders())
            return false;

        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        desc.ByteWidth = static_cast<UINT>(RAW_VERTICES * sizeof( TlVertex ));
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        m_Device->CreateBuffer( &desc, nullptr, m_VertexBuffer.Put() );

        desc.ByteWidth = static_cast<UINT>(RAW_INDICES * sizeof( std::uint16_t ));
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        m_Device->CreateBuffer( &desc, nullptr, m_IndexBuffer.Put() );

        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = sizeof( DrawConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_DrawConstants.Put() );
        desc.ByteWidth = sizeof( PresentConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_PresentConstants.Put() );
        desc.ByteWidth = sizeof( ClearConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_ClearConstants.Put() );
        desc.ByteWidth = 240; // MirrorConstants of the scene shader, also the constants of the reflection composite
        m_Device->CreateBuffer( &desc, nullptr, m_MirrorConstants.Put() );

        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        m_Device->CreateSamplerState( &sampler, m_PresentSampler.Put() );
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        m_Device->CreateSamplerState( &sampler, m_PointSampler.Put() );

        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_Device->CreateBlendState( &blend, m_OpaqueBlend.Put() );
        m_Device->CreateBlendState( &blend, m_ClearColorBlend.Put() );
        blend.RenderTarget[0].RenderTargetWriteMask = 0;
        m_Device->CreateBlendState( &blend, m_ClearDepthOnlyBlend.Put() );

        D3D11_RASTERIZER_DESC raster = {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        m_Device->CreateRasterizerState( &raster, m_PlainRaster.Put() );

        D3D11_DEPTH_STENCIL_DESC depth = {};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth.StencilEnable = TRUE;
        depth.StencilReadMask = 0xFF;
        depth.StencilWriteMask = 0xFF;
        depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
        depth.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
        depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
        depth.BackFace = depth.FrontFace;
        m_Device->CreateDepthStencilState( &depth, m_ClearDepth.Put() );

        D3D11_DEPTH_STENCIL_DESC color_only = {};
        m_Device->CreateDepthStencilState( &color_only, m_ClearColorOnly.Put() );

        m_BatchVertices.reserve( BATCH_VERTICES );
        m_BatchIndices.reserve( BATCH_INDICES );

        // Full, so the first batch maps the new buffers with a discard.
        m_VertexCursor = RAW_VERTICES;
        m_IndexCursor = RAW_INDICES;
        InvalidatePipeline();
        return true;
    }


    bool Driver::CreateShaders()
    {
        m_Compiler = ::LoadLibraryA( "d3dcompiler_47.dll" );

        if (m_Compiler == nullptr)
        {
            Log( "d3dcompiler_47.dll not found" );
            return false;
        }

        const auto compile = reinterpret_cast<CompileFn>(reinterpret_cast<void *>(::GetProcAddress( m_Compiler, "D3DCompile" )));

        if (compile == nullptr)
        {
            return false;
        }

        auto build = [&] ( const char * source, std::size_t length, const char * entry, const char * profile, ComPtr<ID3DBlob> & blob,
            const D3D_SHADER_MACRO * defines = nullptr )
        {
            ComPtr<ID3DBlob> errors;
            const HRESULT hr = compile( source, length, nullptr, defines, nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.Put(),
                errors.Put() );

            if (FAILED( hr ))
            {
                Log( "Shader %s failed: %s", entry, errors ? static_cast<const char *>(errors->GetBufferPointer()) : "?" );
                return false;
            }

            return true;
        };

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;

        if (!build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MainVS", "vs_4_0", vs ) ||
            !build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MainPS", "ps_4_0", ps ))
        {
            return false;
        }

        m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_SceneVs.Put() );
        m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ScenePs.Put() );

        const D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        m_Device->CreateInputLayout( layout, static_cast<UINT>(std::size( layout )), vs->GetBufferPointer(), vs->GetBufferSize(), m_SceneLayout.Put() );

        if (!build( shaders::PRESENT, sizeof( shaders::PRESENT ) - 1, "MainVS", "vs_4_0", vs ) ||
            !build( shaders::PRESENT, sizeof( shaders::PRESENT ) - 1, "MainPS", "ps_4_0", ps ))
        {
            return false;
        }

        m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_PresentVs.Put() );
        m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_PresentPs.Put() );

        if (!build( shaders::CLEAR, sizeof( shaders::CLEAR ) - 1, "MainVS", "vs_4_0", vs ) ||
            !build( shaders::CLEAR, sizeof( shaders::CLEAR ) - 1, "MainPS", "ps_4_0", ps ))
        {
            return false;
        }

        m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_ClearVs.Put() );
        m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ClearPs.Put() );

        // Water reflections (B15), optional: without them the game runs as before.
        if (build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MirrorVS", "vs_4_0", vs ))
        {
            m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_MirrorVs.Put() );
        }
        if (build( shaders::REFLECT, sizeof( shaders::REFLECT ) - 1, "MainVS", "vs_4_0", vs ) &&
            build( shaders::REFLECT, sizeof( shaders::REFLECT ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_ReflCompositeVs.Put() );
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ReflCompositePs.Put() );
        }
        const D3D_SHADER_MACRO multisampled[] = { { "STENCIL_MS", "1" }, { nullptr, nullptr } };
        if (build( shaders::REFLECT, sizeof( shaders::REFLECT ) - 1, "MainPS", "ps_4_1", ps, multisampled ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ReflCompositeMsPs.Put() );
        }

        return m_SceneVs && m_ScenePs && m_SceneLayout && m_PresentVs && m_PresentPs && m_ClearVs && m_ClearPs;
    }


    void Driver::FitWindow()
    {
        if (m_Window == nullptr && m_Callbacks[1] != 0)
            m_Window = reinterpret_cast<HWND( * )()>(m_Callbacks[1])();

        if (m_Window == nullptr)
            return;

        SubclassWindow();

        MONITORINFO info = {};
        info.cbSize = sizeof( info );
        ::GetMonitorInfoA( ::MonitorFromWindow( m_Window, MONITOR_DEFAULTTOPRIMARY ), &info );

        const RECT & area = info.rcMonitor;
        const RECT & work = info.rcWork;

        using GetDpiFn = UINT( WINAPI * )(HWND);
        const auto get_dpi = reinterpret_cast<GetDpiFn>(reinterpret_cast<void *>(::GetProcAddress( ::GetModuleHandleA( "user32.dll" ), "GetDpiForWindow" )));

        Log( "Monitor %ldx%ld, window DPI %u, mode %d", area.right - area.left, area.bottom - area.top, get_dpi != nullptr ? get_dpi( m_Window ) : 0u,
            m_ConfigWindowMode );

        if (m_ConfigWindowMode == WINDOW_WINDOWED)
        {
            // Placed once, later mode changes keep the size and position the player chose.
            if (m_WindowPlaced)
            {
                RECT client = {};
                ::GetClientRect( m_Window, &client );

                m_BackBufferWidth = client.right - client.left;
                m_BackBufferHeight = client.bottom - client.top;
                return;
            }

            m_WindowPlaced = true;

            const int work_width = work.right - work.left;
            const int work_height = work.bottom - work.top;
            int width = m_ConfigWindowWidth;
            int height = m_ConfigWindowHeight;

            if (width <= 0 || height <= 0)
            {
                // Three quarters of the work area with the monitor aspect ratio.
                width = work_width * 3 / 4;
                height = width * (area.bottom - area.top) / std::max<LONG>( area.right - area.left, 1 );

                if (height > work_height * 3 / 4)
                {
                    height = work_height * 3 / 4;
                    width = height * (area.right - area.left) / std::max<LONG>( area.bottom - area.top, 1 );
                }
            }

            const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            const DWORD ex_style = static_cast<DWORD>(::GetWindowLongPtrA( m_Window, GWL_EXSTYLE )) & ~static_cast<DWORD>(WS_EX_TOPMOST);
            RECT rect = { 0, 0, width, height };
            ::AdjustWindowRectEx( &rect, style, FALSE, ex_style );
            const int outer_width = rect.right - rect.left;
            const int outer_height = rect.bottom - rect.top;
            const int x = work.left + std::max( (work_width - outer_width) / 2, 0 );
            const int y = work.top + std::max( (work_height - outer_height) / 2, 0 );

            ::SetWindowLongPtrA( m_Window, GWL_STYLE, static_cast<LONG_PTR>(style) );
            ::SetWindowLongPtrA( m_Window, GWL_EXSTYLE, static_cast<LONG_PTR>(ex_style) );
            ::SetWindowPos( m_Window, HWND_NOTOPMOST, x, y, outer_width, outer_height, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS );

            m_BackBufferWidth = width;
            m_BackBufferHeight = height;
            return;
        }

        // Borderless and exclusive fullscreen cover the whole monitor, the frame is scaled into it.
        ::SetWindowLongPtrA( m_Window, GWL_STYLE, WS_POPUP | WS_VISIBLE );
        ::SetWindowPos( m_Window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS );
        m_BackBufferWidth = area.right - area.left;
        m_BackBufferHeight = area.bottom - area.top;
    }


    void Driver::SubclassWindow()
    {
        if (m_Window == nullptr || m_SubclassedWindow == m_Window)
            return;

        RestoreWindowProc();
        PatchFocusImports();
        m_OriginalWndProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrA( m_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc) ));
        m_SubclassedWindow = m_OriginalWndProc != nullptr ? m_Window : nullptr;
    }


    void Driver::PatchFocusImports()
    {
        if (m_FocusPatched)
            return;

        m_FocusPatched = true;

        const HMODULE game = ::GetModuleHandleA( nullptr );
        g_SetForegroundWindow = reinterpret_cast<SetForegroundWindowFn>(
            PatchImport( game, "USER32.dll", "SetForegroundWindow", reinterpret_cast<void *>(&SetForegroundWindowFilter) ));
        g_SetActiveWindow = reinterpret_cast<SetActiveWindowFn>(
            PatchImport( game, "USER32.dll", "SetActiveWindow", reinterpret_cast<void *>(&SetActiveWindowFilter) ));

        Log( "Focus imports patched %d %d", g_SetForegroundWindow != nullptr ? 1 : 0, g_SetActiveWindow != nullptr ? 1 : 0 );
    }


    BOOL WINAPI Driver::SetForegroundWindowFilter(
        HWND window
    )
    {
        Driver & self = GetInstance();

        if (self.m_Inactive && window == self.m_Window)
        {
            self.Trace( "'SetForegroundWindow' from the game ignored" );
            return TRUE;
        }

        return g_SetForegroundWindow != nullptr ? g_SetForegroundWindow( window ) : ::SetForegroundWindow( window );
    }


    HWND WINAPI Driver::SetActiveWindowFilter(
        HWND window
    )
    {
        Driver & self = GetInstance();

        if (self.m_Inactive && window == self.m_Window)
        {
            self.Trace( "'SetActiveWindow' from the game ignored" );
            return nullptr;
        }

        return g_SetActiveWindow != nullptr ? g_SetActiveWindow( window ) : ::SetActiveWindow( window );
    }


    void Driver::RestoreWindowProc()
    {
        if (m_SubclassedWindow != nullptr && m_OriginalWndProc != nullptr && ::IsWindow( m_SubclassedWindow ))
        {
            ::SetWindowLongPtrA( m_SubclassedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_OriginalWndProc) );
        }

        m_SubclassedWindow = nullptr;
        m_OriginalWndProc = nullptr;
    }


    LRESULT CALLBACK Driver::WindowProc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam
    )
    {
        Driver & self = GetInstance();

        const WNDPROC original = self.m_OriginalWndProc;
        const LRESULT result = original != nullptr ? ::CallWindowProcA( original, window, message, wparam, lparam )
            : ::DefWindowProcA( window, message, wparam, lparam );

        if (message == WM_ACTIVATEAPP)
        {
            self.Trace( "WM_ACTIVATEAPP %u iconic %d", static_cast<unsigned>(wparam), ::IsIconic( window ) ? 1 : 0 );
            self.m_Inactive = wparam == FALSE;

            if (wparam != FALSE)
            {
                self.m_ReenterFullscreen = self.m_ConfigWindowMode == WINDOW_FULLSCREEN;
            }

            // The full screen modes behave like the exclusive mode of the original driver on
            // alt tab, the window gets out of the way and comes back when activated again.
            if (self.m_ConfigMinimize && self.m_ConfigWindowMode != WINDOW_WINDOWED)
            {
                if (wparam == FALSE)
                {
                    ::ShowWindow( window, SW_SHOWMINNOACTIVE );
                }
                else if (::IsIconic( window ))
                {
                    ::ShowWindow( window, SW_RESTORE );
                }
            }
        }

        return result;
    }


    void Driver::EnterFullscreen()
    {
        if (!m_SwapChain || m_ConfigWindowMode != WINDOW_FULLSCREEN)
            return;

        BOOL fullscreen = FALSE;
        m_SwapChain->GetFullscreenState( &fullscreen, nullptr );

        if (fullscreen)
            return;

        // The output the window is on, at its current desktop mode.
        ComPtr<IDXGIOutput> output;
        m_SwapChain->GetContainingOutput( output.Put() );
        DXGI_OUTPUT_DESC output_desc = {};

        if (output)
        {
            output->GetDesc( &output_desc );
        }

        const int width = output ? output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left : m_BackBufferWidth;
        const int height = output ? output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top : m_BackBufferHeight;

        DXGI_MODE_DESC mode = {};
        mode.Width = static_cast<UINT>(width);
        mode.Height = static_cast<UINT>(height);
        mode.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

        const HRESULT hr = m_SwapChain->SetFullscreenState( TRUE, output.Get() );

        if (FAILED( hr ))
        {
            // Not possible while another program is active, tried again on a later page flip.
            Trace( "SetFullscreenState failed %08lX", static_cast<unsigned long>(hr) );
            m_ReenterFullscreen = true;
            return;
        }
        m_SwapChain->ResizeTarget( &mode );
        m_BackBufferWidth = width;
        m_BackBufferHeight = height;
        CreateSwapChain();

        Log( "Exclusive fullscreen %dx%d, back buffer view %d", width, height, m_BackBufferView ? 1 : 0 );
    }


    bool Driver::CreateSwapChain()
    {
        if (m_Window == nullptr && m_Callbacks[1] != 0)
        {
            m_Window = reinterpret_cast<HWND( * )()>(m_Callbacks[1])();
        }

        if (m_Window == nullptr)
        {
            Log( "No window for the swap chain" );
            return false;
        }

        if (m_BackBufferWidth <= 0 || m_BackBufferHeight <= 0)
        {
            RECT client = {};
            ::GetClientRect( m_Window, &client );
            m_BackBufferWidth = std::max<LONG>( client.right - client.left, 1 );
            m_BackBufferHeight = std::max<LONG>( client.bottom - client.top, 1 );
        }

        if (m_SwapChain)
        {
            m_BackBufferView.Reset();
            m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
            InvalidatePipeline();
            m_SwapChain->ResizeBuffers( 0, static_cast<UINT>(m_BackBufferWidth), static_cast<UINT>(m_BackBufferHeight), DXGI_FORMAT_UNKNOWN, m_SwapChainFlags );
        }
        else
        {
            ComPtr<IDXGIDevice> dxgi_device;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            m_Device->QueryInterface( __uuidof(IDXGIDevice), reinterpret_cast<void **>(dxgi_device.Put()) );

            if (dxgi_device)
            {
                dxgi_device->GetAdapter( adapter.Put() );
            }

            if (adapter)
            {
                adapter->GetParent( __uuidof(IDXGIFactory2), reinterpret_cast<void **>(factory.Put()) );
            }

            if (!factory)
            {
                Log( "IDXGIFactory2 not available" );
                return false;
            }

            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width = static_cast<UINT>(m_BackBufferWidth);
            desc.Height = static_cast<UINT>(m_BackBufferHeight);
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            // Without vsync the flip model needs tearing support to show frames at once.
            m_AllowTearing = false;

            if (!m_ConfigVsync)
            {
                ComPtr<IDXGIFactory5> factory5;
                factory->QueryInterface( __uuidof(IDXGIFactory5), reinterpret_cast<void **>(factory5.Put()) );
                BOOL tearing = FALSE;

                if (factory5 && SUCCEEDED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof( tearing ) ) ))
                {
                    m_AllowTearing = tearing != FALSE;
                }
            }

            m_SwapChainFlags = m_AllowTearing ? static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0u;
            desc.Flags = m_SwapChainFlags;

            HRESULT hr = factory->CreateSwapChainForHwnd( m_Device.Get(), m_Window, &desc, nullptr, nullptr, m_SwapChain.Put() );
            if (FAILED( hr ))
            {
                // Windows 8.1 and older have no flip discard.
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr = factory->CreateSwapChainForHwnd( m_Device.Get(), m_Window, &desc, nullptr, nullptr, m_SwapChain.Put() );
            }
            if (FAILED( hr ))
            {
                Log( "CreateSwapChainForHwnd failed %08lX", static_cast<unsigned long>(hr) );
                return false;
            }

            factory->MakeWindowAssociation( m_Window, DXGI_MWA_NO_ALT_ENTER );
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        m_SwapChain->GetBuffer( 0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(back_buffer.Put()) );
        m_Device->CreateRenderTargetView( back_buffer.Get(), nullptr, m_BackBufferView.Put() );

        Log( "Swap chain %dx%d", m_BackBufferWidth, m_BackBufferHeight );

        return static_cast<bool>(m_BackBufferView);
    }


    bool Driver::CreateFrame(
        int width,
        int height
    )
    {
        ReleaseFrame();

        m_Width = width;
        m_Height = height;

        ComputeRenderSize();

        // Highest supported sample count of 16, 8, 4 or 2 up to the configured one, '0' (or '1') is off.
        m_Samples = 1;
        const UINT wanted = static_cast<UINT>(std::clamp( m_ConfigSamples, 1, 16 ));
        for (UINT samples = 16; samples > 1; samples >>= 1)
        {
            if (samples > wanted)
                continue;

            UINT color_levels = 0;
            UINT depth_levels = 0;
            m_Device->CheckMultisampleQualityLevels( DXGI_FORMAT_B8G8R8A8_UNORM, samples, &color_levels );
            m_Device->CheckMultisampleQualityLevels( DXGI_FORMAT_D24_UNORM_S8_UINT, samples, &depth_levels );

            if (color_levels > 0 && depth_levels > 0)
            {
                m_Samples = samples;
                break;
            }
        }

        m_Resolve = m_Samples > 1;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(m_RenderWidth);
        desc.Height = static_cast<UINT>(m_RenderHeight);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = m_Samples;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | (m_Resolve ? 0u : static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE));

        if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_Frame.Put() ) ))
        {
            Log( "Frame %dx%d with %u samples failed", m_RenderWidth, m_RenderHeight, m_Samples );
            return false;
        }

        m_Device->CreateRenderTargetView( m_Frame.Get(), nullptr, m_FrameView.Put() );

        if (m_Resolve)
        {
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            m_Device->CreateTexture2D( &desc, nullptr, m_Resolved.Put() );
        }
        else
        {
            m_Resolved = m_Frame;
        }

        m_Device->CreateShaderResourceView( m_Resolved.Get(), nullptr, m_ResolvedView.Put() );

        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        m_Device->CreateTexture2D( &desc, nullptr, m_Front.Put() );
        m_Device->CreateRenderTargetView( m_Front.Get(), nullptr, m_FrontTarget.Put() );
        m_Device->CreateShaderResourceView( m_Front.Get(), nullptr, m_FrontView.Put() );

        // Depth, D24S8 is what every sample count above supports.
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = m_Samples;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        m_Device->CreateTexture2D( &desc, nullptr, m_Depth.Put() );
        m_Device->CreateDepthStencilView( m_Depth.Get(), nullptr, m_DepthView.Put() );

        // Game sized copies for locks and rectangle copies.
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;

        m_Device->CreateTexture2D( &desc, nullptr, m_GameFrame.Put() );
        m_Device->CreateRenderTargetView( m_GameFrame.Get(), nullptr, m_GameFrameView.Put() );

        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        m_Device->CreateTexture2D( &desc, nullptr, m_Upload.Put() );
        m_Device->CreateShaderResourceView( m_Upload.Get(), nullptr, m_UploadView.Put() );

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        m_Device->CreateTexture2D( &desc, nullptr, m_Staging.Put() );

        m_Clip = { 0, 0, width, height };

        const float black[4] = { 0, 0, 0, 1 };
        m_Context->ClearRenderTargetView( m_FrameView.Get(), black );
        m_Context->ClearRenderTargetView( m_FrontTarget.Get(), black );
        m_Context->ClearDepthStencilView( m_DepthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0 );

        Log( "Frame %dx%d rendered at %dx%d with %u samples", width, height, m_RenderWidth, m_RenderHeight, m_Samples );
        return m_FrameView && m_DepthView && m_ResolvedView && m_FrontView && m_GameFrameView && m_UploadView && m_Staging;
    }


    void Driver::ReleaseFrame()
    {
        if (m_Context)
        {
            m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
            ID3D11ShaderResourceView * none[MAX_STAGES] = {};
            m_Context->PSSetShaderResources( 0, MAX_STAGES, none );
        }

        m_Frame.Reset();
        m_FrameView.Reset();
        m_Resolved.Reset();
        m_ResolvedView.Reset();
        m_Front.Reset();
        m_FrontTarget.Reset();
        m_FrontView.Reset();
        m_Depth.Reset();
        m_DepthView.Reset();
        m_GameFrame.Reset();
        m_GameFrameView.Reset();
        m_Upload.Reset();
        m_UploadView.Reset();
        m_Staging.Reset();

        ResetProbes();
        InvalidatePipeline();
    }


    void Driver::ComputeRenderSize()
    {
        int width = m_Width;
        int height = m_Height;

        if (m_ConfigScale > 0)
        {
            width = m_Width * m_ConfigScale;
            height = m_Height * m_ConfigScale;
        }
        else if (m_ConfigScale == 0 && m_BackBufferWidth > 0 && m_BackBufferHeight > 0)
        {
            // The size the frame covers on the display, never below the game resolution.
            const double fit = std::min( static_cast<double>(m_BackBufferWidth) / m_Width, static_cast<double>(m_BackBufferHeight) / m_Height );
            if (fit > 1.0)
            {
                width = static_cast<int>(std::lround( m_Width * fit ));
                height = static_cast<int>(std::lround( m_Height * fit ));
            }
        }

        // D3D11 texture limit.
        const double limit = std::min( 16384.0 / width, 16384.0 / height );
        if (limit < 1.0)
        {
            width = static_cast<int>( width * limit );
            height = static_cast<int>( height * limit );
        }

        m_RenderWidth = std::max( width, m_Width );
        m_RenderHeight = std::max( height, m_Height );
        m_ScaleX = static_cast<float>( m_RenderWidth ) / static_cast<float>( m_Width );
        m_ScaleY = static_cast<float>( m_RenderHeight ) / static_cast<float>( m_Height );
    }


    D3D11_RECT Driver::ScaledClip() const
    {
        D3D11_RECT rect;
        rect.left = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.left) * m_ScaleX ));
        rect.top = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.top) * m_ScaleY ));
        rect.right = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.right) * m_ScaleX ));
        rect.bottom = static_cast<LONG>(std::lround( static_cast<float>(m_Clip.bottom) * m_ScaleY ));
        return rect;
    }


    void Driver::ResolveFrame()
    {
        if (m_Resolve && m_Frame && m_Resolved)
        {
            m_Context->ResolveSubresource( m_Resolved.Get(), 0, m_Frame.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM );
        }
    }


    void Driver::DownsampleFrame(
        bool front
    )
    {
        if (!front)
            ResolveFrame();

        const float destination[4] = { 0, 0, static_cast<float>(m_Width), static_cast<float>(m_Height) };
        const float source[4] = { 0, 0, 1, 1 };

        Blit( front ? m_FrontView.Get() : m_ResolvedView.Get(), m_GameFrameView.Get(), m_Width, m_Height, destination, source, 1.0f,
            m_PresentSampler.Get() );
    }


    void Driver::Blit(
        ID3D11ShaderResourceView * source,
        ID3D11RenderTargetView * target,
        int target_width,
        int target_height,
        const float * destination_rect,
        const float * source_rect,
        float brightness,
        ID3D11SamplerState * sampler
    )
    {
        // The blit binds its own targets, shaders and states.
        InvalidatePipeline();

        PresentConstants constants = {};
        std::memcpy( constants.m_Rect, destination_rect, sizeof( constants.m_Rect ) );
        std::memcpy( constants.m_Source, source_rect, sizeof( constants.m_Source ) );

        constants.m_Params[0] = brightness;
        constants.m_Params[1] = static_cast<float>(target_width);
        constants.m_Params[2] = static_cast<float>(target_height);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED( m_Context->Map( m_PresentConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, &constants, sizeof( constants ) );
            m_Context->Unmap( m_PresentConstants.Get(), 0 );
        }

        ID3D11ShaderResourceView * none[MAX_STAGES] = {};
        m_Context->PSSetShaderResources( 0, MAX_STAGES, none );
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(target_width), static_cast<float>(target_height), 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        m_Context->RSSetState( m_PlainRaster.Get() );

        const float factor[4] = {};
        m_Context->OMSetBlendState( m_OpaqueBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_ClearColorOnly.Get(), 0 );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
        m_Context->VSSetShader( m_PresentVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_PresentPs.Get(), nullptr, 0 );

        ID3D11Buffer * buffer = m_PresentConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &buffer );
        m_Context->PSSetShaderResources( 0, 1, &source );
        m_Context->PSSetSamplers( 0, 1, &sampler );
        m_Context->Draw( 4, 0 );
        m_Context->PSSetShaderResources( 0, MAX_STAGES, none );
    }


    void Driver::BuildModeList()
    {
        m_Modes.clear();
        m_Modes.resize( 6 );

        // Entries 1 to 5 are the fixed slots the game expects
        const int fixed[5][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 } };

        for (int i = 0; i < 5; ++i)
        {
            ModeRecord & mode = m_Modes[i + 1];
            mode.m_Width = fixed[i][0];
            mode.m_Height = fixed[i][1];
            mode.m_Bpp = 16;
            mode.m_Format = formats::RGB565;
            mode.m_Valid = 1;
            mode.m_Buffers = 3;
            mode.m_Capability = 2;
        }

        auto add = [&] ( int width, int height )
        {
            for (int bpp : { 16, 32 })
            {
                const bool exists = std::any_of( m_Modes.begin() + 1, m_Modes.end(), [&] ( const ModeRecord & mode )
                {
                    return mode.m_Width == width && mode.m_Height == height && mode.m_Bpp == bpp;
                } );

                if (!exists)
                {
                    ModeRecord mode = {};
                    mode.m_Width = width;
                    mode.m_Height = height;
                    mode.m_Bpp = bpp;
                    mode.m_Format = bpp == 32 ? formats::ARGB8888 : formats::RGB565;
                    mode.m_Valid = 1;
                    mode.m_Buffers = 3;
                    mode.m_Capability = 2;
                    m_Modes.push_back( mode );
                }
            }
        };

        // Every mode of the primary display, the frame is rendered at any size anyway.
        DEVMODEA dev = {};
        dev.dmSize = sizeof( dev );

        for (DWORD i = 0; ::EnumDisplaySettingsA( nullptr, i, &dev ) != FALSE; ++i)
        {
            if (dev.dmBitsPerPel >= 16 && dev.dmPelsWidth >= 640 && dev.dmPelsHeight >= 480)
            {
                add( static_cast<int>(dev.dmPelsWidth), static_cast<int>(dev.dmPelsHeight) );
            }
        }

        for (const auto & fixed_mode : fixed)
        {
            add( fixed_mode[0], fixed_mode[1] );
        }

        // The display reports its modes in no particular order and the options list shows them
        // as they come, so everything after the fixed slots is sorted by size and depth.
        std::sort( m_Modes.begin() + 6, m_Modes.end(), [] ( const ModeRecord & left, const ModeRecord & right )
        {
            if (left.m_Width != right.m_Width)
                return left.m_Width < right.m_Width;

            if (left.m_Height != right.m_Height)
                return left.m_Height < right.m_Height;

            return left.m_Bpp < right.m_Bpp;
        } );

        Log( "%d display modes", static_cast<int>(m_Modes.size()) - 1 );
    }

    // THRASH entry points.

    AboutBlock * Driver::About()
    {
        if (m_Modes.empty())
            BuildModeList();

        m_FormatFlags.fill( 0 );
        m_FormatFlags[formats::ARGB1555] = 1;
        m_FormatFlags[formats::RGB565] = 1;
        m_FormatFlags[formats::ARGB8888] = 1;
        m_FormatFlags[formats::ARGB4444] = 1;

        AboutBlock & about = m_About;
        about = {};
        about.m_Signature = ABOUT_SIGNATURE;
        about.m_Size = sizeof( AboutBlock );
        about.m_Version = DRIVER_VERSION;

        // Square textures, power of 2 width and height, windowed capable.
        about.m_Flags = 0x01 | 0x02 | 0x04 | 0x08 | 0x20;
        about.m_TextureWidthMin = 1;
        about.m_TextureWidthMax = 4096;
        about.m_TextureWidthMultiple = 1;
        about.m_TextureHeightMin = 1;
        about.m_TextureHeightMax = 4096;
        about.m_TextureHeightMultiple = 1;
        about.m_FormatCount = static_cast<std::int32_t>(m_FormatFlags.size());
        about.m_Formats = m_FormatFlags.data();
        about.m_PaletteFormatCount = static_cast<std::int32_t>(m_PaletteFlags.size());
        about.m_PaletteFormats = m_PaletteFlags.data();
        about.m_ModeCount = static_cast<std::int32_t>(m_Modes.size()) - 1;
        about.m_Modes = m_Modes.data();
        about.m_Stages = MAX_STAGES;
        about.m_Subtype = SUBTYPE;
        about.m_TextureMemory = TEXTURE_MEMORY;
        about.m_Author = AUTHOR;
        about.m_DxVersion = DX_VERSION;
        std::strncpy( about.m_Name, "Direct3D 11", sizeof( about.m_Name ) - 1 );
        return &about;
    }


    int Driver::Init()
    {
        Log( "THRASH_init" );

        LoadConfig();
        MakeDpiAware();

        if (!CreateDevice())
        {
            return 0;
        }

        BuildModeList();
        m_D3D.Reset();
        ResetStateCache();
        m_StateStore.clear();
        return 1;
    }


    void Driver::LoadConfig()
    {
        if (m_ConfigLoaded)
            return;

        m_ConfigLoaded = true;

        char path[MAX_PATH] = {};
        ::GetModuleFileNameA( nullptr, path, MAX_PATH );

        std::string ini = path;
        const std::size_t slash = ini.find_last_of( "\\/" );
        ini = (slash != std::string::npos ? ini.substr( 0, slash + 1 ) : std::string()) + "nfs5_enhanced.ini";

        m_ConfigScale = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "ResolutionScale", 0, ini.c_str() ));
        m_ConfigSamples = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Antialiasing", 4, ini.c_str() ));
        m_ConfigAnisotropy = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Anisotropy", 0, ini.c_str() ));
        m_ConfigWindowMode = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "WindowMode", 0, ini.c_str() )), 0, 2 );
        m_ConfigWindowWidth = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "WindowWidth", 0, ini.c_str() ));
        m_ConfigWindowHeight = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "WindowHeight", 0, ini.c_str() ));
        m_ConfigVsync = ReadIniBool( "VSync", true, ini );
        m_ConfigMinimize = ReadIniBool( "MinimizeOnFocusLoss", true, ini );
        m_TraceLines = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Trace", 0, ini.c_str() ));
        m_ConfigMeasure = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Measure", 0, ini.c_str() )), 0, 2 );
        m_ConfigAlphaEdgeFix = ::GetPrivateProfileIntA( "Dx11", "AlphaEdgeFix", 0, ini.c_str() ) != 0;
        m_ConfigAlphaEdgeRef = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "AlphaEdgeRef", 128, ini.c_str() )), 1, 255 );
        {
            char mip_scale[32] = {};
            ::GetPrivateProfileStringA( "Dx11", "AlphaMipScale", "0", mip_scale, sizeof( mip_scale ), ini.c_str() );
            m_ConfigAlphaMipScale = std::clamp( static_cast<float>(std::atof( mip_scale )), 0.0f, 2.0f );
        }
        m_ConfigForceTwoSided = ::GetPrivateProfileIntA( "Dx11", "ForceTwoSided", 0, ini.c_str() ) != 0;

        Log( "Config ResolutionScale %d Antialiasing %d Anisotropy %d WindowMode %d Window %dx%d", m_ConfigScale, m_ConfigSamples, m_ConfigAnisotropy,
            m_ConfigWindowMode, m_ConfigWindowWidth, m_ConfigWindowHeight );
        Log( "Config VSync %d MinimizeOnFocusLoss %d", m_ConfigVsync ? 1 : 0, m_ConfigMinimize ? 1 : 0 );

        m_ConfigTestNoMips = ::GetPrivateProfileIntA( "Dx11", "TestNoMips", 0, ini.c_str() ) != 0;
        m_ConfigTestVertexAlpha = ::GetPrivateProfileIntA( "Dx11", "TestVertexAlpha", 0, ini.c_str() ) != 0;
        m_ConfigTestNoZBias = ::GetPrivateProfileIntA( "Dx11", "TestNoZBias", 0, ini.c_str() ) != 0;
        m_ConfigTestDraw = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "TestDraw", 0, ini.c_str() ));
        Log( "Config TestNoZBias %d TestDraw %d", m_ConfigTestNoZBias ? 1 : 0, m_ConfigTestDraw );
        Log( "Config AlphaMipScale %.2f TestNoMips %d TestVertexAlpha %d", m_ConfigAlphaMipScale, m_ConfigTestNoMips ? 1 : 0,
            m_ConfigTestVertexAlpha ? 1 : 0 );
        Log( "Config AlphaEdgeFix %d AlphaEdgeRef %d ForceTwoSided %d", m_ConfigAlphaEdgeFix ? 1 : 0, m_ConfigAlphaEdgeRef,
            m_ConfigForceTwoSided ? 1 : 0 );

        if (m_ConfigMeasure > 0)
        {
            Log( "Config Measure %d", m_ConfigMeasure );
            InstallLightHook();
        }

        // The light switches belong to the LFSP mod: '[Lights]' in lfsp.ini next to the game (doc/LICHT.md 8.2a).
        // A key missing there is still read from '[Dx11]' of nfs5_enhanced.ini (installs before 9.11b).
        const std::string lights_ini = (slash != std::string::npos ? std::string( path, slash + 1 ) : std::string()) + "lfsp.ini";
        const auto light_value = [&]( const char * key, const char * fallback ) -> std::string
        {
            char value[MAX_PATH] = {};
            ::GetPrivateProfileStringA( "Lights", key, "\x01", value, sizeof( value ), lights_ini.c_str() );
            if (value[0] == '\x01')
                ::GetPrivateProfileStringA( "Dx11", key, fallback, value, sizeof( value ), ini.c_str() );
            std::string text = value;
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.pop_back();
            return text;
        };

        m_ConfigHeadlights = std::clamp( std::atoi( light_value( "Headlights", "0" ).c_str() ), 0, 2 );
        m_ConfigHeadlightsDay = std::clamp( std::atoi( light_value( "HeadlightsDay", "0" ).c_str() ), 0, 2 );
        m_ConfigDayStrength = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsDayStrength", "1.0" ).c_str() )), 0.0f, 4.0f );
        m_ConfigDayColour = static_cast<std::uint32_t>(std::strtoul( light_value( "HeadlightsDayColour", "808080" ).c_str(), nullptr, 16 )) & 0xFFFFFFu;
        m_ConfigDayRange = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsDayRange", "1.0" ).c_str() )), 0.25f, 4.0f );

        m_ConfigHeadMapFile = light_value( "HeadlightsMap", "" );
        if (!m_ConfigHeadMapFile.empty() && m_ConfigHeadMapFile.find( ':' ) == std::string::npos)
            m_ConfigHeadMapFile = (slash != std::string::npos ? std::string( path, slash + 1 ) : std::string()) + m_ConfigHeadMapFile;

        m_ConfigHighRange = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsHighRange", "1.0" ).c_str() )), 1.0f, 4.0f );
        m_ConfigHighWidth = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsHighWidth", "1.0" ).c_str() )), 0.25f, 2.0f );
        m_ConfigHighStrength = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsHighStrength", "1.2" ).c_str() )), 0.0f, 4.0f );
        // 'HeadlightsHighHeight' is the height reach on walls (1 = as low beam, 2 = twice as high); the shader takes the
        // inverse as factor on the vertical falloff
        m_ConfigHighHeight = 1.0f / std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsHighHeight", "2.5" ).c_str() )), 0.5f, 10.0f );
        Log( "Config high beam: range %.2f width %.2f strength %.2f height %.2f", m_ConfigHighRange, m_ConfigHighWidth, m_ConfigHighStrength,
            m_ConfigHighHeight );

        m_ConfigTailGlow = std::clamp( std::atoi( light_value( "TailGlow", "1" ).c_str() ), 0, 1 );
        m_ConfigTailColour = static_cast<std::uint32_t>(std::strtoul( light_value( "TailGlowColour", "FF1A0A" ).c_str(), nullptr, 16 )) & 0xFFFFFFu;
        m_ConfigTailStrength = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowStrength", "0" ).c_str() )), 0.0f, 4.0f );
        m_ConfigTailBrake = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowBrake", "0.41" ).c_str() )), 0.0f, 4.0f );
        m_ConfigTailSignals = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowSignals", "0.3" ).c_str() )), 0.0f, 4.0f );
        m_ConfigTailLights = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowLights", "0.12" ).c_str() )), 0.0f, 4.0f );
        m_ConfigTailReverse = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowReverse", "0.35" ).c_str() )), 0.0f, 4.0f );
        m_ConfigTailOffset = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowOffset", "2.1" ).c_str() )), 0.0f, 10.0f );
        m_ConfigTailReach = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowReach", "1.5" ).c_str() )), 0.1f, 20.0f );
        m_ConfigTailWidth = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowWidth", "0.6" ).c_str() )), 0.0f, 3.0f );
        m_ConfigTailSpread = std::clamp( static_cast<float>(std::atof( light_value( "TailGlowSpread", "0.5" ).c_str() )), 0.05f, 5.0f );
        Log( "Config tail glow %d colour %06X strength %.2f lights %.2f brake %.2f signals %.2f reverse %.2f, offset %.2f reach %.2f "
            "width %.2f spread %.2f", m_ConfigTailGlow, m_ConfigTailColour, m_ConfigTailStrength, m_ConfigTailLights, m_ConfigTailBrake,
            m_ConfigTailSignals, m_ConfigTailReverse, m_ConfigTailOffset, m_ConfigTailReach, m_ConfigTailWidth, m_ConfigTailSpread );

        m_ConfigHeadlightsObjects = std::clamp( std::atoi( light_value( "HeadlightsObjects", "1" ).c_str() ), 0, 2 );
        Log( "Config HeadlightsObjects %d", m_ConfigHeadlightsObjects );

        // Water reflections, '[Water]' of lfsp.ini (doc/BUGS.md B15 §12).
        {
            const auto water_value = [&]( const char * key, const char * fallback ) -> std::string
            {
                char value[1024] = {};
                ::GetPrivateProfileStringA( "Water", key, fallback, value, sizeof( value ), lights_ini.c_str() );
                return value;
            };

            m_ConfigReflections = std::clamp( std::atoi( water_value( "Reflections", "1" ).c_str() ), 0, 1 );
            m_ConfigReflectionStrength = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionStrength", "0.6" ).c_str() )), 0.0f, 1.0f );
            m_ConfigReflectionTint = static_cast<std::uint32_t>(std::strtoul( water_value( "ReflectionTint", "FFFFFF" ).c_str(), nullptr, 16 )) & 0xFFFFFFu;
            m_ConfigGuardBand = ::GetPrivateProfileIntA( "World", "GuardBand", 0, lights_ini.c_str() ) != 0;
            {
                char bias[32] = {};
                ::GetPrivateProfileStringA( "World", "SkidDepthBias", "0.003", bias, sizeof( bias ), lights_ini.c_str() );
                m_ConfigSkidBias = std::clamp( static_cast<float>(std::atof( bias )), 0.0f, 0.1f );
                ::GetPrivateProfileStringA( "World", "TextureLodBias", "0", bias, sizeof( bias ), lights_ini.c_str() );
                m_ConfigTextureLodBias = std::clamp( static_cast<float>(std::atof( bias )), -2.0f, 4.0f );
            }
            m_ConfigForceMips = ::GetPrivateProfileIntA( "World", "ForceMips", 0, lights_ini.c_str() ) != 0;
            m_ConfigReflectionDebug = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Water", "ReflectionDebug", 0, lights_ini.c_str() )), 0, 4 );
            m_ConfigReflectionTwoSided = std::clamp( std::atoi( water_value( "ReflectionTwoSided", "1" ).c_str() ), 0, 1 );
            m_ConfigReflectionWaves = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionWaves", "3" ).c_str() )), 0.0f, 20.0f );
            m_ConfigReflectionBlur = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionBlur", "2" ).c_str() )), 0.0f, 20.0f );
            m_ConfigReflectionFresnel = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionFresnel", "0.3" ).c_str() )), 0.0f, 1.0f );
            m_ConfigShoreFade = std::clamp( static_cast<float>(std::atof( water_value( "ShoreFade", "0.27" ).c_str() )), 0.0f, 0.9f );
            m_ConfigWaterFarFade = std::clamp( static_cast<float>(std::atof( water_value( "FarFade", "0.3" ).c_str() )), 0.0f, 0.9f );
            m_ConfigWaterEvenColour = std::clamp( std::atoi( water_value( "WaterEvenColour", "1" ).c_str() ), 0, 1 );
            m_ConfigReflectionScale = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionScale", "0.5" ).c_str() )), 0.25f, 1.0f );
            m_ConfigReflectionGrazing = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionGrazing", "1" ).c_str() )), 0.0f, 1.0f );
            m_ConfigWaterEdgeFade = std::clamp( static_cast<float>(std::atof( water_value( "EdgeFade", "40" ).c_str() )), 0.0f, 200.0f );
            m_ConfigReflectionSky = std::clamp( std::atoi( water_value( "ReflectionSky", "1" ).c_str() ), 0, 1 );
            m_ConfigRemoveBaked = std::clamp( std::atoi( water_value( "RemoveBakedReflections", "1" ).c_str() ), 0, 1 );

            const auto parse_hashes = [&]( const char * key, const char * fallback, std::vector<std::uint32_t> & out ) {
                out.clear();
                const std::string list = water_value( key, fallback );
                for (std::size_t at = 0; at < list.size();)
                {
                    const std::size_t end = std::min( list.find_first_of( ", ;", at ), list.size() );
                    if (end > at)
                        out.push_back( static_cast<std::uint32_t>(std::strtoul( list.substr( at, end - at ).c_str(), nullptr, 16 )) );
                    at = end + 1;
                }
                return list;
            };
            const std::string list = parse_hashes( "WaterTextures", "28A6F8AA", m_ConfigWaterTextures );
            const std::string shore = parse_hashes( "ShoreTextures", "63B3CF44", m_ConfigShoreTextures );
            Log( "Config water: ShoreTextures %s", shore.c_str() );
            // Only these textures are the game's baked reflections; other blended draws of list 1 (trees on rocks) stay.
            const std::string baked = parse_hashes( "BakedTextures", "41C66523,92CDBBAB", m_ConfigBakedTextures );
            const std::string skip = parse_hashes( "ReflectionSkip", "", m_ConfigReflectionSkip );
            const std::string far_water = parse_hashes( "FarWaterTextures", "28884AC8", m_ConfigFarWaterTextures );
            Log( "Config water: EdgeFade %.0f, ReflectionSkip %s, FarWaterTextures %s", m_ConfigWaterEdgeFade, skip.c_str(), far_water.c_str() );

            Log( "Config GuardBand %d", m_ConfigGuardBand ? 1 : 0 );
            Log( "Config water: Reflections %d strength %.2f tint %06X RemoveBakedReflections %d (%s), %zu water textures (%s)", m_ConfigReflections,
                m_ConfigReflectionStrength, m_ConfigReflectionTint, m_ConfigRemoveBaked, baked.c_str(), m_ConfigWaterTextures.size(), list.c_str() );
        }

        m_ConfigOpponents = std::clamp( std::atoi( light_value( "HeadlightsOpponents", "1" ).c_str() ), 0, 1 );
        m_ConfigOpponentsStrength = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsOpponentsStrength", "1.0" ).c_str() )), 0.0f, 4.0f );
        Log( "Config lights from '%s' [Lights] (missing keys from [Dx11]): HeadlightsOpponents %d strength %.2f", lights_ini.c_str(),
            m_ConfigOpponents, m_ConfigOpponentsStrength );

        // Distance probe key: one character, a hex virtual key ("0x2D") or INSERT / HOME / END / PAUSE; empty = off.
        {
            char key[32] = {};
            ::GetPrivateProfileStringA( "World", "DistanceKey", "INSERT", key, sizeof( key ), lights_ini.c_str() );
            std::string name = key;
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
                name.pop_back();
            for (char & c : name)
                c = static_cast<char>(std::toupper( static_cast<unsigned char>(c) ));

            if (name.size() == 1)
                m_ProbeKey = static_cast<unsigned char>(name[0]);
            else if (name.rfind( "0X", 0 ) == 0)
                m_ProbeKey = static_cast<int>(std::strtol( name.c_str() + 2, nullptr, 16 ));
            else if (name == "INSERT" || name == "EINFG")
                m_ProbeKey = VK_INSERT;
            else if (name == "HOME" || name == "POS1")
                m_ProbeKey = VK_HOME;
            else if (name == "END" || name == "ENDE")
                m_ProbeKey = VK_END;
            else if (name == "PAUSE")
                m_ProbeKey = VK_PAUSE;

            Log( "Config DistanceKey '%s' (0x%02X)", name.c_str(), m_ProbeKey );
        }

        Log( "Config Headlights %d HeadlightsDay %d, day strength %.2f colour %06X range %.2f, map '%s'", m_ConfigHeadlights,
            m_ConfigHeadlightsDay, m_ConfigDayStrength, m_ConfigDayColour, m_ConfigDayRange,
            m_ConfigHeadMapFile.empty() ? "game" : m_ConfigHeadMapFile.c_str() );

        if (m_ConfigHeadlights > 0 || m_ConfigMeasure > 0)
        {
            InstallHeadlightHooks();
        }

        Log( "Config SkidDepthBias %.4f", m_ConfigSkidBias );
        Log( "Config TextureLodBias %.2f ForceMips %d", m_ConfigTextureLodBias, m_ConfigForceMips ? 1 : 0 );
        if (m_ConfigSkidBias > 0.0f)
        {
            InstallSkidHook();
        }
    }

    void Driver::MakeDpiAware()
    {
        // The frame is presented at physical pixels, a DPI virtualized window would be
        // scaled up again by the compositor.
        using SetContextFn = BOOL( WINAPI * )(HANDLE);
        using SetThreadContextFn = HANDLE( WINAPI * )(HANDLE);
        const HMODULE user32 = ::GetModuleHandleA( "user32.dll" );
        const auto set_process = reinterpret_cast<SetContextFn>(reinterpret_cast<void *>(::GetProcAddress( user32, "SetProcessDpiAwarenessContext" )));
        const auto set_thread = reinterpret_cast<SetThreadContextFn>(reinterpret_cast<void *>(::GetProcAddress( user32, "SetThreadDpiAwarenessContext" )));
        const HANDLE per_monitor_v2 = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-4));

        if (set_process != nullptr)
        {
            set_process( per_monitor_v2 );
        }

        if (set_thread != nullptr)
        {
            set_thread( per_monitor_v2 );
        }
    }


    int Driver::Is()
    {
        return 0x55;
    }


    int Driver::SelectDisplay(
        int
    )
    {
        return 1;
    }


    int Driver::SetVideoMode(
        int mode,
        int,
        int depth
    )
    {
        if (!CreateDevice())
        {
            return 0;
        }

        if (m_Modes.empty())
        {
            BuildModeList();
        }

        if (mode < 1 || mode >= static_cast<int>( m_Modes.size() ))
        {
            Log( "setvideomode: mode %d out of range", mode );
            return 0;
        }

        const ModeRecord & record = m_Modes[static_cast<std::size_t>( mode )];
        Log( "setvideomode %d: %dx%d depth %d", mode, record.m_Width, record.m_Height, depth );

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        m_Width = record.m_Width;
        m_Height = record.m_Height;

        // Color depth as dx7z picks it, '1' means 16 bit, '2' means 32 bit, capped by the mode.
        int bits = depth == 1 ? 16 : (depth == 2 ? 32 : depth);
        if (record.m_Bpp < bits)
        {
            bits = 16;
        }

        m_FrameBits = bits >= 24 ? 32 : 16;
        FitWindow();

        if (!CreateSwapChain())
        {
            return 0;
        }

        EnterFullscreen();

        if (!CreateFrame( record.m_Width, record.m_Height ))
        {
            return 0;
        }

        // Defaults as dx7z applies them in 6000A630.
        m_D3D.Reset();
        ResetStateCache();
        SetState( 0x130, 0 );
        SetState( 1, 0 );
        SetState( 2, 1 );
        SetState( 7, 1 );
        SetState( 6, 1 );
        SetState( 10, 2 );
        SetState( 0x24, 0x10 );
        SetState( 0x0B, 1 );
        SetState( 3, 0 );
        SetState( 0x0C, 0 );
        SetState( 5, 1 );
        SetState( 0x14, 0 );
        SetState( 0x15, 0 );
        SetState( 0x0E, 0 );
        SetState( 0x16, 0 );
        SetState( 0x17, 0x3F800000 );
        SetState( 0x0F, 0xFFFFFFFF );
        SetState( 0x38, 0 );
        SetState( 0x0D, 0 );
        SetState( 0x2E, 0x3F800000 );
        SetState( 0x18, 0 );
        SetState( 8, 0 );
        SetState( 0x2A, 0 );
        SetState( 0x29, 0 );
        SetState( 0x10029, 0 );
        SetState( 0x2F, 0 );
        SetState( 0x3B, 0 );
        SetState( 0x43, 0x3F800000 );
        SetState( 0x3C, 1 );
        SetState( 4, depth >= 1 ? 2 : 0 );
        SetState( 0x28, 3 );

        // State '3E' counts mode changes.
        StoreState( 0x3E, static_cast<std::uint32_t>(GetState( 0x3E ) + 1) );

        // State '1D' points to the frame description taken from a lock of the back buffer, the
        // game reads the pixel format from it right after the mode change.
        m_FrameInfo = {};
        Window( 1 );

        if (const LockInfo * lock = LockWindow())
        {
            m_FrameInfo = *lock;
            UnlockWindow( nullptr );
        }

        Window( 0 );

        if (m_FrameInfo.m_Pitch == 0)
        {
            m_FrameInfo.m_Pitch = m_Width * (m_FrameBits / 8);
            m_FrameInfo.m_Format = m_FrameBits == 16 ? formats::RGB565 : formats::ARGB8888;
            m_FrameInfo.m_Width = m_Width;
            m_FrameInfo.m_Height = m_Height;
        }

        m_FrameInfo.m_Bits = nullptr;
        StoreState( 0x1D, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&m_FrameInfo)) );

        TextureReset();
        return 1;
    }


    int Driver::Restore()
    {
        Log( "THRASH_restore" );
        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        TextureReset();

        // The game unloads the driver next, everything is released here and not at unload.
        ReleaseAll();
        return 1;
    }


    void Driver::ReleaseAll()
    {
        if (m_Context)
        {
            m_Context->ClearState();
            m_Context->Flush();
        }

        ReleaseFrame();
        m_BackBufferView.Reset();

        if (m_SwapChain)
        {
            m_SwapChain->SetFullscreenState( FALSE, nullptr );
        }

        m_SwapChain.Reset();
        m_BackBufferWidth = 0;
        m_BackBufferHeight = 0;
        RestoreWindowProc();

        m_Samplers.clear();
        m_Blends.clear();
        m_DepthStates.clear();
        m_Rasters.clear();
        m_SceneVs.Reset();
        m_ScenePs.Reset();
        m_SceneLayout.Reset();
        m_PresentVs.Reset();
        m_PresentPs.Reset();
        m_ClearVs.Reset();
        m_ClearPs.Reset();
        m_MirrorVs.Reset();
        m_ReflCompositeVs.Reset();
        m_ReflCompositePs.Reset();
        m_MirrorConstants.Reset();
        m_ReflBlends.clear();
        m_ReflNoColour.Reset();
        m_ReflShoreDebug.Reset();
        m_ReflSky.Reset();
        m_ReflSkyView.Reset();
        m_ReflWeightRtv.Reset();
        m_ReflWeightSrv.Reset();
        m_ReflWeight.Reset();
        m_ReflWeightBlend.Reset();
        m_ReflDepthStates.clear();
        m_ReflRasters.clear();
        m_ReflTargetSrv.Reset();
        m_ReflTargetView.Reset();
        m_ReflTarget.Reset();
        m_ReflDepthView.Reset();
        m_ReflDepth.Reset();
        m_ReflWidth = 0;
        m_ReflHeight = 0;
        m_ReflBatches.clear();
        m_ReflUsed = 0;
        m_ReflWater.clear();
        m_ReflWaterHash.clear();
        m_DrawConstants.Reset();
        m_PresentConstants.Reset();
        m_ClearConstants.Reset();
        m_VertexBuffer.Reset();
        m_IndexBuffer.Reset();
        m_PresentSampler.Reset();
        m_PointSampler.Reset();
        m_OpaqueBlend.Reset();
        m_PlainRaster.Reset();
        m_ClearDepth.Reset();
        m_ClearColorOnly.Reset();
        m_ClearColorBlend.Reset();
        m_ClearDepthOnlyBlend.Reset();
        InvalidatePipeline();
        m_Context.Reset();
        m_Device.Reset();

        Log( "Device released" );
    }


    int Driver::Window(
        int window
    )
    {
        StoreState( 0x3B, static_cast<std::uint32_t>(window) );

        if (window != m_SelectedWindow)
        {
            Trace( "window %d", window );
        }

        m_SelectedWindow = window;
        return window != 0 && m_Frame;
    }


    void Driver::ClearWindow()
    {
        if (!m_FrameView)
            return;

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        const bool depth_only = m_SelectedWindow == 3 || m_SelectedWindow == 5;
        const bool full = m_Clip.left <= 0 && m_Clip.top <= 0 && m_Clip.right >= m_Width && m_Clip.bottom >= m_Height;

        float color[4];
        ColorToFloat4( m_BackgroundColor, color );

        if (full)
        {
            if (!depth_only)
            {
                m_Context->ClearRenderTargetView( m_FrameView.Get(), color );
            }

            m_Context->ClearDepthStencilView( m_DepthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, m_ClearDepthValue, 0 );
            return;
        }

        ClearConstants constants = {};
        const D3D11_RECT clip = ScaledClip();
        constants.m_Rect[0] = static_cast<float>(clip.left);
        constants.m_Rect[1] = static_cast<float>(clip.top);
        constants.m_Rect[2] = static_cast<float>(clip.right - clip.left);
        constants.m_Rect[3] = static_cast<float>(clip.bottom - clip.top);
        constants.m_Target[0] = static_cast<float>(m_RenderWidth);
        constants.m_Target[1] = static_cast<float>(m_RenderHeight);
        constants.m_Target[2] = m_ClearDepthValue;
        std::memcpy( constants.m_Color, color, sizeof( color ) );

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED( m_Context->Map( m_ClearConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, &constants, sizeof( constants ) );
            m_Context->Unmap( m_ClearConstants.Get(), 0 );
        }

        ID3D11RenderTargetView * target = m_FrameView.Get();
        m_Context->OMSetRenderTargets( 1, &target, m_DepthView.Get() );

        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        m_Context->RSSetState( m_PlainRaster.Get() );

        InvalidatePipeline();

        const float factor[4] = {};
        m_Context->OMSetBlendState( depth_only ? m_ClearDepthOnlyBlend.Get() : m_ClearColorBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_ClearDepth.Get(), 0 );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
        m_Context->VSSetShader( m_ClearVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_ClearPs.Get(), nullptr, 0 );
        ID3D11Buffer * buffer = m_ClearConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &buffer );
        m_Context->Draw( 4, 0 );
    }


    int Driver::Clip(
        int x0,
        int y0,
        int x1,
        int y1
    )
    {
        Trace( "clip %d %d %d %d", x0, y0, x1, y1 );
        Flush();

        m_Clip.left = std::clamp( x0, 0, std::max( m_Width, 0 ) );
        m_Clip.top = std::clamp( y0, 0, std::max( m_Height, 0 ) );
        m_Clip.right = std::clamp( x1, 0, std::max( m_Width, 0 ) );
        m_Clip.bottom = std::clamp( y1, 0, std::max( m_Height, 0 ) );
        return 1;
    }


    int Driver::FlushWindow()
    {
        Flush();
        return 1;
    }


    void Driver::PageFlip()
    {
        ++m_FlipCount;
        m_HeadMesh = false;
        m_HeadFrameValid = false;
        m_DayMesh = false;
        Flush();
        if (m_QueueList > 0)
        {
            DrawReflections();
            m_QueueList = 0;
        }
        m_ReflUsed = 0;
        m_ReflSkyCount = 0;
        m_ReflSkyPhase = true;
        m_ReflSkyRun = false;
        LogLampTable();
        ProbeFlip();

        if (m_ConfigMeasure > 0 && ::GetTickCount64() >= m_MeasureNextReport)
        {
            if (m_MeasureNextReport != 0)
            {
                MeasureReport( "periodic" );
            }

            m_MeasureNextReport = ::GetTickCount64() + 10000;
        }

        // Scroll Lock records the next frame, reported at its flip. Not F11: the game maps F1-F12 to its own
        // keys, F11 steps the scenery detail (doc/LICHT.md 6.13).
        if (m_ConfigMeasure > 0)
        {
            CheckWatch();

            if (m_MeasureSnapActive)
            {
                MeasureSnapReport();
                m_MeasureSnapActive = false;
                m_WatchDoneThisSnap = false;
                m_HeadlightLogged = false;
                m_HeadValidated = false;
            }

            const bool key = (::GetAsyncKeyState( VK_SCROLL ) & 0x8000) != 0;
            m_MeasureSnapActive = key && !m_MeasureSnapKey;
            m_MeasureSnapKey = key;
        }

        if (m_Locked)
            UnlockWindow( nullptr );

        if (!m_SwapChain || !m_ResolvedView)
            return;

        // DXGI leaves exclusive mode when the window loses focus, return once it is back.
        if (m_ReenterFullscreen && !m_Inactive && !::IsIconic( m_Window ))
        {
            m_ReenterFullscreen = false;
            EnterFullscreen();
        }

        ResolveFrame();
        m_Context->CopyResource( m_Front.Get(), m_Resolved.Get() );
        PresentFront();

        // Follow the window when something else resized it, the next frame renders at the new size.
        RECT client = {};
        ::GetClientRect( m_Window, &client );

        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;

        if (client_width > 0 && client_height > 0 && (client_width != m_BackBufferWidth || client_height != m_BackBufferHeight))
        {
            m_BackBufferWidth = client_width;
            m_BackBufferHeight = client_height;

            if (CreateSwapChain())
            {
                const int render_width = m_RenderWidth;
                const int render_height = m_RenderHeight;

                ComputeRenderSize();

                if (render_width != m_RenderWidth || render_height != m_RenderHeight)
                {
                    const D3D11_RECT clip = m_Clip;
                    CreateFrame( m_Width, m_Height );
                    m_Clip = clip;
                }
            }
        }
    }


    void Driver::ProbeBatch()
    {
        const auto & render = m_D3D.m_Render;

        if (m_BatchPrimitive != Primitive::TRIANGLES || render[rs::ZENABLE] == 0 || m_ProbeX < 0.0f)
            return;

        const float px = m_ProbeX;
        const float py = m_ProbeY;

        for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
        {
            const TlVertex & a = m_BatchVertices[m_BatchIndices[i]];
            const TlVertex & b = m_BatchVertices[m_BatchIndices[i + 1]];
            const TlVertex & c = m_BatchVertices[m_BatchIndices[i + 2]];

            if ((px < a.m_X && px < b.m_X && px < c.m_X) || (px > a.m_X && px > b.m_X && px > c.m_X) ||
                (py < a.m_Y && py < b.m_Y && py < c.m_Y) || (py > a.m_Y && py > b.m_Y && py > c.m_Y))
                continue;

            const float d = (b.m_Y - c.m_Y) * (a.m_X - c.m_X) + (c.m_X - b.m_X) * (a.m_Y - c.m_Y);
            if (std::fabs( d ) < 1e-6f)
                continue;

            const float l1 = ((b.m_Y - c.m_Y) * (px - c.m_X) + (c.m_X - b.m_X) * (py - c.m_Y)) / d;
            const float l2 = ((c.m_Y - a.m_Y) * (px - c.m_X) + (a.m_X - c.m_X) * (py - c.m_Y)) / d;
            const float l3 = 1.0f - l1 - l2;
            if (l1 < 0.0f || l2 < 0.0f || l3 < 0.0f)
                continue;

            // rhw is linear in screen space, the largest is the nearest surface.
            const float rhw = l1 * a.m_Rhw + l2 * b.m_Rhw + l3 * c.m_Rhw;
            if (rhw > m_ProbeBest)
                m_ProbeBest = rhw;
        }
    }


    void Driver::ProbeFlip()
    {
        if (m_ProbeKey != 0)
        {
            const bool down = ::GetForegroundWindow() == m_Window && (::GetAsyncKeyState( m_ProbeKey ) & 0x8000) != 0;
            if (down && !m_ProbeKeyDown)
            {
                m_ProbeOn = !m_ProbeOn;
                Log( "Distance probe %s", m_ProbeOn ? "on" : "off" );
            }
            m_ProbeKeyDown = down;
        }

        m_ProbeShown = m_ProbeBest;
        m_ProbeBest = 0.0f;
        m_ProbeX = -1.0f;
        m_ProbeY = -1.0f;

        if (!m_ProbeOn || m_RenderWidth <= 0 || m_RenderHeight <= 0)
            return;

        POINT cursor = {};
        if (!::GetCursorPos( &cursor ) || !::ScreenToClient( m_Window, &cursor ))
            return;

        // Same fit as PresentFront: the frame sits centred in the back buffer.
        const float scale = std::min( static_cast<float>(m_BackBufferWidth) / static_cast<float>(m_RenderWidth),
            static_cast<float>(m_BackBufferHeight) / static_cast<float>(m_RenderHeight) );
        const float width = std::round( static_cast<float>(m_RenderWidth) * scale );
        const float height = std::round( static_cast<float>(m_RenderHeight) * scale );
        const float left = std::floor( (static_cast<float>(m_BackBufferWidth) - width) * 0.5f );
        const float top = std::floor( (static_cast<float>(m_BackBufferHeight) - height) * 0.5f );

        m_ProbeCursorX = cursor.x;
        m_ProbeCursorY = cursor.y;
        m_ProbeX = (static_cast<float>(cursor.x) + 0.5f - left) / width * static_cast<float>(m_Width);
        m_ProbeY = (static_cast<float>(cursor.y) + 0.5f - top) / height * static_cast<float>(m_Height);
    }


    void Driver::DrawProbe()
    {
        ID3D11DeviceContext1 * context = nullptr;
        if (FAILED( m_Context->QueryInterface( __uuidof(ID3D11DeviceContext1), reinterpret_cast<void **>(&context) ) ) || !context)
            return;

        std::vector<D3D11_RECT> dark;
        std::vector<D3D11_RECT> light;
        const auto add = [&]( std::vector<D3D11_RECT> & list, int l, int t, int r, int b )
        {
            l = std::max( l, 0 );
            t = std::max( t, 0 );
            r = std::min( r, static_cast<int>(m_BackBufferWidth) );
            b = std::min( b, static_cast<int>(m_BackBufferHeight) );
            if (r > l && b > t)
                list.push_back( { l, t, r, b } );
        };

        const int unit = std::max( 2, static_cast<int>(m_BackBufferHeight) / 360 );
        const int x = m_ProbeCursorX;
        const int y = m_ProbeCursorY;

        // Cross, yellow with a dark rim.
        const int gap = 2 * unit;
        const int arm = 7 * unit;
        const int half = std::max( 1, unit / 2 );
        add( dark, x - arm - 1, y - half - 1, x - gap + 1, y + half + 1 );
        add( dark, x + gap - 1, y - half - 1, x + arm + 1, y + half + 1 );
        add( dark, x - half - 1, y - arm - 1, x + half + 1, y - gap + 1 );
        add( dark, x - half - 1, y + gap - 1, x + half + 1, y + arm + 1 );
        add( light, x - arm, y - half, x - gap, y + half );
        add( light, x + gap, y - half, x + arm, y + half );
        add( light, x - half, y - arm, x + half, y - gap );
        add( light, x - half, y + gap, x + half, y + arm );

        // Distance as seven segment digits beside the cross.
        char text[16] = "---";
        if (m_ProbeShown > 0.0f)
        {
            const float metres = 1.0f / m_ProbeShown;
            std::snprintf( text, sizeof( text ), metres < 100.0f ? "%.1f" : "%.0f", metres );
        }

        static const std::uint8_t segments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
        const int t = unit;
        const int len = 4 * unit;
        const int digit_width = len + 2 * t;
        const int digit_height = 2 * len + 3 * t;
        const int step = digit_width + 2 * unit;
        const int text_x = x + arm + 2 * unit;
        const int text_y = y + gap;

        int width = 0;
        for (const char * c = text; *c; ++c)
            width += *c == '.' ? 2 * t + unit : step;
        add( dark, text_x - unit, text_y - unit, text_x + width + unit, text_y + digit_height + unit );

        int cx = text_x;
        for (const char * c = text; *c; ++c)
        {
            if (*c == '.')
            {
                add( light, cx, text_y + digit_height - t, cx + t, text_y + digit_height );
                cx += 2 * t + unit;
                continue;
            }

            const std::uint8_t mask = *c == '-' ? 0x40 : (*c >= '0' && *c <= '9' ? segments[*c - '0'] : 0);
            const int l = cx;
            const int top = text_y;
            if (mask & 0x01) add( light, l + t, top, l + t + len, top + t );
            if (mask & 0x02) add( light, l + t + len, top + t, l + 2 * t + len, top + t + len );
            if (mask & 0x04) add( light, l + t + len, top + 2 * t + len, l + 2 * t + len, top + 2 * t + 2 * len );
            if (mask & 0x08) add( light, l + t, top + 2 * t + 2 * len, l + t + len, top + 3 * t + 2 * len );
            if (mask & 0x10) add( light, l, top + 2 * t + len, l + t, top + 2 * t + 2 * len );
            if (mask & 0x20) add( light, l, top + t, l + t, top + t + len );
            if (mask & 0x40) add( light, l + t, top + t + len, l + t + len, top + 2 * t + len );
            cx += step;
        }

        const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const float yellow[4] = { 1.0f, 0.9f, 0.2f, 1.0f };
        if (!dark.empty())
            context->ClearView( m_BackBufferView.Get(), black, dark.data(), static_cast<UINT>(dark.size()) );
        if (!light.empty())
            context->ClearView( m_BackBufferView.Get(), yellow, light.data(), static_cast<UINT>(light.size()) );
        context->Release();
    }


    void Driver::PresentFront()
    {
        if (!m_SwapChain || !m_FrontView)
            return;

        // Fit the frame into the back buffer with its own aspect ratio, a frame rendered at
        // the fitted size is copied one to one.
        const float scale = std::min( static_cast<float>(m_BackBufferWidth) / static_cast<float>(m_RenderWidth),
            static_cast<float>(m_BackBufferHeight) / static_cast<float>(m_RenderHeight) );
        const float width = std::round( static_cast<float>(m_RenderWidth) * scale );
        const float height = std::round( static_cast<float>(m_RenderHeight) * scale );
        const float destination[4] = { std::floor( (static_cast<float>(m_BackBufferWidth) - width) * 0.5f ),
            std::floor( (static_cast<float>(m_BackBufferHeight) - height) * 0.5f ), width, height };
        const float source[4] = { 0, 0, 1, 1 };

        ID3D11RenderTargetView * target = m_BackBufferView.Get();

        const float black[4] = { 0, 0, 0, 1 };
        m_Context->ClearRenderTargetView( target, black );
        Blit( m_FrontView.Get(), target, m_BackBufferWidth, m_BackBufferHeight, destination, source, m_Brightness, m_PresentSampler.Get() );

        if (m_ProbeOn)
        {
            DrawProbe();
        }

        // Nothing to show while minimized, a short wait keeps the paused game from spinning.
        if (::IsIconic( m_Window ))
        {
            Trace( "present skipped, minimized" );
            ::Sleep( 16 );
            return;
        }

        BOOL fullscreen = FALSE;
        m_SwapChain->GetFullscreenState( &fullscreen, nullptr );
        const UINT flags = !m_ConfigVsync && m_AllowTearing && !fullscreen ? static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING) : 0u;
        m_SwapChain->Present( m_ConfigVsync ? 1 : 0, flags );
    }


    int Driver::Sync(
        int mode
    )
    {
        Flush();

        if (mode == 0 && m_Context)
        {
            m_Context->Flush();
        }

        return 0;
    }


    LockInfo * Driver::LockWindow()
    {
        if (!m_Frame || !m_Staging || m_SelectedWindow == 0 || m_SelectedWindow == 3 || m_SelectedWindow == 5)
        {
            Trace( "lockwindow refused, window %d", m_SelectedWindow );
            return nullptr;
        }

        Flush();

        if (m_Locked)
        {
            Trace( "lockwindow while locked" );
            return &m_Lock;
        }

        Trace( "lockwindow window %d bits %d", m_SelectedWindow, m_FrameBits );

        if (IsDirect())
        {
            m_Context->CopyResource( m_Staging.Get(), m_Frame.Get() );
        }
        else
        {
            DownsampleFrame( IsFrontSelected() );
            m_Context->CopyResource( m_Staging.Get(), m_GameFrame.Get() );
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_Staging.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped ) ))
        {
            return nullptr;
        }

        m_LockMapped = mapped.pData;
        m_LockMappedPitch = mapped.RowPitch;

        if (m_FrameBits == 16)
        {
            const std::uint32_t pitch = static_cast<std::uint32_t>(m_Width) * 2;
            m_LockPixels.resize( static_cast<std::size_t>(pitch) * static_cast<std::size_t>(m_Height) );
            ToRgb565( static_cast<const std::uint8_t *>(mapped.pData), mapped.RowPitch, m_LockPixels.data(), pitch, m_Width, m_Height );
            m_Lock.m_Bits = m_LockPixels.data();
            m_Lock.m_Pitch = static_cast<std::int32_t>(pitch);
            m_Lock.m_Format = formats::RGB565;
        }
        else
        {
            m_Lock.m_Bits = mapped.pData;
            m_Lock.m_Pitch = static_cast<std::int32_t>(mapped.RowPitch);
            m_Lock.m_Format = formats::ARGB8888;
        }

        m_Lock.m_Width = m_Width;
        m_Lock.m_Height = m_Height;
        m_Locked = true;

        // Kept to find out on unlock whether the game wrote anything.
        const auto * bits = static_cast<const std::uint8_t *>(m_Lock.m_Bits);
        m_LockSnapshot.assign( bits, bits + static_cast<std::size_t>(m_Lock.m_Pitch) * static_cast<std::size_t>(m_Height) );
        return &m_Lock;
    }


    int Driver::UnlockWindow(
        LockInfo *
    )
    {
        if (!m_Locked)
        {
            return 1;
        }

        m_Locked = false;

        const std::size_t size = static_cast<std::size_t>(m_Lock.m_Pitch) * static_cast<std::size_t>(m_Height);
        const bool written = m_LockSnapshot.size() != size || std::memcmp( m_LockSnapshot.data(), m_Lock.m_Bits, size ) != 0;
        Trace( "unlockwindow written %d", written ? 1 : 0 );

        if (written && m_FrameBits == 16)
        {
            FromRgb565( m_LockPixels.data(), static_cast<std::uint32_t>(m_Lock.m_Pitch), static_cast<std::uint8_t *>(m_LockMapped), m_LockMappedPitch,
                m_Width, m_Height );
        }

        m_Context->Unmap( m_Staging.Get(), 0 );
        m_LockMapped = nullptr;

        if (!written)
        {
            return 1;
        }

        if (IsDirect())
        {
            m_Context->CopyResource( m_Frame.Get(), m_Staging.Get() );
        }
        else
        {
            // Scale the game image back over the frame or the front buffer.
            m_Context->CopyResource( m_Upload.Get(), m_Staging.Get() );
            const float destination[4] = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight) };
            const float source[4] = { 0, 0, 1, 1 };
            const bool front = IsFrontSelected();
            Blit( m_UploadView.Get(), front ? m_FrontTarget.Get() : m_FrameView.Get(), m_RenderWidth, m_RenderHeight, destination, source, 1.0f,
                m_PresentSampler.Get() );

            if (front)
            {
                PresentFront();
            }
        }

        return 1;
    }


    int Driver::ReadRect(
        int x,
        int y,
        int width,
        int height,
        void * out
    )
    {
        Trace( "readrect %d %d %d %d", x, y, width, height );

        if (!m_Frame || out == nullptr || width <= 0 || height <= 0)
        {
            return 0;
        }

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        const int x0 = std::clamp( x, 0, m_Width );
        const int y0 = std::clamp( y, 0, m_Height );
        const int x1 = std::clamp( x + width, 0, m_Width );
        const int y1 = std::clamp( y + height, 0, m_Height );

        const std::size_t pixel = m_FrameBits == 16 ? 2 : 4;
        std::memset( out, 0, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * pixel );

        if (x1 <= x0 || y1 <= y0)
        {
            return 1;
        }

        // Small probes skip the downscale of the whole frame, the 4x4 sun probe may take the
        // pixels of its previous request.
        const bool sun_probe = width == 4 && height == 4;

        if ((x1 - x0) * (y1 - y0) <= 32 * 32 && ReadRectProbe( x0, y0, x1, y1, x, y, width, sun_probe, static_cast<std::uint8_t *>(out) ))
        {
            return 1;
        }

        ID3D11Texture2D * source_texture = m_Frame.Get();

        if (!IsDirect())
        {
            DownsampleFrame( IsFrontSelected() );
            source_texture = m_GameFrame.Get();
        }

        D3D11_BOX box = { static_cast<UINT>(x0), static_cast<UINT>(y0), 0, static_cast<UINT>(x1), static_cast<UINT>(y1), 1 };
        m_Context->CopySubresourceRegion( m_Staging.Get(), 0, static_cast<UINT>(x0), static_cast<UINT>(y0), 0, source_texture, 0, &box );
        D3D11_MAPPED_SUBRESOURCE mapped = {};

        if (FAILED( m_Context->Map( m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped ) ))
        {
            return 0;
        }

        auto * destination = static_cast<std::uint8_t *>(out);

        for (int row = y0; row < y1; ++row)
        {
            const auto * source = static_cast<const std::uint8_t *>( mapped.pData ) + static_cast<std::size_t>( row ) * mapped.RowPitch +
                static_cast<std::size_t>( x0 ) * 4;
            auto * line = destination + (static_cast<std::size_t>( row - y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x0 - x )) * pixel;

            if (pixel == 2)
            {
                ToRgb565( source, 0, line, 0, x1 - x0, 1 );
            }
            else
            {
                std::memcpy( line, source, static_cast<std::size_t>(x1 - x0) * 4 );
            }
        }

        m_Context->Unmap( m_Staging.Get(), 0 );
        return 1;
    }


    bool Driver::ReadRectProbe(
        int x0,
        int y0,
        int x1,
        int y1,
        int x,
        int y,
        int width,
        bool allow_previous,
        std::uint8_t * out
    )
    {
        if (IsFrontSelected() || !m_Resolved)
            return false;

        // Render pixels covered by the game pixels, the source of every game pixel is the render
        // pixel under its center like the linear downscale would pick it for small factors.
        ProbeRead request = {};
        request.m_X0 = x0;
        request.m_Y0 = y0;
        request.m_X1 = x1;
        request.m_Y1 = y1;
        request.m_X = x;
        request.m_Y = y;
        request.m_Width = width;
        request.m_ScaleX = m_ScaleX;
        request.m_ScaleY = m_ScaleY;
        request.m_FrameBits = m_FrameBits;
        request.m_RenderX0 = std::clamp( static_cast<int>(std::floor( static_cast<float>(x0) * m_ScaleX )), 0, m_RenderWidth - 1 );
        request.m_RenderY0 = std::clamp( static_cast<int>(std::floor( static_cast<float>(y0) * m_ScaleY )), 0, m_RenderHeight - 1 );
        request.m_RenderX1 = std::clamp( static_cast<int>(std::ceil( static_cast<float>(x1) * m_ScaleX )), request.m_RenderX0 + 1, m_RenderWidth );
        request.m_RenderY1 = std::clamp( static_cast<int>(std::ceil( static_cast<float>(y1) * m_ScaleY )), request.m_RenderY0 + 1, m_RenderHeight );

        if (request.m_RenderX1 - request.m_RenderX0 > PROBE_SIZE || request.m_RenderY1 - request.m_RenderY0 > PROBE_SIZE)
            return false;

        for (ComPtr<ID3D11Texture2D> & probe : m_Probes)
        {
            if (probe)
                continue;

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = PROBE_SIZE;
            desc.Height = PROBE_SIZE;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, probe.Put() ) ))
                return false;
        }

        // The current request is copied in any case, it serves the next sun read.
        const std::size_t slot = m_ProbeNext;
        const std::size_t previous = slot ^ 1;
        m_ProbeNext = previous;

        ResolveFrame();

        const D3D11_BOX box = { static_cast<UINT>(request.m_RenderX0), static_cast<UINT>(request.m_RenderY0), 0,
            static_cast<UINT>(request.m_RenderX1), static_cast<UINT>(request.m_RenderY1), 1 };
        m_Context->CopySubresourceRegion( m_Probes[slot].Get(), 0, 0, 0, 0, m_Resolved.Get(), 0, &box );

        request.m_Time = std::chrono::steady_clock::now();
        request.m_Valid = true;
        m_ProbeReads[slot] = request;

        // A recent read of the same size and frame layout is answered from the previous copy.
        std::size_t source = slot;
        const ProbeRead & last = m_ProbeReads[previous];

        if (allow_previous && last.m_Valid && request.m_Time - last.m_Time < PROBE_REUSE_WINDOW && last.m_Width == width &&
            last.m_X1 - last.m_X0 == x1 - x0 && last.m_Y1 - last.m_Y0 == y1 - y0 && last.m_ScaleX == m_ScaleX && last.m_ScaleY == m_ScaleY &&
            last.m_FrameBits == m_FrameBits)
        {
            source = previous;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};

        if (FAILED( m_Context->Map( m_Probes[source].Get(), 0, D3D11_MAP_READ, 0, &mapped ) ))
        {
            m_ProbeReads[slot].m_Valid = false;
            return false;
        }

        // The game gives up on the lens flare when reads average more than 5 ms.
        const auto waited = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - request.m_Time );

        if (waited.count() > 5000 && m_SlowProbeLogs < 10)
        {
            ++m_SlowProbeLogs;
            Log( "readrect %dx%d waited %.1f ms for the GPU, %s copy", x1 - x0, y1 - y0, static_cast<double>(waited.count()) / 1000.0,
                source == slot ? "current" : "previous" );
        }

        // A copy that answered is not used twice, the one just made answers the next read.
        const ProbeRead read = m_ProbeReads[source];

        if (source == previous)
        {
            m_ProbeReads[previous].m_Valid = false;
        }

        const std::size_t pixel = read.m_FrameBits == 16 ? 2 : 4;

        // The caller cleared the whole output, only the part inside the frame is written.
        for (int row = read.m_Y0; row < read.m_Y1; ++row)
        {
            const int render_row = std::clamp( static_cast<int>( (static_cast<float>(row) + 0.5f) * read.m_ScaleY ), read.m_RenderY0, read.m_RenderY1 - 1 ) -
                read.m_RenderY0;
            const auto * line_in = static_cast<const std::uint8_t *>( mapped.pData ) + static_cast<std::size_t>( render_row ) * mapped.RowPitch;
            auto * line_out = out + (static_cast<std::size_t>( row - read.m_Y ) * static_cast<std::size_t>( read.m_Width ) +
                static_cast<std::size_t>( read.m_X0 - read.m_X )) * pixel;

            for (int column = read.m_X0; column < read.m_X1; ++column)
            {
                const int render_column = std::clamp( static_cast<int>( (static_cast<float>(column) + 0.5f) * read.m_ScaleX ), read.m_RenderX0,
                    read.m_RenderX1 - 1 ) - read.m_RenderX0;
                const auto * source_pixel = line_in + static_cast<std::size_t>( render_column ) * 4;
                auto * destination = line_out + static_cast<std::size_t>( column - read.m_X0 ) * pixel;

                if (pixel == 2)
                {
                    ToRgb565( source_pixel, 0, destination, 0, 1, 1 );
                }
                else
                {
                    std::memcpy( destination, source_pixel, 4 );
                }
            }
        }

        m_Context->Unmap( m_Probes[source].Get(), 0 );
        return true;
    }


    void Driver::ResetProbes()
    {
        for (ComPtr<ID3D11Texture2D> & probe : m_Probes)
        {
            probe.Reset();
        }

        m_ProbeReads = {};
        m_ProbeNext = 0;
    }


    int Driver::WriteRect(
        int x,
        int y,
        int width,
        int height,
        const void * in
    )
    {
        Trace( "writerect %d %d %d %d", x, y, width, height );

        if (!m_Frame || in == nullptr || width <= 0 || height <= 0)
        {
            return 0;
        }

        Flush();

        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        if (x < 0 || y < 0 || x + width > m_Width || y + height > m_Height)
        {
            return 0;
        }

        std::vector<std::uint8_t> converted;

        if (m_FrameBits == 16)
        {
            converted.resize( static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 );
            FromRgb565( static_cast<const std::uint8_t *>(in), static_cast<std::uint32_t>(width) * 2, converted.data(), static_cast<std::uint32_t>(width) * 4,
                width, height );
            in = converted.data();
        }

        D3D11_BOX box = { static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x + width), static_cast<UINT>(y + height), 1 };

        if (IsDirect())
        {
            m_Context->UpdateSubresource( m_Frame.Get(), 0, &box, in, static_cast<UINT>(width) * 4, 0 );
            return 1;
        }

        m_Context->UpdateSubresource( m_Upload.Get(), 0, &box, in, static_cast<UINT>(width) * 4, 0 );

        const float destination[4] = { static_cast<float>(x) * m_ScaleX, static_cast<float>(y) * m_ScaleY, static_cast<float>(width) * m_ScaleX,
            static_cast<float>(height) * m_ScaleY };
        const float source[4] = { static_cast<float>(x) / static_cast<float>(m_Width), static_cast<float>(y) / static_cast<float>(m_Height),
            static_cast<float>(width) / static_cast<float>(m_Width), static_cast<float>(height) / static_cast<float>(m_Height) };

        const bool front = IsFrontSelected();
        Blit( m_UploadView.Get(), front ? m_FrontTarget.Get() : m_FrameView.Get(), m_RenderWidth, m_RenderHeight, destination, source, 1.0f,
            m_PresentSampler.Get() );

        if (front)
        {
            PresentFront();
        }

        return 1;
    }

    // Textures.

    Texture * Driver::TextureAlloc(
        int width,
        int height,
        int format,
        int,
        int mips
    )
    {
        if (!m_Device || width <= 0 || height <= 0)
            return nullptr;

        auto texture = std::make_unique<Texture>();
        texture->m_Width = width;
        texture->m_Height = height;
        texture->m_Format = format & 0xFF;
        texture->m_Stage = mips > 0 && (static_cast<std::uint32_t>(mips) & 0xFFFF0000u) == 0x10000u ? 1 : 0;
        texture->m_Levels = mips > 0 ? static_cast<int>(static_cast<std::uint32_t>(mips) & 0xFFFFu) + 1 : 1;

        // Never more levels than the size allows.
        int max_levels = 1;
        for (int size = std::max( width, height ); size > 1; size >>= 1)
        {
            ++max_levels;
        }

        texture->m_Levels = std::clamp( texture->m_Levels, 1, max_levels );

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = static_cast<UINT>(texture->m_Levels);
        desc.ArraySize = 1;
        desc.Format = formats::ToDxgi( texture->m_Format );
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, texture->m_Texture.Put() ) ))
        {
            Log( "talloc %dx%d format %d failed", width, height, format );
            return nullptr;
        }

        m_Device->CreateShaderResourceView( texture->m_Texture.Get(), nullptr, texture->m_View.Put() );

        for (int level = 0, w = width, h = height; level < texture->m_Levels; ++level, w = std::max( w / 2, 1 ), h = std::max( h / 2, 1 ))
        {
            const std::size_t rows = formats::IsBlockCompressed( texture->m_Format ) ? static_cast<std::size_t>((h + 3) / 4) : static_cast<std::size_t>(h);
            texture->m_Bytes += formats::UploadPitch( texture->m_Format, w ) * rows;
        }

        m_MeasureTextureBytes += texture->m_Bytes;

        if (m_ConfigMeasure > 0)
        {
            const std::uint64_t key = (static_cast<std::uint64_t>(texture->m_Format & 0xFF) << 48) |
                (static_cast<std::uint64_t>(width & 0xFFFF) << 32) | (static_cast<std::uint64_t>(height & 0xFFFF) << 16) |
                static_cast<std::uint64_t>(texture->m_Levels & 0xFFFF);

            if (m_MeasureAllocs[key]++ == 0)
            {
                Log( "M3T talloc %s %dx%d levels %d", FormatName( texture->m_Format ), width, height, texture->m_Levels );
            }
        }

        Texture * raw = texture.get();
        m_Textures.emplace( raw, std::move( texture ) );
        return raw;
    }


    bool Driver::IsTextureHandle(
        std::uintptr_t handle
    ) const
    {
        return handle >= 0x10000 && m_Textures.find( reinterpret_cast<Texture *>(handle) ) != m_Textures.end();
    }


    int Driver::TextureUpdate(
        Texture * texture,
        const void * pixels,
        const std::uint32_t * palette
    )
    {
        if (texture != nullptr && !IsTextureHandle( reinterpret_cast<std::uintptr_t>(texture) ))
        {
            return 0;
        }

        if (texture == nullptr || pixels == nullptr || !texture->m_Texture)
        {
            return texture != nullptr;
        }

        if (m_Bound[0] == texture || m_Bound[1] == texture)
        {
            Flush();
        }

        // Compressed images carry an 8 byte header ahead of the blocks, dx7z skips it too.
        const auto * source = static_cast<const std::uint8_t *>(pixels);

        if (formats::IsBlockCompressed( texture->m_Format ))
        {
            source += 8;
        }

        if (m_ConfigMeasure > 0 && ++texture->m_Updates == 2)
        {
            ++m_MeasureDynamic;
            Log( "M3T dynamic texture %s %dx%d levels %d", FormatName( texture->m_Format ), texture->m_Width, texture->m_Height, texture->m_Levels );
        }

        std::vector<std::uint8_t> converted;
        int width = texture->m_Width;
        int height = texture->m_Height;

        for (int level = 0; level < texture->m_Levels; ++level)
        {
            const std::size_t pitch = formats::UploadPitch( texture->m_Format, width );
            const std::size_t rows = formats::IsBlockCompressed( texture->m_Format ) ? static_cast<std::size_t>( (std::max( height, 1 ) + 3) / 4 )
                : static_cast<std::size_t>( std::max( height, 1 ) );
            converted.resize( pitch * rows );
            formats::ConvertLevel( texture->m_Format, width, height, source, palette, converted.data() );

            if (level == 0)
            {
                std::uint32_t hash = 2166136261u;

                for (std::size_t i = 0; i < converted.size(); ++i)
                {
                    hash = (hash ^ converted[i]) * 16777619u;
                }

                texture->m_Hash = hash;

                if (formats::IsBlockCompressed( texture->m_Format ))
                {
                    // DXT1 may punch through and DXT3 carries alpha, not worth decoding here.
                    texture->m_Translucent = true;
                }
                else
                {
                    const auto * texels = reinterpret_cast<const std::uint32_t *>(converted.data());
                    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(std::max( height, 1 ));

                    texture->m_Translucent = std::any_of( texels, texels + count, [] ( std::uint32_t texel )
                    {
                        return (texel >> 24) != 0xFF;
                    } );
                }
            }
            m_Context->UpdateSubresource( texture->m_Texture.Get(), static_cast<UINT>( level ), nullptr, converted.data(), static_cast<UINT>( pitch ), 0 );

            source += formats::LevelSize( texture->m_Format, width, height );
            width = std::max( width / 2, 1 );
            height = std::max( height / 2, 1 );
        }
        return 1;
    }


    int Driver::TextureFree(
        Texture * texture
    )
    {
        const auto it = m_Textures.find( texture );

        if (it == m_Textures.end())
            return 0;

        Flush();

        for (auto & bound : m_Bound)
        {
            if (bound == texture)
            {
                bound = nullptr;
            }
        }

        it->second->m_Magic = 0;
        m_MeasureUv.erase( it->first );
        m_MeasureTextureBytes -= std::min( m_MeasureTextureBytes, it->second->m_Bytes );
        m_Textures.erase( it );
        return 1;
    }


    int Driver::TextureReset()
    {
        Flush();

        if (m_ConfigMeasure > 0)
        {
            MeasureReport( "texture reset" );
        }
        m_Bound.fill( nullptr );

        for (auto & entry : m_Textures)
        {
            entry.second->m_Magic = 0;
        }

        m_Textures.clear();
        m_MeasureUv.clear();
        m_MeasureTextureBytes = 0;

        // dx7z counts resets in state 37 and unbinds stage 0.
        StoreState( 0x37, static_cast<std::uint32_t>(GetState( 0x37 ) + 1) );
        SetTexture( 0 );
        return 1;
    }


    int Driver::SetTexture(
        std::uintptr_t handle
    )
    {
        if (handle < 16)
        {
            Flush();

            if (handle == 0)
            {
                m_Bound.fill( nullptr );
            }
            else if (handle == 1)
            {
                m_Bound[0] = nullptr;
            }
            else if (handle == 2)
            {
                m_Bound[1] = nullptr;
            }

            StoreState( 1, static_cast<std::uint32_t>(handle) );
            return 1;
        }

        if (!IsTextureHandle( handle ))
            return 0;

        auto * texture = reinterpret_cast<Texture *>(handle);

        if (m_Bound[static_cast<std::size_t>(texture->m_Stage)] != texture)
        {
            Flush();
            m_Bound[static_cast<std::size_t>(texture->m_Stage)] = texture;
        }

        StoreState( 1, static_cast<std::uint32_t>(handle) );
        return 1;
    }

    // States.

    void Driver::StoreState(
        std::uint32_t id,
        std::uint32_t value
    )
    {
        const std::uint32_t stage = (id >> 16) & 7;
        m_StateStore[id & 0xFFFF][stage] = value;
    }


    std::intptr_t Driver::GetState(
        std::uint32_t id
    )
    {
        const auto it = m_StateStore.find( id & 0xFFFF );

        if (it == m_StateStore.end())
            return 0;

        return static_cast<std::intptr_t>(it->second[(id >> 16) & 7]);
    }


    void Driver::SetTextureStageMode(
        int stage,
        std::uint32_t mode
    )
    {
        auto & s = m_D3D.m_Stage[static_cast<std::size_t>(stage)];
        const std::uint32_t current_or_diffuse = stage == 0 ? ta::DIFFUSE : ta::CURRENT;

        auto standard = [&] ( std::uint32_t color_op, std::uint32_t alpha_op )
        {
            s[tss::COLORARG1] = ta::TEXTURE;
            s[tss::COLORARG2] = ta::CURRENT;
            s[tss::COLOROP] = color_op;
            s[tss::ALPHAOP] = alpha_op;
            s[tss::ALPHAARG1] = ta::TEXTURE;
            s[tss::ALPHAARG2] = current_or_diffuse;
        };

        switch (mode)
        {
            case 0:
            case 8:
                if (stage == 0)
                {
                    s[tss::COLOROP] = top::MODULATE;
                    s[tss::COLORARG1] = ta::TEXTURE;
                    s[tss::COLORARG2] = ta::DIFFUSE;
                    s[tss::ALPHAOP] = top::MODULATE;
                    s[tss::ALPHAARG1] = ta::TEXTURE;
                    s[tss::ALPHAARG2] = ta::DIFFUSE;
                }
                else if (mode == 0)
                {
                    s[tss::COLOROP] = top::DISABLE;
                    s[tss::ALPHAOP] = top::DISABLE;
                }
                else
                {
                    s[tss::COLORARG1] = ta::TEXTURE;
                    s[tss::COLORARG2] = ta::CURRENT;
                    s[tss::ALPHAARG1] = ta::TEXTURE;
                    s[tss::ALPHAARG2] = ta::CURRENT;
                    s[tss::COLOROP] = top::BLENDTEXTUREALPHA;
                    s[tss::ALPHAOP] = top::DISABLE;
                }
                break;

            case 1:
                standard( top::ADD, top::ADD );
                break;

            case 2:
                standard( top::DISABLE, top::DISABLE );
                break;

            case 3:
                standard( top::MODULATE, top::MODULATE );
                break;

            case 4:
                standard( top::SUBTRACT, top::SELECTARG1 );
                break;

            case 5:
                standard( top::SELECTARG2, top::ADDSIGNED );
                s[tss::ALPHAARG1] = ta::TEXTURE | ta::COMPLEMENT;
                s[tss::ALPHAARG2] = ta::CURRENT;
                break;

            case 6:
                standard( top::MODULATE2X, top::MODULATE2X );
                break;

            case 7:
                standard( top::MODULATE4X, top::MODULATE4X );
                break;

            case 0x61:
                standard( top::ADDSIGNED, top::ADDSIGNED );
                break;

            case 0x62:
                standard( top::ADD, top::BLENDFACTORALPHA );
                s[tss::ALPHAARG2] = ta::TFACTOR;
                break;

            case 0x63:
                standard( top::BLENDFACTORALPHA, top::SELECTARG1 );
                break;

            default:
                break;
        }
    }

    std::intptr_t Driver::SetState(
        std::uint32_t id,
        std::uintptr_t value
    )
    {
        const std::uint32_t state = id & 0xFFFF;
        const int stage = std::min<int>( static_cast<int>((id >> 16) & 7), MAX_STAGES - 1 );
        const std::uint32_t v = static_cast<std::uint32_t>(value);

        auto & render = m_D3D.m_Render;
        auto & tex_stage = m_D3D.m_Stage[static_cast<std::size_t>(stage)];

        std::intptr_t result = 1;
        const bool cached = state < STATE_CACHE_STATES && IsCachedState( state );

        // Render states ignore the stage bits, texture stage states use the clamped stage, so two
        // ids that write the same value share one cache entry.
        const std::size_t cache_slot = static_cast<std::size_t>(state) * 8 + (IsStageState( state ) ? static_cast<std::size_t>(stage) : 0);

        // The game sets the same states again for every mesh. A value that is already in effect
        // changes nothing, so the batch goes on instead of ending in a separate draw.
        if (cached && m_StateCacheGeneration[cache_slot] == m_StateGeneration && m_StateCacheValue[cache_slot] == v)
        {
            return 1;
        }

        // State changes end the current batch, as dx7z does before every render state call.
        Flush();

        switch (state)
        {
            case 1:
                return SetTexture( value );

            case 2:
                if (v == 0)
                {
                    render[rs::CULLMODE] = 1;
                }
                else if (v == 2)
                {
                    render[rs::CULLMODE] = 2;
                }
                else if (v == 1)
                {
                    render[rs::CULLMODE] = 3;
                }
                else
                {
                    return 0;
                }
                break;

            case 3:
                m_BackgroundColor = v;
                break;

            case 4:
                if (v == 0)
                {
                    render[rs::ZWRITEENABLE] = 0;
                    render[rs::ZENABLE] = 0;
                    render[rs::ZFUNC] = 8;
                }
                else if (v == 1 || v == 2)
                {
                    render[rs::ZENABLE] = 1;
                    render[rs::ZWRITEENABLE] = 1;

                    const std::intptr_t stored = GetState( 0x28 );
                    render[rs::ZFUNC] = static_cast<std::uint32_t>(stored) + 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 5:
                render[rs::DITHERENABLE] = v != 0;
                break;

            case 6:
                if (v == 0)
                {
                    render[rs::SHADEMODE] = 1;
                    render[rs::SPECULARENABLE] = 0;
                    m_WhiteDiffuse = true;
                }
                else if (v == 1 || v == 2)
                {
                    render[rs::SHADEMODE] = 2;
                    render[rs::SPECULARENABLE] = v == 2;
                    m_WhiteDiffuse = false;
                }
                else
                {
                    return 0;
                }
                break;

            case 7:
                if (v == 0)
                {
                    tex_stage[tss::MAGFILTER] = 1;
                    tex_stage[tss::MINFILTER] = 1;
                }
                else if (v == 1)
                {
                    tex_stage[tss::MAGFILTER] = 2;
                    tex_stage[tss::MINFILTER] = 2;
                }
                else if (v == 2)
                {
                    tex_stage[tss::MAGFILTER] = 5;
                    tex_stage[tss::MINFILTER] = 3;
                }
                else
                {
                    return 0;
                }
                break;

            case 8:
                tex_stage[tss::MIPMAPLODBIAS] = v;
                break;

            case 9:
                render[rs::ANTIALIAS] = v >= 2 ? 2 : 0;
                break;

            case 0x0A:
                if (v == 0)
                {
                    render[rs::ALPHABLENDENABLE] = 0;
                }
                else if (v == 2)
                {
                    render[rs::ALPHABLENDENABLE] = 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x0B:
                if (v == 0)
                {
                    tex_stage[tss::MIPFILTER] = 1;
                }
                else if (v == 1)
                {
                    tex_stage[tss::MIPFILTER] = 2;
                }
                else if (v == 3)
                {
                    tex_stage[tss::MIPFILTER] = 3;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x0C:
            case 0x10:
            case 0x11:
            case 0x31:
            case 0x32:
            case 0x33:
                return 0;

            case 0x0D:
            {
                const std::uint32_t address = v == 0 ? 3u : (v == 1 ? 1u : (v == 2 ? 2u : 0u));

                if (address == 0)
                {
                    return 0;
                }

                tex_stage[tss::ADDRESSU] = address;
                tex_stage[tss::ADDRESSV] = address;
                break;
            }

            case 0x0E:
                if (v == 0 || (v & 0xFFFFF000u) != 0)
                {
                    render[rs::FOGDENSITY] = v;
                }
                else
                {
                    render[rs::FOGDENSITY] = AsBits( 1.0f / static_cast<float>(v) );
                }
                break;

            case 0x0F:
                render[rs::FOGCOLOR] = v & 0xFFFFFF;
                break;

            case 0x12:
                m_Windowed = v != 0;
                break;

            case 0x13:
                if (value == 0)
                {
                    m_Callbacks.fill( 0 );
                }
                else
                {
                    std::memcpy( m_Callbacks.data(), reinterpret_cast<const void *>(value), sizeof( m_Callbacks ) );
                }
                break;

            case 0x14:
            case 0x69:
                if (value != 0)
                {
                    // Linear interpolation of the game table over z, stored inverted as in dx7z
                    const auto * table = reinterpret_cast<const std::uint8_t *>(value);

                    for (int i = 0; i < 256; ++i)
                    {
                        m_FogTable[static_cast<std::size_t>( i )] = static_cast<std::uint8_t>( 255 - table[i] );
                    }

                    render[rs::FOGTABLEMODE] = 0;
                    m_FogTableActive = true;
                }
                break;

            case 0x15:
                if (v == 4 || v == 8 || v == 2)
                {
                    m_FogTableActive = false;
                    render[rs::FOGTABLEMODE] = v == 4 ? 1u : (v == 8 ? 2u : 3u);
                    render[rs::FOGVERTEXMODE] = 0;

                    if (v == 2)
                    {
                        render[rs::FOGSTART] = AsBits( 0.0f );
                        render[rs::FOGEND] = AsBits( 1.0f );
                    }
                }
                else if (v == 0)
                {
                    render[rs::FOGENABLE] = 0;
                }
                else if (v == 1)
                {
                    render[rs::FOGENABLE] = 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x16:
            case 0x17:
                break;

            case 0x18:
            case 0x66:
                if (m_LoggedDepthBias.insert( static_cast<std::int32_t>(v) ).second)
                    Log( "Depth bias %d (state 0x%X) first used", static_cast<std::int32_t>(v), state );
                m_DepthBias = m_ConfigTestNoZBias ? 0.0f : static_cast<float>(static_cast<std::int32_t>(v)) * DEPTH_BIAS_UNIT;
                break;

            case 0x19:
                m_Window = reinterpret_cast<HWND>(value);
                break;

            case 0x23:
                result = 1;
                break;

            case 0x24:
                if (v == 0)
                {
                    render[rs::ALPHATESTENABLE] = 0;
                    render[rs::ALPHAFUNC] = 8;
                }
                else
                {
                    const std::intptr_t func = GetState( 0x40 );
                    render[rs::ALPHATESTENABLE] = 1;
                    render[rs::ALPHAFUNC] = m_StateStore.count( 0x40 ) != 0 ? static_cast<std::uint32_t>(func) + 1 : 5u;
                    render[rs::ALPHAREF] = v;
                }
                break;

            case 0x3F:
                // Guard band (FUN_00549220 reads floats at +4 and +0xC and wants both above 1.5 x the screen):
                // with it the game sends triangles across the screen edge whole instead of cutting them on the CPU,
                // D3D11 clips them. Needed for the water reflection (B15) of objects partly off screen.
                if (m_ConfigGuardBand && value != 0)
                {
                    float * guard = reinterpret_cast<float *>(value);
                    guard[0] = -32768.0f;
                    guard[1] = 32768.0f;
                    guard[2] = -32768.0f;
                    guard[3] = 32768.0f;
                    Log( "Guard band reported to the game" );
                    return 1;
                }
                return 0;

            case 0x25:
            case 0x26:
            case 0x193:
                // No callback timer, no DirectDraw object, no D3D7 caps.
                return 0;

            case 0x28:
                if (v > 7)
                {
                    return 1;
                }
                render[rs::ZFUNC] = v + 1;
                break;

            case 0x29:
                SetTextureStageMode( stage, v );
                break;

            case 0x2A:
                m_TwoUvSets = v != 0;
                break;

            case 0x2E:
            case 0x65:
            {
                float brightness = AsFloat( v );

                if (!(brightness > 0.0f))
                    brightness = 0.0f;

                m_Brightness = std::min( brightness, 4.0f );
                break;
            }

            case 0x2F:
                render[rs::STENCILENABLE] = v != 0;
                break;

            case 0x30:
                if (v > 7)
                    return 1;

                render[rs::STENCILFUNC] = v + 1;
                break;

            case 0x34:
            case 0x35:
            case 0x36:
                if (v > 8 || v == 6)
                    return 1;

                render[state == 0x34 ? rs::STENCILFAIL : (state == 0x35 ? rs::STENCILZFAIL : rs::STENCILPASS)] = v + 1;
                break;

            case 0x38:
            case 0x68:
                if (v == 0)
                {
                    render[rs::SRCBLEND] = 5;
                    render[rs::DESTBLEND] = 6;
                }
                else if (v == 1)
                {
                    render[rs::SRCBLEND] = 5;
                    render[rs::DESTBLEND] = 2;
                }
                else if (v == 2)
                {
                    render[rs::SRCBLEND] = 1;
                    render[rs::DESTBLEND] = 6;
                }
                else if (v == 3)
                {
                    render[rs::SRCBLEND] = 9;
                    render[rs::DESTBLEND] = 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 0x39:
            case 0x6A:
                render[rs::ZWRITEENABLE] = v != 0;
                break;

            case 0x3B:
                m_SelectedWindow = static_cast<int>(v);
                break;

            case 0x3C:
            case 0x67:
                m_WaitVblank = v != 0;
                break;

            case 0x40:
                if (v > 7)
                {
                    return 1;
                }
                render[rs::ALPHAFUNC] = v + 1;
                break;

            case 0x41:
            case 0x42:
            {
                const std::uint32_t address = v == 0 ? 3u : (v == 1 ? 1u : (v == 2 ? 2u : 0u));

                if (address == 0)
                {
                    return 0;
                }
                tex_stage[state == 0x41 ? tss::ADDRESSU : tss::ADDRESSV] = address;
                break;
            }

            case 0x43:
                m_ClearDepthValue = std::clamp( AsFloat( v ), 0.0f, 1.0f );
                break;

            case 0x194:
            case 0x195:
            {
                static constexpr std::uint32_t BLENDS[10] = { 2, 1, 5, 6, 7, 8, 3, 9, 4, 10 };

                if (v < 10)
                {
                    render[state == 0x194 ? rs::SRCBLEND : rs::DESTBLEND] = BLENDS[v];
                }
                break;
            }

            case 0x19A:
                render[rs::TEXTUREFACTOR] = v;
                break;

            case 0x1B3:
                DrawRaw( reinterpret_cast<const std::uint32_t *>(value) );
                return 1;

            case 0x1C3:
                // Modulated texture alpha is supported.
                return 1;

            default:
                // Values only stored for 'getstate', like in dx7z [6000A430].
                break;
        }

        StoreState( id, v );

        // States that write the same values as this one no longer know what is in effect.
        ForgetRelatedStates( state );

        if (cached)
        {
            m_StateCacheValue[cache_slot] = v;
            m_StateCacheGeneration[cache_slot] = m_StateGeneration;
        }

        return result;
    }


    void Driver::ForgetRelatedStates(
        std::uint32_t state
    )
    {
        // Groups of states that write the same pipeline values.
        static constexpr std::uint32_t BLEND[] = { 0x38, 0x68, 0x194, 0x195 };
        static constexpr std::uint32_t DEPTH[] = { 0x04, 0x28, 0x39, 0x6A };
        static constexpr std::uint32_t BIAS[] = { 0x18, 0x66 };
        static constexpr std::uint32_t ADDRESS[] = { 0x0D, 0x41, 0x42 };
        static constexpr std::uint32_t ALPHA[] = { 0x24, 0x40 };
        static constexpr std::uint32_t FOG[] = { 0x14, 0x15, 0x69 };

        const auto forget = [&] ( const auto & group )
        {
            if (std::find( std::begin( group ), std::end( group ), state ) == std::end( group ))
                return;

            for (const std::uint32_t other : group)
            {
                if (other == state)
                    continue;

                for (std::size_t stage = 0; stage < 8; ++stage)
                {
                    m_StateCacheGeneration[static_cast<std::size_t>(other) * 8 + stage] = 0;
                }
            }
        };

        forget( BLEND );
        forget( DEPTH );
        forget( BIAS );
        forget( ADDRESS );
        forget( ALPHA );
        forget( FOG );
    }


    bool Driver::IsCachedState(
        std::uint32_t state
    )
    {
        // Only states that do nothing but set pipeline values from their own argument.
        switch (state)
        {
            case 0x02:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0D:
            case 0x0E:
            case 0x0F:
            case 0x15:
            case 0x18:
            case 0x24:
            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2F:
            case 0x30:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x38:
            case 0x39:
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x66:
            case 0x68:
            case 0x6A:
            case 0x194:
            case 0x195:
            case 0x19A:
                return true;

            default:
                return false;
        }
    }


    bool Driver::IsStageState(
        std::uint32_t state
    )
    {
        switch (state)
        {
            case 0x07:
            case 0x08:
            case 0x0B:
            case 0x0D:
            case 0x29:
            case 0x41:
            case 0x42:
                return true;

            default:
                return false;
        }
    }


    void Driver::ResetStateCache()
    {
        if (++m_StateGeneration == 0)
        {
            m_StateCacheGeneration.fill( 0 );
            m_StateGeneration = 1;
        }
    }


    void Driver::InvalidatePipeline()
    {
        m_SceneBound = false;
        m_ConstantsValid = false;
        m_BoundBlend = nullptr;
        m_BoundDepth = nullptr;
        m_BoundStencilRef = 0;
        m_BoundRaster = nullptr;
        m_ScissorValid = false;
        m_BoundTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        m_BoundViews.fill( nullptr );
        m_BoundSamplers.fill( nullptr );

        // The cached objects may have been released together with their maps.
        m_SamplerCache.fill( nullptr );
        m_BlendCache = nullptr;
        m_DepthCache = nullptr;
        m_RasterCache = nullptr;
    }

    // Drawing.

    void Driver::BeginBatch(
        Primitive primitive,
        std::size_t vertices,
        std::size_t indices
    )
    {
        if (m_Locked)
        {
            UnlockWindow( nullptr );
        }

        PrepareDayPending();
        NoteQueueList();

        if (m_BatchPrimitive != primitive || m_BatchVertices.size() + vertices > BATCH_VERTICES ||
            m_BatchIndices.size() + indices > BATCH_INDICES)
        {
            Flush();
        }

        m_BatchPrimitive = primitive;
    }


    void Driver::PushVertex(
        const std::uint8_t * source
    )
    {
        TlVertex vertex = {};
        std::memcpy( &vertex, source, VertexStride() );
        vertex.m_Z += m_DepthBias;

        // Skid marks lie flat at the height of the car body and run straight for several metres, the curved
        // road cuts through them (BUGS.md 19.1). Moving them by a share of the distance keeps the bias small up close.
        if (m_SkidDraw)
            vertex.m_Z = std::max( 0.0f, vertex.m_Z - m_ConfigSkidBias * (1.0f - vertex.m_Z) );

        if (m_WhiteDiffuse)
        {
            vertex.m_Diffuse = 0xFFFFFFFF;
        }
        if (m_FogTableActive)
        {
            const int index = std::clamp( static_cast<int>(std::lround( vertex.m_Z * 255.0f )), 0, 255 );
            vertex.m_Specular = static_cast<std::uint32_t>(m_FogTable[static_cast<std::size_t>(index)]) << 24;
        }

        m_BatchVertices.push_back( vertex );
    }


    void Driver::DrawTriangles(
        const std::uint8_t * vertices,
        const int * indices,
        int count,
        int stride_indices
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::TRIANGLES, 3, 3 );
            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );

            for (int corner = 0; corner < 3; ++corner)
            {
                const int index = indices != nullptr ? indices[i * stride_indices + corner] : i * 3 + corner;
                PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
                m_BatchIndices.push_back( static_cast<std::uint16_t>( base + corner ) );
            }
        }
    }


    void Driver::DrawPolygon(
        const std::uint8_t * const * corners,
        int count
    )
    {
        if (corners == nullptr || (count != 3 && count != 4))
            return;

        for (int corner = 0; corner < count; ++corner)
        {
            if (corners[corner] == nullptr)
            {
                return;
            }
        }

        BeginBatch( Primitive::TRIANGLES, static_cast<std::size_t>( count ), count == 4 ? 6 : 3 );
        const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );

        for (int corner = 0; corner < count; ++corner)
        {
            PushVertex( corners[corner] );
        }

        static constexpr std::uint16_t QUAD[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < (count == 4 ? 6 : 3); ++i)
        {
            m_BatchIndices.push_back( static_cast<std::uint16_t>( base + QUAD[i] ) );
        }
    }


    void Driver::DrawQuads(
        const std::uint8_t * vertices,
        const int * indices,
        int count
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::TRIANGLES, 4, 6 );
            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );

            for (int corner = 0; corner < 4; ++corner)
            {
                const int index = indices != nullptr ? indices[i * 4 + corner] : i * 4 + corner;
                PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
            }
            for (const int corner : { 0, 1, 2, 0, 2, 3 })
            {
                m_BatchIndices.push_back( static_cast<std::uint16_t>( base + corner ) );
            }
        }
    }


    void Driver::DrawStrip(
        const std::uint8_t * vertices,
        const int * indices,
        int triangles,
        bool fan
    )
    {
        if (vertices == nullptr || triangles <= 0)
        {
            return;
        }
        const std::uint32_t stride = VertexStride();
        const int count = triangles + 2;

        if (static_cast<std::size_t>(count) > BATCH_VERTICES)
            return;

        // Strips and fans bypass the per vertex processing in dx7z.
        const float bias = m_DepthBias;
        const bool white = m_WhiteDiffuse;
        const bool table = m_FogTableActive;

        m_DepthBias = 0.0f;
        m_WhiteDiffuse = false;
        m_FogTableActive = false;

        BeginBatch( Primitive::TRIANGLES, static_cast<std::size_t>(count), static_cast<std::size_t>(triangles) * 3 );
        const auto base = static_cast<std::uint16_t>(m_BatchVertices.size());

        for (int i = 0; i < count; ++i)
        {
            const int index = indices != nullptr ? indices[i] : i;
            PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
        }

        for (int i = 0; i < triangles; ++i)
        {
            std::uint16_t a;
            std::uint16_t b;
            std::uint16_t c;

            if (fan)
            {
                a = 0;
                b = static_cast<std::uint16_t>( i + 1 );
                c = static_cast<std::uint16_t>( i + 2 );
            }
            else if ((i & 1) == 0)
            {
                a = static_cast<std::uint16_t>(i);
                b = static_cast<std::uint16_t>(i + 1);
                c = static_cast<std::uint16_t>(i + 2);
            }
            else
            {
                a = static_cast<std::uint16_t>(i + 1);
                b = static_cast<std::uint16_t>(i);
                c = static_cast<std::uint16_t>(i + 2);
            }

            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + a) );
            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + b) );
            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + c) );
        }

        m_DepthBias = bias;
        m_WhiteDiffuse = white;
        m_FogTableActive = table;
    }


    void Driver::DrawLines(
        const std::uint8_t * vertices,
        const int * indices,
        int count,
        bool strip
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::LINES, 2, 2 );

            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );
            int a;
            int b;

            if (strip)
            {
                a = indices != nullptr ? indices[i] : i;
                b = indices != nullptr ? indices[i + 1] : i + 1;
            }
            else
            {
                a = indices != nullptr ? indices[i * 2] : i * 2;
                b = indices != nullptr ? indices[i * 2 + 1] : i * 2 + 1;
            }

            PushVertex( vertices + static_cast<std::size_t>(a) * stride );
            PushVertex( vertices + static_cast<std::size_t>(b) * stride );

            m_BatchIndices.push_back( base );
            m_BatchIndices.push_back( static_cast<std::uint16_t>(base + 1) );
        }
    }


    void Driver::DrawPoints(
        const std::uint8_t * vertices,
        const int * indices,
        int count
    )
    {
        if (vertices == nullptr || count <= 0)
            return;

        const std::uint32_t stride = VertexStride();

        for (int i = 0; i < count; ++i)
        {
            BeginBatch( Primitive::POINTS, 1, 1 );

            const auto base = static_cast<std::uint16_t>( m_BatchVertices.size() );
            const int index = indices != nullptr ? indices[i] : i;
            PushVertex( vertices + static_cast<std::size_t>( index ) * stride );
            m_BatchIndices.push_back( base );
        }
    }


    void Driver::DrawRaw(
        const std::uint32_t * description
    )
    {
        if (description == nullptr || !m_FrameView)
            return;

        PrepareDayPending();
        NoteQueueList();
        Flush();

        if (m_Locked)
            UnlockWindow( nullptr );

        // { primitive, fvf, vertices, vertex count (u16), indices, index count (u16) }.
        const std::uint32_t primitive = description[0];
        const std::uint32_t fvf = description[1];
        const auto * vertices = reinterpret_cast<const std::uint8_t *>(description[2]);
        const std::size_t vertex_count = description[3] & 0xFFFF;
        const auto * indices = reinterpret_cast<const std::uint16_t *>(description[4]);
        const std::size_t index_count = description[5] & 0xFFFF;
        const std::size_t stride = (fvf & 0xF00) == 0x200 ? 40 : 32;

        if (vertices == nullptr || vertex_count == 0 || vertex_count > RAW_VERTICES)
        {
            return;
        }

        // Expand strips and fans into lists so one topology per primitive class suffices.
        std::vector<std::uint16_t> & list = m_BatchIndices;
        list.clear();

        const std::size_t count = indices != nullptr ? index_count : vertex_count;
        auto at = [&] ( std::size_t i ) -> std::uint16_t
        {
            return indices != nullptr ? indices[i] : static_cast<std::uint16_t>(i);
        };

        Primitive kind = Primitive::TRIANGLES;
        switch (primitive)
        {
            case 1:
                kind = Primitive::POINTS;

                for (std::size_t i = 0; i < count; ++i)
                {
                    list.push_back( at( i ) );
                }
                break;

            case 2:
                kind = Primitive::LINES;

                for (std::size_t i = 0; i + 1 < count; i += 2)
                {
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                }
                break;

            case 3:
                kind = Primitive::LINES;

                for (std::size_t i = 0; i + 1 < count; ++i)
                {
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                }
                break;

            case 4:
                for (std::size_t i = 0; i + 2 < count; i += 3)
                {
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                    list.push_back( at( i + 2 ) );
                }
                break;

            case 5:
                for (std::size_t i = 0; i + 2 < count; ++i)
                {
                    const bool odd = (i & 1) != 0;
                    list.push_back( at( odd ? i + 1 : i ) );
                    list.push_back( at( odd ? i : i + 1 ) );
                    list.push_back( at( i + 2 ) );
                }
                break;

            case 6:
                for (std::size_t i = 1; i + 1 < count; ++i)
                {
                    list.push_back( at( 0 ) );
                    list.push_back( at( i ) );
                    list.push_back( at( i + 1 ) );
                }
                break;

            default:
                list.clear();
                return;
        }

        if (list.empty() || list.size() > RAW_INDICES)
        {
            list.clear();
            return;
        }

        m_BatchVertices.resize( vertex_count );

        for (std::size_t i = 0; i < vertex_count; ++i)
        {
            TlVertex vertex = {};
            std::memcpy( &vertex, vertices + i * stride, stride );
            m_BatchVertices[i] = vertex;
        }

        m_BatchPrimitive = kind;
        Flush();
    }


    ID3D11SamplerState * Driver::GetSampler(
        int stage
    )
    {
        const auto & s = m_D3D.m_Stage[static_cast<std::size_t>(stage)];

        const std::uint64_t key = static_cast<std::uint64_t>(s[tss::MAGFILTER] & 7) | (static_cast<std::uint64_t>(s[tss::MINFILTER] & 7) << 3) |
            (static_cast<std::uint64_t>(s[tss::MIPFILTER] & 3) << 6) | (static_cast<std::uint64_t>(s[tss::ADDRESSU] & 7) << 8) |
            (static_cast<std::uint64_t>(s[tss::ADDRESSV] & 7) << 11) | (static_cast<std::uint64_t>(s[tss::MIPMAPLODBIAS]) << 32);

        const std::size_t index = static_cast<std::size_t>(stage);

        if (m_SamplerCache[index] != nullptr && m_SamplerKeys[index] == key)
        {
            return m_SamplerCache[index];
        }

        auto & cached = m_Samplers[key];

        if (!cached)
        {
            const bool anisotropic = s[tss::MAGFILTER] == 5 || s[tss::MINFILTER] == 3 || (m_ConfigAnisotropy > 1 && s[tss::MINFILTER] >= 2);
            const bool mag_linear = s[tss::MAGFILTER] >= 2;
            const bool min_linear = s[tss::MINFILTER] >= 2;
            const std::uint32_t game_mip = s[tss::MIPFILTER];
            const std::uint32_t mip = m_ConfigForceMips && min_linear && game_mip <= 1 ? 3u : game_mip;

            D3D11_SAMPLER_DESC desc = {};
            if (anisotropic)
            {
                desc.Filter = D3D11_FILTER_ANISOTROPIC;
                desc.MaxAnisotropy = m_ConfigAnisotropy > 1 ? static_cast<UINT>(std::min( m_ConfigAnisotropy, 16 )) : 16u;
            }
            else
            {
                int filter = 0;

                if (mip == 3)
                {
                    filter |= 0x1;
                }

                if (mag_linear)
                {
                    filter |= 0x4;
                }

                if (min_linear)
                {
                    filter |= 0x10;
                }

                desc.Filter = static_cast<D3D11_FILTER>(filter);
                desc.MaxAnisotropy = 1;
            }

            desc.AddressU = ToAddress( s[tss::ADDRESSU] );
            desc.AddressV = ToAddress( s[tss::ADDRESSV] );
            desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            // Fine textures shimmer at grazing angles (HD-TEXTUREN.md): the extra bias only applies where mips are sampled.
            desc.MipLODBias = AsFloat( s[tss::MIPMAPLODBIAS] ) + (mip >= 2 ? m_ConfigTextureLodBias : 0.0f);
            desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            desc.MinLOD = 0.0f;

            // Mip filter none samples the top level only.
            desc.MaxLOD = mip <= 1 || m_ConfigTestNoMips ? 0.0f : D3D11_FLOAT32_MAX;
            m_Device->CreateSamplerState( &desc, cached.Put() );

            if (m_ConfigMeasure > 0 && m_MeasureSamplers[key]++ == 0)
            {
                Log( "M3T sampler mag %u min %u mip %u (game %u) bias %.2f aniso %u address %u/%u", s[tss::MAGFILTER], s[tss::MINFILTER], mip, game_mip,
                    desc.MipLODBias, anisotropic ? desc.MaxAnisotropy : 0u, s[tss::ADDRESSU], s[tss::ADDRESSV] );
            }
        }

        m_SamplerKeys[index] = key;
        m_SamplerCache[index] = cached.Get();
        return cached.Get();
    }

    void Driver::ApplyPipeline()
    {
        const auto & render = m_D3D.m_Render;

        // Constants.
        DrawConstants constants = {};
        // Offset of the vertices in game pixels. 0: a clip edge of the game lands on the edge of the render pixels the
        // clip rectangle clears (ScaledClip); with +0.5 a sub view (rear view mirror) kept a line of its clear colour
        // left and on top when the frame is drawn larger than the game sees it, and edges were half covered (MSAA).
        constants.m_Target[0] = 0.0f;
        constants.m_Target[1] = 0.0f;
        constants.m_Target[2] = 1.0f / static_cast<float>(std::max( m_Width, 1 ));
        constants.m_Target[3] = 1.0f / static_cast<float>(std::max( m_Height, 1 ));

        for (int stage = 0; stage < MAX_STAGES; ++stage)
        {
            const auto & s = m_D3D.m_Stage[static_cast<std::size_t>( stage )];

            constants.m_ColorOp[stage][0] = static_cast<std::int32_t>( s[tss::COLOROP] );
            constants.m_ColorOp[stage][1] = static_cast<std::int32_t>( s[tss::COLORARG1] );
            constants.m_ColorOp[stage][2] = static_cast<std::int32_t>( s[tss::COLORARG2] );
            constants.m_AlphaOp[stage][0] = static_cast<std::int32_t>( s[tss::ALPHAOP] );
            constants.m_AlphaOp[stage][1] = static_cast<std::int32_t>( s[tss::ALPHAARG1] );
            constants.m_AlphaOp[stage][2] = static_cast<std::int32_t>( s[tss::ALPHAARG2] );
        }

        ColorToFloat4( render[rs::TEXTUREFACTOR], constants.m_Factor );
        ColorToFloat4( render[rs::FOGCOLOR] | 0xFF000000u, constants.m_FogColor );

        constants.m_FogParams[0] = AsFloat( render[rs::FOGSTART] );
        constants.m_FogParams[1] = AsFloat( render[rs::FOGEND] );
        constants.m_FogParams[2] = AsFloat( render[rs::FOGDENSITY] );
        constants.m_FogParams[3] = static_cast<float>(render[rs::FOGTABLEMODE]);
        constants.m_Flags[0] = render[rs::FOGENABLE] != 0;
        constants.m_Flags[1] = render[rs::SPECULARENABLE] != 0;
        constants.m_Flags[2] = render[rs::ALPHATESTENABLE] != 0 ? static_cast<std::int32_t>(render[rs::ALPHAFUNC]) : 8;
        constants.m_Flags[3] = (m_Bound[0] != nullptr ? 1 : 0) | (m_Bound[1] != nullptr ? 2 : 0);
        constants.m_AlphaRef[0] = static_cast<float>(render[rs::ALPHAREF] & 0xFF) / 255.0f;
        const bool far_fade = m_ConfigWaterFarFade > 0.0f && m_ReflWaterFar > 0.0f && m_ConfigReflections && m_QueueList > 0 && IsWaterTexture();
        constants.m_AlphaRef[2] = far_fade ? m_ReflWaterFar * (1.0f - m_ConfigWaterFarFade) : 0.0f;
        constants.m_AlphaRef[3] = far_fade ? m_ReflWaterFar : 0.0f;
        constants.m_AlphaRef[1] = m_ConfigShoreFade > 0.0f && m_Bound[0] != nullptr &&
            std::find( m_ConfigShoreTextures.begin(), m_ConfigShoreTextures.end(), m_Bound[0]->m_Hash ) != m_ConfigShoreTextures.end()
            ? m_ConfigShoreFade : 0.0f;
        constants.m_Debug[0] = m_ConfigMeasure == 2 && m_MeasureSuspect ? 1.0f : 0.0f;
        constants.m_Debug[1] = static_cast<float>(m_EdgePass);
        constants.m_Debug[2] = static_cast<float>(m_ConfigAlphaEdgeRef) / 255.0f;
        constants.m_Debug[3] = m_ConfigAlphaMipScale;
        constants.m_Test[0] = m_ConfigTestVertexAlpha ? 1.0f : 0.0f;
        constants.m_Clip[0] = static_cast<float>(m_Clip.left);
        constants.m_Clip[1] = static_cast<float>(m_Clip.top);
        constants.m_Clip[2] = static_cast<float>(m_Clip.right);
        constants.m_Clip[3] = static_cast<float>(m_Clip.bottom);

        // Other draws of the frame (cars, signs, objects) with the light of the last track mesh (PLAN.md 11.4, doc/LICHT.md 9.7):
        // only depth tested, opaque or normally alpha blended draws; additive and multiplying passes stay as they are.
        bool object_light = false;
        if (m_HeadMesh)
        {
            ++m_ObjectStats.m_Track;
        }
        else if (m_ConfigHeadlightsObjects > 0 && m_HeadMapView)
        {
            const std::uint32_t source = render[rs::SRCBLEND];
            const std::uint32_t destination = render[rs::DESTBLEND];
            const bool normal_blend = render[rs::ALPHABLENDENABLE] == 0 || source == 12 || (source == 5 && destination == 6);
            if (!m_HeadFrameValid)
                ++m_ObjectStats.m_NoState;
            else if (render[rs::ZENABLE] == 0)
                ++m_ObjectStats.m_NoDepth;
            else if (!normal_blend)
                ++m_ObjectStats.m_Blend;
            else
            {
                ++m_ObjectStats.m_Lit;
                object_light = true;
            }
        }

        if ((m_HeadMesh || object_light) && m_HeadMapView)
        {
            constants.m_Head[0] = 1.0f;
            constants.m_Head[1] = m_HeadScale[0];
            constants.m_Head[2] = m_HeadScale[1];
            constants.m_Head[3] = m_HeadScale[2];
            std::memcpy( constants.m_HeadColour, m_HeadColour.data(), sizeof( constants.m_HeadColour ) );
            if (object_light && m_ConfigHeadlightsObjects == 2)
            {
                for (int ch = 0; ch < 3; ++ch)
                    constants.m_HeadColour[ch] *= 4.0f;
            }
            std::memcpy( constants.m_HeadProjection, m_HeadProjection.data(), sizeof( constants.m_HeadProjection ) );
            std::memcpy( constants.m_ViewToLight, m_HeadViewToLight.data(), sizeof( constants.m_ViewToLight ) );
            const int extra = std::min( m_HeadExtraCount, std::min( static_cast<int>(m_CarLights.size()), MAX_CAR_LIGHTS ) );
            constants.m_Extra[0] = extra;
            std::memcpy( constants.m_HeadHigh, m_HeadHigh.data(), sizeof( constants.m_HeadHigh ) );
            const int rear = std::min( m_HeadRearCount, std::min( static_cast<int>(m_RearLights.size()), MAX_REAR_LIGHTS ) );
            constants.m_Rear[0] = rear;
            constants.m_RearShape[0] = m_ConfigTailOffset;
            constants.m_RearShape[1] = m_ConfigTailReach;
            constants.m_RearShape[2] = m_ConfigTailWidth;
            constants.m_RearShape[3] = m_ConfigTailSpread;
            for (int i = 0; i < rear; ++i)
            {
                const RearLight & light = m_RearLights[static_cast<std::size_t>(i)];
                std::memcpy( constants.m_RearLeft[i], light.m_Left.data(), sizeof( constants.m_RearLeft[i] ) );
                std::memcpy( constants.m_RearRight[i], light.m_Right.data(), sizeof( constants.m_RearRight[i] ) );
                std::memcpy( constants.m_RearViewToLight[i], m_HeadRearViewToLight.data() + i * 16, sizeof( constants.m_RearViewToLight[i] ) );
            }
            for (int i = 0; i < extra; ++i)
            {
                std::memcpy( constants.m_ExtraColour[i], m_CarLights[static_cast<std::size_t>(i)].m_Colour.data(), sizeof( constants.m_ExtraColour[i] ) );
                std::memcpy( constants.m_ExtraViewToLight[i], m_HeadExtraViewToLight.data() + i * 16, sizeof( constants.m_ExtraViewToLight[i] ) );
            }
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED( m_Context->Map( m_DrawConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, &constants, sizeof( constants ) );
            m_Context->Unmap( m_DrawConstants.Get(), 0 );
        }

        // Water reflections (B15): the mirrored copy draws without headlight model and without edge split.
        if (m_ReflCapture)
        {
            DrawConstants kept = constants;
            kept.m_Head[0] = 0.0f;
            kept.m_Debug[1] = 0.0f;
            const auto * bytes = reinterpret_cast<const std::uint8_t *>(&kept);
            m_ReflConstants.assign( bytes, bytes + sizeof( kept ) );
        }

        // Blend.
        const std::uint64_t blend_key = static_cast<std::uint64_t>(render[rs::ALPHABLENDENABLE] != 0) |
            (static_cast<std::uint64_t>(render[rs::SRCBLEND] & 0xFF) << 8) |
            (static_cast<std::uint64_t>(render[rs::DESTBLEND] & 0xFF) << 16);

        auto & blend = m_Blends[blend_key];

        if (!blend)
        {
            D3D11_BLEND_DESC desc = {};
            auto & target = desc.RenderTarget[0];
            std::uint32_t source_blend = render[rs::SRCBLEND];
            std::uint32_t destination_blend = render[rs::DESTBLEND];

            // BOTHSRCALPHA and BOTHINVSRCALPHA set both factors
            if (source_blend == 12)
            {
                source_blend = 5;
                destination_blend = 6;
            }
            else if (source_blend == 13)
            {
                source_blend = 6;
                destination_blend = 5;
            }

            target.BlendEnable = render[rs::ALPHABLENDENABLE] != 0;
            target.SrcBlend = ToBlend( source_blend, false );
            target.DestBlend = ToBlend( destination_blend, false );
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = ToBlend( source_blend, true );
            target.DestBlendAlpha = ToBlend( destination_blend, true );
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            m_Device->CreateBlendState( &desc, blend.Put() );
        }

        const float factor[4] = {};
        m_Context->OMSetBlendState( blend.Get(), factor, 0xFFFFFFFF );

        // Shore pass (B15): stencil only, no colour.
        if (m_ShorePass)
        {
            if (!m_ReflNoColour)
            {
                D3D11_BLEND_DESC desc = {};
                desc.RenderTarget[0].RenderTargetWriteMask = 0;
                m_Device->CreateBlendState( &desc, m_ReflNoColour.Put() );
            }
            m_Context->OMSetBlendState( m_ReflNoColour.Get(), factor, 0xFFFFFFFF );

            // 'ReflectionDebug = 2': the marked shore part in green.
            if (m_ConfigReflectionDebug == 2)
            {
                if (!m_ReflShoreDebug)
                {
                    D3D11_BLEND_DESC desc = {};
                    desc.RenderTarget[0].BlendEnable = TRUE;
                    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
                    desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
                    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
                    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                    m_Device->CreateBlendState( &desc, m_ReflShoreDebug.Put() );
                }
                const float green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
                m_Context->OMSetBlendState( m_ReflShoreDebug.Get(), green, 0xFFFFFFFF );
            }
        }

        // Depth and stencil.
        bool depth_enable = render[rs::ZENABLE] != 0;
        bool depth_write = render[rs::ZWRITEENABLE] != 0 && m_EdgePass != 2;
        std::uint32_t depth_func = render[rs::ZFUNC];

        // '[Dx11] TestDraw' (BUGS.md B21): opaque triangles drawn without full depth test and write.
        if (m_ConfigTestDraw > 0 && m_BatchPrimitive == Primitive::TRIANGLES && render[rs::ALPHABLENDENABLE] == 0 && m_EdgePass == 0 &&
            !m_BackPass && (!depth_enable || !depth_write || depth_func == 8))
        {
            const std::uint32_t hash = m_Bound[0] != nullptr ? m_Bound[0]->m_Hash : 0;
            const std::uint64_t key = static_cast<std::uint64_t>(hash) | (static_cast<std::uint64_t>(depth_enable) << 32) |
                (static_cast<std::uint64_t>(depth_write) << 33) | (static_cast<std::uint64_t>(depth_func & 15) << 34);

            if (m_LoggedNoDepth.size() < 200 && m_LoggedNoDepth.insert( key ).second)
                Log( "B21 opaque draw without depth: texture %08X z %d zwrite %d zfunc %u, %u triangles", hash, depth_enable ? 1 : 0,
                    depth_write ? 1 : 0, depth_func, static_cast<unsigned>(m_BatchIndices.size() / 3) );

            if (m_ConfigTestDraw == 1)
            {
                depth_enable = true;
                depth_write = true;
                depth_func = 4; // LESSEQUAL
            }
        }

        // Water reflections (B15): inside the render queue the water writes stencil bit 1, every solid draw in front
        // of it clears it again, so the reflection only lands on visible water.
        int water_stencil = 0;
        if (m_ConfigReflections && m_QueueList > 0 && m_MirrorVs && m_ReflCompositePs && render[rs::STENCILENABLE] == 0 && depth_enable &&
            m_BatchPrimitive == Primitive::TRIANGLES)
        {
            if (IsWaterTexture() || m_ShorePass)
                water_stencil = 2;
            // Only solid things in front of the water: opaque, or blended while writing depth (boat, cars).
            // Overlays without depth writing (shore fade, shadows) keep the mask, they lie on the water.
            else if (render[rs::ALPHABLENDENABLE] == 0 || render[rs::ZWRITEENABLE] != 0)
                water_stencil = 1;
        }

        const std::uint64_t depth_key = (static_cast<std::uint64_t>(water_stencil) << 40) |
            static_cast<std::uint64_t>(depth_enable) | (static_cast<std::uint64_t>(depth_write) << 1) |
            (static_cast<std::uint64_t>(depth_func & 15) << 2) |
            (static_cast<std::uint64_t>(render[rs::STENCILENABLE] != 0) << 6) |
            (static_cast<std::uint64_t>(render[rs::STENCILFUNC] & 15) << 7) |
            (static_cast<std::uint64_t>(render[rs::STENCILFAIL] & 15) << 11) |
            (static_cast<std::uint64_t>(render[rs::STENCILZFAIL] & 15) << 15) |
            (static_cast<std::uint64_t>(render[rs::STENCILPASS] & 15) << 19) |
            (static_cast<std::uint64_t>(render[rs::STENCILMASK] & 0xFF) << 23) |
            (static_cast<std::uint64_t>(render[rs::STENCILWRITEMASK] & 0xFF) << 31);

        auto & depth = m_DepthStates[depth_key];

        if (!depth)
        {
            D3D11_DEPTH_STENCIL_DESC desc = {};
            desc.DepthEnable = depth_enable;
            desc.DepthWriteMask = depth_enable && depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = ToCompare( depth_func );
            desc.StencilEnable = render[rs::STENCILENABLE] != 0;
            desc.StencilReadMask = static_cast<UINT8>(render[rs::STENCILMASK] & 0xFF);
            desc.StencilWriteMask = static_cast<UINT8>(render[rs::STENCILWRITEMASK] & 0xFF);
            desc.FrontFace.StencilFunc = ToCompare( render[rs::STENCILFUNC] );
            desc.FrontFace.StencilFailOp = ToStencilOp( render[rs::STENCILFAIL] );
            desc.FrontFace.StencilDepthFailOp = ToStencilOp( render[rs::STENCILZFAIL] );
            desc.FrontFace.StencilPassOp = ToStencilOp( render[rs::STENCILPASS] );
            if (water_stencil != 0)
            {
                // Water sets bit 1; solid draws clear it and set bit 2, so stencil 0 stays where only the sky is.
                // Solid draws without depth writing (B21) also set bit 3: the depth there is of what lies behind.
                desc.StencilEnable = TRUE;
                desc.StencilReadMask = 1;
                desc.StencilWriteMask = water_stencil == 1 ? 7 : 1;
                desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
            }
            desc.BackFace = desc.FrontFace;
            m_Device->CreateDepthStencilState( &desc, depth.Put() );
        }

        m_Context->OMSetDepthStencilState( depth.Get(), water_stencil != 0 ? (water_stencil == 2 ? 1u : (depth_write ? 2u : 6u)) : render[rs::STENCILREF] & 0xFF );

        // Rasterizer, D3D7 culls by screen winding like D3D11 with clockwise front faces.
        // Projected headlight overlays share depth with the underlying road geometry (upstream caaa305).
        const bool projected_candidate = !m_BackPass && render[rs::ZENABLE] != 0 &&
            render[rs::ZWRITEENABLE] != 0 && render[rs::ALPHABLENDENABLE] != 0 &&
            render[rs::ZFUNC] == 4 && render[rs::STENCILENABLE] == 0 &&
            m_Bound[0] != nullptr && m_Bound[1] == nullptr &&
            m_Bound[0]->m_Width == 64 && m_Bound[0]->m_Height == 64;
        std::uint32_t cull_mode = render[rs::CULLMODE];

        if (m_BackPass)
        {
            // Keep only the faces the game culls.
            cull_mode = cull_mode == 2 ? 3u : 2u;
        }

        const bool wireframe = render[rs::FILLMODE] == 2 || m_ConfigTestDraw == 2;
        const std::uint64_t raster_key = static_cast<std::uint64_t>(cull_mode & 3) |
            (static_cast<std::uint64_t>(wireframe) << 2) | (static_cast<std::uint64_t>(m_BackPass) << 3) |
            (static_cast<std::uint64_t>(projected_candidate) << 4);

        auto & raster = m_Rasters[raster_key];

        if (!raster)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
            const std::uint32_t cull = cull_mode;
            desc.CullMode = cull == 2 ? D3D11_CULL_FRONT : (cull == 3 ? D3D11_CULL_BACK : D3D11_CULL_NONE);
            desc.FrontCounterClockwise = FALSE;
            desc.DepthClipEnable = TRUE;
            desc.DepthBias = projected_candidate ? -8 : 0;
            desc.ScissorEnable = TRUE;

            if (m_BackPass)
            {
                // Behind a coplanar front face of the same depth, about 4e-6 plus the slope.
                desc.DepthBias = 64;
                desc.SlopeScaledDepthBias = 1.0f;
            }

            m_Device->CreateRasterizerState( &desc, raster.Put() );
        }

        m_Context->RSSetState( raster.Get() );

        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        const D3D11_RECT scissor = ScaledClip();
        m_Context->RSSetScissorRects( 1, &scissor );

        ID3D11RenderTargetView * target = m_FrameView.Get();
        m_Context->OMSetRenderTargets( 1, &target, m_DepthView.Get() );

        m_Context->IASetInputLayout( m_SceneLayout.Get() );
        UINT stride = sizeof( TlVertex );
        UINT offset = 0;
        ID3D11Buffer * vertex_buffer = m_VertexBuffer.Get();
        m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
        m_Context->IASetIndexBuffer( m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
        m_Context->VSSetShader( m_SceneVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_ScenePs.Get(), nullptr, 0 );
        ID3D11Buffer * constant_buffer = m_DrawConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &constant_buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &constant_buffer );

        ID3D11ShaderResourceView * views[MAX_STAGES] = {};
        ID3D11SamplerState * samplers[MAX_STAGES] = {};

        for (int stage = 0; stage < MAX_STAGES; ++stage)
        {
            if (m_Bound[static_cast<std::size_t>( stage )] != nullptr)
            {
                views[stage] = m_Bound[static_cast<std::size_t>( stage )]->m_View.Get();
            }

            samplers[stage] = GetSampler( stage );
        }

        m_Context->PSSetShaderResources( 0, MAX_STAGES, views );
        m_Context->PSSetSamplers( 0, MAX_STAGES, samplers );

        // Headlight map at t2 / s2, '[Dx11] Headlights'.
        ID3D11ShaderResourceView * head_view = m_HeadMapView.Get();
        ID3D11SamplerState * head_sampler = m_HeadSampler.Get();
        m_Context->PSSetShaderResources( 2, 1, &head_view );
        m_Context->PSSetSamplers( 2, 1, &head_sampler );
    }


    // Lines are one pixel wide at any resolution, while the game spaces them for 640x480 scaled up: the
    // damage gauge's gradient, drawn as stacked lines, falls apart into thin stripes (BUGS.md B18). The same
    // happens when the frame is rendered larger than the game's. Each line becomes a quad across its minor
    // axis, as wide as one pixel of 640x480.
    void Driver::WidenLines()
    {
        const float width = std::max( 1.0f, static_cast<float>(m_Height) / 480.0f );

        if (width <= 1.0f && m_ScaleX <= 1.0f && m_ScaleY <= 1.0f)
            return;

        const std::size_t lines = m_BatchIndices.size() / 2;

        if (lines * 4 > std::min<std::size_t>( RAW_VERTICES, 0xFFFF ) || lines * 6 > RAW_INDICES)
            return;

        // Wound so the game's cull mode keeps the quad: 2 removes clockwise, 3 counter-clockwise (y down).
        const std::uint32_t cull = m_D3D.m_Render[rs::CULLMODE];
        std::vector<TlVertex> vertices;
        std::vector<std::uint16_t> indices;
        vertices.reserve( lines * 4 );
        indices.reserve( lines * 6 );

        for (std::size_t i = 0; i < lines; ++i)
        {
            const TlVertex & a = m_BatchVertices[m_BatchIndices[i * 2]];
            const TlVertex & b = m_BatchVertices[m_BatchIndices[i * 2 + 1]];
            const bool flat = std::fabs( b.m_X - a.m_X ) >= std::fabs( b.m_Y - a.m_Y );
            const float dx = flat ? 0.0f : 0.5f * width;
            const float dy = flat ? 0.5f * width : 0.0f;

            const auto base = static_cast<std::uint16_t>(vertices.size());
            TlVertex corner = a;
            corner.m_X -= dx;
            corner.m_Y -= dy;
            vertices.push_back( corner );
            corner.m_X += 2.0f * dx;
            corner.m_Y += 2.0f * dy;
            vertices.push_back( corner );
            corner = b;
            corner.m_X -= dx;
            corner.m_Y -= dy;
            vertices.push_back( corner );
            corner.m_X += 2.0f * dx;
            corner.m_Y += 2.0f * dy;
            vertices.push_back( corner );

            const TlVertex * v = &vertices[base];
            const float winding = (v[1].m_X - v[0].m_X) * (v[2].m_Y - v[0].m_Y) - (v[1].m_Y - v[0].m_Y) * (v[2].m_X - v[0].m_X);
            const bool reverse = (cull == 2 && winding > 0.0f) || (cull == 3 && winding < 0.0f);

            static constexpr int ORDER[2][6] = { { 0, 1, 2, 2, 1, 3 }, { 0, 2, 1, 2, 3, 1 } };

            for (const int k : ORDER[reverse ? 1 : 0])
                indices.push_back( static_cast<std::uint16_t>(base + k) );
        }

        m_BatchVertices.assign( vertices.begin(), vertices.end() );
        m_BatchIndices.assign( indices.begin(), indices.end() );
        m_BatchPrimitive = Primitive::TRIANGLES;
    }


    void Driver::Flush()
    {
        if (m_BatchIndices.empty() || m_BatchVertices.empty() || !m_FrameView)
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            return;
        }

        if (m_BatchPrimitive == Primitive::LINES)
            WidenLines();

        m_MeasureSuspect = false;

        if (m_ProbeOn)
        {
            ProbeBatch();
        }

        if (m_ConfigMeasure > 0)
        {
            MeasureDrawCall();
            MeasurePassCall();

            if (m_MeasureSnapActive)
            {
                MeasureSnapBatch();
            }
        }

        // Blending while writing depth, see '[Dx11] AlphaEdgeFix'.
        const auto & render = m_D3D.m_Render;

        // Water reflections (B15): the game's baked reflections are the alpha blended draws of queue list 1.
        const bool reflections = m_ConfigReflections && m_QueueList > 0 && m_MirrorVs && m_ReflCompositePs &&
            m_BatchPrimitive == Primitive::TRIANGLES;
        // Baked reflections are listed by texture; most come in queue list 1, some (AE995C5F) in list 3.
        if (reflections && m_ConfigRemoveBaked && render[rs::ALPHABLENDENABLE] != 0 && IsBakedTexture())
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            m_BatchPrimitive = Primitive::NONE;
            return;
        }

        const bool water = reflections && IsWaterTexture();
        // '[Water] WaterEvenColour': the game lights the water near the player brighter than further out (a circle
        // around the car). All water vertices get one colour, the running mean of the water drawn so far.
        if (water && m_ConfigWaterEvenColour && !m_BatchVertices.empty())
        {
            double sum[3] = {};
            for (const TlVertex & v : m_BatchVertices)
                for (int c = 0; c < 3; ++c)
                    sum[c] += (v.m_Diffuse >> (16 - c * 8)) & 0xFF;
            const double n = static_cast<double>(m_BatchVertices.size());
            for (int c = 0; c < 3; ++c)
                m_WaterColour[c] = m_WaterColour[c] < 0.0f ? static_cast<float>(sum[c] / n)
                    : m_WaterColour[c] * 0.98f + static_cast<float>(sum[c] / n) * 0.02f;
            const std::uint32_t rgb = (static_cast<std::uint32_t>(std::lround( m_WaterColour[0] )) << 16) |
                (static_cast<std::uint32_t>(std::lround( m_WaterColour[1] )) << 8) | static_cast<std::uint32_t>(std::lround( m_WaterColour[2] ));
            for (TlVertex & v : m_BatchVertices)
                v.m_Diffuse = (v.m_Diffuse & 0xFF000000u) | rgb;
        }

        // The plane is fitted to the water itself; the far sea of some tracks is not flat (B15, jumps).
        if (water && !IsFarWaterTexture())
        {
            constexpr std::size_t MAX_WATER = 3 * 8192;
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size() && m_ReflWater.size() + 3 <= MAX_WATER; i += 3)
            {
                bool valid = true;
                std::array<std::array<float, 3>, 3> corners = {};
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const TlVertex & v = m_BatchVertices[m_BatchIndices[i + k]];
                    valid = valid && v.m_Rhw > 0.0f && v.m_Rhw < 1e30f;
                    corners[k] = { v.m_X, v.m_Y, valid ? 1.0f / v.m_Rhw : 0.0f };
                }
                if (valid)
                {
                    m_ReflWater.insert( m_ReflWater.end(), corners.begin(), corners.end() );
                    m_ReflWaterHash.push_back( m_Bound[0] != nullptr ? m_Bound[0]->m_Hash : 0 );
                }
            }
        }

        // Kept for the mirrored pass: depth tested, opaque or normally blended (no additive lights, no water).
        const std::uint32_t capture_source = render[rs::SRCBLEND];
        const std::uint32_t capture_destination = render[rs::DESTBLEND];
        m_ReflCapture = reflections && !water && render[rs::ZENABLE] != 0 &&
            (render[rs::ALPHABLENDENABLE] == 0 || capture_source == 12 || (capture_source == 5 && capture_destination == 6));

        // The sky comes before the first queue run of the frame (list 0); kept as well and put in front of
        // every reflection of the frame, so water reflecting the sky is not left empty.
        if (m_ReflSkyPhase && m_ConfigReflections && m_QueueList == 0 && m_MirrorVs && m_ReflCompositePs &&
            m_BatchPrimitive == Primitive::TRIANGLES &&
            (render[rs::ALPHABLENDENABLE] == 0 || capture_source == 12 || (capture_source == 5 && capture_destination == 6)))
            m_ReflCapture = true;
        // Only with alpha test on, as the world is drawn. Without it the front end writes depth
        // with fully transparent quads on purpose, the profile list is masked that way (B10).
        const bool edge_split = m_ConfigAlphaEdgeFix && m_BatchPrimitive == Primitive::TRIANGLES &&
            render[rs::ALPHABLENDENABLE] != 0 && render[rs::ALPHATESTENABLE] != 0 && render[rs::ZENABLE] != 0 &&
            render[rs::ZWRITEENABLE] != 0 && BatchTranslucent();

        m_EdgePass = edge_split ? 1 : 0;

        if (m_ConfigMeasure > 0)
        {
            MeasureUvRange();
        }

        ApplyPipeline();

        if (m_ReflCapture)
        {
            m_ReflCapture = false;
            CaptureMirrorBatch();
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED( m_Context->Map( m_VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, m_BatchVertices.data(), m_BatchVertices.size() * sizeof( TlVertex ) );
            m_Context->Unmap( m_VertexBuffer.Get(), 0 );
        }
        if (SUCCEEDED( m_Context->Map( m_IndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            std::memcpy( mapped.pData, m_BatchIndices.data(), m_BatchIndices.size() * sizeof( std::uint16_t ) );
            m_Context->Unmap( m_IndexBuffer.Get(), 0 );
        }

        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        if (m_BatchPrimitive == Primitive::LINES)
        {
            topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        }
        else if (m_BatchPrimitive == Primitive::POINTS)
        {
            topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        }

        m_Context->IASetPrimitiveTopology( topology );
        m_Context->DrawIndexed( static_cast<UINT>(m_BatchIndices.size()), 0, 0 );

        if (edge_split)
        {
            m_EdgePass = 2;
            ApplyPipeline();
            m_Context->DrawIndexed( static_cast<UINT>(m_BatchIndices.size()), 0, 0 );
        }

        // Water reflections (B15): the water part of a shore texture (alpha below the edge reference) joins the
        // water mask, drawn again without colour; the edge pass 2 of the shader keeps only that part.
        if (m_ConfigReflections && m_QueueList > 0 && m_MirrorVs && m_ReflCompositePs && m_BatchPrimitive == Primitive::TRIANGLES &&
            render[rs::ZENABLE] != 0 && m_Bound[0] != nullptr &&
            std::find( m_ConfigShoreTextures.begin(), m_ConfigShoreTextures.end(), m_Bound[0]->m_Hash ) != m_ConfigShoreTextures.end())
        {
            // The whole shore joins the mask; how much it mirrors comes from its transparency (weight target),
            // so the reflection fades in with the shore instead of starting at a hard line.
            m_ShorePass = true;
            m_EdgePass = m_ReflWeightRtv ? 0 : 2;
            ++m_ReflShoreDraws;
            ApplyPipeline();
            m_Context->DrawIndexed( static_cast<UINT>(m_BatchIndices.size()), 0, 0 );
            m_ShorePass = false;

            if (m_ReflWeightRtv && m_ReflWeightBlend)
            {
                ID3D11RenderTargetView * weight = m_ReflWeightRtv.Get();
                m_Context->OMSetRenderTargets( 1, &weight, nullptr );
                const float factor[4] = {};
                m_Context->OMSetBlendState( m_ReflWeightBlend.Get(), factor, 0xFFFFFFFF );
                m_Context->DrawIndexed( static_cast<UINT>(m_BatchIndices.size()), 0, 0 );
                ID3D11RenderTargetView * frame = m_FrameView.Get();
                m_Context->OMSetRenderTargets( 1, &frame, m_DepthView.Get() );
            }
        }

        // '[Dx11] ForceTwoSided', the faces the game culls, pushed back.
        const std::uint32_t game_cull = render[rs::CULLMODE];

        // Opaque textures only. Cut out shapes such as signs come with their own back face,
        // whose cut is a little smaller, so the mirrored front would show as a coloured rim.
        const bool cut_out = (m_Bound[0] != nullptr && m_Bound[0]->m_Translucent) || (m_Bound[1] != nullptr && m_Bound[1]->m_Translucent);

        if (m_ConfigForceTwoSided && !cut_out && m_BatchPrimitive == Primitive::TRIANGLES && (game_cull == 2 || game_cull == 3))
        {
            m_BackPass = true;
            m_EdgePass = 0;
            ApplyPipeline();
            m_Context->DrawIndexed( static_cast<UINT>(m_BatchIndices.size()), 0, 0 );
            m_BackPass = false;
        }

        m_EdgePass = 0;

        m_BatchVertices.clear();
        m_BatchIndices.clear();
        m_BatchPrimitive = Primitive::NONE;
    }

    void Driver::MeasureUvRange()
    {
        const Texture * texture = m_Bound[0];

        if (texture == nullptr || !texture->m_Translucent || std::max( texture->m_Width, texture->m_Height ) < 128 ||
            m_BatchPrimitive != Primitive::TRIANGLES)
            return;

        auto & uv = m_MeasureUv[texture];

        for (const TlVertex & vertex : m_BatchVertices)
        {
            uv.m_MinU = std::min( uv.m_MinU, vertex.m_U0 );
            uv.m_MaxU = std::max( uv.m_MaxU, vertex.m_U0 );
            uv.m_MinV = std::min( uv.m_MinV, vertex.m_V0 );
            uv.m_MaxV = std::max( uv.m_MaxV, vertex.m_V0 );
        }

        ++uv.m_Draws;
    }

    bool Driver::BatchTranslucent() const
    {
        if (m_Bound[0] != nullptr && m_Bound[0]->m_Translucent)
            return true;
        if (m_Bound[1] != nullptr && m_Bound[1]->m_Translucent)
            return true;

        // Faded geometry carries its alpha in the vertex colour.
        return std::any_of( m_BatchVertices.begin(), m_BatchVertices.end(), [] ( const TlVertex & vertex )
        {
            return (vertex.m_Diffuse >> 24) != 0xFF;
        } );
    }

    // Measuring, PLAN.md 3T.1.

    void Driver::MeasureDrawCall()
    {
        const Texture * texture = m_Bound[0];

        if (texture == nullptr)
            return;

        const auto & render = m_D3D.m_Render;
        const auto & stage = m_D3D.m_Stage[0];

        const bool blend = render[rs::ALPHABLENDENABLE] != 0;
        const bool z = render[rs::ZENABLE] != 0;
        const bool z_write = render[rs::ZWRITEENABLE] != 0;
        const bool alpha_test = render[rs::ALPHATESTENABLE] != 0;

        // Blending into the frame while writing depth, the H-B4c pattern of doc/BUGS.md 8.1.
        m_MeasureSuspect = blend && z && z_write;

        const std::uint64_t key = static_cast<std::uint64_t>(texture->m_Format & 0xFF) |
            (static_cast<std::uint64_t>(blend) << 8) |
            (static_cast<std::uint64_t>(render[rs::SRCBLEND] & 15) << 9) |
            (static_cast<std::uint64_t>(render[rs::DESTBLEND] & 15) << 13) |
            (static_cast<std::uint64_t>(z) << 17) |
            (static_cast<std::uint64_t>(z_write) << 18) |
            (static_cast<std::uint64_t>(render[rs::ZFUNC] & 15) << 19) |
            (static_cast<std::uint64_t>(alpha_test) << 23) |
            (static_cast<std::uint64_t>(render[rs::ALPHAFUNC] & 15) << 24) |
            (static_cast<std::uint64_t>(render[rs::ALPHAREF] & 0xFF) << 28) |
            (static_cast<std::uint64_t>(stage[tss::COLOROP] & 31) << 36) |
            (static_cast<std::uint64_t>(stage[tss::ALPHAOP] & 31) << 41) |
            (static_cast<std::uint64_t>(m_Bound[1] != nullptr) << 46);

        auto & entry = m_MeasureDraws[key];

        if (entry.m_Id == 0)
        {
            entry.m_Id = static_cast<int>(m_MeasureDraws.size());
            Log( "M3T draw #%d %s blend %d src %u dst %u z %d zwrite %d zfunc %u atest %d afunc %u aref %u colorop %u alphaop %u stage1 %d%s",
                entry.m_Id, FormatName( texture->m_Format ), blend ? 1 : 0, render[rs::SRCBLEND], render[rs::DESTBLEND], z ? 1 : 0, z_write ? 1 : 0,
                render[rs::ZFUNC], alpha_test ? 1 : 0, render[rs::ALPHAFUNC], render[rs::ALPHAREF] & 0xFF, stage[tss::COLOROP], stage[tss::ALPHAOP],
                m_Bound[1] != nullptr ? 1 : 0, m_MeasureSuspect ? " SUSPECT" : "" );
        }

        ++entry.m_Draws;
        entry.m_Primitives += m_BatchPrimitive == Primitive::TRIANGLES ? m_BatchIndices.size() / 3 :
            (m_BatchPrimitive == Primitive::LINES ? m_BatchIndices.size() / 2 : m_BatchIndices.size());
    }


    void Driver::MeasureReport(
        const char * reason
    )
    {
        Log( "M3T report (%s) at flip %u, %.1f s: %zu draw states, %zu texture shapes, %d dynamic textures, %zu textures alive",
            reason, m_FlipCount, MeasureSeconds(), m_MeasureDraws.size(), m_MeasureAllocs.size(), m_MeasureDynamic, m_Textures.size() );

        // Memory, PLAN.md 2.0b. Address space is what a 32 bit process runs out of first.
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        counters.cb = sizeof( counters );
        MEMORYSTATUSEX status = {};
        status.dwLength = sizeof( status );

        if (::GetProcessMemoryInfo( ::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof( counters ) ) &&
            ::GlobalMemoryStatusEx( &status ))
        {
            constexpr double MB = 1024.0 * 1024.0;
            Log( "M3T   memory: address space %.0f of %.0f MB, private %.0f MB (peak %.0f), working set %.0f MB (peak %.0f), textures %.1f MB",
                static_cast<double>(status.ullTotalVirtual - status.ullAvailVirtual) / MB, static_cast<double>(status.ullTotalVirtual) / MB,
                static_cast<double>(counters.PrivateUsage) / MB, static_cast<double>(counters.PeakPagefileUsage) / MB,
                static_cast<double>(counters.WorkingSetSize) / MB, static_cast<double>(counters.PeakWorkingSetSize) / MB,
                static_cast<double>(m_MeasureTextureBytes) / MB );
        }

        std::vector<const MeasureDraw *> draws;

        for (const auto & [key, entry] : m_MeasureDraws)
        {
            if (entry.m_Draws > 0)
            {
                draws.push_back( &entry );
            }
        }

        std::sort( draws.begin(), draws.end(), [] ( const MeasureDraw * a, const MeasureDraw * b )
        {
            return a->m_Id < b->m_Id;
        } );

        for (const MeasureDraw * entry : draws)
        {
            Log( "M3T   draw #%d: %llu batches, %llu primitives", entry->m_Id, static_cast<unsigned long long>(entry->m_Draws),
                static_cast<unsigned long long>(entry->m_Primitives) );
        }

        // Translucent textures from 128 up, with the coordinate range they were drawn with.
        for (const auto & [texture, uv] : m_MeasureUv)
        {
            if (uv.m_Draws == 0)
                continue;

            Log( "M3T   uv tex %08X %dx%d %s: u %.3f..%.3f v %.3f..%.3f, %llu batches",
                texture->m_Hash, texture->m_Width, texture->m_Height, FormatName( texture->m_Format ),
                uv.m_MinU, uv.m_MaxU, uv.m_MinV, uv.m_MaxV, static_cast<unsigned long long>(uv.m_Draws) );
        }

        std::unordered_map<int, std::uint64_t> per_format;

        for (const auto & [key, count] : m_MeasureAllocs)
        {
            per_format[static_cast<int>(key >> 48)] += count;
        }

        for (const auto & [format, count] : per_format)
        {
            Log( "M3T   talloc %s: %llu", FormatName( format ), static_cast<unsigned long long>(count) );
        }

        // Light passes, sorted by id like the draw states.
        std::vector<const MeasurePass *> passes;

        for (const auto & [key, pass] : m_MeasurePasses)
        {
            if (pass.m_Draws > 0)
            {
                passes.push_back( &pass );
            }
        }

        std::sort( passes.begin(), passes.end(), [] ( const MeasurePass * a, const MeasurePass * b )
        {
            return a->m_Id < b->m_Id;
        } );

        for (const MeasurePass * pass : passes)
        {
            Log( "M3T   pass #%d: %llu batches, %llu primitives, uv0 %.3f..%.3f / %.3f..%.3f, uv1 %.3f..%.3f / %.3f..%.3f", pass->m_Id,
                static_cast<unsigned long long>(pass->m_Draws), static_cast<unsigned long long>(pass->m_Primitives),
                pass->m_MinU0, pass->m_MaxU0, pass->m_MinV0, pass->m_MaxV0, pass->m_MinU1, pass->m_MaxU1, pass->m_MinV1, pass->m_MaxV1 );
        }

        if (m_MeasureColourVertices > 0)
        {
            const double n = static_cast<double>(m_MeasureColourVertices);
            Log( "M3T   vertex colour of opaque world draws: diffuse %.1f %.1f %.1f, specular %.1f %.1f %.1f (RGB 0-255, %llu vertices)",
                m_MeasureColour[0] / n, m_MeasureColour[1] / n, m_MeasureColour[2] / n, m_MeasureColour[3] / n, m_MeasureColour[4] / n,
                m_MeasureColour[5] / n, static_cast<unsigned long long>(m_MeasureColourVertices) );
        }

        // Counts restart, the ids stay so the log reads across reports.
        for (auto & [key, entry] : m_MeasureDraws)
        {
            entry.m_Draws = 0;
            entry.m_Primitives = 0;
        }

        for (auto & [key, pass] : m_MeasurePasses)
        {
            const int id = pass.m_Id;
            pass = {};
            pass.m_Id = id;
        }

        m_MeasureColour = {};
        m_MeasureColourVertices = 0;

        if (m_ConfigHeadlights > 0 && m_ConfigHeadlightsObjects > 0)
        {
            const ObjectLightStats & o = m_ObjectStats;
            Log( "M3T   object lights: batches track %llu, objects lit %llu, not lit: no track light yet %llu, depth off %llu, blend %llu",
                static_cast<unsigned long long>(o.m_Track), static_cast<unsigned long long>(o.m_Lit), static_cast<unsigned long long>(o.m_NoState),
                static_cast<unsigned long long>(o.m_NoDepth), static_cast<unsigned long long>(o.m_Blend) );
            m_ObjectStats = {};
        }

        if (m_ConfigHeadlights > 0 && m_ConfigHeadlightsDay > 0)
        {
            const DayStats & d = m_DayStats;
            Log( "M3T   day lights: %llu track draws (night %llu, no car %llu, switch off %llu), mesh setups (prepared %llu, failed %llu), "
                "%llu mesh draws (%llu lit), night flag %u, player %u, light bytes %u %u",
                static_cast<unsigned long long>(d.m_Calls), static_cast<unsigned long long>(d.m_Night), static_cast<unsigned long long>(d.m_NoCar),
                static_cast<unsigned long long>(d.m_SwitchOff), static_cast<unsigned long long>(d.m_Prepared),
                static_cast<unsigned long long>(d.m_Failed), static_cast<unsigned long long>(d.m_Draws),
                static_cast<unsigned long long>(d.m_LitDraws), d.m_NightFlag, d.m_Index, d.m_Lights[0], d.m_Lights[1] );
            m_DayStats = {};
        }
    }


    void Driver::MeasurePassCall()
    {
        const auto & render = m_D3D.m_Render;
        const auto & stage0 = m_D3D.m_Stage[0];
        const auto & stage1 = m_D3D.m_Stage[1];

        const bool blend = render[rs::ALPHABLENDENABLE] != 0;
        const std::uint32_t src = render[rs::SRCBLEND];
        const std::uint32_t dst = render[rs::DESTBLEND];

        // Vertex colours of the opaque world, where baked or computed vertex lighting shows up.
        if (m_BatchPrimitive == Primitive::TRIANGLES && render[rs::ZENABLE] != 0 && render[rs::ZWRITEENABLE] != 0 &&
            (!blend || render[rs::ALPHATESTENABLE] != 0))
        {
            for (const TlVertex & vertex : m_BatchVertices)
            {
                m_MeasureColour[0] += (vertex.m_Diffuse >> 16) & 0xFF;
                m_MeasureColour[1] += (vertex.m_Diffuse >> 8) & 0xFF;
                m_MeasureColour[2] += vertex.m_Diffuse & 0xFF;
                m_MeasureColour[3] += (vertex.m_Specular >> 16) & 0xFF;
                m_MeasureColour[4] += (vertex.m_Specular >> 8) & 0xFF;
                m_MeasureColour[5] += vertex.m_Specular & 0xFF;
            }

            m_MeasureColourVertices += m_BatchVertices.size();
        }

        // D3D7 blend factors: 1 zero, 2 one, 3 src colour, 4 inv src colour, 9 dest colour, 10 inv dest colour.
        const bool second_stage = m_Bound[1] != nullptr;
        const bool light_blend = blend && (src == 9 || src == 10 || dst == 2 || dst == 3 || dst == 4);

        if (!second_stage && !light_blend)
            return;

        const Texture * tex0 = m_Bound[0];
        const Texture * tex1 = m_Bound[1];

        char key[256];
        // By content hash, so a pass keeps its id when the game reloads its textures.
        std::snprintf( key, sizeof( key ), "%08X %08X %u %u %u %u %u %u %u %u %d %u %u %d %d %d",
            tex0 != nullptr ? tex0->m_Hash : 0u, tex1 != nullptr ? tex1->m_Hash : 0u, stage0[tss::COLOROP],
            stage0[tss::COLORARG1], stage0[tss::COLORARG2], stage1[tss::COLOROP], stage1[tss::COLORARG1], stage1[tss::COLORARG2],
            stage1[tss::ALPHAOP], stage1[tss::TEXCOORDINDEX], blend ? 1 : 0, src, dst, render[rs::ZENABLE] != 0 ? 1 : 0,
            render[rs::ZWRITEENABLE] != 0 ? 1 : 0, static_cast<int>(m_BatchPrimitive) );

        auto & pass = m_MeasurePasses[key];

        if (pass.m_Id == 0)
        {
            pass.m_Id = static_cast<int>(m_MeasurePasses.size());

            const auto describe = [] ( const Texture * texture, char * text, std::size_t size )
            {
                if (texture == nullptr)
                {
                    std::snprintf( text, size, "none" );
                }
                else
                {
                    std::snprintf( text, size, "%08X %dx%d %s alloc-stage %d%s", texture->m_Hash, texture->m_Width, texture->m_Height,
                        FormatName( texture->m_Format ), texture->m_Stage, texture->m_Updates > 1 ? " dynamic" : "" );
                }
            };

            char text0[96];
            char text1[96];
            describe( tex0, text0, sizeof( text0 ) );
            describe( tex1, text1, sizeof( text1 ) );

            Log( "M3T pass #%d at flip %u, %.1f s: tex0 %s, tex1 %s, stage0 op %u arg %u/%u, stage1 op %u arg %u/%u alphaop %u texcoord %u, "
                "blend %d src %u dst %u z %d zwrite %d",
                pass.m_Id, m_FlipCount, MeasureSeconds(), text0, text1, stage0[tss::COLOROP], stage0[tss::COLORARG1], stage0[tss::COLORARG2],
                stage1[tss::COLOROP], stage1[tss::COLORARG1], stage1[tss::COLORARG2], stage1[tss::ALPHAOP], stage1[tss::TEXCOORDINDEX],
                blend ? 1 : 0, src, dst, render[rs::ZENABLE] != 0 ? 1 : 0, render[rs::ZWRITEENABLE] != 0 ? 1 : 0 );

            MeasureDump( tex0 );
            MeasureDump( tex1 );
        }

        for (const TlVertex & vertex : m_BatchVertices)
        {
            pass.m_MinU0 = std::min( pass.m_MinU0, vertex.m_U0 );
            pass.m_MaxU0 = std::max( pass.m_MaxU0, vertex.m_U0 );
            pass.m_MinV0 = std::min( pass.m_MinV0, vertex.m_V0 );
            pass.m_MaxV0 = std::max( pass.m_MaxV0, vertex.m_V0 );
            pass.m_MinU1 = std::min( pass.m_MinU1, vertex.m_U1 );
            pass.m_MaxU1 = std::max( pass.m_MaxU1, vertex.m_U1 );
            pass.m_MinV1 = std::min( pass.m_MinV1, vertex.m_V1 );
            pass.m_MaxV1 = std::max( pass.m_MaxV1, vertex.m_V1 );
        }

        ++pass.m_Draws;
        pass.m_Primitives += m_BatchPrimitive == Primitive::TRIANGLES ? m_BatchIndices.size() / 3 :
            (m_BatchPrimitive == Primitive::LINES ? m_BatchIndices.size() / 2 : m_BatchIndices.size());
    }


    namespace
    {
        // Stack values that point into the game's code right after a call instruction, the usual
        // heuristic for return addresses without frame pointers.
        bool LooksLikeReturn(
            std::uintptr_t value
        )
        {
            static std::uintptr_t exe_begin = 0, exe_end = 0, gimme_begin = 0, gimme_end = 0;

            if (exe_begin == 0)
            {
                const auto range = [] ( const char * name, std::uintptr_t & begin, std::uintptr_t & end )
                {
                    const auto * base = reinterpret_cast<const std::uint8_t *>(::GetModuleHandleA( name ));
                    if (base == nullptr)
                        return;

                    const auto * dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
                    const auto * nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
                    begin = reinterpret_cast<std::uintptr_t>(base) + nt->OptionalHeader.BaseOfCode;
                    end = begin + nt->OptionalHeader.SizeOfCode;
                };

                range( nullptr, exe_begin, exe_end );
                range( "gimme.dll", gimme_begin, gimme_end );
            }

            const bool in_code = (value >= exe_begin + 8 && value < exe_end) || (value >= gimme_begin + 8 && value < gimme_end);
            if (!in_code)
                return false;

            const auto * p = reinterpret_cast<const std::uint8_t *>(value);
            return p[-5] == 0xE8 ||                                   // call rel32
                (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) ||          // call [disp32] / [reg+disp32]
                (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50) ||          // call [reg+disp8]
                (p[-2] == 0xFF && ((p[-1] & 0xF8) == 0xD0 || (p[-1] & 0xF8) == 0x10)); // call reg / [reg]
        }


        // Address as Ghidra shows it (porsche.exe at 0x400000, gimme.dll at 0x10000000).
        void DescribeAddress(
            void * address,
            char * text,
            std::size_t size
        )
        {
            HMODULE module = nullptr;
            char path[MAX_PATH] = {};

            if (address == nullptr || !::GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                static_cast<LPCSTR>(address), &module ) || ::GetModuleFileNameA( module, path, MAX_PATH ) == 0)
            {
                std::snprintf( text, size, "%p", address );
                return;
            }

            const char * name = std::strrchr( path, '\\' );
            name = name != nullptr ? name + 1 : path;
            const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
            std::uintptr_t base = 0;

            if (_stricmp( name, "porsche.exe" ) == 0)
                base = 0x00400000;
            else if (_stricmp( name, "gimme.dll" ) == 0)
                base = 0x10000000;

            if (base != 0)
                std::snprintf( text, size, "%s:%08X", name, static_cast<unsigned>(base + rva) );
            else
                std::snprintf( text, size, "%s+%X", name, static_cast<unsigned>(rva) );
        }


        // Copies memory the game owns, false when it is not readable.
        bool SafeCopy(
            const void * source,
            void * target,
            std::size_t size
        )
        {
#ifdef _MSC_VER
            __try
            {
                std::memcpy( target, source, size );
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
#else
            SIZE_T read = 0;
            return ::ReadProcessMemory( ::GetCurrentProcess(), source, target, size, &read ) != FALSE && read == size;
#endif
        }


        // Hardware write watch (DR0) on the render thread, PLAN.md 9.4d. The handler only records,
        // the log is written at the next flip.
        struct WatchHit
        {
            std::uintptr_t m_Eip = 0;
            std::array<std::uintptr_t, 6> m_Stack = {};
            std::uint32_t m_Value = 0;
            std::uint32_t m_Count = 0;
        };

        constexpr LONG WATCH_HITS = 32;
        std::array<WatchHit, WATCH_HITS> g_WatchHits = {};
        volatile LONG g_WatchHitCount = 0;
        volatile LONG g_WatchTotal = 0;
        std::uintptr_t g_WatchAddress = 0;
        ULONGLONG g_WatchUntil = 0;
        constexpr DWORD DR7_SLOT0 = 0x000F0003;
        constexpr DWORD DR7_WRITE4 = 0x00000001 | (1u << 16) | (3u << 18);

        LONG CALLBACK WatchHandler(
            EXCEPTION_POINTERS * info
        )
        {
            CONTEXT * context = info->ContextRecord;

            if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || (context->Dr6 & 1) == 0)
                return EXCEPTION_CONTINUE_SEARCH;

            context->Dr6 = 0;
            ::InterlockedIncrement( &g_WatchTotal );

            const std::uintptr_t eip = context->Eip;
            const LONG count = g_WatchHitCount;
            bool known = false;

            for (LONG i = 0; i < count; ++i)
            {
                if (g_WatchHits[static_cast<std::size_t>(i)].m_Eip == eip)
                {
                    ++g_WatchHits[static_cast<std::size_t>(i)].m_Count;
                    known = true;
                    break;
                }
            }

            if (!known && count < WATCH_HITS)
            {
                WatchHit & hit = g_WatchHits[static_cast<std::size_t>(count)];
                hit = {};
                hit.m_Eip = eip;
                hit.m_Count = 1;
                SafeCopy( reinterpret_cast<const void *>(g_WatchAddress), &hit.m_Value, sizeof( hit.m_Value ) );

                const auto * stack = reinterpret_cast<const std::uintptr_t *>(context->Esp);
                const auto * top = reinterpret_cast<const std::uintptr_t *>(reinterpret_cast<NT_TIB *>(::NtCurrentTeb())->StackBase);
                std::size_t used = 0;

                for (std::size_t i = 0; i < 1024 && stack + i < top && used < hit.m_Stack.size(); ++i)
                {
                    if (LooksLikeReturn( stack[i] ))
                    {
                        hit.m_Stack[used++] = stack[i];
                    }
                }

                g_WatchHitCount = count + 1;
            }

            if (::GetTickCount64() > g_WatchUntil || g_WatchTotal > 100000)
            {
                context->Dr7 &= ~DR7_SLOT0;
            }

            return EXCEPTION_CONTINUE_EXECUTION;
        }


        struct WatchArm
        {
            HANDLE m_Thread = nullptr;
            std::uintptr_t m_Address = 0;
            bool m_Enable = false;
        };

        DWORD WINAPI WatchArmThread(
            void * parameter
        )
        {
            auto * arm = static_cast<WatchArm *>(parameter);

            if (::SuspendThread( arm->m_Thread ) == static_cast<DWORD>(-1))
                return 1;

            CONTEXT context = {};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;

            if (::GetThreadContext( arm->m_Thread, &context ))
            {
                context.Dr7 &= ~DR7_SLOT0;

                if (arm->m_Enable)
                {
                    context.Dr0 = arm->m_Address;
                    context.Dr7 |= DR7_WRITE4;
                }

                context.Dr6 = 0;
                ::SetThreadContext( arm->m_Thread, &context );
            }

            ::ResumeThread( arm->m_Thread );
            return 0;
        }

        // Debug registers of the calling thread can only be set from another thread.
        bool SetWatch(
            std::uintptr_t address,
            bool enable
        )
        {
            HANDLE self = nullptr;
            if (!::DuplicateHandle( ::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &self, THREAD_ALL_ACCESS, FALSE, 0 ))
                return false;

            WatchArm arm = {};
            arm.m_Thread = self;
            arm.m_Address = address & ~static_cast<std::uintptr_t>(3);
            arm.m_Enable = enable;

            HANDLE helper = ::CreateThread( nullptr, 0, WatchArmThread, &arm, 0, nullptr );
            bool done = false;

            if (helper != nullptr)
            {
                ::WaitForSingleObject( helper, 5000 );
                DWORD code = 1;
                done = ::GetExitCodeThread( helper, &code ) && code == 0;
                ::CloseHandle( helper );
            }

            ::CloseHandle( self );
            return done;
        }


        // Headlight factor FUN_00452910 (cdecl), called from the track mesh node FUN_00453110 (doc/LICHT.md 6.15).
        constexpr std::uintptr_t HEADLIGHT_CALL = 0x0045321A;
        constexpr std::uintptr_t HEADLIGHT_FACTOR = 0x00452910;

        using HeadlightFn = void( __cdecl * )(int, const float *, const float *);

        // Second caller, FUN_004535c0 (further scenery).
        constexpr std::uintptr_t HEADLIGHT_CALL_2 = 0x004536B0;

        void __cdecl HeadlightHook(
            int count,
            const float * positions,
            const float * matrix
        )
        {
            reinterpret_cast<HeadlightFn>(HEADLIGHT_FACTOR)(count, positions, matrix);
            Driver & driver = Driver::GetInstance();
            driver.NoteHeadlight( count, positions, matrix );
            driver.OnHeadlightFactor( count );
        }


        // Redirects 'call rel32' at 'site' from 'expected' to 'hook'.
        bool PatchCall(
            std::uintptr_t site,
            std::uintptr_t expected,
            const void * hook
        )
        {
            auto * call = reinterpret_cast<std::uint8_t *>(site);
            std::int32_t relative = 0;
            std::memcpy( &relative, call + 1, sizeof( relative ) );

            if (call[0] != 0xE8 || site + 5 + static_cast<std::uintptr_t>(relative) != expected)
                return false;

            DWORD old = 0;
            if (!::VirtualProtect( call, 5, PAGE_EXECUTE_READWRITE, &old ))
                return false;

            relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(hook) - (site + 5));
            std::memcpy( call + 1, &relative, sizeof( relative ) );
            ::VirtualProtect( call, 5, old, &old );
            ::FlushInstructionCache( ::GetCurrentProcess(), call, 5 );
            return true;
        }


        // World draw FUN_00543ae0, called directly from the draw node FUN_0053f620 (doc/LICHT.md 6.6).
        constexpr std::uintptr_t ROAD_DRAW_CALL = 0x0053F70F;
        constexpr std::uintptr_t ROAD_DRAW = 0x00543AE0;
        constexpr std::uint32_t ROAD_TEXTURE = 0xC4B0AEFA;

        using RoadDrawFn = void( __thiscall * )(void *, const void *, const void *, const void *, std::uint32_t, std::uint32_t, void *);

        void __fastcall RoadDrawHook(
            void * self,
            void *,
            const void * a,
            const void * b,
            const void * c,
            std::uint32_t count,
            std::uint32_t second,
            void * third
        )
        {
            Driver & driver = Driver::GetInstance();
            driver.OnBeforeMeshDraw();
            reinterpret_cast<RoadDrawFn>(ROAD_DRAW)(self, a, b, c, count, second, third);
            driver.NoteRoadDraw( self, a, b, c, count );
            driver.OnMeshDrawn( self );
        }

        // Sprite and mesh draw FUN_00545ad0, the other branch of the draw node.
        constexpr std::uintptr_t SPRITE_DRAW_CALL = 0x0053F72F;
        constexpr std::uintptr_t SPRITE_DRAW = 0x00545AD0;

        void __fastcall SpriteDrawHook(
            void * self,
            void *,
            const void * a,
            const void * b,
            const void * c,
            std::uint32_t count,
            std::uint32_t second,
            void * third
        )
        {
            reinterpret_cast<RoadDrawFn>(SPRITE_DRAW)(self, a, b, c, count, second, third);
            Driver::GetInstance().OnMeshDrawn( self );
        }


        // Skid mark draw 0x451420, slot 2 of the table 005B3D28 (calls FUN_0047a060), thiscall with two arguments
        // like the track chunk draw (doc/BUGS.md 19.1).
        constexpr std::uintptr_t SKID_DRAW = 0x00451420;
        constexpr std::uintptr_t SKID_DRAW_SLOT = 0x005B3D30;

        void __fastcall SkidDrawHook(
            void * self,
            void *,
            std::uint32_t a,
            std::uint32_t b
        )
        {
            Driver & driver = Driver::GetInstance();
            driver.OnSkidDraw( true );
            reinterpret_cast<void( __thiscall * )(void *, std::uint32_t, std::uint32_t)>(SKID_DRAW)(self, a, b);
            driver.OnSkidDraw( false );
        }


        // Track chunk draw FUN_00450ca0, slot 2 of the table 005B3CF0, thiscall with two arguments. It runs
        // by day and by night; the colour nodes with the headlight code only exist at night (doc/LICHT.md 7.9).
        constexpr std::uintptr_t TRACK_DRAW = 0x00450CA0;
        constexpr std::uintptr_t TRACK_DRAW_SLOT = 0x005B3CF8;

        using TrackDrawFn = void( __thiscall * )(void *, std::uint32_t, std::uint32_t);

        void __fastcall TrackDrawHook(
            void * self,
            void *,
            std::uint32_t a,
            std::uint32_t b
        )
        {
            Driver & driver = Driver::GetInstance();
            driver.OnDayTrackDraw( true );
            reinterpret_cast<TrackDrawFn>(TRACK_DRAW)(self, a, b);
            driver.OnDayTrackDraw( false );
        }

        bool PatchSlot(
            std::uintptr_t slot,
            const void * hook
        )
        {
            auto * entry = reinterpret_cast<std::uintptr_t *>(slot);
            DWORD old = 0;
            if (!::VirtualProtect( entry, sizeof( *entry ), PAGE_READWRITE, &old ))
                return false;

            *entry = reinterpret_cast<std::uintptr_t>(hook);
            ::VirtualProtect( entry, sizeof( *entry ), old, &old );
            return true;
        }


        // 4 x 4 row vector matrices: c = a x b.
        void Multiply(
            const float * a,
            const float * b,
            float * c
        )
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        sum += a[row * 4 + k] * b[k * 4 + column];
                    }
                    c[row * 4 + column] = sum;
                }
            }
        }

        // Own light map for '[Dx11] HeadlightsMap': uncompressed 8, 24 or 32 bit BMP as grey, row 0 next to the car.
        bool LoadHeadMapBmp(
            const std::string & file,
            std::vector<std::uint8_t> & pixels,
            int & width,
            int & height
        )
        {
            std::vector<std::uint8_t> data;
            if (FILE * handle = std::fopen( file.c_str(), "rb" ))
            {
                std::uint8_t buffer[4096];
                std::size_t read = 0;
                while ((read = std::fread( buffer, 1, sizeof( buffer ), handle )) > 0)
                {
                    data.insert( data.end(), buffer, buffer + read );
                }
                std::fclose( handle );
            }

            auto u16 = [&data]( std::size_t at ) { return static_cast<std::uint32_t>(data[at] | (data[at + 1] << 8)); };
            auto u32 = [&data]( std::size_t at ) { return static_cast<std::uint32_t>(data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) | (data[at + 3] << 24)); };

            if (data.size() < 54 || data[0] != 'B' || data[1] != 'M')
                return false;

            const std::uint32_t offset = u32( 10 );
            const std::uint32_t header = u32( 14 );
            const auto w = static_cast<std::int32_t>(u32( 18 ));
            const auto h = static_cast<std::int32_t>(u32( 22 ));
            const std::uint32_t bits = u16( 28 );
            const std::uint32_t compression = u32( 30 );
            const int rows = h < 0 ? -h : h;

            if (header < 40 || compression != 0 || w < 16 || w > 4096 || rows < 16 || rows > 4096 || (bits != 8 && bits != 24 && bits != 32))
                return false;

            const std::size_t stride = ((static_cast<std::size_t>(w) * bits + 31) / 32) * 4;
            if (offset + stride * static_cast<std::size_t>(rows) > data.size())
                return false;

            std::uint8_t palette[256] = {};
            if (bits == 8)
            {
                std::uint32_t colours = u32( 46 );
                if (colours == 0 || colours > 256)
                    colours = 256;
                if (14 + header + colours * 4 > data.size())
                    return false;
                for (std::uint32_t i = 0; i < colours; ++i)
                {
                    const std::uint8_t * entry = data.data() + 14 + header + i * 4;
                    palette[i] = static_cast<std::uint8_t>((entry[0] * 114 + entry[1] * 587 + entry[2] * 299 + 500) / 1000);
                }
            }

            width = w;
            height = rows;
            pixels.assign( static_cast<std::size_t>(w) * static_cast<std::size_t>(rows), 0 );

            for (int row = 0; row < rows; ++row)
            {
                // Bottom up BMP: the first stored row is the bottom of the picture, the map row next to the car.
                const int stored = h > 0 ? row : rows - 1 - row;
                const std::uint8_t * line = data.data() + offset + stride * static_cast<std::size_t>(stored);
                std::uint8_t * out = pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w);

                for (int x = 0; x < w; ++x)
                {
                    if (bits == 8)
                    {
                        out[x] = palette[line[x]];
                    }
                    else
                    {
                        const std::uint8_t * p = line + static_cast<std::size_t>(x) * (bits / 8);
                        out[x] = static_cast<std::uint8_t>((p[0] * 114 + p[1] * 587 + p[2] * 299 + 500) / 1000);
                    }
                }
            }

            return true;
        }

        // Inverse of an affine row vector matrix (rotation and scale in 3 x 3, translation in row 3).
        bool InvertAffine(
            const float * m,
            float * out
        )
        {
            const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
            const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);

            if (std::abs( det ) < 1e-12)
                return false;

            const double r[9] = {
                (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };

            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    out[row * 4 + column] = static_cast<float>(r[row * 3 + column]);
                }
                out[row * 4 + 3] = 0.0f;
            }

            for (int column = 0; column < 3; ++column)
            {
                out[12 + column] = static_cast<float>(-(m[12] * r[column] + m[13] * r[3 + column] + m[14] * r[6 + column]));
            }

            out[15] = 1.0f;
            return true;
        }
    }


    namespace
    {
        // Vertex lighting node of porsche.exe v3.5, vtable slot 0 at 0x005BBD5C (doc/LICHT.md 6.6).
        constexpr std::uintptr_t LIGHT_NODE_SLOT = 0x005BBD5C;
        constexpr std::uintptr_t LIGHT_NODE = 0x00574440;
        constexpr std::uintptr_t NODE_STREAM = 0x0053F430;

        using LightNodeFn = std::uint32_t( __thiscall * )(void *, std::uint32_t, int *);
        using NodeStreamFn = int( __thiscall * )(void *, int, int);

        std::uint32_t __fastcall LightNodeHook(
            void * self,
            void *,
            std::uint32_t a,
            int * b
        )
        {
            Driver::GetInstance().NoteLightNode( self );
            return reinterpret_cast<LightNodeFn>(LIGHT_NODE)(self, a, b);
        }
    }


    void Driver::InstallLightHook()
    {
        auto * slot = reinterpret_cast<std::uintptr_t *>(LIGHT_NODE_SLOT);

        // Only on the expected build: image at 0x400000 and the slot still holds the node.
        if (::GetModuleHandleA( nullptr ) != reinterpret_cast<HMODULE>(0x00400000) || *slot != LIGHT_NODE)
        {
            Log( "M3T light hook not installed" );
            return;
        }

        DWORD old = 0;
        if (::VirtualProtect( slot, sizeof( *slot ), PAGE_READWRITE, &old ))
        {
            *slot = reinterpret_cast<std::uintptr_t>(&LightNodeHook);
            ::VirtualProtect( slot, sizeof( *slot ), old, &old );
            Log( "M3T light hook installed at %08X", static_cast<unsigned>(LIGHT_NODE_SLOT) );
        }

        // Write watch of the snapshot (PLAN.md 9.4d).
        ::AddVectoredExceptionHandler( 1, WatchHandler );
    }


    void Driver::InstallSkidHook()
    {
        if (*reinterpret_cast<const std::uintptr_t *>(SKID_DRAW_SLOT) != SKID_DRAW)
        {
            Log( "Skid hook not installed, table entry %08X does not match", static_cast<unsigned>(SKID_DRAW_SLOT) );
            m_ConfigSkidBias = 0.0f;
            return;
        }

        PatchSlot( SKID_DRAW_SLOT, reinterpret_cast<const void *>(&SkidDrawHook) );
        Log( "Skid hook installed at %08X, depth bias %.4f", static_cast<unsigned>(SKID_DRAW_SLOT), m_ConfigSkidBias );
    }

    void Driver::InstallHeadlightHooks()
    {
        if (::GetModuleHandleA( nullptr ) != reinterpret_cast<HMODULE>(0x00400000))
        {
            Log( "Headlight hooks not installed, unexpected image base" );
            return;
        }

        // All four sites are checked before any is changed, a half hooked game would light twice.
        const struct
        {
            std::uintptr_t m_Site;
            std::uintptr_t m_Target;
            const void * m_Hook;
        } sites[] = {
            { HEADLIGHT_CALL, HEADLIGHT_FACTOR, reinterpret_cast<const void *>(&HeadlightHook) },
            { HEADLIGHT_CALL_2, HEADLIGHT_FACTOR, reinterpret_cast<const void *>(&HeadlightHook) },
            { ROAD_DRAW_CALL, ROAD_DRAW, reinterpret_cast<const void *>(&RoadDrawHook) },
            { SPRITE_DRAW_CALL, SPRITE_DRAW, reinterpret_cast<const void *>(&SpriteDrawHook) },
        };

        for (const auto & site : sites)
        {
            const auto * call = reinterpret_cast<const std::uint8_t *>(site.m_Site);
            std::int32_t relative = 0;
            std::memcpy( &relative, call + 1, sizeof( relative ) );

            if (call[0] != 0xE8 || site.m_Site + 5 + static_cast<std::uintptr_t>(relative) != site.m_Target)
            {
                Log( "Headlight hooks not installed, call at %08X does not match", static_cast<unsigned>(site.m_Site) );
                m_ConfigHeadlights = 0;
                return;
            }
        }

        for (const auto & site : sites)
        {
            PatchCall( site.m_Site, site.m_Target, site.m_Hook );
        }

        // By day: the track chunk draw, '[Dx11] HeadlightsDay'.
        if (m_ConfigHeadlightsDay > 0)
        {
            if (*reinterpret_cast<const std::uintptr_t *>(TRACK_DRAW_SLOT) == TRACK_DRAW)
            {
                PatchSlot( TRACK_DRAW_SLOT, reinterpret_cast<const void *>(&TrackDrawHook) );
                Log( "Headlights by day: track chunk draw hooked at %08X", static_cast<unsigned>(TRACK_DRAW_SLOT) );
            }
            else
            {
                Log( "Headlights by day: track draw table does not match, off" );
                m_ConfigHeadlightsDay = 0;
            }
        }

        Log( "Headlight hooks installed (factor %08X/%08X, draws %08X/%08X)", static_cast<unsigned>(HEADLIGHT_CALL),
            static_cast<unsigned>(HEADLIGHT_CALL_2), static_cast<unsigned>(ROAD_DRAW_CALL), static_cast<unsigned>(SPRITE_DRAW_CALL) );
    }


    bool Driver::UpdateHeadMap(
        bool day
    )
    {
        if (!m_Device)
            return false;

        // Own map from '[Dx11] HeadlightsMap', any size, the shader samples it with the same coordinates.
        if (!m_ConfigHeadMapFile.empty())
        {
            constexpr std::uintptr_t CUSTOM_KEY = 2;
            if (m_HeadMap && m_HeadMapPointer == CUSTOM_KEY)
                return true;

            if (!m_CustomMapTried)
            {
                m_CustomMapTried = true;
                if (!LoadHeadMapBmp( m_ConfigHeadMapFile, m_CustomMap, m_CustomMapWidth, m_CustomMapHeight ))
                {
                    Log( "Headlights: own map '%s' missing or not a supported BMP, the game's map is used", m_ConfigHeadMapFile.c_str() );
                    m_CustomMap.clear();
                }
            }

            if (!m_CustomMap.empty())
            {
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = static_cast<UINT>(m_CustomMapWidth);
                desc.Height = static_cast<UINT>(m_CustomMapHeight);
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_IMMUTABLE;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                const D3D11_SUBRESOURCE_DATA data = { m_CustomMap.data(), static_cast<UINT>(m_CustomMapWidth), 0 };

                m_HeadMap.Reset();
                m_HeadMapView.Reset();
                if (FAILED( m_Device->CreateTexture2D( &desc, &data, m_HeadMap.Put() ) ))
                    return false;

                m_Device->CreateShaderResourceView( m_HeadMap.Get(), nullptr, m_HeadMapView.Put() );
                if (!m_HeadSampler)
                {
                    D3D11_SAMPLER_DESC sampler = {};
                    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                    sampler.MaxLOD = D3D11_FLOAT32_MAX;
                    m_Device->CreateSamplerState( &sampler, m_HeadSampler.Put() );
                }

                m_HeadMapPointer = CUSTOM_KEY;
                Log( "Headlights: own light map '%s' uploaded, %dx%d", m_ConfigHeadMapFile.c_str(), m_CustomMapWidth, m_CustomMapHeight );
                return true;
            }
        }

        std::uint8_t copy[128 * 128];
        std::uintptr_t key = 1;

        if (day)
        {
            // By day the game never loads it: read the same 128 x 128 map from 'GameData\Render\night.psh'
            // (SHPP with one 8 bit entry 'nght', pixels at 0x28, identical to the night copy).
            if (m_HeadMap && m_HeadMapPointer == key)
                return true;

            if (m_DayMap.empty())
            {
                char path[MAX_PATH] = {};
                ::GetModuleFileNameA( nullptr, path, MAX_PATH );
                std::string file = path;
                const std::size_t slash = file.find_last_of( "\\/" );
                file = (slash != std::string::npos ? file.substr( 0, slash + 1 ) : std::string()) + "GameData\\Render\\night.psh";

                std::vector<std::uint8_t> data;
                if (FILE * handle = std::fopen( file.c_str(), "rb" ))
                {
                    std::uint8_t buffer[4096];
                    std::size_t read = 0;
                    while ((read = std::fread( buffer, 1, sizeof( buffer ), handle )) > 0)
                    {
                        data.insert( data.end(), buffer, buffer + read );
                    }
                    std::fclose( handle );
                }

                const bool shape = data.size() >= 0x28 + sizeof( copy ) && std::memcmp( data.data(), "SHPP", 4 ) == 0 &&
                    data[0x1C] == 0x80 && data[0x1D] == 0 && data[0x1E] == 0x80 && data[0x1F] == 0;

                if (!shape)
                {
                    Log( "Headlights: day map '%s' missing or not as expected", file.c_str() );
                    m_ConfigHeadlightsDay = 0;
                    return false;
                }

                m_DayMap.assign( data.begin() + 0x28, data.begin() + 0x28 + static_cast<std::ptrdiff_t>(sizeof( copy )) );
            }

            std::memcpy( copy, m_DayMap.data(), sizeof( copy ) );
        }
        else
        {
            key = *reinterpret_cast<const std::uintptr_t *>(0x0061E1CC);
            if (key == 0)
                return false;

            // The map is static per track, check pointer and content once per frame.
            if (m_HeadMap && key == m_HeadMapPointer && m_HeadMapFlip == m_FlipCount)
                return true;

            if (!SafeCopy( reinterpret_cast<const void *>(key), copy, sizeof( copy ) ))
                return false;
        }

        std::uint32_t sum = 2166136261u;
        for (const std::uint8_t value : copy)
        {
            sum = (sum ^ value) * 16777619u;
        }

        m_HeadMapFlip = m_FlipCount;

        if (m_HeadMap && key == m_HeadMapPointer && sum == m_HeadMapSum)
            return true;

        if (!m_HeadMap)
        {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = 128;
            desc.Height = 128;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_HeadMap.Put() ) ))
                return false;

            m_Device->CreateShaderResourceView( m_HeadMap.Get(), nullptr, m_HeadMapView.Put() );

            D3D11_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            m_Device->CreateSamplerState( &sampler, m_HeadSampler.Put() );
        }

        m_Context->UpdateSubresource( m_HeadMap.Get(), 0, nullptr, copy, 128, 0 );
        m_HeadMapPointer = key;
        m_HeadMapSum = sum;
        Log( "Headlights: light map %s uploaded (hash %08X)", day ? "from night.psh (day)" : "of the game (night)", sum );
        return true;
    }


    // Light colour by model year from the LFSP plugin (lfsp.asi, PLAN.md 9.9): multiplies the colour, no plugin = unchanged.
    void Driver::ApplyLfspTint()
    {
        using TintFn = int( __cdecl * )(float * rgb);
        static TintFn tint_fn = nullptr;
        static bool resolved = false;
        if (!resolved)
        {
            resolved = true;
            if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                tint_fn = reinterpret_cast<TintFn>(::GetProcAddress( lfsp, "LfspHeadlightTint" ));
            Log( "Headlights: LFSP colour by year %s", tint_fn != nullptr ? "found" : "not available (lfsp.asi missing or older)" );
        }

        float tint[3] = { 1.0f, 1.0f, 1.0f };
        const int year = tint_fn != nullptr ? tint_fn( tint ) : 0;
        if (year != m_HeadTintYear)
        {
            m_HeadTintYear = year;
            if (year > 0)
                Log( "Headlights: LFSP tint for model year %d: %.2f %.2f %.2f", year, tint[0], tint[1], tint[2] );
            else
                Log( "Headlights: LFSP tint off" );
        }
        if (year <= 0)
            return;

        for (std::size_t c = 0; c < 3; ++c)
        {
            m_HeadColour[c] *= tint[c];
        }
    }


    namespace
    {
        // Racer list (game: 0x005E8B34, 9 slots, count at 0x00658080). With LFSP [Cars] MaxCars > 9 (lfsp.asi v7w,
        // PLAN.md 12.4) the list is moved and longer; LfspCarList gives the current one and its capacity.
        // Without the export, the game's list.
        const std::uintptr_t * RacerList(
            std::uint32_t * capacity
        )
        {
            using CarListFn = int( __cdecl * )(void *** list);
            static CarListFn list_fn = nullptr;
            static bool resolved = false;
            if (!resolved)
            {
                resolved = true;
                if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                    list_fn = reinterpret_cast<CarListFn>(reinterpret_cast<void *>(::GetProcAddress( lfsp, "LfspCarList" )));
            }
            void ** list = reinterpret_cast<void **>(0x005E8B34);
            int slots = 9;
            if (list_fn != nullptr)
                slots = list_fn( &list );
            *capacity = static_cast<std::uint32_t>(slots);
            return reinterpret_cast<const std::uintptr_t *>(list);
        }

        // Car of racer 'index', 0 outside the list.
        std::uintptr_t RacerCar(
            std::uint32_t index
        )
        {
            std::uint32_t capacity = 0;
            const std::uintptr_t * list = RacerList( &capacity );
            return index < capacity ? list[index] : 0;
        }

        // Number of racers in the race, at most the list's capacity.
        std::uint32_t RacerCount()
        {
            std::uint32_t capacity = 0;
            RacerList( &capacity );
            return std::min( *reinterpret_cast<const std::uint32_t *>(0x00658080), capacity );
        }

        // Tint for another racer from LFSP (lfsp.asi v6d): model year of that car. Without the export, no tint.
        int LfspTintForCar(
            std::uintptr_t car,
            float * rgb
        )
        {
            using TintFn = int( __cdecl * )(const void * car, float * rgb);
            static TintFn tint_fn = nullptr;
            static bool resolved = false;
            if (!resolved)
            {
                resolved = true;
                if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                    tint_fn = reinterpret_cast<TintFn>(::GetProcAddress( lfsp, "LfspHeadlightTintForCar" ));
            }
            return tint_fn != nullptr ? tint_fn( reinterpret_cast<const void *>(car), rgb ) : 0;
        }

        // Same test as FUN_00453110: a byte per headlight in the table at [[car+0x74C]+0x2D4]; 0 = off or broken.
        bool CarLightsOn(
            std::uintptr_t car,
            std::uint8_t (&lights)[2]
        )
        {
            std::uintptr_t object = 0;
            std::uintptr_t table = 0;
            lights[0] = lights[1] = 0;
            SafeCopy( reinterpret_cast<const void *>(car + 0x74C), &object, 4 ) && object != 0 &&
                SafeCopy( reinterpret_cast<const void *>(object + 0x2D4), &table, 4 ) && table != 0 &&
                SafeCopy( reinterpret_cast<const void *>(table), lights, sizeof( lights ) );
            return lights[0] != 0 || lights[1] != 0;
        }
    }


    // Measurement for the tail lights (PLAN.md 11.5, doc/LICHT.md 9): the lamp table at [model + 0x2D4] of the
    // player's car, model = [car + 0x74C]. The light layer of the car model (0x0043D8C4) asks FUN_00443660 with the
    // lamp number of each lamp part; bytes 0 and 1 are the headlights. Logged on every change, at most 400 lines.
    void Driver::LogLampTable()
    {
        if (m_LampLines >= 400 || (m_FlipCount & 1) != 0)
            return;

        const std::uint32_t player = *reinterpret_cast<const std::uint32_t *>(0x0065741C);
        const std::uintptr_t car = RacerCar( player );
        std::uintptr_t model = 0;
        std::uintptr_t table = 0;
        std::uint8_t lamps[16] = {};
        if (car == 0 || !SafeCopy( reinterpret_cast<const void *>(car + 0x74C), &model, 4 ) || model == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(model + 0x2D4), &table, 4 ) || table == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(table), lamps, sizeof( lamps ) ))
            return;

        if (car == m_LampCar && std::memcmp( lamps, m_LampBytes.data(), sizeof( lamps ) ) == 0)
            return;

        m_LampCar = car;
        std::memcpy( m_LampBytes.data(), lamps, sizeof( lamps ) );
        ++m_LampLines;
        Log( "Lamps: player car %08X model %08X table %08X: %02X %02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X %02X %02X "
            "(night flag %u, frame %u)", static_cast<unsigned>(car), static_cast<unsigned>(model), static_cast<unsigned>(table), lamps[0], lamps[1],
            lamps[2], lamps[3], lamps[4], lamps[5], lamps[6], lamps[7], lamps[8], lamps[9], lamps[10], lamps[11], lamps[12], lamps[13], lamps[14],
            lamps[15], static_cast<unsigned>(*reinterpret_cast<const std::uint16_t *>(0x00628A28)), m_FlipCount );
    }


    // High beam from LFSP (lfsp.asi v6e, PLAN.md 9.10), asked once per frame: longer, narrower, brighter and less
    // height falloff for the player's light only. Without the export it stays the low beam.
    float Driver::UpdateHighBeam()
    {
        if (m_HighFlip != m_FlipCount)
        {
            m_HighFlip = m_FlipCount;
            using HighFn = float( __cdecl * )();
            static HighFn high_fn = nullptr;
            static bool resolved = false;
            if (!resolved)
            {
                resolved = true;
                if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                    high_fn = reinterpret_cast<HighFn>(::GetProcAddress( lfsp, "LfspHighBeam" ));
                Log( "Headlights: LFSP high beam %s", high_fn != nullptr ? "found" : "not available (lfsp.asi missing or older)" );
            }
            // Like a headlight flasher it also works with the light switch off (9.10c); the bytes are only logged.
            const std::uint32_t player = *reinterpret_cast<const std::uint32_t *>(0x0065741C);
            const std::uintptr_t car = RacerCar( player );
            std::uint8_t lights[2] = {};
            if (car != 0)
                CarLightsOn( car, lights );
            const float high = high_fn != nullptr ? std::clamp( high_fn(), 0.0f, 1.0f ) : 0.0f;
            if ((high > 0.0f) != m_HighKey)
            {
                m_HighKey = high > 0.0f;
                Log( "Headlights: high beam key %s, light bytes %u %u", m_HighKey ? "down" : "up", lights[0], lights[1] );
            }
            m_High = high;
        }
        return m_High;
    }


    void Driver::ApplyHighBeam()
    {
        UpdateHighBeam();

        // Scales map distance to map rows: a longer reach means a smaller z scale, a narrower cone a larger x scale.
        m_HeadHigh[0] = 1.0f / (1.0f + (m_ConfigHighRange - 1.0f) * m_High);
        m_HeadHigh[1] = 1.0f / (1.0f + (m_ConfigHighWidth - 1.0f) * m_High);
        m_HeadHigh[2] = 1.0f + (m_ConfigHighHeight - 1.0f) * m_High;
        m_HeadHigh[3] = 0.0f;
        const float strength = 1.0f + (m_ConfigHighStrength - 1.0f) * m_High;
        for (std::size_t c = 0; c < 3; ++c)
        {
            m_HeadColour[c] *= strength;
        }
    }


    // Object to light space of a car like FUN_004526c0 for the player (doc/LICHT.md 8.4a): 3 x 3 = rotation
    // car+0x364 transposed (FUN_00532a00) or as stored, translation = -position car+0x330 through that 3 x 3
    // (FUN_00532a40 via the engine's matrix stack, row or column form unknown), then FUN_00539cd0 builds the
    // 4 x 4 from the 3 x 3 rows and the translation row, plus the forward offset 0x0061BD50 on element 14.
    // layout 1: transposed, row form; 2: as stored, row form; 3: transposed, column form; 4: as stored, column form.
    bool Driver::CarObjectToLight(
        std::uintptr_t car,
        int layout,
        float * out
    )
    {
        float position[3] = {};
        float rotation[9] = {};
        if (layout < 1 || layout > 4 ||
            !SafeCopy( reinterpret_cast<const void *>(car + 0x330), position, sizeof( position ) ) ||
            !SafeCopy( reinterpret_cast<const void *>(car + 0x364), rotation, sizeof( rotation ) ))
            return false;

        const bool transposed = layout == 1 || layout == 3;
        const bool column_form = layout >= 3;
        float m[9] = {};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
                m[row * 3 + column] = transposed ? rotation[column * 3 + row] : rotation[row * 3 + column];
        }

        float t[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
                t[i] -= column_form ? m[i * 3 + j] * position[j] : position[j] * m[j * 3 + i];
        }

        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
                out[row * 4 + column] = m[row * 3 + column];
            out[row * 4 + 3] = 0.0f;
            out[12 + row] = t[row];
        }
        out[15] = 1.0f;
        out[14] += *reinterpret_cast<const float *>(0x0061BD50);
        return true;
    }


    // Tail light glow of all racers (PLAN.md 11.5, doc/LICHT.md 9.5): rear light space = the car's light space turned
    // by 180 degrees (x and z negated, z > 0 behind the car, x > 0 its left). Strength per side from the lamp table
    // [[car + 0x74C] + 0x2D4] (doc/LICHT.md 9.3): tail 12/13, brake 2/3; a broken lamp is 0. With the light switch on
    // the game keeps 2/3 at 1 as dimmed tail lights (strength [model + 0x2D8] < 1), braking sets the strength to 1 (11.5e).
    // Turn signals (11.5d/e, doc/LICHT.md 9.6): LFSP knows the position and colour of each lamp of the model
    // (LfspModelLamps). Lamps at model z < 0 are at the front and light a second glow in front of the car, each in the
    // colour of its lamp. The game blinks 4/5 and 10/11 in phase P = (time [0x6B7C7C] / ([0x5DEB48] / 2)) & 1 and the
    // rear lamps 6/7 in the opposite phase; the glow blinks all of them in phase P. A lamp that stays dark in its own
    // phase for 3 frames in a row counts as broken.
    void Driver::CollectRearLights(
        std::uint32_t racers
    )
    {
        if (m_ConfigTailGlow == 0 || m_CarMatrixLayout <= 0)
            return;

        struct LfspLamp
        {
            int m_Number;
            float m_Pos[3];
            std::uint32_t m_Colour;
        };
        using LampsFn = int( __cdecl * )(const void *, LfspLamp *, int);
        static LampsFn lamps_fn = nullptr;
        static bool resolved = false;
        if (!resolved)
        {
            resolved = true;
            if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                lamps_fn = reinterpret_cast<LampsFn>(::GetProcAddress( lfsp, "LfspModelLamps" ));
            Log( "Tail glow: LFSP lamp data %s", lamps_fn != nullptr ? "found" : "not available (lfsp.asi missing or older), no signal glow" );
        }

        float colour[4] = {};
        ColorToFloat4( 0xFF000000u | m_ConfigTailColour, colour );
        const float forward_offset = *reinterpret_cast<const float *>(0x0061BD50);

        for (std::uint32_t i = 0; i < racers && m_RearLights.size() < static_cast<std::size_t>(MAX_REAR_LIGHTS); ++i)
        {
            const std::uintptr_t car = RacerCar( i );
            std::uintptr_t model = 0;
            std::uintptr_t table = 0;
            std::uint8_t lamps[14] = {};
            if (car == 0 || !SafeCopy( reinterpret_cast<const void *>(car + 0x74C), &model, 4 ) || model == 0 ||
                !SafeCopy( reinterpret_cast<const void *>(model + 0x2D4), &table, 4 ) || table == 0 ||
                !SafeCopy( reinterpret_cast<const void *>(table), lamps, sizeof( lamps ) ))
                continue;

            RearLight light;
            light.m_Car = car;
            if (!CarObjectToLight( car, m_CarMatrixLayout, light.m_ObjectToLight.data() ))
                continue;
            RearLight front = light;
            for (int row = 0; row < 4; ++row)
            {
                light.m_ObjectToLight[static_cast<std::size_t>(row * 4)] = -light.m_ObjectToLight[static_cast<std::size_t>(row * 4)];
                light.m_ObjectToLight[static_cast<std::size_t>(row * 4 + 2)] = -light.m_ObjectToLight[static_cast<std::size_t>(row * 4 + 2)];
            }

            // Light switch on (bit 0 of car + 0xD87, not the flasher): the tail lights glow brighter (11.5f).
            std::uint8_t switch_bits = 0;
            SafeCopy( reinterpret_cast<const void *>(car + 0xD87), &switch_bits, 1 );
            const float tail_strength = m_ConfigTailStrength + ((switch_bits & 1) != 0 ? m_ConfigTailLights : 0.0f);
            const float tail[2] = { lamps[12] != 0 ? tail_strength : 0.0f, lamps[13] != 0 ? tail_strength : 0.0f };
            std::uintptr_t strengths = 0;
            float strength[4] = {};
            SafeCopy( reinterpret_cast<const void *>(model + 0x2D8), &strengths, 4 ) && strengths != 0 &&
                SafeCopy( reinterpret_cast<const void *>(strengths), strength, sizeof( strength ) );
            const float brake[2] = { lamps[2] != 0 && strength[2] > 0.99f ? m_ConfigTailBrake : 0.0f,
                                     lamps[3] != 0 && strength[3] > 0.99f ? m_ConfigTailBrake : 0.0f };
            for (std::size_t c = 0; c < 3; ++c)
            {
                light.m_Left[c] = colour[c] * (tail[0] + brake[0]);
                light.m_Right[c] = colour[c] * (tail[1] + brake[1]);
            }

            // Signal lamps that are lit right now; 10/11 only count while the signal of that side is set, otherwise
            // they show the brake. Bits 3/4 of car + 0xD87, hazards car + 0xB50 == 3 (doc/LICHT.md 9.3).
            std::uint8_t bits = 0;
            std::int32_t hazard = 0;
            SafeCopy( reinterpret_cast<const void *>(car + 0xD87), &bits, 1 );
            SafeCopy( reinterpret_cast<const void *>(car + 0xB50), &hazard, 4 );
            const bool side_set[2] = { (bits & 0x08) != 0 || hazard == 3, (bits & 0x10) != 0 || hazard == 3 };
            const std::int32_t half = *reinterpret_cast<const std::int32_t *>(0x005DEB48) / 2;
            const bool phase = half > 0 && ((*reinterpret_cast<const std::int32_t *>(0x006B7C7C) / half) & 1) != 0;
            if (i < m_SignalCar.size() && m_SignalCar[i] != car)
            {
                m_SignalCar[i] = car;
                m_SignalMiss[i].fill( 0 );
            }
            bool front_lit = false;
            float front_z = 0.0f;
            LfspLamp known[16] = {};
            const int count = lamps_fn != nullptr && (m_ConfigTailSignals > 0.0f || m_ConfigTailReverse > 0.0f)
                ? lamps_fn( reinterpret_cast<const void *>(model), known, 16 ) : 0;
            for (int k = 0; k < count; ++k)
            {
                const int n = known[k].m_Number;
                // Reversing light (11.5g): lamp 8 is on in reverse gear (car + 0xD82 == 0, doc/LICHT.md 9.3) and 0 when
                // broken; only cars whose model has the lamp. Lights both sides behind the car in the colour of the lamp.
                if (n == 8)
                {
                    if (lamps[8] != 0 && m_ConfigTailReverse > 0.0f)
                    {
                        float lamp[4] = {};
                        ColorToFloat4( 0xFF000000u | known[k].m_Colour, lamp );
                        const float peak = std::max( lamp[0], std::max( lamp[1], lamp[2] ) );
                        for (std::size_t c = 0; c < 3; ++c)
                        {
                            const float value = (peak < 0.05f ? 1.0f : lamp[c] / peak) * m_ConfigTailReverse;
                            light.m_Left[c] += value;
                            light.m_Right[c] += value;
                        }
                    }
                    continue;
                }
                const bool signal = n >= 4 && n <= 7;
                const bool combined = n == 10 || n == 11;
                if ((!signal && !combined) || n >= static_cast<int>(sizeof( lamps )) || i >= m_SignalCar.size())
                    continue;
                const int side = (n & 1) == 0 ? 0 : 1; // 0 = left (4, 6, 10), 1 = right (5, 7, 11)
                if (!side_set[side])
                    continue;
                std::uint8_t & miss = m_SignalMiss[i][static_cast<std::size_t>(n)];
                // The game lights this lamp now: 6/7 in the opposite phase, unless LFSP removed the NOT (11.5f, 0x43943A).
                const bool rear_inverted = *reinterpret_cast<const std::uint16_t *>(0x0043943A) == 0xD0F7;
                if (((n == 6 || n == 7) && rear_inverted) != phase)
                    miss = lamps[n] != 0 ? 0 : static_cast<std::uint8_t>(std::min( miss + 1, 255 ));
                if (!phase || miss >= 3)
                    continue;

                // Colour of the lamp, brightest channel = 1; black (no colour in the model) -> amber.
                float lamp[4] = {};
                ColorToFloat4( 0xFF000000u | known[k].m_Colour, lamp );
                const float peak = std::max( lamp[0], std::max( lamp[1], lamp[2] ) );
                if (peak < 0.05f)
                {
                    lamp[0] = 1.0f;
                    lamp[1] = 0.55f;
                    lamp[2] = 0.0f;
                }
                else
                {
                    for (std::size_t c = 0; c < 3; ++c)
                        lamp[c] /= peak;
                }

                // Model: x > 0 left (lamps 0, 4, 6), z < 0 front (lamps 0/1). Rear space: x > 0 is the car's left;
                // front space (not turned): x > 0 is its right, z = -model z + forward offset.
                const bool in_front = known[k].m_Pos[2] < 0.0f;
                float * target = in_front ? (side == 0 ? front.m_Right.data() : front.m_Left.data())
                                          : (side == 0 ? light.m_Left.data() : light.m_Right.data());
                for (std::size_t c = 0; c < 3; ++c)
                    target[c] = std::max( target[c], lamp[c] * m_ConfigTailSignals );
                if (in_front)
                {
                    front_lit = true;
                    front_z = std::max( front_z, -known[k].m_Pos[2] );
                }
            }
            if (count > 0 && (m_LampsLogged & (1 << i)) == 0)
            {
                m_LampsLogged |= 1 << i;
                for (int k = 0; k < count; ++k)
                {
                    Log( "Tail glow: racer %u lamp %2d at %.2f %.2f %.2f colour %08X", i, known[k].m_Number, known[k].m_Pos[0],
                        known[k].m_Pos[1], known[k].m_Pos[2], known[k].m_Colour );
                }
            }

            if (light.m_Left[0] + light.m_Left[1] + light.m_Left[2] + light.m_Right[0] + light.m_Right[1] + light.m_Right[2] > 0.0f)
                m_RearLights.push_back( light );
            if (front_lit && m_RearLights.size() < static_cast<std::size_t>(MAX_REAR_LIGHTS))
            {
                // Front light space z = model z + forward offset (CarObjectToLight); the glow starts at the lamp.
                const float offset = front_z + forward_offset;
                front.m_Left[3] = offset > 0.3f && offset < 5.0f ? offset : m_ConfigTailOffset;
                m_RearLights.push_back( front );
            }
        }

        if (static_cast<int>(m_RearLights.size()) != m_RearLightsLogged)
        {
            m_RearLightsLogged = static_cast<int>(m_RearLights.size());
            Log( "Tail glow: %u of %u racers", static_cast<unsigned>(m_RearLights.size()), racers );
        }
    }


    // The other racers with their light on (PLAN.md 9.11), once per frame.
    void Driver::CollectCarLights()
    {
        if (m_CarLightsFlip == m_FlipCount)
            return;

        m_CarLightsFlip = m_FlipCount;
        m_CarLights.clear();
        m_RearLights.clear();
        if (m_ConfigOpponents == 0 && m_ConfigTailGlow == 0)
            return;

        const std::uint32_t player = *reinterpret_cast<const std::uint32_t *>(0x0065741C);
        const std::uint32_t racers = RacerCount();
        const bool night = *reinterpret_cast<const std::uint16_t *>(0x00628A28) != 0;
        const std::uintptr_t player_car = RacerCar( player );

        // Check the layout of the rotation once against the game's own player matrix: FUN_004526c0 builds it in
        // every night frame (also with the switch off), by day the day path builds it (9.11c).
        if (m_CarMatrixLayout == 0 && player_car != 0)
        {
            if (night || m_GameMatrixFlip == m_FlipCount)
            {
                const auto * game = reinterpret_cast<const float *>(0x0061BCCC);
                float difference[5] = { 1e30f, 1e30f, 1e30f, 1e30f, 1e30f };
                int best = 1;
                for (int layout = 1; layout <= 4; ++layout)
                {
                    float mine[16] = {};
                    if (!CarObjectToLight( player_car, layout, mine ))
                        continue;
                    difference[layout] = 0.0f;
                    for (int i = 0; i < 16; ++i)
                        difference[layout] = std::max( difference[layout], std::abs( mine[i] - game[i] ) );
                    if (difference[layout] < difference[best])
                        best = layout;
                }
                // A stale 0x0061BCCC (built for the previous frame) does not match: try again on later frames.
                ++m_CarMatrixTries;
                m_CarMatrixLayout = difference[best] < 0.05f ? best : (m_CarMatrixTries < 20 ? 0 : -1);
                Log( "Headlights opponents: car matrix check against 0x0061BCCC, difference transposed/row %g, stored/row %g, "
                    "transposed/column %g, stored/column %g -> %d%s (try %d)", difference[1], difference[2], difference[3], difference[4],
                    best, m_CarMatrixLayout > 0 ? "" : m_CarMatrixLayout == 0 ? ", no match yet" : ", no match, opponents off",
                    m_CarMatrixTries );
                if (m_CarMatrixLayout < 0)
                {
                    float position[3] = {};
                    float rotation[9] = {};
                    SafeCopy( reinterpret_cast<const void *>(player_car + 0x330), position, sizeof( position ) );
                    SafeCopy( reinterpret_cast<const void *>(player_car + 0x364), rotation, sizeof( rotation ) );
                    Log( "Headlights opponents:   position %g %g %g, rotation %g %g %g | %g %g %g | %g %g %g, offset %g", position[0],
                        position[1], position[2], rotation[0], rotation[1], rotation[2], rotation[3], rotation[4], rotation[5], rotation[6],
                        rotation[7], rotation[8], *reinterpret_cast<const float *>(0x0061BD50) );
                    for (int row = 0; row < 4; ++row)
                    {
                        Log( "Headlights opponents:   game row %d: %g %g %g %g", row, game[row * 4], game[row * 4 + 1], game[row * 4 + 2],
                            game[row * 4 + 3] );
                    }
                }
            }
        }
        if (m_CarMatrixLayout <= 0)
            return;

        float base[4] = {};
        if (night)
        {
            ColorToFloat4( *reinterpret_cast<const std::uint32_t *>(0x0061BD4C), base );
        }
        else
        {
            ColorToFloat4( 0xFF000000u | m_ConfigDayColour, base );
            for (std::size_t c = 0; c < 3; ++c)
                base[c] *= m_ConfigDayStrength;
        }

        CollectRearLights( racers );

        std::uint32_t states = 0;
        for (std::uint32_t i = 0; i < racers && m_ConfigOpponents != 0 && m_CarLights.size() < static_cast<std::size_t>(MAX_CAR_LIGHTS); ++i)
        {
            const std::uintptr_t car = RacerCar( i );
            std::uint8_t lights[2] = {};
            if (car == 0 || i == player || !CarLightsOn( car, lights ))
                continue;

            CarLight light;
            light.m_Car = car;
            if (!CarObjectToLight( car, m_CarMatrixLayout, light.m_ObjectToLight.data() ))
                continue;

            float tint[3] = { 1.0f, 1.0f, 1.0f };
            LfspTintForCar( car, tint );
            for (std::size_t c = 0; c < 3; ++c)
                light.m_Colour[c] = base[c] * tint[c] * m_ConfigOpponentsStrength;
            light.m_Colour[3] = 1.0f;
            states |= 1u << i;
            m_CarLights.push_back( light );
        }

        if (static_cast<int>(states) != m_CarLightsLogged)
        {
            m_CarLightsLogged = static_cast<int>(states);
            Log( "Headlights opponents: %u of %u racers with light (%s, mask %03X, player %u)", static_cast<unsigned>(m_CarLights.size()),
                racers, night ? "night" : "day", states, player );
        }
    }


    bool Driver::PrepareHeadMesh(
        bool day
    )
    {
        // The renderer's EAGL camera (renderer[0], FUN_005393a0) keeps object x view at +0x188 and the
        // projection at +0x148; FUN_005385b0 transforms the vertices with their product into clip space.
        const std::uintptr_t renderer = *reinterpret_cast<const std::uintptr_t *>(0x0069E0D4);
        std::uintptr_t camera = 0;
        float model_view[16] = {};
        float projection_matrix[16] = {};
        bool valid = renderer != 0 && SafeCopy( reinterpret_cast<const void *>(renderer), &camera, 4 ) && camera != 0 &&
            SafeCopy( reinterpret_cast<const void *>(camera + 0x188), model_view, sizeof( model_view ) ) &&
            SafeCopy( reinterpret_cast<const void *>(camera + 0x148), projection_matrix, sizeof( projection_matrix ) );

        // Clip (x, y, w) is affine in the view position: columns 0, 1 and the w column of the projection.
        // At night w is column 3. By day the camera keeps column 3 at (0, 0, 0, 1) and the renderer
        // divides by the depth of column 2 (z = -z view, doc/LICHT.md 7.12).
        const bool affine = std::abs( projection_matrix[3] ) < 1e-6f && std::abs( projection_matrix[7] ) < 1e-6f &&
            std::abs( projection_matrix[11] ) < 1e-6f;
        const int w = affine ? 2 : 3;
        m_HeadWColumn = w;
        if (valid && (m_HeadColumnLogged & (1u << (w + (day ? 4 : 0)))) == 0)
        {
            m_HeadColumnLogged |= 1u << (w + (day ? 4 : 0));
            Log( "Headlights: %s projection, w from column %d: %8.4f %8.4f %8.4f %8.4f | %8.4f %8.4f %8.4f %8.4f | %8.4f %8.4f %8.4f %8.4f | "
                "%8.4f %8.4f %8.4f %8.4f", day ? "day" : "night", w, projection_matrix[0], projection_matrix[1], projection_matrix[2],
                projection_matrix[3], projection_matrix[4], projection_matrix[5], projection_matrix[6], projection_matrix[7], projection_matrix[8],
                projection_matrix[9], projection_matrix[10], projection_matrix[11], projection_matrix[12], projection_matrix[13],
                projection_matrix[14], projection_matrix[15] );
        }
        const float clip_of_view[16] = {
            projection_matrix[0], projection_matrix[1], projection_matrix[w], 0.0f,
            projection_matrix[4], projection_matrix[5], projection_matrix[4 + w], 0.0f,
            projection_matrix[8], projection_matrix[9], projection_matrix[8 + w], 0.0f,
            projection_matrix[12], projection_matrix[13], projection_matrix[12 + w], 1.0f };
        float view_of_clip[16] = {};
        float inverse[16] = {};
        m_HeadFailStep = valid ? 0 : 1;
        valid = valid && InvertAffine( clip_of_view, view_of_clip ) && InvertAffine( model_view, inverse );
        if (!valid && m_HeadFailStep == 0)
            m_HeadFailStep = 2;

        // Viewport of the renderer FUN_00543ae0: x = xc / wc * fx + cx, y = yc / wc * fy + cy.
        float projection[4] = {};
        valid = valid && renderer != 0 && SafeCopy( reinterpret_cast<const void *>(renderer + 0x2C), projection, 8 ) &&
            SafeCopy( reinterpret_cast<const void *>(renderer + 0x38), projection + 2, 8 ) && projection[0] != 0.0f && projection[1] != 0.0f;
        if (!valid && m_HeadFailStep == 0)
            m_HeadFailStep = 3;

        // The map does not depend on the matrices, by day it is loaded even when they are not ready yet.
        const bool map = (valid || day) && UpdateHeadMap( day );
        if (valid && !map)
            m_HeadFailStep = 4;
        valid = valid && map;

        Flush();

        if (!valid)
        {
            if (day && m_DayFailDumps < 3)
            {
                ++m_DayFailDumps;
                Log( "Headlights by day: step %d failed, renderer %08X camera %08X, viewport %g %g %g %g", m_HeadFailStep,
                    static_cast<unsigned>(renderer), static_cast<unsigned>(camera), projection[0], projection[1], projection[2], projection[3] );
                for (int row = 0; row < 4; ++row)
                {
                    Log( "Headlights by day:   model view %10.4f %10.4f %10.4f %10.4f | projection %10.4f %10.4f %10.4f %10.4f",
                        model_view[row * 4], model_view[row * 4 + 1], model_view[row * 4 + 2], model_view[row * 4 + 3],
                        projection_matrix[row * 4], projection_matrix[row * 4 + 1], projection_matrix[row * 4 + 2], projection_matrix[row * 4 + 3] );
                }
            }
            m_HeadMesh = false;
            m_HeadFrameValid = false;
            return false;
        }

        // Screen side (xc, yc, wc, 1) -> view -> object -> headlight space.
        float clip_to_object[16] = {};
        Multiply( view_of_clip, inverse, clip_to_object );
        Multiply( clip_to_object, reinterpret_cast<const float *>(0x0061BCCC), m_HeadViewToLight.data() );

        CollectCarLights();
        m_HeadExtraCount = 0;
        for (const CarLight & light : m_CarLights)
        {
            Multiply( clip_to_object, light.m_ObjectToLight.data(), m_HeadExtraViewToLight.data() + m_HeadExtraCount * 16 );
            ++m_HeadExtraCount;
        }
        m_HeadRearCount = 0;
        for (const RearLight & light : m_RearLights)
        {
            Multiply( clip_to_object, light.m_ObjectToLight.data(), m_HeadRearViewToLight.data() + m_HeadRearCount * 16 );
            ++m_HeadRearCount;
        }
        std::memcpy( m_HeadModelView.data(), model_view, sizeof( model_view ) );
        std::memcpy( m_HeadProjectionMatrix.data(), projection_matrix, sizeof( projection_matrix ) );
        std::memcpy( m_HeadProjection.data(), projection, sizeof( projection ) );

        if (day)
        {
            // The values FUN_00452680 sets for a night race (scale x 3.62, z 1.28, colour grey 128),
            // adjusted by 'HeadlightsDayRange', 'HeadlightsDayColour' and 'HeadlightsDayStrength'.
            const std::uint32_t scale_x = 0x4067AE14u;
            const std::uint32_t scale_z = 0x3FA3D70Au;
            std::memcpy( &m_HeadScale[0], &scale_x, sizeof( float ) );
            std::memcpy( &m_HeadScale[1], &scale_z, sizeof( float ) );
            // At night this path only runs for the other cars (player light off): no day range.
            const bool night = *reinterpret_cast<const std::uint16_t *>(0x00628A28) != 0;
            m_HeadScale[0] /= night ? 1.0f : m_ConfigDayRange;
            m_HeadScale[1] /= night ? 1.0f : m_ConfigDayRange;
            if (night)
            {
                // Night through the track path: player light off (flasher) or only the other cars.
                ColorToFloat4( *reinterpret_cast<const std::uint32_t *>(0x0061BD4C), m_HeadColour.data() );
            }
            else
            {
                ColorToFloat4( 0xFF000000u | m_ConfigDayColour, m_HeadColour.data() );
                for (std::size_t c = 0; c < 3; ++c)
                {
                    m_HeadColour[c] *= m_ConfigDayStrength;
                }
            }
        }
        else
        {
            m_HeadScale[0] = *reinterpret_cast<const float *>(0x0061D170);
            m_HeadScale[1] = *reinterpret_cast<const float *>(0x0061E178);
            ColorToFloat4( *reinterpret_cast<const std::uint32_t *>(0x0061BD4C), m_HeadColour.data() );
        }

        ApplyLfspTint();
        ApplyHighBeam();
        if (m_HeadPlayerOff)
        {
            m_HeadColour[0] = m_HeadColour[1] = m_HeadColour[2] = 0.0f;
        }
        m_HeadScale[2] = *reinterpret_cast<const float *>(0x005CEC60);
        m_HeadMesh = true;
        m_HeadFrameValid = true;
        return true;
    }


    void Driver::OnHeadlightFactor(
        int count
    )
    {
        if (m_ConfigHeadlights == 0 || count <= 0)
            return;

        m_HeadCount = 0;
        m_HeadPlayerOff = false;
        if (!PrepareHeadMesh( false ))
            return;

        m_HeadCount = count;

        // Mode 1: the game adds nothing, FUN_00453110 copies the base colour (factor <= 0).
        if (m_ConfigHeadlights == 1)
        {
            std::memset( reinterpret_cast<void *>(0x0061BD60), 0, static_cast<std::size_t>(std::min( count, 4096 )) * sizeof( float ) );
        }
    }


    void Driver::OnBeforeMeshDraw()
    {
        if (!m_DayMesh)
            return;

        // FUN_00543ae0 updates the camera matrices itself (FUN_005393a0), the light is prepared at its
        // first draw into the driver (doc/LICHT.md 7.11).
        Flush();
        m_HeadMesh = false;
        m_DayPending = true;
    }


    void Driver::PrepareDayPending()
    {
        if (!m_DayPending)
            return;

        m_DayPending = false;

        if (PrepareHeadMesh( true ))
        {
            ++m_DayStats.m_Prepared;
            if ((m_DayLogged & 4) == 0)
            {
                m_DayLogged |= 4;
                Log( "Headlights by day: first lit track draw" );
            }
        }
        else
        {
            ++m_DayStats.m_Failed;
            if ((m_DayLogged & 8) == 0)
            {
                m_DayLogged |= 8;
                Log( "Headlights by day: track draw not lit, step %d failed", m_HeadFailStep );
            }
        }
    }


    void Driver::OnDayTrackDraw(
        bool begin
    )
    {
        if (!begin)
        {
            if (m_DayMesh)
            {
                Flush();
                m_HeadMesh = false;
                m_DayMesh = false;
            }
            m_DayPending = false;
            return;
        }

        if (m_ConfigHeadlights == 0 || (m_ConfigHeadlightsDay == 0 && m_ConfigOpponents == 0 && m_ConfigTailGlow == 0))
            return;

        ++m_DayStats.m_Calls;

        // Also at night: when the player's light is off, the other cars light the track through this path.
        auto * night = reinterpret_cast<std::uint16_t *>(0x00628A28);
        m_DayStats.m_NightFlag = *night;
        if ((m_DayLogged & 1) == 0)
        {
            m_DayLogged |= 1;
            Log( "Headlights by day: first track chunk draw, night flag %u, player %u", static_cast<unsigned>(*night),
                *reinterpret_cast<const std::uint32_t *>(0x0065741C) );
        }

        const std::uint32_t index = *reinterpret_cast<const std::uint32_t *>(0x0065741C);
        m_DayStats.m_Index = index;
        const std::uintptr_t car = RacerCar( index );
        if (car == 0)
        {
            ++m_DayStats.m_NoCar;
            return;
        }

        std::uint8_t lights[2] = {};
        const bool switch_on = CarLightsOn( car, lights );
        m_DayStats.m_Lights[0] = lights[0];
        m_DayStats.m_Lights[1] = lights[1];

        if ((m_DayLogged & 2) == 0)
        {
            m_DayLogged |= 2;
            Log( "Headlights by day: player car %08X, light bytes %u %u", static_cast<unsigned>(car), lights[0], lights[1] );
        }

        // The high beam key lights the player's own beam even with the switch off (flasher, 9.10c).
        const bool flasher = UpdateHighBeam() > 0.0f;
        const bool player_on = flasher || (*night == 0 && (m_ConfigHeadlightsDay == 2 || (m_ConfigHeadlightsDay == 1 && switch_on)));

        // Night with the player's light on: the game's own path through FUN_00452910 lights the meshes.
        if (*night != 0 && switch_on)
        {
            ++m_DayStats.m_Night;
            return;
        }

        if ((player_on || m_CarMatrixLayout == 0) && *night == 0 && m_DayLightFlip != m_FlipCount)
        {
            m_DayLightFlip = m_FlipCount;

            // FUN_004526c0 builds the headlight matrix 0x0061BCCC from the car in DAT_0061bcc8, but only
            // in night mode: set the 16 bit night flag for the call. No police light by day.
            *reinterpret_cast<std::uintptr_t *>(0x0061BCC8) = car;
            *reinterpret_cast<float *>(0x0061BD50) = 0.0f;
            const std::uint16_t saved = *night;
            *night = 1;
            reinterpret_cast<void( __cdecl * )()>(0x004526C0)();
            *night = saved;
            *reinterpret_cast<std::uintptr_t *>(0x0061E180) = 0;
            m_GameMatrixFlip = m_FlipCount;
        }

        // Player light off (night, 'HeadlightsDay' 0, or switch off with 1) and no flasher: only the other cars light the track.
        if (!player_on)
        {
            ++m_DayStats.m_SwitchOff;
            CollectCarLights();
            if (m_CarLights.empty() && m_RearLights.empty())
                return;
        }
        m_HeadPlayerOff = !player_on;

        // The matrices of the chunk are only valid inside it, OnBeforeMeshDraw prepares every draw.
        m_HeadCount = 0;
        m_DayMesh = true;
    }


    void Driver::OnMeshDrawn(
        void * renderer
    )
    {
        ++m_DayStats.m_Draws;

        if (!m_HeadMesh)
            return;

        ++m_DayStats.m_LitDraws;

        // By day the light ends with the world draw, the next one prepares it again.
        if (m_DayMesh)
        {
            if ((m_DayLogged & 16) == 0)
            {
                m_DayLogged |= 16;
                Log( "Headlights by day check: viewport fx %g fy %g cx %g cy %g, w column %d, scale %g %g %g", m_HeadProjection[0],
                    m_HeadProjection[1], m_HeadProjection[2], m_HeadProjection[3], m_HeadWColumn, m_HeadScale[0], m_HeadScale[1], m_HeadScale[2] );
                const auto * vertices = reinterpret_cast<const std::uint8_t *>(renderer) + 0x434;
                for (int i = 0; i < 4; ++i)
                {
                    float tl[4] = {};
                    if (!SafeCopy( vertices + static_cast<std::size_t>(i) * 32, tl, sizeof( tl ) ) || tl[3] <= 0.0f)
                        continue;

                    const float wc = 1.0f / tl[3];
                    const float view[4] = { (tl[0] - m_HeadProjection[2]) * wc / m_HeadProjection[0],
                        (tl[1] - m_HeadProjection[3]) * wc / m_HeadProjection[1], wc, 1.0f };
                    float light[3] = {};
                    for (int column = 0; column < 3; ++column)
                    {
                        light[column] = view[0] * m_HeadViewToLight[column] + view[1] * m_HeadViewToLight[4 + column] +
                            view[2] * m_HeadViewToLight[8 + column] + m_HeadViewToLight[12 + column];
                    }
                    Log( "Headlights by day check: v%d screen %.1f %.1f rhw %.6f -> light %.3f %.3f %.3f", i, tl[0], tl[1], tl[3], light[0],
                        light[1], light[2] );
                }
            }
            Flush();
            m_HeadMesh = false;
            m_DayPending = false;
            return;
        }

        // Check of the reconstruction in the snapshot frame: light space from the vertex array the
        // driver received versus the game's own result in DAT_0061d178 (doc/LICHT.md 7.3).
        if (m_MeasureSnapActive && !m_HeadValidated)
        {
            m_HeadValidated = true;
            const auto * engine = reinterpret_cast<const float *>(0x0061D178);
            const auto * vertices = reinterpret_cast<const std::uint8_t *>(renderer) + 0x434;

            Log( "Headlights check: projection fx %g fy %g cx %g cy %g, scale %g %g %g, colour %.3f %.3f %.3f", m_HeadProjection[0],
                m_HeadProjection[1], m_HeadProjection[2], m_HeadProjection[3], m_HeadScale[0], m_HeadScale[1], m_HeadScale[2], m_HeadColour[0],
                m_HeadColour[1], m_HeadColour[2] );

            for (int row = 0; row < 4; ++row)
            {
                Log( "Headlights check: model view row %d: %10.4f %10.4f %10.4f %10.4f | projection %10.4f %10.4f %10.4f %10.4f", row,
                    m_HeadModelView[row * 4], m_HeadModelView[row * 4 + 1], m_HeadModelView[row * 4 + 2], m_HeadModelView[row * 4 + 3],
                    m_HeadProjectionMatrix[row * 4], m_HeadProjectionMatrix[row * 4 + 1], m_HeadProjectionMatrix[row * 4 + 2],
                    m_HeadProjectionMatrix[row * 4 + 3] );
            }

            for (int i = 0; i < m_HeadCount && i < 6; ++i)
            {
                float tl[4] = {};
                if (!SafeCopy( vertices + static_cast<std::size_t>(i) * 32, tl, sizeof( tl ) ) || tl[3] <= 0.0f)
                    continue;

                const float w = 1.0f / tl[3];
                const float view[4] = { (tl[0] - m_HeadProjection[2]) * w / m_HeadProjection[0], (tl[1] - m_HeadProjection[3]) * w / m_HeadProjection[1],
                    w, 1.0f };
                float light[3] = {};
                for (int column = 0; column < 3; ++column)
                {
                    light[column] = view[0] * m_HeadViewToLight[column] + view[1] * m_HeadViewToLight[4 + column] +
                        view[2] * m_HeadViewToLight[8 + column] + m_HeadViewToLight[12 + column];
                }

                Log( "Headlights check: v%d screen %.1f %.1f rhw %.6f -> light %.3f %.3f %.3f, game %.3f %.3f %.3f", i, tl[0], tl[1], tl[3],
                    light[0], light[1], light[2], engine[i * 4], engine[i * 4 + 1], engine[i * 4 + 2] );
            }
        }

        Flush();
        m_HeadMesh = false;
    }


    void Driver::NoteHeadlight(
        int count,
        const float * positions,
        const float * matrix
    )
    {
        if (!m_MeasureSnapActive || m_HeadlightLogged)
            return;

        m_HeadlightLogged = true;

        // Globals of FUN_00452910 / FUN_00452a50 (doc/LICHT.md 6.15).
        const auto f = [] ( std::uintptr_t address ) { return *reinterpret_cast<const float *>(address); };
        const auto u = [] ( std::uintptr_t address ) { return *reinterpret_cast<const std::uint32_t *>(address); };

        Log( "M3T headlight: count %d, colour %08X, flags %08X, second light %08X, player %u, map %08X", count, u( 0x0061BD4C ),
            u( 0x0061BD5C ), u( 0x0061E180 ), u( 0x0065741C ), u( 0x0061E1CC ) );
        Log( "M3T headlight: scale x %g (raw %g), z %g (raw %g), y %g, k %g; second: xz %g, y %g, k %g", f( 0x0061D170 ), f( 0x0061BD58 ),
            f( 0x0061E178 ), f( 0x0061BD54 ), f( 0x005CEC60 ), f( 0x005CEC64 ), f( 0x005CEC6C ), f( 0x005CEC70 ), f( 0x005CEC68 ) );

        const auto log_matrix = [&] ( const char * name, const float * m )
        {
            for (int row = 0; row < 4; ++row)
            {
                Log( "M3T headlight: %s row %d: %10.4f %10.4f %10.4f %10.4f", name, row, m[row * 4], m[row * 4 + 1], m[row * 4 + 2], m[row * 4 + 3] );
            }
        };

        log_matrix( "camera (param 3)", matrix );
        log_matrix( "light   0061BCCC", reinterpret_cast<const float *>(0x0061BCCC) );
        log_matrix( "second  0061E184", reinterpret_cast<const float *>(0x0061E184) );

        // Input positions (stride 16), light space (DAT_0061d178, stride 16), factor (DAT_0061bd60).
        const auto * light = reinterpret_cast<const float *>(0x0061D178);
        const auto * factor = reinterpret_cast<const float *>(0x0061BD60);

        for (int i = 0; i < count && i < 12; ++i)
        {
            Log( "M3T headlight: v%-2d in %9.3f %9.3f %9.3f  light %9.3f %9.3f %9.3f  factor %.4f", i, positions[i * 4], positions[i * 4 + 1],
                positions[i * 4 + 2], light[i * 4], light[i * 4 + 1], light[i * 4 + 2], factor[i] );
        }

        // The 128 x 128 byte light map, once per run, as a grey BMP next to the log.
        const auto * map = reinterpret_cast<const std::uint8_t *>(u( 0x0061E1CC ));
        if (m_HeadlightMapDumped || map == nullptr)
            return;

        std::uint8_t copy[128 * 128];
        if (!SafeCopy( map, copy, sizeof( copy ) ))
        {
            Log( "M3T headlight: map not readable" );
            return;
        }

        m_HeadlightMapDumped = true;

        int low = 255, high = 0, used = 0;
        for (const std::uint8_t value : copy)
        {
            low = std::min<int>( low, value );
            high = std::max<int>( high, value );
            used += value != 0 ? 1 : 0;
        }

        Log( "M3T headlight: map values %d..%d, %d of 16384 non-zero (row = forward iz, column = side ix + 64)", low, high, used );

        const std::size_t slash = m_LogPath.find_last_of( "\\/" );
        const std::string path = (slash != std::string::npos ? m_LogPath.substr( 0, slash + 1 ) : std::string()) + "m3t_headlight_map_128x128.bmp";

        if (FILE * file = std::fopen( path.c_str(), "wb" ))
        {
            BITMAPFILEHEADER file_header = {};
            BITMAPINFOHEADER info_header = {};
            file_header.bfType = 0x4D42;
            file_header.bfOffBits = sizeof( file_header ) + sizeof( info_header );
            file_header.bfSize = file_header.bfOffBits + 128 * 128 * 3;
            info_header.biSize = sizeof( info_header );
            info_header.biWidth = 128;
            info_header.biHeight = -128;
            info_header.biPlanes = 1;
            info_header.biBitCount = 24;
            info_header.biCompression = BI_RGB;
            std::fwrite( &file_header, sizeof( file_header ), 1, file );
            std::fwrite( &info_header, sizeof( info_header ), 1, file );

            std::uint8_t row[128 * 3];
            for (int y = 0; y < 128; ++y)
            {
                for (int x = 0; x < 128; ++x)
                {
                    row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = copy[y * 128 + x];
                }
                std::fwrite( row, 1, sizeof( row ), file );
            }

            std::fclose( file );
            Log( "M3T headlight: map dumped to m3t_headlight_map_128x128.bmp" );
        }

        // Raw bytes too, for offline recomputation.
        const std::string raw = (slash != std::string::npos ? m_LogPath.substr( 0, slash + 1 ) : std::string()) + "m3t_headlight_map_128x128.raw";
        if (FILE * file = std::fopen( raw.c_str(), "wb" ))
        {
            std::fwrite( copy, 1, sizeof( copy ), file );
            std::fclose( file );
        }
    }


    void Driver::NoteRoadDraw(
        void * renderer,
        const void * positions,
        const void * second,
        const void * third,
        std::uint32_t count
    )
    {
        if (!m_MeasureSnapActive || m_WatchDoneThisSnap || m_WatchArmed || m_Bound[0] == nullptr || m_Bound[0]->m_Hash != ROAD_TEXTURE ||
            m_BatchVertices.empty())
            return;

        m_WatchDoneThisSnap = true;

        const std::uint32_t diffuse = m_BatchVertices.back().m_Diffuse;
        const void * streams[3] = { positions, second, third };
        Log( "M3T watch: road draw, count %u, last diffuse %08X, streams %p %p %p", count, diffuse, positions, second, third );

        std::uintptr_t found = 0;
        const std::size_t limit = std::min<std::size_t>( count > 0 ? count : 1024, 4096 ) * 8;

        for (std::size_t s = 0; s < 3; ++s)
        {
            if (streams[s] == nullptr)
                continue;

            std::uint32_t head[8] = {};
            if (SafeCopy( streams[s], head, sizeof( head ) ))
            {
                Log( "M3T watch:   stream %zu: %08X %08X %08X %08X %08X %08X %08X %08X", s + 1, head[0], head[1], head[2], head[3], head[4],
                    head[5], head[6], head[7] );
            }

            std::vector<std::uint32_t> data( limit );
            if (found != 0 || !SafeCopy( streams[s], data.data(), data.size() * sizeof( std::uint32_t ) ))
                continue;

            for (std::size_t i = 0; i < data.size(); ++i)
            {
                if ((data[i] & 0x00FFFFFF) == (diffuse & 0x00FFFFFF))
                {
                    found = reinterpret_cast<std::uintptr_t>(streams[s]) + i * 4;
                    Log( "M3T watch: diffuse found in stream %zu at +%zX (dword %zu)", s + 1, i * 4, i );
                    break;
                }
            }
        }

        // FUN_00543ae0 copies the colours into its own vertex array (32 bytes per vertex from +0x434,
        // diffuse at +0x444) and hands that array to the driver. Watch the first diffuse there.
        const std::uintptr_t array = reinterpret_cast<std::uintptr_t>(renderer) + 0x444;
        std::uint32_t copied[2] = {};
        SafeCopy( reinterpret_cast<const void *>(array), copied, sizeof( copied ) );
        Log( "M3T watch: renderer %p, vertex 0 diffuse %08X specular %08X%s", renderer, copied[0], copied[1],
            found != 0 ? "" : " (last diffuse not in the source streams)" );
        found = array;

        if (found == 0)
            return;

        g_WatchAddress = found;
        g_WatchHitCount = 0;
        g_WatchTotal = 0;
        g_WatchUntil = ::GetTickCount64() + 1000;

        if (SetWatch( found, true ))
        {
            m_WatchArmed = true;
            Log( "M3T watch: armed on %08X for 1 s", static_cast<unsigned>(found) );
        }
        else
        {
            Log( "M3T watch: arming failed" );
        }
    }


    void Driver::CheckWatch()
    {
        if (!m_WatchArmed || ::GetTickCount64() < g_WatchUntil + 250)
            return;

        SetWatch( 0, false );
        m_WatchArmed = false;

        Log( "M3T watch report: %ld writes to %08X, %ld writing places", static_cast<long>(g_WatchTotal), static_cast<unsigned>(g_WatchAddress),
            static_cast<long>(g_WatchHitCount) );

        for (LONG i = 0; i < g_WatchHitCount; ++i)
        {
            const WatchHit & hit = g_WatchHits[static_cast<std::size_t>(i)];
            char frames[7][48] = {};
            DescribeAddress( reinterpret_cast<void *>(hit.m_Eip), frames[0], sizeof( frames[0] ) );

            for (std::size_t f = 0; f < hit.m_Stack.size(); ++f)
            {
                if (hit.m_Stack[f] != 0)
                {
                    DescribeAddress( reinterpret_cast<void *>(hit.m_Stack[f]), frames[f + 1], sizeof( frames[f + 1] ) );
                }
            }

            Log( "M3T   write after %s (value %08X): %u times, from %s < %s < %s < %s < %s < %s", frames[0], hit.m_Value, hit.m_Count,
                frames[1], frames[2], frames[3], frames[4], frames[5], frames[6] );
        }
    }


    void Driver::NoteLightNode(
        void * node
    )
    {
        if (!m_MeasureSnapActive)
            return;

        ++m_MeasureLightCalls;
        m_LitSinceDraw = true;

        // Same access as the node itself: stream 10 holds the light list, count first,
        // vectors at +1, types at +0x21, colours at +0x2A (4 floats each).
        const int stream = reinterpret_cast<NodeStreamFn>(NODE_STREAM)(node, 10, 0);
        if (stream == 0)
            return;

        const auto table = *reinterpret_cast<const std::uintptr_t *>(*reinterpret_cast<const std::uintptr_t *>(stream + 0x20) + 8);
        const auto * lights = *reinterpret_cast<const int * const *>(table + 4 + static_cast<std::uintptr_t>(*reinterpret_cast<const int *>(stream + 0x1C)) * 8);
        if (lights == nullptr)
            return;

        MeasureLights entry = {};
        entry.m_Count = std::clamp( lights[0], 0, 8 );
        const auto * floats = reinterpret_cast<const float *>(lights);
        std::string key = std::to_string( entry.m_Count );

        for (std::size_t i = 0; i < static_cast<std::size_t>(entry.m_Count); ++i)
        {
            entry.m_Types[i] = lights[0x21 + i];
            for (std::size_t c = 0; c < 4; ++c)
            {
                entry.m_Vectors[i][c] = floats[1 + i * 4 + c];
                entry.m_Colours[i][c] = floats[0x2A + i * 4 + c];
            }

            char part[160];
            std::snprintf( part, sizeof( part ), "|%d %.1f %.1f %.1f %.2f %.2f %.2f", entry.m_Types[i], entry.m_Vectors[i][0],
                entry.m_Vectors[i][1], entry.m_Vectors[i][2], entry.m_Colours[i][0], entry.m_Colours[i][1], entry.m_Colours[i][2] );
            key += part;
        }

        auto & slot = m_MeasureLights[key];
        if (slot.m_Calls == 0)
        {
            slot = entry;
        }

        ++slot.m_Calls;
    }


    void Driver::NoteCaller(
        void * address
    )
    {
        if (!m_MeasureSnapActive)
            return;

        // The direct return address is exact. Further entries are stack values that point into the
        // game's code right after a call instruction, the usual heuristic without frame pointers.
        MeasureCaller caller = {};
        caller.m_Stack[0] = address;

        const auto after_call = LooksLikeReturn;

        const auto * stack = static_cast<const std::uintptr_t *>(_AddressOfReturnAddress());
        const auto top = reinterpret_cast<const std::uintptr_t *>(reinterpret_cast<NT_TIB *>(::NtCurrentTeb())->StackBase);
        std::size_t used = 1;
        bool past = false;

        for (std::size_t i = 0; i < 2048 && stack + i < top && used < caller.m_Stack.size(); ++i)
        {
            const std::uintptr_t value = stack[i];

            if (!past)
            {
                past = value == reinterpret_cast<std::uintptr_t>(address);
                continue;
            }

            if (after_call( value ) && value != reinterpret_cast<std::uintptr_t>(caller.m_Stack[used - 1]))
            {
                caller.m_Stack[used++] = reinterpret_cast<void *>(value);
            }
        }

        const auto & render = m_D3D.m_Render;
        caller.m_Hash = m_Bound[0] != nullptr ? m_Bound[0]->m_Hash : 0u;
        caller.m_Blend = render[rs::ALPHABLENDENABLE] != 0;
        caller.m_Src = caller.m_Blend ? render[rs::SRCBLEND] : 0u;
        caller.m_Dst = caller.m_Blend ? render[rs::DESTBLEND] : 0u;
        caller.m_Lit = m_LitSinceDraw;
        m_LitSinceDraw = false;

        char key[128];
        std::snprintf( key, sizeof( key ), "%p %p %p %p %p %p %08X %d %u %u %d", caller.m_Stack[0], caller.m_Stack[1], caller.m_Stack[2],
            caller.m_Stack[3], caller.m_Stack[4], caller.m_Stack[5], caller.m_Hash, caller.m_Blend ? 1 : 0, caller.m_Src, caller.m_Dst,
            caller.m_Lit ? 1 : 0 );

        auto & entry = m_MeasureCallers[key];
        if (entry.m_Calls == 0)
        {
            entry = caller;
        }

        ++entry.m_Calls;
    }


    void Driver::MeasureSnapBatch()
    {
        if (m_BatchPrimitive != Primitive::TRIANGLES)
            return;

        const auto & render = m_D3D.m_Render;
        const bool blend = render[rs::ALPHABLENDENABLE] != 0;
        const Texture * texture = m_Bound[0];

        const std::uint32_t cull = render[rs::CULLMODE];

        // B15: list of the game's render queue being drawn, from LFSP (lfsp.asi v7f); -1 = not available.
        using QueueListFn = int( * )();
        static QueueListFn queue_list_fn = nullptr;
        static bool queue_list_asked = false;

        if (!queue_list_asked)
        {
            queue_list_asked = true;

            if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                queue_list_fn = reinterpret_cast<QueueListFn>(reinterpret_cast<void *>(::GetProcAddress( lfsp, "LfspQueueList" )));

            Log( "M3T queue list from LFSP %s", queue_list_fn != nullptr ? "found" : "not available" );
        }

        const int list = queue_list_fn != nullptr ? queue_list_fn() : -1;

        char key[64];
        std::snprintf( key, sizeof( key ), "%p %d %u %u %u %d", static_cast<const void *>(texture), blend ? 1 : 0,
            blend ? render[rs::SRCBLEND] : 0u, blend ? render[rs::DESTBLEND] : 0u, cull, list );

        auto & snap = m_MeasureSnap[key];
        snap.m_Texture = texture;
        snap.m_Blend = blend;
        snap.m_Src = render[rs::SRCBLEND];
        snap.m_Dst = render[rs::DESTBLEND];
        snap.m_Cull = cull;
        snap.m_List = list;
        ++snap.m_Draws;
        snap.m_Vertices += m_BatchVertices.size();

        for (const TlVertex & v : m_BatchVertices)
        {
            snap.m_MinX = std::min( snap.m_MinX, v.m_X );
            snap.m_MinY = std::min( snap.m_MinY, v.m_Y );
            snap.m_MaxX = std::max( snap.m_MaxX, v.m_X );
            snap.m_MaxY = std::max( snap.m_MaxY, v.m_Y );
        }

        // Colour weighted by the screen area of each triangle, so a lit wall counts as much as it covers.
        for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
        {
            const TlVertex & a = m_BatchVertices[m_BatchIndices[i]];
            const TlVertex & b = m_BatchVertices[m_BatchIndices[i + 1]];
            const TlVertex & c = m_BatchVertices[m_BatchIndices[i + 2]];
            const double signed_area = ((b.m_X - a.m_X) * (c.m_Y - a.m_Y) - (c.m_X - a.m_X) * (b.m_Y - a.m_Y)) * 0.5;
            const double area = std::abs( signed_area );

            // Screen y points down, so a positive cross product is clockwise on screen.
            if (signed_area > 0.0)
            {
                snap.m_AreaCw += area;
            }

            for (int channel = 0; channel < 3; ++channel)
            {
                const int shift = 16 - channel * 8;
                const double mean = (((a.m_Diffuse >> shift) & 0xFF) + ((b.m_Diffuse >> shift) & 0xFF) + ((c.m_Diffuse >> shift) & 0xFF)) / 3.0;
                snap.m_Colour[static_cast<std::size_t>(channel)] += area * mean;
            }

            snap.m_Area += area;
        }
    }


    void Driver::MeasureSnapReport()
    {
        ++m_MeasureSnapCount;

        std::vector<const MeasureSnap *> snaps;
        double area = 0.0;
        std::array<double, 3> colour = {};

        for (const auto & [key, snap] : m_MeasureSnap)
        {
            snaps.push_back( &snap );

            if (!snap.m_Blend)
            {
                area += snap.m_Area;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    colour[c] += snap.m_Colour[c];
                }
            }
        }

        std::sort( snaps.begin(), snaps.end(), [] ( const MeasureSnap * a, const MeasureSnap * b )
        {
            return a->m_Area > b->m_Area;
        } );

        Log( "M3T snapshot #%d at flip %u, %.1f s: %zu draw groups, opaque area %.0f px, opaque diffuse %.1f %.1f %.1f",
            m_MeasureSnapCount, m_FlipCount, MeasureSeconds(), snaps.size(), area,
            area > 0.0 ? colour[0] / area : 0.0, area > 0.0 ? colour[1] / area : 0.0, area > 0.0 ? colour[2] / area : 0.0 );

        constexpr std::size_t MAX_LINES = 80;

        for (std::size_t i = 0; i < snaps.size() && i < MAX_LINES; ++i)
        {
            const MeasureSnap & snap = *snaps[i];
            const Texture * texture = snap.m_Texture;
            const double a = snap.m_Area > 0.0 ? snap.m_Area : 1.0;

            Log( "M3T   snap #%d tex %08X %dx%d blend %d src %u dst %u cull %u list %d: %llu batches, %llu vertices, area %.0f px (cw %.0f%%), "
                "diffuse %.1f %.1f %.1f, rect %.0f,%.0f-%.0f,%.0f",
                m_MeasureSnapCount, texture != nullptr ? texture->m_Hash : 0u, texture != nullptr ? texture->m_Width : 0,
                texture != nullptr ? texture->m_Height : 0, snap.m_Blend ? 1 : 0, snap.m_Blend ? snap.m_Src : 0u, snap.m_Blend ? snap.m_Dst : 0u,
                snap.m_Cull, snap.m_List, static_cast<unsigned long long>(snap.m_Draws), static_cast<unsigned long long>(snap.m_Vertices), snap.m_Area,
                100.0 * snap.m_AreaCw / a, snap.m_Colour[0] / a, snap.m_Colour[1] / a, snap.m_Colour[2] / a,
                snap.m_MinX, snap.m_MinY, snap.m_MaxX, snap.m_MaxY );

            // B15: pictures of the largest textures of the frame, to match the mirrored copy against the original.
            if (i < 40)
            {
                MeasureDump( texture );
            }
        }

        m_MeasureSnap.clear();

        // Who drew what, addresses as Ghidra shows them (porsche.exe at 0x400000, gimme.dll at 0x10000000).
        const auto describe = DescribeAddress;

        std::vector<const MeasureCaller *> callers;

        for (const auto & [key, caller] : m_MeasureCallers)
        {
            callers.push_back( &caller );
        }

        std::sort( callers.begin(), callers.end(), [] ( const MeasureCaller * a, const MeasureCaller * b )
        {
            return a->m_Calls > b->m_Calls;
        } );

        constexpr std::size_t MAX_CALLERS = 400;

        for (std::size_t i = 0; i < callers.size() && i < MAX_CALLERS; ++i)
        {
            const MeasureCaller & caller = *callers[i];
            char frames[6][48] = {};

            for (std::size_t f = 0; f < caller.m_Stack.size(); ++f)
            {
                if (caller.m_Stack[f] != nullptr)
                {
                    describe( caller.m_Stack[f], frames[f], sizeof( frames[f] ) );
                }
            }

            Log( "M3T   caller #%d tex %08X blend %d src %u dst %u lit %d: %llu calls, from %s < %s < %s < %s < %s < %s", m_MeasureSnapCount,
                caller.m_Hash, caller.m_Blend ? 1 : 0, caller.m_Src, caller.m_Dst, caller.m_Lit ? 1 : 0,
                static_cast<unsigned long long>(caller.m_Calls), frames[0],
                frames[1], frames[2], frames[3], frames[4], frames[5] );
        }

        m_MeasureCallers.clear();

        // Light lists of the lighting node, PLAN.md 9.4c.
        Log( "M3T   light node: %llu calls, %zu distinct light lists", static_cast<unsigned long long>(m_MeasureLightCalls), m_MeasureLights.size() );

        for (const auto & [key, lights] : m_MeasureLights)
        {
            Log( "M3T   lights #%d: %d lights, %llu calls", m_MeasureSnapCount, lights.m_Count, static_cast<unsigned long long>(lights.m_Calls) );

            for (std::size_t i = 0; i < static_cast<std::size_t>(lights.m_Count); ++i)
            {
                const auto & v = lights.m_Vectors[i];
                const auto & c = lights.m_Colours[i];
                Log( "M3T     light %zu: type %d, vector %.3f %.3f %.3f %.3f, colour %.3f %.3f %.3f %.3f", i, lights.m_Types[i],
                    v[0], v[1], v[2], v[3], c[0], c[1], c[2], c[3] );
            }
        }

        m_MeasureLights.clear();
        m_MeasureLightCalls = 0;
    }


    void Driver::MeasureDump(
        const Texture * texture
    )
    {
        constexpr std::size_t MAX_DUMPS = 64;

        if (texture == nullptr || !texture->m_Texture || formats::IsBlockCompressed( texture->m_Format ) ||
            m_MeasureDumps.size() >= MAX_DUMPS || !m_MeasureDumps.emplace( texture->m_Hash, true ).second)
            return;

        // Top level through a staging copy, the textures live on the GPU only.
        D3D11_TEXTURE2D_DESC desc = {};
        texture->m_Texture->GetDesc( &desc );
        desc.MipLevels = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, staging.Put() ) ))
            return;

        m_Context->CopySubresourceRegion( staging.Get(), 0, 0, 0, 0, texture->m_Texture.Get(), 0, nullptr );

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( staging.Get(), 0, D3D11_MAP_READ, 0, &mapped ) ))
            return;

        const std::size_t slash = m_LogPath.find_last_of( "\\/" );
        const std::string folder = slash != std::string::npos ? m_LogPath.substr( 0, slash + 1 ) : std::string();
        const int width = static_cast<int>(desc.Width);
        const int height = static_cast<int>(desc.Height);

        // 24 bit top-down BMP, once with the colour and once with alpha as grey.
        const auto write = [&] ( const char * suffix, bool alpha )
        {
            char name[64];
            std::snprintf( name, sizeof( name ), "m3t_%08X_%dx%d%s.bmp", texture->m_Hash, width, height, suffix );

            FILE * file = std::fopen( (folder + name).c_str(), "wb" );
            if (file == nullptr)
                return;

            const std::uint32_t row_bytes = (static_cast<std::uint32_t>(width) * 3 + 3) & ~3u;

            BITMAPFILEHEADER file_header = {};
            BITMAPINFOHEADER info_header = {};
            file_header.bfType = 0x4D42;
            file_header.bfOffBits = sizeof( file_header ) + sizeof( info_header );
            file_header.bfSize = file_header.bfOffBits + row_bytes * static_cast<std::uint32_t>(height);
            info_header.biSize = sizeof( info_header );
            info_header.biWidth = width;
            info_header.biHeight = -height;
            info_header.biPlanes = 1;
            info_header.biBitCount = 24;
            info_header.biCompression = BI_RGB;

            std::fwrite( &file_header, sizeof( file_header ), 1, file );
            std::fwrite( &info_header, sizeof( info_header ), 1, file );

            std::vector<std::uint8_t> row( row_bytes, 0 );

            for (int y = 0; y < height; ++y)
            {
                const auto * texels = reinterpret_cast<const std::uint8_t *>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;

                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t * bgra = texels + static_cast<std::size_t>(x) * 4;
                    for (int c = 0; c < 3; ++c)
                    {
                        row[static_cast<std::size_t>(x) * 3 + static_cast<std::size_t>(c)] = alpha ? bgra[3] : bgra[c];
                    }
                }

                std::fwrite( row.data(), 1, row.size(), file );
            }

            std::fclose( file );
            Log( "M3T   dump %s", name );
        };

        write( "", false );
        write( "_alpha", true );

        m_Context->Unmap( staging.Get(), 0 );
    }


    // ---- Water reflections, doc/BUGS.md B15 §12 (way E) ----

    int Driver::QueryQueueList()
    {
        using QueueListFn = int( * )();
        static QueueListFn fn = nullptr;
        static bool asked = false;

        if (!asked)
        {
            asked = true;

            if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                fn = reinterpret_cast<QueueListFn>(reinterpret_cast<void *>(::GetProcAddress( lfsp, "LfspQueueList" )));

            Log( "Water: queue list from LFSP %s", fn != nullptr ? "found" : "not available (lfsp.asi missing or older than v7f), no reflections" );
        }

        return fn != nullptr ? fn() : -1;
    }


    void Driver::NoteQueueList()
    {
        if (!m_ConfigReflections || !m_MirrorVs || !m_ReflCompositePs)
            return;

        const int list = std::max( QueryQueueList(), 0 );

        if (list == m_QueueList)
            return;

        // The pending batch still belongs to the old list.
        Flush();
        const int previous = m_QueueList;
        m_QueueList = list;

        // Clip and camera belong to the view of this queue run: at its end the game may already have
        // switched to the next view (rear view mirror after the main view).
        if (previous == 0 && list > 0)
        {
            m_ReflSkyRun = false;
            if (m_ReflSkyPhase)
            {
                m_ReflSkyPhase = false;
                m_ReflSkyCount = m_ReflUsed;
                m_ReflSkyRun = m_ConfigReflectionSky != 0;
            }
            m_ReflShoreDraws = 0;
            PrepareReflWeight();
            m_ReflClip = ScaledClip();
            m_ReflCameraValid = ReadCamera( m_ReflClipOfView.data(), m_ReflViewOfClip.data(), m_ReflViewport.data() );
        }

        if (previous > 0 && list == 0)
            DrawReflections();
    }


    void Driver::PrepareReflWeight()
    {
        if (m_ConfigShoreTextures.empty() || m_RenderWidth <= 0 || m_RenderHeight <= 0)
            return;

        D3D11_TEXTURE2D_DESC have = {};
        if (m_ReflWeight)
            m_ReflWeight->GetDesc( &have );

        if (!m_ReflWeight || have.Width != static_cast<UINT>(m_RenderWidth) || have.Height != static_cast<UINT>(m_RenderHeight))
        {
            m_ReflWeightRtv.Reset();
            m_ReflWeightSrv.Reset();
            m_ReflWeight.Reset();
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(m_RenderWidth);
            desc.Height = static_cast<UINT>(m_RenderHeight);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflWeight.Put() ) ) ||
                FAILED( m_Device->CreateRenderTargetView( m_ReflWeight.Get(), nullptr, m_ReflWeightRtv.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_ReflWeight.Get(), nullptr, m_ReflWeightSrv.Put() ) ))
            {
                m_ReflWeightRtv.Reset();
                m_ReflWeightSrv.Reset();
                m_ReflWeight.Reset();
                return;
            }
        }

        if (!m_ReflWeightBlend)
        {
            // weight = weight * (1 - alpha), the target starts at 1.
            D3D11_BLEND_DESC desc = {};
            auto & target = desc.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_ZERO;
            target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ZERO;
            target.DestBlendAlpha = D3D11_BLEND_ONE;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
            m_Device->CreateBlendState( &desc, m_ReflWeightBlend.Put() );
        }

        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        m_Context->ClearRenderTargetView( m_ReflWeightRtv.Get(), one );
    }


    bool Driver::PrepareSkyImage()
    {
        if (m_RenderWidth <= 0 || m_RenderHeight <= 0)
            return false;

        D3D11_TEXTURE2D_DESC have = {};
        if (m_ReflSky)
            m_ReflSky->GetDesc( &have );

        if (!m_ReflSky || have.Width != static_cast<UINT>(m_RenderWidth) || have.Height != static_cast<UINT>(m_RenderHeight))
        {
            m_ReflSkyRtv.Reset();
            m_ReflSkyView.Reset();
            m_ReflSky.Reset();
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(m_RenderWidth);
            desc.Height = static_cast<UINT>(m_RenderHeight);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflSky.Put() ) ) ||
                FAILED( m_Device->CreateRenderTargetView( m_ReflSky.Get(), nullptr, m_ReflSkyRtv.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_ReflSky.Get(), nullptr, m_ReflSkyView.Put() ) ))
            {
                m_ReflSky.Reset();
                m_ReflSkyRtv.Reset();
                m_ReflSkyView.Reset();
                return false;
            }
        }
        return true;
    }


    bool Driver::CopyFrame()
    {
        if (!m_Frame)
            return false;

        D3D11_TEXTURE2D_DESC desc = {};
        m_Frame->GetDesc( &desc );

        D3D11_TEXTURE2D_DESC have = {};
        if (m_ReflFrameCopy)
            m_ReflFrameCopy->GetDesc( &have );

        if (!m_ReflFrameCopy || have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format)
        {
            m_ReflFrameCopyView.Reset();
            m_ReflFrameCopy.Reset();
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflFrameCopy.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_ReflFrameCopy.Get(), nullptr, m_ReflFrameCopyView.Put() ) ))
            {
                m_ReflFrameCopy.Reset();
                m_ReflFrameCopyView.Reset();
                return false;
            }
            m_Frame->GetDesc( &desc );
        }

        if (desc.SampleDesc.Count > 1)
            m_Context->ResolveSubresource( m_ReflFrameCopy.Get(), 0, m_Frame.Get(), 0, desc.Format );
        else
            m_Context->CopyResource( m_ReflFrameCopy.Get(), m_Frame.Get() );
        return true;
    }


    bool Driver::CopyStencil()
    {
        if (!m_Depth)
            return false;

        D3D11_TEXTURE2D_DESC desc = {};
        m_Depth->GetDesc( &desc );

        D3D11_TEXTURE2D_DESC have = {};
        if (m_ReflStencil)
            m_ReflStencil->GetDesc( &have );

        if (!m_ReflStencil || have.Width != desc.Width || have.Height != desc.Height || have.SampleDesc.Count != desc.SampleDesc.Count)
        {
            m_ReflStencilView.Reset();
            m_ReflDepthCopyView.Reset();
            m_ReflStencil.Reset();
            // Same typeless group as D24S8, so the whole depth buffer copies over; read as stencil and as depth.
            desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
            view.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
            if (desc.SampleDesc.Count > 1)
            {
                view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MipLevels = 1;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC depth_view = view;
            depth_view.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflStencil.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_ReflStencil.Get(), &view, m_ReflStencilView.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_ReflStencil.Get(), &depth_view, m_ReflDepthCopyView.Put() ) ))
            {
                m_ReflStencil.Reset();
                m_ReflStencilView.Reset();
                m_ReflDepthCopyView.Reset();
                Log( "Water: depth and stencil copy %ux%u x%u failed", desc.Width, desc.Height, desc.SampleDesc.Count );
                return false;
            }
        }

        m_Context->CopyResource( m_ReflStencil.Get(), m_Depth.Get() );
        return true;
    }


    bool Driver::IsWaterTexture() const
    {
        const Texture * texture = m_Bound[0];

        if (texture == nullptr)
            return false;

        return std::find( m_ConfigWaterTextures.begin(), m_ConfigWaterTextures.end(), texture->m_Hash ) != m_ConfigWaterTextures.end() ||
            std::find( m_ConfigFarWaterTextures.begin(), m_ConfigFarWaterTextures.end(), texture->m_Hash ) != m_ConfigFarWaterTextures.end();
    }

    bool Driver::IsFarWaterTexture() const
    {
        const Texture * texture = m_Bound[0];
        return texture != nullptr &&
            std::find( m_ConfigFarWaterTextures.begin(), m_ConfigFarWaterTextures.end(), texture->m_Hash ) != m_ConfigFarWaterTextures.end();
    }

    bool Driver::IsBakedTexture() const
    {
        const Texture * texture = m_Bound[0];

        if (texture == nullptr)
            return false;

        return std::find( m_ConfigBakedTextures.begin(), m_ConfigBakedTextures.end(), texture->m_Hash ) != m_ConfigBakedTextures.end();
    }


    bool Driver::ReadCamera(
        float clip_of_view[16],
        float view_of_clip[16],
        float viewport[4]
    )
    {
        const std::uintptr_t renderer = *reinterpret_cast<const std::uintptr_t *>(0x0069E0D4);
        std::uintptr_t camera = 0;
        float projection_matrix[16] = {};

        if (renderer == 0 || !SafeCopy( reinterpret_cast<const void *>(renderer), &camera, 4 ) || camera == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(camera + 0x148), projection_matrix, sizeof( projection_matrix ) ) ||
            !SafeCopy( reinterpret_cast<const void *>(renderer + 0x2C), viewport, 8 ) ||
            !SafeCopy( reinterpret_cast<const void *>(renderer + 0x38), viewport + 2, 8 ) || viewport[0] == 0.0f || viewport[1] == 0.0f)
            return false;

        // w from column 3 at night, column 2 by day (doc/LICHT.md 7.12), as PrepareHeadMesh.
        const bool affine = std::abs( projection_matrix[3] ) < 1e-6f && std::abs( projection_matrix[7] ) < 1e-6f &&
            std::abs( projection_matrix[11] ) < 1e-6f;
        const int w = affine ? 2 : 3;
        const float matrix[16] = {
            projection_matrix[0], projection_matrix[1], projection_matrix[w], 0.0f,
            projection_matrix[4], projection_matrix[5], projection_matrix[4 + w], 0.0f,
            projection_matrix[8], projection_matrix[9], projection_matrix[8 + w], 0.0f,
            projection_matrix[12], projection_matrix[13], projection_matrix[12 + w], 1.0f };
        std::memcpy( clip_of_view, matrix, sizeof( matrix ) );
        return InvertAffine( clip_of_view, view_of_clip );
    }


    void Driver::CaptureMirrorBatch()
    {
        // Bounded per queue run, a frame of the game stays far below.
        constexpr std::size_t MAX_BATCHES = 8192;

        if (m_ReflUsed >= MAX_BATCHES || m_ReflConstants.empty())
            return;

        const std::uint32_t hash = m_Bound[0] != nullptr ? m_Bound[0]->m_Hash : 0;
        if (std::find( m_ConfigReflectionSkip.begin(), m_ConfigReflectionSkip.end(), hash ) != m_ConfigReflectionSkip.end())
            return;

        if (m_ReflUsed == m_ReflBatches.size())
            m_ReflBatches.emplace_back();

        MirrorBatch & batch = m_ReflBatches[m_ReflUsed++];
        const auto & render = m_D3D.m_Render;
        batch.m_Vertices.assign( m_BatchVertices.begin(), m_BatchVertices.end() );
        batch.m_Indices.assign( m_BatchIndices.begin(), m_BatchIndices.end() );
        batch.m_Constants = m_ReflConstants;
        batch.m_BlendKey = render[rs::ALPHABLENDENABLE] != 0 ? 1u : 0u;
        batch.m_DepthWrite = render[rs::ZWRITEENABLE] != 0;
        batch.m_Cull = render[rs::CULLMODE];
        batch.m_Hash = hash;

        for (int stage = 0; stage < 2; ++stage)
        {
            const Texture * texture = m_Bound[static_cast<std::size_t>(stage)];
            batch.m_Views[static_cast<std::size_t>(stage)] = texture != nullptr ? texture->m_View.Get() : nullptr;
            batch.m_Samplers[static_cast<std::size_t>(stage)] = GetSampler( stage );
        }
    }


    void Driver::DrawReflections()
    {
        const std::size_t batches = m_ReflUsed;
        const std::size_t water = m_ReflWater.size() / 3;
        m_ReflUsed = m_ReflSkyCount; // the sky stays for the next view of the frame

        struct ClearWater
        {
            std::vector<std::array<float, 3>> & m_Water;
            std::vector<std::uint32_t> & m_Hash;
            ~ClearWater() { m_Water.clear(); m_Hash.clear(); }
        } clear_water{ m_ReflWater, m_ReflWaterHash };

        if (batches == 0 || water == 0 || !m_FrameView || !m_DepthView)
        {
            if (m_ReflTrace > 0)
                Log( "Water: queue end at flip %u without reflection: %zu draws, %zu water triangles, %u shore draws", m_FlipCount, batches, water,
                    m_ReflShoreDraws );
            return;
        }

        ++m_ReflFrames;

        float clip_of_view[16] = {};
        float view_of_clip[16] = {};
        float viewport[4] = {};
        std::memcpy( clip_of_view, m_ReflClipOfView.data(), sizeof( clip_of_view ) );
        std::memcpy( view_of_clip, m_ReflViewOfClip.data(), sizeof( view_of_clip ) );
        std::memcpy( viewport, m_ReflViewport.data(), sizeof( viewport ) );

        if (!m_ReflCameraValid)
        {
            if (m_ReflLogged < 3)
            {
                ++m_ReflLogged;
                Log( "Water: camera not readable, no reflection" );
            }
            return;
        }

        // Water plane in view space from the water triangles (Newell normal, centroid).
        const auto to_view = [&]( const std::array<float, 3> & p, double out[3] )
        {
            const double w = p[2];
            const double clip[4] = { (p[0] - viewport[2]) * w / viewport[0], (p[1] - viewport[3]) * w / viewport[1], w, 1.0 };
            for (int j = 0; j < 3; ++j)
                out[j] = clip[0] * view_of_clip[j] + clip[1] * view_of_clip[4 + j] + clip[2] * view_of_clip[8 + j] + clip[3] * view_of_clip[12 + j];
        };

        double normal[3] = {};
        double centre[3] = {};
        double total_weight = 0.0;

        // Far end of the water surface (largest w) for '[Water] FarFade', main view only, smoothed.
        if (m_ReflSkyRun)
        {
            float farthest = 0.0f;
            for (const auto & corner : m_ReflWater)
                farthest = std::max( farthest, corner[2] );
            m_ReflWaterFar = m_ReflWaterFar <= 0.0f ? farthest : m_ReflWaterFar * 0.9f + farthest * 0.1f;
        }

        for (std::size_t t = 0; t < water; ++t)
        {
            double a[3], b[3], c[3];
            to_view( m_ReflWater[t * 3], a );
            to_view( m_ReflWater[t * 3 + 1], b );
            to_view( m_ReflWater[t * 3 + 2], c );
            const double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const double v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            const double cross[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };

            // All triangles the same way round: towards the camera (the origin).
            const double towards = -(cross[0] * a[0] + cross[1] * a[1] + cross[2] * a[2]);
            const double size = std::sqrt( cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2] );
            if (size < 1e-12)
                continue;

            // Near water counts most: far, huge triangles let small errors tilt the plane and move the whole
            // reflection (B15, jumps between frames). Unit normal and centre, weighted by 1 / distance².
            const double mid[3] = { (a[0] + b[0] + c[0]) / 3.0, (a[1] + b[1] + c[1]) / 3.0, (a[2] + b[2] + c[2]) / 3.0 };
            const double distance2 = mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2];
            const double weight = 1.0 / std::max( distance2, 1.0 );
            const double sign = (towards < 0.0 ? -1.0 : 1.0) * weight / size;

            for (int j = 0; j < 3; ++j)
            {
                normal[j] += sign * cross[j];
                centre[j] += mid[j] * weight;
            }
            total_weight += weight;
        }

        const double length = std::sqrt( normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2] );

        if (length < 1e-9 || total_weight <= 0.0)
            return;

        float plane[4] = {};
        for (int j = 0; j < 3; ++j)
        {
            plane[j] = static_cast<float>(normal[j] / length);
            centre[j] /= total_weight;
        }
        plane[3] = static_cast<float>(-(plane[0] * centre[0] + plane[1] * centre[1] + plane[2] * centre[2]));

        if (plane[3] <= 0.0f)
            return; // camera under the water plane

        // Scroll Lock with '[Dx11] Measure' records the next 120 reflections (B15, jumping between frames).
        if (m_MeasureSnapActive && m_ReflTrace == 0)
            m_ReflTrace = 120;
        const bool trace = m_ReflTrace > 0;
        if (trace)
            --m_ReflTrace;

        if (m_ReflLogged < 6 || trace)
        {
            ++m_ReflLogged;
            Log( "Water: reflection #%llu flip %u, %zu draws, %zu water triangles, %u shore draws, sky %d, plane %.3f %.3f %.3f %.2f, viewport %.1f %.1f %.1f %.1f, clip %ld %ld %ld %ld",
                static_cast<unsigned long long>(m_ReflFrames), m_FlipCount, batches, water, m_ReflShoreDraws, m_ReflSkyRun && m_ReflSkyView ? 1 : 0, plane[0], plane[1], plane[2], plane[3],
                viewport[0], viewport[1], viewport[2], viewport[3], m_ReflClip.left, m_ReflClip.top, m_ReflClip.right, m_ReflClip.bottom );
        }

        // First frame of the trace: the kept draws by texture, where they are on screen, their w and z (z against
        // 1/w as z = a + b/w; a draw off that line has a w of its own) and their height above the plane.
        if (trace && m_ReflTrace == 119)
        {
            struct Group
            {
                std::size_t m_Batches = 0, m_Triangles = 0, m_Count = 0;
                double m_X[2] = { 1e30, -1e30 }, m_Y[2] = { 1e30, -1e30 }, m_W[2] = { 1e30, -1e30 }, m_Z[2] = { 1e30, -1e30 };
                double m_Above[2] = { 1e30, -1e30 };
                double m_R = 0, m_Zs = 0, m_RR = 0, m_RZ = 0;
            };
            std::map<std::uint32_t, Group> groups;
            for (std::size_t i = std::min( m_ReflSkyCount, batches ); i < batches; ++i)
            {
                const MirrorBatch & batch = m_ReflBatches[i];
                Group & group = groups[batch.m_Hash];
                ++group.m_Batches;
                group.m_Triangles += batch.m_Indices.size() / 3;
                for (const TlVertex & v : batch.m_Vertices)
                {
                    if (!(v.m_Rhw > 0.0f && v.m_Rhw < 1e30f))
                        continue;
                    const double w = 1.0 / v.m_Rhw;
                    double view[3];
                    to_view( { v.m_X, v.m_Y, static_cast<float>(w) }, view );
                    const double above = plane[0] * view[0] + plane[1] * view[1] + plane[2] * view[2] + plane[3];
                    const double values[5] = { v.m_X, v.m_Y, w, v.m_Z, above };
                    double * ranges[5] = { group.m_X, group.m_Y, group.m_W, group.m_Z, group.m_Above };
                    for (int k = 0; k < 5; ++k)
                    {
                        ranges[k][0] = std::min( ranges[k][0], values[k] );
                        ranges[k][1] = std::max( ranges[k][1], values[k] );
                    }
                    ++group.m_Count;
                    group.m_R += v.m_Rhw;
                    group.m_Zs += v.m_Z;
                    group.m_RR += static_cast<double>(v.m_Rhw) * v.m_Rhw;
                    group.m_RZ += static_cast<double>(v.m_Rhw) * v.m_Z;
                }
            }
            // The water triangles of the plane fit by texture: w, distance of their corners to the plane, share of the fit.
            struct Fit
            {
                std::size_t m_Triangles = 0;
                double m_W[2] = { 1e30, -1e30 }, m_Above[2] = { 1e30, -1e30 };
                double m_Weight = 0.0;
            };
            std::map<std::uint32_t, Fit> fits;
            double fit_total = 0.0;
            for (std::size_t t = 0; t < water && t < m_ReflWaterHash.size(); ++t)
            {
                Fit & fit = fits[m_ReflWaterHash[t]];
                ++fit.m_Triangles;
                double mid[3] = {};
                for (int k = 0; k < 3; ++k)
                {
                    const auto & corner = m_ReflWater[t * 3 + k];
                    double view[3];
                    to_view( corner, view );
                    const double above = plane[0] * view[0] + plane[1] * view[1] + plane[2] * view[2] + plane[3];
                    fit.m_W[0] = std::min( fit.m_W[0], static_cast<double>(corner[2]) );
                    fit.m_W[1] = std::max( fit.m_W[1], static_cast<double>(corner[2]) );
                    fit.m_Above[0] = std::min( fit.m_Above[0], above );
                    fit.m_Above[1] = std::max( fit.m_Above[1], above );
                    for (int j = 0; j < 3; ++j)
                        mid[j] += view[j] / 3.0;
                }
                const double weight = 1.0 / std::max( mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2], 1.0 );
                fit.m_Weight += weight;
                fit_total += weight;
            }
            Log( "Water: water triangles by texture: texture triangles, w, distance of the corners to the plane, share of the fit" );
            for (const auto & [hash, fit] : fits)
                Log( "Water:   %08X %zu, w %.2f..%.2f, plane %.3f..%.3f, fit %.1f %%", hash, fit.m_Triangles, fit.m_W[0], fit.m_W[1], fit.m_Above[0],
                    fit.m_Above[1], fit_total > 0.0 ? fit.m_Weight / fit_total * 100.0 : 0.0 );

            Log( "Water: kept draws by texture (%zu sky draws left out): texture batches triangles, screen x y, w, z, z = a + b/w, height above water",
                std::min( m_ReflSkyCount, batches ) );
            for (const auto & [hash, group] : groups)
            {
                if (group.m_Count == 0)
                    continue;
                const double n = static_cast<double>(group.m_Count);
                const double variance = group.m_RR / n - (group.m_R / n) * (group.m_R / n);
                const double b = variance > 1e-18 ? (group.m_RZ / n - (group.m_R / n) * (group.m_Zs / n)) / variance : 0.0;
                const double a = group.m_Zs / n - b * group.m_R / n;
                Log( "Water:   %08X %zu %zu, x %.0f..%.0f y %.0f..%.0f, w %.2f..%.2f, z %.5f..%.5f, a %.5f b %.5f, above %.2f..%.2f", hash,
                    group.m_Batches, group.m_Triangles, group.m_X[0], group.m_X[1], group.m_Y[0], group.m_Y[1], group.m_W[0], group.m_W[1],
                    group.m_Z[0], group.m_Z[1], a, b, group.m_Above[0], group.m_Above[1] );
            }
        }

        // Target and depth of the reflection, one sample, the render size times '[Water] ReflectionScale'.
        const int refl_width = std::max( 1, static_cast<int>(std::lround( m_RenderWidth * m_ConfigReflectionScale )) );
        const int refl_height = std::max( 1, static_cast<int>(std::lround( m_RenderHeight * m_ConfigReflectionScale )) );
        if (m_ReflWidth != refl_width || m_ReflHeight != refl_height || !m_ReflTargetView)
        {
            m_ReflTargetSrv.Reset();
            m_ReflTargetView.Reset();
            m_ReflTarget.Reset();
            m_ReflDepthView.Reset();
            m_ReflDepth.Reset();

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(refl_width);
            desc.Height = static_cast<UINT>(refl_height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            m_Device->CreateTexture2D( &desc, nullptr, m_ReflTarget.Put() );
            desc.Format = DXGI_FORMAT_D32_FLOAT;
            desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            m_Device->CreateTexture2D( &desc, nullptr, m_ReflDepth.Put() );

            if (m_ReflTarget && m_ReflDepth)
            {
                m_Device->CreateRenderTargetView( m_ReflTarget.Get(), nullptr, m_ReflTargetView.Put() );
                m_Device->CreateShaderResourceView( m_ReflTarget.Get(), nullptr, m_ReflTargetSrv.Put() );
                m_Device->CreateDepthStencilView( m_ReflDepth.Get(), nullptr, m_ReflDepthView.Put() );
            }

            m_ReflWidth = refl_width;
            m_ReflHeight = refl_height;
            Log( "Water: reflection target %dx%d %s", m_ReflWidth, m_ReflHeight, m_ReflTargetView && m_ReflDepthView ? "created" : "FAILED" );
        }

        if (!m_ReflTargetView || !m_ReflDepthView || !m_ReflTargetSrv)
            return;

        const float transparent[4] = {};
        m_Context->ClearRenderTargetView( m_ReflTargetView.Get(), transparent );
        m_Context->ClearDepthStencilView( m_ReflDepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0 );

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        // The sky dome moves with the camera and is smaller than the camera height over the water: mirrored at
        // the water its lower, blue half came into view. The sky is far away, so it is mirrored at the plane
        // through the camera (d = 0) instead, that only turns the directions round.
        const auto set_mirror_plane = [&]( bool sky )
        {
            if (SUCCEEDED( m_Context->Map( m_MirrorConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            {
                auto * out = static_cast<float *>(mapped.pData);
                std::memcpy( out, plane, sizeof( plane ) );
                if (sky)
                    out[3] = 0.0f;
                std::memcpy( out + 4, viewport, sizeof( float ) * 4 );
                std::memcpy( out + 8, view_of_clip, sizeof( float ) * 16 );
                std::memcpy( out + 24, clip_of_view, sizeof( float ) * 16 );
                m_Context->Unmap( m_MirrorConstants.Get(), 0 );
            }
        };
        const std::size_t sky_batches = std::min( m_ReflSkyCount, batches );

        // Depth of the game z = 1 - n / w; n follows the near plane of the camera (0.5 chase view, 0.15 bonnet and
        // cockpit). Median of (1 - z) * w over the kept draws, for the depth lookups of the composite.
        float depth_scale = 0.5f;
        {
            std::vector<float> samples;
            for (std::size_t i = sky_batches; i < batches && samples.size() < 4096; ++i)
                for (const TlVertex & v : m_ReflBatches[i].m_Vertices)
                    if (v.m_Rhw > 0.0f && v.m_Rhw < 1e30f && v.m_Z < 1.0f && samples.size() < 4096)
                        samples.push_back( (1.0f - v.m_Z) / v.m_Rhw );
            if (!samples.empty())
            {
                std::nth_element( samples.begin(), samples.begin() + samples.size() / 2, samples.end() );
                depth_scale = std::clamp( samples[samples.size() / 2], 0.01f, 10.0f );
            }
        }
        set_mirror_plane( sky_batches > 0 );

        ID3D11RenderTargetView * reflection_target = m_ReflTargetView.Get();
        m_Context->OMSetRenderTargets( 1, &reflection_target, m_ReflDepthView.Get() );
        D3D11_VIEWPORT view = { 0, 0, static_cast<float>(refl_width), static_cast<float>(refl_height), 0, 1 };
        m_Context->RSSetViewports( 1, &view );
        const float sx = static_cast<float>(refl_width) / static_cast<float>(std::max( m_RenderWidth, 1 ));
        const float sy = static_cast<float>(refl_height) / static_cast<float>(std::max( m_RenderHeight, 1 ));
        const D3D11_RECT scissor = { static_cast<LONG>(m_ReflClip.left * sx), static_cast<LONG>(m_ReflClip.top * sy),
            static_cast<LONG>(std::ceil( m_ReflClip.right * sx )), static_cast<LONG>(std::ceil( m_ReflClip.bottom * sy )) };
        m_Context->RSSetScissorRects( 1, &scissor );

        m_Context->IASetInputLayout( m_SceneLayout.Get() );
        UINT stride = sizeof( TlVertex );
        UINT offset = 0;
        ID3D11Buffer * vertex_buffer = m_VertexBuffer.Get();
        m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
        m_Context->IASetIndexBuffer( m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_MirrorVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_ScenePs.Get(), nullptr, 0 );
        ID3D11Buffer * buffers[2] = { m_DrawConstants.Get(), m_MirrorConstants.Get() };
        m_Context->VSSetConstantBuffers( 0, 2, buffers );
        m_Context->PSSetConstantBuffers( 0, 1, buffers );
        ID3D11ShaderResourceView * no_view = nullptr;
        m_Context->PSSetShaderResources( 2, 1, &no_view );

        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        // Vertices, indices, constants, textures and samplers of a kept draw.
        const auto upload = [&]( const MirrorBatch & batch )
        {
            if (batch.m_Vertices.empty() || batch.m_Indices.empty() || batch.m_Vertices.size() > RAW_VERTICES ||
                batch.m_Indices.size() > RAW_INDICES)
                return false;

            if (SUCCEEDED( m_Context->Map( m_VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            {
                std::memcpy( mapped.pData, batch.m_Vertices.data(), batch.m_Vertices.size() * sizeof( TlVertex ) );
                m_Context->Unmap( m_VertexBuffer.Get(), 0 );
            }
            if (SUCCEEDED( m_Context->Map( m_IndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            {
                std::memcpy( mapped.pData, batch.m_Indices.data(), batch.m_Indices.size() * sizeof( std::uint16_t ) );
                m_Context->Unmap( m_IndexBuffer.Get(), 0 );
            }
            if (SUCCEEDED( m_Context->Map( m_DrawConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            {
                std::memcpy( mapped.pData, batch.m_Constants.data(), std::min( batch.m_Constants.size(), sizeof( DrawConstants ) ) );
                m_Context->Unmap( m_DrawConstants.Get(), 0 );
            }

            ID3D11ShaderResourceView * views[2] = { batch.m_Views[0], batch.m_Views[1] };
            ID3D11SamplerState * samplers[2] = { batch.m_Samplers[0], batch.m_Samplers[1] };
            m_Context->PSSetShaderResources( 0, 2, views );
            m_Context->PSSetSamplers( 0, 2, samplers );
            return true;
        };

        for (std::size_t i = 0; i < batches; ++i)
        {
            if (i == sky_batches && i > 0)
                set_mirror_plane( false );
            // With the sky lookup of the composite the sky comes from the frame itself, in one piece with the
            // part above the top edge; the mirrored dome would leave a seam where it ends.
            if (i < sky_batches && m_ReflSkyRun)
                continue;

            const MirrorBatch & batch = m_ReflBatches[i];

            if (!upload( batch ))
                continue;

            // Opaque: colour as is, coverage 1 (blend factor). Blended: over the target, coverage accumulated.
            auto & blend = m_ReflBlends[batch.m_BlendKey];
            if (!blend)
            {
                D3D11_BLEND_DESC desc = {};
                auto & target = desc.RenderTarget[0];
                target.BlendEnable = TRUE;
                target.BlendOp = D3D11_BLEND_OP_ADD;
                target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                if (batch.m_BlendKey != 0)
                {
                    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
                    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                    target.SrcBlendAlpha = D3D11_BLEND_ONE;
                    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                }
                else
                {
                    target.SrcBlend = D3D11_BLEND_ONE;
                    target.DestBlend = D3D11_BLEND_ZERO;
                    target.SrcBlendAlpha = D3D11_BLEND_BLEND_FACTOR;
                    target.DestBlendAlpha = D3D11_BLEND_ZERO;
                }
                m_Device->CreateBlendState( &desc, blend.Put() );
            }
            m_Context->OMSetBlendState( blend.Get(), one, 0xFFFFFFFF );

            auto & depth = m_ReflDepthStates[batch.m_DepthWrite ? 1u : 0u];
            if (!depth)
            {
                D3D11_DEPTH_STENCIL_DESC desc = {};
                desc.DepthEnable = TRUE;
                desc.DepthWriteMask = batch.m_DepthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                m_Device->CreateDepthStencilState( &desc, depth.Put() );
            }
            m_Context->OMSetDepthStencilState( depth.Get(), 0 );

            // Mirroring turns the screen winding round.
            auto & raster = m_ReflRasters[batch.m_Cull & 3];
            if (!raster)
            {
                D3D11_RASTERIZER_DESC desc = {};
                desc.FillMode = D3D11_FILL_SOLID;
                const std::uint32_t cull = batch.m_Cull & 3;
                desc.CullMode = cull == 2 ? D3D11_CULL_BACK : (cull == 3 ? D3D11_CULL_FRONT : D3D11_CULL_NONE);
                // '[Water] ReflectionTwoSided': the mirrored camera looks from below and sees faces the game culls.
                if (m_ConfigReflectionTwoSided)
                    desc.CullMode = D3D11_CULL_NONE;
                desc.FrontCounterClockwise = FALSE;
                desc.DepthClipEnable = TRUE;
                desc.ScissorEnable = TRUE;
                m_Device->CreateRasterizerState( &desc, raster.Put() );
            }
            m_Context->RSSetState( raster.Get() );

            m_Context->DrawIndexed( static_cast<UINT>(batch.m_Indices.size()), 0, 0 );
        }

        // The sky alone, the draws of the sky phase as the game made them (not mirrored): the composite looks up
        // the reflected sky there. Taken from the frame, anything in front of the sky (flags, poles) would be
        // mirrored at the horizon like the sky itself.
        if (m_ReflSkyRun)
            m_ReflSkyRun = PrepareSkyImage();
        // The sky image and the composite are of the render size.
        const D3D11_VIEWPORT full = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 0, 1 };
        m_Context->RSSetViewports( 1, &full );
        const D3D11_RECT full_scissor = m_ReflClip;
        m_Context->RSSetScissorRects( 1, &full_scissor );
        if (m_ReflSkyRun)
        {
            ID3D11RenderTargetView * sky_target = m_ReflSkyRtv.Get();
            m_Context->OMSetRenderTargets( 1, &sky_target, nullptr );
            // The sky dome is blended, over the background colour of the game like in the frame.
            float background[4];
            ColorToFloat4( m_BackgroundColor, background );
            background[3] = 1.0f;
            m_Context->ClearRenderTargetView( sky_target, background );
            m_Context->VSSetShader( m_SceneVs.Get(), nullptr, 0 );

            auto & sky_depth = m_ReflDepthStates[0x200];
            if (!sky_depth)
            {
                D3D11_DEPTH_STENCIL_DESC desc = {};
                desc.DepthEnable = FALSE;
                desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
                m_Device->CreateDepthStencilState( &desc, sky_depth.Put() );
            }
            m_Context->OMSetDepthStencilState( sky_depth.Get(), 0 );

            auto & sky_raster = m_ReflRasters[0x200];
            if (!sky_raster)
            {
                D3D11_RASTERIZER_DESC desc = {};
                desc.FillMode = D3D11_FILL_SOLID;
                desc.CullMode = D3D11_CULL_NONE;
                desc.DepthClipEnable = FALSE;
                m_Device->CreateRasterizerState( &desc, sky_raster.Put() );
            }
            m_Context->RSSetState( sky_raster.Get() );

            for (std::size_t i = 0; i < sky_batches; ++i)
            {
                const MirrorBatch & batch = m_ReflBatches[i];
                if (!upload( batch ))
                    continue;
                const bool blended = batch.m_BlendKey != 0;
                auto & blend = m_ReflBlends[blended ? 0x201u : 0x200u];
                if (!blend)
                {
                    D3D11_BLEND_DESC desc = {};
                    auto & target = desc.RenderTarget[0];
                    target.BlendEnable = blended ? TRUE : FALSE;
                    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
                    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                    target.BlendOp = D3D11_BLEND_OP_ADD;
                    target.SrcBlendAlpha = D3D11_BLEND_ZERO;
                    target.DestBlendAlpha = D3D11_BLEND_ONE;
                    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                    m_Device->CreateBlendState( &desc, blend.Put() );
                }
                m_Context->OMSetBlendState( blend.Get(), one, 0xFFFFFFFF );
                m_Context->DrawIndexed( static_cast<UINT>(batch.m_Indices.size()), 0, 0 );
            }
        }
        // Before the composite clears the water bit: open sky (stencil 0) next to the water for '[Water] EdgeFade',
        // solid draws in front of the sky for the sky lookup.
        // The frame and its depth: holes of the mirrored scene (geometry the game never drew) take the frame in the
        // mirrored direction where what is seen there is far away (then it lies like the sky, mirrored at the horizon).
        const bool stencil = m_ReflSkyRun && (m_Samples > 1 ? m_ReflCompositeMsPs : m_ReflCompositePs) && CopyStencil() && CopyFrame();
        const bool edge_fade = stencil && m_ConfigWaterEdgeFade > 0.0f;

        // Over the water pixels of the frame (stencil bit 1, then cleared), tinted and with the strength as alpha.
        ID3D11RenderTargetView * frame_target = m_FrameView.Get();
        m_Context->OMSetRenderTargets( 1, &frame_target, m_DepthView.Get() );

        if (SUCCEEDED( m_Context->Map( m_MirrorConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            auto * out = static_cast<float *>(mapped.pData);
            out[0] = static_cast<float>((m_ConfigReflectionTint >> 16) & 0xFF) / 255.0f;
            out[1] = static_cast<float>((m_ConfigReflectionTint >> 8) & 0xFF) / 255.0f;
            out[2] = static_cast<float>(m_ConfigReflectionTint & 0xFF) / 255.0f;
            out[3] = m_ConfigReflectionDebug == 1 || m_ConfigReflectionDebug == 2 ? -1.0f : m_ConfigReflectionStrength;
            // Reflected sky where the mirrored scene is empty: plane, viewport, camera and whether the sky copy is valid.
            std::memcpy( out + 4, plane, sizeof( plane ) );
            std::memcpy( out + 8, viewport, sizeof( float ) * 4 );
            out[12] = m_ReflSkyRun && m_ReflSkyView ? 1.0f : 0.0f;
            out[13] = static_cast<float>(::GetTickCount64() % 3600000ull) / 1000.0f;
            out[14] = m_ConfigReflectionWaves;
            out[15] = m_ConfigReflectionBlur;
            std::memcpy( out + 16, view_of_clip, sizeof( float ) * 16 );
            std::memcpy( out + 32, clip_of_view, sizeof( float ) * 16 );
            out[48] = m_ConfigReflectionFresnel;
            out[49] = m_ReflWeightSrv ? 1.0f : 0.0f;
            const bool far_fade = m_ConfigWaterFarFade > 0.0f && m_ReflWaterFar > 0.0f && m_ReflSkyRun;
            out[50] = far_fade ? m_ReflWaterFar * (1.0f - m_ConfigWaterFarFade) : 0.0f;
            out[51] = far_fade ? m_ReflWaterFar : 0.0f;
            out[52] = edge_fade ? m_ConfigWaterEdgeFade : 0.0f;
            out[53] = m_ConfigReflectionDebug >= 3 ? static_cast<float>(m_ConfigReflectionDebug) : 0.0f;
            out[54] = stencil ? 1.0f : 0.0f;
            out[55] = m_ConfigReflectionGrazing;
            out[56] = static_cast<float>(m_RenderWidth);
            out[57] = static_cast<float>(m_RenderHeight);
            out[58] = depth_scale;
            out[59] = 0.0f;
            m_Context->Unmap( m_MirrorConstants.Get(), 0 );
        }

        auto & composite_blend = m_ReflBlends[0x100];
        if (!composite_blend)
        {
            D3D11_BLEND_DESC desc = {};
            auto & target = desc.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ZERO;
            target.DestBlendAlpha = D3D11_BLEND_ONE;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            m_Device->CreateBlendState( &desc, composite_blend.Put() );
        }
        m_Context->OMSetBlendState( composite_blend.Get(), one, 0xFFFFFFFF );

        // With the stencil copy the shader picks the pixels itself (water, holes in it); the water bit is cleared everywhere.
        auto & composite_depth = m_ReflDepthStates[stencil ? 0x101u : 0x100u];
        if (!composite_depth)
        {
            D3D11_DEPTH_STENCIL_DESC desc = {};
            desc.DepthEnable = FALSE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            desc.StencilEnable = TRUE;
            desc.StencilReadMask = 1;
            desc.StencilWriteMask = 1;
            desc.FrontFace.StencilFunc = stencil ? D3D11_COMPARISON_ALWAYS : D3D11_COMPARISON_EQUAL;
            desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_ZERO;
            desc.BackFace = desc.FrontFace;
            m_Device->CreateDepthStencilState( &desc, composite_depth.Put() );
        }
        m_Context->OMSetDepthStencilState( composite_depth.Get(), 1 );

        auto & composite_raster = m_ReflRasters[0x100];
        if (!composite_raster)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.DepthClipEnable = FALSE;
            desc.ScissorEnable = TRUE;
            m_Device->CreateRasterizerState( &desc, composite_raster.Put() );
        }
        m_Context->RSSetState( composite_raster.Get() );

        m_Context->IASetInputLayout( nullptr );
        m_Context->VSSetShader( m_ReflCompositeVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( stencil && m_Samples > 1 ? m_ReflCompositeMsPs.Get() : m_ReflCompositePs.Get(), nullptr, 0 );
        ID3D11Buffer * tint_buffer = m_MirrorConstants.Get();
        m_Context->PSSetConstantBuffers( 0, 1, &tint_buffer );
        ID3D11ShaderResourceView * reflection[6] = { m_ReflTargetSrv.Get(), m_ReflSkyView.Get(), m_ReflWeightSrv.Get(),
            stencil ? m_ReflStencilView.Get() : nullptr, stencil ? m_ReflFrameCopyView.Get() : nullptr, stencil ? m_ReflDepthCopyView.Get() : nullptr };
        m_Context->PSSetShaderResources( 0, 6, reflection );
        ID3D11SamplerState * sky_sampler = m_PresentSampler.Get();
        m_Context->PSSetSamplers( 1, 1, &sky_sampler );
        m_Context->Draw( 3, 0 );

        // The next draws set their own state; the target must not stay bound as a texture.
        ID3D11ShaderResourceView * none[6] = {};
        m_Context->PSSetShaderResources( 0, 6, none );
    }
}
