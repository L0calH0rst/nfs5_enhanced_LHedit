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
    float4 g_HeadHigh; // high beam of the player's light: factors on the z, x and y scales (PLAN.md 9.10)
    int4 g_Rear; // x: number of tail light glows (PLAN.md 11.5)
    float4 g_RearShape; // rear offset, reach, lamp x, lamp spread
    float4 g_Test; // test switches (BUGS.md B21): x '[Dx11] TestVertexAlpha', y '[Weather] NightEdgeLight'
    float4 g_Clip; // clip rectangle of the game in game pixels: left, top, right, bottom
    float4 g_Night; // rgb: factor, w: 0 off, 1 world, 2 sky, 3 light sprites (LFSP LfspNightGradeEx)
    float4 g_Weather; // x snow white amount (0 off), y desaturation, z slope (0 uniform, 1 only faces up), w test view (LFSP LfspWeatherFx)
    float4 g_Weather2; // x fog exponent (storm, 1 = as the game), yzw snow colour
    row_major float4x4 g_SnowToWorld; // screen side (xc, yc, wc, 1) to track space, for the slope of the snow
    // The light arrays come last (the driver uploads only the used entries of each).
    float4 g_ExtraColour[16];
    row_major float4x4 g_ExtraViewToLight[16];
    float4 g_RearLeft[16]; // w: offset of this glow, 0 = g_RearShape.x (front glow of the turn signals, 11.5d)
    float4 g_RearRight[16];
    row_major float4x4 g_RearViewToLight[16];
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
    float m_EyeDepth : TEXCOORD4; // view depth w for the exponential fog
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
    output.m_EyeDepth = w;

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
    float m_EyeDepth : TEXCOORD4;
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
    output.m_EyeDepth = c.z; // the fog of the reflected point
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

float4 Pipeline( float4 diffuse, float4 specular, float4 tex0, float4 tex1 )
{
    float4 current = diffuse;
    if ( g_ColorOp[0].x > 1 )
    {
        current = RunStage( 0, current, diffuse, specular, tex0 );
        if ( g_ColorOp[1].x > 1 )
            current = RunStage( 1, current, diffuse, specular, tex1 );
    }
    if ( g_Flags.y != 0 )
        current.rgb = saturate( current.rgb + specular.rgb );
    return current;
}

#ifdef EARLYZ
[earlydepthstencil]
#endif
float4 MainPS( PixelIn input ) : SV_Target
{
    float4 diffuse = input.m_Diffuse;
    if ( g_Test.x > 0 )
        diffuse.a = 1.0;
    float4 specular = input.m_Specular;
    bool nightWorld = g_Night.w > 0.5 && g_Night.w < 1.5;
    float3 nightMul = nightWorld ? g_Night.rgb : float3( 1.0, 1.0, 1.0 );
    float4 diffuseBase = diffuse; // without headlights: the night factor only darkens this part

    float4 tex0 = ( g_Flags.w & 1 ) ? g_Texture0.Sample( g_Sampler0, input.m_Uv.xy ) : float4( 1, 1, 1, 1 );
    float4 tex1 = ( g_Flags.w & 2 ) ? g_Texture1.Sample( g_Sampler1, input.m_Uv.zw ) : float4( 1, 1, 1, 1 );

    // Snow white (LFSP SnowWhite, g_Weather.x > 0, track draws only): the vertices carry no normals, the face normal comes
    // from the screen derivatives of the position in track space. Facing the camera, y = up. Before any discard: the
    // derivatives need all pixels of the quad.
    float snowUp = 1.0;
    float3 snowNormal = float3( 0.0, 1.0, 0.0 );
#ifndef MIRROR_PASS
    if ( g_Weather.x > 0.0 && ( g_Weather.z > 0.0 || g_Weather.w > 0.5 ) && g_Head.x > 0.0 && g_SnowToWorld[3].w > 0.5 )
    {
        float3 p = mul( float4( input.m_View, 1.0 ), g_SnowToWorld ).xyz;
        float3 n = cross( ddx( p ), ddy( p ) );
        float len = length( n );
        if ( len > 1e-12 )
        {
            n /= len;
            if ( dot( n, g_SnowToWorld[3].xyz - p ) < 0.0 )
                n = -n;
            snowNormal = n;
            snowUp = smoothstep( 0.35, 0.85, n.y );
        }
    }
#endif

    // Shore textures fade out towards the water: the lower alpha range becomes transparent, the rest ramps up.
    if ( g_AlphaRef.y > 0.0 )
    {
        tex0.a = saturate( ( tex0.a - g_AlphaRef.y ) / ( 1.0 - g_AlphaRef.y ) );
        // B33 ('[Water] ShoreWaterLight'): the see-through part takes the light colour of the water, the opaque ground keeps
        // its own; at dusk the shore is lit warm and bright, the water cool and dark, and the edge between them stood out.
        if ( g_Test.w > 0.0 )
        {
            uint packed = (uint)( g_Test.z + 0.5 );
            float3 water = float3( ( packed >> 16 ) & 255, ( packed >> 8 ) & 255, packed & 255 ) / 255.0;
            diffuseBase.rgb = lerp( diffuseBase.rgb, water, g_Test.w * ( 1.0 - tex0.a ) );
        }
    }

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

    // The world without any light: its alpha (no light changes the alpha) decides every discard, so the lights below run
    // only for pixels that stay. The edge passes of '[Dx11] AlphaEdgeFix' and the alpha test throw most pixels of a leaf away.
    float4 baseColour = Pipeline( diffuseBase, specular, tex0, tex1 );
    float alpha = baseColour.a;

    // Water fades out towards the far end of its surface ('[Water] FarFade'), the sea of the sky shows through.
    if ( g_AlphaRef.w > g_AlphaRef.z && g_AlphaRef.w > 0.0 )
        alpha *= saturate( ( g_AlphaRef.w - input.m_Position.w ) / ( g_AlphaRef.w - g_AlphaRef.z ) );

    if ( g_Debug.y == 3 )
    {
        // Alpha to coverage (AlphaEdgeFix = 3): the alpha is the coverage of the samples, only what is almost clear goes.
        if ( alpha < 0.02 )
            discard;
    }
    else
    {
        if ( !AlphaPasses( alpha ) )
            discard;

        // '[Dx11] AlphaEdgeFix', pass 1 keeps the opaque part, pass 2 the faint edge.
        if ( g_Debug.y == 1 && alpha < g_Debug.z )
            discard;
        if ( g_Debug.y == 2 && alpha >= g_Debug.z )
            discard;
    }

    float4 current = baseColour;
    float3 nightLight = float3( 0.0, 0.0, 0.0 ); // night: the light of the lights on the darkened world

    // MIRROR_PASS ('[Water] ReflectionSimpleShader'): the mirrored scene of the water has no light loops.
#ifndef MIRROR_PASS
    // The game's headlight model per pixel (doc/LICHT.md 7.2): light map lookup in the ground
    // plane of the headlight, minus height, added to the vertex colour before the texture.
    if ( g_Head.x > 0.0 )
    {
        float3 lightAdd = float3( 0.0, 0.0, 0.0 );
        float iz = input.m_Light.z * g_Head.z * g_HeadHigh.x;
        float ix = input.m_Light.x * g_Head.y * g_HeadHigh.y;
        if ( iz > 0.5 && iz < 126.5 && abs( ix ) < 62.5 )
        {
            float map = g_HeadMap.SampleLevel( g_HeadSampler, float2( ( ix + 64.5 ) / 128.0, ( iz + 0.5 ) / 128.0 ), 0 ).r * 255.0;
            float factor = max( map - abs( input.m_Light.y * g_Head.w * g_HeadHigh.z ), 0.0 ) / 256.0;
            // Not clamped before the texture (PLAN.md 9.9b): with a coloured ambient (orange street light) a
            // channel near 1 would cap and the spot would turn to the headlight colour. RunStage clamps after
            // the texture, so it is texture x (ambient + headlight).
            lightAdd += g_HeadColour.rgb * factor;
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
                lightAdd += g_ExtraColour[i].rgb * factor;
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
                lightAdd += ( g_RearLeft[r].rgb * left + g_RearRight[r].rgb * right ) * along * up;
            }
        }

        // Night (PLAN.md 9.14a test): the light goes onto the vertex colour already darkened by the night factor. On the
        // day colour a white texture (road markings) was at 1 before any light, the clamp ate the light part.
        if ( nightWorld )
        {
            float4 dark = float4( diffuseBase.rgb * nightMul, diffuseBase.a );
            nightLight = Pipeline( float4( dark.rgb + lightAdd, dark.a ), specular, tex0, tex1 ).rgb - Pipeline( dark, specular, tex0, tex1 ).rgb;
        }
        else
            current = Pipeline( float4( diffuse.rgb + lightAdd, diffuse.a ), specular, tex0, tex1 );
    }
#endif

    // Night over a day track (LFSP NightGrade): result = night factor x (world without lights) + the light of the lights.
    // Dividing the lights by the factor before the texture clipped them, so the base pass and the lit pass run apart.
    // '[Weather] NightEdgeLight' (g_Test.y): on alpha tested textures the faint edge (alpha below the edge reference) keeps
    // the colour of its padding, bright texels; lit by the headlights they showed a yellow rim around leaves. The light
    // part fades out with the alpha there.
#ifdef MIRROR_PASS
    bool edgeLight = false;
#else
    bool edgeLight = g_Test.y > 0.0 && g_Head.x > 0.0 && g_Flags.z != 8;
#endif
    if ( nightWorld || edgeLight )
    {
        float3 lightPart = nightWorld ? nightLight : current.rgb - baseColour.rgb;
        if ( edgeLight )
            lightPart *= saturate( baseColour.a / max( g_Debug.z, 0.004 ) );
        current.rgb = baseColour.rgb * nightMul + lightPart;
    }
    current.a = alpha;

    // Snow white: a little desaturated, then mixed towards the snow colour (shaded by the vertex colour of the game, and
    // by the night or storm factor of the world). With the slope (g_Weather.z) mostly on faces that point up.
    if ( g_Weather.x > 0.0 )
    {
        const float3 lumWeights = float3( 0.299, 0.587, 0.114 );
        current.rgb = lerp( current.rgb, dot( current.rgb, lumWeights ).xxx, g_Weather.y );
        float shade = saturate( dot( diffuseBase.rgb, lumWeights ) * 1.2 + 0.2 );
        float amount = g_Weather.x * lerp( 1.0, snowUp, saturate( g_Weather.z ) );
        current.rgb = lerp( current.rgb, g_Weather2.yzw * shade * nightMul, amount );
    }

    if ( g_Flags.x != 0 )
    {
        float fog;
        int mode = (int)g_FogParams.w;
        float depth = saturate( input.m_Depth );
        float eyeDepth = max( input.m_EyeDepth, 0.0 );

        if ( mode == 1 )
            fog = exp( -g_FogParams.z * eyeDepth );
        else if ( mode == 2 )
            fog = exp( -( g_FogParams.z * eyeDepth ) * ( g_FogParams.z * eyeDepth ) );
        else if ( mode == 3 )
            fog = ( g_FogParams.y - depth ) / max( g_FogParams.y - g_FogParams.x, 1e-6 );
        else
            fog = specular.a;
        // Storm (LFSP StormFog): the share seen through the fog to a power, thicker fog at the same start.
        if ( g_Weather2.x > 1.0 )
            fog = pow( saturate( fog ), g_Weather2.x );
        current.rgb = lerp( g_FogColor.rgb * nightMul, current.rgb, saturate( fog ) );
    }

    // Night, sky (2) and light sprites (3): the factor of the draw.
    if ( g_Night.w > 1.5 )
        current.rgb *= g_Night.rgb;

    // '[Weather] SnowDebug': 1 = snow share as grey (white = full snow), 2 = face normal in track space as colour.
    if ( g_Weather.w > 1.5 )
        current.rgb = snowNormal * 0.5 + 0.5;
    else if ( g_Weather.w > 0.5 )
        current.rgb = ( g_Weather.x > 0.0 ? lerp( 1.0, snowUp, saturate( g_Weather.z ) ) : 0.0 ).xxx;

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

    // Same vertex stage as PRESENT. The pixel stage enlarges a small movie frame with a bicubic
    // filter from the Mitchell Netravali family, B = 1 - 2 C, so C = 0.5 is Catmull Rom, 1/3 is
    // Mitchell and 0 is the soft B spline. 'g_Params.w' is C.
    inline constexpr char MOVIE[] = R"hlsl(
cbuffer PresentConstants : register( b0 )
{
    float4 g_Rect;
    float4 g_Params; // brightness, target width, target height, bicubic C
    float4 g_Source; // source u, v, width, height in texture coordinates
};

Texture2D g_Texture : register( t0 );

struct PixelIn
{
    float4 m_Position : SV_Position;
    float2 m_Uv : TEXCOORD0;
};

float4 Weights( float t, float c )
{
    const float b = 1.0 - 2.0 * c;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float4 w = float4(
        ( -b / 6.0 - c ) * t3 + ( b / 2.0 + 2.0 * c ) * t2 + ( -b / 2.0 - c ) * t + b / 6.0,
        ( 2.0 - 1.5 * b - c ) * t3 + ( -3.0 + 2.0 * b + c ) * t2 + ( 1.0 - b / 3.0 ),
        ( -2.0 + 1.5 * b + c ) * t3 + ( 3.0 - 2.5 * b - 2.0 * c ) * t2 + ( b / 2.0 + c ) * t + b / 6.0,
        ( b / 6.0 + c ) * t3 - c * t2 );
    return w;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    uint width, height;
    g_Texture.GetDimensions( width, height );
    const float2 size = float2( width, height );
    const float2 position = input.m_Uv * size - 0.5;
    const float2 base = floor( position );
    const float2 fraction = position - base;
    const float4 wx = Weights( fraction.x, g_Params.w );
    const float4 wy = Weights( fraction.y, g_Params.w );
    // Rows outside the visible part [black bars cut by the driver] are not sampled.
    const int2 low = int2( 0, (int)round( g_Source.y * (float)height ) );
    const int2 high = int2( (int)width - 1, (int)round( ( g_Source.y + g_Source.w ) * (float)height ) - 1 );
    float3 color = 0.0;

    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            const int2 texel = clamp( int2( base ) + int2( i - 1, j - 1 ), low, high );
            color += g_Texture.Load( int3( texel, 0 ) ).rgb * ( wx[i] * wy[j] );
        }
    }

    return float4( saturate( color * g_Params.x ), 1.0 );
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
    float4 g_Light; // mirrored lights (9.14a): x boost, y streak step (pixels at 1080 lines, 0 = none), z: light target bound
    float4 g_WaveU; // waves fixed on the water (9.14b): xyz a world axis on the water in view space, w the camera along it (mod 32 m)
    float4 g_WaveV; // the second world axis on the water, w the camera along it (mod 32 m)
    float4 g_Sun; // xyz sun direction in view space, w glint strength (0 = off)
    float4 g_Wave; // x waves fixed on the water, y slope, z largest shift of the lookup (pixels at 1080 lines), w tile size (m)
    float4 g_WaveK0; // wave vectors 1 and 2 in whole cycles per tile (main direction and spread from the CPU)
    float4 g_WaveK1; // wave vectors 3 and 4
    float4 g_WaveWind; // xy main direction on the water (the chop drifts with it), z relief height of the waves (m, 0 = flat)
};

Texture2D g_Reflection : register( t0 );
Texture2D g_SkyFrame : register( t1 ); // the sky alone, drawn again from the sky phase
SamplerState g_SkySampler : register( s1 );
Texture2D<float> g_Weight : register( t2 ); // 1 water, 1 - alpha on the shore
// Stencil of the frame: bit 1 water, bit 2 solid draw of the render queue; 0 = only the sky drawn there.
// The frame before the composite and its depth (z = 1 - n / w, n in g_Size.z).
// Stencil and depth are sample 0 of the depth buffer, single sampled by REFLECT_PACK.
Texture2D g_Frame : register( t4 );
Texture2D<uint> g_Stencil : register( t3 );
Texture2D<float> g_Depth : register( t5 );
Texture2D g_Lights : register( t6 ); // mirrored additive lights (9.14a), the size of the reflection target
uint StencilAt( int2 p ) { return g_Stencil.Load( int3( p, 0 ) ); }
float DepthAt( int2 p ) { return g_Depth.Load( int3( p, 0 ) ); }

// Value noise that repeats every 'cells' cells (x in tiles), smooth.
float TileHash( float2 c, float cells )
{
    c = c - cells * floor( c / cells );
    return frac( sin( dot( c, float2( 127.1, 311.7 ) ) ) * 43758.5453 );
}
float TileNoise( float2 x, float cells )
{
    float2 g = x * cells;
    float2 i = floor( g );
    float2 f = g - i;
    f = f * f * ( 3.0 - 2.0 * f );
    return lerp( lerp( TileHash( i, cells ), TileHash( i + float2( 1.0, 0.0 ), cells ), f.x ),
        lerp( TileHash( i + float2( 0.0, 1.0 ), cells ), TileHash( i + float2( 1.0, 1.0 ), cells ), f.x ), f.y );
}
// Slope of the noise (per tile) by central differences.
float2 TileNoiseSlope( float2 x, float cells )
{
    float h = 0.25 / cells;
    return float2( TileNoise( x + float2( h, 0.0 ), cells ) - TileNoise( x - float2( h, 0.0 ), cells ),
        TileNoise( x + float2( 0.0, h ), cells ) - TileNoise( x - float2( 0.0, h ), cells ) ) / ( 2.0 * h );
}

// Slope of the water on a square tile (everything repeats per tile, so the CPU wraps the camera position without a jump).
// Four long waves (wavelengths tile / 9.2, / 13.9, / 26.4, / 49; '[Water] ReflectionWaveSize' 3: tile 96 m, 10.5 ... 2 m, deep
// water speed) and an irregular chop of two drifting noise layers (cells of 4 and 2 m), all scaled by slowly drifting
// patches of calm and rough water (long waves and chop each their own), so it does not look even. Anything smaller than two pixels fades out (no flicker far away).
float2 WaveSlope( float2 x, float t, float footprint, float tile )
{
    // Wave vectors about one main direction, like the wave areas of GTA V (the CPU rounds them to whole cycles per tile).
    const float2 cycles[4] = { g_WaveK0.xy, g_WaveK0.zw, g_WaveK1.xy, g_WaveK1.zw };
    const float share[4] = { 0.25, 0.2, 0.12, 0.08 };
    float2 slope = float2( 0.0, 0.0 );
    [unroll] for ( int i = 0; i < 4; ++i )
    {
        float2 k = cycles[i] * ( 6.2831853 / tile );
        float kl = length( k );
        float fade = saturate( 6.2831853 / ( kl * footprint * 2.0 ) - 1.0 );
        slope += k / kl * share[i] * fade * cos( dot( k, x ) - sqrt( 9.81 * kl ) * t );
    }
    float2 u = x / tile;
    // Patches for the long waves: two coarse layers (cells of 32 and 14 m at size 3), drifting slowly; nearly calm down to
    // a tenth, rough up to twice.
    float patch = TileNoise( u + float2( 0.0011, 0.0007 ) * t, 3.0 ) * 0.6 + TileNoise( u - float2( 0.0005, 0.0013 ) * t, 7.0 ) * 0.4;
    slope *= lerp( 0.1, 2.0, smoothstep( 0.3, 0.7, patch ) );
    // Chop: noise height of about a quarter of its cell, so its slope is about 0.4.
    const float chop_cells[2] = { 24.0, 48.0 };
    // The chop drifts with the wind (main direction), the second layer a little faster and to the side.
    float2 wind = g_WaveWind.xy;
    float2 side = float2( -wind.y, wind.x );
    const float2 chop_drift[2] = { wind * 0.0035 + side * 0.0008, wind * 0.0050 - side * 0.0012 };
    [unroll] for ( int c = 0; c < 2; ++c )
    {
        float cell = tile / chop_cells[c];
        float fade = saturate( cell / ( footprint * 2.0 ) - 1.0 );
        // Gusts: patches of their own for the chop (cells of 19 and 8 m at size 3), so it does not follow the long waves.
        float gust = TileNoise( u + float2( -0.0019, 0.0009 ) * t, 5.0 ) * 0.5 + TileNoise( u + float2( 0.0008, 0.0021 ) * t, 12.0 ) * 0.5;
        slope += TileNoiseSlope( u + chop_drift[c] * t, chop_cells[c] ) * ( 0.27 / chop_cells[c] ) * fade * lerp( 0.0, 2.2, smoothstep( 0.3, 0.75, gust ) );
    }
    return slope;
}

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
    // The band only counts near the top edge (uv.y below 0.2), most of the water mirrors sky further down:
    // its 18 lookups only where they count.
    if ( uv.y >= 0.2 )
        return near;
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


// Height of the long waves for the relief ('[Water] ReflectionWaveHeight'): the same four waves as WaveSlope, amplitude of
// each as its share over its wave number, normalised to -1 ... 1; waves shorter than two steps of the march fade out.
// grad: the slope of that normalised height (per metre).
float WaveHeight( float2 x, float t, float step, float tile, out float2 grad )
{
    const float2 cycles[4] = { g_WaveK0.xy, g_WaveK0.zw, g_WaveK1.xy, g_WaveK1.zw };
    const float share[4] = { 0.25, 0.2, 0.12, 0.08 };
    float height = 0.0;
    float total = 0.0;
    grad = float2( 0.0, 0.0 );
    [unroll] for ( int i = 0; i < 4; ++i )
    {
        float2 k = cycles[i] * ( 6.2831853 / tile );
        float kl = length( k );
        float amplitude = share[i] / kl;
        total += amplitude;
        amplitude *= saturate( 6.2831853 / ( kl * step * 2.0 ) - 1.0 );
        float phase = dot( k, x ) - sqrt( 9.81 * kl ) * t;
        height += amplitude * sin( phase );
        grad += amplitude * k * cos( phase );
    }
    grad /= total;
    return height / total;
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

    // The view ray of the pixel and where it meets the water plane (depth w of the water point).
    float t = g_Sky.y;
    float2 game = p * ( 2.0 * g_Viewport.zw ) / size;
    float3 ray_w = mul( float4( ( game.x - g_Viewport.z ) / g_Viewport.x, ( game.y - g_Viewport.w ) / g_Viewport.y, 1.0, 0.0 ), g_ViewOfClip ).xyz;
    float3 ray = normalize( ray_w );
    float down = -dot( ray_w, g_Plane.xyz );
    float water_w = down > 1e-5 ? g_Plane.w / down : 1e9;
    float3 normal = g_Plane.xyz;
    // Shore weight, averaged over the water pixels around (three rings up to 24 pixels at 1080 lines): the weight jumps where
    // the water geometry meets a shore texture, and that edge stood out in the strength of the reflection and the waves.
    float weight = 1.0;
    if ( g_Fresnel.y > 0.5 )
    {
        weight = g_Weight.Load( int3( position.xy, 0 ) );
        if ( g_Edge.z > 0.5 )
        {
            float sum = weight;
            float count = 1.0;
            [unroll] for ( int ring = 1; ring <= 3; ++ring )
            {
                [unroll] for ( int k = 0; k < 8; ++k )
                {
                    float angle = ( k + ( ring & 1 ) * 0.5 ) * 0.7853982;
                    int2 q = clamp( int2( p + float2( cos( angle ), sin( angle ) ) * ring * 8.0 * scale ), int2( 0, 0 ), int2( size ) - 1 );
                    if ( ( StencilAt( q ) & 1 ) != 0 )
                    {
                        sum += g_Weight.Load( int3( q, 0 ) );
                        count += 1.0;
                    }
                }
            }
            weight = sum / count;
        }
    }
    float2 wave;
    if ( g_Wave.x > 0.5 && down > 1e-5 )
    {
        // Waves fixed on the water ('[Water] ReflectionWorldWaves', 9.14b): the slope at the world point bends the normal; the
        // bent reflection, mirrored back at the flat plane, is the camera ray whose pixel of the mirrored scene shows there.
        float tile = max( g_Wave.w, 1.0 );
        float len = length( ray_w );
        float2 world_at = float2( g_WaveU.w, g_WaveV.w );                              // world point of the ray at s = 0
        float2 world_dir = float2( dot( ray_w, g_WaveU.xyz ), dot( ray_w, g_WaveV.xyz ) ); // its change per s
        // Patches of calm and rough long waves at the flat water point (they change over 50 m, one lookup is enough).
        float2 u_flat = ( world_at + world_dir * water_w ) / tile;
        float patch = TileNoise( u_flat + float2( 0.0011, 0.0007 ) * t, 3.0 ) * 0.6 + TileNoise( u_flat - float2( 0.0005, 0.0013 ) * t, 7.0 ) * 0.4;
        float patch_factor = lerp( 0.1, 2.0, smoothstep( 0.3, 0.7, patch ) );
        // Relief ('[Water] ReflectionWaveHeight', 9.14b): the view ray marched over the wave heights (half the height x the
        // patches, so at most the height above and below the plane); the first crest it meets is what the pixel shows. Crests
        // hide the troughs behind them, far waves crowd together: the flat water seems to rise. Calm at the shore.
        float relief = g_WaveWind.z * weight;
        float s_hit = water_w;
        float lift = 0.0; // height of the hit point above the plane (m)
        float2 relief_grad = float2( 0.0, 0.0 );
        if ( relief > 0.01 )
        {
            float scale_h = 0.5 * relief * patch_factor;
            float s0 = max( ( g_Plane.w - relief ) / down, 0.02 * water_w );
            float s1 = ( g_Plane.w + relief ) / down;
            float step = ( s1 - s0 ) * len / 12.0;
            float2 grad;
            float prev_s = s0;
            float prev_diff = ( g_Plane.w - down * s0 ) - scale_h * WaveHeight( world_at + world_dir * s0, t, step, tile, grad );
            s_hit = s1;
            [loop] for ( int i = 1; i <= 12; ++i )
            {
                float s = s0 + ( s1 - s0 ) * ( i / 12.0 );
                float diff = ( g_Plane.w - down * s ) - scale_h * WaveHeight( world_at + world_dir * s, t, step, tile, grad );
                if ( diff <= 0.0 )
                {
                    s_hit = prev_s + ( s - prev_s ) * prev_diff / max( prev_diff - diff, 1e-5 );
                    break;
                }
                prev_s = s;
                prev_diff = diff;
            }
            lift = g_Plane.w - down * s_hit;
            WaveHeight( world_at + world_dir * s_hit, t, step, tile, grad );
            // Only a third of the tilt of the relief goes into the reflection: at full tilt (about 25 degrees at 1.5 m) the
            // lookup moved up to 100 pixels and the water turned into abstract smears; the crests still hide the troughs.
            relief_grad = grad * scale_h * 0.35;
            water_w = s_hit;
        }
        float3 at = ray_w * s_hit;
        float2 world = world_at + world_dir * s_hit;
        // Metres per pixel, stretched along the view at a flat angle; only by the square root (fully stretched, all waves
        // faded out when looking flat over wide water).
        float footprint = s_hit * 2.0 * g_Viewport.w / ( abs( g_Viewport.y ) * size.y ) / max( sqrt( saturate( -dot( ray, g_Plane.xyz ) ) ), 0.2 );
        // Towards the shore the waves calm down with the shore weight: the reflection there is weaker anyway, without this
        // the edge of the water geometry stood out against the moving water next to it.
        float2 slope = WaveSlope( world, t, footprint, tile ) * g_Wave.y * weight + relief_grad;
        normal = normalize( g_Plane.xyz - slope.x * g_WaveU.xyz - slope.y * g_WaveV.xyz );
        float3 bent = reflect( reflect( ray, normal ), g_Plane.xyz );
        // Flat: only the direction counts (mirrored scene far away). Relief: the reflection leaves the crest at its height,
        // so the mirror image lies 2 x lift lower; for the distance of the mirrored things take twice that of the water point.
        float3 c = relief > 0.01 ? mul( float4( at - 2.0 * lift * g_Plane.xyz + bent * ( 2.0 * s_hit * len ), 1.0 ), g_ClipOfView ).xyz
                                 : mul( float4( bent, 0.0 ), g_ClipOfView ).xyz;
        wave = float2( 0.0, 0.0 );
        if ( c.z > 1e-4 )
        {
            float2 game_bent = c.xy / c.z * g_Viewport.xy + g_Viewport.zw;
            float limit = g_Wave.z * scale * ( relief > 0.01 ? 1.5 : 1.0 );
            wave = clamp( game_bent * size / ( 2.0 * g_Viewport.zw ) - p, -limit, limit );
        }
    }
    else
    {
        // Small waves on the screen: the lookup swings a little sideways and up and down, slowly moving.
        wave = float2( sin( p.y * 0.09 / scale + t * 0.8 ) + 0.5 * sin( p.y * 0.23 / scale + p.x * 0.01 / scale - t * 1.1 ),
            0.6 * sin( p.x * 0.05 / scale + p.y * 0.04 / scale + t * 0.6 ) ) * g_Sky.z * scale;
    }
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
    float scene_cover = saturate( colour.a ); // the mirrored scene in front of the sky: no sun glint there
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
    float steep = saturate( -dot( ray, normal ) ); // with the bent normal of the waves (9.14b)
    float fresnel = lerp( g_Fresnel.x, 1.0, pow( 1.0 - steep, 3.0 ) );
    // Same far fade as the water itself: the depth w of the water point is where the ray meets the plane.
    if ( g_Fresnel.w > g_Fresnel.z && g_Fresnel.w > 0.0 )
        fresnel *= saturate( ( g_Fresnel.w - water_w ) / ( g_Fresnel.w - g_Fresnel.z ) );
    float3 mirrored = saturate( rgb ) * g_Tint.rgb;
    float alpha = saturate( colour.a ) * g_Tint.a * fresnel * weight;
    // Looking along the water it mirrors almost everything ('[Water] ReflectionGrazing'): far water takes the
    // colour of the reflected sky, like the far sea of the game next to it.
    alpha = lerp( alpha, saturate( colour.a ) * max( g_Tint.a, g_Edge.w ) * weight, pow( 1.0 - steep, 8.0 ) );
    // Mirrored lights (9.14a, '[Water] ReflectionLights'): their own target, so only lights are boosted and streaked, never a
    // bright wall by day. Six lookups above the pixel draw the streak down the water towards the camera, fading out.
    if ( g_Light.z > 0.5 )
    {
        float3 lights = g_Lights.SampleLevel( g_SkySampler, uv, 0 ).rgb;
        if ( g_Light.y > 0.0 )
        {
            [unroll] for ( int s = 1; s <= 6; ++s )
                lights += g_Lights.SampleLevel( g_SkySampler, uv - float2( 0.0, s * g_Light.y * scale / size.y ), 0 ).rgb * ( 1.0 - s / 7.0 ) * 0.5;
        }
        lights *= g_Light.x * weight;
        // Added to the reflection; the blend over the frame takes the larger coverage, so a light on clear water shows.
        float added = saturate( max( lights.r, max( lights.g, lights.b ) ) );
        float3 total = mirrored * alpha + lights;
        alpha = max( alpha, added );
        mirrored = saturate( total / max( alpha, 1e-4 ) );
    }
    // Sun glint ('[Water] ReflectionSunGlint', 9.14b): sharp highlight where the bent reflection points at the sun, only with
    // the sun above the water and where the mirrored scene does not cover the sky.
    if ( g_Sun.w > 0.0 && dot( g_Sun.xyz, g_Plane.xyz ) > 0.0 )
    {
        float glint = pow( saturate( dot( reflect( ray, normal ), g_Sun.xyz ) ), 500.0 ) * g_Sun.w * weight * ( 1.0 - scene_cover );
        float3 sun_light = float3( 1.0, 0.96, 0.88 ) * glint;
        float3 total = mirrored * alpha + sun_light;
        alpha = max( alpha, saturate( glint ) );
        mirrored = saturate( total / max( alpha, 1e-4 ) );
    }
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
            // The sky image may be smaller than the render size ('[Water] ReflectionSkyScale').
            float3 beyond = g_SkyFrame.SampleLevel( g_SkySampler, p / size, 0 ).rgb;
            // frame * (1 - a) + mirrored * a, then towards the sky by edge: one blend with alpha a + edge - a * edge.
            float total = alpha + edge - alpha * edge;
            mirrored = ( mirrored * alpha * ( 1.0 - edge ) + beyond * edge ) / max( total, 1e-4 );
            alpha = total;
        }
    }
    return float4( mirrored, alpha );
}
)hlsl";

    // Water reflections (B15): stencil and depth of the frame, sample 0, into two single sampled targets for the
    // composite (REFLECT). Fullscreen triangle of REFLECT's MainVS.
    inline constexpr char REFLECT_PACK[] = R"hlsl(
#ifdef STENCIL_MS
Texture2DMS<uint2> g_Stencil : register( t0 );
Texture2DMS<float> g_Depth : register( t1 );
uint StencilAt( int2 p ) { return g_Stencil.Load( p, 0 ).g; }
float DepthAt( int2 p ) { return g_Depth.Load( p, 0 ); }
#else
Texture2D<uint2> g_Stencil : register( t0 );
Texture2D<float> g_Depth : register( t1 );
uint StencilAt( int2 p ) { return g_Stencil.Load( int3( p, 0 ) ).g; }
float DepthAt( int2 p ) { return g_Depth.Load( int3( p, 0 ) ); }
#endif

struct PackOut
{
    uint m_Stencil : SV_Target0;
    float m_Depth : SV_Target1;
};

PackOut MainPS( float4 position : SV_Position )
{
    PackOut output;
    int2 p = int2( position.xy );
    output.m_Stencil = StencilAt( p );
    output.m_Depth = DepthAt( p );
    return output;
}
)hlsl";

    // Car reflections (PLAN.md 9.12b, doc/LICHT.md 12): the body of a car drawn once more, the frame in the reflected
    // direction over it. Position and normal per vertex in the camera space of the game come from the driver (normals
    // averaged over the triangles of the draw), the direction is projected to the screen as a point at infinity.
    inline constexpr char CAR_REFLECT[] = R"hlsl(
cbuffer CarReflectConstants : register( b0 )
{
    float4 g_Target; // vertex offset in game pixels (x, y), 1 / width, 1 / height of the game
    float4 g_Viewport; // fx, fy, cx, cy of the game projection
    row_major float4x4 g_ViewOfClip;
    row_major float4x4 g_ClipOfView;
    float4 g_Params; // strength, reflectance head on, blur level of the frame, debug
    float4 g_Forward; // xyz: view direction of the camera in its space, w: alpha reference of the texture (< 0: no test)
    float4 g_Glass; // x: alpha below which a blended pixel is glass (< 0: not blended), y: 1 = reflect glass too
    float4 g_Up; // xyz: up of the car in view space (0: unknown), w: strength of reflections below the horizon
    float4 g_SkyColour; // rgb: sky where the frame has nothing (the fog colour), w: 1 = use it
    float4 g_Shape; // x: exponent of the Fresnel term (5 = Schlick, smaller = softer change), y: mip levels the frame copy is smaller by,
                    // z: 1 = blur from the shared chain (9.16a, '[CarReflections] BlurChain'), w: its top level
    float4 g_CarWeather; // weather on the car (LFSP LfspWeatherCar, PLAN.md 15.3): x snow 0..1, y light of the snow, z rain drops 0..1, w time s
    float4 g_CarWeather2; // x drop cells per metre on the car (w3: in the space of the part), yzw snow colour
    float4 g_CarWeather3; // x sign of g_Up for the snow (from the side of the camera), y test view ('[Weather] SnowDebug' 3 up-ness,
                          // 4 drops), z shine of the drops ('[Weather] CarDropShine', w7), w evenness of the snow ('CarSnowSoft', w9)
    float4 g_CarFlow; // w5: xyz distance the drops of this car have crept (space of the part, m, wrapped), w speed of the car m/s
    float4 g_CarFlowDir; // xyz direction of travel in the space of the part, w brightness of the snow ('[Weather] CarSnowBright', w8)
    float4 g_CarFlowView; // xyz direction of travel in view space
    float4 g_Highlight; // 9.15b: x boost of bright reflections ('[CarReflections] HighlightBoost', 0 = off), y brightness from which
    float4 g_Coat; // 9.15b clearcoat: x 1 = on, y strength of the base coat, z its extra blur levels, w how much it takes the paint colour
};

// Weather on the car: a hash per cell of the position in the space of the part (w3; stable on the car, not mirrored like
// the UV of the car textures, no texture needed).
float CarHash( float2 p )
{
    p -= floor( p * ( 1.0 / 289.0 ) ) * 289.0; // small arguments for sin (fine snow has up to 240 cells per metre)
    return frac( sin( dot( p, float2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

float CarNoise( float2 p )
{
    float2 i = floor( p );
    float2 f = frac( p );
    f = f * f * ( 3.0 - 2.0 * f );
    return lerp( lerp( CarHash( i ), CarHash( i + float2( 1, 0 ) ), f.x ), lerp( CarHash( i + float2( 0, 1 ) ), CarHash( i + float2( 1, 1 ) ), f.x ), f.y );
}

// One layer of rain drops: at most one bead per cell of q. Each cell lives through drops one after the other (w6: a new
// place, size and chance every 5 to 12 s, so the drops renew while standing too). rel = offset from the centre in radii
// (along q). On steep faces (slide > 0) the bead runs down during its life; with speed it is drawn out along the flow.
float DropLayer( float2 q, float aaCell, float density, float slide, float down, float2 along, float stretch, float salt,
                 out float2 rel )
{
    // w11: one cell, at most one drop, its place fully random in the cell, a chance below 1 per cell; each cell lives
    // through drops one after the other (new place every 5 to 12 s). Size as w6.
    float2 cell = floor( q );
    float pace = 0.08 + 0.12 * CarHash( cell + salt + 17.31 );
    float phase = g_CarWeather.w * pace + CarHash( cell + salt );
    float cycle = floor( phase );
    float life = phase - cycle;
    float2 drop = cell + salt + cycle * 7.13;
    float h = CarHash( drop + 1.7 );
    float h2 = CarHash( drop + 23.9 );
    float h3 = CarHash( drop + 11.1 );
    // Streaks (w9): not every drop is drawn out the same, streaking drops are fewer.
    float own = stretch * ( h3 < 0.3 ? 0.0 : ( h3 - 0.3 ) * ( h3 - 0.3 ) * 4.0 );
    float present = step( h, density * ( 1.0 - 0.4 * saturate( own / 3.0 ) ) ) * smoothstep( 0.0, 0.04, life ) *
                    ( 1.0 - smoothstep( 0.85, 1.0, life ) );
    float radius = ( 0.11 + 0.12 * h2 ) * ( 1.0 - 0.3 * slide ) / ( 1.0 + 0.3 * own );
    float2 centre = float2( CarHash( drop + 3.17 ), CarHash( drop + 7.73 ) ) * 0.5 + 0.25;
    centre.y += down * slide * ( life - 0.5 ) * 0.3;
    rel = ( frac( q ) - centre ) / radius;
    rel.y /= 1.0 + slide * 0.7;
    float ra = dot( rel, along );
    float2 side = float2( -along.y, along.x );
    rel += along * ( ra / ( 1.0 + own ) - ra );
    rel += side * sin( ra * 0.8 + h2 * 6.28 ) * 0.25 * saturate( own );
    // LOD (w11): a drop whose radius is under about 2.5 pixels fades out completely by 1 pixel, there only the averaged wet
    // gloss of the paint stays; no single bright or dark pixels from drops smaller than a pixel.
    float pixels = radius / max( aaCell, 1e-6 );
    float lod = smoothstep( 1.0, 2.5, pixels );
    float aa = max( 1.0 / max( pixels, 1e-3 ), 0.02 );
    return ( 1.0 - smoothstep( 1.0 - aa, 1.0 + aa, length( rel ) ) ) * present * lod;
}

Texture2D g_Scene : register( t0 );
Texture2D g_Texture : register( t1 );
Texture2D g_Chain : register( t2 ); // shared blur chain: level 0 half the frame, each further level half as large and softer
Texture2D g_SkyImage : register( t3 ); // sky image of the water reflection, previous frame (9.15a, g_SkyColour.w = 2)
SamplerState g_SceneSampler : register( s0 );
SamplerState g_TextureSampler : register( s1 );

struct VertexIn
{
    float4 m_Position : POSITION;
    float3 m_View : TEXCOORD0;
    float3 m_Normal : TEXCOORD1;
    float3 m_Uv : TEXCOORD2; // u, v, alpha of the vertex colour
    float3 m_Object : TEXCOORD3; // position in the space of the part (m)
};

struct PixelIn
{
    float4 m_Position : SV_Position;
    float3 m_View : TEXCOORD0;
    float3 m_Normal : TEXCOORD1;
    float3 m_Uv : TEXCOORD2;
    float3 m_Object : TEXCOORD3;
};

PixelIn MainVS( VertexIn input )
{
    PixelIn output;
    float rhw = input.m_Position.w;
    float w = ( rhw > 0.0 && rhw < 1e30 ) ? 1.0 / rhw : 1.0;

    // As the scene shader, so the depth is the same and the body passes the test equal.
    float2 ndc;
    ndc.x = ( input.m_Position.x + g_Target.x ) * g_Target.z * 2.0 - 1.0;
    ndc.y = 1.0 - ( input.m_Position.y + g_Target.y ) * g_Target.w * 2.0;
    output.m_Position = float4( ndc * w, saturate( input.m_Position.z ) * w, w );
    output.m_View = input.m_View;
    output.m_Normal = input.m_Normal;
    output.m_Uv = input.m_Uv;
    output.m_Object = input.m_Object;
    return output;
}

float4 MainPS( PixelIn input ) : SV_Target
{
    // Weather on the car (w3): the position in the space of the part, projected onto the main plane of the face (the axis
    // of the face normal left out; y = up of the car). Before any clip: the derivatives need all pixels of the quad.
    float3 o = input.m_Object;
    float3 faceO = abs( cross( ddx( o ), ddy( o ) ) );
    bool top = faceO.y >= faceO.x && faceO.y >= faceO.z;
    float2 q = top ? o.xz : ( faceO.x >= faceO.z ? o.zy : o.xy );
    // Drops: the same plane, shifted by the distance they have crept with the speed of the car (w5).
    float3 od = o + g_CarFlow.xyz;
    float2 dropQ = ( top ? od.xz : ( faceO.x >= faceO.z ? od.zy : od.xy ) ) * g_CarWeather2.x;
    float2 dq1 = ddx( dropQ );
    float2 dq2 = ddy( dropQ );
    float dropAa = max( max( abs( dq1.x ) + abs( dq2.x ), abs( dq1.y ) + abs( dq2.y ) ), 1e-6 );
    float3 dp1 = ddx( input.m_View );
    float3 dp2 = ddy( input.m_View );

    // Cut out parts (grilles) keep their holes; the game blends the whole body, glass is what is not opaque.
    float4 paint = g_Texture.Sample( g_TextureSampler, input.m_Uv.xy );
    float alpha = paint.a * input.m_Uv.z;
    if ( g_Forward.w >= 0.0 )
        clip( alpha - g_Forward.w );
    if ( g_Glass.x >= 0.0 && g_Glass.y == 0.0 )
        clip( alpha - g_Glass.x );

    float3 v = normalize( input.m_View );
    float3 n = normalize( input.m_Normal );
    if ( dot( n, v ) > 0.0 )
        n = -n;
    float facing = saturate( -dot( n, v ) );
    float3 r = reflect( v, n );
    float3 sky_seen = g_SkyColour.rgb; // what the sky above the frame looks like, and how much of it this direction takes
    float sky_amount = 0.0;

    // Upwards or downwards in the world: the ground is near and dark, the frame shows it sharp and too strong.
    bool has_up = dot( g_Up.xyz, g_Up.xyz ) > 0.5;
    float upward = has_up ? dot( r, g_Up.xyz ) : 0.0;

    // The frame only shows what lies ahead; a direction behind the camera is mirrored to the front.
    float along = dot( r, g_Forward.xyz );
    r += g_Forward.xyz * ( max( abs( along ), 0.2 ) - along );

    // Point at infinity in that direction: x = xc / wc * fx + cx. Outside the frame, towards its middle onto the edge.
    float3 c = mul( float4( r, 0.0 ), g_ClipOfView ).xyz;
    float2 screen = c.xy / c.z * g_Viewport.xy + g_Viewport.zw;
    float2 p = ( screen + g_Target.xy ) * g_Target.zw * 2.0 - 1.0;
    float edge = max( abs( p.x ), abs( p.y ) );
    float outside = saturate( ( edge - 1.0 ) / 0.3 );
    p /= max( 1.0, edge / 0.99 );
    // Towards and beyond the edge blurrier and then the mean of the frame, the last row of pixels would become streaks.
    // Levels of the full size frame; a smaller copy ('[CarReflections] SourceScale') starts g_Shape.y levels further down.
    float level = lerp( g_Params.z, max( g_Params.z, 6.0 ), saturate( ( edge - 0.8 ) / 0.4 ) );
    float blur = max( level - g_Shape.y, 0.0 );
    float3 colour;
    // The chain starts at half size: level n of the frame is level n - 1 there, filtered between its levels.
    if ( g_Shape.z > 0.5 )
        colour = g_Chain.SampleLevel( g_SceneSampler, p * 0.5 + 0.5, clamp( level - 1.0, 0.0, g_Shape.w ) ).rgb;
    else
        colour = g_Scene.SampleLevel( g_SceneSampler, p * 0.5 + 0.5, blur ).rgb;
    colour = lerp( colour, g_Scene.SampleLevel( g_SceneSampler, float2( 0.5, 0.5 ), 12.0 ).rgb, saturate( ( edge - 1.0 ) / 0.6 ) );

    // Up out of the frame there is sky, not the stretched edge of the frame. Behind the camera the mirrored frame
    // stays: the plain fog colour there made the back of the car look as if it reflected nothing.
    float strength = g_Params.x;
    if ( has_up )
    {
        float sky = outside * saturate( upward * 4.0 + 0.5 ) * saturate( g_SkyColour.w );
        float3 sky_colour = g_SkyColour.rgb;
        // '[CarReflections] Sky' (9.15a): the sky itself, sky colour and clouds. Above the frame its top row (the direction
        // is pulled onto the edge like the frame lookup), softened by five taps.
        if ( g_SkyColour.w > 1.5 )
        {
            float2 sky_uv = float2( saturate( p.x * 0.5 + 0.5 ), clamp( p.y * 0.5 + 0.5, 0.02, 0.98 ) );
            sky_colour = g_SkyImage.SampleLevel( g_SceneSampler, sky_uv, 0 ).rgb * 0.4 +
                ( g_SkyImage.SampleLevel( g_SceneSampler, sky_uv + float2( 0.02, 0.0 ), 0 ).rgb +
                  g_SkyImage.SampleLevel( g_SceneSampler, sky_uv - float2( 0.02, 0.0 ), 0 ).rgb +
                  g_SkyImage.SampleLevel( g_SceneSampler, sky_uv + float2( 0.0, 0.015 ), 0 ).rgb +
                  g_SkyImage.SampleLevel( g_SceneSampler, sky_uv + float2( 0.0, 0.03 ), 0 ).rgb ) * 0.15;
        }
        colour = lerp( colour, sky_colour, sky );
        sky_seen = sky_colour;
        sky_amount = sky;
        strength *= lerp( g_Up.w, 1.0, saturate( upward * 4.0 + 1.0 ) );
    }

    float fresnel = g_Params.y + ( 1.0 - g_Params.y ) * pow( 1.0 - facing, g_Shape.x );

    if ( g_Params.w == 1.0 )
        return float4( n * 0.5 + 0.5, 1.0 );
    if ( g_Params.w == 2.0 )
        return float4( colour, 1.0 );
    if ( g_Params.w == 3.0 )
        return float4( fresnel.xxx, 1.0 );

    // Highlights (9.15b, '[CarReflections] HighlightBoost'/'HighlightThreshold'): what is brighter than the threshold (lamps,
    // bright sky, the sun) is boosted and covers more of the paint; the dark ground stays as it was.
    float luma = dot( colour, float3( 0.2126, 0.7152, 0.0722 ) );
    float highlight = max( luma - g_Highlight.y, 0.0 ) * g_Highlight.x;
    colour += highlight * colour;

    float a = saturate( strength * fresnel );
    // Clearcoat (9.15b, '[CarReflections] ClearCoat'): car paint is a coloured base under a clear lacquer. The lacquer mirrors
    // sharp like glass (4 % head on, all of it at a flat angle, Schlick); the base mirrors soft and in the colour of the paint
    // (taken from the hue of the texture), with what the lacquer lets through. Both laid over the paint in one pass.
    if ( g_Coat.x > 0.5 )
    {
        float lacquer = 0.04 + 0.96 * pow( 1.0 - facing, 5.0 );
        float base_level = level + g_Coat.z;
        float3 base_colour;
        if ( g_Shape.z > 0.5 )
            base_colour = g_Chain.SampleLevel( g_SceneSampler, p * 0.5 + 0.5, clamp( base_level - 1.0, 0.0, g_Shape.w ) ).rgb;
        else
            base_colour = g_Scene.SampleLevel( g_SceneSampler, p * 0.5 + 0.5, max( base_level - g_Shape.y, 0.0 ) ).rgb;
        base_colour = lerp( base_colour, sky_seen, sky_amount );
        float hue_scale = max( max( paint.r, paint.g ), max( paint.b, 0.05 ) );
        base_colour *= lerp( float3( 1.0, 1.0, 1.0 ), paint.rgb / hue_scale, g_Coat.w );
        float lacquer_part = saturate( strength * lacquer );
        float base_part = saturate( g_Coat.y * ( strength / max( g_Params.x, 1e-4 ) ) ) * ( 1.0 - lacquer_part );
        float coat_a = saturate( lacquer_part + base_part );
        colour = ( colour * lacquer_part + base_colour * base_part ) / max( coat_a, 1e-4 );
        a = coat_a;
    }
    a = saturate( a + saturate( highlight ) * strength );

    // Rain drops (LFSP CarDrops, w11 = the look of w6 plus a LOD filter and safe maths): the wet paint a little glossier and
    // at most about 5 % darker; up close single clear beads with the bent reflection and a faint glint, further away only
    // the wet gloss. A bead never makes the paint darker than that, bent reflection lookups are clamped, every
    // normalisation is guarded (a zero vector gave black pixels). Streaks only where the air runs along the face (w9).
    if ( g_CarWeather.z > 0.0 )
    {
        float shine = g_CarWeather3.z;
        float wetAll = 1.0 - 0.03 * g_CarWeather.z;
        float glossA = saturate( a * ( 1.0 + 0.5 * g_CarWeather.z ) );
        float wetA = 1.0 - ( 1.0 - glossA ) * wetAll;
        colour = colour * glossA / max( wetA, 1e-4 );
        a = wetA;
        float upness = has_up ? saturate( dot( n, g_Up.xyz * g_CarWeather3.x ) ) : 0.5;
        float slide = top ? 0.0 : saturate( 1.0 - upness * 1.5 );
        float down = -g_CarWeather3.x; // q.y is the car's y on the side planes
        float speed = g_CarFlow.w;
        float2 along = top ? g_CarFlowDir.xz : ( faceO.x >= faceO.z ? g_CarFlowDir.zy : g_CarFlowDir.xy );
        float alongLength = length( along );
        along = alongLength > 1e-3 ? along / alongLength : float2( 0.0, 1.0 );
        float stretch = saturate( ( speed - 12.0 ) / 18.0 ) * 1.5 * saturate( alongLength * 1.5 - 0.3 );
        float front = saturate( dot( n, g_CarFlowView.xyz ) * 2.0 );
        float blown = saturate( ( speed - 36.0 ) / 15.0 ) * front;
        float density = g_CarWeather.z * lerp( 0.35, 1.0, upness ) * ( 1.0 - blown );
        float2 rel1, rel2;
        float m1 = DropLayer( dropQ, dropAa, density, slide, down, along, stretch, 0.0, rel1 );
        // Large drops on a grid turned by 37 degrees, so the two grids do not line up.
        const float2 turn = float2( 0.7986, 0.6018 );
        float2 q2 = dropQ * 0.5;
        q2 = float2( q2.x * turn.x - q2.y * turn.y, q2.x * turn.y + q2.y * turn.x );
        float2 along2 = float2( along.x * turn.x - along.y * turn.y, along.x * turn.y + along.y * turn.x );
        float m2 = DropLayer( q2, dropAa * 0.5, density * 0.3, slide, down, along2, stretch, 41.7, rel2 );
        rel2 = float2( rel2.x * turn.x + rel2.y * turn.y, -rel2.x * turn.y + rel2.y * turn.x );
        float m = max( m1, m2 );
        // Test view '[Weather] SnowDebug = 4': red small drops, green large drops, blue alpha of the reflection.
        if ( g_CarWeather3.y > 3.5 )
            return float4( m1, m2, a, 1.0 );
        if ( m > 0.0 )
        {
            float2 rel = m2 > m1 ? rel2 : rel1;
            // Directions of +q in view space (cotangent frame from the screen derivatives), guarded against zero vectors.
            float3 cg = cross( dp1, dp2 );
            float3 ng = cg * rsqrt( max( dot( cg, cg ), 1e-30 ) );
            float3 dp2perp = cross( dp2, ng );
            float3 dp1perp = cross( ng, dp1 );
            float3 tq = dp2perp * dq1.x + dp1perp * dq2.x;
            float3 bq = dp2perp * dq1.y + dp1perp * dq2.y;
            float scale = rsqrt( max( max( dot( tq, tq ), dot( bq, bq ) ), 1e-30 ) );
            tq *= scale;
            bq *= scale;
            float s2 = saturate( dot( rel, rel ) );
            float3 nbRaw = n * sqrt( 1.0 - 0.85 * s2 ) + ( tq * rel.x + bq * rel.y ) * 0.9;
            float3 nb = nbRaw * rsqrt( max( dot( nbRaw, nbRaw ), 1e-12 ) );
            if ( dot( nb, v ) > 0.0 )
                nb = n;
            float3 rb = reflect( v, nb );
            float alongB = dot( rb, g_Forward.xyz );
            rb += g_Forward.xyz * ( max( abs( alongB ), 0.2 ) - alongB );
            float3 cb = mul( float4( rb, 0.0 ), g_ClipOfView ).xyz;
            float2 pb = ( cb.xy / ( abs( cb.z ) > 1e-4 ? cb.z : 1e-4 ) * g_Viewport.xy + g_Viewport.zw + g_Target.xy ) * g_Target.zw * 2.0 - 1.0;
            float edgeB = max( abs( pb.x ), abs( pb.y ) );
            pb /= max( 1.0, edgeB / 0.99 );
            float3 reflB = saturate( g_Scene.SampleLevel( g_SceneSampler, pb * 0.5 + 0.5, max( g_Params.z - 1.0 - g_Shape.y, 0.0 ) ).rgb );
            if ( has_up )
                reflB = lerp( reflB, g_SkyColour.rgb, saturate( ( edgeB - 1.0 ) / 0.3 ) * saturate( dot( rb, g_Up.xyz ) * 4.0 + 0.5 ) );
            float fb = 0.04 + 0.96 * pow( 1.0 - saturate( -dot( nb, v ) ), 5.0 );
            float3 lightRaw = has_up ? g_Up.xyz * g_CarWeather3.x - v * 0.6 : -v;
            float3 light = lightRaw * rsqrt( max( dot( lightRaw, lightRaw ), 1e-12 ) );
            float glint = pow( saturate( dot( rb, light ) ), 200.0 ) * 2.0 * shine * g_CarWeather.y;
            // As w6: the bead covers a little more of the paint with its brighter reflection (at most 2 %).
            float dark = lerp( 1.0, 0.98, m );
            float outA = 1.0 - ( 1.0 - a ) * dark;
            colour = ( colour * a * dark + reflB * fb * 1.3 * shine * m ) / max( outA, 1e-4 );
            a = outA;
            // The glint on top: white of the world's light over everything below, so it only brightens.
            float g = saturate( glint * m );
            float glintA = 1.0 - ( 1.0 - a ) * ( 1.0 - g );
            colour = ( colour * a * ( 1.0 - g ) + g_CarWeather.y * g ) / max( glintA, 1e-4 );
            a = glintA;
        }
    }

    // Test view '[Weather] SnowDebug = 3': red = face points up (gets snow), blue = points down, black = up unknown.
    if ( g_CarWeather3.y > 2.5 && g_CarWeather3.y < 3.5 )
    {
        float facingUp = has_up ? dot( n, g_Up.xyz * g_CarWeather3.x ) : 0.0;
        return float4( saturate( facingUp ), 0.0, saturate( -facingUp ), 1.0 );
    }

    // Snow on the car (LFSP CarSnow, w9): an even thin layer of powder that grows over every face pointing up, only gently
    // varied by large low waves (no threshold, no blots). The noise runs on the position in the space of the part in three
    // planes at once, so it does not jump at triangle edges (the main plane of a face does). The amount follows the
    // interpolated normal: much on top, gently less down the sides, none below. '[Weather] CarSnowSoft' = how even (1 =
    // even, smaller = more varied), '[Weather] CarSnowBright' = brightness; neutral grey-white, lit like the world.
    if ( g_CarWeather.x > 0.0 && has_up )
    {
        float upDot = dot( n, g_Up.xyz * g_CarWeather3.x );
        float slope = smoothstep( 0.15, 0.95, upDot );
        if ( slope > 0.0 )
        {
            float fp = dropAa / max( g_CarWeather2.x, 1.0 ); // metres per pixel on the face
            float3 oS = o * 2.5;
            float wave = ( CarNoise( oS.xy ) + CarNoise( oS.yz + 3.7 ) + CarNoise( oS.zx + 8.1 ) ) / 3.0;
            float3 oS2 = o * 7.0;
            wave = wave * 0.65 + ( CarNoise( oS2.xy + 1.3 ) + CarNoise( oS2.yz + 5.9 ) + CarNoise( oS2.zx + 2.2 ) ) / 3.0 * 0.35 *
                   saturate( 1.6 - 7.0 * fp * 2.2 );
            float vary = clamp( 0.5 / max( g_CarWeather3.w, 0.1 ), 0.1, 1.5 );
            float cover = saturate( g_CarWeather.x * 1.6 ) * slope;
            float k = cover * saturate( 1.0 + vary * ( wave - 0.5 ) * 2.0 ) * 0.9;
            if ( k > 0.0 )
            {
                float3 oG = o * 90.0;
                float grain = ( CarNoise( oG.xy ) + CarNoise( oG.yz + 4.4 ) ) * 0.5 - 0.5;
                grain *= saturate( 1.6 - 90.0 * fp * 2.2 );
                float3 snow = g_CarWeather2.yzw * float3( 0.97, 0.97, 0.96 ) * ( 1.0 + grain * 0.12 ) * ( 0.88 + 0.12 * saturate( upDot ) );
                snow *= g_CarFlowDir.w * g_CarWeather.y;
                float outA = 1.0 - ( 1.0 - a ) * ( 1.0 - k );
                colour = ( colour * a * ( 1.0 - k ) + snow * k ) / max( outA, 1e-4 );
                a = outA;
            }
        }
    }
    return float4( colour, a );
}
)hlsl";

    // Rain and snow in the air (PLAN.md 16.4 / 16.5, dx11z p2): replaces the game's precipitation (one triangle per particle,
    // at most 300). Thousands of thin streaks or soft flakes, drawn instanced without a vertex buffer: each instance hashes
    // its place in a box of the world around the camera that falls with the precipitation and wraps, so the particles stay
    // in the world. Streak = the path relative to the camera during a short exposure (fall + wind - camera motion), so they
    // stretch with speed. Depth tested against the scene (z = 1 - 0.5 / w like the game), not written.
    inline constexpr char PRECIP[] = R"hlsl(
cbuffer PrecipConstants : register( b0 )
{
    float4 g_Target; // 0, 0, 1 / width, 1 / height of the game
    float4 g_Viewport; // fx, fy, cx, cy of the game projection
    row_major float4x4 g_ClipOfView;
    float4 g_CamPos; // xyz camera in the world, w size of the box (m)
    float4 g_Right; // xyz right of the camera in the world, w kind (1 rain, 2 snow)
    float4 g_Up; // xyz up, w intensity 0..1
    float4 g_Fwd; // xyz forward, w time s
    float4 g_Fall; // xyz fall and wind (m/s, world), w exposure s
    float4 g_CamVel; // xyz velocity of the camera (m/s, world), w width of a particle (m)
    float4 g_Colour; // rgb light of the particles, a opacity
    float4 g_Extra; // x lightning flash, y least width in pixels, z longest streak (m), w unused
};

struct PrecipOut
{
    float4 m_Position : SV_Position;
    float2 m_Uv : TEXCOORD0; // along from the start of the capsule, across (pixels)
    nointerpolation float3 m_Seg : TEXCOORD1; // length, radius (pixels), opacity
};

uint PHash( uint x )
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float3 PRand3( uint i )
{
    return float3( PHash( i * 3u + 1u ), PHash( i * 3u + 2u ), PHash( i * 3u + 3u ) ) * ( 1.0 / 4294967296.0 );
}

float3 ToView( float3 world )
{
    float3 d = world - g_CamPos.xyz;
    return float3( dot( d, g_Right.xyz ), dot( d, g_Up.xyz ), -dot( d, g_Fwd.xyz ) );
}

PrecipOut PrecipVS( uint vid : SV_VertexID, uint iid : SV_InstanceID )
{
    PrecipOut o;
    o.m_Position = float4( 0.0, 0.0, -1.0, 1.0 );
    o.m_Uv = float2( 0.0, 0.0 );
    o.m_Seg = float3( 1.0, 1.0, 0.0 );

    bool snow = g_Right.w > 1.5;
    float box = g_CamPos.w;
    float t = g_Fwd.w;
    float3 r = PRand3( iid );
    float3 r2 = PRand3( iid + 7919u );
    float3 fall = g_Fall.xyz * ( snow ? lerp( 0.6, 1.3, r2.x ) : lerp( 0.85, 1.15, r2.x ) );
    float3 centre = g_CamPos.xyz + g_Fwd.xyz * box * 0.3;
    float3 offset = ( frac( ( r * box + fall * t - centre ) / box ) - 0.5 ) * box;
    float3 p = centre + offset;
    if ( snow )
    {
        // Flakes tumble and swirl about their path.
        float phase = r2.y * 6.2832;
        p += float3( sin( t * ( 0.7 + r2.z ) + phase ), 0.3 * sin( t * 1.3 + phase * 2.0 ), cos( t * ( 0.5 + r2.y ) + phase * 1.3 ) ) * 0.3;
    }
    float edge = max( max( abs( offset.x ), abs( offset.y ) ), abs( offset.z ) ) / box;
    float fade = saturate( ( 0.5 - edge ) * 8.0 );

    float3 head = ToView( p );
    float3 tail = ToView( p - ( fall - g_CamVel.xyz ) * g_Fall.w );
    float3 along = tail - head;
    float alongLength = length( along );
    if ( alongLength > g_Extra.z )
        tail = head + along * ( g_Extra.z / alongLength );
    float3 ch = mul( float4( head, 1.0 ), g_ClipOfView ).xyz;
    float3 ct = mul( float4( tail, 1.0 ), g_ClipOfView ).xyz;
    if ( ch.z < 0.6 || ct.z < 0.6 )
        return o;
    float2 sh = ch.xy / ch.z * g_Viewport.xy + g_Viewport.zw;
    float2 st = ct.xy / ct.z * g_Viewport.xy + g_Viewport.zw;
    float2 axis = st - sh;
    float axisLength = length( axis );
    float widthTrue = g_CamVel.w * g_Viewport.x / ch.z;
    float width = max( widthTrue, g_Extra.y );
    float cover = saturate( widthTrue / g_Extra.y ); // thinner than the least width: fainter instead
    float2 dir = axisLength > 1e-3 ? axis / axisLength : float2( 0.0, 1.0 );
    float2 perp = float2( -dir.y, dir.x );

    // Two triangles: along 0 (head) .. 1 (tail), across -1 .. 1.
    const float2 corners[6] = { float2( 0, -1 ), float2( 1, -1 ), float2( 0, 1 ), float2( 0, 1 ), float2( 1, -1 ), float2( 1, 1 ) };
    float2 q = corners[vid];
    float2 a0 = sh - dir * width * 0.5;
    float2 a1 = st + dir * width * 0.5;
    float2 s = lerp( a0, a1, q.x ) + perp * q.y * width * 0.5;
    float w = lerp( ch.z, ct.z, q.x );
    o.m_Position = float4( ( s.x * g_Target.z * 2.0 - 1.0 ) * w, ( 1.0 - s.y * g_Target.w * 2.0 ) * w, w - 0.5, w );
    float total = axisLength + width;
    o.m_Uv = float2( q.x * total, q.y * width * 0.5 );
    // A long streak spreads the same water over more pixels: fainter.
    float energy = snow ? 1.0 : saturate( 3.0 * width / max( axisLength, width ) + 0.25 );
    o.m_Seg = float3( total, width * 0.5, g_Colour.a * fade * cover * energy * lerp( 0.6, 1.0, r2.z ) );
    return o;
}

float4 PrecipPS( PrecipOut input ) : SV_Target
{
    float total = input.m_Seg.x;
    float radius = input.m_Seg.y;
    float dx = max( max( radius - input.m_Uv.x, input.m_Uv.x - ( total - radius ) ), 0.0 );
    float d = length( float2( dx, input.m_Uv.y ) ) / radius;
    float a = ( 1.0 - smoothstep( 0.25, 1.0, d ) ) * input.m_Seg.z;
    if ( g_Right.w < 1.5 )
        a *= lerp( 1.0, 0.35, saturate( input.m_Uv.x / total ) ); // rain: the head is brightest
    clip( a - 0.002 );
    return float4( g_Colour.rgb * ( 1.0 + g_Extra.x ), a );
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

    // Car reflections, '[CarReflections] SourceScale' 0.5 (REFLEXION-WETTER.md 9.13d): the resolved frame drawn into the half
    // size copy. One bilinear lookup in the middle of each 2 x 2 block is its mean. Fullscreen triangle of REFLECT's MainVS.
    inline constexpr char CAR_DOWN[] = R"hlsl(
Texture2D g_Source : register( t0 );
SamplerState g_Sampler : register( s0 );

float4 MainPS( float4 position : SV_Position ) : SV_Target
{
    float2 size;
    g_Source.GetDimensions( size.x, size.y );
    // Pixel centre x + 0.5 of the half target lies on the corner 2x + 1 of the full source.
    return g_Source.SampleLevel( g_Sampler, position.xy * 2.0 / size, 0 );
}
)hlsl";

    // Ambient occlusion ('[AmbientOcclusion]', after the look of MXAO type 2 the user tuned in ReShade; own code, nothing taken
    // from it). Horizon based occlusion with a visibility bitmask (Therrien et al. 2023): per slice 32 sectors of the hemisphere
    // around the normal, each sample covers the sectors between its front and its back face (objects get a thickness), cosine
    // weighted. Depth z = 1 - n / w of the game (single sampled copy of PackStencil), view space as REFLECT. Fade over the depth
    // the way ReShade linearises it (far plane F), so 'FadeDepth' means the same as there. Passes, fullscreen triangle of
    // REFLECT's MainVS: DepthHalfPS / DepthMipPS the pyramid of the linear depth ('DepthMips' 1), MainPS raw occlusion
    // (checkerboard per frame with 'ShadingRate' 1), FilterPS first edge aware regression filter and the accumulation over the
    // frames ('Temporal' 1), ApplyPS second filter and the factor for the frame (multiplicative blend, so MSAA edges stay).
    // 'Method' 1 (GT-VBAO, Salm 2024; GTAO, Jimenez et al. 2016): the sectors are equal shares of the cosine weighted solid
    // angle of the slice (with the |sin| of the slices about the view direction, which 'Method' 0 leaves out: there the road
    // seen at a flat angle came out too dark and faces towards the camera too bright), the back face of a sample lies along
    // its own view ray.
    inline constexpr char AO[] = R"hlsl(
cbuffer AoConstants : register( b0 )
{
    row_major float4x4 g_ViewOfClip;
    float4 g_Viewport; // fx, fy, cx, cy of the game
    float4 g_Size; // render width, height, near plane n of z = 1 - n / w, frame number
    float4 g_Params; // x radius ('SampleRadius'), y amount, z fade depth, w far plane F of the ReShade depth
    float4 g_Quality; // x slices, y samples per side, z shading rate (0 all, 1 checkerboard), w debug (1 = show the raw occlusion)
    float4 g_Filter; // x filter passes (0, 1, 2), y 1 = radius grows with the distance (share of the width), 0 = radius in metres,
                     // z share of the history ('Temporal', 0 = off), w levels of the depth pyramid ('DepthMips', 0 = off)
    float4 g_Method; // x 'Method' (0 as MXAO, 1 exact), y 'Thickness' scale, z 1 = the last frame can be reprojected, w 'AngleBias' (radians)
    float4 g_Apply; // x 'MultiBounce', y unused, z fog mode (-1 none, 0 table over z, 1 exp, 2 exp2, 3 linear over z), w fog exponent
    float4 g_Fog; // fog start, end, density (game render states), w unused
    row_major float4x4 g_PrevClipOfView; // view of this frame -> clip (x, y, w, 1) of the last frame
    float4 g_PrevViewport; // fx, fy, cx, cy of the last frame
    float4 g_FogTable[64]; // visibility through the fog over z of the game table (index z x 255), four per register
};

Texture2D<float> g_Depth : register( t0 );
Texture2D<float2> g_Ao : register( t1 ); // x occlusion (1 = open), y depth as ReShade sees it, negative where the normal is unsure
Texture2D g_Frame : register( t2 ); // the resolved frame (ApplyPS)
Texture2D<float> g_DepthMips : register( t3 ); // linear depth w, level 0 = half size (MainPS)
Texture2D<float2> g_History : register( t4 ); // FilterPS's result of the last frame (FilterPS)
Texture2D<float> g_DepthSource : register( t5 ); // one level of the pyramid (DepthMipPS)

static const float PI = 3.14159265;

// acos by polynomial (Abramowitz & Stegun 4.4.45, error below 1e-4 rad, far under the 1/32 of a sector). The native acos is
// several times slower and runs twice per sample. The argument has to be within -1 ... 1.
float2 FastAcos( float2 x )
{
    float2 a = abs( x );
    float2 r = ( ( -0.0187293 * a + 0.0742610 ) * a - 0.2121144 ) * a + 1.5707288;
    r *= sqrt( 1.0 - a );
    return x >= 0.0 ? r : PI - r;
}

float FastAcos( float x )
{
    return FastAcos( x.xx ).x;
}

float2 GameOf( float2 pixel )
{
    return pixel * ( 2.0 * g_Viewport.zw ) / g_Size.xy;
}

float3 RayAt( float2 pixel )
{
    float2 game = GameOf( pixel );
    return mul( float4( ( game.x - g_Viewport.z ) / g_Viewport.x, ( game.y - g_Viewport.w ) / g_Viewport.y, 1.0, 0.0 ), g_ViewOfClip ).xyz;
}

float DepthW( float z )
{
    return g_Size.z / max( 1.0 - z, 1e-6 );
}

float3 ViewAt( int2 pixel )
{
    float z = g_Depth.Load( int3( clamp( pixel, int2( 0, 0 ), int2( g_Size.xy ) - 1 ), 0 ) );
    return RayAt( pixel + 0.5 ) * DepthW( z );
}

// Depth the way ReShade linearises it (RESHADE_DEPTH_LINEARIZATION_FAR_PLANE), for the fade; and back to w.
float ReshadeDepth( float z )
{
    return z / ( g_Params.w - z * ( g_Params.w - 1.0 ) );
}

float WOfReshade( float d )
{
    float z = d * g_Params.w / ( 1.0 + d * ( g_Params.w - 1.0 ) );
    return DepthW( z );
}

float FadeOf( float d )
{
    float fade = saturate( 1.0 - d * d );
    float x = d / max( g_Params.z, 1e-4 );
    return fade * saturate( exp2( -x * x ) );
}

// Share seen through the fog of the game at depth z, as the scene shader computes it (vertex table over z, exp / exp2 over w,
// linear over z, storm exponent). 1 without fog or with 'FogFade' 0.
float FogVisibility( float z )
{
    float mode = g_Apply.z;
    if ( mode < -0.5 )
        return 1.0;
    float fog;
    if ( mode < 0.5 )
    {
        uint k = uint( clamp( round( z * 255.0 ), 0.0, 255.0 ) );
        float4 four = g_FogTable[k >> 2];
        uint part = k & 3u;
        fog = part == 0u ? four.x : ( part == 1u ? four.y : ( part == 2u ? four.z : four.w ) );
    }
    else if ( mode < 1.5 )
        fog = exp( -g_Fog.z * DepthW( z ) );
    else if ( mode < 2.5 )
        fog = exp( -( g_Fog.z * DepthW( z ) ) * ( g_Fog.z * DepthW( z ) ) );
    else
        fog = ( g_Fog.y - z ) / max( g_Fog.y - g_Fog.x, 1e-6 );
    fog = saturate( fog );
    if ( g_Apply.w > 1.0 )
        fog = pow( fog, g_Apply.w );
    return fog;
}

// Normal from the four neighbours: each of the four corner normals weighted by how little the depth jumps on its two sides.
// 'step' = distance of the neighbours in pixels.
float3 NormalAt( int2 pixel, float3 centre, out float sure, int step )
{
    float3 l = ViewAt( pixel + int2( -step, 0 ) ) - centre;
    float3 r = ViewAt( pixel + int2( step, 0 ) ) - centre;
    float3 t = ViewAt( pixel + int2( 0, -step ) ) - centre;
    float3 b = ViewAt( pixel + int2( 0, step ) ) - centre;
    float scale = max( length( centre ), 1e-3 );
    float4 jump = abs( float4( dot( l, centre ), dot( r, centre ), dot( t, centre ), dot( b, centre ) ) ) / ( scale * scale );
    float4 weight = 1.0 / ( 1e-4 + ( jump.xzyw + jump.zywx ) * ( jump.xzyw + jump.zywx ) * 1e3 );
    sure = saturate( dot( weight, 1.0 ) * 1e-4 );
    float3 n = cross( t, l ) * weight.x * rsqrt( max( dot( cross( t, l ), cross( t, l ) ), 1e-20 ) ) +
               cross( r, t ) * weight.y * rsqrt( max( dot( cross( r, t ), cross( r, t ) ), 1e-20 ) ) +
               cross( b, r ) * weight.z * rsqrt( max( dot( cross( b, r ), cross( b, r ) ), 1e-20 ) ) +
               cross( l, b ) * weight.w * rsqrt( max( dot( cross( l, b ), cross( l, b ) ), 1e-20 ) );
    n *= rsqrt( max( dot( n, n ), 1e-20 ) );
    // Towards the camera, whatever the handedness of the view space.
    return dot( n, -centre ) < 0.0 ? -n : n;
}

// Depth pyramid ('DepthMips' 1, after XeGTAO): w of 2 x 2 pixels, the nearest of them and those within 5 % behind it averaged
// (a thin object in front stays, the background behind its edge does not pull it back).
float Reduce( float4 w )
{
    float nearest = min( min( w.x, w.y ), min( w.z, w.w ) );
    float4 weight = saturate( 1.0 - ( w - nearest ) / ( nearest * 0.05 + 1e-3 ) );
    return dot( weight, w ) / max( dot( weight, 1.0 ), 1e-6 );
}

float DepthHalfPS( float4 position : SV_Position ) : SV_Target
{
    int2 q = int2( position.xy ) * 2;
    int2 last = int2( g_Size.xy ) - 1;
    float4 z = float4( g_Depth.Load( int3( min( q, last ), 0 ) ), g_Depth.Load( int3( min( q + int2( 1, 0 ), last ), 0 ) ),
                       g_Depth.Load( int3( min( q + int2( 0, 1 ), last ), 0 ) ), g_Depth.Load( int3( min( q + int2( 1, 1 ), last ), 0 ) ) );
    return Reduce( g_Size.z / max( 1.0 - z, 1e-6 ) );
}

float DepthMipPS( float4 position : SV_Position ) : SV_Target
{
    int2 q = int2( position.xy ) * 2;
    return Reduce( float4( g_DepthSource.Load( int3( q, 0 ) ), g_DepthSource.Load( int3( q + int2( 1, 0 ), 0 ) ),
                           g_DepthSource.Load( int3( q + int2( 0, 1 ), 0 ) ), g_DepthSource.Load( int3( q + int2( 1, 1 ), 0 ) ) ) );
}

// GTAO slice integral of cos(t - g) |sin t| from 0 to t (t signed from the view direction, g the angle of the projected
// normal): rises monotonically over the hemisphere g - pi/2 ... g + pi/2, from there to there it is cos g + g sin g.
float2 SliceCdf( float2 t, float g )
{
    return sign( t ) * 0.25 * ( cos( g ) - cos( 2.0 * t - g ) + 2.0 * t * sin( g ) );
}

float2 MainPS( float4 position : SV_Position ) : SV_Target
{
    int2 pixel = int2( position.xy );
    // 'ShadingRate' 1: half the pixels each frame in a checkerboard, the others keep the last value.
    if ( g_Quality.z > 0.5 && ( ( uint( pixel.x + pixel.y ) & 1u ) != ( uint( g_Size.w ) & 1u ) ) )
        discard;

    float z = g_Depth.Load( int3( pixel, 0 ) );
    float d = ReshadeDepth( z );
    float fade = z >= 0.99999 ? 0.0 : FadeOf( d ) * FogVisibility( z );
    if ( fade < 0.001 )
        return float2( 1.0, d );

    float w_centre = DepthW( z );
    float3 ray_centre = RayAt( pixel + 0.5 );
    float3 p = ray_centre * w_centre;
    float3 p_exact = p;
    // The ray is linear in the pixel: one step in x or y adds these (instead of a matrix per sample).
    float3 ray_dx = g_ViewOfClip[0].xyz * ( 2.0 * g_Viewport.z / ( g_Size.x * g_Viewport.x ) );
    float3 ray_dy = g_ViewOfClip[1].xyz * ( 2.0 * g_Viewport.w / ( g_Size.y * g_Viewport.y ) );
    // B35: the depth is 24 bit; on sloped faces further away it falls into small steps (a step in w is w^2 / n x 2^-24, at
    // 300 units several centimetres). The normal of the direct neighbours followed the steps and the near samples counted
    // every step edge as occlusion: thin dark dashes along the face. The normal also from neighbours two pixels away (across
    // the steps), and the samples are taken from a point lifted along the normal by three steps.
    float sure;
    float sure_wide;
    float3 n1 = NormalAt( pixel, p, sure, 1 );
    float3 n2 = NormalAt( pixel, p, sure_wide, 2 );
    float3 n = normalize( n1 + n2 );
    if ( dot( n, -p ) < 0.0 )
        n = -n;
    sure = min( sure, sure_wide );
    float quantum = w_centre * w_centre / max( g_Size.z, 1e-6 ) * exp2( -24.0 );
    float3 lift = n * ( 3.0 * quantum );
    p *= 0.996;
    float3 v = normalize( -p );
    bool exact = g_Method.x > 0.5;

    // Radius: a share of the width on the screen ('IncreaseRadiusWithDistance' 1, then it grows in the world with the distance)
    // or metres in the world. Its size in the world at this depth gives the thickness of the objects.
    // The pixel size follows the depth along the view axis (as MXAO's p.z), not the distance along the ray: with the ray
    // length the radius shrank towards the screen edges (wide field of view) although the plane at that depth is flat.
    float metres_per_pixel = w_centre * ( 2.0 * g_Viewport.z / g_Size.x ) / abs( g_Viewport.x );
    // As MXAO: screen radius 0.03 x 'SampleRadius' of the width, or 'SampleRadius' / 2 metres in the world.
    float radius_pixels = g_Filter.y > 0.5 ? g_Params.x * 0.03 * g_Size.x : g_Params.x * 0.5 / max( metres_per_pixel, 1e-6 );
    radius_pixels = min( radius_pixels, g_Size.x * 0.25 );
    float radius_world = radius_pixels * metres_per_pixel;
    float thickness = log( 1.0 + radius_world ) / 3.0 * g_Method.y;

    // Interleaved 4 x 4 jitter. The slice angle and the sample offsets get two different permutations of the 16 values (MXAO
    // uses one value for both): the 4 x 4 block of the filter then holds 16 different pairs of angle and offset spread over
    // the plane, not 16 pairs on the diagonal where a small angle always came with a near offset (less noise after the filter).
    // With 'Temporal' both move on each frame (R2 sequence), so the accumulation sees new directions and offsets.
    uint index = ( uint( pixel.x ) & 3u ) + ( uint( pixel.y ) & 3u ) * 4u;
    float jitter = ( float( ( index * 11u ) % 16u ) + 0.5 ) / 16.0;
    float jitter_offset = ( float( reversebits( index ) >> 28 ) + 0.5 ) / 16.0;
    if ( g_Filter.z > 0.0 )
    {
        float frame = float( uint( g_Size.w ) & 255u );
        jitter = frac( jitter + frame * 0.7548777 );
        jitter_offset = frac( jitter_offset + frame * 0.5698403 );
    }

    int slices = int( g_Quality.x );
    int samples = int( g_Quality.y );
    int levels = int( g_Filter.w );
    float visibility = 0.0;
    float weight_sum = 0.0;
    [loop] for ( int slice = 0; slice < slices; ++slice )
    {
        float angle = ( slice + jitter ) * PI / slices;
        float2 dir = float2( cos( angle ), sin( angle ) );
        // The slice plane through the view direction and this screen direction, from the ray one pixel further at the same depth.
        float3 ray_next = ray_centre + ray_dx * dir.x + ray_dy * dir.y;
        float3 axis = ray_next * ( length( p_exact ) * rsqrt( max( dot( ray_next, ray_next ), 1e-12 ) ) ) - p_exact;
        axis -= v * dot( axis, v );
        axis *= rsqrt( max( dot( axis, axis ), 1e-20 ) );
        float3 plane_normal = cross( axis, v );
        float3 projected = n - plane_normal * dot( n, plane_normal );
        float projected_length = length( projected );
        if ( projected_length < 1e-4 )
            continue;
        projected /= projected_length;
        float normal_angle = FastAcos( clamp( dot( projected, v ), -1.0, 1.0 ) ) *( dot( projected, axis ) < 0.0 ? -1.0 : 1.0 );
        // 'Method' 1: the hemisphere of the slice as a share of its weighted solid angle (total cos g + g sin g).
        float2 hemisphere = float2( normal_angle - 0.5 * PI, normal_angle + 0.5 * PI );
        float2 cdf_ends = SliceCdf( hemisphere, normal_angle );
        float slice_total = max( cdf_ends.y - cdf_ends.x, 1e-6 );

        uint open = 0xFFFFFFFFu;
        [unroll] for ( int side = 0; side < 2; ++side )
        {
            float sign_side = side == 0 ? 1.0 : -1.0;
            [loop] for ( int i = 0; i < samples; ++i )
            {
                float s = ( i + jitter_offset ) / samples;
                float2 offset = dir * sign_side * max( s * s * radius_pixels, 1.0 + i );
                float2 at = position.xy + offset;
                if ( any( at < 0.0 ) || any( at >= g_Size.xy ) )
                    break;
                int2 tap = int2( at );
                float2 step_pixels = float2( tap - pixel );
                float3 ray_sample = ray_centre + ray_dx * step_pixels.x + ray_dy * step_pixels.y;
                // 'DepthMips': far samples from the pyramid (level from log2 of the distance, as XeGTAO's offset 3.3).
                int level = levels > 0 ? clamp( int( log2( length( offset ) ) - 3.3 ), 0, levels ) : 0;
                float w_sample;
                if ( level > 0 )
                    w_sample = g_DepthMips.Load( int3( min( tap >> level, ( int2( g_Size.xy ) >> level ) - 1 ), level - 1 ) );
                else
                    w_sample = DepthW( g_Depth.Load( int3( tap, 0 ) ) );
                float3 delta = ray_sample * w_sample - ( p + lift );
                // Back face: 'Method' 0 behind the centre along its view ray, 'Method' 1 behind the sample along its own.
                float3 back = exact ? delta + ray_sample * ( thickness * rsqrt( max( dot( ray_sample, ray_sample ), 1e-12 ) ) )
                                    : delta - v * thickness;
                // Angles from the view direction towards the axis side, front face and back face of the sample.
                float2 cosines = float2( dot( delta, v ), dot( back, v ) ) * rsqrt( max( float2( dot( delta, delta ), dot( back, back ) ), 1e-12 ) );
                float2 horizon = FastAcos( clamp( cosines, -1.0, 1.0 ) );
                horizon *= sign_side;
                // B35 ('AngleBias', as HBAO's angle bias): the roads are flat triangles a few degrees apart. With the radius
                // over several of them each triangle saw its neighbours slightly above its own plane and got its own grey value,
                // with a hard step at every edge. The horizon is moved towards the face by the bias: rises below it occlude
                // nothing, steeper ones (cars, walls, kerbs) lose only that angle.
                horizon += sign_side * g_Method.w;
                float2 sector;
                if ( exact )
                {
                    float2 t = clamp( horizon, hemisphere.x, hemisphere.y );
                    sector = saturate( ( SliceCdf( t, normal_angle ) - cdf_ends.x ) / slice_total );
                }
                else
                {
                    // Into 0 ... 1 over the hemisphere around the normal, cosine weighted (smoothstep).
                    sector = saturate( ( horizon - normal_angle ) / PI + 0.5 );
                    sector = sector * sector * ( 3.0 - 2.0 * sector );
                }
                float2 range = float2( min( sector.x, sector.y ), max( sector.x, sector.y ) );
                uint first = uint( range.x * 32.0 );
                uint count = uint( range.y * 32.0 ) - first;
                uint bits = count >= 32u ? 0xFFFFFFFFu : ( ( ( 1u << count ) - 1u ) << first );
                open &= ~bits;
            }
        }
        float slice_weight = exact ? projected_length * slice_total : projected_length;
        visibility += countbits( open ) / 32.0 * slice_weight;
        weight_sum += slice_weight;
    }
    float ao = weight_sum > 0.0 ? saturate( visibility / weight_sum ) : 1.0;

    // 'Amount': up to 1 the share of the occlusion, above that a power.
    ao = lerp( 1.0, ao, saturate( g_Params.y ) );
    if ( g_Params.y > 1.0 )
        ao = lerp( ao, ao * ao, saturate( g_Params.y - 1.0 ) );
    ao = lerp( 1.0, ao, fade );
    // Unsure normal (edge): depth negative, the filter takes more of the neighbours there.
    return float2( ao, sure < 0.5 ? -d : d );
}

// Edge aware regression: occlusion as a linear function of depth over a 4 x 4 block (shifted the other way in the second pass).
// B35: in the first pass with the checkerboard ('ShadingRate' 1) only the half computed this frame: the other half keeps the
// occlusion and depth of the last frame, not reprojected, and dragged streaks along while moving. The variance has a floor
// relative to the depth (1 % of it): on flat ground far away the depth hardly changes within the block, the slope blew the
// noise up into wavy bands along the depth lines. The result stays within the taps used (no bright halos from extrapolation).
// 'depth' is the depth of this frame at the pixel (for a skipped pixel of the checkerboard the stored one is the last frame's).
float FilterAt( int2 pixel, float flip, float depth_here, out float lowest, out float highest )
{
    float4 moments = 0.0; // depth, depth^2, occlusion, occlusion x depth
    float count = 0.0;
    lowest = 1.0;
    highest = 0.0;
    bool fresh_only = flip < 0.5 && g_Quality.z > 0.5;
    uint parity = uint( g_Size.w ) & 1u;
    int2 start = flip > 0.5 ? int2( -2, -2 ) : int2( -1, -1 );
    [unroll] for ( int y = 0; y < 4; ++y )
    {
        [unroll] for ( int x = 0; x < 4; ++x )
        {
            int2 q = clamp( pixel + start + int2( x, y ), int2( 0, 0 ), int2( g_Size.xy ) - 1 );
            float w = ( !fresh_only || ( ( uint( q.x + q.y ) & 1u ) == parity ) ) ? 1.0 : 0.0;
            float2 tap = g_Ao.Load( int3( q, 0 ) );
            float depth = abs( tap.y );
            moments += w * float4( depth, depth * depth, tap.x, tap.x * depth );
            count += w;
            if ( w > 0.0 )
            {
                lowest = min( lowest, tap.x );
                highest = max( highest, tap.x );
            }
        }
    }
    moments /= max( count, 1.0 );
    float variance = moments.y - moments.x * moments.x;
    float floor_variance = max( moments.x * moments.x * 1e-4, depth_here < 0.0 ? exp2( -12.0 ) : exp2( -30.0 ) );
    float slope = ( moments.w - moments.x * moments.z ) / max( variance, floor_variance );
    return clamp( slope * abs( depth_here ) + moments.z - slope * moments.x, lowest, highest );
}

// 'Temporal': the result of the last frame at the place this pixel had there (world camera of both frames, depth of this one),
// bilinear over the taps whose depth matches (1.5 %), clamped to the range of this frame's taps around it, mixed in.
float Accumulate( int2 pixel, float z, float current, float lowest, float highest )
{
    if ( g_Method.z < 0.5 || z >= 0.99999 )
        return current;
    float3 p = RayAt( pixel + 0.5 ) * DepthW( z );
    float4 q = mul( float4( p, 1.0 ), g_PrevClipOfView );
    if ( q.z <= g_Size.z * 0.5 )
        return current;
    float2 game = float2( q.x / q.z * g_PrevViewport.x + g_PrevViewport.z, q.y / q.z * g_PrevViewport.y + g_PrevViewport.w );
    float2 at = game * g_Size.xy / ( 2.0 * g_PrevViewport.zw ) - 0.5;
    int2 base = int2( floor( at ) );
    float2 f = at - base;
    float sum = 0.0;
    float weight = 0.0;
    [unroll] for ( int i = 0; i < 4; ++i )
    {
        int2 o = int2( i & 1, i >> 1 );
        int2 t = base + o;
        if ( any( t < 0 ) || any( t >= int2( g_Size.xy ) ) )
            continue;
        float2 tap = g_History.Load( int3( t, 0 ) );
        float bilinear = ( o.x ? f.x : 1.0 - f.x ) * ( o.y ? f.y : 1.0 - f.y );
        float w_tap = WOfReshade( abs( tap.y ) );
        float match = abs( w_tap - q.z ) < 0.015 * q.z + 0.02 ? 1.0 : 0.0;
        sum += bilinear * match * tap.x;
        weight += bilinear * match;
    }
    if ( weight < 0.3 )
        return current;
    float history = clamp( sum / weight, lowest - 0.05, highest + 0.05 );
    return lerp( current, history, g_Filter.z * saturate( weight ) );
}

float2 FilterPS( float4 position : SV_Position ) : SV_Target
{
    int2 pixel = int2( position.xy );
    float2 centre = g_Ao.Load( int3( pixel, 0 ) );
    // Depth of this frame (a skipped pixel of the checkerboard still holds the last frame's); the sign (unsure normal) stays.
    float z = g_Depth.Load( int3( pixel, 0 ) );
    float depth = ReshadeDepth( z ) * ( centre.y < 0.0 ? -1.0 : 1.0 );
    float lowest = centre.x;
    float highest = centre.x;
    float ao = centre.x;
    // With 'Temporal' the filter also gives the range for the history; with the checkerboard its result is needed in any case.
    if ( g_Filter.x > 0.5 || g_Filter.z > 0.0 )
    {
        float filtered = FilterAt( pixel, 0.0, depth, lowest, highest );
        if ( g_Filter.x > 0.5 || g_Quality.z > 0.5 )
            ao = filtered;
    }
    if ( g_Filter.z > 0.0 )
        ao = Accumulate( pixel, z, ao, lowest, highest );
    return float2( ao, depth );
}

// 'MultiBounce' (Jimenez et al. 2016): light the near surroundings throw back, from the occlusion and the albedo (here the
// colour of the frame as a stand in): bright surfaces lose less, colours stay.
float3 MultiBounce( float v, float3 albedo )
{
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max( v, ( ( v * a + b ) * v + c ) * v );
}

// The factor for the frame: the colour taken into a soft HDR range (c^2 / (1.1 - c^2)), darkened there, mapped back. So bright
// parts lose little, dark corners get darker. Written as factor = new / old and blended multiplicatively over the MSAA frame.
float4 ApplyPS( float4 position : SV_Position ) : SV_Target
{
    int2 pixel = int2( position.xy );
    float ao;
    if ( g_Filter.x > 1.5 )
    {
        float lowest;
        float highest;
        ao = FilterAt( pixel, 1.0, g_Ao.Load( int3( pixel, 0 ) ).y, lowest, highest );
    }
    else
        ao = g_Ao.Load( int3( pixel, 0 ) ).x;
    if ( g_Quality.w > 0.5 )
        return float4( ao.xxx, 1.0 );
    float3 colour = saturate( g_Frame.Load( int3( pixel, 0 ) ).rgb );
    float3 hdr = colour * colour;
    float3 occlusion = g_Apply.x > 0.5 ? MultiBounce( ao, hdr ) : ao.xxx;
    hdr = hdr / ( 1.1 - hdr );
    hdr *= occlusion;
    float3 result = sqrt( 1.1 * hdr / ( hdr + 1.0 ) );
    float3 factor = colour > 1e-3 ? saturate( result / colour ) : occlusion;
    return float4( factor, 1.0 );
}
)hlsl";

    // Shared blur chain (REFLEXION-WETTER.md 9.16a): the scene copy filtered down into 5 levels from half size on (dual Kawase).
    // BLUR_DOWN: target half the source; centre (the mean of the middle 2 x 2) and four diagonal lookups one source texel out,
    // each the mean of a 2 x 2 block. BLUR_UP: a level drawn into the next larger size with a tent of 8 lookups, so the small
    // levels are soft instead of blocky. The source view holds one level; its size comes from GetDimensions.
    // Fullscreen triangle of REFLECT's MainVS.
    inline constexpr char BLUR[] = R"hlsl(
Texture2D g_Source : register( t0 );
SamplerState g_Sampler : register( s0 );

float4 DownPS( float4 position : SV_Position ) : SV_Target
{
    float2 size;
    g_Source.GetDimensions( size.x, size.y );
    float2 texel = 1.0 / size;
    float2 uv = position.xy * 2.0 * texel;
    float4 sum = g_Source.SampleLevel( g_Sampler, uv, 0 ) * 4.0;
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( -texel.x, -texel.y ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( texel.x, -texel.y ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( -texel.x, texel.y ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( texel.x, texel.y ), 0 );
    return sum / 8.0;
}

float4 UpPS( float4 position : SV_Position ) : SV_Target
{
    float2 size;
    g_Source.GetDimensions( size.x, size.y );
    float2 texel = 1.0 / size;
    // Pixel centre of the target, twice the source size, in source texture coordinates.
    float2 uv = position.xy * 0.5 * texel;
    float4 sum = g_Source.SampleLevel( g_Sampler, uv + float2( -texel.x, 0.0 ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( texel.x, 0.0 ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( 0.0, -texel.y ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( 0.0, texel.y ), 0 );
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( -0.5 * texel.x, -0.5 * texel.y ), 0 ) * 2.0;
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( 0.5 * texel.x, -0.5 * texel.y ), 0 ) * 2.0;
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( -0.5 * texel.x, 0.5 * texel.y ), 0 ) * 2.0;
    sum += g_Source.SampleLevel( g_Sampler, uv + float2( 0.5 * texel.x, 0.5 * texel.y ), 0 ) * 2.0;
    return sum / 12.0;
}
)hlsl";

    // Sky edge (BUGS.md B32, '[Dx11] SkyEdgeFill'). g_Frame: copy of the frame right after the sky, alpha = share of the clear
    // colour g_Clear left in the pixel. ColumnPS (target: render width x 2): row 0 the sky colour and row of the first fully
    // covered pixel from the top, row 1 the same from the bottom, row -1 without sky in the column. FillPS: the clear colour
    // left in a pixel becomes the edge colour of its column (between two edges a blend of both). Fullscreen triangle of
    // REFLECT's MainVS.
    inline constexpr char SKY_EDGE[] = R"hlsl(
Texture2D g_Frame : register( t0 );
Texture2D<float4> g_Table : register( t1 );

cbuffer SkyEdge : register( b0 )
{
    float4 g_Clear;
    float4 g_Size; // render width, height, smoothing radius, seam outside the band
    float4 g_Seam; // seam inside the band
};

// The clear colour as it is in the frame for column x on one side of the band: the top (upper) or bottom pixel when only
// clear colour is left there (it may be a flat sky plane instead of the clear, or graded by the night settings), else the
// colour of the clear. Per side: the flat plane often covers only the upper part, below the band is the real clear colour
// (B32, 2026-11-19: the top pixel taken for the bottom turned the fill there dark blue).
float3 ClearOf( int x, bool upper )
{
    float4 edge = g_Frame.Load( int3( x, upper ? 0 : (int)g_Size.y - 1, 0 ) );
    return edge.a > 0.98 ? edge.rgb : g_Clear.rgb;
}

float4 ColumnPS( float4 position : SV_Position ) : SV_Target
{
    int x = (int)position.x;
    int height = (int)g_Size.y;
    bool top = position.y < 1.0;
    float3 clear = ClearOf( x, top );
    int y = top ? 0 : height - 1;
    int step = top ? 1 : -1;
    [loop]
    for (int i = 0; i < height; ++i)
    {
        if (g_Frame.Load( int3( x, y, 0 ) ).a < 0.02)
        {
            // Mean of 8 rows from 2 pixels further in: clear of the soft (multisampled) edge and of single bright texels.
            float3 sum = 0.0;
            [unroll]
            for (int k = 2; k < 10; ++k)
            {
                float4 inner = g_Frame.Load( int3( x, clamp( y + step * k, 0, height - 1 ), 0 ) );
                sum += saturate( inner.rgb - clear * inner.a );
            }
            return float4( sum / 8.0, (float)y );
        }
        y += step;
    }
    return float4( 0.0, 0.0, 0.0, -1.0 );
}

// The edge colours of the columns around (g_Size.z columns to each side) averaged, the edge row stays that of the column.
// A column without any sky pixel (the dome left the screen there, e.g. a high camera looking down) takes colour and row of
// the nearest columns with sky to the left and right, blended by distance, so its clear colour is filled too.
float4 SmoothPS( float4 position : SV_Position ) : SV_Target
{
    int2 p = int2( position.xy );
    int width = (int)g_Size.x;
    int radius = (int)g_Size.z;
    float4 own = g_Table.Load( int3( p, 0 ) );
    if (own.w < 0.0)
    {
        float4 left = float4( 0.0, 0.0, 0.0, -1.0 );
        float4 right = left;
        int dl = 0;
        int dr = 0;
        [loop]
        for (int d = 1; d < width && ( left.w < 0.0 || right.w < 0.0 ); ++d)
        {
            if (left.w < 0.0 && p.x - d >= 0)
            {
                left = g_Table.Load( int3( p.x - d, p.y, 0 ) );
                dl = d;
            }
            if (right.w < 0.0 && p.x + d < width)
            {
                right = g_Table.Load( int3( p.x + d, p.y, 0 ) );
                dr = d;
            }
            if (p.x - d < 0 && p.x + d >= width)
                break;
        }
        if (left.w < 0.0)
            return right;
        if (right.w < 0.0)
            return left;
        return lerp( left, right, (float)dl / (float)( dl + dr ) );
    }
    float3 sum = 0.0;
    float count = 0.0;
    [loop]
    for (int k = -radius; k <= radius; ++k)
    {
        float4 other = g_Table.Load( int3( clamp( p.x + k, 0, width - 1 ), p.y, 0 ) );
        if (other.w >= 0.0)
        {
            sum += other.rgb;
            count += 1.0;
        }
    }
    return float4( sum / max( count, 1.0 ), own.w );
}

float4 FillPS( float4 position : SV_Position ) : SV_Target
{
    int2 p = int2( position.xy );
    float4 frame = g_Frame.Load( int3( p, 0 ) );
    float4 upper = g_Table.Load( int3( p.x, 0, 0 ) );
    float4 lower = g_Table.Load( int3( p.x, 1, 0 ) );
    if (upper.w < 0.0)
        discard;
    float y = position.y;
    // Soft seam: g_Size.w pixels to both sides of the nearer edge blend into the edge colour, so the fringe of the
    // sky texture (bright filtered texels at its alpha border) and mixed edge samples leave no line.
    float edge = y < 0.5 * ( upper.w + lower.w ) ? upper.w : lower.w;
    // Positive inside the band (below the upper edge, above the lower one).
    float inside = edge == upper.w ? y - edge : edge - y;
    float seam = inside >= 0.0 ? ( g_Seam.x > 0.0 ? saturate( 1.0 - inside / g_Seam.x ) : 0.0 ) : saturate( 1.0 + inside / g_Size.w );
    seam = seam * seam * ( 3.0 - 2.0 * seam );
    if (frame.a < 1.0 / 512.0 && seam <= 0.0)
        discard;
    float3 fill = y <= upper.w ? upper.rgb : y >= lower.w ? lower.rgb :
        lerp( upper.rgb, lower.rgb, saturate( ( y - upper.w ) / max( lower.w - upper.w, 1.0 ) ) );
    // Base under the sky: a pixel with only clear colour left is its own base, else the clear colour of its side.
    float3 base = frame.a > 0.98 ? frame.rgb : ClearOf( p.x, y < 0.5 * ( upper.w + lower.w ) );
    float3 colour = saturate( frame.rgb + ( fill - base ) * frame.a );
    // 'SkyEdgeFill = 2': red = share of clear colour replaced (bright red = pure fill), green = soft seam, blue line = edge row.
    if (g_Clear.w > 1.5)
        return float4( frame.a, 0.6 * seam, abs( y - edge ) < 1.0 ? 1.0 : 0.0, 1.0 );
    return float4( lerp( colour, fill, seam ), 1.0 );
}
)hlsl";
}