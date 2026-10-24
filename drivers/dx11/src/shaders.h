#pragma once

// HLSL sources, compiled at start up.
// The scene shader emulates the D3D7 fixed function pipeline for pre transformed
// vertices. Two texture stages, specular add, alpha test and fog.

namespace dx11z::shaders
{
    inline constexpr char SCENE[] = R"hlsl(
cbuffer DrawConstants : register( b0 )
{
    float4 g_Target; // vertex offset in game pixels (x, y), 1 / width, 1 / height of the game
    int4 g_ColorOp[2]; // op, arg1, arg2, unused
    int4 g_AlphaOp[2];
    float4 g_Factor;
    float4 g_FogColor;
    float4 g_FogParams; // start, end, density, table mode
    int4 g_Flags; // fog enable, specular enable, alpha func, texture mask
    float4 g_AlphaRef; // x: reference 0 to 1, y: shore fade ('[Water] ShoreFade', alpha below it transparent), z/w: water far fade start/end (w)
    float4 g_Debug; // x: tint for '[Dx11] Measure = 2', y: edge pass, z: edge reference, w: '[Dx11] AlphaMipScale'
    float4 g_Head; // '[Dx11] Headlights': x on, y scale x, z scale z, w scale y
    float4 g_HeadColour;
    float4 g_HeadProjection; // fx, fy, cx, cy of the game projection
    row_major float4x4 g_ViewToLight;
    int4 g_Extra; // x: number of other racers' headlights (PLAN.md 9.11)
    float4 g_ExtraColour[7];
    row_major float4x4 g_ExtraViewToLight[7];
    float4 g_HeadHigh; // high beam of the player's light: factors on the z, x and y scales (PLAN.md 9.10)
    int4 g_Rear; // x: number of tail light glows (PLAN.md 11.5)
    float4 g_RearShape; // rear offset, reach, lamp x, lamp spread
    float4 g_RearLeft[16]; // w: offset of this glow, 0 = g_RearShape.x (front glow of the turn signals, 11.5d)
    float4 g_RearRight[16];
    row_major float4x4 g_RearViewToLight[16];
    float4 g_Test; // test switches (BUGS.md B21): x '[Dx11] TestVertexAlpha'
    float4 g_Clip; // clip rectangle of the game in game pixels: left, top, right, bottom
};

Texture2D g_Texture0 : register( t0 );
Texture2D g_Texture1 : register( t1 );
SamplerState g_Sampler0 : register( s0 );
SamplerState g_Sampler1 : register( s1 );
Texture2D g_HeadMap : register( t2 );
SamplerState g_HeadSampler : register( s2 );

struct VertexIn
{
    float4 m_Position : POSITION;
    float4 m_Diffuse : COLOR0;
    float4 m_Specular : COLOR1;
    float2 m_Uv0 : TEXCOORD0;
    float2 m_Uv1 : TEXCOORD1;
};

struct PixelIn
{
    float4 m_Position : SV_Position;
    float4 m_Diffuse : COLOR0;
    float4 m_Specular : COLOR1;
    float4 m_Uv : TEXCOORD0;
    float m_Depth : TEXCOORD1;
    float3 m_Light : TEXCOORD2;
    float3 m_View : TEXCOORD3;
};

PixelIn MainVS( VertexIn input )
{
    PixelIn output;
    float rhw = input.m_Position.w;
    float w = ( rhw > 0.0 && rhw < 1e30 ) ? 1.0 / rhw : 1.0;

    // Game pixels to the render target; the offset (g_Target.xy) keeps clip edges on render pixel edges.
    // The game clips polygons a pixel inside its clip rectangle, the frame clears all of it: a sub view (rear view
    // mirror) kept a line of its clear colour at the edges. Corners that close to an edge go onto it.
    float2 at = input.m_Position.xy;
    if ( g_Clip.z > g_Clip.x + 4.0 && g_Clip.w > g_Clip.y + 4.0 )
    {
        at.x = at.x < g_Clip.x + 1.01 && at.x > g_Clip.x - 0.01 ? g_Clip.x : at.x;
        at.x = at.x > g_Clip.z - 1.01 && at.x < g_Clip.z + 0.01 ? g_Clip.z : at.x;
        at.y = at.y < g_Clip.y + 1.01 && at.y > g_Clip.y - 0.01 ? g_Clip.y : at.y;
        at.y = at.y > g_Clip.w - 1.01 && at.y < g_Clip.w + 0.01 ? g_Clip.w : at.y;
    }
    float2 ndc;
    ndc.x = ( at.x + g_Target.x ) * g_Target.z * 2.0 - 1.0;
    ndc.y = 1.0 - ( at.y + g_Target.y ) * g_Target.w * 2.0;

    output.m_Position = float4( ndc * w, saturate( input.m_Position.z ) * w, w );
    output.m_Diffuse = input.m_Diffuse.zyxw;
    output.m_Specular = input.m_Specular.zyxw;
    output.m_Uv = float4( input.m_Uv0, input.m_Uv1 );
    output.m_Depth = input.m_Position.z;

    // Headlight space of the vertex: undo the game projection (x = xv * fx / zv + cx), then the
    // camera and headlight matrices. Affine in the view, so the interpolation is exact per pixel.
    output.m_Light = float3( 0.0, 0.0, -1.0 );
    output.m_View = float3( 0.0, 0.0, 0.0 );
    if ( g_Head.x > 0.0 && rhw > 0.0 && rhw < 1e30 )
    {
        float4 view = float4( ( input.m_Position.x - g_HeadProjection.z ) * w / g_HeadProjection.x,
            ( input.m_Position.y - g_HeadProjection.w ) * w / g_HeadProjection.y, w, 1.0 );
        output.m_Light = mul( view, g_ViewToLight ).xyz;
        output.m_View = view.xyz;
    }

    return output;
}

// Water reflections (doc/BUGS.md B15, way E): the vertex is taken back to the camera space of the game
// (screen -> clip xc, yc, wc -> view), mirrored at the water plane and projected again. The part of the
// original below the water is clipped, the depth is an own one of the reflection target (1 - near / w).
cbuffer MirrorConstants : register( b1 )
{
    float4 g_MirrorPlane; // n.xyz, d in view space, above the water > 0
    float4 g_MirrorViewport; // fx, fy, cx, cy
    row_major float4x4 g_ViewOfClip;
    row_major float4x4 g_ClipOfView;
};

struct MirrorOut
{
    float4 m_Position : SV_Position;
    float4 m_Diffuse : COLOR0;
    float4 m_Specular : COLOR1;
    float4 m_Uv : TEXCOORD0;
    float m_Depth : TEXCOORD1;
    float3 m_Light : TEXCOORD2;
    float3 m_View : TEXCOORD3;
    float m_Clip : SV_ClipDistance0;
};

MirrorOut MirrorVS( VertexIn input )
{
    MirrorOut output;
    float rhw = input.m_Position.w;
    float w = ( rhw > 0.0 && rhw < 1e30 ) ? 1.0 / rhw : 1.0;

    float4 clip = float4( ( input.m_Position.x - g_MirrorViewport.z ) * w / g_MirrorViewport.x,
        ( input.m_Position.y - g_MirrorViewport.w ) * w / g_MirrorViewport.y, w, 1.0 );
    float3 view = mul( clip, g_ViewOfClip ).xyz;
    float above = dot( g_MirrorPlane.xyz, view ) + g_MirrorPlane.w;
    float3 mirrored = view - 2.0 * above * g_MirrorPlane.xyz;
    float3 c = mul( float4( mirrored, 1.0 ), g_ClipOfView ).xyz;

    // Screen x = xc / wc * fx + cx, as homogeneous coordinates so the hardware clips at the camera.
    float sx = c.x * g_MirrorViewport.x + ( g_MirrorViewport.z + g_Target.x ) * c.z;
    float sy = c.y * g_MirrorViewport.y + ( g_MirrorViewport.w + g_Target.y ) * c.z;
    output.m_Position = float4( sx * g_Target.z * 2.0 - c.z, c.z - sy * g_Target.w * 2.0, c.z - 0.05, c.z );
    output.m_Diffuse = input.m_Diffuse.zyxw;
    output.m_Specular = input.m_Specular.zyxw;
    output.m_Uv = float4( input.m_Uv0, input.m_Uv1 );
    output.m_Depth = input.m_Position.z;
    output.m_Light = float3( 0.0, 0.0, -1.0 );
    output.m_View = float3( 0.0, 0.0, 0.0 );
    output.m_Clip = above;
    return output;
}

float4 Argument( int arg, float4 current, float4 diffuse, float4 specular, float4 tex )
{
    float4 value;

    int source = arg & 0x0F;
    if ( source == 0 )
        value = diffuse;
    else if ( source == 1 )
        value = current;
    else if ( source == 2 )
        value = tex;
    else if ( source == 3 )
        value = g_Factor;
    else
        value = specular;

    if ( ( arg & 0x10 ) != 0 )
        value = 1.0 - value;
    if ( ( arg & 0x20 ) != 0 )
        value = value.aaaa;

    return value;
}

float4 Combine( int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 tex )
{
    switch ( op )
    {
    case 2:
        return a1;
    case 3:
        return a2;
    case 4:
        return a1 * a2;
    case 5:
        return a1 * a2 * 2.0;
    case 6:
        return a1 * a2 * 4.0;
    case 7:
        return a1 + a2;
    case 8:
        return a1 + a2 - 0.5;
    case 9:
        return ( a1 + a2 - 0.5 ) * 2.0;
    case 10:
        return a1 - a2;
    case 11:
        return a1 + a2 - a1 * a2;
    case 12:
        return lerp( a2, a1, diffuse.a );
    case 13:
        return lerp( a2, a1, tex.a );
    case 14:
        return lerp( a2, a1, g_Factor.a );
    case 15:
        return a1 + a2 * ( 1.0 - tex.a );
    case 16:
        return lerp( a2, a1, current.a );
    default:
        return current;
    }
}

float4 RunStage( int stage, float4 current, float4 diffuse, float4 specular, float4 tex )
{
    int4 color = g_ColorOp[stage];
    int4 alpha = g_AlphaOp[stage];

    float4 c1 = Argument( color.y, current, diffuse, specular, tex );
    float4 c2 = Argument( color.z, current, diffuse, specular, tex );
    float3 rgb = Combine( color.x, c1, c2, current, diffuse, tex ).rgb;

    float a = current.a;

    if ( alpha.x > 1 )
    {
        float4 a1 = Argument( alpha.y, current, diffuse, specular, tex );
        float4 a2 = Argument( alpha.z, current, diffuse, specular, tex );
        a = Combine( alpha.x, a1, a2, current, diffuse, tex ).a;
    }

    return saturate( float4( rgb, a ) );
}

bool AlphaPasses( float a )
{
    int func = g_Flags.z;
    float ref = g_AlphaRef.x;

    switch ( func )
    {
    case 1:
        return false;
    case 2:
        return a < ref;
    case 3:
        return a == ref;
    case 4:
        return a <= ref;
    case 5:
        return a > ref;
    case 6:
        return a != ref;
    case 7:
        return a >= ref;
    default:
        return true;
    }
}

float4 MainPS( PixelIn input ) : SV_Target
{
    float4 diffuse = input.m_Diffuse;
    if ( g_Test.x > 0 )
        diffuse.a = 1.0;
    float4 specular = input.m_Specular;

    // The game's headlight model per pixel (doc/LICHT.md 7.2): light map lookup in the ground
    // plane of the headlight, minus height, added to the vertex colour before the texture.
    if ( g_Head.x > 0.0 )
    {
        float iz = input.m_Light.z * g_Head.z * g_HeadHigh.x;
        float ix = input.m_Light.x * g_Head.y * g_HeadHigh.y;
        if ( iz > 0.5 && iz < 126.5 && abs( ix ) < 62.5 )
        {
            float map = g_HeadMap.SampleLevel( g_HeadSampler, float2( ( ix + 64.5 ) / 128.0, ( iz + 0.5 ) / 128.0 ), 0 ).r * 255.0;
            float factor = max( map - abs( input.m_Light.y * g_Head.w * g_HeadHigh.z ), 0.0 ) / 256.0;
            // Not clamped before the texture (PLAN.md 9.9b): with a coloured ambient (orange street light) a
            // channel near 1 would cap and the spot would turn to the headlight colour. RunStage clamps after
            // the texture, so it is texture x (ambient + headlight).
            diffuse.rgb = diffuse.rgb + g_HeadColour.rgb * factor;
        }

        // The other racers' headlights, same map and scales, each with its own matrix and colour.
        for ( int i = 0; i < g_Extra.x; ++i )
        {
            float3 light = mul( float4( input.m_View, 1.0 ), g_ExtraViewToLight[i] ).xyz;
            float jz = light.z * g_Head.z;
            float jx = light.x * g_Head.y;
            if ( jz > 0.5 && jz < 126.5 && abs( jx ) < 62.5 )
            {
                float map = g_HeadMap.SampleLevel( g_HeadSampler, float2( ( jx + 64.5 ) / 128.0, ( jz + 0.5 ) / 128.0 ), 0 ).r * 255.0;
                float factor = max( map - abs( light.y * g_Head.w ), 0.0 ) / 256.0;
                diffuse.rgb = diffuse.rgb + g_ExtraColour[i].rgb * factor;
            }
        }

        // Tail light glow: soft red light on the ground and walls just behind each car, per side (PLAN.md 11.5).
        for ( int r = 0; r < g_Rear.x; ++r )
        {
            float3 rear = mul( float4( input.m_View, 1.0 ), g_RearViewToLight[r] ).xyz;
            float d = rear.z - ( g_RearLeft[r].w > 0.0 ? g_RearLeft[r].w : g_RearShape.x );
            if ( d > -0.3 && d < g_RearShape.y * 6.0 )
            {
                float along = exp( -max( d, 0.0 ) / g_RearShape.y ) * saturate( ( d + 0.3 ) / 0.3 );
                float up = exp( -abs( rear.y ) / 1.2 );
                float spread = g_RearShape.w * ( 1.0 + max( d, 0.0 ) * 0.5 );
                float left = exp( -( rear.x - g_RearShape.z ) * ( rear.x - g_RearShape.z ) / ( spread * spread ) );
                float right = exp( -( rear.x + g_RearShape.z ) * ( rear.x + g_RearShape.z ) / ( spread * spread ) );
                diffuse.rgb = diffuse.rgb + ( g_RearLeft[r].rgb * left + g_RearRight[r].rgb * right ) * along * up;
            }
        }
    }

    float4 tex0 = ( g_Flags.w & 1 ) ? g_Texture0.Sample( g_Sampler0, input.m_Uv.xy ) : float4( 1, 1, 1, 1 );
    float4 tex1 = ( g_Flags.w & 2 ) ? g_Texture1.Sample( g_Sampler1, input.m_Uv.zw ) : float4( 1, 1, 1, 1 );

    // Shore textures fade out towards the water: the lower alpha range becomes transparent, the rest ramps up.
    if ( g_AlphaRef.y > 0.0 )
        tex0.a = saturate( ( tex0.a - g_AlphaRef.y ) / ( 1.0 - g_AlphaRef.y ) );

    // '[Dx11] AlphaMipScale' (BUGS.md B21): alpha-tested textures lose coverage in the smaller mip levels, so thin
    // cut-out edges open up in the distance and close again up close. Scale alpha with the mip level of texture 0.
    if ( g_Debug.w > 0 && g_Flags.z != 8 && ( g_Flags.w & 1 ) )
    {
        float2 size;
        g_Texture0.GetDimensions( size.x, size.y );
        float2 texel_dx = ddx( input.m_Uv.xy * size );
        float2 texel_dy = ddy( input.m_Uv.xy * size );
        float lod = max( 0.5 * log2( max( dot( texel_dx, texel_dx ), dot( texel_dy, texel_dy ) ) ), 0.0 );
        tex0.a *= 1.0 + lod * g_Debug.w;
    }

    float4 current = diffuse;

    if ( g_ColorOp[0].x > 1 )
    {
        current = RunStage( 0, current, diffuse, specular, tex0 );
        if ( g_ColorOp[1].x > 1 )
            current = RunStage( 1, current, diffuse, specular, tex1 );
    }

    if ( g_Flags.y != 0 )
        current.rgb = saturate( current.rgb + specular.rgb );

    // Water fades out towards the far end of its surface ('[Water] FarFade'), the sea of the sky shows through.
    if ( g_AlphaRef.w > g_AlphaRef.z && g_AlphaRef.w > 0.0 )
        current.a *= saturate( ( g_AlphaRef.w - input.m_Position.w ) / ( g_AlphaRef.w - g_AlphaRef.z ) );

    if ( !AlphaPasses( current.a ) )
        discard;

    // '[Dx11] AlphaEdgeFix', pass 1 keeps the opaque part, pass 2 the faint edge.
    if ( g_Debug.y == 1 && current.a < g_Debug.z )
        discard;
    if ( g_Debug.y == 2 && current.a >= g_Debug.z )
        discard;

    if ( g_Flags.x != 0 )
    {
        float fog;
        int mode = (int)g_FogParams.w;
        float depth = saturate( input.m_Depth );

        if ( mode == 1 )
            fog = exp( -g_FogParams.z * depth );
        else if ( mode == 2 )
            fog = exp( -( g_FogParams.z * depth ) * ( g_FogParams.z * depth ) );
        else if ( mode == 3 )
            fog = ( g_FogParams.y - depth ) / max( g_FogParams.y - g_FogParams.x, 1e-6 );
        else
            fog = specular.a;
        current.rgb = lerp( g_FogColor.rgb, current.rgb, saturate( fog ) );
    }

    if ( g_Debug.x > 0 )
        current.rgb = lerp( current.rgb, float3( 1, 0, 1 ), 0.7 );

    return current;
}
)hlsl";

    // Copies a rectangle of one texture into a rectangle of a target, used for present,
    // for scaling the frame to the game resolution on lock and back on write.
    inline constexpr char PRESENT[] = R"hlsl(
cbuffer PresentConstants : register( b0 )
{
    float4 g_Rect; // destination x, y, width, height in target pixels
    float4 g_Params; // brightness, target width, target height, unused
    float4 g_Source; // source u, v, width, height in texture coordinates
};

Texture2D g_Texture : register( t0 );
SamplerState g_Sampler : register( s0 );

struct PixelIn
{
    float4 m_Position : SV_Position;
    float2 m_Uv : TEXCOORD0;
};

PixelIn MainVS( uint id : SV_VertexID )
{
    float2 corner = float2( ( id << 1 ) & 2, id & 2 ) * 0.5;
    float2 pixel = g_Rect.xy + corner * g_Rect.zw;
    PixelIn output;
    output.m_Position = float4( pixel.x / g_Params.y * 2.0 - 1.0, 1.0 - pixel.y / g_Params.z * 2.0, 0.0, 1.0 );
    output.m_Uv = g_Source.xy + corner * g_Source.zw;
    return output;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    float4 color = g_Texture.Sample( g_Sampler, input.m_Uv );
    // THRASH state 2E scales the display gamma ramp.
    color.rgb = saturate( color.rgb * g_Params.x );
    return color;
}
)hlsl";

    // Lays the reflection target over the water pixels (stencil test outside), with tint and strength.
    inline constexpr char REFLECT[] = R"hlsl(
cbuffer ReflectConstants : register( b0 )
{
    float4 g_Tint; // rgb tint, a strength
    float4 g_Plane; // water plane in view space, n.xyz, d
    float4 g_Viewport; // fx, fy, cx, cy of the game
    float4 g_Sky; // x: sky copy valid, y: time in seconds, z: wave amplitude, w: blur radius (pixels at 1080 lines)
    row_major float4x4 g_ViewOfClip;
    row_major float4x4 g_ClipOfView;
    float4 g_Fresnel; // x: share of the reflection looking straight down (1 = no Fresnel), y: weight target bound, z/w: water far fade start/end (w)
    float4 g_Edge; // x: edge fade radius (pixels at 1080 lines, 0 = off), y: 'ReflectionDebug' 3 or 4, z: frame, depth and stencil copies bound, w: strength looking along the water
    float4 g_Size; // xy: render size (the reflection target may be smaller), z: depth scale n of z = 1 - n / w
};

Texture2D g_Reflection : register( t0 );
Texture2D g_SkyFrame : register( t1 ); // the sky alone, drawn again from the sky phase
SamplerState g_SkySampler : register( s1 );
Texture2D<float> g_Weight : register( t2 ); // 1 water, 1 - alpha on the shore
// Stencil of the frame: bit 1 water, bit 2 solid draw of the render queue; 0 = only the sky drawn there.
// The frame before the composite and its depth (z = 1 - n / w, n in g_Size.z).
Texture2D g_Frame : register( t4 );
#ifdef STENCIL_MS
Texture2DMS<uint2> g_Stencil : register( t3 );
Texture2DMS<float> g_Depth : register( t5 );
uint StencilAt( int2 p ) { return g_Stencil.Load( p, 0 ).g; }
float DepthAt( int2 p ) { return g_Depth.Load( p, 0 ); }
#else
Texture2D<uint2> g_Stencil : register( t3 );
Texture2D<float> g_Depth : register( t5 );
uint StencilAt( int2 p ) { return g_Stencil.Load( int3( p, 0 ) ).g; }
float DepthAt( int2 p ) { return g_Depth.Load( int3( p, 0 ) ); }
#endif

// The sky is far away: its reflection only depends on the direction. The view ray of the pixel, mirrored at the
// water plane, projected back onto the screen; above the top edge the top row of the sky copy is used.
float3 ReflectedSky( float2 pixel, float2 size, out float found, out float sky_v )
{
    found = 0.0;
    sky_v = -1.0;
    float2 game = pixel * ( 2.0 * g_Viewport.zw ) / size;
    float3 ray = mul( float4( ( game.x - g_Viewport.z ) / g_Viewport.x, ( game.y - g_Viewport.w ) / g_Viewport.y, 1.0, 0.0 ), g_ViewOfClip ).xyz;
    float facing = dot( ray, g_Plane.xyz );
    if ( facing >= 0.0 )
        return float3( 0.0, 0.0, 0.0 );
    float3 mirrored = ray - 2.0 * facing * g_Plane.xyz;
    float3 c = mul( float4( mirrored, 0.0 ), g_ClipOfView ).xyz;
    float2 uv;
    if ( c.z > 1e-4 )
        uv = float2( c.x / c.z * g_Viewport.x + g_Viewport.z, c.y / c.z * g_Viewport.y + g_Viewport.w ) / ( 2.0 * g_Viewport.zw );
    else
        uv = float2( 0.5, -1.0 ); // straight up or behind: the top of the sky
    // Across for the band above the top edge by the angle of the direction, the same as uv.x in the middle;
    // c.x / c.z jumps where the ray turns past the side of the camera (a line moving over the water).
    float band_x = 0.5 + atan2( c.x, c.z ) * g_Viewport.x / ( 2.0 * g_Viewport.z );
    found = 1.0;
    sky_v = uv.y;
    // Above the top edge nothing was drawn: instead of repeating the top row, fade into a blurred band of the
    // upper sky (a few rows down, the very top may be empty).
    float3 near = g_SkyFrame.SampleLevel( g_SkySampler, saturate( uv ), 0 ).rgb;
    // What the frame shows there, if it is far (over 12 times the height of the camera above the water): mirrored
    // it lies close to where the sky lies, like the rock behind a hole of the mirrored scene. Near things (flags
    // on the promenade) would land in the wrong place, there the sky alone.
    if ( g_Edge.z > 0.5 && uv.y >= 0.0 && uv.y <= 1.0 && uv.x >= 0.0 && uv.x <= 1.0 )
    {
        int2 q = clamp( int2( uv * size ), int2( 0, 0 ), int2( size ) - 1 );
        // Only solid things: where the depth is still cleared it is sky, a flag laid over it without depth stays out.
        float z = DepthAt( q );
        float w = g_Size.z / max( 1.0 - z, 1e-6 );
        float distant = z < 0.99999 ? saturate( ( w / max( g_Plane.w, 0.1 ) - 12.0 ) / 12.0 ) : 0.0;
        // Out towards the edges of the frame, so the switch to the sky alone beyond them leaves no line.
        distant *= saturate( uv.y * 10.0 ) * saturate( ( 1.0 - uv.y ) * 10.0 ) * saturate( uv.x * 10.0 ) * saturate( ( 1.0 - uv.x ) * 10.0 );
        near = lerp( near, g_Frame.Load( int3( q, 0 ) ).rgb, distant );
    }
    float3 band = float3( 0.0, 0.0, 0.0 );
    [unroll] for ( int k = 0; k < 6; ++k )
    {
        [unroll] for ( int j = 0; j < 3; ++j )
            band += g_SkyFrame.SampleLevel( g_SkySampler, saturate( float2( band_x + ( k - 2.5 ) * 0.025, 0.02 + j * 0.04 ) ), 0 ).rgb;
    }
    band /= 18.0;
    return lerp( near, band, saturate( ( 0.2 - uv.y ) / 0.4 ) );
}

float4 MainVS( uint id : SV_VertexID ) : SV_Position
{
    float2 corner = float2( ( id << 1 ) & 2, id & 2 );
    return float4( corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0 );
}

float4 MainPS( float4 position : SV_Position ) : SV_Target
{
    float2 size = g_Size.xy;
    float scale = size.y / 1080.0;

    float2 p = position.xy;

    // Not water, but seen through the water plane (deeper than where the ray meets it): a hole in the water
    // geometry or the end of it under the water, gets the reflection too. The fitted plane is off by some
    // tenths of a metre near and more far away: only from 1 m plus 2 % of the distance under it, fading in
    // over as much again.
    float hole = 0.0;
    uint here = g_Edge.z > 0.5 ? StencilAt( int2( p ) ) : 1u;
    if ( ( here & 1 ) == 0 )
    {
        // Drawn without depth (B21): the depth is of what lies behind it.
        if ( ( here & 4 ) != 0 )
            discard;
        float2 at = p * ( 2.0 * g_Viewport.zw ) / size;
        float3 ray_at = mul( float4( ( at.x - g_Viewport.z ) / g_Viewport.x, ( at.y - g_Viewport.w ) / g_Viewport.y, 1.0, 0.0 ), g_ViewOfClip ).xyz;
        float down_at = -dot( ray_at, g_Plane.xyz );
        float z = DepthAt( int2( p ) );
        if ( down_at <= 1e-5 || z >= 0.99999 )
            discard;
        float w = g_Size.z / max( 1.0 - z, 1e-6 );
        float margin = 1.0 + 0.02 * w;
        hole = saturate( ( ( w - g_Plane.w / down_at ) * down_at - margin ) / margin );
        if ( hole <= 0.0 )
            discard;
        if ( g_Edge.y > 3.5 )
            return float4( 1.0, 0.0, 0.0, hole );
    }

    // Small waves: the lookup swings a little sideways and up and down, slowly moving.
    float t = g_Sky.y;
    float2 wave = float2( sin( p.y * 0.09 / scale + t * 0.8 ) + 0.5 * sin( p.y * 0.23 / scale + p.x * 0.01 / scale - t * 1.1 ),
        0.6 * sin( p.x * 0.05 / scale + p.y * 0.04 / scale + t * 0.6 ) ) * g_Sky.z * scale;
    float2 uv = ( p + wave ) / size;

    // Soft blur, five taps (the colour is premultiplied by the coverage, so it averages right).
    float2 r = g_Sky.w * scale / size;
    float4 colour = g_Reflection.SampleLevel( g_SkySampler, uv, 0 ) * 0.4 +
        ( g_Reflection.SampleLevel( g_SkySampler, uv + float2( r.x, 0.0 ), 0 ) + g_Reflection.SampleLevel( g_SkySampler, uv - float2( r.x, 0.0 ), 0 ) +
          g_Reflection.SampleLevel( g_SkySampler, uv + float2( 0.0, r.y ), 0 ) + g_Reflection.SampleLevel( g_SkySampler, uv - float2( 0.0, r.y ), 0 ) ) * 0.15;
    // At the waterline the waves and the blur leave a gap of a few pixels between the object and its mirror
    // image: the mirror image reaches up to it (looked up below, where the mirrored object continues).
    if ( colour.a < 0.999 )
    {
        [unroll] for ( int k = 1; k <= 3; ++k )
        {
            float4 below = g_Reflection.SampleLevel( g_SkySampler, uv + float2( 0.0, k * 1.5 * scale / size.y ), 0 );
            colour += below * ( 1.0 - saturate( colour.a ) );
        }
    }
    // Blended draws were laid over black, so the colour carries their coverage.
    float3 rgb = colour.a > 0.001 ? colour.rgb / colour.a : float3( 0.0, 0.0, 0.0 );
    // Not covered by the mirrored scene (it was never drawn): the reflected sky fills the rest.
    bool sky_filled = false;
    if ( g_Sky.x > 0.5 )
    {
        float found;
        float sky_v;
        float3 sky = ReflectedSky( p + wave, size, found, sky_v );
        if ( found > 0.5 )
        {
            // The game draws only what is in the frame: the mirrored scene ends at the mirror image of the top
            // edge (a line moving over the water). It fades into the sky before it gets there.
            float a = saturate( colour.a ) * saturate( sky_v / 0.12 );
            rgb = saturate( rgb ) * a + sky * ( 1.0 - a );
            sky_filled = a < 0.999;
            colour.a = 1.0;
        }
    }
    // '[Water] ReflectionDebug' (strength sent negative): water mask without reflection in magenta,
    // filled with the reflected sky in cyan.
    if ( g_Tint.a < 0.0 && sky_filled )
        return float4( 0.0, 1.0, 1.0, 1.0 );
    if ( g_Tint.a < 0.0 )
        return colour.a > 0.001 ? float4( saturate( rgb ), 1.0 ) : float4( 1.0, 0.0, 1.0, 1.0 );
    // Fresnel: water looked at from above mirrors little, at a flat angle a lot. Also hides that the sky
    // straight above (mirrored when looking steeply down) was never drawn.
    float2 game = p * ( 2.0 * g_Viewport.zw ) / size;
    float3 ray = normalize( mul( float4( ( game.x - g_Viewport.z ) / g_Viewport.x, ( game.y - g_Viewport.w ) / g_Viewport.y, 1.0, 0.0 ), g_ViewOfClip ).xyz );
    float steep = saturate( -dot( ray, g_Plane.xyz ) );
    float fresnel = lerp( g_Fresnel.x, 1.0, pow( 1.0 - steep, 3.0 ) );
    // Same far fade as the water itself: the depth w of the water point is where the ray meets the plane.
    if ( g_Fresnel.w > g_Fresnel.z && g_Fresnel.w > 0.0 )
    {
        float3 ray_w = mul( float4( ( game.x - g_Viewport.z ) / g_Viewport.x, ( game.y - g_Viewport.w ) / g_Viewport.y, 1.0, 0.0 ), g_ViewOfClip ).xyz;
        float down = -dot( ray_w, g_Plane.xyz );
        float water_w = down > 1e-5 ? g_Plane.w / down : 1e9;
        fresnel *= saturate( ( g_Fresnel.w - water_w ) / ( g_Fresnel.w - g_Fresnel.z ) );
    }
    float weight = g_Fresnel.y > 0.5 ? g_Weight.Load( int3( position.xy, 0 ) ) : 1.0;
    float3 mirrored = saturate( rgb ) * g_Tint.rgb;
    float alpha = saturate( colour.a ) * g_Tint.a * fresnel * weight;
    // Looking along the water it mirrors almost everything ('[Water] ReflectionGrazing'): far water takes the
    // colour of the reflected sky, like the far sea of the game next to it.
    alpha = lerp( alpha, saturate( colour.a ) * max( g_Tint.a, g_Edge.w ) * weight, pow( 1.0 - steep, 8.0 ) );
    // The hole shows ground, not the water: the water around it (frame before the composite) with the
    // reflection over it as on the water, laid over the hole.
    if ( hole > 0.0 )
    {
        float3 base = float3( 0.0, 0.0, 0.0 );
        float found_water = 0.0;
        [unroll] for ( int ring = 1; ring <= 4; ++ring )
        {
            [unroll] for ( int k = 0; k < 8; ++k )
            {
                float angle = ( k + ( ring & 1 ) * 0.5 ) * 0.7853982;
                int2 q = clamp( int2( p + float2( cos( angle ), sin( angle ) ) * ring * 8.0 * scale ), int2( 0, 0 ), int2( size ) - 1 );
                if ( ( StencilAt( q ) & 1 ) != 0 )
                {
                    base += g_Frame.Load( int3( q, 0 ) ).rgb;
                    found_water += 1.0;
                }
            }
        }
        if ( found_water > 0.0 )
        {
            mirrored = lerp( base / found_water, mirrored, alpha );
            alpha = hole;
        }
        else
        {
            alpha = max( alpha, 0.85 ) * hole;
        }
        return float4( mirrored, alpha );
    }

    // Where the water surface ends against open sky (map end, sea painted into the sky), the water with its
    // reflection fades into what lies behind it, the sky image at this pixel. Share of open sky in a disc
    // around the pixel: half at the edge.
    if ( g_Edge.x > 0.0 )
    {
        float radius = g_Edge.x * scale;
        int2 last = int2( size ) - 1;
        float open = 0.0;
        [unroll] for ( int ring = 1; ring <= 6; ++ring )
        {
            [unroll] for ( int k = 0; k < 12; ++k )
            {
                float angle = ( k + ( ring & 1 ) * 0.5 ) * 0.5235988;
                int2 q = clamp( int2( p + float2( cos( angle ), sin( angle ) ) * radius * ( ring / 6.0 ) ), int2( 0, 0 ), last );
                if ( ( StencilAt( q ) & 3 ) == 0 )
                    open += 1.0;
            }
        }
        float edge = open > 0.0 ? smoothstep( 0.0, 1.0, saturate( open / 72.0 * 2.0 ) ) : 0.0;
        if ( g_Edge.y > 2.5 && g_Edge.y < 3.5 )
            return float4( edge, 0.0, 1.0 - edge, 1.0 );
        if ( edge > 0.0 )
        {
            float3 beyond = g_SkyFrame.Load( int3( p, 0 ) ).rgb;
            // frame * (1 - a) + mirrored * a, then towards the sky by edge: one blend with alpha a + edge - a * edge.
            float total = alpha + edge - alpha * edge;
            mirrored = ( mirrored * alpha * ( 1.0 - edge ) + beyond * edge ) / max( total, 1e-4 );
            alpha = total;
        }
    }
    return float4( mirrored, alpha );
}
)hlsl";

    inline constexpr char CLEAR[] = R"hlsl(
cbuffer ClearConstants : register( b0 )
{
    float4 g_Rect; // x, y, width, height in target pixels
    float4 g_Target; // target width, height, depth value, unused
    float4 g_Color;
};

struct PixelIn
{
    float4 m_Position : SV_Position;
};

PixelIn MainVS( uint id : SV_VertexID )
{
    float2 corner = float2( ( id << 1 ) & 2, id & 2 ) * 0.5;
    float2 pixel = g_Rect.xy + corner * g_Rect.zw;
    PixelIn output;
    output.m_Position = float4( pixel.x / g_Target.x * 2.0 - 1.0, 1.0 - pixel.y / g_Target.y * 2.0, g_Target.z, 1.0 );
    return output;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    return g_Color;
}
)hlsl";
}