#pragma once

// HLSL sources, compiled at start up.
// The scene shader emulates the D3D7 fixed function pipeline for pre transformed
// vertices. Two texture stages, specular add, alpha test and fog.

namespace dx11z::shaders
{
    inline constexpr char SCENE[] = R"hlsl(
cbuffer DrawConstants : register( b0 )
{
    float4 g_Target; // width, height, 1 / width, 1 / height
    int4 g_ColorOp[2]; // op, arg1, arg2, unused
    int4 g_AlphaOp[2];
    float4 g_Factor;
    float4 g_FogColor;
    float4 g_FogParams; // start, end, density, table mode
    int4 g_Flags; // fog enable, specular enable, alpha func, texture mask
    float4 g_AlphaRef; // reference 0 to 1
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

    // D3D7 pixel centers sit on integers, D3D11 ones on halves.
    float2 ndc;
    ndc.x = ( input.m_Position.x + 0.5 ) * g_Target.z * 2.0 - 1.0;
    ndc.y = 1.0 - ( input.m_Position.y + 0.5 ) * g_Target.w * 2.0;

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
    float sx = c.x * g_MirrorViewport.x + ( g_MirrorViewport.z + 0.5 ) * c.z;
    float sy = c.y * g_MirrorViewport.y + ( g_MirrorViewport.w + 0.5 ) * c.z;
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
};

Texture2D g_Reflection : register( t0 );

float4 MainVS( uint id : SV_VertexID ) : SV_Position
{
    float2 corner = float2( ( id << 1 ) & 2, id & 2 );
    return float4( corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0 );
}

float4 MainPS( float4 position : SV_Position ) : SV_Target
{
    float4 colour = g_Reflection.Load( int3( position.xy, 0 ) );
    // Blended draws were laid over black, so the colour carries their coverage.
    float3 rgb = colour.a > 0.001 ? colour.rgb / colour.a : float3( 0.0, 0.0, 0.0 );
    // '[Water] ReflectionDebug' (strength sent negative): water mask without reflection in magenta.
    if ( g_Tint.a < 0.0 )
        return colour.a > 0.001 ? float4( saturate( rgb ), 1.0 ) : float4( 1.0, 0.0, 1.0, 1.0 );
    return float4( saturate( rgb ) * g_Tint.rgb, saturate( colour.a ) * g_Tint.a );
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