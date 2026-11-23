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
        // Entries of the game changed by the driver (import slots, table entries, call targets), 4 bytes each. The game
        // frees the driver at exit; RestoreGamePatches puts them back, so nothing calls into the freed module.
        struct GamePatch
        {
            std::uintptr_t m_Address;
            std::uint32_t m_Original;
        };

        std::vector<GamePatch> g_GamePatches;

        std::int64_t PerfNow()
        {
            LARGE_INTEGER now = {};
            ::QueryPerformanceCounter( &now );
            return now.QuadPart;
        }

        void RememberPatch(
            std::uintptr_t address
        )
        {
            std::uint32_t original = 0;
            std::memcpy( &original, reinterpret_cast<const void *>(address), sizeof( original ) );
            g_GamePatches.push_back( { address, original } );
        }

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
    // Ring of the car reflections (CarReflBatch), apart from the one of the game's batches.
    constexpr std::size_t CAR_REFL_VERTICES = 65536;
    constexpr std::size_t CAR_REFL_INDICES = CAR_REFL_VERTICES * 4;

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

        // Frames of the movie player are packed 4:2:2 with 7 bit components, two pixels per 32 bit
        // word: byte 0 is V, byte 1 is U, byte 2 is Y of the second pixel and byte 3 is Y of the
        // first. This is the conversion of the game ['sub_56AD70', 'sub_592610'] with a full range
        // result: the ITU-R BT.601 factors, biases per channel and 2 * field - bias clamped.
        // The game repeats the color of a pair for both pixels. The color sits between the two
        // pixels of a pair, so each pixel takes three quarters of its own pair and one quarter of
        // the neighbor on its side, which removes the color blocks at edges.
        void FromMovieYuv(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            std::uint8_t * destination,
            std::uint32_t destination_pitch,
            int width,
            int height
        )
        {
            auto channel = [] ( int field, int bias )
            {
                return static_cast<std::uint32_t>(std::clamp( 2 * (field & 0x1FF) - bias, 0, 255 ));
            };

            const int pairs = width / 2;

            for (int y = 0; y < height; ++y)
            {
                const std::uint8_t * row = source + static_cast<std::size_t>( y ) * source_pitch;
                auto * out = reinterpret_cast<std::uint32_t *>( destination + static_cast<std::size_t>( y ) * destination_pitch );

                for (int pair = 0; pair < pairs; ++pair)
                {
                    const std::uint8_t * in = row + pair * 4;
                    const std::uint8_t * before = row + std::max( pair - 1, 0 ) * 4;
                    const std::uint8_t * after = row + std::min( pair + 1, pairs - 1 ) * 4;
                    const int luma[2] = { in[3] & 0x7F, in[2] & 0x7F };
                    const std::uint8_t * neighbor[2] = { before, after };

                    for (int k = 0; k < 2; ++k)
                    {
                        const float v = 0.75f * static_cast<float>(in[0] & 0x7F) + 0.25f * static_cast<float>(neighbor[k][0] & 0x7F);
                        const float u = 0.75f * static_cast<float>(in[1] & 0x7F) + 0.25f * static_cast<float>(neighbor[k][1] & 0x7F);
                        const float fv = v - 64.0f + 0.5f;
                        const float fu = u - 64.0f + 0.5f;

                        const std::uint32_t r = channel( 89 + luma[k] + static_cast<int>(std::lrintf( fv * 1.402f )), 177 );
                        const std::uint32_t g = channel( 67 + luma[k] + static_cast<int>(std::lrintf( fu * -0.3441f )) + static_cast<int>(std::lrintf( fv * -0.7141f )), 133 );
                        const std::uint32_t b = channel( 113 + luma[k] + static_cast<int>(std::lrintf( fu * 1.772f )), 225 );
                        out[pair * 2 + k] = 0xFF000000u | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }

        // Counts the rows at the top and at the bottom of a movie frame that are black from edge
        // to edge, the letterbox some movies carry inside the frame. Returns 'false' for a frame
        // that is black all over, which says nothing about the bars.
        bool FindMovieBars(
            const std::uint8_t * source,
            std::uint32_t source_pitch,
            int width,
            int height,
            int & top,
            int & bottom
        )
        {
            constexpr int BLACK = 3;

            auto black = [&] ( int y )
            {
                const std::uint8_t * in = source + static_cast<std::size_t>( y ) * source_pitch;

                for (int x = 0; x + 1 < width; x += 2, in += 4)
                {
                    if ((in[2] & 0x7F) > BLACK || (in[3] & 0x7F) > BLACK)
                        return false;
                }

                return true;
            };

            top = 0;
            bottom = 0;

            while (top < height && black( top ))
                ++top;

            if (top == height)
                return false;

            while (bottom < height - top && black( height - 1 - bottom ))
                ++bottom;

            return true;
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
                    RememberPatch( reinterpret_cast<std::uintptr_t>(&slots->u1.Function) );
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

            // The log of the previous start stays as <name>.prev.log (B28: a restart overwrote the log of the race).
            const std::string previous = m_LogPath.substr( 0, m_LogPath.size() - 4 ) + ".prev.log";
            ::MoveFileExA( m_LogPath.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING );

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

        desc.ByteWidth = static_cast<UINT>(CAR_REFL_VERTICES * sizeof( CarReflVertex ));
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        m_Device->CreateBuffer( &desc, nullptr, m_CarReflVertexBuffer.Put() );

        desc.ByteWidth = static_cast<UINT>(CAR_REFL_INDICES * sizeof( std::uint16_t ));
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        m_Device->CreateBuffer( &desc, nullptr, m_CarReflIndexBuffer.Put() );

        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.ByteWidth = sizeof( DrawConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_DrawConstants.Put() );
        desc.ByteWidth = sizeof( PresentConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_PresentConstants.Put() );
        desc.ByteWidth = sizeof( ClearConstants );
        m_Device->CreateBuffer( &desc, nullptr, m_ClearConstants.Put() );
        desc.ByteWidth = 368; // MirrorConstants of the scene shader, also the constants of the reflection composite (9.14b: 368)
        m_Device->CreateBuffer( &desc, nullptr, m_MirrorConstants.Put() );
        desc.ByteWidth = 432; // CarReflectConstants (15.3: + weather on the car, w5: + flow of the drops, 9.15b: + highlight, clearcoat, B36: + sky by direction)
        m_Device->CreateBuffer( &desc, nullptr, m_CarReflConstants.Put() );
        desc.ByteWidth = 1296; // AoConstants (shaders::AO; 9zd: + method, apply, fog, last camera, fog table)
        m_Device->CreateBuffer( &desc, nullptr, m_AoConstants.Put() );

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
        m_CarReflVertexCursor = CAR_REFL_VERTICES;
        m_CarReflIndexCursor = CAR_REFL_INDICES;
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

        // Variant for draws that cannot discard (no alpha test, no edge pass): forced early depth and stencil test, which the
        // discard in the shader otherwise holds back on some hardware. Needs shader model 5 (feature level 11).
        if (m_Device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0 && m_ConfigEarlyZ != 0)
        {
            const D3D_SHADER_MACRO early[] = { { "EARLYZ", "1" }, { nullptr, nullptr } };
            ComPtr<ID3DBlob> ps_early;
            if (build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MainPS", "ps_5_0", ps_early, early ))
                m_Device->CreatePixelShader( ps_early->GetBufferPointer(), ps_early->GetBufferSize(), nullptr, m_ScenePsEarly.Put() );
            Log( "Scene pixel shader with early depth test: %s", m_ScenePsEarly ? "built" : "not available" );
        }

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

        // Movies fall back to the game's own scaling when this one does not build.
        if (build( shaders::MOVIE, sizeof( shaders::MOVIE ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_MoviePs.Put() );
        }

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
        // '[Water] ReflectionSimpleShader' (9.13d): the scene shader without its light loops for the mirrored draws.
        const D3D_SHADER_MACRO mirror_pass[] = { { "MIRROR_PASS", "1" }, { nullptr, nullptr } };
        if (build( shaders::SCENE, sizeof( shaders::SCENE ) - 1, "MainPS", "ps_4_0", ps, mirror_pass ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_MirrorPs.Put() );
        }
        if (build( shaders::REFLECT, sizeof( shaders::REFLECT ) - 1, "MainVS", "vs_4_0", vs ) &&
            build( shaders::REFLECT, sizeof( shaders::REFLECT ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_ReflCompositeVs.Put() );
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ReflCompositePs.Put() );
        }
        if (build( shaders::REFLECT_PACK, sizeof( shaders::REFLECT_PACK ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ReflPackPs.Put() );
        }
        const D3D_SHADER_MACRO multisampled[] = { { "STENCIL_MS", "1" }, { nullptr, nullptr } };
        if (build( shaders::REFLECT_PACK, sizeof( shaders::REFLECT_PACK ) - 1, "MainPS", "ps_4_1", ps, multisampled ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ReflPackMsPs.Put() );
        }

        // Car reflections (PLAN.md 9.12b), optional as well.
        if (build( shaders::CAR_REFLECT, sizeof( shaders::CAR_REFLECT ) - 1, "MainVS", "vs_4_0", vs ) &&
            build( shaders::CAR_REFLECT, sizeof( shaders::CAR_REFLECT ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_CarReflVs.Put() );
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_CarReflPs.Put() );
            const D3D11_INPUT_ELEMENT_DESC car_layout[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            m_Device->CreateInputLayout( car_layout, static_cast<UINT>(std::size( car_layout )), vs->GetBufferPointer(), vs->GetBufferSize(),
                m_CarReflLayout.Put() );
        }
        // B36: the sky panorama of the car reflections.
        if (build( shaders::CAR_SKY_PANO, sizeof( shaders::CAR_SKY_PANO ) - 1, "MainVS", "vs_4_0", vs ) &&
            build( shaders::CAR_SKY_PANO, sizeof( shaders::CAR_SKY_PANO ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_CarSkyPanoVs.Put() );
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_CarSkyPanoPs.Put() );
            const D3D11_INPUT_ELEMENT_DESC pano_layout[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            m_Device->CreateInputLayout( pano_layout, 3, vs->GetBufferPointer(), vs->GetBufferSize(), m_CarSkyPanoLayout.Put() );
        }
        // Own rain and snow (16.4 / 16.5).
        if (build( shaders::PRECIP, sizeof( shaders::PRECIP ) - 1, "PrecipVS", "vs_4_0", vs ) &&
            build( shaders::PRECIP, sizeof( shaders::PRECIP ) - 1, "PrecipPS", "ps_4_0", ps ))
        {
            m_Device->CreateVertexShader( vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_PrecipVs.Put() );
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_PrecipPs.Put() );
        }
        if (build( shaders::CAR_DOWN, sizeof( shaders::CAR_DOWN ) - 1, "MainPS", "ps_4_0", ps ))
        {
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_CarReflDownPs.Put() );
        }
        // Shared blur chain (9.16a).
        if (build( shaders::BLUR, sizeof( shaders::BLUR ) - 1, "DownPS", "ps_4_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_BlurDownPs.Put() );
        if (build( shaders::BLUR, sizeof( shaders::BLUR ) - 1, "UpPS", "ps_4_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_BlurUpPs.Put() );
        // Ambient occlusion ('[AmbientOcclusion]'), shader model 5 for the bit counts of the sectors.
        if (build( shaders::AO, sizeof( shaders::AO ) - 1, "MainPS", "ps_5_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_AoPs.Put() );
        if (build( shaders::AO, sizeof( shaders::AO ) - 1, "FilterPS", "ps_5_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_AoFilterPs.Put() );
        if (build( shaders::AO, sizeof( shaders::AO ) - 1, "ApplyPS", "ps_5_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_AoApplyPs.Put() );
        if (build( shaders::AO, sizeof( shaders::AO ) - 1, "DepthHalfPS", "ps_5_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_AoDepthHalfPs.Put() );
        if (build( shaders::AO, sizeof( shaders::AO ) - 1, "DepthMipPS", "ps_5_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_AoDepthMipPs.Put() );
        if (build( shaders::AO, sizeof( shaders::AO ) - 1, "UpsamplePS", "ps_5_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_AoUpsamplePs.Put() );
        // '[Dx11] SkyEdgeFill' (B32).
        if (build( shaders::SKY_EDGE, sizeof( shaders::SKY_EDGE ) - 1, "ColumnPS", "ps_4_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_SkyEdgeColumnPs.Put() );
        if (build( shaders::SKY_EDGE, sizeof( shaders::SKY_EDGE ) - 1, "SmoothPS", "ps_4_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_SkyEdgeSmoothPs.Put() );
        if (build( shaders::SKY_EDGE, sizeof( shaders::SKY_EDGE ) - 1, "FillPS", "ps_4_0", ps ))
            m_Device->CreatePixelShader( ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_SkyEdgeFillPs.Put() );

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

        // Depth, D24S8 is what every sample count above supports. Typeless with shader views where the device
        // allows it, so the water reflections read stencil and depth without copying the whole buffer.
        desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        desc.SampleDesc.Count = m_Samples;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        {
            const bool multisampled = m_Samples > 1;
            D3D11_DEPTH_STENCIL_VIEW_DESC depth_view = {};
            depth_view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depth_view.ViewDimension = multisampled ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
            D3D11_SHADER_RESOURCE_VIEW_DESC stencil_view = {};
            stencil_view.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
            stencil_view.ViewDimension = multisampled ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
            stencil_view.Texture2D.MipLevels = multisampled ? 0 : 1;
            D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv = stencil_view;
            depth_srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_Depth.Put() ) ) ||
                FAILED( m_Device->CreateDepthStencilView( m_Depth.Get(), &depth_view, m_DepthView.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_Depth.Get(), &stencil_view, m_DepthStencilSrv.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_Depth.Get(), &depth_srv, m_DepthSrv.Put() ) ))
            {
                m_DepthStencilSrv.Reset();
                m_DepthSrv.Reset();
                m_DepthView.Reset();
                m_Depth.Reset();
                desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                m_Device->CreateTexture2D( &desc, nullptr, m_Depth.Put() );
                m_Device->CreateDepthStencilView( m_Depth.Get(), nullptr, m_DepthView.Put() );
            }
            Log( "Depth %s", m_DepthSrv ? "readable as texture" : "not readable as texture, the water reflections copy it" );
        }

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
        m_DepthStencilSrv.Reset();
        m_DepthSrv.Reset();
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
        ID3D11SamplerState * sampler,
        ID3D11PixelShader * pixel_shader,
        float shape
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
        constants.m_Params[3] = shape;

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
        m_Context->PSSetShader( pixel_shader != nullptr ? pixel_shader : m_PresentPs.Get(), nullptr, 0 );

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
        m_ConfigFogEnabled = ReadIniBool( "FogEnabled", true, ini );
        m_ConfigMovie = ReadIniBool( "MovieFilter", true, ini );
        m_ConfigMovieFill = ReadIniBool( "MovieFill", true, ini );
        m_MovieSharpness = static_cast<float>(std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "MovieSharpness", 33, ini.c_str() )), 0, 100 )) / 100.0f;
        m_TraceLines = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "Trace", 0, ini.c_str() ));
        const int alpha_edge_mode = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "AlphaEdgeFix", 0, ini.c_str() ));
        m_ConfigAlphaEdgeFix = alpha_edge_mode != 0 && alpha_edge_mode != 3; // 1 and 2: the two pass fix
        m_ConfigAlphaToCoverage = alpha_edge_mode == 3; // 3: alpha to coverage, one pass
        m_ConfigAlphaEdgeRef = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "AlphaEdgeRef", 128, ini.c_str() )), 1, 255 );
        {
            char mip_scale[32] = {};
            ::GetPrivateProfileStringA( "Dx11", "AlphaMipScale", "0", mip_scale, sizeof( mip_scale ), ini.c_str() );
            m_ConfigAlphaMipScale = std::clamp( static_cast<float>(std::atof( mip_scale )), 0.0f, 2.0f );
        }
        m_ConfigForceTwoSided = ::GetPrivateProfileIntA( "Dx11", "ForceTwoSided", 0, ini.c_str() ) != 0;

        Log( "Config ResolutionScale %d Antialiasing %d Anisotropy %d WindowMode %d Window %dx%d", m_ConfigScale, m_ConfigSamples, m_ConfigAnisotropy,
            m_ConfigWindowMode, m_ConfigWindowWidth, m_ConfigWindowHeight );
        Log( "Config VSync %d MinimizeOnFocusLoss %d FogEnabled %d", m_ConfigVsync ? 1 : 0, m_ConfigMinimize ? 1 : 0, m_ConfigFogEnabled ? 1 : 0 );

        m_ConfigTestNoMips = ::GetPrivateProfileIntA( "Dx11", "TestNoMips", 0, ini.c_str() ) != 0;
        m_ConfigTestVertexAlpha = ::GetPrivateProfileIntA( "Dx11", "TestVertexAlpha", 0, ini.c_str() ) != 0;
        m_ConfigTestNoZBias = ::GetPrivateProfileIntA( "Dx11", "TestNoZBias", 0, ini.c_str() ) != 0;
        m_ConfigTestDraw = static_cast<int>(::GetPrivateProfileIntA( "Dx11", "TestDraw", 0, ini.c_str() ));
        Log( "Config TestNoZBias %d TestDraw %d", m_ConfigTestNoZBias ? 1 : 0, m_ConfigTestDraw );
        m_ConfigSkyEdgeFill = std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "SkyEdgeFill", 1, ini.c_str() )), 0, 1 );
        m_ConfigSkyEdgeBlend = static_cast<float>(std::clamp( static_cast<int>(::GetPrivateProfileIntA( "Dx11", "SkyEdgeBlend", 12, ini.c_str() )), 0, 64 ));
        Log( "Config SkyEdgeFill %d SkyEdgeBlend %.0f", m_ConfigSkyEdgeFill, m_ConfigSkyEdgeBlend );
        Log( "Config AlphaMipScale %.2f TestNoMips %d TestVertexAlpha %d", m_ConfigAlphaMipScale, m_ConfigTestNoMips ? 1 : 0,
            m_ConfigTestVertexAlpha ? 1 : 0 );
        Log( "Config AlphaEdgeFix %d AlphaEdgeRef %d ForceTwoSided %d", m_ConfigAlphaToCoverage ? 3 : (m_ConfigAlphaEdgeFix ? 1 : 0), m_ConfigAlphaEdgeRef,
            m_ConfigForceTwoSided ? 1 : 0 );


        // The light switches belong to the LFSP mod: '[Lights]' in lfsp.ini next to the game (doc/LICHT.md 8.2a).
        // A key missing there is still read from '[Dx11]' of nfs5_enhanced.ini (installs before 9.11b).
        const std::string lights_ini = (slash != std::string::npos ? std::string( path, slash + 1 ) : std::string()) + "lfsp.ini";
        m_LfspIni = lights_ini;
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

        m_ConfigNightEdgeLight = ::GetPrivateProfileIntA( "Weather", "NightEdgeLight", 1, lights_ini.c_str() ) != 0;
        Log( "Config NightEdgeLight %d", m_ConfigNightEdgeLight ? 1 : 0 );
        {
            char snow[32] = {};
            ::GetPrivateProfileStringA( "Weather", "SnowWhite", "0.25", snow, sizeof( snow ), lights_ini.c_str() );
            m_ConfigSnowWhite = ::GetPrivateProfileIntA( "Weather", "Enabled", 1, lights_ini.c_str() ) != 0 && std::atof( snow ) > 0.0;
            char cells[32] = {};
            ::GetPrivateProfileStringA( "Weather", "CarDropCells", "110", cells, sizeof( cells ), lights_ini.c_str() );
            m_ConfigCarDropCells = std::clamp( static_cast<float>(std::atof( cells )), 4.0f, 400.0f );
            char flow[32] = {};
            ::GetPrivateProfileStringA( "Weather", "CarDropFlow", "0.005", flow, sizeof( flow ), lights_ini.c_str() );
            m_ConfigCarDropFlow = std::clamp( static_cast<float>(std::atof( flow )), 0.0f, 0.1f );
            char shine[32] = {};
            ::GetPrivateProfileStringA( "Weather", "CarDropShine", "1.0", shine, sizeof( shine ), lights_ini.c_str() );
            m_ConfigCarDropShine = std::clamp( static_cast<float>(std::atof( shine )), 0.0f, 5.0f );
            {
                char value[32] = {};
                m_ConfigPrecip = ::GetPrivateProfileIntA( "Weather", "Precip", 1, lights_ini.c_str() );
                ::GetPrivateProfileStringA( "Weather", "PrecipRain", "7000", value, sizeof( value ), lights_ini.c_str() );
                m_ConfigPrecipRain = std::clamp( static_cast<float>(std::atof( value )), 0.0f, 60000.0f );
                ::GetPrivateProfileStringA( "Weather", "PrecipSnow", "5000", value, sizeof( value ), lights_ini.c_str() );
                m_ConfigPrecipSnow = std::clamp( static_cast<float>(std::atof( value )), 0.0f, 60000.0f );
                ::GetPrivateProfileStringA( "Weather", "PrecipBright", "1.0", value, sizeof( value ), lights_ini.c_str() );
                m_ConfigPrecipBright = std::clamp( static_cast<float>(std::atof( value )), 0.0f, 4.0f );
                Log( "Precipitation (dx11z p2): %s, rain %.0f streaks, snow %.0f flakes, bright %.2f", m_ConfigPrecip > 0 ? "own" : "the game's",
                    m_ConfigPrecipRain, m_ConfigPrecipSnow, m_ConfigPrecipBright );
            }
            char snow_soft[32] = {};
            ::GetPrivateProfileStringA( "Weather", "CarSnowSoft", "1.0", snow_soft, sizeof( snow_soft ), lights_ini.c_str() );
            m_ConfigCarSnowSoft = std::clamp( static_cast<float>(std::atof( snow_soft )), 0.0f, 5.0f );
            char snow_bright[32] = {};
            ::GetPrivateProfileStringA( "Weather", "CarSnowBright", "0.5", snow_bright, sizeof( snow_bright ), lights_ini.c_str() );
            m_ConfigCarSnowBright = std::clamp( static_cast<float>(std::atof( snow_bright )), 0.0f, 2.0f );
            Log( "Weather fx (dx11z weather w1: storm, lightning, snow white): SnowWhite %s, track draw hook %s", snow,
                m_ConfigSnowWhite ? "wanted" : "not needed" );
        }
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

        m_ConfigHeadlightsObjects = std::clamp( std::atoi( light_value( "HeadlightsObjects", "1" ).c_str() ), 0, 1 );
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
            m_ConfigShoreWaterLight = std::clamp( static_cast<float>(std::atof( water_value( "ShoreWaterLight", "1" ).c_str() )), 0.0f, 1.0f );
            Log( "Config water: ShoreWaterLight %.2f", m_ConfigShoreWaterLight );
            m_ConfigWaterFarFade = std::clamp( static_cast<float>(std::atof( water_value( "FarFade", "0.3" ).c_str() )), 0.0f, 0.9f );
            m_ConfigWaterEvenColour = std::clamp( std::atoi( water_value( "WaterEvenColour", "2" ).c_str() ), 0, 2 );
            m_ConfigReflectionScale = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionScale", "0.5" ).c_str() )), 0.25f, 1.0f );
            m_ConfigReflectionGrazing = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionGrazing", "1" ).c_str() )), 0.0f, 1.0f );
            m_ConfigWaterEdgeFade = std::clamp( static_cast<float>(std::atof( water_value( "EdgeFade", "40" ).c_str() )), 0.0f, 200.0f );
            m_ConfigReflectionSky = std::clamp( std::atoi( water_value( "ReflectionSky", "1" ).c_str() ), 0, 1 );
            m_ConfigRemoveBaked = std::clamp( std::atoi( water_value( "RemoveBakedReflections", "1" ).c_str() ), 0, 1 );
            // Cheaper reflections (REFLEXION-WETTER.md 9.13d), each as before by default.
            m_ConfigReflectionSimpleShader = std::clamp( std::atoi( water_value( "ReflectionSimpleShader", "0" ).c_str() ), 0, 1 );
            m_ConfigReflectionMinSize = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionMinSize", "0" ).c_str() )), 0.0f, 200.0f );
            m_ConfigReflectionMirrorView = std::clamp( std::atoi( water_value( "ReflectionMirrorView", "1" ).c_str() ), 0, 1 );
            m_ConfigReflectionSkyScale = std::atof( water_value( "ReflectionSkyScale", "1" ).c_str() ) < 0.75 ? 0.5f : 1.0f;
            m_ConfigReflectionLights = std::clamp( std::atoi( water_value( "ReflectionLights", "0" ).c_str() ), 0, 1 );
            m_ConfigReflectionLightBoost = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionLightBoost", "3" ).c_str() )), 0.0f, 16.0f );
            m_ConfigReflectionLightStreak = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionLightStreak", "6" ).c_str() )), 0.0f, 40.0f );
            // Waves fixed on the water, sun glint, strength at night (REFLEXION-WETTER.md 9.14b), off by default.
            m_ConfigReflectionWorldWaves = std::clamp( std::atoi( water_value( "ReflectionWorldWaves", "0" ).c_str() ), 0, 1 );
            m_ConfigReflectionWaveSlope = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionWaveSlope", "0.06" ).c_str() )), 0.0f, 0.5f );
            m_ConfigReflectionSunGlint = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionSunGlint", "0" ).c_str() )), 0.0f, 20.0f );
            m_ConfigReflectionWaveSize = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionWaveSize", "5" ).c_str() )), 0.25f, 16.0f );
            m_ConfigReflectionWaveDirection = static_cast<float>(std::atof( water_value( "ReflectionWaveDirection", "35" ).c_str() ));
            m_ConfigReflectionWaveSpread = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionWaveSpread", "1" ).c_str() )), 0.0f, 3.0f );
            m_ConfigReflectionWaveHeight = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionWaveHeight", "0" ).c_str() )), 0.0f, 6.0f );
            {
                char strength[32] = {};
                std::snprintf( strength, sizeof( strength ), "%.3f", m_ConfigReflectionStrength );
                m_ConfigReflectionStrengthNight = std::clamp( static_cast<float>(std::atof( water_value( "ReflectionStrengthNight", strength ).c_str() )), 0.0f, 1.0f );
            }
            Log( "Config water: ReflectionWorldWaves %d, ReflectionWaveSize %.2f, ReflectionWaveDirection %.0f, ReflectionWaveSpread %.2f, ReflectionWaveHeight %.2f, ReflectionWaveSlope %.3f, ReflectionSunGlint %.2f, ReflectionStrengthNight %.2f",
                m_ConfigReflectionWorldWaves, m_ConfigReflectionWaveSize, m_ConfigReflectionWaveDirection, m_ConfigReflectionWaveSpread, m_ConfigReflectionWaveHeight,
                m_ConfigReflectionWaveSlope,
                m_ConfigReflectionSunGlint, m_ConfigReflectionStrengthNight );
            Log( "Config water: ReflectionLights %d, ReflectionLightBoost %.1f, ReflectionLightStreak %.1f", m_ConfigReflectionLights,
                m_ConfigReflectionLightBoost, m_ConfigReflectionLightStreak );
            Log( "Config water: ReflectionSimpleShader %d, ReflectionMinSize %.0f, ReflectionMirrorView %d, ReflectionSkyScale %.1f",
                m_ConfigReflectionSimpleShader, m_ConfigReflectionMinSize, m_ConfigReflectionMirrorView, m_ConfigReflectionSkyScale );

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
            const std::string shore = parse_hashes( "ShoreTextures", "63B3CF44,A1C11FBF", m_ConfigShoreTextures );
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
        m_ConfigTrafficGlow = std::clamp( static_cast<float>(std::atof( light_value( "TrafficHeadlightGlow", "0.35" ).c_str() )), 0.0f, 4.0f );
        m_ConfigTrafficRange = std::clamp( static_cast<float>(std::atof( light_value( "TrafficHeadlightRange", "0.5" ).c_str() )), 0.1f, 2.0f );
        m_ConfigOpponentsRange = std::clamp( static_cast<float>(std::atof( light_value( "HeadlightsOpponentsRange", "1.5" ).c_str() )), 0.25f, 4.0f );
        m_ConfigCarLightRadius = std::clamp( static_cast<float>(std::atof( light_value( "CarLightRadius", "250" ).c_str() )), 50.0f, 1000.0f );
        m_ConfigMaxCarLights = std::clamp( std::atoi( light_value( "MaxCarLights", "16" ).c_str() ), 1, MAX_CAR_LIGHTS );
        m_ConfigCoverageMid = std::clamp( static_cast<float>(std::atof( light_value( "AlphaCoverageMid", "0.12" ).c_str() )), 0.0f, 1.0f );
        m_ConfigEarlyZ = std::clamp( std::atoi( light_value( "EarlyZ", "2" ).c_str() ), 0, 2 );
        m_ConfigLightCulling = std::clamp( std::atoi( light_value( "LightCulling", "1" ).c_str() ), 0, 1 );
        m_ConfigNightGradeBlended = std::clamp( std::atoi( light_value( "NightGradeBlended", "1" ).c_str() ), 0, 1 );
        Log( "Config lights from '%s' [Lights] (missing keys from [Dx11]): HeadlightsOpponents %d strength %.2f range %.2f, traffic range %.2f, "
            "CarLightRadius %.0f", lights_ini.c_str(), m_ConfigOpponents, m_ConfigOpponentsStrength, m_ConfigOpponentsRange, m_ConfigTrafficRange,
            m_ConfigCarLightRadius );

        // Stage 2 (PLAN.md 9.12b): own reflection pass over the car bodies.
        {
            const auto car_value = [&]( const char * key, const char * fallback ) -> std::string
            {
                char value[MAX_PATH] = {};
                ::GetPrivateProfileStringA( "CarReflections", key, fallback, value, sizeof( value ), lights_ini.c_str() );
                std::string text = value;
                const std::size_t comment = text.find( ';' );
                if (comment != std::string::npos)
                    text.erase( comment );
                while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                    text.pop_back();
                return text;
            };
            m_ConfigCarRefl = std::clamp( std::atoi( car_value( "Reflections", "0" ).c_str() ), 0, 1 );
            m_ConfigCarReflStrength = std::clamp( static_cast<float>(std::atof( car_value( "Strength", "0.8" ).c_str() )), 0.0f, 1.0f );
            m_ConfigCarReflF0 = std::clamp( static_cast<float>(std::atof( car_value( "Reflectance", "0.15" ).c_str() )), 0.0f, 1.0f );
            m_ConfigCarReflBlur = std::clamp( static_cast<float>(std::atof( car_value( "Blur", "2.0" ).c_str() )), 0.0f, 8.0f );
            m_ConfigCarReflFresnel = std::clamp( static_cast<float>(std::atof( car_value( "Fresnel", "3.0" ).c_str() )), 1.0f, 8.0f );
            m_ConfigCarReflInterval = std::clamp( std::atoi( car_value( "UpdateInterval", "1" ).c_str() ), 1, 8 );
            m_ConfigCarReflMinSize = std::clamp( static_cast<float>(std::atof( car_value( "MinSize", "0" ).c_str() )), 0.0f, 400.0f );
            m_ConfigCarReflDebug = std::clamp( std::atoi( car_value( "Debug", "0" ).c_str() ), 0, 3 );
            // '[CarReflections] MatteKeep' (B28): the paint/cloth split of a matte part decided once per car and kept.
            m_ConfigCarReflMatteKeep = std::clamp( std::atoi( car_value( "MatteKeep", "1" ).c_str() ), 0, 1 );
            m_ConfigCarReflMatteGrey = std::clamp( std::atoi( car_value( "MatteGrey", "0" ).c_str() ), 0, 1 );
            m_ConfigCarReflSkipAlphaMid = std::clamp( static_cast<float>(std::atof( car_value( "SkipAlphaMid", "0.3" ).c_str() )), 0.0f, 1.0f );
            {
                const std::string glass = car_value( "GlassParts", "35,47,48,50,54" );
                m_ConfigCarReflGlassParts.clear();
                for (std::size_t at = 0; at < glass.size();)
                {
                    const std::size_t end = std::min( glass.find_first_of( ", ", at ), glass.size() );
                    if (end > at)
                    {
                        const int part = std::atoi( glass.substr( at, end - at ).c_str() );
                        if (part >= 0 && part < 256)
                            m_ConfigCarReflGlassParts.push_back( part );
                    }
                    at = end + 1;
                }
                m_ConfigCarReflGlassStrength = std::clamp( static_cast<float>(std::atof( car_value( "GlassStrength", "0.5" ).c_str() )), 0.0f, 1.0f );
                m_ConfigCarReflGlassF0 = std::clamp( static_cast<float>(std::atof( car_value( "GlassReflectance", "0.12" ).c_str() )), 0.0f, 1.0f );
                m_ConfigCarReflGlassSkyBoost = std::clamp( static_cast<float>(std::atof( car_value( "GlassSkyBoost", "2" ).c_str() )), 1.0f, 8.0f );
                const std::string skip_parts = car_value( "SkipParts", "19,20,21,40" );
                m_ConfigCarReflSkipParts.clear();
                for (std::size_t at = 0; at < skip_parts.size();)
                {
                    const std::size_t end = std::min( skip_parts.find_first_of( ", ", at ), skip_parts.size() );
                    if (end > at)
                    {
                        const int part = std::atoi( skip_parts.substr( at, end - at ).c_str() );
                        if (part >= 0 && part < 256)
                            m_ConfigCarReflSkipParts.push_back( part );
                    }
                    at = end + 1;
                }
                Log( "Config car reflections: glass parts '%s' as glass (strength %.2f, reflectance %.2f, sky boost %.1f), SkipAlphaMid %.2f, "
                    "no reflection on parts '%s'", glass.c_str(), m_ConfigCarReflGlassStrength, m_ConfigCarReflGlassF0,
                    m_ConfigCarReflGlassSkyBoost, m_ConfigCarReflSkipAlphaMid, skip_parts.c_str() );
            }
            // '[CarReflections] SourceScale' (9.13d): size of the frame copy, 1 = full, 0.5 = half (the blur level keeps the look).
            m_ConfigCarReflSourceScale = std::atof( car_value( "SourceScale", "1" ).c_str() ) < 0.75 ? 0.5f : 1.0f;
            // '[CarReflections] BlurChain' (9.16a): the blur from the shared soft chain instead of the mip levels of the copy.
            m_ConfigCarReflBlurChain = std::clamp( std::atoi( car_value( "BlurChain", "0" ).c_str() ), 0, 1 );
            // '[CarReflections] Sky' (9.15a): above the frame the sky image of the water reflection (previous frame) instead of the fog colour.
            m_ConfigCarReflSky = std::clamp( std::atoi( car_value( "Sky", "0" ).c_str() ), 0, 1 );
            m_ConfigCarReflSkyHold = std::clamp( static_cast<float>(std::atof( car_value( "SkyHold", "-1" ).c_str() )), -1.0f, 3600.0f );
            Log( "Config car reflections: Sky %d (sky image of the water reflection, needs '[Water] ReflectionSky'), SkyHold %.1f s",
                m_ConfigCarReflSky, m_ConfigCarReflSkyHold );
            m_ConfigCarReflSkyPanorama = std::clamp( std::atoi( car_value( "SkyByDirection", "1" ).c_str() ), 0, 1 );
            Log( "Config car reflections: SkyByDirection %d (B36)", m_ConfigCarReflSkyPanorama );
            // 9.15b: highlights, crease angle of the normals, clearcoat; all off by default.
            m_ConfigCarReflHighlightBoost = std::clamp( static_cast<float>(std::atof( car_value( "HighlightBoost", "0" ).c_str() )), 0.0f, 16.0f );
            m_ConfigCarReflHighlightThreshold = std::clamp( static_cast<float>(std::atof( car_value( "HighlightThreshold", "0.75" ).c_str() )), 0.0f, 1.0f );
            {
                const float crease = std::clamp( static_cast<float>(std::atof( car_value( "CreaseAngle", "0" ).c_str() )), 0.0f, 180.0f );
                m_ConfigCarReflCreaseDeg = crease;
                m_ConfigCarReflCreaseCos = crease > 0.0f && crease < 180.0f ? std::cos( crease * 0.01745329f ) : -2.0f;
            }
            m_ConfigCarReflClearCoat = std::clamp( std::atoi( car_value( "ClearCoat", "0" ).c_str() ), 0, 1 );
            m_ConfigCarReflBaseCoat = std::clamp( static_cast<float>(std::atof( car_value( "BaseCoat", "0.2" ).c_str() )), 0.0f, 1.0f );
            m_ConfigCarReflBaseBlur = std::clamp( static_cast<float>(std::atof( car_value( "BaseCoatBlur", "2.5" ).c_str() )), 0.0f, 6.0f );
            m_ConfigCarReflBaseTint = std::clamp( static_cast<float>(std::atof( car_value( "BaseCoatTint", "0.7" ).c_str() )), 0.0f, 1.0f );
            Log( "Config car reflections 9.15b: HighlightBoost %.2f, HighlightThreshold %.2f, CreaseAngle cos %.3f, ClearCoat %d, BaseCoat %.2f, BaseCoatBlur %.1f, BaseCoatTint %.2f",
                m_ConfigCarReflHighlightBoost, m_ConfigCarReflHighlightThreshold, m_ConfigCarReflCreaseCos, m_ConfigCarReflClearCoat, m_ConfigCarReflBaseCoat,
                m_ConfigCarReflBaseBlur, m_ConfigCarReflBaseTint );
            // 9.15e: normals in the space of the part with one orientation per batch; off by default.
            m_ConfigCarReflObjectNormals = std::clamp( std::atoi( car_value( "ObjectNormals", "0" ).c_str() ), 0, 1 );
            m_ConfigCarReflSeamFix = std::clamp( std::atoi( car_value( "SeamFix", "0" ).c_str() ), 0, 1 );
            Log( "Config car reflections 9.15e: ObjectNormals %d, SeamFix %d (needs ObjectNormals)", m_ConfigCarReflObjectNormals,
                m_ConfigCarReflSeamFix );
            // Ambient occlusion ('[AmbientOcclusion]').
            {
                const auto ao_value = [&]( const char * key, const char * fallback ) -> std::string
                {
                    char value[64] = {};
                    ::GetPrivateProfileStringA( "AmbientOcclusion", key, fallback, value, sizeof( value ), lights_ini.c_str() );
                    return value;
                };
                m_ConfigAo = std::clamp( std::atoi( ao_value( "Enable", "0" ).c_str() ), 0, 1 );
                // 'Quality' 0 low, 1 medium, 2 high. Without it the old 'SampleQuality' (0 ... 6) maps to the nearest
                // remaining level: 0/1 -> low, 2 -> medium, 3 and above -> high.
                {
                    const std::string quality = ao_value( "Quality", "" );
                    if (!quality.empty())
                        m_ConfigAoQuality = std::clamp( std::atoi( quality.c_str() ), 0, 2 );
                    else
                    {
                        const int old = std::clamp( std::atoi( ao_value( "SampleQuality", "2" ).c_str() ), 0, 6 );
                        m_ConfigAoQuality = old <= 1 ? 0 : (old == 2 ? 1 : 2);
                    }
                }
                m_ConfigAoShadingRate = std::clamp( std::atoi( ao_value( "ShadingRate", "1" ).c_str() ), 0, 1 );
                m_ConfigAoRadius = std::clamp( static_cast<float>(std::atof( ao_value( "SampleRadius", "1.893" ).c_str() )), 0.5f, 10.0f );
                m_ConfigAoGrow = std::clamp( std::atoi( ao_value( "IncreaseRadiusWithDistance", "1" ).c_str() ), 0, 1 );
                m_ConfigAoAmount = std::clamp( static_cast<float>(std::atof( ao_value( "Amount", "1.0" ).c_str() )), 0.0f, 2.0f );
                // 'FadeDistance' in metres. INIs before it have 'FadeDepth' (0 ... 1 on the normalised depth with far plane
                // 'FadeFarPlane'); those keep working unchanged as long as 'FadeDistance' is missing.
                {
                    const std::string distance = ao_value( "FadeDistance", "" );
                    const std::string old_fade = ao_value( "FadeDepth", "" );
                    if (!distance.empty() || old_fade.empty())
                        m_ConfigAoFadeDistance = std::clamp( static_cast<float>(std::atof( distance.empty() ? "250" : distance.c_str() )), 1.0f, 20000.0f );
                    else
                    {
                        m_ConfigAoFadeDistance = -1.0f;
                        m_ConfigAoFade = std::clamp( static_cast<float>(std::atof( old_fade.c_str() )), 0.01f, 1.0f );
                        m_ConfigAoFarPlane = std::clamp( static_cast<float>(std::atof( ao_value( "FadeFarPlane", "1000" ).c_str() )), 10.0f, 100000.0f );
                    }
                }
                m_ConfigAoFilter = std::clamp( std::atoi( ao_value( "FilterQuality", "2" ).c_str() ), 0, 2 );
                m_ConfigAoDebug = std::clamp( std::atoi( ao_value( "Debug", "0" ).c_str() ), 0, 1 );
                // 9zd: exact weighting, thickness, depth pyramid, accumulation over the frames, multi bounce, fog.
                m_ConfigAoMethod = std::clamp( std::atoi( ao_value( "Method", "0" ).c_str() ), 0, 1 );
                m_ConfigAoThickness = std::clamp( static_cast<float>(std::atof( ao_value( "Thickness", "1.0" ).c_str() )), 0.1f, 4.0f );
                m_ConfigAoAngleBias = std::clamp( static_cast<float>(std::atof( ao_value( "AngleBias", "10" ).c_str() )), 0.0f, 30.0f );
                m_ConfigAoFill = std::clamp( static_cast<float>(std::atof( ao_value( "Fill", "0.35" ).c_str() )), 0.0f, 1.0f );
                m_ConfigAoDepthMips = std::clamp( std::atoi( ao_value( "DepthMips", "0" ).c_str() ), 0, 1 );
                m_ConfigAoHalfRes = std::clamp( std::atoi( ao_value( "HalfRes", "0" ).c_str() ), 0, 1 );
                m_ConfigAoTemporal = std::clamp( std::atoi( ao_value( "Temporal", "0" ).c_str() ), 0, 1 );
                m_ConfigAoTemporalBlend = std::clamp( static_cast<float>(std::atof( ao_value( "TemporalBlend", "0.85" ).c_str() )), 0.0f, 0.97f );
                m_ConfigAoMultiBounce = std::clamp( std::atoi( ao_value( "MultiBounce", "0" ).c_str() ), 0, 1 );
                m_ConfigAoFogFade = std::clamp( std::atoi( ao_value( "FogFade", "0" ).c_str() ), 0, 1 );
                Log( "Config ambient occlusion: Enable %d, Quality %d, ShadingRate %d, SampleRadius %.3f, IncreaseRadiusWithDistance %d, Amount %.2f, FadeDistance %.0f m%s, FilterQuality %d, Debug %d",
                    m_ConfigAo, m_ConfigAoQuality, m_ConfigAoShadingRate, m_ConfigAoRadius, m_ConfigAoGrow, m_ConfigAoAmount,
                    m_ConfigAoFadeDistance > 0.0f ? m_ConfigAoFadeDistance : 0.0f, m_ConfigAoFadeDistance > 0.0f ? "" : " (old FadeDepth/FadeFarPlane)",
                    m_ConfigAoFilter, m_ConfigAoDebug );
                if (m_ConfigAoFadeDistance <= 0.0f)
                    Log( "Config ambient occlusion: old keys FadeDepth %.3f FadeFarPlane %.0f, please use FadeDistance (metres)", m_ConfigAoFade,
                        m_ConfigAoFarPlane );
                Log( "Config ambient occlusion: AngleBias %.1f, Fill %.2f, HalfRes %d", m_ConfigAoAngleBias, m_ConfigAoFill, m_ConfigAoHalfRes );
                Log( "Config ambient occlusion (9zd): Method %d, Thickness %.2f, DepthMips %d, Temporal %d, TemporalBlend %.2f, MultiBounce %d, FogFade %d",
                    m_ConfigAoMethod, m_ConfigAoThickness, m_ConfigAoDepthMips, m_ConfigAoTemporal, m_ConfigAoTemporalBlend, m_ConfigAoMultiBounce,
                    m_ConfigAoFogFade );
            }
            // The game's light decal (Y2T1, Y2T2, doc/LICHT.md 12) is blended over the body with the same state as glass.
            const std::string skip = car_value( "SkipTextures", "4A522215,984E4128" );
            m_ConfigCarReflSkip.clear();
            for (std::size_t at = 0; at < skip.size();)
            {
                const std::size_t end = std::min( skip.find_first_of( ", ", at ), skip.size() );
                if (end > at)
                    m_ConfigCarReflSkip.push_back( static_cast<std::uint32_t>(std::strtoul( skip.substr( at, end - at ).c_str(), nullptr, 16 )) );
                at = end + 1;
            }
            m_ConfigCarReflGround = std::clamp( static_cast<float>(std::atof( car_value( "Ground", "0.35" ).c_str() )), 0.0f, 1.0f );
            // Cloth tops 37 (folding) and 41 (fixed), the interior and what lies under the car; the numbers are the same
            // for all cars (part list of the Carparts Editor, doc/LICHT.md 12). "part/variant" for one variant.
            const std::string matte = car_value( "MatteParts", "9,11,12,15,23,24,25,26,28,29,30,33,37,41,43,44,45,46,49,56,59" );
            m_ConfigCarReflMatte.clear();
            for (std::size_t at = 0; at < matte.size();)
            {
                const std::size_t end = std::min( matte.find_first_of( ", ", at ), matte.size() );
                if (end > at)
                {
                    const std::string item = matte.substr( at, end - at );
                    const std::size_t divider = item.find( '/' );
                    const int part = std::atoi( item.c_str() );
                    const int variant = divider != std::string::npos ? std::atoi( item.c_str() + divider + 1 ) : 0xFF;
                    if (part >= 0 && part < 256 && variant >= 0 && variant < 256)
                        m_ConfigCarReflMatte.push_back( static_cast<std::uint32_t>(part << 8 | variant) );
                }
                at = end + 1;
            }
            {
                const std::string solid = car_value( "MatteSolid", "41" );
                m_ConfigCarReflMatteSolid.clear();
                for (std::size_t at = 0; at < solid.size();)
                {
                    const std::size_t end = std::min( solid.find_first_of( ", ", at ), solid.size() );
                    if (end > at)
                    {
                        const int part = std::atoi( solid.substr( at, end - at ).c_str() );
                        if (part >= 0 && part < 256)
                            m_ConfigCarReflMatteSolid.push_back( part );
                    }
                    at = end + 1;
                }
                Log( "Config car reflections: matte solid '%s', keep %d, grey %d", solid.c_str(), m_ConfigCarReflMatteKeep,
                    m_ConfigCarReflMatteGrey );
            }
            Log( "Config car reflections: Reflections %d strength %.2f reflectance %.2f fresnel %.1f blur %.1f ground %.2f debug %d, skip '%s', "
                "matte '%s'", m_ConfigCarRefl, m_ConfigCarReflStrength, m_ConfigCarReflF0, m_ConfigCarReflFresnel, m_ConfigCarReflBlur, m_ConfigCarReflGround,
                m_ConfigCarReflDebug, skip.c_str(), matte.c_str() );
            Log( "Config car reflections: SourceScale %.1f, BlurChain %d", m_ConfigCarReflSourceScale, m_ConfigCarReflBlurChain );
            if (m_ConfigCarRefl > 0)
                InstallCarReflHook();
        }
        InstallPrecipHooks();

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

        if (m_ConfigHeadlights > 0)
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
        m_ScenePsEarly.Reset();
        m_SceneLayout.Reset();
        m_PresentVs.Reset();
        m_PresentPs.Reset();
        m_MoviePs.Reset();
        m_MovieView.Reset();
        m_MovieTexture.Reset();
        m_MovieWidth = 0;
        m_MovieHeight = 0;
        m_ClearVs.Reset();
        m_ClearPs.Reset();
        m_MirrorVs.Reset();
        m_MirrorPs.Reset();
        m_CarReflDownPs.Reset();
        m_CarReflResolveView.Reset();
        m_CarReflResolve.Reset();
        m_CarReflSourceRtv.Reset();
        m_BlurDownPs.Reset();
        m_BlurUpPs.Reset();
        m_BlurChainView.Reset();
        for (auto & view : m_BlurDownRtv)
            view.Reset();
        for (auto & view : m_BlurDownSrv)
            view.Reset();
        for (auto & view : m_BlurChainRtv)
            view.Reset();
        m_BlurDown.Reset();
        m_BlurChain.Reset();
        m_BlurChainValid = false;
        m_ReflCompositeVs.Reset();
        m_SkyEdgeCopyView.Reset();
        m_SkyEdgeCopy.Reset();
        m_SkyEdgeTableRtv.Reset();
        m_SkyEdgeTableView.Reset();
        m_SkyEdgeTable.Reset();
        m_SkyEdgeSmoothRtv.Reset();
        m_SkyEdgeSmoothView.Reset();
        m_SkyEdgeSmooth.Reset();
        m_SkyEdgeSmoothPs.Reset();
        m_SkyEdgeColumnPs.Reset();
        m_SkyEdgeFillPs.Reset();
        m_SkyEdgeConstants.Reset();
        m_SkyEdgeRaster.Reset();
        m_SkyEdgeState = 0;
        m_ReflCompositePs.Reset();
        m_ReflPackPs.Reset();
        m_ReflPackMsPs.Reset();
        m_ReflMaskRtv.Reset();
        m_ReflMaskSrv.Reset();
        m_ReflMask.Reset();
        m_ReflMaskDepthRtv.Reset();
        m_ReflMaskDepthSrv.Reset();
        m_ReflMaskDepth.Reset();
        m_AoRawRtv.Reset();
        m_AoRawSrv.Reset();
        m_AoRaw.Reset();
        m_AoTempRtv.Reset();
        m_AoTempSrv.Reset();
        m_AoTemp.Reset();
        m_AoHistoryRtv.Reset();
        m_AoHistorySrv.Reset();
        m_AoHistory.Reset();
        for (int level = 0; level < AO_DEPTH_LEVELS; ++level)
        {
            m_AoDepthLevelSrv[level].Reset();
            m_AoDepthLevelRtv[level].Reset();
        }
        m_AoDepthMipsSrv.Reset();
        m_AoDepthMips.Reset();
        m_AoPrevValid = false;
        m_AoConstants.Reset();
        m_AoMultiply.Reset();
        m_AoRaster.Reset();
        m_AoPs.Reset();
        m_AoFilterPs.Reset();
        m_AoApplyPs.Reset();
        m_AoDepthHalfPs.Reset();
        m_AoDepthMipPs.Reset();
        m_AoUpsamplePs.Reset();
        m_AoHalfRtv.Reset();
        m_AoHalfSrv.Reset();
        m_AoHalf.Reset();
        m_MirrorConstants.Reset();
        m_ReflBlends.clear();
        m_ReflNoColour.Reset();
        m_ReflShoreDebug.Reset();
        m_ReflSky.Reset();
        m_ReflSkyView.Reset();
        m_SkyImageFlip = 0xFFFFFFFFu; // B36: no held sky image without its texture
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
        m_CarReflVertexBuffer.Reset();
        m_CarReflIndexBuffer.Reset();
        m_CarReflPending.clear();
        m_CarReflPendingCount = 0;
        m_CarReflWeldCache.clear();
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
                // '[Dx11] SkyEdgeFill' (B32): alpha 1 = all clear colour, the sky draws lower it.
                if (m_ConfigSkyEdgeFill != 0)
                {
                    color[3] = 1.0f;
                    std::memcpy( m_SkyEdgeClear, color, sizeof( color ) );
                    m_SkyEdgeState = 1;
                }
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
        // B36: a sky image held without water ('SkyHold' -1) belongs to its race; the menus end it.
        if (*reinterpret_cast<const std::int32_t *>(0x0065B928) != 0)
        {
            m_SkyImageFlip = 0xFFFFFFFFu;
            m_CarSkyBlend = 0.0f;
        }
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
        if (m_ConfigCarRefl > 0)
            CarReflReport();
        ProbeFlip();


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

        if (m_BatchPrimitive != Primitive::TRIANGLES || m_ProbeX < 0.0f)
            return;
        // Draws without depth test count in the list (B32: sky, overlays), not for the distance.
        const bool depth = render[rs::ZENABLE] != 0;

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
            if (depth && rhw > m_ProbeBest)
                m_ProbeBest = rhw;
            if (m_ProbeHits.size() < 160)
            {
                const auto mix = [&]( int shift, std::uint32_t ca, std::uint32_t cb, std::uint32_t cc )
                {
                    const float v = l1 * static_cast<float>((ca >> shift) & 0xFF) + l2 * static_cast<float>((cb >> shift) & 0xFF) +
                        l3 * static_cast<float>((cc >> shift) & 0xFF);
                    return static_cast<std::uint32_t>(std::clamp( v + 0.5f, 0.0f, 255.0f ));
                };
                ProbeHit hit;
                hit.m_Rhw = rhw;
                hit.m_Hash = m_Bound[0] != nullptr ? m_Bound[0]->m_Hash : 0;
                hit.m_AlphaMid = m_Bound[0] != nullptr && m_Bound[0]->m_Translucent ? m_Bound[0]->m_AlphaMid : 0.0f;
                hit.m_Blend = render[rs::ALPHABLENDENABLE] != 0 ? 1 : 0;
                hit.m_Source = static_cast<std::uint8_t>(render[rs::SRCBLEND]);
                hit.m_Destination = static_cast<std::uint8_t>(render[rs::DESTBLEND]);
                hit.m_DepthWrite = render[rs::ZWRITEENABLE] != 0 ? 1 : 0;
                hit.m_Water = IsWaterTexture() ? 1 : 0;
                hit.m_Shore = m_Bound[0] != nullptr &&
                    std::find( m_ConfigShoreTextures.begin(), m_ConfigShoreTextures.end(), m_Bound[0]->m_Hash ) != m_ConfigShoreTextures.end() ? 1 : 0;
                hit.m_Sky = BatchIsSky() ? 1 : 0;
                hit.m_ZEnable = depth ? 1 : 0;
                hit.m_ZFunc = static_cast<std::uint8_t>(render[rs::ZFUNC]);
                hit.m_Z = l1 * a.m_Z + l2 * b.m_Z + l3 * c.m_Z;
                hit.m_Diffuse = (mix( 24, a.m_Diffuse, b.m_Diffuse, c.m_Diffuse ) << 24) | (mix( 16, a.m_Diffuse, b.m_Diffuse, c.m_Diffuse ) << 16) |
                    (mix( 8, a.m_Diffuse, b.m_Diffuse, c.m_Diffuse ) << 8) | mix( 0, a.m_Diffuse, b.m_Diffuse, c.m_Diffuse );
                hit.m_Fog = render[rs::FOGENABLE] != 0 ? 1 : 0;
                hit.m_FogFactor = static_cast<std::uint8_t>(mix( 24, a.m_Specular, b.m_Specular, c.m_Specular ));
                hit.m_FogColour = static_cast<std::uint32_t>(render[rs::FOGCOLOR]) & 0xFFFFFFu;
                hit.m_SkyEdge = static_cast<std::uint8_t>(m_SkyEdgeState);
                hit.m_Vertices = static_cast<std::uint32_t>(m_BatchVertices.size());
                hit.m_CarMask = m_CarLodMask;
                hit.m_CarPart = m_CarPart;
                hit.m_AlphaMin = m_Bound[0] != nullptr ? m_Bound[0]->m_AlphaMin : 255;
                hit.m_Translucent = m_Bound[0] != nullptr && m_Bound[0]->m_Translucent ? 1 : 0;
                m_ProbeHits.push_back( hit );
            }
            break; // one hit per draw is enough
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
        // Once a second: the draws under the probe, in drawing order (shore textures for '[Water] ShoreTextures').
        if (m_ProbeOn && !m_ProbeHits.empty() && ::GetTickCount64() - m_ProbeLogTick >= 1000)
        {
            m_ProbeLogTick = ::GetTickCount64();
            Log( "Distance probe draws (%zu) at %.0f %.0f, in drawing order:", m_ProbeHits.size(), m_ProbeX, m_ProbeY );
            for (const ProbeHit & hit : m_ProbeHits)
            {
                Log( "  %08X d %.1f %s%s%sblend %u %u/%u zw %u alpha mid %.2f | z %.4f ztest %u func %u, ARGB %08X, fog %u factor %u "
                    "colour %06X, %u vertices, sky edge state %u",
                    hit.m_Hash, hit.m_Rhw > 0.0f ? 1.0f / hit.m_Rhw : 0.0f, hit.m_Sky ? "SKY " : "", hit.m_Water ? "water " : "",
                    hit.m_Shore ? "shore " : "", hit.m_Blend, hit.m_Source, hit.m_Destination, hit.m_DepthWrite, hit.m_AlphaMid, hit.m_Z,
                    hit.m_ZEnable, hit.m_ZFunc, hit.m_Diffuse, hit.m_Fog, hit.m_FogFactor, hit.m_FogColour, hit.m_Vertices, hit.m_SkyEdge );
                // B36: car draws, what the reflections made of them. The paint is cut where texel alpha x vertex alpha is
                // under the glass reference (0.95 for blended paint).
                if (hit.m_CarMask != 0)
                    Log( "    car pass %02X part %d/%d, texture alpha min %u%s: %s, glass ref %.2f", hit.m_CarMask,
                        hit.m_CarPart >= 0 ? hit.m_CarPart >> 8 : -1, hit.m_CarPart >= 0 ? hit.m_CarPart & 0xFF : -1, hit.m_AlphaMin,
                        hit.m_Translucent ? " translucent" : "", hit.m_CarOutcome != nullptr ? hit.m_CarOutcome : "not looked at",
                        hit.m_CarGlassRef );
                if (hit.m_CarMask != 0 && hit.m_CarDetail[0] != 0)
                    Log( "      %s", hit.m_CarDetail );
            }
        }
        m_ProbeHits.clear();
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
        PresentBackBuffer();
    }


    void Driver::PresentBackBuffer()
    {
        if (!m_SwapChain)
            return;

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


    bool Driver::MovieBlit(
        const void * pixels,
        int pitch,
        int width,
        int height,
        int x,
        int y,
        int scale
    )
    {
        // Only for a movie drawn into the front buffer.
        if (!m_ConfigMovie || !m_MoviePs || !m_Front || !IsFrontSelected() || pixels == nullptr || scale < 1 || width < 8 || height < 8 ||
            width > 2048 || height > 2048 || pitch < width * 2 || (width & 1) != 0)
        {
            return false;
        }

        Flush();

        if (m_MovieWidth != width || m_MovieHeight != height || !m_MovieTexture)
        {
            m_MovieView.Reset();
            m_MovieTexture.Reset();

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(width);
            desc.Height = static_cast<UINT>(height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_MovieTexture.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_MovieTexture.Get(), nullptr, m_MovieView.Put() ) ))
            {
                m_MovieView.Reset();
                m_MovieTexture.Reset();
                return false;
            }

            m_MovieWidth = width;
            m_MovieHeight = height;
            m_MovieBarTop = -1;
            m_MovieBarBottom = -1;
        }

        // A pause between frames means another movie, its bars are measured again.
        const DWORD now = ::GetTickCount();

        if (now - m_MovieTick > 500)
        {
            m_MovieBarTop = -1;
            m_MovieBarBottom = -1;
        }

        m_MovieTick = now;

        if (!m_MovieLogged)
        {
            m_MovieLogged = true;
            Log( "Movie frame %dx%d at %d,%d scale %d, bicubic sharpness %.2f", width, height, x, y, scale, m_MovieSharpness );
        }

        m_MoviePixels.resize( static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 );
        FromMovieYuv( static_cast<const std::uint8_t *>(pixels), static_cast<std::uint32_t>(pitch), m_MoviePixels.data(), static_cast<std::uint32_t>(width) * 4, width, height );
        m_Context->UpdateSubresource( m_MovieTexture.Get(), 0, nullptr, m_MoviePixels.data(), static_cast<UINT>(width) * 4, 0 );

        // A movie as wide as the screen fills the window, in the aspect ratio of what is left
        // after the black bars of the frame, the smallest bars seen so far.
        if (m_ConfigMovieFill && x == 0 && width * scale == m_Width && m_BackBufferView && !::IsIconic( m_Window ))
        {
            int top = 0;
            int bottom = 0;

            if (FindMovieBars( static_cast<const std::uint8_t *>(pixels), static_cast<std::uint32_t>(pitch), width, height, top, bottom ))
            {
                const int previous_top = m_MovieBarTop;
                const int previous_bottom = m_MovieBarBottom;
                m_MovieBarTop = m_MovieBarTop < 0 ? top : std::min( m_MovieBarTop, top );
                m_MovieBarBottom = m_MovieBarBottom < 0 ? bottom : std::min( m_MovieBarBottom, bottom );

                if (m_MovieBarTop != previous_top || m_MovieBarBottom != previous_bottom)
                {
                    Log( "Movie black bars %d at the top, %d at the bottom", m_MovieBarTop, m_MovieBarBottom );
                }
            }

            int cut_top = std::max( m_MovieBarTop, 0 );
            int cut_bottom = std::max( m_MovieBarBottom, 0 );

            // A frame that is mostly bars is not trusted.
            if (cut_top + cut_bottom > height / 2)
            {
                cut_top = 0;
                cut_bottom = 0;
            }

            const int visible = height - cut_top - cut_bottom;
            const float region[4] = { 0, static_cast<float>(cut_top) / static_cast<float>(height), 1, static_cast<float>(visible) / static_cast<float>(height) };

            const float target_width = static_cast<float>(m_BackBufferWidth);
            const float target_height = static_cast<float>(m_BackBufferHeight);
            const float movie_width = static_cast<float>(width * scale);
            const float movie_height = static_cast<float>(visible * scale);
            const float fit = std::min( target_width / movie_width, target_height / movie_height );
            const float fitted_width = std::round( movie_width * fit );
            const float fitted_height = std::round( movie_height * fit );
            const float place[4] = { std::floor( (target_width - fitted_width) * 0.5f ), std::floor( (target_height - fitted_height) * 0.5f ), fitted_width, fitted_height };

            const float black[4] = { 0, 0, 0, 1 };
            m_Context->ClearRenderTargetView( m_BackBufferView.Get(), black );
            Blit( m_MovieView.Get(), m_BackBufferView.Get(), m_BackBufferWidth, m_BackBufferHeight, place, region, m_Brightness, m_PointSampler.Get(), m_MoviePs.Get(),
                m_MovieSharpness );
            PresentBackBuffer();
            return true;
        }

        // Edges snap to whole pixels of the frame so the picture has no soft border.
        const float left = std::round( static_cast<float>(x) * m_ScaleX );
        const float top = std::round( static_cast<float>(y) * m_ScaleY );
        const float right = std::round( static_cast<float>(x + width * scale) * m_ScaleX );
        const float bottom = std::round( static_cast<float>(y + height * scale) * m_ScaleY );
        const float destination[4] = { left, top, right - left, bottom - top };
        const float source[4] = { 0, 0, 1, 1 };

        Blit( m_MovieView.Get(), m_FrontTarget.Get(), m_RenderWidth, m_RenderHeight, destination, source, 1.0f, m_PointSampler.Get(), m_MoviePs.Get(), m_MovieSharpness );
        PresentFront();
        return true;
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
                    texture->m_AlphaMid = 0.0f;
                    texture->m_AlphaMin = 0;
                    if (texture->m_Format == formats::DXT3)
                    {
                        // 16 blocks of 4 bit alpha ahead of the colour of each 16 byte block.
                        std::size_t mid = 0;
                        const std::size_t blocks = converted.size() / 16;
                        for (std::size_t block = 0; block < blocks; ++block)
                            for (std::size_t i = 0; i < 8; ++i)
                            {
                                const std::uint8_t pair = converted[block * 16 + i];
                                mid += ((pair & 0x0F) >= 2 && (pair & 0x0F) <= 13 ? 1u : 0u) + ((pair >> 4) >= 2 && (pair >> 4) <= 13 ? 1u : 0u);
                            }
                        texture->m_AlphaMid = blocks > 0 ? static_cast<float>(mid) / static_cast<float>(blocks * 16) : 0.0f;
                        std::uint8_t lowest = 15;
                        for (std::size_t block = 0; block < blocks; ++block)
                            for (std::size_t i = 0; i < 8; ++i)
                                lowest = std::min<std::uint8_t>( lowest, std::min<std::uint8_t>( converted[block * 16 + i] & 0x0F, converted[block * 16 + i] >> 4 ) );
                        texture->m_AlphaMin = static_cast<std::uint8_t>(lowest * 17);
                        std::size_t half = 0, clear = 0;
                        for (std::size_t block = 0; block < blocks; ++block)
                            for (std::size_t i = 0; i < 8; ++i)
                                for (const int a : { converted[block * 16 + i] & 0x0F, converted[block * 16 + i] >> 4 })
                                {
                                    half += a >= 6 && a <= 10 ? 1u : 0u;
                                    clear += a == 0 ? 1u : 0u;
                                }
                        texture->m_GlassPage = blocks > 0 && half * 100 >= blocks * 16 * 45 && clear * 100 < blocks * 16 * 2;
                    }
                }
                else
                {
                    const auto * texels = reinterpret_cast<const std::uint32_t *>(converted.data());
                    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(std::max( height, 1 ));

                    texture->m_Translucent = std::any_of( texels, texels + count, [] ( std::uint32_t texel )
                    {
                        return (texel >> 24) != 0xFF;
                    } );
                    const std::size_t mid = static_cast<std::size_t>(std::count_if( texels, texels + count, [] ( std::uint32_t texel )
                    {
                        const std::uint32_t alpha = texel >> 24;
                        return alpha >= 32 && alpha <= 224;
                    } ));
                    texture->m_AlphaMid = count > 0 ? static_cast<float>(mid) / static_cast<float>(count) : 0.0f;
                    std::uint32_t lowest = 255;
                    for (std::size_t i = 0; i < count; ++i)
                        lowest = std::min( lowest, texels[i] >> 24 );
                    texture->m_AlphaMin = static_cast<std::uint8_t>(lowest);
                    std::size_t half = 0, clear = 0;
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const std::uint32_t alpha = texels[i] >> 24;
                        half += alpha >= 96 && alpha <= 176 ? 1u : 0u;
                        clear += alpha == 0 ? 1u : 0u;
                    }
                    texture->m_GlassPage = count > 0 && half * 100 >= count * 45 && clear * 100 < count * 2;
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
        m_Textures.erase( it );
        return 1;
    }


    int Driver::TextureReset()
    {
        Flush();

        m_Bound.fill( nullptr );

        for (auto & entry : m_Textures)
        {
            entry.second->m_Magic = 0;
        }

        m_Textures.clear();

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

        }

        m_SamplerKeys[index] = key;
        m_SamplerCache[index] = cached.Get();
        return cached.Get();
    }

    bool Driver::UploadRing(
        const TlVertex * vertices,
        std::size_t vertex_count,
        const std::uint16_t * indices,
        std::size_t index_count,
        UINT & base_vertex,
        UINT & first_index
    )
    {
        if (vertex_count == 0 || index_count == 0 || vertex_count > RAW_VERTICES || index_count > RAW_INDICES)
            return false;

        // Both buffers start anew together, the draws already queued keep the old ones.
        const bool wrap = m_VertexCursor + vertex_count > RAW_VERTICES || m_IndexCursor + index_count > RAW_INDICES;
        if (wrap)
        {
            m_VertexCursor = 0;
            m_IndexCursor = 0;
        }
        const D3D11_MAP map = wrap ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_VertexBuffer.Get(), 0, map, 0, &mapped ) ))
        {
            m_VertexCursor = RAW_VERTICES;
            m_IndexCursor = RAW_INDICES;
            return false;
        }
        std::memcpy( static_cast<TlVertex *>(mapped.pData) + m_VertexCursor, vertices, vertex_count * sizeof( TlVertex ) );
        m_Context->Unmap( m_VertexBuffer.Get(), 0 );

        if (FAILED( m_Context->Map( m_IndexBuffer.Get(), 0, map, 0, &mapped ) ))
        {
            m_VertexCursor = RAW_VERTICES;
            m_IndexCursor = RAW_INDICES;
            return false;
        }
        std::memcpy( static_cast<std::uint16_t *>(mapped.pData) + m_IndexCursor, indices, index_count * sizeof( std::uint16_t ) );
        m_Context->Unmap( m_IndexBuffer.Get(), 0 );

        base_vertex = static_cast<UINT>(m_VertexCursor);
        first_index = static_cast<UINT>(m_IndexCursor);
        m_VertexCursor += vertex_count;
        m_IndexCursor += index_count;
        return true;
    }


    void Driver::UploadDrawConstants(
        const void * data,
        std::size_t size
    )
    {
        size = std::min( size, sizeof( DrawConstants ) );
        if (size != sizeof( DrawConstants ))
        {
            // A shorter block leaves the rest of the buffer undefined, the next upload compares against nothing.
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            m_ConstantsValid = false;
            if (SUCCEEDED( m_Context->Map( m_DrawConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            {
                std::memcpy( mapped.pData, data, size );
                m_Context->Unmap( m_DrawConstants.Get(), 0 );
            }
            return;
        }

        // The block is the head (everything before the light arrays) and the first m_Extra / m_Rear entries of each array: the
        // shader reads no more of them, so only these are compared and written (a draw without lights moves 400 bytes, not 3216).
        const auto & constants = *static_cast<const DrawConstants *>(data);
        const std::size_t extra = static_cast<std::size_t>(std::clamp( constants.m_Extra[0], 0, MAX_CAR_LIGHTS ));
        const std::size_t rear = static_cast<std::size_t>(std::clamp( constants.m_Rear[0], 0, MAX_REAR_LIGHTS ));
        struct Region
        {
            std::size_t m_Offset;
            std::size_t m_Size;
        };
        const Region regions[6] = {
            { 0, offsetof( DrawConstants, m_ExtraColour ) },
            { offsetof( DrawConstants, m_ExtraColour ), extra * sizeof( constants.m_ExtraColour[0] ) },
            { offsetof( DrawConstants, m_ExtraViewToLight ), extra * sizeof( constants.m_ExtraViewToLight[0] ) },
            { offsetof( DrawConstants, m_RearLeft ), rear * sizeof( constants.m_RearLeft[0] ) },
            { offsetof( DrawConstants, m_RearRight ), rear * sizeof( constants.m_RearRight[0] ) },
            { offsetof( DrawConstants, m_RearViewToLight ), rear * sizeof( constants.m_RearViewToLight[0] ) } };
        const auto * source = static_cast<const std::uint8_t *>(data);
        auto * last = reinterpret_cast<std::uint8_t *>(&m_LastConstants);

        // Unchanged since the last upload: most batches of a frame share their state, only the texture differs.
        if (m_ConstantsValid)
        {
            bool same = true;
            for (const Region & region : regions)
                same = same && (region.m_Size == 0 || std::memcmp( last + region.m_Offset, source + region.m_Offset, region.m_Size ) == 0);
            if (same)
                return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_DrawConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            m_ConstantsValid = false;
            return;
        }
        std::size_t written = 0;
        for (const Region & region : regions)
        {
            if (region.m_Size == 0)
                continue;
            std::memcpy( static_cast<std::uint8_t *>(mapped.pData) + region.m_Offset, source + region.m_Offset, region.m_Size );
            std::memcpy( last + region.m_Offset, source + region.m_Offset, region.m_Size );
            written += region.m_Size;
        }
        m_Context->Unmap( m_DrawConstants.Get(), 0 );
        m_ConstantsValid = true;
    }



    // True when nothing in the scene shader can throw a pixel of this draw away: no alpha test, or an alpha test that the texture
    // cannot fail. The alpha of a pixel is at least the product of the smallest texture alphas, the vertex alpha (all opaque) and
    // the texture factor alpha; the shore and far fade of the water change the alpha and stay out.
    bool Driver::CannotDiscard(
        const DrawConstants & constants
    ) const
    {
        const auto & render = m_D3D.m_Render;
        if (render[rs::ALPHATESTENABLE] == 0 || constants.m_Flags[2] == 8)
            return true;
        if (m_ConfigEarlyZ < 2 || constants.m_AlphaRef[1] > 0.0f || constants.m_AlphaRef[3] > 0.0f)
            return false;
        const std::int32_t func = constants.m_Flags[2];
        if (func != 5 && func != 7)
            return false;
        float least = static_cast<float>(render[rs::TEXTUREFACTOR] >> 24) / 255.0f;
        for (const Texture * texture : { m_Bound[0], m_Bound[1] })
            if (texture != nullptr)
                least *= static_cast<float>(texture->m_AlphaMin) / 255.0f;
        if (m_BatchVertices.empty())
            return false;
        for (const TlVertex & vertex : m_BatchVertices)
            if ((vertex.m_Diffuse >> 24) != 0xFF)
                return false;
        const float reference = static_cast<float>(render[rs::ALPHAREF] & 0xFF);
        return least * 255.0f >= reference + 1.0f;
    }

    // The bounding sphere of the batch in view space, from the same vertex transform as the vertex shader (screen position and
    // rhw back to the camera space of the game). False when the vertices do not allow it (rhw 0, no projection yet).
    bool Driver::BatchViewSphere(
        float * centre,
        float & radius
    ) const
    {
        if (m_BatchVertices.empty() || m_HeadProjection[0] == 0.0f || m_HeadProjection[1] == 0.0f)
            return false;

        float low[3] = { 1e30f, 1e30f, 1e30f };
        float high[3] = { -1e30f, -1e30f, -1e30f };
        for (const TlVertex & vertex : m_BatchVertices)
        {
            if (!(vertex.m_Rhw > 0.0f && vertex.m_Rhw < 1e30f))
                return false;
            const float w = 1.0f / vertex.m_Rhw;
            const float view[3] = { (vertex.m_X - m_HeadProjection[2]) * w / m_HeadProjection[0],
                (vertex.m_Y - m_HeadProjection[3]) * w / m_HeadProjection[1], w };
            for (int axis = 0; axis < 3; ++axis)
            {
                low[axis] = std::min( low[axis], view[axis] );
                high[axis] = std::max( high[axis], view[axis] );
            }
        }
        float half = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            centre[axis] = 0.5f * (low[axis] + high[axis]);
            const float extent = 0.5f * (high[axis] - low[axis]);
            half += extent * extent;
        }
        // A margin for a light matrix with a little scale and for float noise.
        radius = std::sqrt( half ) * 1.1f + 0.5f;
        return true;
    }


    void Driver::ApplyPipeline()
    {
        const auto & render = m_D3D.m_Render;

        // '[Dx11] SkyEdgeFill' (B32): the sky comes right after the full clear; the first other draw ends it.
        m_SkyEdgeDraw = false;
        m_SkyEdgeFlatDraw = false;
        if (m_SkyEdgeState == 1 || m_SkyEdgeState == 2)
        {
            if (BatchIsSky())
            {
                // Some skies start with an opaque untextured plane of one vertex colour behind the band: it stands for the
                // clear colour (share stays 1), the band edge then blends into it like into the clear colour.
                if (m_SkyEdgeState == 1 && m_Bound[0] == nullptr && render[rs::ALPHABLENDENABLE] == 0)
                {
                    const std::uint32_t first = m_BatchVertices.front().m_Diffuse;
                    m_SkyEdgeFlatDraw = (first >> 24) == 0xFF && std::all_of( m_BatchVertices.begin(), m_BatchVertices.end(),
                        [first] ( const TlVertex & v ) { return v.m_Diffuse == first; } );
                }
                m_SkyEdgeState = 2;
                m_SkyEdgeDraw = m_EdgePass != 3;
            }
            else
            {
                if (m_SkyEdgeState == 2)
                    SkyEdgeFill();
                m_SkyEdgeState = 3;
            }
        }

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
        constants.m_Flags[0] = m_ConfigFogEnabled && render[rs::FOGENABLE] != 0;
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
        constants.m_Debug[0] = 0.0f;
        constants.m_Debug[1] = static_cast<float>(m_EdgePass);
        constants.m_Debug[2] = static_cast<float>(m_ConfigAlphaEdgeRef) / 255.0f;
        constants.m_Debug[3] = m_ConfigAlphaMipScale;
        constants.m_Test[0] = m_ConfigTestVertexAlpha ? 1.0f : 0.0f;
        constants.m_Test[1] = m_ConfigNightEdgeLight ? 1.0f : 0.0f;
        // B33: light colour of the water for the see-through part of shore textures (z: RGB packed, exact in a float;
        // w: amount), only on shore draws (g_AlphaRef.y > 0) and once the water colour is known.
        constants.m_Test[2] = 0.0f;
        constants.m_Test[3] = 0.0f;
        if (m_ConfigShoreWaterLight > 0.0f && constants.m_AlphaRef[1] > 0.0f && m_WaterColour[0] >= 0.0f)
        {
            std::uint32_t rgb = 0;
            for (int c = 0; c < 3; ++c)
                rgb = rgb << 8 | static_cast<std::uint32_t>(std::clamp( std::lround( m_WaterColour[static_cast<std::size_t>(c)] ), 0L, 255L ));
            constants.m_Test[2] = static_cast<float>(rgb);
            constants.m_Test[3] = m_ConfigShoreWaterLight;
        }
        constants.m_Clip[0] = static_cast<float>(m_Clip.left);
        constants.m_Clip[1] = static_cast<float>(m_Clip.top);
        constants.m_Clip[2] = static_cast<float>(m_Clip.right);
        constants.m_Clip[3] = static_cast<float>(m_Clip.bottom);

        float flat_z[2] = { 1e30f, -1e30f }, flat_rhw[2] = { 1e30f, -1e30f }, flat_n[2] = { 1e30f, -1e30f };
        for (const TlVertex & v : m_BatchVertices)
        {
            // World vertices follow z = 1 - n / w = 1 - n * rhw with n the near plane (0.15 bonnet, 0.5 chase view).
            const float n = v.m_Rhw > 1e-9f ? (1.0f - v.m_Z) / v.m_Rhw : 1e9f;
            flat_n[0] = std::min( flat_n[0], n );
            flat_n[1] = std::max( flat_n[1], n );
            flat_z[0] = std::min( flat_z[0], v.m_Z );
            flat_z[1] = std::max( flat_z[1], v.m_Z );
            flat_rhw[0] = std::min( flat_rhw[0], v.m_Rhw );
            flat_rhw[1] = std::max( flat_rhw[1], v.m_Rhw );
        }
        // The HUD phase (Hud_Draw) runs R_SetZFunc 7 = depth func ALWAYS, the world uses LESSEQUAL; rhw 1 and up is screen space. A batch of
        // one camera facing quad (billboard foliage) also has one depth for all its vertices: that alone is no HUD.
        const bool flat_draw = !m_BatchVertices.empty() && (flat_rhw[0] >= 0.999f || render[rs::ZFUNC] == 8);

        // Night over a day track (LFSP 'NightGrade'), per draw one of:
        //  1 world: opaque or normally blended draws with depth writing, factor on the world without its lights;
        //  2 sky: depth func ALWAYS at the far end (z near 1, rhw tiny), factor of 'SkyDarken' on the sky and its clouds;
        //  3 light sprites: additive draws (flares, lamps, car light sprites), brightened by 'NightLightBoost'.
        // The HUD (also depth func ALWAYS, but not far), sprites with normal blending and no depth write stay as they are.
        {
            using NightFn = int( __cdecl * )(float *);
            static NightFn night_fn = nullptr;
            static bool night_resolved = false;
            if (!night_resolved)
            {
                night_resolved = true;
                if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                    night_fn = reinterpret_cast<NightFn>(::GetProcAddress( lfsp, "LfspNightGradeEx" ));
                Log( "Night grade (dx11z build 9zk, settings menu removed): LFSP export %s", night_fn != nullptr ? "found" : "not available" );
            }
            const std::uint32_t night_source = render[rs::SRCBLEND];
            const std::uint32_t night_destination = render[rs::DESTBLEND];
            const bool night_normal = night_source == 12 || (night_source == 5 && night_destination == 6);
            const bool night_additive = render[rs::ALPHABLENDENABLE] != 0 && !night_normal && night_destination == 2;
            float grade[9] = {}; // world rgb, sky rgb, light boost rgb
            static std::uint64_t night_counts[6] = {};
            int mode = 0;
            m_NightGradeSeen = night_fn != nullptr && night_fn( grade ) != 0;
            // Weather (LFSP 'Storm', 'Lightning', 'SnowWhite', PLAN.md 15.2): read once per frame, the same draw classes as the
            // night. Storm and lightning multiply the factors of world and sky; snow white is for track draws of class 1 only.
            const bool weather_grade = WeatherFxFrame() && m_WeatherGrade;
            const bool weather_snow = m_WeatherFxOn && m_WeatherFx[7] > 0.0f;
            if (weather_grade)
            {
                if (!m_NightGradeSeen)
                    std::fill( std::begin( grade ), std::end( grade ), 1.0f );
                for (int c = 0; c < 3; ++c)
                {
                    grade[c] *= m_WeatherFx[c];
                    grade[3 + c] *= m_WeatherFx[3 + c];
                }
                if (m_WeatherFx[6] > 1.0f && !flat_draw)
                    constants.m_Weather2[0] = m_WeatherFx[6];
            }
            if (m_NightGradeSeen || weather_grade || weather_snow)
            {
                const bool sky = render[rs::ZFUNC] == 8 && !m_BatchVertices.empty() && flat_z[0] >= 0.99f && flat_rhw[1] < 0.05f;
                if (sky)
                    mode = (render[rs::ALPHABLENDENABLE] == 0 || night_normal) ? 2 : 0;
                else if (flat_draw)
                    mode = 0;
                else if (night_additive)
                    mode = 3;
                else if (render[rs::ZENABLE] != 0 && (render[rs::ALPHABLENDENABLE] == 0 ||
                    (night_normal && (render[rs::ZWRITEENABLE] != 0 || m_ConfigNightGradeBlended != 0))))
                    mode = 1;
                if (m_NightGradeSeen)
                {
                // Which textures stay bright (not graded, not HUD) and which alpha tested ones are graded, to find rims (log below).
                {
                    const std::uint32_t texture = m_Bound[0] != nullptr ? static_cast<std::uint32_t>(m_Bound[0]->m_Hash) : 0u;
                    auto & seen = m_NightTextures[texture];
                    if (mode == 0 && !flat_draw)
                        ++seen[0];
                    else if (mode == 1 && render[rs::ALPHATESTENABLE] != 0)
                        ++seen[1];
                    else if (mode == 0)
                        ++seen[2];
                }
                ++night_counts[mode == 0 ? (flat_draw && !sky ? 4 : 5) : mode - 1 + (mode > 0 ? 0 : 0)];
                static std::uint32_t night_logged_flip = 0;
                static int night_examples = 0;
                if (m_FlipCount / 600 != night_logged_flip)
                {
                    night_logged_flip = m_FlipCount / 600;
                    Log( "Night grade: world %llu, sky %llu, light sprites %llu, HUD/2D %llu, other sprites %llu (frame %u, blended without depth write %s)",
                        static_cast<unsigned long long>(night_counts[0]), static_cast<unsigned long long>(night_counts[1]),
                        static_cast<unsigned long long>(night_counts[2]), static_cast<unsigned long long>(night_counts[4]),
                        static_cast<unsigned long long>(night_counts[5]), m_FlipCount, m_ConfigNightGradeBlended != 0 ? "graded" : "not graded" );
                    // The textures with the most draws since the last report: kept bright (column 0), graded and alpha tested (1), HUD (2).
                    for (int column = 0; column < 3; ++column)
                    {
                        std::vector<std::pair<std::uint64_t, std::uint32_t>> top;
                        for (const auto & entry : m_NightTextures)
                            if (entry.second[static_cast<std::size_t>(column)] > 0)
                                top.emplace_back( entry.second[static_cast<std::size_t>(column)], entry.first );
                        std::sort( top.begin(), top.end(), []( const auto & a, const auto & b ) { return a.first > b.first; } );
                        std::string line;
                        for (std::size_t i = 0; i < top.size() && i < 8; ++i)
                        {
                            char item[40] = {};
                            std::snprintf( item, sizeof( item ), " %08X:%llu", top[i].second, static_cast<unsigned long long>(top[i].first) );
                            line += item;
                        }
                        Log( "Night grade textures (%s):%s", column == 0 ? "kept bright" : column == 1 ? "graded, alpha tested" : "HUD/2D", line.c_str() );
                    }
                    m_NightTextures.clear();
                }
                if (night_examples < 60 && (mode == 2 || mode == 3 || flat_draw) && (night_counts[mode == 0 ? 4 : mode - 1] % 4000 == 1))
                {
                    ++night_examples;
                    Log( "Night grade example: mode %d, %u vertices, z %g..%g, rhw %g..%g, zfunc %u, zwrite %u, blend %u src %u dst %u, texture %08X",
                        mode, static_cast<unsigned>(m_BatchVertices.size()), flat_z[0], flat_z[1], flat_rhw[0], flat_rhw[1],
                        static_cast<unsigned>(render[rs::ZFUNC]), static_cast<unsigned>(render[rs::ZWRITEENABLE]),
                        static_cast<unsigned>(render[rs::ALPHABLENDENABLE]), night_source, night_destination,
                        m_Bound[0] != nullptr ? static_cast<unsigned>(m_Bound[0]->m_Hash) : 0u );
                }
                }
            }
            // Light sprites (3) only at night; storm alone leaves them as they are.
            if (mode > 0 && (m_NightGradeSeen || (weather_grade && mode != 3)))
            {
                const float * rgb = &grade[(mode - 1) * 3];
                constants.m_Night[0] = rgb[0];
                constants.m_Night[1] = rgb[1];
                constants.m_Night[2] = rgb[2];
                constants.m_Night[3] = static_cast<float>(mode);
                if (mode == 1)
                    m_CarWeatherLightNext = (rgb[0] + rgb[1] + rgb[2]) / 3.0f; // light of the snow on the cars, next frame
            }
            // Snow white: track draws (track chunk draw, not a car part), not the water.
            m_SnowDraw = weather_snow && mode == 1 && m_TrackDrawDepth > 0 && m_CarLodMask == 0 && !IsWaterTexture();
            if (m_SnowDraw)
            {
                constants.m_Weather[0] = m_WeatherFx[7];
                constants.m_Weather[1] = m_WeatherFx[8];
                constants.m_Weather[2] = m_WeatherFx[9];
                constants.m_Weather[3] = m_WeatherFx[10];
                constants.m_Weather2[1] = m_WeatherFx[11];
                constants.m_Weather2[2] = m_WeatherFx[12];
                constants.m_Weather2[3] = m_WeatherFx[13];
                ++m_WeatherStats[0];
            }
            else if (weather_snow && mode == 1)
                ++m_WeatherStats[m_TrackDrawDepth > 0 ? 2 : 1];
        }

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
                // HUD and menus (ESC menu over the frozen race) are drawn with depth test on and flat vertices: all the same
                // depth and rhw 1 or higher (nothing nearer than a metre). The light of the world must not reach them.
                if (flat_draw)
                {
                    static int flat_logged = 0;
                    if (flat_logged < 12)
                    {
                        ++flat_logged;
                        Log( "Headlights objects: flat draw left out (HUD/menu), %u vertices, z %g..%g, rhw %g..%g", static_cast<unsigned>(m_BatchVertices.size()),
                            flat_z[0], flat_z[1], flat_rhw[0], flat_rhw[1] );
                    }
                    ++m_ObjectStats.m_NoDepth;
                }
                else
                {
                    ++m_ObjectStats.m_Lit;
                    object_light = true;
                }
            }
        }

        if ((m_HeadMesh || object_light) && m_HeadMapView)
        {
            constants.m_Head[0] = 1.0f;
            constants.m_Head[1] = m_HeadScale[0];
            constants.m_Head[2] = m_HeadScale[1];
            constants.m_Head[3] = m_HeadScale[2];
            std::memcpy( constants.m_HeadColour, m_HeadColour.data(), sizeof( constants.m_HeadColour ) );
            std::memcpy( constants.m_HeadProjection, m_HeadProjection.data(), sizeof( constants.m_HeadProjection ) );
            std::memcpy( constants.m_ViewToLight, m_HeadViewToLight.data(), sizeof( constants.m_ViewToLight ) );
            // Snow slope: the same screen side as the light, to track space (camera position in the last row).
            if (m_SnowDraw && m_HeadMesh)
            {
                std::memcpy( constants.m_SnowToWorld, m_HeadClipToObject.data(), sizeof( constants.m_SnowToWorld ) );
                constants.m_SnowToWorld[15] = 1.0f;
                ++m_WeatherStats[3];
            }
            const int extra_all = std::min( m_HeadExtraCount, std::min( static_cast<int>(m_CarLights.size()), MAX_CAR_LIGHTS ) );
            const int rear_all = std::min( m_HeadRearCount, std::min( static_cast<int>(m_RearLights.size()), MAX_REAR_LIGHTS ) );
            std::memcpy( constants.m_HeadHigh, m_HeadHigh.data(), sizeof( constants.m_HeadHigh ) );
            constants.m_RearShape[0] = m_ConfigTailOffset;
            constants.m_RearShape[1] = m_ConfigTailReach;
            constants.m_RearShape[2] = m_ConfigTailWidth;
            constants.m_RearShape[3] = m_ConfigTailSpread;

            // Light culling (exact, '[Lights] LightCulling'): a light whose area the bounding sphere of this batch does not touch
            // adds nothing to any of its pixels, so it stays out of the constants. The areas are those the shader tests: car lights
            // z in (0.5, 126.5) / z scale, x within 62.5 / x scale, height (map 255) / y scale; tail glows z behind the lamps up to
            // 6 reaches (the shader's own cut) and height up to 8.4 (e^(-height / 1.2) below 1/1000).
            float centre[3] = {};
            float radius = 0.0f;
            const bool cull = m_ConfigLightCulling != 0 && (extra_all > 0 || rear_all > 0) && m_HeadScale[0] > 1e-4f && m_HeadScale[1] > 1e-4f && m_HeadScale[2] > 1e-4f &&
                BatchViewSphere( centre, radius );
            int extra = 0;
            for (int i = 0; i < extra_all; ++i)
            {
                const float * m = m_HeadExtraViewToLight.data() + i * 16;
                if (cull)
                {
                    const float lx = centre[0] * m[0] + centre[1] * m[4] + centre[2] * m[8] + m[12];
                    const float ly = centre[0] * m[1] + centre[1] * m[5] + centre[2] * m[9] + m[13];
                    const float lz = centre[0] * m[2] + centre[1] * m[6] + centre[2] * m[10] + m[14];
                    if (lz + radius < 0.5f / m_HeadScale[1] || lz - radius > 126.5f / m_HeadScale[1] ||
                        std::abs( lx ) - radius > 62.5f / m_HeadScale[0] || std::abs( ly ) - radius > 255.0f / m_HeadScale[2])
                        continue;
                }
                std::memcpy( constants.m_ExtraColour[extra], m_CarLights[static_cast<std::size_t>(i)].m_Colour.data(), sizeof( constants.m_ExtraColour[extra] ) );
                std::memcpy( constants.m_ExtraViewToLight[extra], m, sizeof( constants.m_ExtraViewToLight[extra] ) );
                ++extra;
            }
            constants.m_Extra[0] = extra;
            int rear = 0;
            for (int i = 0; i < rear_all; ++i)
            {
                const RearLight & light = m_RearLights[static_cast<std::size_t>(i)];
                const float * m = m_HeadRearViewToLight.data() + i * 16;
                if (cull)
                {
                    const float ly = centre[0] * m[1] + centre[1] * m[5] + centre[2] * m[9] + m[13];
                    const float lz = centre[0] * m[2] + centre[1] * m[6] + centre[2] * m[10] + m[14];
                    const float offset = light.m_Left[3] > 0.0f ? light.m_Left[3] : m_ConfigTailOffset;
                    if (lz - offset + radius < -0.3f || lz - offset - radius > m_ConfigTailReach * 6.0f || std::abs( ly ) - radius > 8.4f)
                        continue;
                }
                std::memcpy( constants.m_RearLeft[rear], light.m_Left.data(), sizeof( constants.m_RearLeft[rear] ) );
                std::memcpy( constants.m_RearRight[rear], light.m_Right.data(), sizeof( constants.m_RearRight[rear] ) );
                std::memcpy( constants.m_RearViewToLight[rear], m, sizeof( constants.m_RearViewToLight[rear] ) );
                ++rear;
            }
            constants.m_Rear[0] = rear;
        }


        // Fog of the main view's world draws for the ambient occlusion ('[AmbientOcclusion] FogFade'): the first fogged draw
        // of the run (the track fog is the same for all of them). Mode 0 without the game's table is vertex fog the game
        // computed itself, it cannot be followed over the depth.
        if (m_ConfigAo && m_ConfigAoFogFade && m_AoFogMode < 0 && m_QueueList > 0 && m_ReflViewIsMain && constants.m_Flags[0] != 0 &&
            !flat_draw && !m_BackPass)
        {
            const int mode = static_cast<int>(constants.m_FogParams[3]);
            if (mode != 0 || m_FogTableActive)
            {
                m_AoFogMode = mode;
                m_AoFogParams[0] = constants.m_FogParams[0];
                m_AoFogParams[1] = constants.m_FogParams[1];
                m_AoFogParams[2] = constants.m_FogParams[2];
                m_AoFogParams[3] = constants.m_Weather2[0];
                m_AoFogTable = m_FogTable;
            }
        }

        UploadDrawConstants( &constants, sizeof( constants ) );

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
        const std::uint64_t blend_key = static_cast<std::uint64_t>(render[rs::ALPHABLENDENABLE] != 0) | (static_cast<std::uint64_t>(m_EdgePass == 3) << 32) |
            (static_cast<std::uint64_t>(render[rs::SRCBLEND] & 0xFF) << 8) |
            (static_cast<std::uint64_t>(render[rs::DESTBLEND] & 0xFF) << 16) | (static_cast<std::uint64_t>(m_SkyEdgeDraw) << 40) |
            (static_cast<std::uint64_t>(m_SkyEdgeDraw && m_SkyEdgeFlatDraw) << 41);

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
            // Alpha to coverage (AlphaEdgeFix = 3): the alpha picks the covered samples of the MSAA target, no blending on top.
            if (m_EdgePass == 3)
            {
                desc.AlphaToCoverageEnable = TRUE;
                target.BlendEnable = FALSE;
            }
            target.SrcBlend = ToBlend( source_blend, false );
            target.DestBlend = ToBlend( destination_blend, false );
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = ToBlend( source_blend, true );
            target.DestBlendAlpha = ToBlend( destination_blend, true );
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            // '[Dx11] SkyEdgeFill' (B32): alpha = share of the clear colour left, it only shrinks by the destination factor
            // (opaque: 0, alpha blended: times 1 - alpha, additive: unchanged).
            if (m_SkyEdgeDraw)
            {
                if (!target.BlendEnable)
                {
                    target.BlendEnable = TRUE;
                    target.SrcBlend = D3D11_BLEND_ONE;
                    target.DestBlend = D3D11_BLEND_ZERO;
                    target.DestBlendAlpha = D3D11_BLEND_ZERO;
                }
                target.SrcBlendAlpha = D3D11_BLEND_ZERO;
                // The flat plane: share 1 again (its alpha, vertex alpha 255 without texture).
                if (m_SkyEdgeFlatDraw)
                {
                    target.SrcBlendAlpha = D3D11_BLEND_ONE;
                    target.DestBlendAlpha = D3D11_BLEND_ZERO;
                }
            }
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
        // Not on car parts: their 64x64 window textures matched too, the bias put them in front of the car reflection (B34).
        const bool projected_candidate = !m_BackPass && m_CarLodMask == 0 && render[rs::ZENABLE] != 0 &&
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
        // No alpha test and no edge pass: nothing in the shader can discard this draw, so the depth test may run before it.
        const bool early_depth = m_ScenePsEarly && m_ConfigEarlyZ != 0 && m_EdgePass == 0 && !m_ShorePass && CannotDiscard( constants );
        m_Context->PSSetShader( early_depth ? m_ScenePsEarly.Get() : m_ScenePs.Get(), nullptr, 0 );
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


        const std::size_t probe_first = m_ProbeHits.size();
        m_ProbeFirst = probe_first;
        if (m_ProbeOn)
        {
            ProbeBatch();
        }


        // Blending while writing depth, see '[Dx11] AlphaEdgeFix'.
        const auto & render = m_D3D.m_Render;

        if (m_PrecipMark != 0)
            PrecipMeasureBatch();

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
        // Mode 2 (B33): not the mean but the ground level, the mean of the darkest quarter. At dusk and night the circle is
        // the car's own light; the mean then lit all the water brighter than the shore next to it, whose vertices keep the
        // game's colours, and the edge between them stood out.
        if (water && m_ConfigWaterEvenColour && !m_BatchVertices.empty())
        {
            double sum[3] = {};
            double n = 0.0;
            if (m_ConfigWaterEvenColour == 2)
            {
                std::vector<std::pair<std::uint32_t, std::uint32_t>> order; // brightness, colour
                order.reserve( m_BatchVertices.size() );
                for (const TlVertex & v : m_BatchVertices)
                    order.emplace_back( ((v.m_Diffuse >> 16) & 0xFF) * 2 + ((v.m_Diffuse >> 8) & 0xFF) * 5 + (v.m_Diffuse & 0xFF), v.m_Diffuse );
                const std::size_t take = std::max<std::size_t>( 1, order.size() / 4 );
                std::nth_element( order.begin(), order.begin() + static_cast<std::ptrdiff_t>(take - 1), order.end() );
                for (std::size_t i = 0; i < take; ++i)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += (order[i].second >> (16 - c * 8)) & 0xFF;
                n = static_cast<double>(take);
            }
            else
            {
                for (const TlVertex & v : m_BatchVertices)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += (v.m_Diffuse >> (16 - c * 8)) & 0xFF;
                n = static_cast<double>(m_BatchVertices.size());
            }
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
        // Not the skid marks: flat on the road they hardly show in the mirror, but after a while there are
        // a hundred thousand triangles of them, copied and drawn once more each frame.
        const std::uint32_t capture_source = render[rs::SRCBLEND];
        const std::uint32_t capture_destination = render[rs::DESTBLEND];
        // Not kept in the rear view mirror with '[Water] ReflectionMirrorView' 0, it gets no reflection.
        // '[Water] ReflectionLights' (9.14a): additive lights (SrcAlpha or One onto One) too, mirrored into a target of their own.
        const bool capture_light = m_ConfigReflectionLights != 0 && render[rs::ALPHABLENDENABLE] != 0 &&
            (capture_source == 5 || capture_source == 2) && capture_destination == 2;
        m_ReflCapture = reflections && !water && !m_SkidDraw && render[rs::ZENABLE] != 0 && (m_ConfigReflectionMirrorView != 0 || m_ReflViewIsMain) &&
            (render[rs::ALPHABLENDENABLE] == 0 || capture_source == 12 || (capture_source == 5 && capture_destination == 6) || capture_light);

        // The sky comes before the first queue run of the frame (list 0); kept as well and put in front of
        // every reflection of the frame, so water reflecting the sky is not left empty.
        if (m_ReflSkyPhase && m_ConfigReflections && m_QueueList == 0 && m_MirrorVs && m_ReflCompositePs &&
            m_BatchPrimitive == Primitive::TRIANGLES &&
            (render[rs::ALPHABLENDENABLE] == 0 || capture_source == 12 || (capture_source == 5 && capture_destination == 6)))
            m_ReflCapture = true;
        // Only with alpha test on, as the world is drawn. Without it the front end writes depth
        // with fully transparent quads on purpose, the profile list is masked that way (B10).
        const bool edge_candidate = (m_ConfigAlphaEdgeFix || m_ConfigAlphaToCoverage) && m_BatchPrimitive == Primitive::TRIANGLES &&
            render[rs::ALPHABLENDENABLE] != 0 && render[rs::ALPHATESTENABLE] != 0 && render[rs::ZENABLE] != 0 &&
            render[rs::ZWRITEENABLE] != 0 && BatchTranslucent();
        // Alpha to coverage (AlphaEdgeFix = 3) is for the alpha tested world of a race only. Menus and the HUD (front end, depth
        // func ALWAYS, screen space vertices) are half transparent gradients and glyph edges: eight coverage steps make bands and
        // stairs there. Those draws keep the two pass fix, as they have with AlphaEdgeFix = 1.
        bool coverage_world = false;
        if (edge_candidate && m_ConfigAlphaToCoverage)
        {
            float rhw_min = 1e30f;
            for (const TlVertex & vertex : m_BatchVertices)
                rhw_min = std::min( rhw_min, vertex.m_Rhw );
            coverage_world = *reinterpret_cast<const std::int32_t *>(0x0065B928) == 0 && render[rs::ZFUNC] != 8 && rhw_min < 0.999f;
            // Only real cut outs: the alpha of water edges, shadows, smoke and glass is a gradient and the coverage of eight samples
            // would turn it into bands. A fade through the vertex colour is a gradient as well.
            float mid = 0.0f;
            for (const Texture * texture : { m_Bound[0], m_Bound[1] })
                if (texture != nullptr && texture->m_Translucent)
                    mid = std::max( mid, texture->m_AlphaMid );
            const bool vertex_fade = std::any_of( m_BatchVertices.begin(), m_BatchVertices.end(), [] ( const TlVertex & vertex )
            {
                return (vertex.m_Diffuse >> 24) != 0xFF;
            } );
            const bool gradient = mid > m_ConfigCoverageMid || vertex_fade || IsWaterTexture() ||
                (m_Bound[0] != nullptr && std::find( m_ConfigShoreTextures.begin(), m_ConfigShoreTextures.end(), m_Bound[0]->m_Hash ) != m_ConfigShoreTextures.end());
            const bool menu = !coverage_world;
            coverage_world = coverage_world && !gradient;
            // One line per texture, the first 80: the decision and why.
            static std::set<std::uint32_t> coverage_seen;
            const std::uint32_t texture_hash = m_Bound[0] != nullptr ? static_cast<std::uint32_t>(m_Bound[0]->m_Hash) : 0u;
            if (coverage_seen.size() < 80 && coverage_seen.insert( texture_hash ).second)
                Log( "AlphaEdgeFix 3: texture %08X -> %s (%s, alpha 32..224 share %.3f of limit %.2f, vertex alpha %s, water %d, front end %d, zfunc %u, rhw min %g, %u vertices)",
                    texture_hash, coverage_world ? "alpha to coverage" : "two pass", menu ? "menu/HUD" : gradient ? "gradient" : "cut out", mid,
                    m_ConfigCoverageMid, vertex_fade ? "fades" : "opaque", IsWaterTexture() ? 1 : 0,
                    *reinterpret_cast<const std::int32_t *>(0x0065B928) != 0 ? 1 : 0, static_cast<unsigned>(render[rs::ZFUNC]), rhw_min,
                    static_cast<unsigned>(m_BatchVertices.size()) );
        }
        const bool edge_split = edge_candidate && (!m_ConfigAlphaToCoverage || !coverage_world);

        CarSkyNoteBatch(); // B36: the band and the flat colour of the sky, for the car reflections

        m_EdgePass = edge_split ? 1 : (edge_candidate ? 3 : 0);


        ApplyPipeline();

        if (m_ReflCapture)
        {
            m_ReflCapture = false;
            CaptureMirrorBatch();
        }

        // Car reflections draw a car part batch once more from a ring of their own (CarReflBatch).
        const bool car_reflection = m_CarLodMask != 0 && m_ConfigCarRefl > 0;
        UINT base_vertex = 0;
        UINT first_index = 0;
        if (!UploadRing( m_BatchVertices.data(), m_BatchVertices.size(), m_BatchIndices.data(), m_BatchIndices.size(), base_vertex, first_index ))
        {
            m_BatchVertices.clear();
            m_BatchIndices.clear();
            m_BatchPrimitive = Primitive::NONE;
            return;
        }
        const UINT index_count = static_cast<UINT>(m_BatchIndices.size());

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
        m_Context->DrawIndexed( index_count, first_index, static_cast<INT>(base_vertex) );

        if (edge_split)
        {
            m_EdgePass = 2;
            ApplyPipeline();
            m_Context->DrawIndexed( index_count, first_index, static_cast<INT>(base_vertex) );
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
            m_Context->DrawIndexed( index_count, first_index, static_cast<INT>(base_vertex) );
            m_ShorePass = false;

            if (m_ReflWeightRtv && m_ReflWeightBlend)
            {
                ID3D11RenderTargetView * weight = m_ReflWeightRtv.Get();
                m_Context->OMSetRenderTargets( 1, &weight, nullptr );
                const float factor[4] = {};
                m_Context->OMSetBlendState( m_ReflWeightBlend.Get(), factor, 0xFFFFFFFF );
                m_Context->DrawIndexed( index_count, first_index, static_cast<INT>(base_vertex) );
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
            m_Context->DrawIndexed( index_count, first_index, static_cast<INT>(base_vertex) );
            m_BackPass = false;
        }

        m_EdgePass = 0;

        // Car reflections (PLAN.md 9.12b): body parts of R3DCarLOD::Draw once more, with the reflection.
        if (car_reflection)
        {
            m_CarReflOutcome = "left out (no reason noted)";
            m_CarReflGlassRefLast = 0.0f;
            CarReflBatch();
            // B36: what became of the draw under the probe.
            if (m_ProbeHits.size() > probe_first)
            {
                m_ProbeHits[probe_first].m_CarOutcome = m_CarReflOutcome;
                m_ProbeHits[probe_first].m_CarGlassRef = m_CarReflGlassRefLast;
            }
        }

        m_BatchVertices.clear();
        m_BatchIndices.clear();
        m_BatchPrimitive = Primitive::NONE;
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

    // Helpers for reading game memory.

    namespace
    {
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

            RememberPatch( site + 1 );
            relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(hook) - (site + 5));
            std::memcpy( call + 1, &relative, sizeof( relative ) );
            ::VirtualProtect( call, 5, old, &old );
            ::FlushInstructionCache( ::GetCurrentProcess(), call, 5 );
            return true;
        }


        // World draw FUN_00543ae0, called directly from the draw node FUN_0053f620 (doc/LICHT.md 6.6).
        constexpr std::uintptr_t ROAD_DRAW_CALL = 0x0053F70F;
        constexpr std::uintptr_t ROAD_DRAW = 0x00543AE0;

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
            driver.OnTrackChunk( true );
            reinterpret_cast<TrackDrawFn>(TRACK_DRAW)(self, a, b);
            driver.OnTrackChunk( false );
        }

        // Precipitation (PLAN.md 16.0a, measure build p1): R_DrawPrecipitation 0x4564E0, fastcall (ecx = precipitation
        // object, +4 = particles drawn), called in R_WeatherUpdateDraw 0x459020 for rain (0x4591B0) and snow (0x4591B8),
        // with the view at the origin and depth writing off (knowledge.md, row "Niederschlag des Spiels").
        constexpr std::uintptr_t PRECIP_DRAW = 0x004564E0;
        constexpr std::uintptr_t PRECIP_RAIN_CALL = 0x004591B0;
        constexpr std::uintptr_t PRECIP_SNOW_CALL = 0x004591B8;

        using PrecipDrawFn = void( __fastcall * )(void *);

        void __fastcall PrecipRainHook(
            void * precipitation
        )
        {
            Driver & driver = Driver::GetInstance();
            if (!driver.OnPrecipDraw( 1, precipitation, true ))
                reinterpret_cast<PrecipDrawFn>(PRECIP_DRAW)(precipitation);
            driver.OnPrecipDraw( 1, precipitation, false );
        }

        void __fastcall PrecipSnowHook(
            void * precipitation
        )
        {
            Driver & driver = Driver::GetInstance();
            if (!driver.OnPrecipDraw( 2, precipitation, true ))
                reinterpret_cast<PrecipDrawFn>(PRECIP_DRAW)(precipitation);
            driver.OnPrecipDraw( 2, precipitation, false );
        }

        // Car part draw R3DCarLOD::Draw 0x43C830, slot 2 of the table 0x5B36AC, thiscall (model part list, pass mask,
        // in-car flag); called by R3DObject::Draw 0x444E00 once per pass (doc/re/04-FAHRZEUG.md 3.4, PLAN.md 9.12b).
        constexpr std::uintptr_t CAR_LOD_DRAW = 0x0043C830;
        constexpr std::uintptr_t CAR_LOD_DRAW_SLOT = 0x005B36B4;

        // FRONT_END 0x65B928 (src/game/addresses.h): set while the menus run.
        constexpr std::uintptr_t FRONT_END_PTR = 0x0065B928;

        // The 3D rooms of the menus (garage.crp, dealer2a …) are FE3DObjects drawn like a car, every entry of the layer
        // is part 0 variant 0 (BUGS.md 21). Entries at +0xC, count +0x10, 0x54 bytes each, +0x44 -> head, byte 0 part, 1 variant.
        bool IsMenuRoom(
            const void * self
        )
        {
            if (*reinterpret_cast<const std::uint32_t *>(FRONT_END_PTR) == 0 || self == nullptr)
                return false;
            const auto * layer = static_cast<const std::uint8_t *>(self);
            const auto * entries = *reinterpret_cast<const std::uint8_t * const *>(layer + 0xC);
            const auto count = *reinterpret_cast<const std::uint32_t *>(layer + 0x10);
            if (entries == nullptr || count == 0 || count > 1024)
                return false;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const auto * head = *reinterpret_cast<const std::uint8_t * const *>(entries + i * 0x54 + 0x44);
                if (head == nullptr || head[0] != 0 || head[1] != 0)
                    return false;
            }
            return true;
        }

        void __fastcall CarLodDrawHook(
            void * self,
            void *,
            std::uint32_t a,
            std::uint32_t mask,
            std::uint32_t inside
        )
        {
            Driver & driver = Driver::GetInstance();
            const std::uint32_t previous = driver.BeginCarLodDraw( mask & 0xFF, IsMenuRoom( self ), self );
            reinterpret_cast<void( __thiscall * )(void *, std::uint32_t, std::uint32_t, std::uint32_t)>(CAR_LOD_DRAW)(self, a, mask, inside);
            driver.EndCarLodDraw( previous );
        }

        // File_CrpGetEntry 0x59F220, thiscall (list entry of the part, id), called by R3DCarLOD::Draw at 0x43C8F8 once
        // for each part it draws, right before its meshes. List entry +0 is the part's .crp entry 'Atri'; its child 'Base'
        // holds part and variant at +0x44 / +0x45 (as LFSP ReadPartBase, doc/FENSTER-VERDECK.md 8.3c).
        constexpr std::uintptr_t CAR_PART_ENTRY_CALL = 0x0043C8F8;
        constexpr std::uintptr_t CRP_GET_ENTRY = 0x0059F220;
        constexpr std::uintptr_t CRP_CHILD = 0x0059F7C0;
        constexpr std::uintptr_t CRP_BASE = 0x0059F870;
        constexpr std::uint32_t CRP_TAG_BASE = 0x42617365;

        std::uint32_t __fastcall CarPartEntryHook(
            void * entry,
            void *,
            std::uint32_t id
        )
        {
            int part = -1;
            int variant = -1;
            auto * atri = *reinterpret_cast<std::uint8_t **>(entry);
            if (atri != nullptr)
            {
                auto * child = reinterpret_cast<std::uint8_t *( __thiscall * )(void *, std::uint32_t)>(CRP_CHILD)(atri, CRP_TAG_BASE);
                auto * first = reinterpret_cast<std::uint8_t *( __thiscall * )(void *)>(CRP_BASE)(atri);
                std::uint32_t words[3] = {};
                std::memcpy( words, atri, sizeof( words ) );
                const std::uint8_t * end = first + ((words[1] >> 5) << 4) + (words[2] << 4);
                if (child != nullptr && child != end)
                {
                    std::uint32_t flags = 0;
                    std::uint32_t offset = 0;
                    std::memcpy( &flags, child + 4, sizeof( flags ) );
                    std::memcpy( &offset, child + 0xC, sizeof( offset ) );
                    const std::uint8_t * data = (flags & 2) != 0 ? child + offset : reinterpret_cast<const std::uint8_t *>(offset);
                    part = data[0x44];
                    variant = data[0x45];
                }
            }
            Driver::GetInstance().OnCarPart( part, variant );
            return reinterpret_cast<std::uint32_t( __thiscall * )(void *, std::uint32_t)>(CRP_GET_ENTRY)(entry, id);
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

            RememberPatch( slot );
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

    void Driver::InstallCarReflHook()
    {
        if (::GetModuleHandleA( nullptr ) != reinterpret_cast<HMODULE>(0x00400000) ||
            *reinterpret_cast<const std::uintptr_t *>(CAR_LOD_DRAW_SLOT) != CAR_LOD_DRAW)
        {
            Log( "Car reflections: hook not installed, table entry %08X does not match", static_cast<unsigned>(CAR_LOD_DRAW_SLOT) );
            m_ConfigCarRefl = 0;
            return;
        }

        PatchSlot( CAR_LOD_DRAW_SLOT, reinterpret_cast<const void *>(&CarLodDrawHook) );
        Log( "Car reflections: part draw hooked at %08X", static_cast<unsigned>(CAR_LOD_DRAW_SLOT) );

        // Part numbers for 'MatteParts' / 'OnlyPart'; without it every part is reflected.
        if (PatchCall( CAR_PART_ENTRY_CALL, CRP_GET_ENTRY, reinterpret_cast<const void *>(&CarPartEntryHook) ))
            Log( "Car reflections: part entry hooked at %08X", static_cast<unsigned>(CAR_PART_ENTRY_CALL) );
        else
            Log( "Car reflections: part entry not hooked, call at %08X does not match", static_cast<unsigned>(CAR_PART_ENTRY_CALL) );
    }

    void Driver::InstallPrecipHooks()
    {
        if (::GetModuleHandleA( nullptr ) != reinterpret_cast<HMODULE>(0x00400000))
            return;
        const bool rain = PatchCall( PRECIP_RAIN_CALL, PRECIP_DRAW, reinterpret_cast<const void *>(&PrecipRainHook) );
        const bool snow = PatchCall( PRECIP_SNOW_CALL, PRECIP_DRAW, reinterpret_cast<const void *>(&PrecipSnowHook) );
        Log( "Precipitation (dx11z p2): rain draw %s at %08X, snow draw %s at %08X", rain ? "marked" : "not marked (call does not match)",
            static_cast<unsigned>(PRECIP_RAIN_CALL), snow ? "marked" : "not marked (call does not match)", static_cast<unsigned>(PRECIP_SNOW_CALL) );
    }

    bool Driver::OnPrecipDraw(
        int kind,
        const void * precipitation,
        bool begin
    )
    {
        // Own batches inside the mark: what is pending belongs to the draws before it.
        Flush();
        if (!begin)
        {
            m_PrecipMark = 0;
            const ULONGLONG now = ::GetTickCount64();
            if (now >= m_PrecipNextReport && (m_PrecipStats[0].m_Marks + m_PrecipStats[1].m_Marks) > 0)
            {
                m_PrecipNextReport = now + 10000;
                static const char * const names[2] = { "rain", "snow" };
                for (int k = 0; k < 2; ++k)
                {
                    PrecipStats & s = m_PrecipStats[k];
                    if (s.m_MarksWithParticles == 0)
                        continue;
                    char hashes[64] = {};
                    for (int h = 0; h < s.m_HashCount; ++h)
                        std::snprintf( hashes + std::strlen( hashes ), sizeof( hashes ) - std::strlen( hashes ), "%s%08X", h ? "," : "",
                            static_cast<unsigned>(s.m_Hashes[h]) );
                    Log( "Precipitation %s: own draws %llu (%llu particles each on average); "
                        "marks %llu (with particles %llu, most %d), game draws inside %llu, vertices %llu, indices %llu, "
                        "textures %s, blend %u (%u/%u), z enable %u write %u func %u, cull %u, fog %u, queue list %d, primitive %d, "
                        "diffuse %08X, box x %.0f..%.0f y %.0f..%.0f, rhw %.4f..%.4f, z %.4f..%.4f",
                        names[k], static_cast<unsigned long long>(m_PrecipOwn[k]),
                        static_cast<unsigned long long>(m_PrecipOwn[k] ? m_PrecipOwnInstances[k] / m_PrecipOwn[k] : 0),
                        static_cast<unsigned long long>(s.m_Marks), static_cast<unsigned long long>(s.m_MarksWithParticles),
                        s.m_ParticlesMax, static_cast<unsigned long long>(s.m_Draws), static_cast<unsigned long long>(s.m_Vertices),
                        static_cast<unsigned long long>(s.m_Indices), s.m_HashCount ? hashes : "-", s.m_Blend, s.m_Src, s.m_Dst,
                        s.m_ZEnable, s.m_ZWrite, s.m_ZFunc, s.m_Cull, s.m_Fog, s.m_QueueList, s.m_Primitive,
                        static_cast<unsigned>(s.m_Diffuse), s.m_Box[0], s.m_Box[2], s.m_Box[1], s.m_Box[3], s.m_Rhw[0], s.m_Rhw[1],
                        s.m_Z[0], s.m_Z[1] );
                }
                m_PrecipStats[0] = PrecipStats();
                m_PrecipStats[1] = PrecipStats();
                m_PrecipOwn[0] = m_PrecipOwn[1] = 0;
                m_PrecipOwnInstances[0] = m_PrecipOwnInstances[1] = 0;
            }
            return false;
        }
        m_PrecipMark = kind;
        PrecipStats & s = m_PrecipStats[kind - 1];
        ++s.m_Marks;
        int particles = 0;
        if (SafeCopy( static_cast<const std::uint8_t *>(precipitation) + 4, &particles, 4 ) && particles > 0)
        {
            ++s.m_MarksWithParticles;
            s.m_ParticlesMax = std::max( s.m_ParticlesMax, particles );
        }
        if (m_ConfigPrecip > 0 && PrecipDrawOwn( kind, precipitation ))
        {
            m_PrecipMark = 0;
            return true;
        }
        return false;
    }

    // Own rain and snow (16.4 / 16.5). The view of the game is at the origin here (R_WeatherUpdateDraw); the precipitation
    // object keeps the camera of this frame: rows right, up, forward at + 0x85CC, position at + 0x85F0 (R_UpdatePrecipitation).
    // Strength at + 0 (rain or snow 0..1 of the sky track, after LFSP's menu and cover). false: the game draws its own.
    bool Driver::PrecipDrawOwn(
        int kind,
        const void * precipitation
    )
    {
        if (!m_PrecipVs || !m_PrecipPs || !m_FrameView)
            return false;
        const auto * object = static_cast<const std::uint8_t *>(precipitation);
        float strength = 0.0f;
        float camera[12] = {};
        if (!SafeCopy( object, &strength, 4 ) || !SafeCopy( object + 0x85CC, camera, sizeof( camera ) ))
            return false;
        for (int r = 0; r < 3; ++r)
        {
            const float length = camera[r * 3] * camera[r * 3] + camera[r * 3 + 1] * camera[r * 3 + 1] + camera[r * 3 + 2] * camera[r * 3 + 2];
            if (!(length > 0.9f && length < 1.1f))
                return false;
        }
        float clip_of_view[16] = {}, view_of_clip[16] = {}, viewport[4] = {};
        if (!ReadCamera( clip_of_view, view_of_clip, viewport ))
            return false;
        strength = std::clamp( strength, 0.0f, 1.0f );
        const float count_max = kind == 1 ? m_ConfigPrecipRain : m_ConfigPrecipSnow;
        const UINT instances = static_cast<UINT>(strength * count_max);
        if (instances == 0)
            return true; // nothing falls: the game's few particles stay away too

        // Camera velocity once per frame from its place between game steps (64 Hz).
        if (m_PrecipCamFlip != m_FlipCount)
        {
            m_PrecipCamFlip = m_FlipCount;
            int step = 0;
            SafeCopy( reinterpret_cast<const void *>(0x00606AC4), &step, 4 );
            const float * position = camera + 9;
            if (m_PrecipCamHave && step != m_PrecipCamStep)
            {
                const int steps = step - m_PrecipCamStep;
                const float dt = static_cast<float>(steps) / 64.0f;
                float velocity[3] = {};
                float speed2 = 0.0f;
                for (int j = 0; j < 3; ++j)
                {
                    velocity[j] = (position[j] - m_PrecipCamPos[j]) / std::max( dt, 1e-3f );
                    speed2 += velocity[j] * velocity[j];
                }
                const bool jump = steps < 0 || steps > 64 || speed2 > 120.0f * 120.0f;
                for (int j = 0; j < 3; ++j)
                    m_PrecipCamVel[j] = jump ? 0.0f : m_PrecipCamVel[j] + (velocity[j] - m_PrecipCamVel[j]) * std::min( 1.0f, dt * 6.0f );
            }
            std::memcpy( m_PrecipCamPos, position, sizeof( m_PrecipCamPos ) );
            m_PrecipCamStep = step;
            m_PrecipCamHave = true;
        }

        if (!m_PrecipConstants)
        {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = 256;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            m_Device->CreateBuffer( &desc, nullptr, m_PrecipConstants.Put() );
        }
        if (!m_PrecipBlend)
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
            m_Device->CreateBlendState( &desc, m_PrecipBlend.Put() );
        }
        if (!m_PrecipDepth)
        {
            D3D11_DEPTH_STENCIL_DESC desc = {};
            desc.DepthEnable = TRUE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            m_Device->CreateDepthStencilState( &desc, m_PrecipDepth.Put() );
        }
        if (!m_PrecipRaster)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.DepthClipEnable = TRUE;
            desc.ScissorEnable = TRUE;
            desc.MultisampleEnable = TRUE;
            m_Device->CreateRasterizerState( &desc, m_PrecipRaster.Put() );
        }
        if (!m_PrecipConstants || !m_PrecipBlend || !m_PrecipDepth || !m_PrecipRaster)
            return false;

        // Wind: a light breeze, in the storm '[Weather] StormDropWind' (LfspWeatherState, else its default 1.8) x 3 m/s.
        const bool storm = m_WeatherFxOn && (m_WeatherFx[15] > 3.5f);
        const float storm_wind = m_WeatherStateOn && m_WeatherState[5] > 0.0f ? m_WeatherState[5] : 1.8f;
        const float wind = 0.8f + (storm ? storm_wind * 3.0f : 0.0f);
        const bool rain = kind == 1;
        const float fall = rain ? 9.0f : 1.1f;
        const float light = m_CarWeatherLight * m_ConfigPrecipBright;
        const float flash = m_WeatherFxOn ? m_WeatherFx[14] : 0.0f;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_PrecipConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            return false;
        float c[64] = {};
        c[2] = 1.0f / static_cast<float>(std::max( m_Width, 1 ));
        c[3] = 1.0f / static_cast<float>(std::max( m_Height, 1 ));
        std::memcpy( c + 4, viewport, sizeof( viewport ) );
        std::memcpy( c + 8, clip_of_view, sizeof( clip_of_view ) );
        std::memcpy( c + 24, camera + 9, sizeof( float ) * 3 );
        c[27] = rain ? 22.0f : 26.0f; // box
        std::memcpy( c + 28, camera, sizeof( float ) * 3 );
        c[31] = static_cast<float>(kind);
        std::memcpy( c + 32, camera + 3, sizeof( float ) * 3 );
        c[35] = strength;
        std::memcpy( c + 36, camera + 6, sizeof( float ) * 3 );
        c[39] = static_cast<float>(static_cast<double>(::GetTickCount64() % 3600000ull) / 1000.0);
        c[40] = wind * 0.94f * (rain ? 1.0f : 0.8f);
        c[41] = -fall;
        c[42] = wind * 0.34f * (rain ? 1.0f : 0.8f);
        c[43] = rain ? 0.035f : 0.02f; // exposure
        std::memcpy( c + 44, m_PrecipCamVel, sizeof( m_PrecipCamVel ) );
        c[47] = rain ? 0.0035f : 0.018f; // width
        c[48] = (rain ? 0.72f : 0.92f) * light;
        c[49] = (rain ? 0.75f : 0.94f) * light;
        c[50] = (rain ? 0.8f : 1.0f) * light;
        c[51] = std::min( (rain ? 0.28f : 0.75f) * m_ConfigPrecipBright, 1.0f );
        c[52] = flash;
        c[53] = rain ? 1.0f : 1.5f; // least width in pixels
        c[54] = rain ? 2.5f : 0.6f; // longest streak
        std::memcpy( mapped.pData, c, sizeof( c ) );
        m_Context->Unmap( m_PrecipConstants.Get(), 0 );

        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_PrecipVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_PrecipPs.Get(), nullptr, 0 );
        ID3D11Buffer * constant_buffer = m_PrecipConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &constant_buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &constant_buffer );
        const float factor[4] = {};
        m_Context->OMSetBlendState( m_PrecipBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_PrecipDepth.Get(), 0 );
        m_Context->RSSetState( m_PrecipRaster.Get() );
        m_Context->DrawInstanced( 6, instances, 0, 0 );
        ++m_PrecipOwn[kind - 1];
        m_PrecipOwnInstances[kind - 1] += instances;

        // The game's pipeline back (as CarReflDrawPending).
        m_Context->IASetInputLayout( m_SceneLayout.Get() );
        UINT stride = sizeof( TlVertex );
        UINT offset = 0;
        ID3D11Buffer * vertex_buffer = m_VertexBuffer.Get();
        m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
        m_Context->IASetIndexBuffer( m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
        InvalidatePipeline();
        return true;
    }

    void Driver::PrecipMeasureBatch()
    {
        PrecipStats & s = m_PrecipStats[m_PrecipMark - 1];
        const auto & render = m_D3D.m_Render;
        ++s.m_Draws;
        s.m_Vertices += m_BatchVertices.size();
        s.m_Indices += m_BatchIndices.size();
        const std::uint32_t hash = m_Bound[0] != nullptr ? static_cast<std::uint32_t>(m_Bound[0]->m_Hash) : 0u;
        bool known = false;
        for (int h = 0; h < s.m_HashCount; ++h)
            known = known || s.m_Hashes[h] == hash;
        if (!known && s.m_HashCount < 4)
            s.m_Hashes[s.m_HashCount++] = hash;
        s.m_Blend = render[rs::ALPHABLENDENABLE];
        s.m_Src = render[rs::SRCBLEND];
        s.m_Dst = render[rs::DESTBLEND];
        s.m_ZEnable = render[rs::ZENABLE];
        s.m_ZWrite = render[rs::ZWRITEENABLE];
        s.m_ZFunc = render[rs::ZFUNC];
        s.m_Cull = render[rs::CULLMODE];
        s.m_Fog = render[rs::FOGENABLE];
        s.m_QueueList = m_QueueList;
        s.m_Primitive = static_cast<int>(m_BatchPrimitive);
        for (const TlVertex & v : m_BatchVertices)
        {
            s.m_Box[0] = std::min( s.m_Box[0], v.m_X );
            s.m_Box[1] = std::min( s.m_Box[1], v.m_Y );
            s.m_Box[2] = std::max( s.m_Box[2], v.m_X );
            s.m_Box[3] = std::max( s.m_Box[3], v.m_Y );
            s.m_Rhw[0] = std::min( s.m_Rhw[0], v.m_Rhw );
            s.m_Rhw[1] = std::max( s.m_Rhw[1], v.m_Rhw );
            s.m_Z[0] = std::min( s.m_Z[0], v.m_Z );
            s.m_Z[1] = std::max( s.m_Z[1], v.m_Z );
        }
        if (!m_BatchVertices.empty())
            s.m_Diffuse = m_BatchVertices[0].m_Diffuse;
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

        // By day: the track chunk draw, '[Dx11] HeadlightsDay'; also for snow white (LFSP '[Weather] SnowWhite'), which needs to
        // know the track draws.
        if (m_ConfigHeadlightsDay > 0 || m_ConfigSnowWhite)
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


    std::uint32_t Driver::BeginCarLodDraw(
        std::uint32_t mask,
        bool room,
        const void * object
    )
    {
        const std::uint32_t previous = m_CarLodMask;
        m_CarLodObject = object;

        if (m_ConfigCarRefl <= 0)
            return previous;

        // The draws so far are not of this pass; batches end at the edges of the call.
        Flush();
        CarReflDrawPending();
        m_CarReflGlassCount = 0;

        // A menu room is no car: no reflection on its walls, and the frame copy waits for the car so it shows the room.
        if (room)
        {
            if (!m_CarReflRoomLogged)
            {
                m_CarReflRoomLogged = true;
                Log( "Car reflections: menu room left out (pass %02X)", mask );
            }
            m_CarLodMask = 0;
            return previous;
        }

        ++m_CarReflLodCalls;
        m_CarLodMask = mask != 0 ? mask : 0x100;
        m_CarPartMatte = false;
        m_CarPartBody = false;
        m_CarPart = -1;

        // The camera of this view (the rear view mirror has its own), read like the water reflections do.
        float clip_of_view[16] = {};
        float view_of_clip[16] = {};
        float viewport[4] = {};
        m_CarReflCamera = ReadCamera( clip_of_view, view_of_clip, viewport );
        if (m_CarReflCamera)
        {
            std::memcpy( m_CarReflClipOfView.data(), clip_of_view, sizeof( clip_of_view ) );
            std::memcpy( m_CarReflViewOfClip.data(), view_of_clip, sizeof( view_of_clip ) );
            std::memcpy( m_CarReflViewport.data(), viewport, sizeof( viewport ) );
        }

        // B36: the sky by direction: the axes of this view, and the panorama when the sky has changed.
        if (m_ConfigCarReflSky != 0 && m_ConfigCarReflSkyPanorama != 0)
        {
            float position[3] = {};
            m_CarSkyRowsValid = ReadWorldCamera( m_CarSkyRows, position );
            CarSkyBuildPanorama();
        }

        // What is reflected: the frame as it is before the first car part of the frame, the world without the cars.
        if (m_CarReflCopyFlip != m_FlipCount)
        {
            m_CarReflCopyFlip = m_FlipCount;
            // Every N frames only: the paint is no perfect mirror (Blur), the copy of the frame before looks nearly the same.
            if (m_ConfigCarReflInterval <= 1 || !m_CarReflSourceValid || m_FlipCount % static_cast<std::uint64_t>(m_ConfigCarReflInterval) == 0)
            {
                m_CarReflSourceValid = CopyCarReflSource();
                // The shared blur chain from the full size frame (the resolve of a half size copy, else level 0 of the copy).
                if (m_ConfigCarReflBlurChain > 0)
                    m_BlurChainValid = m_CarReflSourceValid &&
                        BuildBlurChain( m_CarReflLevelOffset > 0.0f ? m_CarReflResolveView.Get() : m_CarReflSourceView.Get() );
            }
            ++m_CarReflFrames;
        }

        return previous;
    }


    void Driver::EndCarLodDraw(
        std::uint32_t previous
    )
    {
        if (m_ConfigCarRefl <= 0)
            return;

        Flush();
        // The reflections of the call, collected per state (9.13c): after all its parts, before anything drawn later.
        CarReflDrawPending();
        m_CarLodMask = previous;
        m_CarPartMatte = false;
        m_CarPartBody = false;
        m_CarPart = -1;
    }


    void Driver::OnCarPart(
        int part,
        int variant
    )
    {
        if (m_ConfigCarRefl <= 0 || m_CarLodMask == 0)
            return;

        bool matte = false;
        if (part >= 0)
            for (const std::uint32_t entry : m_ConfigCarReflMatte)
                matte = matte || ((entry >> 8) == static_cast<std::uint32_t>(part) &&
                    ((entry & 0xFF) == 0xFF || (entry & 0xFF) == static_cast<std::uint32_t>(variant)));

        // Each part of a pass once, to find the ones that should be matte.
        const std::uint32_t key = (m_CarLodMask & 0xFFF) << 16 | static_cast<std::uint32_t>(part & 0xFF) << 8 | static_cast<std::uint32_t>(variant & 0xFF);
        if (m_CarReflPartsSeen.insert( key ).second && m_CarReflPartsSeen.size() <= 400)
            Log( "Car reflections: pass %02X part %d/%d%s", m_CarLodMask, part, variant, matte ? " matte" : "" );

        // The batch so far belongs to the previous part; the body gets batches of its own to learn the paint textures,
        // each matte part too, for the log.
        const bool body = part == 1;
        const int id = part << 8 | (variant & 0xFF);
        // B34: glass and 'SkipParts' as well: in a batch with a neighbour they would count as whichever part came last.
        const auto listed = [part]( const std::vector<int> & list ) { return std::find( list.begin(), list.end(), part ) != list.end(); };
        const bool own = listed( m_ConfigCarReflGlassParts ) || listed( m_ConfigCarReflSkipParts );
        if (matte != m_CarPartMatte || body != m_CarPartBody || own != m_CarPartOwn || ((matte || own) && id != m_CarPart))
            Flush();
        m_CarPartMatte = matte;
        m_CarPartBody = body;
        m_CarPartOwn = own;
        m_CarPart = id;
    }


    bool Driver::CopyCarReflSource()
    {
        if (!m_Frame)
            return false;

        D3D11_TEXTURE2D_DESC desc = {};
        m_Frame->GetDesc( &desc );

        // '[CarReflections] SourceScale' 0.5 (9.13d): resolved at full size without levels, drawn into a half size copy.
        const bool half = m_ConfigCarReflSourceScale < 1.0f && m_CarReflDownPs && m_ReflCompositeVs;
        const UINT width = half ? std::max( 1u, (desc.Width + 1) / 2 ) : desc.Width;
        const UINT height = half ? std::max( 1u, (desc.Height + 1) / 2 ) : desc.Height;
        m_CarReflLevelOffset = half ? 1.0f : 0.0f;

        D3D11_TEXTURE2D_DESC have = {};
        if (m_CarReflSource)
            m_CarReflSource->GetDesc( &have );

        if (!m_CarReflSource || have.Width != width || have.Height != height || have.Format != desc.Format)
        {
            m_CarReflSourceView.Reset();
            m_CarReflSourceRtv.Reset();
            m_CarReflSource.Reset();

            // All mip levels: the paint is no perfect mirror, '[CarReflections] Blur' picks the level.
            D3D11_TEXTURE2D_DESC copy = desc;
            copy.Width = width;
            copy.Height = height;
            copy.MipLevels = 0;
            copy.ArraySize = 1;
            copy.SampleDesc.Count = 1;
            copy.SampleDesc.Quality = 0;
            copy.Usage = D3D11_USAGE_DEFAULT;
            copy.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            copy.CPUAccessFlags = 0;
            copy.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            if (FAILED( m_Device->CreateTexture2D( &copy, nullptr, m_CarReflSource.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_CarReflSource.Get(), nullptr, m_CarReflSourceView.Put() ) ))
            {
                m_CarReflSource.Reset();
                m_CarReflSourceView.Reset();
                if (m_CarReflLines < 300)
                {
                    ++m_CarReflLines;
                    Log( "Car reflections: frame copy %ux%u format %u failed", desc.Width, desc.Height, static_cast<unsigned>(desc.Format) );
                }
                return false;
            }
            m_CarReflSource->GetDesc( &copy );
            Log( "Car reflections: frame copy %ux%u with %u levels created", copy.Width, copy.Height, copy.MipLevels );
        }

        if (!half)
        {
            if (desc.SampleDesc.Count > 1)
                m_Context->ResolveSubresource( m_CarReflSource.Get(), 0, m_Frame.Get(), 0, desc.Format );
            else
                m_Context->CopySubresourceRegion( m_CarReflSource.Get(), 0, 0, 0, 0, m_Frame.Get(), 0, nullptr );
            m_Context->GenerateMips( m_CarReflSourceView.Get() );
            return true;
        }

        // Half size: the frame resolved (or copied) at full size, one level only.
        D3D11_TEXTURE2D_DESC resolved = {};
        if (m_CarReflResolve)
            m_CarReflResolve->GetDesc( &resolved );
        if (!m_CarReflResolve || resolved.Width != desc.Width || resolved.Height != desc.Height || resolved.Format != desc.Format)
        {
            m_CarReflResolveView.Reset();
            m_CarReflResolve.Reset();
            resolved = desc;
            resolved.MipLevels = 1;
            resolved.ArraySize = 1;
            resolved.SampleDesc.Count = 1;
            resolved.SampleDesc.Quality = 0;
            resolved.Usage = D3D11_USAGE_DEFAULT;
            resolved.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            resolved.CPUAccessFlags = 0;
            resolved.MiscFlags = 0;
            if (FAILED( m_Device->CreateTexture2D( &resolved, nullptr, m_CarReflResolve.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_CarReflResolve.Get(), nullptr, m_CarReflResolveView.Put() ) ))
            {
                m_CarReflResolve.Reset();
                m_CarReflResolveView.Reset();
                return false;
            }
            Log( "Car reflections: resolve %ux%u for the half size copy created", desc.Width, desc.Height );
        }
        if (!m_CarReflSourceRtv && FAILED( m_Device->CreateRenderTargetView( m_CarReflSource.Get(), nullptr, m_CarReflSourceRtv.Put() ) ))
            return false;

        if (desc.SampleDesc.Count > 1)
            m_Context->ResolveSubresource( m_CarReflResolve.Get(), 0, m_Frame.Get(), 0, desc.Format );
        else
            m_Context->CopyResource( m_CarReflResolve.Get(), m_Frame.Get() );

        // Into level 0 of the copy: a fullscreen triangle, every 2 x 2 block of the frame one pixel (CAR_DOWN).
        auto & raster = m_ReflRasters[0x300];
        if (!raster)
        {
            D3D11_RASTERIZER_DESC raster_desc = {};
            raster_desc.FillMode = D3D11_FILL_SOLID;
            raster_desc.CullMode = D3D11_CULL_NONE;
            raster_desc.DepthClipEnable = FALSE;
            m_Device->CreateRasterizerState( &raster_desc, raster.Put() );
        }
        ID3D11RenderTargetView * target = m_CarReflSourceRtv.Get();
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        m_Context->OMSetBlendState( nullptr, nullptr, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( nullptr, 0 );
        m_Context->RSSetState( raster.Get() );
        const D3D11_VIEWPORT view = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
        m_Context->RSSetViewports( 1, &view );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_ReflCompositeVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_CarReflDownPs.Get(), nullptr, 0 );
        ID3D11ShaderResourceView * source = m_CarReflResolveView.Get();
        m_Context->PSSetShaderResources( 0, 1, &source );
        ID3D11SamplerState * sampler = m_PresentSampler.Get();
        m_Context->PSSetSamplers( 0, 1, &sampler );
        m_Context->Draw( 3, 0 );
        ID3D11ShaderResourceView * none = nullptr;
        m_Context->PSSetShaderResources( 0, 1, &none );
        // The frame target comes back with the next draw (ApplyPipeline binds target, viewport and shaders each time).
        ID3D11RenderTargetView * no_target = nullptr;
        m_Context->OMSetRenderTargets( 1, &no_target, nullptr );
        m_Context->GenerateMips( m_CarReflSourceView.Get() );
        InvalidatePipeline();
        return true;
    }


    bool Driver::BuildBlurChain(
        ID3D11ShaderResourceView * source
    )
    {
        if (source == nullptr || !m_Frame || !m_BlurDownPs || !m_BlurUpPs || !m_ReflCompositeVs)
            return false;

        D3D11_TEXTURE2D_DESC frame = {};
        m_Frame->GetDesc( &frame );
        const UINT width = std::max( 1u, frame.Width / 2 );
        const UINT height = std::max( 1u, frame.Height / 2 );

        D3D11_TEXTURE2D_DESC have = {};
        if (m_BlurDown)
            m_BlurDown->GetDesc( &have );
        if (!m_BlurDown || !m_BlurChain || have.Width != width || have.Height != height || have.Format != frame.Format)
        {
            m_BlurChainView.Reset();
            for (int k = 0; k < BLUR_DOWN_LEVELS; ++k)
            {
                m_BlurDownRtv[k].Reset();
                m_BlurDownSrv[k].Reset();
                m_BlurChainRtv[k].Reset();
            }
            m_BlurDown.Reset();
            m_BlurChain.Reset();

            // As many levels as stay at least 2 pixels on the short side, at most BLUR_DOWN_LEVELS.
            UINT levels = 1;
            while (levels < BLUR_DOWN_LEVELS && std::min( width, height ) >> levels >= 2)
                ++levels;
            if (levels < 2)
                return false;

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = levels;
            desc.ArraySize = 1;
            desc.Format = frame.Format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            bool made = SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_BlurDown.Put() ) );
            desc.MipLevels = levels - 1;
            made = made && SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_BlurChain.Put() ) );
            for (UINT k = 0; made && k < levels; ++k)
            {
                D3D11_RENDER_TARGET_VIEW_DESC target = {};
                target.Format = frame.Format;
                target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                target.Texture2D.MipSlice = k;
                D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
                view.Format = frame.Format;
                view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MostDetailedMip = k;
                view.Texture2D.MipLevels = 1;
                made = SUCCEEDED( m_Device->CreateRenderTargetView( m_BlurDown.Get(), &target, m_BlurDownRtv[k].Put() ) ) &&
                    SUCCEEDED( m_Device->CreateShaderResourceView( m_BlurDown.Get(), &view, m_BlurDownSrv[k].Put() ) );
                if (made && k + 1 < levels)
                    made = SUCCEEDED( m_Device->CreateRenderTargetView( m_BlurChain.Get(), &target, m_BlurChainRtv[k].Put() ) );
            }
            made = made && SUCCEEDED( m_Device->CreateShaderResourceView( m_BlurChain.Get(), nullptr, m_BlurChainView.Put() ) );
            Log( "Blur chain: %ux%u, %u levels down, %u levels in the chain %s", width, height, levels, levels - 1, made ? "created" : "FAILED" );
            if (!made)
            {
                m_BlurChainView.Reset();
                m_BlurDown.Reset();
                m_BlurChain.Reset();
                m_BlurDownLevels = 0;
                return false;
            }
            m_BlurDownLevels = levels;
        }

        auto & raster = m_ReflRasters[0x300];
        if (!raster)
        {
            D3D11_RASTERIZER_DESC raster_desc = {};
            raster_desc.FillMode = D3D11_FILL_SOLID;
            raster_desc.CullMode = D3D11_CULL_NONE;
            raster_desc.DepthClipEnable = FALSE;
            m_Device->CreateRasterizerState( &raster_desc, raster.Put() );
        }
        m_Context->OMSetBlendState( nullptr, nullptr, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( nullptr, 0 );
        m_Context->RSSetState( raster.Get() );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_ReflCompositeVs.Get(), nullptr, 0 );
        ID3D11SamplerState * sampler = m_PresentSampler.Get();
        m_Context->PSSetSamplers( 0, 1, &sampler );

        ID3D11ShaderResourceView * none = nullptr;
        const auto pass = [&]( ID3D11RenderTargetView * target, ID3D11ShaderResourceView * input, UINT level )
        {
            // The input of the last pass may be a level of this texture: unbound first, then the target, then the input.
            m_Context->PSSetShaderResources( 0, 1, &none );
            m_Context->OMSetRenderTargets( 1, &target, nullptr );
            const D3D11_VIEWPORT view = { 0, 0, static_cast<float>(std::max( 1u, width >> level )), static_cast<float>(std::max( 1u, height >> level )), 0, 1 };
            m_Context->RSSetViewports( 1, &view );
            m_Context->PSSetShaderResources( 0, 1, &input );
            m_Context->Draw( 3, 0 );
        };

        // Down: level 0 from the full size frame, each further level from the one before.
        m_Context->PSSetShader( m_BlurDownPs.Get(), nullptr, 0 );
        for (UINT k = 0; k < m_BlurDownLevels; ++k)
            pass( m_BlurDownRtv[k].Get(), k == 0 ? source : m_BlurDownSrv[k - 1].Get(), k );
        // Up: chain level k is down level k + 1 drawn with the tent at the size of level k.
        m_Context->PSSetShader( m_BlurUpPs.Get(), nullptr, 0 );
        for (UINT k = 0; k + 1 < m_BlurDownLevels; ++k)
            pass( m_BlurChainRtv[k].Get(), m_BlurDownSrv[k + 1].Get(), k );

        m_Context->PSSetShaderResources( 0, 1, &none );
        ID3D11RenderTargetView * no_target = nullptr;
        m_Context->OMSetRenderTargets( 1, &no_target, nullptr );
        InvalidatePipeline();
        return true;
    }


    void Driver::CarReflBatch()
    {
        const std::uint32_t mask = m_CarLodMask;
        const auto & render = m_D3D.m_Render;

        if (m_BatchPrimitive != Primitive::TRIANGLES || render[rs::ZENABLE] == 0 || m_BatchIndices.size() < 3)
        {
            m_CarReflOutcome = "no triangles or no depth test";
            return;
        }

        const Texture * texture = m_Bound[0];
        const std::uint32_t hash = texture != nullptr ? texture->m_Hash : 0;
        const bool blended = render[rs::ALPHABLENDENABLE] != 0;
        const std::uint32_t source = render[rs::SRCBLEND];
        const std::uint32_t destination = render[rs::DESTBLEND];
        CarReflStats & stats = m_CarReflStats[mask];

        // Each texture of a pass once, to tell paint from tyres, lights and glass.
        if (m_CarReflSeen.emplace( (static_cast<std::uint64_t>(mask) << 32) | hash, 1 ).second && m_CarReflLines < 300)
        {
            ++m_CarReflLines;
            Log( "Car reflections: pass %02X texture %08X %dx%d%s, blend %u (%u/%u), alpha test %u ref %u, z write %u, cull %u, %zu vertices "
                "%zu triangles", mask, hash, texture != nullptr ? texture->m_Width : 0, texture != nullptr ? texture->m_Height : 0,
                texture != nullptr && texture->m_Translucent ? " translucent" : "", blended ? 1u : 0u, source, destination,
                render[rs::ALPHATESTENABLE] != 0 ? 1u : 0u, render[rs::ALPHAREF] & 0xFF, render[rs::ZWRITEENABLE] != 0 ? 1u : 0u,
                render[rs::CULLMODE], m_BatchVertices.size(), m_BatchIndices.size() / 3 );
        }

        // Body passes only (0x50, in the car 0x10 and 0x40), no wheels (0x20) and no interior (0x30).
        if ((mask & 0x20) != 0 || (mask & 0x50) == 0)
        {
            m_CarReflOutcome = "pass is no body pass";
            return;
        }

        if (std::find( m_ConfigCarReflSkip.begin(), m_ConfigCarReflSkip.end(), hash ) != m_ConfigCarReflSkip.end())
        {
            ++stats.m_Skipped;
            m_CarReflOutcome = "SkipTextures";
            return;
        }
        // B34: door handle and lamps ('SkipParts'): small grey details, the paint reflection made them a bright ring.
        if (m_CarPart >= 0 && std::find( m_ConfigCarReflSkipParts.begin(), m_ConfigCarReflSkipParts.end(), m_CarPart >> 8 ) != m_ConfigCarReflSkipParts.end())
        {
            ++stats.m_Skipped;
            m_CarReflOutcome = "SkipParts";
            return;
        }
        // B34: glass parts reflect as a whole as glass (own strength, no paint look); 'GlassStrength' 0 = none at all.
        const bool glass_part = m_CarPart >= 0 &&
            std::find( m_ConfigCarReflGlassParts.begin(), m_ConfigCarReflGlassParts.end(), m_CarPart >> 8 ) != m_ConfigCarReflGlassParts.end();
        if (glass_part)
        {
            if (m_CarReflGlassLogged.insert( static_cast<int>(reinterpret_cast<std::uintptr_t>(m_CarLodObject) * 31u) ^ m_CarPart ).second &&
                m_CarReflGlassLogged.size() <= 128 && m_CarReflLines < 400)
            {
                ++m_CarReflLines;
                Log( "Car reflections: car %p glass part %d/%d texture %08X reflected as glass (GlassParts, B34)", m_CarLodObject,
                    m_CarPart >> 8, m_CarPart & 0xFF, hash );
            }
            if (m_ConfigCarReflGlassStrength <= 0.0f)
            {
                ++stats.m_Skipped;
                m_CarReflOutcome = "glass part, GlassStrength 0";
                return;
            }
        }
        // B34: the windows of most cars are the glass texture on a body part: they reflect as glass, only their see-through texels.
        const bool glass_page = !glass_part && blended && texture != nullptr && texture->m_GlassPage && m_ConfigCarReflGlassStrength > 0.0f;
        if (glass_page && m_CarReflAlphaSkipped.insert( hash ).second && m_CarReflAlphaSkipped.size() <= 64 && m_CarReflLines < 300)
        {
            ++m_CarReflLines;
            Log( "Car reflections: texture %08X %dx%d part %d/%d is a glass texture, reflected as glass (B34)", hash, texture->m_Width,
                texture->m_Height, m_CarPart >> 8, m_CarPart & 0xFF );
        }
        // B34: the game draws every window twice with the same corners (the glass, then a second layer). One reflection only.
        if ((glass_part || glass_page) && !m_BatchVertices.empty())
        {
            const TlVertex & first = m_BatchVertices[0];
            if (m_CarReflGlassCount == m_BatchVertices.size() && m_CarReflGlassFirst[0] == first.m_X &&
                m_CarReflGlassFirst[1] == first.m_Y && m_CarReflGlassFirst[2] == first.m_Z)
            {
                ++stats.m_Skipped;
                m_CarReflOutcome = "second layer of a window";
                return;
            }
            m_CarReflGlassCount = m_BatchVertices.size();
            m_CarReflGlassFirst[0] = first.m_X;
            m_CarReflGlassFirst[1] = first.m_Y;
            m_CarReflGlassFirst[2] = first.m_Z;
        }
        // B34: grilles and meshes, mostly half transparent texels.
        // Not on glass parts: glass is half transparent all over and reflects as glass (B34, the GT1 side window stayed bare).
        if (!glass_part && !glass_page && m_ConfigCarReflSkipAlphaMid > 0.0f && texture != nullptr && texture->m_Translucent &&
            texture->m_AlphaMid >= m_ConfigCarReflSkipAlphaMid)
        {
            ++stats.m_Skipped;
            if (m_CarReflAlphaSkipped.insert( hash ).second && m_CarReflAlphaSkipped.size() <= 64 && m_CarReflLines < 300)
            {
                ++m_CarReflLines;
                Log( "Car reflections: texture %08X %dx%d part %d/%d left out, %.0f%% half transparent (SkipAlphaMid %.2f, B34)", hash,
                    texture->m_Width, texture->m_Height, m_CarPart >> 8, m_CarPart & 0xFF, texture->m_AlphaMid * 100.0f, m_ConfigCarReflSkipAlphaMid );
            }
            m_CarReflOutcome = "SkipAlphaMid (grille)";
            return;
        }

        // Cell of a texture coordinate, in 64 x 64 cells of the texture.
        const auto cell = []( float t ) { return std::clamp( static_cast<int>(std::floor( t * 64.0f )), 0, 63 ); };
        const std::size_t count = m_BatchVertices.size();
        const auto corners = [&]( std::size_t i ) -> std::array<std::uint16_t, 3>
        {
            const std::uint16_t ia = m_BatchIndices[i], ib = m_BatchIndices[i + 1], ic = m_BatchIndices[i + 2];
            return { ia, ib, ic };
        };

        if (m_CarPartBody && hash != 0 && m_CarReflBodyTextures.insert( hash ).second)
        {
            if (m_CarReflBodyTextures.size() > 256)
                m_CarReflBodyTextures = { hash };
            if (m_CarReflLines < 300)
            {
                ++m_CarReflLines;
                Log( "Car reflections: paint texture %08X (body)", hash );
            }
        }

        // Glossy parts mark the cells of their textures they cover: the cells of their corners and those whose centre is
        // inside.
        // The triangles of a part and level do not change from frame to frame: mark the cells once per (texture, part, pass, size).
        const std::uint64_t cells_key = (static_cast<std::uint64_t>(hash) << 32) ^ (static_cast<std::uint64_t>(m_CarPart & 0xFFFF) << 16) ^
            (static_cast<std::uint64_t>(mask & 0xFF) << 8) ^ (static_cast<std::uint64_t>(m_BatchIndices.size() / 3) * 0x9E3779B1ull);
        if (!m_CarPartMatte && hash != 0 && m_CarReflCellsDone.insert( cells_key ).second)
        {
            if (m_CarReflCellsDone.size() > 4096)
                m_CarReflCellsDone = { cells_key };
            auto found = m_CarReflPaintCells.find( hash );
            if (found == m_CarReflPaintCells.end())
            {
                if (m_CarReflPaintCells.size() > 256)
                {
                    m_CarReflPaintCells.clear();
                    m_CarReflCellsDone = { cells_key };
                }
                found = m_CarReflPaintCells.emplace( hash, std::array<std::uint64_t, 64>{} ).first;
            }
            auto & cells = found->second;
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                const auto c = corners( i );
                if (c[0] >= count || c[1] >= count || c[2] >= count)
                    continue;
                float u[3], v[3];
                for (int j = 0; j < 3; ++j)
                {
                    u[j] = m_BatchVertices[c[j]].m_U0 * 64.0f;
                    v[j] = m_BatchVertices[c[j]].m_V0 * 64.0f;
                    cells[cell( v[j] / 64.0f )] |= 1ull << cell( u[j] / 64.0f );
                }
                const float area = (u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0]);
                if (std::abs( area ) < 1e-9f)
                    continue;
                const int x0 = cell( std::min( { u[0], u[1], u[2] } ) / 64.0f ), x1 = cell( std::max( { u[0], u[1], u[2] } ) / 64.0f );
                const int y0 = cell( std::min( { v[0], v[1], v[2] } ) / 64.0f ), y1 = cell( std::max( { v[0], v[1], v[2] } ) / 64.0f );
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                    {
                        const float px = x + 0.5f, py = y + 0.5f;
                        const float a = ((u[1] - px) * (v[2] - py) - (u[2] - px) * (v[1] - py)) / area;
                        const float b = ((u[2] - px) * (v[0] - py) - (u[0] - px) * (v[2] - py)) / area;
                        if (a >= 0.0f && b >= 0.0f && a + b <= 1.0f)
                            cells[static_cast<std::size_t>(y)] |= 1ull << x;
                    }
            }
        }

        // Cloth tops and other parts from '[CarReflections] MatteParts', apart from the paint in them (split below).
        // B28: parts of '[CarReflections] MatteSolid' hold no paint, they stay matte all over.
        const bool matte_solid = m_CarPartMatte && m_CarPart >= 0 &&
            std::find( m_ConfigCarReflMatteSolid.begin(), m_ConfigCarReflMatteSolid.end(), m_CarPart >> 8 ) != m_ConfigCarReflMatteSolid.end();
        // B34: a body part may use a corner of the interior texture, that does not make the interior paint. A texture on which
        // one pure interior part has many triangles is the interior page, all matte parts on it stay matte. Measured on 40 paint
        // textures: at most 20 triangles of such a part (small trim); on the interior page 123 to 257.
        constexpr std::size_t INTERIOR_PAGE_TRIANGLES = 60;
        static constexpr int INTERIOR_PARTS[] = { 9, 24, 25, 26, 28, 29, 30, 43, 45, 46, 49, 56, 59 };
        if (m_CarPartMatte && hash != 0 && m_CarPart >= 0 && m_BatchIndices.size() / 3 >= INTERIOR_PAGE_TRIANGLES &&
            std::find( std::begin( INTERIOR_PARTS ), std::end( INTERIOR_PARTS ), m_CarPart >> 8 ) != std::end( INTERIOR_PARTS ))
        {
            if (m_CarReflInteriorPages.size() > 1024)
                m_CarReflInteriorPages.clear();
            if (m_CarReflInteriorPages.insert( hash ).second && m_CarReflLines < 400)
            {
                ++m_CarReflLines;
                Log( "Car reflections: texture %08X is the interior page (part %d, %zu triangles), its matte parts stay matte (B34)", hash,
                    m_CarPart >> 8, m_BatchIndices.size() / 3 );
            }
        }
        const bool interior_page = m_CarPartMatte && m_CarReflInteriorPages.count( hash ) != 0;
        const auto paint_cells = m_CarPartMatte && !matte_solid && !interior_page && m_CarReflBodyTextures.count( hash ) != 0 ? m_CarReflPaintCells.find( hash )
            : m_CarReflPaintCells.end();
        const bool matte_paint = paint_cells != m_CarReflPaintCells.end();
        const auto log_matte = [&]( std::size_t kept )
        {
            // Once matte and once as paint (the first frame the paint cells are not known yet).
            const std::uint64_t key = static_cast<std::uint64_t>(mask) << 56 | (matte_paint ? 1ull << 55 : 0ull) |
                static_cast<std::uint64_t>(m_CarPart & 0xFFFFFF) << 32 | hash;
            if (!m_CarReflMatteSeen.insert( key ).second || m_CarReflLines >= 300)
                return;
            ++m_CarReflLines;
            Log( "Car reflections: pass %02X matte part %d/%d texture %08X %dx%d%s, %zu of %zu triangles reflected as paint", mask,
                m_CarPart >> 8, m_CarPart & 0xFF, hash, texture != nullptr ? texture->m_Width : 0, texture != nullptr ? texture->m_Height : 0,
                matte_paint ? " (paint)" : "", kept, m_BatchIndices.size() / 3 );
            if (!matte_paint || m_ConfigCarReflDebug == 0)
                return;

            // With '[CarReflections] Debug', next to the log: the cells glossy parts cover ('#') and the corners of the part's
            // triangles on the texture.
            const std::size_t slash = m_LogPath.find_last_of( "\\/" );
            char name[64];
            std::snprintf( name, sizeof( name ), "carrefl_uv_%d_%d_%08X.txt", m_CarPart >> 8, m_CarPart & 0xFF, hash );
            FILE * file = std::fopen( ((slash != std::string::npos ? m_LogPath.substr( 0, slash + 1 ) : std::string()) + name).c_str(), "w" );
            if (file == nullptr)
                return;
            for (std::size_t y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 64; ++x)
                    std::fputc( (paint_cells->second[y] >> x & 1) != 0 ? '#' : '.', file );
                std::fputc( '\n', file );
            }
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                const auto c = corners( i );
                if (c[0] < count && c[1] < count && c[2] < count)
                    std::fprintf( file, "%.4f %.4f %.4f %.4f %.4f %.4f\n", m_BatchVertices[c[0]].m_U0, m_BatchVertices[c[0]].m_V0,
                        m_BatchVertices[c[1]].m_U0, m_BatchVertices[c[1]].m_V0, m_BatchVertices[c[2]].m_U0, m_BatchVertices[c[2]].m_V0 );
            }
            std::fclose( file );
        };
        // B28 (rear deck of 41): in a MatteSolid part the triangles of clearly coloured texels stay paint (red rear lid of a
        // 356 hardtop), the grey, black or scratched cloth stays matte. Needs readable colours (not BC compressed).
        bool solid_split = false;
        if (matte_solid && texture != nullptr)
        {
            const std::vector<std::uint32_t> & colours = CarReflCellColours( texture );
            if (!colours.empty())
            {
                const auto coloured = [&]( float u, float v )
                {
                    const std::uint32_t c = colours[static_cast<std::size_t>(cell( v )) * 64 + static_cast<std::size_t>(cell( u ))];
                    if (c == 0xFFFFFFFFu)
                        return false;
                    const int r = static_cast<int>(c >> 16 & 0xFF), g = static_cast<int>(c >> 8 & 0xFF), b = static_cast<int>(c & 0xFF);
                    return std::max( { r, g, b } ) - std::min( { r, g, b } ) >= 48;
                };
                m_CarReflIndices.clear();
                for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
                {
                    const auto c = corners( i );
                    if (c[0] >= count || c[1] >= count || c[2] >= count)
                        continue;
                    const TlVertex & a = m_BatchVertices[c[0]];
                    const TlVertex & b = m_BatchVertices[c[1]];
                    const TlVertex & d = m_BatchVertices[c[2]];
                    const float u = (a.m_U0 + b.m_U0 + d.m_U0) / 3.0f;
                    const float v = (a.m_V0 + b.m_V0 + d.m_V0) / 3.0f;
                    int votes = coloured( u, v ) ? 1 : 0;
                    for (const TlVertex * corner : { &a, &b, &d })
                        votes += coloured( (u + corner->m_U0) * 0.5f, (v + corner->m_V0) * 0.5f ) ? 1 : 0;
                    if (votes >= 3)
                        m_CarReflIndices.insert( m_CarReflIndices.end(), { c[0], c[1], c[2] } );
                }
                const std::size_t kept = m_CarReflIndices.size() / 3;
                const std::uint64_t key = 1ull << 63 | static_cast<std::uint64_t>(m_CarPart & 0xFFFFFF) << 32 | hash;
                if (m_CarReflMatteSeen.insert( key ).second && m_CarReflLines < 300)
                {
                    ++m_CarReflLines;
                    Log( "Car reflections: matte solid part %d/%d texture %08X: %zu of %zu coloured triangles reflected as paint (B28)",
                        m_CarPart >> 8, m_CarPart & 0xFF, hash, kept, m_BatchIndices.size() / 3 );
                }
                solid_split = kept != 0;
            }
        }
        if (m_CarPartMatte && !matte_paint && !solid_split)
        {
            log_matte( 0 );
            ++stats.m_Matte;
            m_CarReflOutcome = "matte part (MatteParts)";
            return;
        }

        // Paint in a matte part: only the triangles whose centre lies on cells a glossy part covers too (the rear
        // deck next to a cloth top on the same page). The body comes after the tops in R3DCarLOD::Draw: the first frame
        // the tops are matte all over.
        if (matte_paint)
        {
            const auto & cells = paint_cells->second;

            // Elsewhere the colour: the colours of the covered cells (each at least 2 % of them) are the paint with its stripes,
            // triangles of about these colours are paint too (cloth is dark, unless the car is).
            const std::vector<std::uint32_t> & colours = CarReflCellColours( texture );
            const auto channels = []( std::uint32_t c )
            {
                return std::array<int, 3>{ static_cast<int>(c >> 16 & 0xFF), static_cast<int>(c >> 8 & 0xFF), static_cast<int>(c & 0xFF) };
            };
            // 9.15e: kept per texture while the covered cells (and the grey setting) stay the same.
            std::uint64_t signature = 14695981039346656037ull ^ colours.size();
            for (const std::uint64_t row : cells)
            {
                signature ^= row;
                signature *= 1099511628211ull;
            }
            CarReflPalette & kept_palette = m_CarReflPalettes[hash];
            std::vector<std::array<int, 3>> & palette = kept_palette.m_Colours;
            const bool palette_fresh = kept_palette.m_Signature == signature && kept_palette.m_Grey == m_ConfigCarReflMatteGrey;
            if (!palette_fresh)
            {
                kept_palette.m_Signature = signature;
                kept_palette.m_Grey = m_ConfigCarReflMatteGrey;
                palette.clear();
            }
            if (!palette_fresh && !colours.empty())
            {
                std::map<std::uint32_t, std::array<std::uint32_t, 4>> bins; // count, sums of r, g, b
                std::uint32_t covered = 0;
                for (std::size_t y = 0; y < 64; ++y)
                    for (std::size_t x = 0; x < 64; ++x)
                        if ((cells[y] >> x & 1) != 0 && colours[y * 64 + x] != 0xFFFFFFFFu)
                        {
                            const auto rgb = channels( colours[y * 64 + x] );
                            auto & bin = bins[static_cast<std::uint32_t>((rgb[0] >> 4) << 8 | (rgb[1] >> 4) << 4 | (rgb[2] >> 4))];
                            ++bin[0];
                            ++covered;
                            for (std::size_t j = 0; j < 3; ++j)
                                bin[j + 1] += static_cast<std::uint32_t>(rgb[j]);
                        }
                // The main colour: the largest bin (B28).
                std::uint32_t largest = 0;
                for (const auto & entry : bins)
                    largest = std::max( largest, entry.second[0] );
                std::string listed;
                for (const auto & entry : bins)
                    if (entry.second[0] * 50 >= covered)
                    {
                        const std::array<int, 3> rgb = { static_cast<int>(entry.second[1] / entry.second[0]),
                            static_cast<int>(entry.second[2] / entry.second[0]), static_cast<int>(entry.second[3] / entry.second[0]) };
                        // Grey (spread of the channels below 32) only as the main colour: trim and shading of a coloured car
                        // are no paint, and the dark cloth of the 356 is close to them.
                        const int spread = std::max( { rgb[0], rgb[1], rgb[2] } ) - std::min( { rgb[0], rgb[1], rgb[2] } );
                        const bool grey = spread < 32 && entry.second[0] != largest;
                        char text[48];
                        std::snprintf( text, sizeof( text ), " %02X%02X%02X %u%%%s", rgb[0], rgb[1], rgb[2],
                            entry.second[0] * 100 / std::max( covered, 1u ), grey ? " grey" : "" );
                        listed += text;
                        if (!grey || m_ConfigCarReflMatteGrey > 0)
                            palette.push_back( rgb );
                    }
                if (m_CarReflPaletteLogged.insert( hash ).second && m_CarReflPaletteLogged.size() <= 64 && m_CarReflLines < 300)
                {
                    ++m_CarReflLines;
                    Log( "Car reflections: paint colours of texture %08X:%s%s", hash, listed.c_str(),
                        m_ConfigCarReflMatteGrey > 0 ? " (grey kept)" : "" );
                }
            }
            const auto like_paint = [&]( float u, float v )
            {
                const std::uint32_t colour = colours[static_cast<std::size_t>(cell( v )) * 64 + static_cast<std::size_t>(cell( u ))];
                if (colour == 0xFFFFFFFFu)
                    return false;
                const auto rgb = channels( colour );
                for (const auto & paint : palette)
                {
                    int distance = 0;
                    for (std::size_t j = 0; j < 3; ++j)
                        distance += (rgb[j] - paint[j]) * (rgb[j] - paint[j]);
                    if (distance < 40 * 40)
                        return true;
                }
                return false;
            };

            // B28: the decision of the first time this car drew this part with this geometry, if there is one.
            const std::size_t triangles = m_BatchIndices.size() / 3;
            std::uint64_t geometry = 14695981039346656037ull;
            const auto mix = [&geometry]( std::uint64_t value )
            {
                for (int shift = 0; shift < 64; shift += 8)
                {
                    geometry ^= value >> shift & 0xFF;
                    geometry *= 1099511628211ull;
                }
            };
            CarReflMatteKeep * kept_before = nullptr;
            if (m_ConfigCarReflMatteKeep > 0)
            {
                mix( reinterpret_cast<std::uintptr_t>(m_CarLodObject) );
                mix( static_cast<std::uint32_t>(m_CarPart) );
                mix( triangles );
                for (std::size_t i = 0; i < m_BatchIndices.size(); ++i)
                {
                    const std::uint16_t index = m_BatchIndices[i];
                    if (index >= count)
                        continue;
                    const std::uint32_t qu = static_cast<std::uint32_t>(std::lround( m_BatchVertices[index].m_U0 * 4096.0f ));
                    const std::uint32_t qv = static_cast<std::uint32_t>(std::lround( m_BatchVertices[index].m_V0 * 4096.0f ));
                    mix( static_cast<std::uint64_t>(qu) << 32 | qv );
                }
                const auto found = m_CarReflMatteKeep.find( geometry );
                if (found != m_CarReflMatteKeep.end() && found->second.m_Keep.size() == triangles)
                    kept_before = &found->second;
            }

            m_CarReflIndices.clear();
            // 9.15e: with a decision kept, the triangles are judged again only for the one comparison after a page change
            // (damage, logged once); otherwise the kept decision is used as it is.
            const bool judge = kept_before == nullptr || (kept_before->m_Hash != hash && !kept_before->m_Logged);
            static thread_local std::vector<std::uint8_t> decision;
            decision.assign( kept_before == nullptr ? triangles : 0, 0 );
            std::size_t changed = 0;
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                const auto c = corners( i );
                if (c[0] >= count || c[1] >= count || c[2] >= count)
                    continue;
                bool keep = false;
                if (judge)
                {
                    const TlVertex & a = m_BatchVertices[c[0]];
                    const TlVertex & b = m_BatchVertices[c[1]];
                    const TlVertex & d = m_BatchVertices[c[2]];
                    const float u = (a.m_U0 + b.m_U0 + d.m_U0) / 3.0f;
                    const float v = (a.m_V0 + b.m_V0 + d.m_V0) / 3.0f;
                    keep = (cells[static_cast<std::size_t>(cell( v ))] >> cell( u ) & 1) != 0;
                    if (!keep && !palette.empty())
                    {
                        // The centre and halfway to each corner, most of them like the paint.
                        int votes = like_paint( u, v ) ? 1 : 0;
                        for (const TlVertex * corner : { &a, &b, &d })
                            votes += like_paint( (u + corner->m_U0) * 0.5f, (v + corner->m_V0) * 0.5f ) ? 1 : 0;
                        keep = votes >= 3;
                    }
                }
                if (kept_before != nullptr)
                {
                    const bool before = kept_before->m_Keep[i / 3] != 0;
                    changed += judge && before != keep ? 1 : 0;
                    keep = before;
                }
                else
                    decision[i / 3] = keep ? 1 : 0;
                if (keep)
                    m_CarReflIndices.insert( m_CarReflIndices.end(), { c[0], c[1], c[2] } );
            }
            if (m_ConfigCarReflMatteKeep > 0)
            {
                if (kept_before == nullptr)
                {
                    if (m_CarReflMatteKeep.size() >= 4096)
                        m_CarReflMatteKeep.clear();
                    m_CarReflMatteKeep[geometry] = { hash, false, std::move( decision ) };
                }
                else if (judge)
                {
                    // Compared once after the page changed (9.15e: also when nothing changed, so it is not judged again).
                    kept_before->m_Logged = true;
                    if (changed != 0 && m_CarReflLines < 300)
                    {
                        // Once per part and car: the new page (damage) would have reflected other triangles.
                        ++m_CarReflLines;
                        Log( "Car reflections: matte part %d/%d texture %08X (was %08X): %zu of %zu triangles kept as decided first (B28)",
                            m_CarPart >> 8, m_CarPart & 0xFF, hash, kept_before->m_Hash, changed, triangles );
                    }
                }
            }
            const std::size_t kept = m_CarReflIndices.size() / 3;
            log_matte( kept );
            stats.m_MatteTriangles += m_BatchIndices.size() / 3 - kept;
            if (kept == 0)
            {
                ++stats.m_Matte;
                m_CarReflOutcome = "matte part, no paint triangles";
                return;
            }
        }

        // The game blends the whole body (SrcAlpha/InvSrcAlpha), opaque paint and glass are told apart per pixel by alpha
        // (shaders::CAR_REFLECT); other blends (additive lights) are left out.
        if (blended && !(source == 5 && destination == 6))
        {
            ++stats.m_Blended;
            m_CarReflOutcome = "other blend";
            return;
        }

        if (!m_CarReflCamera)
        {
            ++m_CarReflNoCamera;
            m_CarReflOutcome = "no camera";
            return;
        }

        if (!m_CarReflSourceValid || !m_CarReflVs || !m_CarReflPs || !m_CarReflLayout || !m_CarReflConstants ||
            !m_CarReflVertexBuffer || !m_CarReflIndexBuffer || count > CAR_REFL_VERTICES || m_BatchIndices.size() > CAR_REFL_INDICES)
        {
            m_CarReflOutcome = "not ready or too big";
            return;
        }

        // Small and far parts: a reflection a few pixels wide is not seen ('[CarReflections] MinSize', in pixels of 1440 lines).
        if (m_ConfigCarReflMinSize > 0.0f)
        {
            float low_x = 1e30f, high_x = -1e30f, low_y = 1e30f, high_y = -1e30f;
            for (const TlVertex & v : m_BatchVertices)
            {
                low_x = std::min( low_x, v.m_X );
                high_x = std::max( high_x, v.m_X );
                low_y = std::min( low_y, v.m_Y );
                high_y = std::max( high_y, v.m_Y );
            }
            if (std::max( high_x - low_x, high_y - low_y ) * 1440.0f / static_cast<float>(std::max( m_Height, 1 )) < m_ConfigCarReflMinSize)
            {
                ++stats.m_Skipped;
                m_CarReflOutcome = "MinSize";
                return;
            }
        }

        // Up of the car: row 1 of object x view (camera + 0x188, row vectors, as PrepareHeadMesh) of the part drawn last.
        const std::uintptr_t renderer = *reinterpret_cast<const std::uintptr_t *>(0x0069E0D4);
        std::uintptr_t camera = 0;
        float model_view[16] = {};
        double up[3] = {};
        if (renderer != 0 && SafeCopy( reinterpret_cast<const void *>(renderer), &camera, 4 ) && camera != 0 &&
            SafeCopy( reinterpret_cast<const void *>(camera + 0x188), model_view, sizeof( model_view ) ))
        {
            const double up_length = std::sqrt( static_cast<double>(model_view[4]) * model_view[4] +
                static_cast<double>(model_view[5]) * model_view[5] + static_cast<double>(model_view[6]) * model_view[6] );
            if (up_length > 1e-6 && up_length < 1e6)
                for (int j = 0; j < 3; ++j)
                    up[j] = model_view[4 + j] / up_length;
            // Snow on the car (15.3b): which way this "up" points was never checked on screen. The camera is above the car
            // in nearly every view (chase, bonnet, cockpit, most TV cameras), so the side of the camera votes for the sign.
            if (up_length > 1e-6 && up_length < 1e6)
            {
                const double side = -(model_view[12] * up[0] + model_view[13] * up[1] + model_view[14] * up[2]);
                if (std::abs( side ) > 0.2)
                {
                    m_CarUpVote += ((side > 0.0 ? 1.0f : -1.0f) - m_CarUpVote) * 0.01f;
                    ++m_CarUpVotes[side > 0.0 ? 0 : 1];
                }
            }
        }
        // Weather on the car (w3): view -> space of the part, the inverse of object x view (row vectors: view = object x R + t).
        // Drops and snow hash this position, the UV of the car textures are mirrored left and right.
        double object_of_view[9] = {};
        // 9.15e: R (rows of object x view) and its inverse as it is, the mirror of w4 not taken out.
        double object_rows[9] = {};
        double object_raw[9] = {};
        bool object_valid = false;
        {
            const double r[9] = { model_view[0], model_view[1], model_view[2], model_view[4], model_view[5], model_view[6],
                                  model_view[8], model_view[9], model_view[10] };
            const double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
            if (std::abs( det ) > 1e-9 && std::abs( det ) < 1e9)
            {
                const double inv = 1.0 / det;
                object_of_view[0] = (r[4] * r[8] - r[5] * r[7]) * inv;
                object_of_view[1] = (r[2] * r[7] - r[1] * r[8]) * inv;
                object_of_view[2] = (r[1] * r[5] - r[2] * r[4]) * inv;
                object_of_view[3] = (r[5] * r[6] - r[3] * r[8]) * inv;
                object_of_view[4] = (r[0] * r[8] - r[2] * r[6]) * inv;
                object_of_view[5] = (r[2] * r[3] - r[0] * r[5]) * inv;
                object_of_view[6] = (r[3] * r[7] - r[4] * r[6]) * inv;
                object_of_view[7] = (r[1] * r[6] - r[0] * r[7]) * inv;
                object_of_view[8] = (r[0] * r[4] - r[1] * r[3]) * inv;
                object_valid = true;
                std::memcpy( object_rows, r, sizeof( object_rows ) );
                std::memcpy( object_raw, object_of_view, sizeof( object_raw ) );
                // w4: a negative determinant is a mirrored draw (one half of the body drawn twice, flipped left/right): the
                // same object x on both sides. With y up and z along the car kept, the flipped axis is x.
                if (det < 0.0)
                {
                    for (int j = 0; j < 3; ++j)
                        object_of_view[j * 3] = -object_of_view[j * 3];
                    ++m_CarMirrorParts[1];
                }
                else
                    ++m_CarMirrorParts[0];
            }
        }
        // Drops flowing with the speed of the car (w5): only when there are drops.
        int car_flow = -1;
        if (object_valid && m_CarWeatherOn && m_CarWeather[1] > 0.0f)
        {
            const double r[9] = { model_view[0], model_view[1], model_view[2], model_view[4], model_view[5], model_view[6],
                                  model_view[8], model_view[9], model_view[10] };
            const double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
            car_flow = CarFlowTrackFor( model_view, det < 0.0 );
            ++m_CarFlowStats[car_flow >= 0 ? 0 : 1];
        }

        // Camera space of every corner: screen -> clip (xc, yc, wc) -> view, as the water reflections.
        const float * viewport = m_CarReflViewport.data();
        const float * view_of_clip = m_CarReflViewOfClip.data();
        const auto valid = [&]( const TlVertex & v ) { return v.m_Rhw > 0.0f && v.m_Rhw < 1e30f; };
        m_CarReflView.resize( count );
        for (std::size_t i = 0; i < count; ++i)
        {
            const TlVertex & v = m_BatchVertices[i];
            auto & out = m_CarReflView[i];
            if (!valid( v ))
            {
                out = { 0.0, 0.0, 0.0 };
                continue;
            }
            const double w = 1.0 / v.m_Rhw;
            const double clip[4] = { (v.m_X - viewport[2]) * w / viewport[0], (v.m_Y - viewport[3]) * w / viewport[1], w, 1.0 };
            for (int j = 0; j < 3; ++j)
                out[static_cast<std::size_t>(j)] = clip[0] * view_of_clip[j] + clip[1] * view_of_clip[4 + j] + clip[2] * view_of_clip[8 + j] +
                    clip[3] * view_of_clip[12 + j];
        }

        // Corners at the same point are one vertex for the normals (the driver gets the triangles one by one).
        // The groups of a part are kept (m_CarReflWeldCache) and checked on a few corners; sorted anew only when they fail.
        const auto same_point = [&]( std::uint32_t a, std::uint32_t b )
        {
            const TlVertex & p = m_BatchVertices[a];
            const TlVertex & q = m_BatchVertices[b];
            return std::memcmp( &p.m_X, &q.m_X, 4 ) == 0 && std::memcmp( &p.m_Y, &q.m_Y, 4 ) == 0 && std::memcmp( &p.m_Rhw, &q.m_Rhw, 4 ) == 0;
        };
        // 9.15e: texture, part, pass and sizes alone were not enough. Two meshes with the same counts (left and right half
        // modelled apart, the same part of two cars) shared one entry and threw each other out every frame (17 % rebuilt
        // in the garage, 29 % in a race). A fingerprint of the index list and the texture coordinates tells them apart.
        std::uint64_t shape = 14695981039346656037ull;
        {
            const auto mix = [&shape]( std::uint32_t value )
            {
                shape ^= value;
                shape *= 1099511628211ull;
            };
            for (const std::uint16_t index : m_BatchIndices)
                mix( index );
            for (const TlVertex & v : m_BatchVertices)
            {
                mix( static_cast<std::uint32_t>(std::lround( v.m_U0 * 4096.0f )) );
                mix( static_cast<std::uint32_t>(std::lround( v.m_V0 * 4096.0f )) );
            }
        }
        const std::uint64_t weld_key = cells_key ^ (static_cast<std::uint64_t>(count) * 0xC2B2AE3D27D4EB4Full) ^
            (static_cast<std::uint64_t>(blended ? 1 : 0) << 63) ^ (shape * 0x9E3779B97F4A7C15ull);
        CarReflWeld * weld_entry = nullptr;
        bool weld_known = false;
        {
            const auto found = m_CarReflWeldCache.find( weld_key );
            if (found != m_CarReflWeldCache.end() && found->second.m_Group.size() == count)
            {
                weld_known = true;
                CarReflWeld & entry = found->second;
                bool still = true;
                for (int k = 0; still && k < entry.m_SameCount; ++k)
                    still = same_point( entry.m_Same[2 * k], entry.m_Same[2 * k + 1] );
                for (int k = 0; still && k < entry.m_ApartCount; ++k)
                    still = !same_point( entry.m_Apart[2 * k], entry.m_Apart[2 * k + 1] );
                if (still)
                    weld_entry = &entry;
            }
        }
        if (weld_entry != nullptr)
        {
            ++m_CarReflWeldHits;
            weld_entry->m_LastUse = m_FlipCount;
        }
        else
        {
            ++m_CarReflWeldMisses;
            ++(weld_known ? m_CarReflWeldFailed : m_CarReflWeldNew);
            // The keys once, then sorted with the index: the same groups as comparing the vertices over and over.
            struct WeldKey
            {
                std::uint32_t m_Key[3];
                std::uint32_t m_Index;
            };
            static thread_local std::vector<WeldKey> weld;
            weld.resize( count );
            for (std::size_t i = 0; i < count; ++i)
            {
                const TlVertex & v = m_BatchVertices[i];
                std::memcpy( &weld[i].m_Key[0], &v.m_X, 4 );
                std::memcpy( &weld[i].m_Key[1], &v.m_Y, 4 );
                std::memcpy( &weld[i].m_Key[2], &v.m_Rhw, 4 );
                weld[i].m_Index = static_cast<std::uint32_t>(i);
            }
            std::sort( weld.begin(), weld.end(), []( const WeldKey & a, const WeldKey & b )
            {
                return std::lexicographical_compare( a.m_Key, a.m_Key + 3, b.m_Key, b.m_Key + 3 );
            } );
            // Full: first the entries not used for 60 frames (variants of cars far behind, clipped shapes of an earlier
            // view); only when that does not free a quarter, all of it as before.
            if (m_CarReflWeldCache.size() >= 4096)
            {
                for (auto it = m_CarReflWeldCache.begin(); it != m_CarReflWeldCache.end();)
                {
                    if (m_FlipCount - it->second.m_LastUse > 60 && it->first != weld_key)
                    {
                        it = m_CarReflWeldCache.erase( it );
                        ++m_CarReflWeldEvicted;
                    }
                    else
                        ++it;
                }
                if (m_CarReflWeldCache.size() >= 3072)
                {
                    m_CarReflWeldCache.clear();
                    ++m_CarReflWeldClears;
                }
            }
            CarReflWeld & entry = m_CarReflWeldCache[weld_key];
            entry.m_LastUse = m_FlipCount;
            entry.m_ObjNormal.clear(); // 9.15e: new groups, the kept normals no longer fit
            entry.m_Group.resize( count );
            std::uint32_t groups = 0;
            for (std::size_t k = 0; k < count; ++k)
            {
                if (k > 0 && std::memcmp( weld[k].m_Key, weld[k - 1].m_Key, 12 ) != 0)
                    ++groups;
                entry.m_Group[weld[k].m_Index] = groups;
            }
            entry.m_Groups = groups;

            // Test pairs spread over the part: the first corner of a group with a later one of it, and neighbouring corners
            // of different groups.
            static thread_local std::vector<std::uint32_t> first;
            first.assign( static_cast<std::size_t>(groups) + 1, 0xFFFFFFFFu );
            for (std::size_t i = 0; i < count; ++i)
            {
                auto & f = first[entry.m_Group[i]];
                if (f == 0xFFFFFFFFu)
                    f = static_cast<std::uint32_t>(i);
            }
            entry.m_SameCount = 0;
            entry.m_ApartCount = 0;
            for (std::size_t k = 0; k < 8; ++k)
            {
                const std::size_t start = k * count / 8;
                for (std::size_t i = start; i < count && entry.m_SameCount <= static_cast<int>(k); ++i)
                {
                    const std::uint32_t f = first[entry.m_Group[i]];
                    if (f != i && entry.m_SameCount < 8)
                    {
                        entry.m_Same[2 * entry.m_SameCount] = f;
                        entry.m_Same[2 * entry.m_SameCount + 1] = static_cast<std::uint32_t>(i);
                        ++entry.m_SameCount;
                    }
                }
                if (start + 1 < count && entry.m_Group[start] != entry.m_Group[start + 1] && entry.m_ApartCount < 8)
                {
                    entry.m_Apart[2 * entry.m_ApartCount] = static_cast<std::uint32_t>(start);
                    entry.m_Apart[2 * entry.m_ApartCount + 1] = static_cast<std::uint32_t>(start + 1);
                    ++entry.m_ApartCount;
                }
            }
            weld_entry = &entry;
        }
        CarReflWeld & weld = *weld_entry;
        const std::vector<std::uint32_t> & group_of = weld.m_Group;
        const std::uint32_t groups = weld.m_Groups;

        // 9.15e '[CarReflections] ObjectNormals': a part is rigid in its own space, so its normals are built once there and
        // only turned into view space each frame (REFLEXION-WETTER.md C9-C12). Raw object space = (view - t) x R^-1 with the
        // mirror of w4 kept, so both mirrored halves of a body share the cached normals and R^-T turns them outwards again.
        const bool object_normals = m_ConfigCarReflObjectNormals != 0 && object_valid;
        const int seam_fix = object_normals && m_ConfigCarReflSeamFix != 0 ? 1 : 0;
        const auto raw_object = [&]( std::size_t i, double out[3] )
        {
            const auto & view = m_CarReflView[i];
            const double d[3] = { view[0] - model_view[12], view[1] - model_view[13], view[2] - model_view[14] };
            for (int j = 0; j < 3; ++j)
                out[j] = d[0] * object_raw[j] + d[1] * object_raw[3 + j] + d[2] * object_raw[6 + j];
        };
        float extent = 0.0f;
        bool cached = false;
        if (object_normals)
        {
            float low_x = 1e30f, high_x = -1e30f, low_y = 1e30f, high_y = -1e30f;
            for (const TlVertex & v : m_BatchVertices)
            {
                low_x = std::min( low_x, v.m_X );
                high_x = std::max( high_x, v.m_X );
                low_y = std::min( low_y, v.m_Y );
                high_y = std::max( high_y, v.m_Y );
            }
            extent = std::max( high_x - low_x, high_y - low_y );
            // Built far away (few pixels, rounded screen positions) and now drawn much larger: build anew.
            cached = weld.m_ObjNormal.size() == count && weld.m_ObjCrease == m_ConfigCarReflCreaseCos && weld.m_ObjSeam == seam_fix &&
                extent <= weld.m_ObjExtent * 2.0f + 1.0f;
            for (int k = 0; cached && k < weld.m_ObjCheckCount; ++k)
            {
                const std::uint32_t index = weld.m_ObjCheckIndex[static_cast<std::size_t>(k)];
                if (!valid( m_BatchVertices[index] ))
                {
                    cached = false;
                    break;
                }
                double p[3];
                raw_object( index, p );
                const auto & kept = weld.m_ObjCheck[static_cast<std::size_t>(k)];
                for (int j = 0; j < 3; ++j)
                    cached = cached && std::abs( p[j] - kept[static_cast<std::size_t>(j)] ) < 0.005;
            }
        }

        // Orientation of the faces (9.15e, C10). Before: each face turned towards the camera, so the back faces the GPU culls
        // counted as front faces and bent the corners on the outline towards the camera (and jumped when a face turned).
        // Now one sign for the whole batch: the faces the GPU draws (screen winding against CULLMODE) face the camera, the
        // index order then gives every other face the same way round. A sign wrong as a whole does no harm (the shader turns
        // the normal to the camera per pixel); what counts is that all faces of a point agree.
        int orient = 0; // 0 = each face to the camera (as before), +1 / -1 = fixed sign for the batch
        if (object_normals && !cached)
        {
            const std::uint32_t cull_mode = render[rs::CULLMODE] & 3;
            std::int64_t vote = 0;
            std::uint64_t visible = 0;
            double centre[3] = {};
            std::size_t centre_count = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (valid( m_BatchVertices[i] ))
                {
                    for (int j = 0; j < 3; ++j)
                        centre[j] += m_CarReflView[i][static_cast<std::size_t>(j)];
                    ++centre_count;
                }
            for (double & x : centre)
                x /= static_cast<double>(std::max<std::size_t>( centre_count, 1 ));
            double outward = 0.0;
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                const std::uint16_t ia = m_BatchIndices[i], ib = m_BatchIndices[i + 1], ic = m_BatchIndices[i + 2];
                if (ia >= count || ib >= count || ic >= count || !valid( m_BatchVertices[ia] ) || !valid( m_BatchVertices[ib] ) ||
                    !valid( m_BatchVertices[ic] ))
                    continue;
                const auto & a = m_CarReflView[ia];
                const auto & b = m_CarReflView[ib];
                const auto & c = m_CarReflView[ic];
                const double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
                const double v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
                const double cross[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
                const double facing = cross[0] * a[0] + cross[1] * a[1] + cross[2] * a[2];
                // Centre of the part as a fallback without culling: faces point away from it.
                const double mid[3] = { (a[0] + b[0] + c[0]) / 3.0 - centre[0], (a[1] + b[1] + c[1]) / 3.0 - centre[1],
                                        (a[2] + b[2] + c[2]) / 3.0 - centre[2] };
                outward += cross[0] * mid[0] + cross[1] * mid[1] + cross[2] * mid[2];
                if (cull_mode != 2 && cull_mode != 3)
                    continue;
                const TlVertex & pa = m_BatchVertices[ia];
                const TlVertex & pb = m_BatchVertices[ib];
                const TlVertex & pc = m_BatchVertices[ic];
                // Screen y points down: area > 0 is clockwise on screen. CULL_CW (2) drops those, CULL_CCW (3) the others.
                const double area = (static_cast<double>(pb.m_X) - pa.m_X) * (static_cast<double>(pc.m_Y) - pa.m_Y) -
                    (static_cast<double>(pc.m_X) - pa.m_X) * (static_cast<double>(pb.m_Y) - pa.m_Y);
                if (std::abs( area ) < 1e-6 || facing == 0.0)
                    continue;
                if ((cull_mode == 2 ? area < 0.0 : area > 0.0))
                {
                    ++visible;
                    vote += facing < 0.0 ? 1 : -1;
                }
            }
            if (visible > 0)
            {
                const std::uint64_t against = (visible - static_cast<std::uint64_t>(vote < 0 ? -vote : vote)) / 2;
                m_CarReflOrientVisible += visible;
                m_CarReflOrientAgainst += against;
                // Mixed winding (more than a tenth against the rest): turned per face as before, and not kept.
                if (against * 10 > visible)
                    ++m_CarReflOrientMixed;
                else
                {
                    orient = vote >= 0 ? 1 : -1;
                    ++m_CarReflOrientCull;
                }
            }
            else
            {
                orient = outward >= 0.0 ? 1 : -1;
                ++m_CarReflOrientCentre;
            }
        }

        const auto face_of = [&]( std::size_t i, double cross[3] ) -> bool
        {
            const std::uint16_t ia = m_BatchIndices[i], ib = m_BatchIndices[i + 1], ic = m_BatchIndices[i + 2];
            if (ia >= count || ib >= count || ic >= count || !valid( m_BatchVertices[ia] ) || !valid( m_BatchVertices[ib] ) ||
                !valid( m_BatchVertices[ic] ))
                return false;
            const auto & a = m_CarReflView[ia];
            const auto & b = m_CarReflView[ib];
            const auto & c = m_CarReflView[ic];
            const double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const double v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            cross[0] = u[1] * v[2] - u[2] * v[1];
            cross[1] = u[2] * v[0] - u[0] * v[2];
            cross[2] = u[0] * v[1] - u[1] * v[0];
            const bool turn = orient == 0 ? cross[0] * a[0] + cross[1] * a[1] + cross[2] * a[2] > 0.0 : orient < 0;
            if (turn)
                for (int j = 0; j < 3; ++j)
                    cross[j] = -cross[j];
            return true;
        };

        // Area weighted face normals, summed per point (not needed when the part's normals are kept).
        if (!cached)
        {
            m_CarReflNormal.assign( static_cast<std::size_t>(groups) + 1, { 0.0, 0.0, 0.0 } );
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                double cross[3];
                if (!face_of( i, cross ))
                    continue;
                for (int k = 0; k < 3; ++k)
                {
                    auto & sum = m_CarReflNormal[group_of[m_BatchIndices[i + k]]];
                    for (int j = 0; j < 3; ++j)
                        sum[static_cast<std::size_t>(j)] += cross[j];
                }
            }
        }

        // '[CarReflections] CreaseAngle' (9.15b): a corner takes only the faces at its point that lie within the angle of its own
        // faces (those using this very corner), so creases and sharp edges stay sharp instead of being rounded over.
        const bool crease = m_ConfigCarReflCreaseCos >= -1.0f && !cached;
        static thread_local std::vector<std::array<double, 3>> crease_own;
        static thread_local std::vector<std::array<double, 3>> crease_sum;
        static thread_local std::vector<std::uint32_t> group_start;
        static thread_local std::vector<std::uint32_t> group_list;
        if (crease)
        {
            crease_own.assign( count, { 0.0, 0.0, 0.0 } );
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                double cross[3];
                if (!face_of( i, cross ))
                    continue;
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 3; ++j)
                        crease_own[m_BatchIndices[i + k]][static_cast<std::size_t>(j)] += cross[j];
            }
            for (auto & own : crease_own)
            {
                const double length = std::sqrt( own[0] * own[0] + own[1] * own[1] + own[2] * own[2] );
                if (length > 1e-20)
                    for (double & x : own)
                        x /= length;
            }
            // The corners of each point, as a list per group.
            group_start.assign( static_cast<std::size_t>(groups) + 2, 0 );
            for (std::size_t i = 0; i < count; ++i)
                ++group_start[group_of[i] + 1];
            for (std::size_t g = 1; g < group_start.size(); ++g)
                group_start[g] += group_start[g - 1];
            group_list.resize( count );
            {
                static thread_local std::vector<std::uint32_t> cursor;
                cursor.assign( group_start.begin(), group_start.end() - 1 );
                for (std::size_t i = 0; i < count; ++i)
                    group_list[cursor[group_of[i]]++] = static_cast<std::uint32_t>(i);
            }
            crease_sum.assign( count, { 0.0, 0.0, 0.0 } );
            const double limit = m_ConfigCarReflCreaseCos;
            for (std::size_t i = 0; i + 2 < m_BatchIndices.size(); i += 3)
            {
                double cross[3];
                if (!face_of( i, cross ))
                    continue;
                const double length = std::sqrt( cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2] );
                if (length <= 1e-20)
                    continue;
                for (int k = 0; k < 3; ++k)
                {
                    const std::uint32_t group = group_of[m_BatchIndices[i + k]];
                    for (std::uint32_t at = group_start[group]; at < group_start[group + 1]; ++at)
                    {
                        const std::uint32_t corner = group_list[at];
                        const auto & own = crease_own[corner];
                        if ((cross[0] * own[0] + cross[1] * own[1] + cross[2] * own[2]) / length >= limit)
                            for (int j = 0; j < 3; ++j)
                                crease_sum[corner][static_cast<std::size_t>(j)] += cross[j];
                    }
                }
            }
        }

        // Normal of every corner in view space (zero = no face at this corner): from the part cache, or summed above and then
        // kept in the space of the part (9.15e).
        static thread_local std::vector<std::array<double, 3>> corner_normal;
        corner_normal.resize( count );
        const auto to_view = [&]( const std::array<float, 3> & n, std::array<double, 3> & out )
        {
            for (int k = 0; k < 3; ++k)
                out[static_cast<std::size_t>(k)] = n[0] * object_raw[k * 3] + n[1] * object_raw[k * 3 + 1] + n[2] * object_raw[k * 3 + 2];
            const double length = std::sqrt( out[0] * out[0] + out[1] * out[1] + out[2] * out[2] );
            for (double & x : out)
                x = length > 1e-20 ? x / length : 0.0;
        };
        if (cached)
        {
            for (std::size_t i = 0; i < count; ++i)
                to_view( weld.m_ObjNormal[i], corner_normal[i] );
            ++m_CarReflObjCached;
        }
        else
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto & crease_corner = crease ? crease_sum[i] : m_CarReflNormal[group_of[i]];
                const bool crease_found = crease && (crease_corner[0] != 0.0 || crease_corner[1] != 0.0 || crease_corner[2] != 0.0);
                const auto & sum = crease_found ? crease_corner : m_CarReflNormal[group_of[i]];
                const double length = std::sqrt( sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2] );
                for (int j = 0; j < 3; ++j)
                    corner_normal[i][static_cast<std::size_t>(j)] = length > 1e-20 ? sum[static_cast<std::size_t>(j)] / length : 0.0;
            }
            // Keep them: n_object = n_view x R^T (normals go with R^-T). With a fixed orientation only, the old way per face
            // depends on the camera.
            if (object_normals && orient != 0)
            {
                weld.m_ObjNormal.resize( count );
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto & n = corner_normal[i];
                    double o[3];
                    for (int j = 0; j < 3; ++j)
                        o[j] = n[0] * object_rows[j * 3] + n[1] * object_rows[j * 3 + 1] + n[2] * object_rows[j * 3 + 2];
                    double length = std::sqrt( o[0] * o[0] + o[1] * o[1] + o[2] * o[2] );
                    // SeamFix (C12): the body halves are drawn apart, mirrored about x = 0 of the part, and never welded there.
                    // A corner on that plane gets the normal of the two halves together: no part across.
                    if (seam_fix != 0 && length > 1e-20 && valid( m_BatchVertices[i] ))
                    {
                        double p[3];
                        raw_object( i, p );
                        if (std::abs( p[0] ) < 0.002 && std::abs( o[0] ) < 0.7 * length)
                        {
                            o[0] = 0.0;
                            length = std::sqrt( o[1] * o[1] + o[2] * o[2] );
                            ++m_CarReflSeamCorners;
                        }
                    }
                    auto & kept = weld.m_ObjNormal[i];
                    for (int j = 0; j < 3; ++j)
                        kept[static_cast<std::size_t>(j)] = length > 1e-20 ? static_cast<float>(o[j] / length) : 0.0f;
                    if (seam_fix != 0)
                        to_view( kept, corner_normal[i] );
                }
                // Corners to check the shape by, spread over the part.
                weld.m_ObjCheckCount = 0;
                for (std::size_t k = 0; k < 8 && count > 0; ++k)
                {
                    for (std::size_t i = k * count / 8; i < (k + 1) * count / 8; ++i)
                        if (valid( m_BatchVertices[i] ))
                        {
                            double p[3];
                            raw_object( i, p );
                            const std::size_t at = static_cast<std::size_t>(weld.m_ObjCheckCount++);
                            weld.m_ObjCheckIndex[at] = static_cast<std::uint32_t>(i);
                            weld.m_ObjCheck[at] = { static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]) };
                            break;
                        }
                }
                weld.m_ObjCrease = m_ConfigCarReflCreaseCos;
                weld.m_ObjSeam = seam_fix;
                weld.m_ObjExtent = extent;
                ++m_CarReflObjBuilt;
            }
        }

        m_CarReflVertices.resize( count );
        for (std::size_t i = 0; i < count; ++i)
        {
            const TlVertex & v = m_BatchVertices[i];
            CarReflVertex & out = m_CarReflVertices[i];
            out.m_Position[0] = v.m_X;
            out.m_Position[1] = v.m_Y;
            out.m_Position[2] = v.m_Z;
            out.m_Position[3] = v.m_Rhw;
            const auto & view = m_CarReflView[i];
            const auto & normal = corner_normal[i];
            const bool has_normal = normal[0] != 0.0 || normal[1] != 0.0 || normal[2] != 0.0;
            const double view_length = std::sqrt( view[0] * view[0] + view[1] * view[1] + view[2] * view[2] );
            for (int j = 0; j < 3; ++j)
            {
                out.m_View[j] = static_cast<float>(view[static_cast<std::size_t>(j)]);
                // Without a face: facing the camera.
                out.m_Normal[j] = static_cast<float>(has_normal ? normal[static_cast<std::size_t>(j)]
                    : (view_length > 0.0 ? -view[static_cast<std::size_t>(j)] / view_length : 0.0));
            }
            out.m_Uv[0] = v.m_U0;
            out.m_Uv[1] = v.m_V0;
            out.m_Uv[2] = blended ? static_cast<float>(v.m_Diffuse >> 24) / 255.0f : 1.0f;
            // object = (view - t) x R^-1; without a matrix the view position (drops then move with the camera)
            const double d[3] = { view[0] - model_view[12], view[1] - model_view[13], view[2] - model_view[14] };
            for (int j = 0; j < 3; ++j)
                out.m_Object[j] = static_cast<float>(object_valid
                    ? d[0] * object_of_view[j] + d[1] * object_of_view[3 + j] + d[2] * object_of_view[6 + j]
                    : view[static_cast<std::size_t>(j)]);
        }

        // Paint of a matte part: its triangles instead of those of the batch.
        const std::vector<std::uint16_t> & indices = matte_paint || solid_split ? m_CarReflIndices : m_BatchIndices;

        // Collected with the draws of the same state and drawn at the end of R3DCarLOD::Draw (CarReflDrawPending).
        ID3D11ShaderResourceView * const view = texture != nullptr ? texture->m_View.Get() : nullptr;
        ID3D11SamplerState * const sampler = GetSampler( 0 );
        const std::uint32_t cull = render[rs::CULLMODE] & 3;
        const std::uint32_t fog = render[rs::FOGCOLOR] | 0xFF000000u;
        const float alpha_ref = render[rs::ALPHATESTENABLE] != 0 && texture != nullptr && texture->m_Translucent
            ? static_cast<float>(render[rs::ALPHAREF] & 0xFF) / 255.0f : -1.0f;
        // Blended draws: below this alpha the pixel is glass, reflected only with '[CarReflections] Glass'.
        // B34: -2 marks a glass part, reflected as a whole with the glass settings (CarReflDrawPending).
        const float glass_ref = glass_part ? -2.0f : glass_page ? CAR_REFL_GLASS_PAGE : (blended && texture != nullptr ? 0.95f : -1.0f);
        const float up_f[3] = { static_cast<float>(up[0]), static_cast<float>(up[1]), static_cast<float>(up[2]) };
        m_CarReflOutcome = glass_part ? "reflected as glass" : "reflected";
        m_CarReflGlassRefLast = glass_ref;
        // B36: at the probe, what the shader gets: normal, up of the car, the reflected direction, how much of it points up
        // and where it looks the frame up (|p| > 1 = above/beside the frame, the sky colour there).
        if (m_ProbeOn && m_ProbeX >= 0.0f && m_ProbeHits.size() > m_ProbeFirst)
        {
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::uint16_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
                if (ia >= count || ib >= count || ic >= count)
                    continue;
                const CarReflVertex & a = m_CarReflVertices[ia];
                const CarReflVertex & b = m_CarReflVertices[ib];
                const CarReflVertex & c = m_CarReflVertices[ic];
                const float d = (b.m_Position[1] - c.m_Position[1]) * (a.m_Position[0] - c.m_Position[0]) +
                    (c.m_Position[0] - b.m_Position[0]) * (a.m_Position[1] - c.m_Position[1]);
                if (std::fabs( d ) < 1e-6f)
                    continue;
                const float l1 = ((b.m_Position[1] - c.m_Position[1]) * (m_ProbeX - c.m_Position[0]) +
                    (c.m_Position[0] - b.m_Position[0]) * (m_ProbeY - c.m_Position[1])) / d;
                const float l2 = ((c.m_Position[1] - a.m_Position[1]) * (m_ProbeX - c.m_Position[0]) +
                    (a.m_Position[0] - c.m_Position[0]) * (m_ProbeY - c.m_Position[1])) / d;
                const float l3 = 1.0f - l1 - l2;
                if (l1 < 0.0f || l2 < 0.0f || l3 < 0.0f)
                    continue;
                double n[3], v[3];
                for (int j = 0; j < 3; ++j)
                {
                    n[j] = l1 * a.m_Normal[j] + l2 * b.m_Normal[j] + l3 * c.m_Normal[j];
                    v[j] = l1 * a.m_View[j] + l2 * b.m_View[j] + l3 * c.m_View[j];
                }
                const auto unit = []( double * x )
                {
                    const double l = std::sqrt( x[0] * x[0] + x[1] * x[1] + x[2] * x[2] );
                    if (l > 1e-12)
                        for (int j = 0; j < 3; ++j)
                            x[j] /= l;
                };
                unit( n );
                unit( v );
                if (n[0] * v[0] + n[1] * v[1] + n[2] * v[2] > 0.0)
                    for (double & x : n)
                        x = -x;
                const double nv = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
                double r[3];
                for (int j = 0; j < 3; ++j)
                    r[j] = v[j] - 2.0 * nv * n[j];
                const double upward = r[0] * up_f[0] + r[1] * up_f[1] + r[2] * up_f[2];
                const double normal_up = n[0] * up_f[0] + n[1] * up_f[1] + n[2] * up_f[2];
                const float * m = m_CarReflClipOfView.data();
                double cl[3];
                for (int j = 0; j < 3; ++j)
                    cl[j] = r[0] * m[j] + r[1] * m[4 + j] + r[2] * m[8 + j];
                const float * vp = m_CarReflViewport.data();
                double px = 99.0, py = 99.0;
                if (std::fabs( cl[2] ) > 1e-9)
                {
                    px = (cl[0] / cl[2] * vp[0] + vp[2]) / std::max( vp[2], 1.0f ) - 1.0;
                    py = (cl[1] / cl[2] * vp[1] + vp[3]) / std::max( vp[3], 1.0f ) - 1.0;
                }
                std::snprintf( m_ProbeHits[m_ProbeFirst].m_CarDetail, sizeof( m_ProbeHits[m_ProbeFirst].m_CarDetail ),
                    "normal %+.2f %+.2f %+.2f, up %+.2f %+.2f %+.2f (normal.up %+.2f), facing %.2f, reflected %+.2f %+.2f %+.2f "
                    "(upward %+.2f, behind camera %d), frame lookup %+.2f %+.2f", n[0], n[1], n[2], up_f[0], up_f[1], up_f[2],
                    normal_up, -nv, r[0], r[1], r[2], upward, cl[2] <= 0.0 ? 1 : 0, px, py );
                break;
            }
        }
        CarReflPending * pending = nullptr;
        for (std::size_t k = 0; k < m_CarReflPendingCount && pending == nullptr; ++k)
        {
            CarReflPending & p = m_CarReflPending[k];
            if (p.m_View == view && p.m_Sampler == sampler && p.m_Cull == cull && p.m_Fog == fog && p.m_AlphaRef == alpha_ref &&
                p.m_GlassRef == glass_ref && std::memcmp( p.m_Up, up_f, sizeof( up_f ) ) == 0 && p.m_CarFlow == car_flow)
                pending = &p;
        }
        if (pending == nullptr)
        {
            if (m_CarReflPendingCount == m_CarReflPending.size())
                m_CarReflPending.emplace_back();
            pending = &m_CarReflPending[m_CarReflPendingCount++];
            pending->m_View = view;
            pending->m_Sampler = sampler;
            pending->m_Cull = cull;
            pending->m_Fog = fog;
            pending->m_AlphaRef = alpha_ref;
            pending->m_GlassRef = glass_ref;
            std::memcpy( pending->m_Up, up_f, sizeof( up_f ) );
            pending->m_CarFlow = car_flow;
            pending->m_Vertices.clear();
            pending->m_Indices.clear();
        }
        // 16 bit indices: at most 65536 corners per draw, which the ring holds too.
        if (pending->m_Vertices.size() + count > CAR_REFL_VERTICES || pending->m_Indices.size() + indices.size() > CAR_REFL_INDICES)
        {
            CarReflDraw( *pending );
            pending->m_Vertices.clear();
            pending->m_Indices.clear();
        }
        const std::size_t base = pending->m_Vertices.size();
        pending->m_Vertices.insert( pending->m_Vertices.end(), m_CarReflVertices.begin(), m_CarReflVertices.begin() + static_cast<std::ptrdiff_t>(count) );
        pending->m_Indices.reserve( pending->m_Indices.size() + indices.size() );
        for (const std::uint16_t index : indices)
            pending->m_Indices.push_back( static_cast<std::uint16_t>(index + base) );
        ++m_CarReflPendingParts;

        ++stats.m_Drawn;
        stats.m_Triangles += indices.size() / 3;
    }


    void Driver::CarReflDrawPending()
    {
        if (m_CarReflPendingCount == 0)
            return;
        for (std::size_t k = 0; k < m_CarReflPendingCount; ++k)
        {
            CarReflPending & pending = m_CarReflPending[k];
            CarReflDraw( pending );
            pending.m_Vertices.clear();
            pending.m_Indices.clear();
            pending.m_View = nullptr;
        }
        m_CarReflPendingCount = 0;

        // The game's ring back in place (ApplyPipeline binds it for every draw as well).
        ID3D11ShaderResourceView * none[4] = {};
        m_Context->PSSetShaderResources( 0, 4, none );
        m_Context->IASetInputLayout( m_SceneLayout.Get() );
        UINT stride = sizeof( TlVertex );
        UINT offset = 0;
        ID3D11Buffer * vertex_buffer = m_VertexBuffer.Get();
        m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
        m_Context->IASetIndexBuffer( m_IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
        InvalidatePipeline();
    }


    // '[CarReflections] Sky' (9.15a): the sky image of the water reflection is drawn at the end of the main view, after the
    // cars; the cars take the one of the previous frame (the sky barely moves in a frame). Main view only: the rear view
    // mirror looks the other way. Without water on screen there is no sky image, the fog colour stays.
    bool Driver::CarSkyImageUsable() const
    {
        return m_ConfigCarReflSky != 0 && m_ReflSkyView && m_ReflViewIsMain && m_SkyImageFlip != 0xFFFFFFFFu &&
            m_FlipCount - m_SkyImageFlip <= 2;
    }


    // B36: the sky image exists only while water is on screen. With it the paint shows sky and bright highlights
    // ('HighlightBoost'), without it the plain fog colour: the red 996 turned matt from one frame to the next when a little
    // water at the edge of the screen went out of view. The last image stays for 'SkyHold' seconds, then it fades out over
    // 0.75 s (and in again the same way).
    float Driver::CarSkyImageBlend()
    {
        if (m_ConfigCarReflSky == 0 || !m_ReflSkyView || !m_ReflViewIsMain || m_SkyImageFlip == 0xFFFFFFFFu)
            return 0.0f;
        if (m_CarSkyBlendFlip != m_FlipCount)
        {
            const double now = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
            const double dt = m_CarSkyBlendFlip == 0xFFFFFFFFu ? 0.0 : std::clamp( now - m_CarSkyBlendTime, 0.0, 0.25 );
            m_CarSkyBlendFlip = m_FlipCount;
            m_CarSkyBlendTime = now;
            const bool fresh = CarSkyImageUsable();
            if (fresh)
                m_CarSkyLastFresh = now;
            if (m_ConfigCarReflSkyHold == 0.0f)
                m_CarSkyBlend = fresh ? 1.0f : 0.0f;
            else
            {
                const float target = fresh || m_ConfigCarReflSkyHold < 0.0f || now - m_CarSkyLastFresh <= m_ConfigCarReflSkyHold ? 1.0f : 0.0f;
                const float step = static_cast<float>(dt / 0.75);
                m_CarSkyBlend = target > m_CarSkyBlend ? std::min( target, m_CarSkyBlend + step ) : std::max( target, m_CarSkyBlend - step );
            }
            if ((fresh ? 1 : 0) != m_CarSkyFreshLogged && m_CarSkyLogLines < 200)
            {
                ++m_CarSkyLogLines;
                m_CarSkyFreshLogged = fresh ? 1 : 0;
                Log( "Car reflections: sky image %s (flip %u, share %.2f, B36)", fresh ? "back" : "gone, held", m_FlipCount, m_CarSkyBlend );
            }
        }
        return m_CarSkyBlend;
    }


    // B36: the car reflection looked the sky up at the top row of the sky image of this camera; a small change of the camera
    // moved that row through the whole height of the sky texture. The game keeps the horizon band as a ring of 16 segments
    // (SkyHorizon, last level of Sky_Current): it is drawn once into a panorama and looked up by the direction in the world.
    void Driver::CarSkyNoteBatch()
    {
        if (m_ConfigCarRefl <= 0 || m_ConfigCarReflSky == 0 || m_ConfigCarReflSkyPanorama == 0 || !BatchIsSky())
            return;

        // The flat plane behind the band, the colour of the sky above it.
        if (m_Bound[0] == nullptr)
        {
            if (m_D3D.m_Render[rs::ALPHABLENDENABLE] == 0)
            {
                m_CarSkyFlatColour = m_BatchVertices.front().m_Diffuse | 0xFF000000u;
                m_CarSkyFlatFlip = m_FlipCount;
            }
            return;
        }

        std::uintptr_t sky = 0;
        std::int32_t levels = 0;
        std::uintptr_t horizon = 0;
        if (!SafeCopy( reinterpret_cast<const void *>(0x00627E48), &sky, 4 ) || sky == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(sky + 0x28), &levels, 4 ) || levels < 1 || levels > 10 ||
            !SafeCopy( reinterpret_cast<const void *>(sky + static_cast<std::uintptr_t>(levels - 1) * 4), &horizon, 4 ) || horizon == 0)
            return;

        if (horizon != m_CarSkyHorizon)
        {
            std::uint32_t vtable = 0;
            std::int32_t count = 0;
            unsigned char ready = 0;
            std::uint32_t positions = 0, uvs = 0, colours = 0;
            if (!SafeCopy( reinterpret_cast<const void *>(horizon), &vtable, 4 ) || vtable != 0x005B3E54u ||
                !SafeCopy( reinterpret_cast<const void *>(horizon + 0x28), &count, 4 ) || count != 0x80 ||
                !SafeCopy( reinterpret_cast<const void *>(horizon + 0x54), &ready, 1 ) || ready == 0 ||
                !SafeCopy( reinterpret_cast<const void *>(horizon + 0x2C), &positions, 4 ) || positions == 0 ||
                !SafeCopy( reinterpret_cast<const void *>(horizon + 0x34), &uvs, 4 ) || uvs == 0 ||
                !SafeCopy( reinterpret_cast<const void *>(horizon + 0x3C), &colours, 4 ) || colours == 0 ||
                !SafeCopy( reinterpret_cast<const void *>(static_cast<std::uintptr_t>(positions)), m_CarSkyBandPositions.data(), sizeof( m_CarSkyBandPositions ) ) ||
                !SafeCopy( reinterpret_cast<const void *>(static_cast<std::uintptr_t>(uvs)), m_CarSkyBandUv.data(), sizeof( m_CarSkyBandUv ) ) ||
                !SafeCopy( reinterpret_cast<const void *>(static_cast<std::uintptr_t>(colours)), m_CarSkyBandColours.data(), sizeof( m_CarSkyBandColours ) ))
                return;
            // A new sky. The flat plane comes before the band: its colour of this frame is the new one, an older one is not.
            if (m_CarSkyFlatFlip != m_FlipCount)
                m_CarSkyFlatFlip = ~0ull;
            m_CarSkyHorizon = horizon;
            m_CarSkyBandView.Reset();
            m_CarSkyBandHash = 0;
            m_CarSkyPanoValid = false;
        }
        if (m_CarSkyBandView)
            return;

        // The texture of the band: the draw whose corners carry the texture coordinates of the ring.
        std::size_t matches = 0;
        for (const TlVertex & vertex : m_BatchVertices)
            for (const auto & uv : m_CarSkyBandUv)
                if (std::abs( vertex.m_U0 - uv[0] ) < 1e-3f && std::abs( vertex.m_V0 - uv[1] ) < 1e-3f)
                {
                    ++matches;
                    break;
                }
        if (matches >= 4 && matches * 2 >= m_BatchVertices.size() && m_Bound[0]->m_View)
        {
            m_CarSkyBandView = m_Bound[0]->m_View;
            m_CarSkyBandHash = static_cast<std::uint32_t>(m_Bound[0]->m_Hash);
        }
    }


    bool Driver::CarSkyPanoramaUsable() const
    {
        return m_ConfigCarReflSky != 0 && m_ConfigCarReflSkyPanorama != 0 && m_CarSkyPanoValid && m_CarSkyRowsValid && m_CarSkyPanoView &&
            *reinterpret_cast<const std::int32_t *>(0x0065B928) == 0;
    }


    void Driver::CarSkyBuildPanorama()
    {
        if (*reinterpret_cast<const std::int32_t *>(0x0065B928) != 0 || !m_CarSkyBandView || !m_CarSkyPanoVs || !m_CarSkyPanoPs || !m_CarSkyPanoLayout)
            return;

        // Above the band: the flat colour of the sky, without one the clear colour of the game. The flat plane is not drawn in
        // every view (the game culls it), so its colour is kept: falling back for a frame made the whole sky jump.
        const std::uint32_t zenith = (m_CarSkyFlatFlip != ~0ull ? m_CarSkyFlatColour : m_BackgroundColor) | 0xFF000000u;
        if (m_CarSkyPanoValid && m_CarSkyBuiltHorizon == m_CarSkyHorizon && m_CarSkyBuiltHash == m_CarSkyBandHash && m_CarSkyBuiltZenith == zenith)
            return;

        if (!m_CarSkyPano)
        {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = 512;
            desc.Height = 256;
            desc.MipLevels = 0;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, m_CarSkyPano.Put() ) ) ||
                FAILED( m_Device->CreateRenderTargetView( m_CarSkyPano.Get(), nullptr, m_CarSkyPanoRtv.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_CarSkyPano.Get(), nullptr, m_CarSkyPanoView.Put() ) ))
            {
                m_CarSkyPano.Reset();
                m_CarSkyPanoRtv.Reset();
                m_CarSkyPanoView.Reset();
                return;
            }
            D3D11_BLEND_DESC blend = {};
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            m_Device->CreateBlendState( &blend, m_CarSkyPanoBlend.Put() );
            D3D11_DEPTH_STENCIL_DESC depth = {};
            depth.DepthEnable = FALSE;
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            m_Device->CreateDepthStencilState( &depth, m_CarSkyPanoDepth.Put() );
            D3D11_RASTERIZER_DESC raster = {};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = FALSE;
            m_Device->CreateRasterizerState( &raster, m_CarSkyPanoRaster.Put() );
            D3D11_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            m_Device->CreateSamplerState( &sampler, m_CarSkyPanoSampler.Put() );
        }
        if (!m_CarSkyPanoBlend || !m_CarSkyPanoDepth || !m_CarSkyPanoRaster || !m_CarSkyPanoSampler)
            return;

        struct PanoVertex
        {
            float m_X, m_Y, m_U, m_V;
            std::uint32_t m_Colour;
        };
        std::vector<PanoVertex> vertices;
        vertices.reserve( 16 * 3 * 6 * 2 );
        const auto height = []( float y ) { return 2.0f * (y + 0.35f) / 1.35f - 1.0f; }; // direction height -> clip y
        const auto around = []( const std::array<float, 4> & p )
        {
            return static_cast<float>(std::atan2( static_cast<double>(p[0]), static_cast<double>(p[2]) ) / 6.283185307179586 + 0.5);
        };
        const auto rise = []( const std::array<float, 4> & p )
        {
            const double flat = std::sqrt( static_cast<double>(p[0]) * p[0] + static_cast<double>(p[2]) * p[2] );
            return static_cast<float>(p[1] / std::sqrt( flat * flat + static_cast<double>(p[1]) * p[1] ));
        };
        float band_top = -1.0f, band_bottom = 1.0f;
        for (std::size_t segment = 0; segment < 16; ++segment)
        {
            const std::size_t base = segment * 4;
            const auto & top_a = m_CarSkyBandPositions[base];
            const auto & top_b = m_CarSkyBandPositions[base + 1];
            const auto & low_a = m_CarSkyBandPositions[base + 3];
            float a0 = around( top_a );
            float a1 = around( top_b );
            if (a1 - a0 > 0.5f)
                a1 -= 1.0f;
            else if (a0 - a1 > 0.5f)
                a1 += 1.0f;
            const float y_top = rise( top_a );
            const float y_low = rise( low_a );
            band_top = std::max( band_top, y_top );
            band_bottom = std::min( band_bottom, y_low );
            const auto & uv_ta = m_CarSkyBandUv[base];
            const auto & uv_tb = m_CarSkyBandUv[base + 1];
            const auto & uv_lb = m_CarSkyBandUv[base + 2];
            const auto & uv_la = m_CarSkyBandUv[base + 3];
            const std::uint32_t c_ta = m_CarSkyBandColours[base], c_tb = m_CarSkyBandColours[base + 1];
            const std::uint32_t c_lb = m_CarSkyBandColours[base + 2], c_la = m_CarSkyBandColours[base + 3];
            for (const float shift : { 0.0f, 1.0f, -1.0f })
            {
                const float x0 = 2.0f * (a0 + shift) - 1.0f;
                const float x1 = 2.0f * (a1 + shift) - 1.0f;
                if (std::max( x0, x1 ) <= -1.0f || std::min( x0, x1 ) >= 1.0f)
                    continue;
                const auto quad = [&]( const PanoVertex & a, const PanoVertex & b, const PanoVertex & c, const PanoVertex & d )
                {
                    vertices.push_back( a );
                    vertices.push_back( b );
                    vertices.push_back( c );
                    vertices.push_back( a );
                    vertices.push_back( c );
                    vertices.push_back( d );
                };
                // The band.
                quad( { x0, height( y_top ), uv_ta[0], uv_ta[1], c_ta }, { x1, height( y_top ), uv_tb[0], uv_tb[1], c_tb },
                    { x1, height( y_low ), uv_lb[0], uv_lb[1], c_lb }, { x0, height( y_low ), uv_la[0], uv_la[1], c_la } );
                // Above it the top row fades into the flat colour, no edge.
                const float y_fade = std::min( y_top + 0.14f, 1.0f );
                quad( { x0, height( y_fade ), uv_ta[0], uv_ta[1], c_ta & 0x00FFFFFFu }, { x1, height( y_fade ), uv_tb[0], uv_tb[1], c_tb & 0x00FFFFFFu },
                    { x1, height( y_top ), uv_tb[0], uv_tb[1], c_tb }, { x0, height( y_top ), uv_ta[0], uv_ta[1], c_ta } );
                // Below it the bottom row down to the end of the panorama.
                quad( { x0, height( y_low ), uv_la[0], uv_la[1], c_la }, { x1, height( y_low ), uv_lb[0], uv_lb[1], c_lb },
                    { x1, -1.0f, uv_lb[0], uv_lb[1], c_lb }, { x0, -1.0f, uv_la[0], uv_la[1], c_la } );
            }
        }

        D3D11_BUFFER_DESC buffer_desc = {};
        buffer_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof( PanoVertex ));
        buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = vertices.data();
        if (vertices.empty() || FAILED( m_Device->CreateBuffer( &buffer_desc, &data, m_CarSkyPanoVertices.Put() ) ))
            return;

        ID3D11ShaderResourceView * none[4] = {};
        m_Context->PSSetShaderResources( 0, 4, none );
        ID3D11RenderTargetView * target = m_CarSkyPanoRtv.Get();
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        float clear[4];
        ColorToFloat4( zenith, clear );
        clear[3] = 1.0f;
        m_Context->ClearRenderTargetView( target, clear );
        const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, 512.0f, 256.0f, 0.0f, 1.0f };
        m_Context->RSSetViewports( 1, &viewport );
        m_Context->RSSetState( m_CarSkyPanoRaster.Get() );
        const float factor[4] = {};
        m_Context->OMSetBlendState( m_CarSkyPanoBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_CarSkyPanoDepth.Get(), 0 );
        m_Context->IASetInputLayout( m_CarSkyPanoLayout.Get() );
        UINT stride = sizeof( PanoVertex );
        UINT offset = 0;
        ID3D11Buffer * vertex_buffer = m_CarSkyPanoVertices.Get();
        m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_CarSkyPanoVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_CarSkyPanoPs.Get(), nullptr, 0 );
        ID3D11ShaderResourceView * band = m_CarSkyBandView.Get();
        ID3D11SamplerState * band_sampler = m_PresentSampler.Get();
        m_Context->PSSetShaderResources( 0, 1, &band );
        m_Context->PSSetSamplers( 0, 1, &band_sampler );
        m_Context->Draw( static_cast<UINT>(vertices.size()), 0 );
        m_Context->PSSetShaderResources( 0, 1, none );
        ID3D11RenderTargetView * no_target = nullptr;
        m_Context->OMSetRenderTargets( 1, &no_target, nullptr );
        m_Context->GenerateMips( m_CarSkyPanoView.Get() );

        m_CarSkyBuiltHorizon = m_CarSkyHorizon;
        m_CarSkyBuiltHash = m_CarSkyBandHash;
        m_CarSkyBuiltZenith = zenith;
        m_CarSkyPanoValid = true;
        Log( "Car reflections: sky panorama built (B36): band texture %08X, height of the band %.3f to %.3f, colour above it %08X",
            m_CarSkyBandHash, band_bottom, band_top, zenith );
    }


    void Driver::CarReflDraw(
        CarReflPending & pending
    )
    {
        const std::size_t count = pending.m_Vertices.size();
        const std::size_t index_count = pending.m_Indices.size();
        if (count == 0 || index_count == 0)
            return;

        // Vertices and indices into the own ring, as UploadRing: appended without overwrite, both anew together when full.
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const bool wrap = m_CarReflVertexCursor + count > CAR_REFL_VERTICES || m_CarReflIndexCursor + index_count > CAR_REFL_INDICES;
        if (wrap)
        {
            m_CarReflVertexCursor = 0;
            m_CarReflIndexCursor = 0;
        }
        const D3D11_MAP ring_map = wrap ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
        if (FAILED( m_Context->Map( m_CarReflVertexBuffer.Get(), 0, ring_map, 0, &mapped ) ))
        {
            m_CarReflVertexCursor = CAR_REFL_VERTICES;
            m_CarReflIndexCursor = CAR_REFL_INDICES;
            return;
        }
        std::memcpy( static_cast<CarReflVertex *>(mapped.pData) + m_CarReflVertexCursor, pending.m_Vertices.data(), count * sizeof( CarReflVertex ) );
        m_Context->Unmap( m_CarReflVertexBuffer.Get(), 0 );
        if (FAILED( m_Context->Map( m_CarReflIndexBuffer.Get(), 0, ring_map, 0, &mapped ) ))
        {
            m_CarReflVertexCursor = CAR_REFL_VERTICES;
            m_CarReflIndexCursor = CAR_REFL_INDICES;
            return;
        }
        std::memcpy( static_cast<std::uint16_t *>(mapped.pData) + m_CarReflIndexCursor, pending.m_Indices.data(), index_count * sizeof( std::uint16_t ) );
        m_Context->Unmap( m_CarReflIndexBuffer.Get(), 0 );
        const UINT base_vertex = static_cast<UINT>(m_CarReflVertexCursor);
        const UINT first_index = static_cast<UINT>(m_CarReflIndexCursor);
        m_CarReflVertexCursor += count;
        m_CarReflIndexCursor += index_count;

        const float * viewport = m_CarReflViewport.data();
        const float * view_of_clip = m_CarReflViewOfClip.data();
        // B36: the sky by direction when the panorama is there, else the sky image of the water reflection.
        const bool sky_panorama = CarSkyPanoramaUsable();
        const float sky_blend = sky_panorama ? 1.0f : CarSkyImageBlend();
        const bool sky_image = sky_blend > 0.0f;

        // Layout of 'CarReflectConstants' in shaders::CAR_REFLECT.
        if (SUCCEEDED( m_Context->Map( m_CarReflConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
        {
            float constants[108] = {};
            // B36: view -> world for the sky by direction (x right + y up - z forward).
            if (sky_panorama)
            {
                for (int j = 0; j < 3; ++j)
                {
                    constants[96 + j * 4] = m_CarSkyRows[j];
                    constants[97 + j * 4] = m_CarSkyRows[3 + j];
                    constants[98 + j * 4] = -m_CarSkyRows[6 + j];
                }
                constants[99] = 1.0f;
            }
            // 9.15b: highlights and clearcoat (g_Highlight, g_Coat).
            constants[88] = m_ConfigCarReflHighlightBoost;
            constants[89] = m_ConfigCarReflHighlightThreshold;
            constants[92] = m_ConfigCarReflClearCoat != 0 ? 1.0f : 0.0f;
            constants[93] = m_ConfigCarReflBaseCoat;
            constants[94] = m_ConfigCarReflBaseBlur;
            constants[95] = m_ConfigCarReflBaseTint;
            constants[2] = 1.0f / static_cast<float>(std::max( m_Width, 1 ));
            constants[3] = 1.0f / static_cast<float>(std::max( m_Height, 1 ));
            std::memcpy( constants + 4, viewport, sizeof( float ) * 4 );
            std::memcpy( constants + 8, view_of_clip, sizeof( float ) * 16 );
            std::memcpy( constants + 24, m_CarReflClipOfView.data(), sizeof( float ) * 16 );
            constants[40] = m_ConfigCarReflStrength;
            constants[41] = m_ConfigCarReflF0;
            constants[42] = m_ConfigCarReflBlur;
            constants[43] = static_cast<float>(m_ConfigCarReflDebug);
            // Direction of the camera: the view direction with the largest wc, the w column of the projection.
            const float * clip_of_view = m_CarReflClipOfView.data();
            const double forward[3] = { clip_of_view[2], clip_of_view[6], clip_of_view[10] };
            const double forward_length = std::sqrt( forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2] );
            for (int j = 0; j < 3; ++j)
                constants[44 + j] = forward_length > 0.0 ? static_cast<float>(forward[j] / forward_length) : (j == 2 ? 1.0f : 0.0f);
            constants[47] = pending.m_AlphaRef;
            constants[48] = pending.m_GlassRef;
            constants[49] = 0.0f; // was the old 'Glass' key (all translucent pixels as paint), replaced by 'GlassParts'
            const bool glass_part = pending.m_GlassRef < -1.5f;
            for (int j = 0; j < 3; ++j)
                constants[52 + j] = pending.m_Up[j];
            constants[55] = m_ConfigCarReflGround;
            // The fog colour of the world stands in for the sky.
            float fog[4] = {};
            ColorToFloat4( pending.m_Fog, fog );
            constants[56] = fog[0];
            constants[57] = fog[1];
            constants[58] = fog[2];
            constants[59] = sky_image ? 1.0f + sky_blend : 1.0f; // above 1: share of the sky image at t3 (9.15a, B36)
            constants[60] = m_ConfigCarReflFresnel;
            constants[61] = m_CarReflLevelOffset;
            const bool chain = m_ConfigCarReflBlurChain > 0 && m_BlurChainValid && m_BlurChainView;
            constants[62] = chain ? 1.0f : 0.0f;
            constants[63] = chain ? static_cast<float>(m_BlurDownLevels - 2) : 0.0f;
            // Weather on the car (PLAN.md 15.3): snow, its light, drops, time; drops per texture unit, snow colour.
            WeatherFxFrame();
            if (m_CarWeatherOn)
            {
                constants[64] = m_CarWeather[0];
                constants[65] = m_CarWeatherLight;
                constants[66] = m_CarWeather[1];
                constants[67] = m_CarWeather[2];
                constants[68] = m_ConfigCarDropCells;
                constants[69] = m_WeatherFx[11] > 0.0f ? m_WeatherFx[11] : 0.94f;
                constants[70] = m_WeatherFx[12] > 0.0f ? m_WeatherFx[12] : 0.96f;
                constants[71] = m_WeatherFx[13] > 0.0f ? m_WeatherFx[13] : 1.0f;
                constants[72] = m_CarUpVote >= 0.0f ? 1.0f : -1.0f; // sign of "up" for the snow
                constants[73] = m_CarWeather[3] > 2.5f ? m_CarWeather[3] : 0.0f; // '[Weather] SnowDebug' 3 up-ness, 4 drops
                constants[74] = m_ConfigCarDropShine;
                constants[75] = m_ConfigCarSnowSoft;
                constants[83] = m_ConfigCarSnowBright;
                // w5: flow of the drops of this car: crept distance (space of the part), speed, direction of travel in the
                // space of the part and in view space.
                if (pending.m_CarFlow >= 0 && pending.m_CarFlow < static_cast<int>(std::size( m_CarFlowTracks )))
                {
                    const CarFlowTrack & track = m_CarFlowTracks[pending.m_CarFlow];
                    for (int j = 0; j < 3; ++j)
                    {
                        constants[76 + j] = track.m_Flow[j];
                        constants[80 + j] = track.m_DirObject[j];
                        constants[84 + j] = track.m_DirView[j];
                    }
                    constants[79] = track.m_Speed;
                }
                ++m_WeatherStats[m_CarWeather[0] > 0.0f ? 4 : 5];
            }
            // B34: the glass texture on a body part: glass look. All other blended textures keep the cut (handle and lamp rims).
            if (pending.m_GlassRef == CAR_REFL_GLASS_PAGE)
            {
                constants[50] = m_ConfigCarReflGlassStrength;
                constants[51] = std::clamp( m_ConfigCarReflGlassF0 / m_ConfigCarReflGlassStrength, 0.0f, 1.0f );
            }
            if (glass_part || pending.m_GlassRef == CAR_REFL_GLASS_PAGE)
                constants[90] = m_ConfigCarReflGlassSkyBoost;
            // B34: glass parts: glass look, no clip by alpha, no clearcoat, Fresnel exponent 3; highlights as on the paint
            // (bright sky on the glass).
            if (glass_part)
            {
                // Cover = strength x (F0 + (1 - F0) x (1 - facing)^3): 'GlassReflectance' head on, 'GlassStrength' at a flat
                // angle (was strength x Schlick: 2 to 3 % at ordinary angles, not to be seen).
                const float glass_strength = std::max( m_ConfigCarReflGlassStrength, 1e-4f );
                constants[40] = glass_strength;
                constants[41] = std::clamp( m_ConfigCarReflGlassF0 / glass_strength, 0.0f, 1.0f );
                constants[48] = -1.0f;
                constants[60] = 3.0f;
                constants[92] = 0.0f;
            }
            std::memcpy( mapped.pData, constants, sizeof( constants ) );
            m_Context->Unmap( m_CarReflConstants.Get(), 0 );
        }

        if (!m_CarReflBlend)
        {
            // Over the paint by the strength of the reflection, the alpha of the frame stays.
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
            m_Device->CreateBlendState( &desc, m_CarReflBlend.Put() );
        }
        if (!m_CarReflDepth)
        {
            // Same corners, same depth: only where the part itself is in front.
            D3D11_DEPTH_STENCIL_DESC desc = {};
            desc.DepthEnable = TRUE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            m_Device->CreateDepthStencilState( &desc, m_CarReflDepth.Put() );
        }

        // Culled as the game culls the part (keys 0x300 + mode, apart from those of ApplyPipeline).
        const std::uint32_t cull = pending.m_Cull;
        auto & raster = m_Rasters[0x300u + cull];
        if (!raster)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = cull == 2 ? D3D11_CULL_FRONT : (cull == 3 ? D3D11_CULL_BACK : D3D11_CULL_NONE);
            desc.FrontCounterClockwise = FALSE;
            desc.DepthClipEnable = TRUE;
            desc.ScissorEnable = TRUE;
            m_Device->CreateRasterizerState( &desc, raster.Put() );
        }

        m_Context->IASetInputLayout( m_CarReflLayout.Get() );
        UINT stride = sizeof( CarReflVertex );
        UINT offset = 0;
        ID3D11Buffer * vertex_buffer = m_CarReflVertexBuffer.Get();
        m_Context->IASetVertexBuffers( 0, 1, &vertex_buffer, &stride, &offset );
        m_Context->IASetIndexBuffer( m_CarReflIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0 );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_CarReflVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( m_CarReflPs.Get(), nullptr, 0 );
        ID3D11Buffer * constant_buffer = m_CarReflConstants.Get();
        m_Context->VSSetConstantBuffers( 0, 1, &constant_buffer );
        m_Context->PSSetConstantBuffers( 0, 1, &constant_buffer );
        ID3D11ShaderResourceView * views[4] = { m_CarReflSourceView.Get(), pending.m_View,
            m_ConfigCarReflBlurChain > 0 && m_BlurChainValid ? m_BlurChainView.Get() : nullptr,
            sky_panorama ? m_CarSkyPanoView.Get() : (sky_image ? m_ReflSkyView.Get() : nullptr) };
        ID3D11SamplerState * samplers[3] = { m_PresentSampler.Get(), pending.m_Sampler, m_CarSkyPanoSampler.Get() };
        m_Context->PSSetShaderResources( 0, 4, views );
        m_Context->PSSetSamplers( 0, 3, samplers );
        const float factor[4] = {};
        m_Context->OMSetBlendState( m_CarReflBlend.Get(), factor, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( m_CarReflDepth.Get(), 0 );
        m_Context->RSSetState( raster.Get() );
        m_Context->DrawIndexed( static_cast<UINT>(index_count), first_index, static_cast<INT>(base_vertex) );
        ++m_CarReflPendingDraws;
    }


    const std::vector<std::uint32_t> & Driver::CarReflCellColours(
        const Texture * texture
    )
    {
        auto [found, added] = m_CarReflCellColours.try_emplace( texture != nullptr ? texture->m_Hash : 0 );
        std::vector<std::uint32_t> & colours = found->second;
        if (!added || texture == nullptr || !texture->m_Texture || formats::IsBlockCompressed( texture->m_Format ))
            return colours;

        // Top level through a staging copy, once per texture (as MeasureDump); uploads are B8G8R8A8.
        D3D11_TEXTURE2D_DESC desc = {};
        texture->m_Texture->GetDesc( &desc );
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.Width < 64 || desc.Height < 64)
            return colours;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED( m_Device->CreateTexture2D( &desc, nullptr, staging.Put() ) ))
            return colours;
        m_Context->CopySubresourceRegion( staging.Get(), 0, 0, 0, 0, texture->m_Texture.Get(), 0, nullptr );
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( staging.Get(), 0, D3D11_MAP_READ, 0, &mapped ) ))
            return colours;

        // Mean of the texels with alpha of at least one half, 0xFFFFFFFF for a cell without any.
        colours.assign( 64 * 64, 0xFFFFFFFFu );
        const std::uint32_t cell_width = desc.Width / 64;
        const std::uint32_t cell_height = desc.Height / 64;
        for (std::uint32_t cy = 0; cy < 64; ++cy)
            for (std::uint32_t cx = 0; cx < 64; ++cx)
            {
                std::uint32_t sums[4] = {};
                for (std::uint32_t y = cy * cell_height; y < (cy + 1) * cell_height; ++y)
                {
                    const auto * row = static_cast<const std::uint8_t *>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
                    for (std::uint32_t x = cx * cell_width; x < (cx + 1) * cell_width; ++x)
                    {
                        const std::uint8_t * bgra = row + static_cast<std::size_t>(x) * 4;
                        if (bgra[3] < 128)
                            continue;
                        ++sums[0];
                        sums[1] += bgra[2];
                        sums[2] += bgra[1];
                        sums[3] += bgra[0];
                    }
                }
                if (sums[0] != 0)
                    colours[static_cast<std::size_t>(cy) * 64 + cx] = (sums[1] / sums[0]) << 16 | (sums[2] / sums[0]) << 8 | sums[3] / sums[0];
            }
        m_Context->Unmap( staging.Get(), 0 );

        // Damage paints into the page and changes its hash, so the page is read again, now with scratched cloth that looks
        // like the paint (B28). A page that mostly matches a baseline of the same size (apart from the damaged cells)
        // takes the baseline's colours: the undamaged state, the first one seen.
        const auto near_colour = []( std::uint32_t a, std::uint32_t b )
        {
            int distance = 0;
            for (int shift = 0; shift < 24; shift += 8)
            {
                const int d = static_cast<int>(a >> shift & 0xFF) - static_cast<int>(b >> shift & 0xFF);
                distance += d * d;
            }
            return distance < 40 * 40;
        };
        bool inherited = false;
        for (const auto & baseline : m_CarReflBaselines)
        {
            if (baseline.m_Width != desc.Width || baseline.m_Height != desc.Height)
                continue;
            // The same layout (cells with and without texels, 95 %) and the same colours in 85 % of them: another model or
            // another paint of the same size must not take over (its cloth would read as paint).
            std::size_t valid = 0, either = 0, same = 0;
            for (std::size_t i = 0; i < 64 * 64; ++i)
            {
                const bool a = baseline.m_Colours[i] != 0xFFFFFFFFu, b = colours[i] != 0xFFFFFFFFu;
                either += a || b ? 1 : 0;
                if (a && b)
                {
                    ++valid;
                    same += near_colour( baseline.m_Colours[i], colours[i] ) ? 1 : 0;
                }
            }
            if (valid >= 64 && valid * 100 >= either * 95 && same * 100 >= valid * 85)
            {
                colours = baseline.m_Colours;
                inherited = true;
                break;
            }
        }
        if (!inherited)
        {
            if (m_CarReflBaselines.size() >= 32)
                m_CarReflBaselines.erase( m_CarReflBaselines.begin() );
            m_CarReflBaselines.push_back( { desc.Width, desc.Height, colours } );
        }
        if (m_CarReflLines < 300)
        {
            ++m_CarReflLines;
            Log( "Car reflections: colours of paint texture %08X read (%ux%u)%s", texture->m_Hash, desc.Width, desc.Height,
                inherited ? ", undamaged colours kept" : "" );
        }
        return colours;
    }


    void Driver::CarReflReport()
    {
        const ULONGLONG now = ::GetTickCount64();
        if (now < m_CarReflNextReport)
            return;
        const bool first = m_CarReflNextReport == 0;
        m_CarReflNextReport = now + 10000;
        if (first || m_CarReflLodCalls == 0)
            return;

        Log( "Car reflections report: %llu frames with car parts, %llu part draws, %llu batches without camera, frame copy %s",
            static_cast<unsigned long long>(m_CarReflFrames), static_cast<unsigned long long>(m_CarReflLodCalls),
            static_cast<unsigned long long>(m_CarReflNoCamera), m_CarReflSourceValid ? "ok" : "missing" );
        Log( "  collected: %llu reflected batches in %llu draws; corner groups kept %llu, built anew %llu (%zu parts kept)",
            static_cast<unsigned long long>(m_CarReflPendingParts), static_cast<unsigned long long>(m_CarReflPendingDraws),
            static_cast<unsigned long long>(m_CarReflWeldHits), static_cast<unsigned long long>(m_CarReflWeldMisses), m_CarReflWeldCache.size() );
        if (m_ConfigCarReflObjectNormals != 0)
            Log( "  object normals (9.15e): %llu batches from the part cache, %llu built; oriented by culling %llu, by the centre %llu, "
                "mixed winding %llu (turned per face); drawn faces against the sign %llu of %llu; seam corners %llu",
                static_cast<unsigned long long>(m_CarReflObjCached), static_cast<unsigned long long>(m_CarReflObjBuilt),
                static_cast<unsigned long long>(m_CarReflOrientCull), static_cast<unsigned long long>(m_CarReflOrientCentre),
                static_cast<unsigned long long>(m_CarReflOrientMixed), static_cast<unsigned long long>(m_CarReflOrientAgainst),
                static_cast<unsigned long long>(m_CarReflOrientVisible), static_cast<unsigned long long>(m_CarReflSeamCorners) );
        Log( "  corner groups built anew because: no entry %llu, test corners failed %llu; old entries dropped %llu, cache emptied %llu times",
            static_cast<unsigned long long>(m_CarReflWeldNew), static_cast<unsigned long long>(m_CarReflWeldFailed),
            static_cast<unsigned long long>(m_CarReflWeldEvicted), static_cast<unsigned long long>(m_CarReflWeldClears) );
        m_CarReflWeldNew = m_CarReflWeldFailed = m_CarReflWeldClears = m_CarReflWeldEvicted = 0;
        m_CarReflObjCached = m_CarReflObjBuilt = 0;
        m_CarReflOrientCull = m_CarReflOrientCentre = m_CarReflOrientMixed = 0;
        m_CarReflOrientVisible = m_CarReflOrientAgainst = m_CarReflSeamCorners = 0;
        m_CarReflPendingParts = 0;
        m_CarReflPendingDraws = 0;
        m_CarReflWeldHits = 0;
        m_CarReflWeldMisses = 0;
        for (auto & [mask, stats] : m_CarReflStats)
        {
            Log( "  pass %02X: %llu reflected (%llu triangles), %llu blended left out, %llu skipped by texture, %llu matte parts, "
                "%llu matte triangles of paint textures", mask,
                static_cast<unsigned long long>(stats.m_Drawn), static_cast<unsigned long long>(stats.m_Triangles),
                static_cast<unsigned long long>(stats.m_Blended), static_cast<unsigned long long>(stats.m_Skipped),
                static_cast<unsigned long long>(stats.m_Matte), static_cast<unsigned long long>(stats.m_MatteTriangles) );
            stats = {};
        }
        m_CarReflFrames = 0;
        m_CarReflLodCalls = 0;
        m_CarReflNoCamera = 0;
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


    // The game flags the race as night (0x628A28) and has the night track layers that light the meshes. LFSP sets the flag
    // as well for "Night" on a track that is a day track in the game; it has no such layers, so the day path must light it.
    static bool NightNative()
    {
        using SyntheticFn = int( __cdecl * )();
        static SyntheticFn synthetic_fn = nullptr;
        static bool resolved = false;
        if (!resolved)
        {
            resolved = true;
            if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                synthetic_fn = reinterpret_cast<SyntheticFn>(::GetProcAddress( lfsp, "LfspNightSynthetic" ));
        }
        return *reinterpret_cast<const std::uint16_t *>(0x00628A28) != 0 && (synthetic_fn == nullptr || synthetic_fn() == 0);
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
        const bool night = NightNative();
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

        // Light position: the camera (the light map reaches about 100 units, doc/LICHT.md 8.4); without a camera the player.
        float eye[3] = {};
        {
            std::uintptr_t context = 0;
            std::uintptr_t view = 0;
            if (SafeCopy( reinterpret_cast<const void *>(0x00628130), &context, 4 ) && context != 0 &&
                SafeCopy( reinterpret_cast<const void *>(context + 0x10), &view, 4 ) && view != 0)
                SafeCopy( reinterpret_cast<const void *>(view + 0x10C), eye, sizeof( eye ) );
            if (eye[0] == 0.0f && eye[1] == 0.0f && eye[2] == 0.0f && player_car != 0)
                SafeCopy( reinterpret_cast<const void *>(player_car + 0x330), eye, sizeof( eye ) );
        }
        // '[Lights] CarLightRadius' (B33): was 100, cars further out never lit the road.
        const float reach2 = m_ConfigCarLightRadius * m_ConfigCarLightRadius;
        const std::size_t limit = static_cast<std::size_t>(std::clamp( m_ConfigMaxCarLights, 1, MAX_CAR_LIGHTS ));

        // Opponents and police first, then the traffic cars (flag 0x10 at car + 0x52C, doc/VERKEHR.md 1), each group nearest
        // to the camera first and only within reach, as many as the limit allows: traffic never pushes out an opponent.
        // Traffic only glows weakly ('TrafficHeadlightGlow') and only in a night race (also a forced one).
        const bool race_night = *reinterpret_cast<const std::uint16_t *>(0x00628A28) != 0;
        const std::int64_t collect_start = PerfNow();
        std::vector<std::pair<float, std::uint32_t>> opponent_order;
        std::vector<std::pair<float, std::uint32_t>> traffic_order;
        std::uint32_t states = 0;
        for (std::uint32_t i = 0; i < racers && m_ConfigOpponents != 0; ++i)
        {
            const std::uintptr_t car = RacerCar( i );
            std::uint8_t lights[2] = {};
            if (car == 0 || i == player || !CarLightsOn( car, lights ))
                continue;
            std::uint8_t kind = 0;
            SafeCopy( reinterpret_cast<const void *>(car + 0x52C), &kind, 1 );
            const bool traffic = (kind & 0x10) != 0;
            if (traffic && (m_ConfigTrafficGlow <= 0.0f || !race_night))
                continue;
            float position[3] = {};
            SafeCopy( reinterpret_cast<const void *>(car + 0x330), position, sizeof( position ) );
            const float dx = position[0] - eye[0], dy = position[1] - eye[1], dz = position[2] - eye[2];
            const float distance2 = dx * dx + dy * dy + dz * dz;
            if (distance2 > reach2)
                continue;
            (traffic ? traffic_order : opponent_order).emplace_back( distance2, i );
        }
        std::sort( opponent_order.begin(), opponent_order.end() );
        std::sort( traffic_order.begin(), traffic_order.end() );
        std::uint32_t traffic_lit = 0;
        for (int group = 0; group < 2; ++group)
        {
            const bool traffic = group == 1;
            for (const auto & entry : (traffic ? traffic_order : opponent_order))
            {
                if (m_CarLights.size() >= limit)
                    break;
                const std::uintptr_t car = RacerCar( entry.second );
                CarLight light;
                light.m_Car = car;
                if (!CarObjectToLight( car, m_CarMatrixLayout, light.m_ObjectToLight.data() ))
                    continue;
                // Reach (B33): opponents '[Lights] HeadlightsOpponentsRange', traffic that times 'TrafficHeadlightRange'.
                const float range = traffic ? m_ConfigOpponentsRange * m_ConfigTrafficRange : m_ConfigOpponentsRange;
                if (range > 0.0f && range != 1.0f)
                {
                    // Other reach: scale the forward axis (column 2) of light space, keeping the lamp start (offset 0x0061BD50).
                    const float k = 1.0f / range;
                    const float offset = *reinterpret_cast<const float *>(0x0061BD50);
                    for (int row = 0; row < 3; ++row)
                        light.m_ObjectToLight[static_cast<std::size_t>(row * 4 + 2)] *= k;
                    light.m_ObjectToLight[14] = (light.m_ObjectToLight[14] - offset) * k + offset;
                }
                float tint[3] = { 1.0f, 1.0f, 1.0f };
                LfspTintForCar( car, tint );
                const float strength = traffic ? m_ConfigTrafficGlow : m_ConfigOpponentsStrength;
                for (std::size_t c = 0; c < 3; ++c)
                    light.m_Colour[c] = base[c] * tint[c] * strength;
                light.m_Colour[3] = 1.0f;
                states |= 1u << entry.second;
                traffic_lit += traffic ? 1 : 0;
                m_CarLights.push_back( light );
            }
        }

        // Cost of the collection (CPU) and the number of active lights, averaged over 600 frames.
        m_CarLightsTicks += PerfNow() - collect_start;
        m_CarLightsActive += static_cast<std::uint64_t>(m_CarLights.size());
        if (++m_CarLightsFrames >= 600)
        {
            LARGE_INTEGER frequency = {};
            ::QueryPerformanceFrequency( &frequency );
            Log( "Car lights: %.1f active on average (limit %zu, reach %.0f, now %u: %u opponents, %u traffic), collection %.1f us per frame",
                static_cast<double>(m_CarLightsActive) / static_cast<double>(m_CarLightsFrames), limit, static_cast<double>(m_ConfigCarLightRadius),
                static_cast<unsigned>(m_CarLights.size()), static_cast<unsigned>(m_CarLights.size()) - traffic_lit, traffic_lit,
                static_cast<double>(m_CarLightsTicks) * 1e6 / std::max( 1.0, static_cast<double>(frequency.QuadPart) ) / static_cast<double>(m_CarLightsFrames) );
            m_CarLightsFrames = 0;
            m_CarLightsTicks = 0;
            m_CarLightsActive = 0;
        }

        if (static_cast<int>(states) != m_CarLightsLogged)
        {
            m_CarLightsLogged = static_cast<int>(states);
            Log( "Headlights opponents: %u of %u racers with light (%s, mask %03X, player %u, traffic with glow %u of %u candidates, %u opponents in reach)",
                static_cast<unsigned>(m_CarLights.size()), racers, night ? "night" : "day", states, player, traffic_lit,
                static_cast<unsigned>(traffic_order.size()), static_cast<unsigned>(opponent_order.size()) );
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
        std::memcpy( m_HeadClipToObject.data(), clip_to_object, sizeof( clip_to_object ) );
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
            const bool night = NightNative();
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
            if ((m_DayLogged & 4) == 0)
            {
                m_DayLogged |= 4;
                Log( "Headlights by day: first lit track draw" );
            }
        }
        else
        {
            if ((m_DayLogged & 8) == 0)
            {
                m_DayLogged |= 8;
                Log( "Headlights by day: track draw not lit, step %d failed", m_HeadFailStep );
            }
        }
    }


    void Driver::OnTrackChunk(
        bool begin
    )
    {
        // Snow white goes on the draws inside the chunk draw: batches must not reach across its edges.
        if (m_ConfigSnowWhite && m_WeatherFxOn && m_WeatherFx[7] > 0.0f)
            Flush();
        m_TrackDrawDepth = std::max( m_TrackDrawDepth + (begin ? 1 : -1), 0 );
        if (m_ConfigHeadlightsDay > 0)
            OnDayTrackDraw( begin );
    }


    bool Driver::WeatherFxFrame()
    {
        using WeatherFn = int( __cdecl * )(float *);
        static WeatherFn weather_fn = nullptr;
        static bool weather_resolved = false;
        if (!weather_resolved)
        {
            weather_resolved = true;
            if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                weather_fn = reinterpret_cast<WeatherFn>(::GetProcAddress( lfsp, "LfspWeatherFx" ));
            Log( "Weather fx (dx11z weather w11: storm, lightning, snow white, snow and clear drops on cars, fixed on the car, fine snow, drops flow with speed, renew, visible beads, even snow, varied streaks, drops as w6 with LOD): LFSP export LfspWeatherFx %s",
                weather_fn != nullptr ? "found" : "not available" );
        }
        if (weather_fn == nullptr)
            return false;
        if (m_WeatherFxFlip == m_FlipCount)
            return m_WeatherFxOn;

        m_WeatherFxFlip = m_FlipCount;
        m_WeatherFxOn = weather_fn( m_WeatherFx ) != 0;

        // Snow and drops on the cars (LfspWeatherCar, LFSP v9r): lit like the world of the last frame (night, storm).
        {
            using CarFn = int( __cdecl * )(float *);
            static CarFn car_fn = nullptr;
            static bool car_resolved = false;
            if (!car_resolved)
            {
                car_resolved = true;
                if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                    car_fn = reinterpret_cast<CarFn>(::GetProcAddress( lfsp, "LfspWeatherCar" ));
                Log( "Weather fx (dx11z weather w11): LFSP export LfspWeatherCar %s", car_fn != nullptr ? "found" : "not available" );
            }
            m_CarWeatherOn = car_fn != nullptr && car_fn( m_CarWeather ) != 0;
            m_CarWeatherLight = m_CarWeatherLightNext * (NightNative() ? 0.35f : 1.0f);
            m_CarWeatherLightNext = 1.0f;
        }
        // Wetness of the track (LfspWeatherState, LFSP v9t, PLAN.md 16.1): read once per frame, logged every 10 s, no effect yet.
        {
            using StateFn = int( __cdecl * )(float *);
            static StateFn state_fn = nullptr;
            static bool state_resolved = false;
            if (!state_resolved)
            {
                state_resolved = true;
                if (HMODULE lfsp = ::GetModuleHandleA( "lfsp.asi" ))
                    state_fn = reinterpret_cast<StateFn>(::GetProcAddress( lfsp, "LfspWeatherState" ));
                Log( "Wet (dx11z wet w1): LFSP export LfspWeatherState %s", state_fn != nullptr ? "found" : "not available" );
            }
            m_WeatherStateOn = state_fn != nullptr && state_fn( m_WeatherState ) != 0;
            const ULONGLONG now = ::GetTickCount64();
            if (m_WeatherStateOn && now >= m_WeatherStateNextReport)
            {
                m_WeatherStateNextReport = now + 10000;
                Log( "Wet: rain %.2f, snow %.2f, wetness %.3f, snow cover %.3f, roof %.2f, wind %.2f, time %.1f s",
                    m_WeatherState[0], m_WeatherState[1], m_WeatherState[2], m_WeatherState[3], m_WeatherState[4], m_WeatherState[5],
                    m_WeatherState[7] );
            }
        }
        m_WeatherGrade = false;
        if (m_WeatherFxOn)
        {
            for (int i = 0; i < 6; ++i)
                m_WeatherGrade = m_WeatherGrade || std::abs( m_WeatherFx[i] - 1.0f ) > 1e-4f;
            m_WeatherGrade = m_WeatherGrade || m_WeatherFx[6] > 1.0f;
        }
        if (m_WeatherFxOn || m_CarWeatherOn)
        {
            const ULONGLONG now = ::GetTickCount64();
            if (now >= m_WeatherNextReport)
            {
                m_WeatherNextReport = now + 10000;
                Log( "Weather fx: mode %.0f, world %.2f %.2f %.2f, sky %.2f %.2f %.2f, fog x%.2f, lightning %.2f, snow %.2f "
                    "(desaturate %.2f, slope %.2f); snow draws %llu (with slope matrix %llu), left out: outside track draw %llu, car/water %llu; "
                    "cars: snow %.2f (light %.2f), drops %.2f, reflection draws with snow %llu, with drops only %llu; "
                    "car up: camera on its side %llu, on the other %llu, sign %+.0f; car parts mirrored %llu of %llu; "
                    "drops flow: parts with a car track %llu, without %llu, fastest car %.0f km/h",
                    m_WeatherFx[15], m_WeatherFx[0], m_WeatherFx[1], m_WeatherFx[2], m_WeatherFx[3], m_WeatherFx[4], m_WeatherFx[5],
                    m_WeatherFx[6], m_WeatherFx[14], m_WeatherFx[7], m_WeatherFx[8], m_WeatherFx[9],
                    static_cast<unsigned long long>(m_WeatherStats[0]), static_cast<unsigned long long>(m_WeatherStats[3]),
                    static_cast<unsigned long long>(m_WeatherStats[1]), static_cast<unsigned long long>(m_WeatherStats[2]),
                    m_CarWeather[0], m_CarWeatherLight, m_CarWeather[1], static_cast<unsigned long long>(m_WeatherStats[4]),
                    static_cast<unsigned long long>(m_WeatherStats[5]), static_cast<unsigned long long>(m_CarUpVotes[0]),
                    static_cast<unsigned long long>(m_CarUpVotes[1]), m_CarUpVote >= 0.0f ? 1.0 : -1.0,
                    static_cast<unsigned long long>(m_CarMirrorParts[1]),
                    static_cast<unsigned long long>(m_CarMirrorParts[0] + m_CarMirrorParts[1]),
                    static_cast<unsigned long long>(m_CarFlowStats[0]), static_cast<unsigned long long>(m_CarFlowStats[1]),
                    m_CarFlowMaxSpeed * 3.6 );
                m_CarFlowStats[0] = m_CarFlowStats[1] = 0;
                m_CarFlowMaxSpeed = 0.0f;
                std::fill( std::begin( m_WeatherStats ), std::end( m_WeatherStats ), 0ull );
                m_CarUpVotes[0] = m_CarUpVotes[1] = 0;
                m_CarMirrorParts[0] = m_CarMirrorParts[1] = 0;
            }
        }
        return m_WeatherFxOn;
    }


    // Rain drops flowing on a car (w5). World place of the part origin from the world camera (as 9.14b: world = position +
    // x right + y up - z forward), the car = the track nearest to it (4 m). Updated once per frame per car, the speed only
    // when the game step (Sim_gStepCount 0x606AC4, 64 Hz) moved on. The drops creep back with the air from about 30 km/h on
    // ('[Weather] CarDropFlow' m/s per m/s above 8 m/s, w6: 0.005 = 0.15 m/s at 140 km/h; w5 had 0.03, drops then ran
    // back nearly with the speed of the car); the phase wraps at 578 cells, where the hash of the shader repeats.
    int Driver::CarFlowTrackFor( const float model_view[16], bool mirrored )
    {
        std::uintptr_t context = 0;
        std::uintptr_t view = 0;
        float block[12] = {};
        if (!SafeCopy( reinterpret_cast<const void *>(0x00628130), &context, 4 ) || context == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(context + 0x10), &view, 4 ) || view == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(view + 0xE8), block, sizeof( block ) ))
            return -1;
        if (block[9] == 0.0f && block[10] == 0.0f && block[11] == 0.0f)
            return -1; // sky and precipitation draw with the position at the origin
        const float * right = block;
        const float * up = block + 3;
        const float * forward = block + 6;
        const auto to_world = [&]( const float * v, float * out )
        {
            for (int j = 0; j < 3; ++j)
                out[j] = v[0] * right[j] + v[1] * up[j] - v[2] * forward[j];
        };
        float origin[3] = {};
        to_world( model_view + 12, origin );
        for (int j = 0; j < 3; ++j)
            origin[j] += block[9 + j];
        int step = 0;
        SafeCopy( reinterpret_cast<const void *>(0x00606AC4), &step, 4 );

        int best = -1;
        float best_distance = 16.0f; // 4 m squared
        int oldest = 0;
        std::uint32_t oldest_age = 0;
        for (int k = 0; k < static_cast<int>(std::size( m_CarFlowTracks )); ++k)
        {
            const CarFlowTrack & track = m_CarFlowTracks[k];
            const std::uint32_t age = track.m_Used ? m_FlipCount - track.m_Flip : 0xFFFFFFFFu;
            if (age >= oldest_age)
            {
                oldest_age = age;
                oldest = k;
            }
            if (!track.m_Used)
                continue;
            float distance = 0.0f;
            for (int j = 0; j < 3; ++j)
                distance += (track.m_Position[j] - origin[j]) * (track.m_Position[j] - origin[j]);
            if (distance < best_distance)
            {
                best_distance = distance;
                best = k;
            }
        }
        if (best < 0)
        {
            CarFlowTrack & track = m_CarFlowTracks[oldest];
            track = CarFlowTrack();
            std::memcpy( track.m_Position, origin, sizeof( origin ) );
            track.m_Step = step;
            track.m_Flip = m_FlipCount;
            track.m_Used = true;
            return oldest;
        }
        CarFlowTrack & track = m_CarFlowTracks[best];
        if (track.m_Flip == m_FlipCount)
            return best;
        track.m_Flip = m_FlipCount;
        const int steps = step - track.m_Step;
        if (steps <= 0)
            return best;
        const float dt = static_cast<float>(steps) / 64.0f;
        float speed2 = 0.0f;
        float velocity[3] = {};
        for (int j = 0; j < 3; ++j)
        {
            velocity[j] = (origin[j] - track.m_Position[j]) / dt;
            speed2 += velocity[j] * velocity[j];
        }
        std::memcpy( track.m_Position, origin, sizeof( origin ) );
        track.m_Step = step;
        if (steps > 64 || speed2 > 120.0f * 120.0f)
            return best; // pause, replay jump, reset to the track: keep the old speed
        const float blend = std::min( 1.0f, dt * 4.0f );
        for (int j = 0; j < 3; ++j)
            track.m_Velocity[j] += (velocity[j] - track.m_Velocity[j]) * blend;
        const float speed = std::sqrt( track.m_Velocity[0] * track.m_Velocity[0] + track.m_Velocity[1] * track.m_Velocity[1] +
            track.m_Velocity[2] * track.m_Velocity[2] );
        track.m_Speed = speed;
        m_CarFlowMaxSpeed = std::max( m_CarFlowMaxSpeed, speed );
        if (speed < 0.3f)
            return best;
        // Direction of travel in the space of the part: object axis i in the world is row i of object x view turned into the
        // world; o_i = d . a_i / |a_i|^2. Mirrored parts: x of the part is flipped as in the corners (w4).
        float direction[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            float axis[3] = {};
            to_world( model_view + i * 4, axis );
            const float length2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
            direction[i] = length2 > 1e-12f ? (track.m_Velocity[0] * axis[0] + track.m_Velocity[1] * axis[1] +
                track.m_Velocity[2] * axis[2]) / (length2 * speed) : 0.0f;
        }
        if (mirrored)
            direction[0] = -direction[0];
        const float period = 578.0f / std::max( m_ConfigCarDropCells, 4.0f );
        const float creep = m_ConfigCarDropFlow * std::max( speed - 8.0f, 0.0f );
        for (int j = 0; j < 3; ++j)
        {
            track.m_DirObject[j] = direction[j];
            track.m_Flow[j] += direction[j] * creep * dt;
            track.m_Flow[j] -= std::floor( track.m_Flow[j] / period ) * period;
        }
        // View space of the direction: x right, y up, z = -forward.
        for (int j = 0; j < 3; ++j)
        {
            const float * axis = j == 0 ? right : (j == 1 ? up : forward);
            const float dot = (track.m_Velocity[0] * axis[0] + track.m_Velocity[1] * axis[1] + track.m_Velocity[2] * axis[2]) / speed;
            track.m_DirView[j] = j == 2 ? -dot : dot;
        }
        return best;
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


        // Also at night: when the player's light is off, the other cars light the track through this path.
        auto * night = reinterpret_cast<std::uint16_t *>(0x00628A28);
        if ((m_DayLogged & 1) == 0)
        {
            m_DayLogged |= 1;
            Log( "Headlights by day: first track chunk draw, night flag %u, player %u", static_cast<unsigned>(*night),
                *reinterpret_cast<const std::uint32_t *>(0x0065741C) );
        }

        const std::uint32_t index = *reinterpret_cast<const std::uint32_t *>(0x0065741C);
        const std::uintptr_t car = RacerCar( index );
        if (car == 0)
        {
            return;
        }

        std::uint8_t lights[2] = {};
        const bool switch_on = CarLightsOn( car, lights );

        if ((m_DayLogged & 2) == 0)
        {
            m_DayLogged |= 2;
            Log( "Headlights by day: player car %08X, light bytes %u %u", static_cast<unsigned>(car), lights[0], lights[1] );
        }

        // The high beam key lights the player's own beam even with the switch off (flasher, 9.10c).
        const bool flasher = UpdateHighBeam() > 0.0f;
        const bool native_night = NightNative();
        const bool player_on = flasher || (!native_night && (m_ConfigHeadlightsDay == 2 || (m_ConfigHeadlightsDay == 1 && switch_on)));

        // Night with the player's light on: the game's own path through FUN_00452910 lights the meshes.
        if (native_night && switch_on)
        {
            return;
        }

        if ((player_on || m_CarMatrixLayout == 0) && !native_night && m_DayLightFlip != m_FlipCount)
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

        if (!m_HeadMesh)
            return;


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

        Flush();
        m_HeadMesh = false;
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
        // The queue end also serves the ambient occlusion ('[AmbientOcclusion]'), with or without water reflections.
        if (!(m_ConfigReflections || m_ConfigAo) || !m_MirrorVs || !m_ReflCompositePs)
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
            // The run after the sky phase is the main view; later runs of the frame are the rear view mirror.
            m_ReflViewIsMain = m_ReflSkyPhase;
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
            // World camera for the waves (9.14b). At the end of the run the rain may have set the view to the origin.
            ++m_ReflWorldReads;
            m_ReflWorldFresh = ReadWorldCamera( m_ReflWorldRows, m_ReflWorldPosition );
            if (m_ReflWorldFresh)
                m_ReflWorldValid = true;
            else
                ++m_ReflWorldFails;
            // Fog of this view for the ambient occlusion ('FogFade'), taken from its world draws.
            if (m_ReflViewIsMain)
                m_AoFogMode = -1;
            // Near plane of the view (R_View + 0x18, doc/re/STRUKTUREN.md 2.2): n of the game depth z = 1 - n / w.
            {
                std::uintptr_t context = 0;
                std::uintptr_t view = 0;
                float near_plane = 0.0f;
                if (SafeCopy( reinterpret_cast<const void *>(0x00628130), &context, 4 ) && context != 0 &&
                    SafeCopy( reinterpret_cast<const void *>(context + 0x10), &view, 4 ) && view != 0 &&
                    SafeCopy( reinterpret_cast<const void *>(view + 0x18), &near_plane, 4 ) && near_plane > 0.01f && near_plane < 10.0f)
                    m_ReflNear = near_plane;
            }
        }

        if (previous > 0 && list == 0)
        {
            if (m_ConfigReflections)
                DrawReflections();
            // Ambient occlusion after the world, the cars and the water, before the HUD; main view only.
            if (m_ConfigAo && m_ReflViewIsMain)
                DrawAmbientOcclusion();
        }
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


    // ---- Sky edge, doc/BUGS.md B32 ----

    bool Driver::BatchIsSky() const
    {
        const auto & render = m_D3D.m_Render;

        if (render[rs::ZFUNC] != 8 || m_BatchVertices.empty())
            return false;

        // As the profile tells the sky: depth func ALWAYS at the far end (z near 1, rhw tiny).
        float z_min = 1e30f, rhw_max = -1e30f;
        for (const TlVertex & v : m_BatchVertices)
        {
            z_min = std::min( z_min, v.m_Z );
            rhw_max = std::max( rhw_max, v.m_Rhw );
        }
        return z_min >= 0.99f && rhw_max < 0.05f;
    }


    void Driver::SkyEdgeFill()
    {
        if (!m_Frame || !m_FrameView || !m_ReflCompositeVs || !m_SkyEdgeColumnPs || !m_SkyEdgeSmoothPs || !m_SkyEdgeFillPs || m_RenderWidth <= 0 ||
            m_RenderHeight <= 0)
            return;

        D3D11_TEXTURE2D_DESC desc = {};
        m_Frame->GetDesc( &desc );

        D3D11_TEXTURE2D_DESC have = {};
        if (m_SkyEdgeCopy)
            m_SkyEdgeCopy->GetDesc( &have );

        if (!m_SkyEdgeCopy || have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format)
        {
            m_SkyEdgeCopyView.Reset();
            m_SkyEdgeCopy.Reset();
            D3D11_TEXTURE2D_DESC copy = desc;
            copy.MipLevels = 1;
            copy.ArraySize = 1;
            copy.SampleDesc.Count = 1;
            copy.SampleDesc.Quality = 0;
            copy.Usage = D3D11_USAGE_DEFAULT;
            copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copy.CPUAccessFlags = 0;
            copy.MiscFlags = 0;
            if (FAILED( m_Device->CreateTexture2D( &copy, nullptr, m_SkyEdgeCopy.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_SkyEdgeCopy.Get(), nullptr, m_SkyEdgeCopyView.Put() ) ))
            {
                m_SkyEdgeCopyView.Reset();
                m_SkyEdgeCopy.Reset();
                Log( "SkyEdgeFill: frame copy %ux%u failed, off", desc.Width, desc.Height );
                m_ConfigSkyEdgeFill = 0;
                return;
            }
        }

        const UINT width = static_cast<UINT>(m_RenderWidth);
        const UINT height = static_cast<UINT>(m_RenderHeight);

        have = {};
        if (m_SkyEdgeTable)
            m_SkyEdgeTable->GetDesc( &have );

        if (!m_SkyEdgeTable || !m_SkyEdgeSmooth || have.Width != width)
        {
            m_SkyEdgeTableRtv.Reset();
            m_SkyEdgeTableView.Reset();
            m_SkyEdgeTable.Reset();
            m_SkyEdgeSmoothRtv.Reset();
            m_SkyEdgeSmoothView.Reset();
            m_SkyEdgeSmooth.Reset();
            D3D11_TEXTURE2D_DESC table = {};
            table.Width = width;
            table.Height = 2;
            table.MipLevels = 1;
            table.ArraySize = 1;
            table.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            table.SampleDesc.Count = 1;
            table.Usage = D3D11_USAGE_DEFAULT;
            table.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED( m_Device->CreateTexture2D( &table, nullptr, m_SkyEdgeTable.Put() ) ) ||
                FAILED( m_Device->CreateRenderTargetView( m_SkyEdgeTable.Get(), nullptr, m_SkyEdgeTableRtv.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_SkyEdgeTable.Get(), nullptr, m_SkyEdgeTableView.Put() ) ) ||
                FAILED( m_Device->CreateTexture2D( &table, nullptr, m_SkyEdgeSmooth.Put() ) ) ||
                FAILED( m_Device->CreateRenderTargetView( m_SkyEdgeSmooth.Get(), nullptr, m_SkyEdgeSmoothRtv.Put() ) ) ||
                FAILED( m_Device->CreateShaderResourceView( m_SkyEdgeSmooth.Get(), nullptr, m_SkyEdgeSmoothView.Put() ) ))
            {
                m_SkyEdgeTableRtv.Reset();
                m_SkyEdgeTableView.Reset();
                m_SkyEdgeTable.Reset();
                m_SkyEdgeSmoothRtv.Reset();
                m_SkyEdgeSmoothView.Reset();
                m_SkyEdgeSmooth.Reset();
                Log( "SkyEdgeFill: edge table %u failed, off", width );
                m_ConfigSkyEdgeFill = 0;
                return;
            }
        }

        if (!m_SkyEdgeConstants)
        {
            D3D11_BUFFER_DESC buffer = {};
            buffer.ByteWidth = 48;
            buffer.Usage = D3D11_USAGE_DYNAMIC;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED( m_Device->CreateBuffer( &buffer, nullptr, m_SkyEdgeConstants.Put() ) ))
                return;
        }

        if (!m_SkyEdgeRaster)
        {
            D3D11_RASTERIZER_DESC raster = {};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = FALSE;
            if (FAILED( m_Device->CreateRasterizerState( &raster, m_SkyEdgeRaster.Put() ) ))
                return;
        }

        // Smoothing radius 12 columns and soft seam 6 rows at 2560 x 1440, scaled with the render size.
        const float radius = std::max( 4.0f, std::round( 12.0f * static_cast<float>(width) / 2560.0f ) );
        const float seam = std::max( 3.0f, 6.0f * static_cast<float>(height) / 1440.0f );
        // Into the band '[Dx11] SkyEdgeBlend' rows (at 1440): the top rows of a 16 bit band texture keep a colour step there.
        const float seam_in = m_ConfigSkyEdgeBlend * static_cast<float>(height) / 1440.0f;
        float constants[12] = { m_SkyEdgeClear[0], m_SkyEdgeClear[1], m_SkyEdgeClear[2], 1.0f,
            static_cast<float>(width), static_cast<float>(height), radius, seam, seam_in, 0.0f, 0.0f, 0.0f };
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_SkyEdgeConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            return;
        std::memcpy( mapped.pData, constants, sizeof( constants ) );
        m_Context->Unmap( m_SkyEdgeConstants.Get(), 0 );

        ID3D11ShaderResourceView * none[2] = {};
        m_Context->PSSetShaderResources( 0, 2, none );

        if (desc.SampleDesc.Count > 1)
            m_Context->ResolveSubresource( m_SkyEdgeCopy.Get(), 0, m_Frame.Get(), 0, desc.Format );
        else
            m_Context->CopyResource( m_SkyEdgeCopy.Get(), m_Frame.Get() );

        m_Context->OMSetBlendState( nullptr, nullptr, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( nullptr, 0 );
        m_Context->RSSetState( m_SkyEdgeRaster.Get() );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_ReflCompositeVs.Get(), nullptr, 0 );
        ID3D11Buffer * buffer = m_SkyEdgeConstants.Get();
        m_Context->PSSetConstantBuffers( 0, 1, &buffer );

        // Pass 1: per column the first fully covered pixel from the top (row 0) and from the bottom (row 1).
        ID3D11RenderTargetView * table_target = m_SkyEdgeTableRtv.Get();
        m_Context->OMSetRenderTargets( 1, &table_target, nullptr );
        D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(width), 2.0f, 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        ID3D11ShaderResourceView * frame_view = m_SkyEdgeCopyView.Get();
        m_Context->PSSetShaderResources( 0, 1, &frame_view );
        m_Context->PSSetShader( m_SkyEdgeColumnPs.Get(), nullptr, 0 );
        m_Context->Draw( 3, 0 );

        // Pass 1b: edge colours averaged over the columns around, so neighbouring columns differ by almost nothing.
        ID3D11RenderTargetView * smooth_target = m_SkyEdgeSmoothRtv.Get();
        m_Context->OMSetRenderTargets( 1, &smooth_target, nullptr );
        ID3D11ShaderResourceView * table_views[2] = { nullptr, m_SkyEdgeTableView.Get() };
        m_Context->PSSetShaderResources( 0, 2, table_views );
        m_Context->PSSetShader( m_SkyEdgeSmoothPs.Get(), nullptr, 0 );
        m_Context->Draw( 3, 0 );
        m_Context->PSSetShaderResources( 0, 2, none );

        // Pass 2: the clear colour left in a pixel becomes the colour of the sky edge of its column. Depth stays as cleared.
        ID3D11RenderTargetView * frame_target = m_FrameView.Get();
        m_Context->OMSetRenderTargets( 1, &frame_target, nullptr );
        viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
        m_Context->RSSetViewports( 1, &viewport );
        ID3D11ShaderResourceView * views[2] = { m_SkyEdgeCopyView.Get(), m_SkyEdgeSmoothView.Get() };
        m_Context->PSSetShaderResources( 0, 2, views );
        m_Context->PSSetShader( m_SkyEdgeFillPs.Get(), nullptr, 0 );
        m_Context->Draw( 3, 0 );

        m_Context->PSSetShaderResources( 0, 2, none );
        InvalidatePipeline();
    }


    bool Driver::PrepareSkyImage()
    {
        if (m_RenderWidth <= 0 || m_RenderHeight <= 0)
            return false;

        // '[Water] ReflectionSkyScale': the sky is smooth, half the render size is enough (9.13d).
        const UINT width = static_cast<UINT>(std::max( 1L, std::lround( m_RenderWidth * m_ConfigReflectionSkyScale ) ));
        const UINT height = static_cast<UINT>(std::max( 1L, std::lround( m_RenderHeight * m_ConfigReflectionSkyScale ) ));

        D3D11_TEXTURE2D_DESC have = {};
        if (m_ReflSky)
            m_ReflSky->GetDesc( &have );

        if (!m_ReflSky || have.Width != width || have.Height != height)
        {
            m_ReflSkyRtv.Reset();
            m_ReflSkyView.Reset();
            m_ReflSky.Reset();
            m_SkyImageFlip = 0xFFFFFFFFu; // B36: the new texture holds no sky yet
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
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


    // Ambient occlusion ('[AmbientOcclusion]', shaders::AO): at the end of the main view's render queue, after the world, the
    // cars and the water, before the HUD. Depth sample 0 single sampled (PackStencil); with 'DepthMips' a pyramid of the linear
    // depth from half size on; raw occlusion into a persistent target (with 'ShadingRate' 1 only half the pixels per frame),
    // one filter pass (with 'Temporal' also the reprojected last frame) into one of two alternating targets, the second filter
    // and the factor in the pass over the frame (multiplicative blend over the MSAA samples; the colour for the factor from the
    // resolved copy, CopyFrame). The game's next draw sets its own state again (ApplyPipeline).
    void Driver::DrawAmbientOcclusion()
    {
        if (!m_AoPs || !m_AoFilterPs || !m_AoApplyPs || !m_AoConstants || !m_ReflCompositeVs || !m_ReflCameraValid || !m_FrameView ||
            m_RenderWidth <= 0 || m_RenderHeight <= 0)
            return;


        D3D11_TEXTURE2D_DESC have = {};
        if (m_AoRaw)
            m_AoRaw->GetDesc( &have );
        if (!m_AoRaw || !m_AoHistory || have.Width != static_cast<UINT>(m_RenderWidth) || have.Height != static_cast<UINT>(m_RenderHeight))
        {
            m_AoRawRtv.Reset();
            m_AoRawSrv.Reset();
            m_AoRaw.Reset();
            m_AoTempRtv.Reset();
            m_AoTempSrv.Reset();
            m_AoTemp.Reset();
            m_AoHistoryRtv.Reset();
            m_AoHistorySrv.Reset();
            m_AoHistory.Reset();
            m_AoPrevValid = false;
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(m_RenderWidth);
            desc.Height = static_cast<UINT>(m_RenderHeight);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R16G16_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            const bool made = SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_AoRaw.Put() ) ) &&
                SUCCEEDED( m_Device->CreateRenderTargetView( m_AoRaw.Get(), nullptr, m_AoRawRtv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateShaderResourceView( m_AoRaw.Get(), nullptr, m_AoRawSrv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_AoTemp.Put() ) ) &&
                SUCCEEDED( m_Device->CreateRenderTargetView( m_AoTemp.Get(), nullptr, m_AoTempRtv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateShaderResourceView( m_AoTemp.Get(), nullptr, m_AoTempSrv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_AoHistory.Put() ) ) &&
                SUCCEEDED( m_Device->CreateRenderTargetView( m_AoHistory.Get(), nullptr, m_AoHistoryRtv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateShaderResourceView( m_AoHistory.Get(), nullptr, m_AoHistorySrv.Put() ) );
            Log( "Ambient occlusion: targets %dx%d %s", m_RenderWidth, m_RenderHeight, made ? "created" : "FAILED" );
            if (!made)
            {
                m_AoRaw.Reset();
                m_AoTemp.Reset();
                m_AoHistory.Reset();
                return;
            }
            const float open[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            m_Context->ClearRenderTargetView( m_AoRawRtv.Get(), open );
            m_Context->ClearRenderTargetView( m_AoTempRtv.Get(), open );
            m_Context->ClearRenderTargetView( m_AoHistoryRtv.Get(), open );
        }

        // Pyramid of the linear depth ('DepthMips'): half ... 1/16 of the render size.
        const int half_width = std::max( m_RenderWidth >> 1, 1 );
        const int half_height = std::max( m_RenderHeight >> 1, 1 );
        bool mips = m_ConfigAoDepthMips != 0 && m_AoDepthHalfPs && m_AoDepthMipPs;
        if (mips)
        {
            D3D11_TEXTURE2D_DESC levels = {};
            if (m_AoDepthMips)
                m_AoDepthMips->GetDesc( &levels );
            if (!m_AoDepthMips || levels.Width != static_cast<UINT>(half_width) || levels.Height != static_cast<UINT>(half_height))
            {
                for (int level = 0; level < AO_DEPTH_LEVELS; ++level)
                {
                    m_AoDepthLevelSrv[level].Reset();
                    m_AoDepthLevelRtv[level].Reset();
                }
                m_AoDepthMipsSrv.Reset();
                m_AoDepthMips.Reset();
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = static_cast<UINT>(half_width);
                desc.Height = static_cast<UINT>(half_height);
                desc.MipLevels = AO_DEPTH_LEVELS;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R32_FLOAT;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                bool made = SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_AoDepthMips.Put() ) ) &&
                    SUCCEEDED( m_Device->CreateShaderResourceView( m_AoDepthMips.Get(), nullptr, m_AoDepthMipsSrv.Put() ) );
                for (int level = 0; made && level < AO_DEPTH_LEVELS; ++level)
                {
                    D3D11_RENDER_TARGET_VIEW_DESC rtv = {};
                    rtv.Format = desc.Format;
                    rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    rtv.Texture2D.MipSlice = static_cast<UINT>(level);
                    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
                    srv.Format = desc.Format;
                    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srv.Texture2D.MostDetailedMip = static_cast<UINT>(level);
                    srv.Texture2D.MipLevels = 1;
                    made = SUCCEEDED( m_Device->CreateRenderTargetView( m_AoDepthMips.Get(), &rtv, m_AoDepthLevelRtv[level].Put() ) ) &&
                        SUCCEEDED( m_Device->CreateShaderResourceView( m_AoDepthMips.Get(), &srv, m_AoDepthLevelSrv[level].Put() ) );
                }
                Log( "Ambient occlusion: depth pyramid %dx%d, %d levels %s", half_width, half_height, AO_DEPTH_LEVELS, made ? "created" : "FAILED" );
                if (!made)
                {
                    for (int level = 0; level < AO_DEPTH_LEVELS; ++level)
                    {
                        m_AoDepthLevelSrv[level].Reset();
                        m_AoDepthLevelRtv[level].Reset();
                    }
                    m_AoDepthMipsSrv.Reset();
                    m_AoDepthMips.Reset();
                }
            }
            mips = m_AoDepthMips && m_AoDepthMipsSrv;
        }

        // 'HalfRes': target of the raw occlusion at half width and height.
        bool half_res = m_ConfigAoHalfRes != 0 && m_AoUpsamplePs;
        if (half_res)
        {
            D3D11_TEXTURE2D_DESC have_half = {};
            if (m_AoHalf)
                m_AoHalf->GetDesc( &have_half );
            if (!m_AoHalf || have_half.Width != static_cast<UINT>(half_width) || have_half.Height != static_cast<UINT>(half_height))
            {
                m_AoHalfRtv.Reset();
                m_AoHalfSrv.Reset();
                m_AoHalf.Reset();
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = static_cast<UINT>(half_width);
                desc.Height = static_cast<UINT>(half_height);
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R16G16_FLOAT;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                const bool made = SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_AoHalf.Put() ) ) &&
                    SUCCEEDED( m_Device->CreateRenderTargetView( m_AoHalf.Get(), nullptr, m_AoHalfRtv.Put() ) ) &&
                    SUCCEEDED( m_Device->CreateShaderResourceView( m_AoHalf.Get(), nullptr, m_AoHalfSrv.Put() ) );
                Log( "Ambient occlusion: half size target %dx%d %s (HalfRes)", half_width, half_height, made ? "created" : "FAILED" );
                if (!made)
                {
                    m_AoHalfRtv.Reset();
                    m_AoHalfSrv.Reset();
                    m_AoHalf.Reset();
                }
            }
            half_res = m_AoHalfRtv && m_AoHalfSrv;
        }

        if (!m_AoMultiply)
        {
            // frame x factor
            D3D11_BLEND_DESC desc = {};
            auto & target = desc.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_ZERO;
            target.DestBlend = D3D11_BLEND_SRC_COLOR;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ZERO;
            target.DestBlendAlpha = D3D11_BLEND_ONE;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            m_Device->CreateBlendState( &desc, m_AoMultiply.Put() );
        }
        if (!m_AoRaster)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.DepthClipEnable = FALSE;
            m_Device->CreateRasterizerState( &desc, m_AoRaster.Put() );
        }

        // Depth of the frame (sample 0, single sampled) and the resolved colour for the factor.
        if (!PackStencil() || !m_ReflMaskDepthSrv || !CopyFrame() || !m_ReflFrameCopyView)
            return;

        // 'Temporal': view of this frame -> clip of the last one, from both world cameras. Only straight after the last AO
        // frame, with the world camera read in both, and not across a camera cut (more than 30 m between the frames).
        const bool temporal = m_ConfigAoTemporal != 0;
        bool reproject = false;
        float prev_clip[16] = {};
        if (temporal && m_AoPrevValid && m_ReflWorldFresh && m_AoPrevFlip + 1 == m_FlipCount)
        {
            const float * rows = m_ReflWorldRows;
            const float * last = m_AoPrevRows;
            const float move[3] = { m_ReflWorldPosition[0] - m_AoPrevPosition[0], m_ReflWorldPosition[1] - m_AoPrevPosition[1],
                m_ReflWorldPosition[2] - m_AoPrevPosition[2] };
            if (move[0] * move[0] + move[1] * move[1] + move[2] * move[2] < 900.0f)
            {
                // A world direction into the last view: dot with its rows, z negated (view = right, up, -forward).
                const auto to_last = [&]( const float world[3], float out[4] ) {
                    out[0] = world[0] * last[0] + world[1] * last[1] + world[2] * last[2];
                    out[1] = world[0] * last[3] + world[1] * last[4] + world[2] * last[5];
                    out[2] = -(world[0] * last[6] + world[1] * last[7] + world[2] * last[8]);
                    out[3] = 0.0f;
                };
                // Rows (row vectors): the images of the view axes x, y, z of this frame (world right, up, -forward), then the move.
                float basis[4][4] = {};
                const float minus_forward[3] = { -rows[6], -rows[7], -rows[8] };
                to_last( rows, basis[0] );
                to_last( rows + 3, basis[1] );
                to_last( minus_forward, basis[2] );
                to_last( move, basis[3] );
                basis[3][3] = 1.0f;
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                    {
                        float sum = 0.0f;
                        for (int k = 0; k < 4; ++k)
                            sum += basis[i][k] * m_AoPrevClipOfView[k * 4 + j];
                        prev_clip[i * 4 + j] = sum;
                    }
                reproject = true;
            }
        }
        const bool filter_pass = m_ConfigAoFilter >= 1 || temporal;
        const bool fog = m_ConfigAoFogFade != 0 && m_AoFogMode >= 0;

        // Slices x samples per side: low, medium, high.
        static const int presets[3][2] = { { 2, 4 }, { 2, 10 }, { 4, 14 } };
        m_ConfigAoQuality = std::clamp( m_ConfigAoQuality, 0, 2 );
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED( m_Context->Map( m_AoConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ))
            return;
        {
            float constants[324] = {};
            std::memcpy( constants, m_ReflViewOfClip.data(), sizeof( float ) * 16 );
            std::memcpy( constants + 16, m_ReflViewport.data(), sizeof( float ) * 4 );
            constants[20] = static_cast<float>(m_RenderWidth);
            constants[21] = static_cast<float>(m_RenderHeight);
            constants[22] = m_ReflNear;
            constants[23] = static_cast<float>(++m_AoFrame & 0xFFFF);
            constants[24] = m_ConfigAoRadius;
            constants[25] = m_ConfigAoAmount;
            // 'FadeDistance' D metres -> the same point on the normalised depth d = z / (F - z (F - 1)), z = 1 - n / w:
            // d = (w - n) / (w + (F - 1) n) at w = D.
            if (m_ConfigAoFadeDistance > 0.0f)
            {
                const float n = m_ReflNear > 0.0f ? m_ReflNear : 0.5f;
                const float far_plane = m_ConfigAoFarPlane;
                constants[26] = std::clamp( (m_ConfigAoFadeDistance - n) / (m_ConfigAoFadeDistance + (far_plane - 1.0f) * n), 0.01f, 1.0f );
            }
            else
                constants[26] = m_ConfigAoFade;
            constants[27] = m_ConfigAoFarPlane;
            constants[28] = static_cast<float>(presets[m_ConfigAoQuality][0]);
            constants[29] = static_cast<float>(presets[m_ConfigAoQuality][1]);
            // 'HalfRes' and the checkerboard are not combined: the half size pass computes every one of its pixels.
            constants[30] = half_res ? 0.0f : static_cast<float>(m_ConfigAoShadingRate);
            constants[47] = half_res ? 1.0f : 0.0f;
            constants[31] = static_cast<float>(m_ConfigAoDebug);
            constants[32] = static_cast<float>(m_ConfigAoFilter);
            constants[33] = static_cast<float>(m_ConfigAoGrow);
            constants[34] = temporal ? m_ConfigAoTemporalBlend : 0.0f;
            constants[35] = mips ? static_cast<float>(AO_DEPTH_LEVELS) : 0.0f;
            constants[36] = static_cast<float>(m_ConfigAoMethod);
            constants[37] = m_ConfigAoThickness;
            constants[38] = reproject ? 1.0f : 0.0f;
            constants[39] = std::clamp( m_ConfigAoAngleBias, 0.0f, 30.0f ) * 3.14159265f / 180.0f;
            constants[40] = static_cast<float>(m_ConfigAoMultiBounce);
            constants[41] = std::clamp( m_ConfigAoFill, 0.0f, 1.0f );
            constants[42] = fog ? static_cast<float>(m_AoFogMode) : -1.0f;
            constants[43] = fog ? m_AoFogParams[3] : 0.0f;
            constants[44] = m_AoFogParams[0];
            constants[45] = m_AoFogParams[1];
            constants[46] = m_AoFogParams[2];
            std::memcpy( constants + 48, prev_clip, sizeof( prev_clip ) );
            std::memcpy( constants + 64, m_AoPrevViewport, sizeof( m_AoPrevViewport ) );
            for (int i = 0; i < 256; ++i)
                constants[68 + i] = static_cast<float>(m_AoFogTable[static_cast<std::size_t>(i)]) / 255.0f;
            std::memcpy( mapped.pData, constants, sizeof( constants ) );
        }
        m_Context->Unmap( m_AoConstants.Get(), 0 );

        m_Context->RSSetState( m_AoRaster.Get() );
        m_Context->OMSetDepthStencilState( nullptr, 0 );
        m_Context->IASetInputLayout( nullptr );
        m_Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
        m_Context->VSSetShader( m_ReflCompositeVs.Get(), nullptr, 0 );
        ID3D11Buffer * constant_buffer = m_AoConstants.Get();
        m_Context->PSSetConstantBuffers( 0, 1, &constant_buffer );
        m_Context->OMSetBlendState( nullptr, nullptr, 0xFFFFFFFF );
        ID3D11ShaderResourceView * none[6] = {};
        ID3D11ShaderResourceView * depth = m_ReflMaskDepthSrv.Get();

        // 0: the depth pyramid ('DepthMips'), level 0 from the depth, each further one from the level above.
        if (mips)
        {
            for (int level = 0; level < AO_DEPTH_LEVELS; ++level)
            {
                const float width = static_cast<float>(std::max( m_RenderWidth >> (level + 1), 1 ));
                const float height = static_cast<float>(std::max( m_RenderHeight >> (level + 1), 1 ));
                const D3D11_VIEWPORT viewport = { 0, 0, width, height, 0, 1 };
                m_Context->RSSetViewports( 1, &viewport );
                ID3D11RenderTargetView * level_target = m_AoDepthLevelRtv[level].Get();
                m_Context->OMSetRenderTargets( 1, &level_target, nullptr );
                if (level == 0)
                {
                    m_Context->PSSetShaderResources( 0, 1, &depth );
                    m_Context->PSSetShader( m_AoDepthHalfPs.Get(), nullptr, 0 );
                }
                else
                {
                    ID3D11ShaderResourceView * source = m_AoDepthLevelSrv[level - 1].Get();
                    m_Context->PSSetShaderResources( 5, 1, &source );
                    m_Context->PSSetShader( m_AoDepthMipPs.Get(), nullptr, 0 );
                }
                m_Context->Draw( 3, 0 );
                m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
                m_Context->PSSetShaderResources( 0, 6, none );
            }
        }

        const D3D11_VIEWPORT full = { 0, 0, static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight), 0, 1 };
        m_Context->RSSetViewports( 1, &full );

        // 1: raw occlusion (persistent: the skipped half of the checkerboard keeps last frame's value).
        ID3D11RenderTargetView * target = half_res ? m_AoHalfRtv.Get() : m_AoRawRtv.Get();
        if (half_res)
        {
            const D3D11_VIEWPORT half = { 0, 0, static_cast<float>(half_width), static_cast<float>(half_height), 0, 1 };
            m_Context->RSSetViewports( 1, &half );
        }
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        {
            ID3D11ShaderResourceView * views[4] = { depth, nullptr, nullptr, mips ? m_AoDepthMipsSrv.Get() : nullptr };
            m_Context->PSSetShaderResources( 0, 4, views );
        }
        m_Context->PSSetShader( m_AoPs.Get(), nullptr, 0 );
        m_Context->Draw( 3, 0 );
        m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
        m_Context->PSSetShaderResources( 0, 6, none );
        // 'HalfRes': back to full size into the raw target, by depth (UpsamplePS); the filters read it as before.
        if (half_res)
        {
            m_Context->RSSetViewports( 1, &full );
            target = m_AoRawRtv.Get();
            m_Context->OMSetRenderTargets( 1, &target, nullptr );
            ID3D11ShaderResourceView * views[2] = { depth, m_AoHalfSrv.Get() };
            m_Context->PSSetShaderResources( 0, 2, views );
            m_Context->PSSetShader( m_AoUpsamplePs.Get(), nullptr, 0 );
            m_Context->Draw( 3, 0 );
            m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
            m_Context->PSSetShaderResources( 0, 6, none );
        }

        // 2: first filter pass and the accumulation; its target alternates, the other one holds the last frame.
        ID3D11ShaderResourceView * occlusion = m_AoRawSrv.Get();
        if (filter_pass)
        {
            target = m_AoHistoryFlip ? m_AoHistoryRtv.Get() : m_AoTempRtv.Get();
            ID3D11ShaderResourceView * last = m_AoHistoryFlip ? m_AoTempSrv.Get() : m_AoHistorySrv.Get();
            m_Context->OMSetRenderTargets( 1, &target, nullptr );
            ID3D11ShaderResourceView * views[5] = { depth, occlusion, nullptr, nullptr, last };
            m_Context->PSSetShaderResources( 0, 5, views );
            m_Context->PSSetShader( m_AoFilterPs.Get(), nullptr, 0 );
            m_Context->Draw( 3, 0 );
            m_Context->OMSetRenderTargets( 0, nullptr, nullptr );
            m_Context->PSSetShaderResources( 0, 6, none );
            occlusion = m_AoHistoryFlip ? m_AoHistorySrv.Get() : m_AoTempSrv.Get();
            m_AoHistoryFlip = !m_AoHistoryFlip;
        }

        // 3: second filter and the factor over the frame ('Debug' 1: the occlusion itself).
        target = m_FrameView.Get();
        m_Context->OMSetRenderTargets( 1, &target, nullptr );
        const float factor[4] = {};
        m_Context->OMSetBlendState( m_ConfigAoDebug ? nullptr : m_AoMultiply.Get(), factor, 0xFFFFFFFF );
        ID3D11ShaderResourceView * views[3] = { nullptr, occlusion, m_ReflFrameCopyView.Get() };
        m_Context->PSSetShaderResources( 0, 3, views );
        m_Context->PSSetShader( m_AoApplyPs.Get(), nullptr, 0 );
        m_Context->Draw( 3, 0 );
        m_Context->PSSetShaderResources( 0, 6, none );

        // The frame and its depth bound again, as after the water composite.
        m_Context->OMSetRenderTargets( 1, &target, m_DepthView.Get() );

        // This frame's camera for the next one.
        m_AoPrevValid = filter_pass && m_ReflWorldFresh;
        m_AoPrevFlip = m_FlipCount;
        if (m_AoPrevValid)
        {
            std::memcpy( m_AoPrevClipOfView, m_ReflClipOfView.data(), sizeof( m_AoPrevClipOfView ) );
            std::memcpy( m_AoPrevViewport, m_ReflViewport.data(), sizeof( m_AoPrevViewport ) );
            std::memcpy( m_AoPrevRows, m_ReflWorldRows, sizeof( m_AoPrevRows ) );
            std::memcpy( m_AoPrevPosition, m_ReflWorldPosition, sizeof( m_AoPrevPosition ) );
        }

        ++m_AoRuns;
        if (m_AoRuns == 1)
            Log( "Ambient occlusion (dx11z 9zd): run %llu, %dx%d, near %.3f, slices %d x %d samples per side, shading rate %d, filter %d, "
                 "method %d, depth mips %d, half res %d, temporal %d (reprojected %d), fog mode %d",
                static_cast<unsigned long long>(m_AoRuns), m_RenderWidth, m_RenderHeight, m_ReflNear, presets[m_ConfigAoQuality][0],
                presets[m_ConfigAoQuality][1], m_ConfigAoShadingRate, m_ConfigAoFilter, m_ConfigAoMethod, mips ? 1 : 0, half_res ? 1 : 0, temporal ? 1 : 0,
                reproject ? 1 : 0, fog ? m_AoFogMode : -1 );
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


    bool Driver::PackStencil()
    {
        ID3D11PixelShader * pack = m_Samples > 1 ? m_ReflPackMsPs.Get() : m_ReflPackPs.Get();
        if (pack == nullptr || m_RenderWidth <= 0 || m_RenderHeight <= 0)
            return false;

        // The depth buffer itself, or a copy of it where it cannot be read as a texture.
        ID3D11ShaderResourceView * sources[2] = { m_DepthStencilSrv.Get(), m_DepthSrv.Get() };
        if (sources[0] == nullptr || sources[1] == nullptr)
        {
            if (!CopyStencil())
                return false;
            sources[0] = m_ReflStencilView.Get();
            sources[1] = m_ReflDepthCopyView.Get();
        }

        D3D11_TEXTURE2D_DESC have = {};
        if (m_ReflMask)
            m_ReflMask->GetDesc( &have );

        if (!m_ReflMask || !m_ReflMaskDepth || have.Width != static_cast<UINT>(m_RenderWidth) || have.Height != static_cast<UINT>(m_RenderHeight))
        {
            m_ReflMaskRtv.Reset();
            m_ReflMaskSrv.Reset();
            m_ReflMask.Reset();
            m_ReflMaskDepthRtv.Reset();
            m_ReflMaskDepthSrv.Reset();
            m_ReflMaskDepth.Reset();
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(m_RenderWidth);
            desc.Height = static_cast<UINT>(m_RenderHeight);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8_UINT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            bool made = SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflMask.Put() ) ) &&
                SUCCEEDED( m_Device->CreateRenderTargetView( m_ReflMask.Get(), nullptr, m_ReflMaskRtv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateShaderResourceView( m_ReflMask.Get(), nullptr, m_ReflMaskSrv.Put() ) );
            desc.Format = DXGI_FORMAT_R32_FLOAT;
            made = made && SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflMaskDepth.Put() ) ) &&
                SUCCEEDED( m_Device->CreateRenderTargetView( m_ReflMaskDepth.Get(), nullptr, m_ReflMaskDepthRtv.Put() ) ) &&
                SUCCEEDED( m_Device->CreateShaderResourceView( m_ReflMaskDepth.Get(), nullptr, m_ReflMaskDepthSrv.Put() ) );
            Log( "Water: stencil and depth pack %dx%d %s", m_RenderWidth, m_RenderHeight, made ? "created" : "FAILED" );
            if (!made)
            {
                m_ReflMaskRtv.Reset();
                m_ReflMaskSrv.Reset();
                m_ReflMask.Reset();
                m_ReflMaskDepthRtv.Reset();
                m_ReflMaskDepthSrv.Reset();
                m_ReflMaskDepth.Reset();
                return false;
            }
        }

        // The whole render size: the lookups of the composite reach past the clip of the view. The scissor of the
        // composite stays set, this pass draws without it.
        ID3D11RenderTargetView * targets[2] = { m_ReflMaskRtv.Get(), m_ReflMaskDepthRtv.Get() };
        m_Context->OMSetRenderTargets( 2, targets, nullptr );
        m_Context->OMSetBlendState( nullptr, nullptr, 0xFFFFFFFF );
        m_Context->OMSetDepthStencilState( nullptr, 0 );

        auto & raster = m_ReflRasters[0x300];
        if (!raster)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.DepthClipEnable = FALSE;
            m_Device->CreateRasterizerState( &desc, raster.Put() );
        }
        m_Context->RSSetState( raster.Get() );

        m_Context->IASetInputLayout( nullptr );
        m_Context->VSSetShader( m_ReflCompositeVs.Get(), nullptr, 0 );
        m_Context->PSSetShader( pack, nullptr, 0 );
        m_Context->PSSetShaderResources( 0, 2, sources );
        m_Context->Draw( 3, 0 );

        // The depth buffer is bound again for the composite: not as a texture at the same time.
        ID3D11ShaderResourceView * none[2] = {};
        m_Context->PSSetShaderResources( 0, 2, none );
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


    // World position and axes of the active view (REFLEXION-WETTER.md 9.14b, doc/KAMERA.md §6): R_View in RENDER_CONTEXT + 0x10,
    // camera block at + 0xB8: rows right, up, forward at + 0x30 (view + 0xE8), position at + 0x54 (view + 0x10C).
    // R_ViewApplyViewport -> Game_ViewMirror negates forward and transposes, so the view space of the game is
    // (right, up, -forward) about the position: world = position + x right + y up - z forward.
    bool Driver::ReadWorldCamera( float rows[9], float position[3] )
    {
        std::uintptr_t context = 0;
        std::uintptr_t view = 0;
        float block[12] = {};
        if (!SafeCopy( reinterpret_cast<const void *>(0x00628130), &context, 4 ) || context == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(context + 0x10), &view, 4 ) || view == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(view + 0xE8), block, sizeof( block ) ))
            return false;
        for (int r = 0; r < 3; ++r)
        {
            const float length = block[r * 3] * block[r * 3] + block[r * 3 + 1] * block[r * 3 + 1] + block[r * 3 + 2] * block[r * 3 + 2];
            if (!(length > 0.9f && length < 1.1f))
                return false;
        }
        // The sky and the precipitation draw with the position briefly at the origin.
        if (block[9] == 0.0f && block[10] == 0.0f && block[11] == 0.0f)
            return false;
        std::memcpy( rows, block, sizeof( float ) * 9 );
        std::memcpy( position, block + 9, sizeof( float ) * 3 );
        return true;
    }

    // Sun of the sky (R_SunCtor 0x460780, object in 0x627E5C): + 0x11C its place on the sky from R_SkyDirFromAngles, drawn
    // with the camera at the origin, so a direction in world axes; + 0x140 set when the track has a sun (angles not 0).
    bool Driver::ReadSunDirection( float direction[3] )
    {
        std::uintptr_t sun = 0;
        unsigned char present = 0;
        float place[3] = {};
        if (!SafeCopy( reinterpret_cast<const void *>(0x00627E5C), &sun, 4 ) || sun == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(sun + 0x140), &present, 1 ) || present == 0 ||
            !SafeCopy( reinterpret_cast<const void *>(sun + 0x11C), place, sizeof( place ) ))
            return false;
        const float length = std::sqrt( place[0] * place[0] + place[1] * place[1] + place[2] * place[2] );
        if (!(length > 1e-3f))
            return false;
        for (int i = 0; i < 3; ++i)
            direction[i] = place[i] / length;
        return true;
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
        // Additive lights (9.14a): One onto the frame, kept only with '[Water] ReflectionLights'.
        if (render[rs::ALPHABLENDENABLE] != 0 && render[rs::DESTBLEND] == 2 && (render[rs::SRCBLEND] == 5 || render[rs::SRCBLEND] == 2))
            batch.m_BlendKey = render[rs::SRCBLEND] == 2 ? 3u : 2u; // 2: SrcAlpha + One, 3: One + One
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

        // The queue end may run for the ambient occlusion alone.
        if (!m_ConfigReflections)
            return;

        // '[Water] ReflectionMirrorView' 0: no reflection in the rear view mirror (9.13d).
        if (m_ConfigReflectionMirrorView == 0 && !m_ReflViewIsMain)
        {
            ++m_ReflMirrorViewSkipped;
            return;
        }

        if (batches == 0 || water == 0 || !m_FrameView || !m_DepthView)
        {
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

        if (m_ReflLogged < 6)
        {
            ++m_ReflLogged;
            Log( "Water: reflection #%llu flip %u, %zu draws, %zu water triangles, %u shore draws, sky %d, plane %.3f %.3f %.3f %.2f, viewport %.1f %.1f %.1f %.1f, clip %ld %ld %ld %ld",
                static_cast<unsigned long long>(m_ReflFrames), m_FlipCount, batches, water, m_ReflShoreDraws, m_ReflSkyRun && m_ReflSkyView ? 1 : 0, plane[0], plane[1], plane[2], plane[3],
                viewport[0], viewport[1], viewport[2], viewport[3], m_ReflClip.left, m_ReflClip.top, m_ReflClip.right, m_ReflClip.bottom );
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

        // Target of the mirrored lights (9.14a), half float: lights add up past 1, the composite boosts them.
        if (m_ConfigReflectionLights != 0)
        {
            D3D11_TEXTURE2D_DESC have = {};
            if (m_ReflLight)
                m_ReflLight->GetDesc( &have );
            if (!m_ReflLight || have.Width != static_cast<UINT>(refl_width) || have.Height != static_cast<UINT>(refl_height))
            {
                m_ReflLightView.Reset();
                m_ReflLightSrv.Reset();
                m_ReflLight.Reset();
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = static_cast<UINT>(refl_width);
                desc.Height = static_cast<UINT>(refl_height);
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                const bool made = SUCCEEDED( m_Device->CreateTexture2D( &desc, nullptr, m_ReflLight.Put() ) ) &&
                    SUCCEEDED( m_Device->CreateRenderTargetView( m_ReflLight.Get(), nullptr, m_ReflLightView.Put() ) ) &&
                    SUCCEEDED( m_Device->CreateShaderResourceView( m_ReflLight.Get(), nullptr, m_ReflLightSrv.Put() ) );
                Log( "Water: light target %dx%d %s", refl_width, refl_height, made ? "created" : "FAILED" );
                if (!made)
                {
                    m_ReflLightView.Reset();
                    m_ReflLightSrv.Reset();
                    m_ReflLight.Reset();
                }
            }
        }

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
        // The kept draws have no headlights (CaptureMirrorBatch): '[Water] ReflectionSimpleShader' leaves the light loops out.
        ID3D11PixelShader * const mirror_ps = m_ConfigReflectionSimpleShader != 0 && m_MirrorPs ? m_MirrorPs.Get() : m_ScenePs.Get();
        m_Context->PSSetShader( mirror_ps, nullptr, 0 );
        ID3D11Buffer * buffers[2] = { m_DrawConstants.Get(), m_MirrorConstants.Get() };
        m_Context->VSSetConstantBuffers( 0, 2, buffers );
        m_Context->PSSetConstantBuffers( 0, 1, buffers );
        // '[Water] ReflectionLights' (9.14a): the additive lights go into a target of their own (same depth), boosted and
        // streaked by the composite.
        bool lights_bound = false;
        const bool lights_target = m_ConfigReflectionLights != 0 && m_ReflLightView;
        if (lights_target)
            m_Context->ClearRenderTargetView( m_ReflLightView.Get(), transparent );
        ID3D11ShaderResourceView * no_view = nullptr;
        m_Context->PSSetShaderResources( 2, 1, &no_view );

        const float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        // Vertices, indices, constants, textures and samplers of a kept draw; where its vertices and indices lie.
        UINT base_vertex = 0;
        UINT first_index = 0;
        const auto upload = [&]( const MirrorBatch & batch )
        {
            if (!UploadRing( batch.m_Vertices.data(), batch.m_Vertices.size(), batch.m_Indices.data(), batch.m_Indices.size(),
                base_vertex, first_index ))
                return false;

            UploadDrawConstants( batch.m_Constants.data(), batch.m_Constants.size() );

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

            // Sorted out on the CPU (9.13d), not the sky: wholly under the water plane (the mirror pass clips it away anyway),
            // or smaller on screen than '[Water] ReflectionMinSize'. Corners without a valid w count as above (drawn).
            if (i >= sky_batches)
            {
                const bool size_test = m_ConfigReflectionMinSize > 0.0f;
                bool above = false;
                float low_x = 1e30f, high_x = -1e30f, low_y = 1e30f, high_y = -1e30f;
                for (const TlVertex & v : batch.m_Vertices)
                {
                    if (size_test)
                    {
                        low_x = std::min( low_x, v.m_X );
                        high_x = std::max( high_x, v.m_X );
                        low_y = std::min( low_y, v.m_Y );
                        high_y = std::max( high_y, v.m_Y );
                    }
                    if (above)
                    {
                        if (!size_test)
                            break;
                        continue;
                    }
                    if (!(v.m_Rhw > 0.0f && v.m_Rhw < 1e30f))
                    {
                        above = true;
                        continue;
                    }
                    double view_point[3];
                    to_view( { v.m_X, v.m_Y, 1.0f / v.m_Rhw }, view_point );
                    above = plane[0] * view_point[0] + plane[1] * view_point[1] + plane[2] * view_point[2] + plane[3] >= 0.0;
                }
                if (!above)
                {
                    ++m_ReflCulledBelow;
                    continue;
                }
                if (m_ConfigReflectionMinSize > 0.0f && !batch.m_Vertices.empty() &&
                    std::max( high_x - low_x, high_y - low_y ) * 1440.0f / static_cast<float>(std::max( m_Height, 1 )) < m_ConfigReflectionMinSize)
                {
                    ++m_ReflCulledSmall;
                    continue;
                }
                ++m_ReflKept;
            }

            // Additive lights (9.14a) into their own target, everything else into the reflection target.
            const bool light = batch.m_BlendKey >= 2;
            if (light && !lights_target)
                continue;
            if (light != lights_bound)
            {
                lights_bound = light;
                ID3D11RenderTargetView * target = light ? m_ReflLightView.Get() : m_ReflTargetView.Get();
                m_Context->OMSetRenderTargets( 1, &target, m_ReflDepthView.Get() );
            }
            if (light)
                ++m_ReflLightsKept;

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
                if (batch.m_BlendKey >= 2)
                {
                    // Lights add up in their own target, as the game adds them to the frame.
                    target.SrcBlend = batch.m_BlendKey == 3 ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA;
                    target.DestBlend = D3D11_BLEND_ONE;
                    target.SrcBlendAlpha = D3D11_BLEND_ONE;
                    target.DestBlendAlpha = D3D11_BLEND_ONE;
                    target.BlendOpAlpha = D3D11_BLEND_OP_MAX;
                }
                else if (batch.m_BlendKey != 0)
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

            m_Context->DrawIndexed( static_cast<UINT>(batch.m_Indices.size()), first_index, static_cast<INT>(base_vertex) );
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
            // '[Water] ReflectionSkyScale' 0.5: the sky image half size, the composite looks it up by texture coordinates.
            D3D11_TEXTURE2D_DESC sky_desc = {};
            m_ReflSky->GetDesc( &sky_desc );
            const float kx = static_cast<float>(sky_desc.Width) / static_cast<float>(std::max( m_RenderWidth, 1 ));
            const float ky = static_cast<float>(sky_desc.Height) / static_cast<float>(std::max( m_RenderHeight, 1 ));
            const D3D11_VIEWPORT sky_view = { 0, 0, static_cast<float>(sky_desc.Width), static_cast<float>(sky_desc.Height), 0, 1 };
            m_Context->RSSetViewports( 1, &sky_view );
            const D3D11_RECT sky_scissor = { static_cast<LONG>(m_ReflClip.left * kx), static_cast<LONG>(m_ReflClip.top * ky),
                static_cast<LONG>(std::ceil( m_ReflClip.right * kx )), static_cast<LONG>(std::ceil( m_ReflClip.bottom * ky )) };
            m_Context->RSSetScissorRects( 1, &sky_scissor );
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
                m_Context->DrawIndexed( static_cast<UINT>(batch.m_Indices.size()), first_index, static_cast<INT>(base_vertex) );
            }
            m_Context->RSSetViewports( 1, &full );
            m_Context->RSSetScissorRects( 1, &full_scissor );
            // The car reflections of the next frame look the sky up here (9.15a, main view only).
            if (m_ReflViewIsMain)
                m_SkyImageFlip = m_FlipCount;
        }
        // Before the composite clears the water bit: open sky (stencil 0) next to the water for '[Water] EdgeFade',
        // solid draws in front of the sky for the sky lookup.
        // The frame and its depth: holes of the mirrored scene (geometry the game never drew) take the frame in the
        // mirrored direction where what is seen there is far away (then it lies like the sky, mirrored at the horizon).
        const bool stencil = m_ReflSkyRun && PackStencil() && CopyFrame();
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
            const bool night_water = NightNative() || m_NightGradeSeen;
            out[3] = m_ConfigReflectionDebug == 1 || m_ConfigReflectionDebug == 2 ? -1.0f :
                (night_water ? m_ConfigReflectionStrengthNight : m_ConfigReflectionStrength);
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
            // Mirrored lights (9.14a): boost, streak step (pixels at 1080 lines), light target bound.
            out[60] = m_ConfigReflectionLightBoost;
            out[61] = m_ConfigReflectionLightStreak;
            out[62] = lights_target ? 1.0f : 0.0f;
            out[63] = 0.0f;
            // Waves fixed on the water and sun glint (9.14b): two world axes on the water plane in view space with the
            // world coordinate of the camera along them (wrapped to the 32 m tile of the waves), the sun in view space.
            for (int i = 64; i < 92; ++i)
                out[i] = 0.0f;
            float rows[9] = {};
            float position[3] = {};
            const bool world_waves = m_ConfigReflectionWorldWaves != 0 && m_ConfigReflectionWaveSlope > 0.0f;
            const bool glint = m_ConfigReflectionSunGlint > 0.0f && !night_water;
            if ((world_waves || glint) && m_ReflWorldValid)
            {
                std::memcpy( rows, m_ReflWorldRows, sizeof( rows ) );
                std::memcpy( position, m_ReflWorldPosition, sizeof( position ) );
                // View -> world direction: x right + y up - z forward; world -> view: dot with each row, z negated.
                const auto view_to_world = [&]( const float * v, float * w ) {
                    for (int i = 0; i < 3; ++i)
                        w[i] = v[0] * rows[i] + v[1] * rows[3 + i] - v[2] * rows[6 + i];
                };
                const auto world_to_view = [&]( const float * w, float * v ) {
                    v[0] = w[0] * rows[0] + w[1] * rows[1] + w[2] * rows[2];
                    v[1] = w[0] * rows[3] + w[1] * rows[4] + w[2] * rows[5];
                    v[2] = -(w[0] * rows[6] + w[1] * rows[7] + w[2] * rows[8]);
                };
                float normal_world[3] = {};
                view_to_world( plane, normal_world );
                // Up of the world: the world axis closest to the water normal; the other two span the water (stable
                // from frame to frame, the fitted plane wobbles a little).
                int up = 0;
                for (int i = 1; i < 3; ++i)
                    if (std::abs( normal_world[i] ) > std::abs( normal_world[up] ))
                        up = i;
                const int axis_u = up == 0 ? 1 : 0;
                const int axis_v = up == 2 ? 1 : 2;
                if (world_waves && std::abs( normal_world[up] ) > 0.9f)
                {
                    float world_u[3] = {};
                    float world_v[3] = {};
                    world_u[axis_u] = 1.0f;
                    world_v[axis_v] = 1.0f;
                    world_to_view( world_u, out + 64 );
                    world_to_view( world_v, out + 68 );
                    const double tile = 32.0 * m_ConfigReflectionWaveSize;
                    out[67] = static_cast<float>(std::fmod( static_cast<double>(position[axis_u]), tile ));
                    out[71] = static_cast<float>(std::fmod( static_cast<double>(position[axis_v]), tile ));
                    out[79] = static_cast<float>(tile);
                    // Like the wave areas of GTA V: one main direction (wind, swell), the shorter waves spread more about it.
                    // Whole cycles per tile, so the pattern still repeats per tile; the direction is good to a few degrees.
                    const float cycles[4] = { 9.2f, 13.9f, 26.4f, 49.0f };
                    const float offsets[4] = { 0.0f, 22.0f, -28.0f, 50.0f };
                    const float main = m_ConfigReflectionWaveDirection * 0.01745329f;
                    for (int i = 0; i < 4; ++i)
                    {
                        const float angle = main + offsets[i] * m_ConfigReflectionWaveSpread * 0.01745329f;
                        float kx = std::round( cycles[i] * std::cos( angle ) );
                        float ky = std::round( cycles[i] * std::sin( angle ) );
                        if (kx == 0.0f && ky == 0.0f)
                            kx = 1.0f;
                        out[80 + i * 2] = kx;
                        out[81 + i * 2] = ky;
                    }
                    out[88] = std::cos( main );
                    out[89] = std::sin( main );
                    out[90] = m_ConfigReflectionWaveHeight;
                    out[76] = 1.0f;
                    out[77] = m_ConfigReflectionWaveSlope;
                    out[78] = 40.0f; // largest shift of the lookup (pixels at 1080 lines)
                }
                float sun[3] = {};
                const bool sun_found = ReadSunDirection( sun );
                if (glint && sun_found)
                {
                    world_to_view( sun, out + 72 );
                    out[75] = m_ConfigReflectionSunGlint;
                }
            }
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
        m_Context->PSSetShader( m_ReflCompositePs.Get(), nullptr, 0 );
        ID3D11Buffer * tint_buffer = m_MirrorConstants.Get();
        m_Context->PSSetConstantBuffers( 0, 1, &tint_buffer );
        ID3D11ShaderResourceView * reflection[7] = { m_ReflTargetSrv.Get(), m_ReflSkyView.Get(), m_ReflWeightSrv.Get(),
            stencil ? m_ReflMaskSrv.Get() : nullptr, stencil ? m_ReflFrameCopyView.Get() : nullptr, stencil ? m_ReflMaskDepthSrv.Get() : nullptr,
            lights_target ? m_ReflLightSrv.Get() : nullptr };
        m_Context->PSSetShaderResources( 0, 7, reflection );
        ID3D11SamplerState * sky_sampler = m_PresentSampler.Get();
        m_Context->PSSetSamplers( 1, 1, &sky_sampler );
        m_Context->Draw( 3, 0 );

        // The next draws set their own state; the target must not stay bound as a texture.
        ID3D11ShaderResourceView * none[7] = {};
        m_Context->PSSetShaderResources( 0, 7, none );
    }


    void RestoreGamePatches()
    {
        // Backwards, so an entry changed twice gets the game's own value.
        for (auto patch = g_GamePatches.rbegin(); patch != g_GamePatches.rend(); ++patch)
        {
            void * address = reinterpret_cast<void *>(patch->m_Address);
            DWORD old = 0;
            if (!::VirtualProtect( address, sizeof( patch->m_Original ), PAGE_EXECUTE_READWRITE, &old ))
                continue;
            std::memcpy( address, &patch->m_Original, sizeof( patch->m_Original ) );
            ::VirtualProtect( address, sizeof( patch->m_Original ), old, &old );
        }
        g_GamePatches.clear();
        ::FlushInstructionCache( ::GetCurrentProcess(), nullptr, 0 );
    }
}
