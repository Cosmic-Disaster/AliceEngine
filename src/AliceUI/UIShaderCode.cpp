#include "AliceUI/UIShaderCode.h"

namespace Alice
{
	namespace AliceUIShader
	{
		const char* UIVS = R"(
cbuffer UIConstants : register(b0)
{
    float4x4 gViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.Position = mul(float4(input.Position, 1.0f), gViewProj);
    o.TexCoord = input.TexCoord;
    o.Color = input.Color;
    return o;
}
)";

		const char* UIPixelPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer UIPixelConstants : register(b1)
{
    float4 gOutlineColor;
    float4 gGlowColor;
    float4 gVitalColor;
    float4 gVitalBgColor;
    float4 gParams0;
    float4 gParams1;
    float4 gParams2;
    float4 gParams3;
    float4 gParams4;
    float4 gParams5;
    float4 gTime;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float ComputeOutline(float2 uv)
{
    if (gParams0.y < 0.5f)
        return 0.0f;

    uint w, h;
    gTexture.GetDimensions(w, h);
    float2 texel = 1.0f / float2(max(w, 1u), max(h, 1u));
    float thickness = max(gParams0.x, 0.0f);
    float2 o = texel * thickness;

    float a = gTexture.Sample(gSampler, uv).a;
    float m = 0.0f;
    m = max(m, gTexture.Sample(gSampler, uv + float2( o.x, 0.0f)).a);
    m = max(m, gTexture.Sample(gSampler, uv + float2(-o.x, 0.0f)).a);
    m = max(m, gTexture.Sample(gSampler, uv + float2(0.0f,  o.y)).a);
    m = max(m, gTexture.Sample(gSampler, uv + float2(0.0f, -o.y)).a);

    return saturate(m - a);
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = input.TexCoord;
    float4 tex = gTexture.Sample(gSampler, uv);
    float4 color = tex * input.Color;

    // Vital sign graph
    if (gParams4.x > 0.5f)
    {
        float amp = gParams4.y;
        float freq = gParams4.z;
        float speed = gParams4.w;
        float thickness = gParams5.x;
        float wave = sin((uv.x * freq * 6.2831853f) + gTime.x * speed) * amp;
        float y = 0.5f + wave;
        float dist = abs(uv.y - y);
        float lineMask = smoothstep(thickness, 0.0f, dist);
        color = lerp(gVitalBgColor, gVitalColor, lineMask);
    }

    // Radial cooldown / mask
    if (gParams0.z > 0.5f)
    {
        float2 d = uv - 0.5f;
        float r = length(d);
        float inner = gParams1.x;
        float outer = gParams1.y;
        float soft = max(gParams1.z, 0.0001f);
        float ring = smoothstep(inner, inner + soft, r) * (1.0f - smoothstep(outer - soft, outer, r));

        float ang = atan2(d.y, d.x) + 1.5707963f;
        if (ang < 0.0f) ang += 6.2831853f;
        float ang01 = ang / 6.2831853f;
        float offset = gParams2.x / 6.2831853f;
        ang01 = frac(ang01 + offset);

        float fill = saturate(gParams0.w);
        float cw = gParams1.w;
        float mask = (cw > 0.5f) ? step(ang01, fill) : step(1.0f - ang01, fill);

        float dim = saturate(gParams2.y);
        color.rgb *= lerp(dim, 1.0f, mask);
        color.a *= ring;
    }

    // Outline
    float outline = ComputeOutline(uv);
    if (outline > 0.0f)
    {
        float4 o = gOutlineColor;
        o.a *= outline;
        color = lerp(o, color, saturate(tex.a));
    }

    // Glow sweep
    if (gParams2.z > 0.5f)
    {
        float angle = gParams3.z;
        float2 dir = float2(cos(angle), sin(angle));
        float phase = dot(uv - 0.5f, dir) + gTime.x * gParams3.y;
        float band = abs(frac(phase) - 0.5f) * 2.0f;
        float width = max(gParams3.x, 0.001f);
        float glow = smoothstep(width, 0.0f, band);
        color.rgb += gGlowColor.rgb * gParams2.w * glow;
    }

    // Grayscale
    if (gParams3.w > 0.0f)
    {
        float lum = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
        color.rgb = lerp(color.rgb, lum.xxx, saturate(gParams3.w));
    }

    // Global alpha
    color.a *= gTime.y;

    return color;
}
)";

		const char* UIGrayPS = R"(
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    float4 tex = gTexture.Sample(gSampler, input.TexCoord);
    float lum = dot(tex.rgb, float3(0.299f, 0.587f, 0.114f));
    float4 gray = float4(lum, lum, lum, tex.a);
    return gray * input.Color;
}
)";
	}
}
