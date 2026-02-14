#pragma once

namespace Alice
{
    /// 디퍼드 렌더링 전용 셰이더 코드
    class DeferredShader
    {
    public:
        // G-Buffer Vertex Shader
        inline static const char* GBufferVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    float3 N = normalize(mul(float4(input.Normal, 0.0f), gWorld).xyz);
    
    // 아웃라인: 모든 쉐이딩 모드에서 normal 방향으로 확장 (아웃라인 두께가 0보다 클 때만)
    float3 posOffset = (gOutlineWidth > 0.0f) ? (N * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(input.Position + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal = N;
    
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Instanced Vertex Shader (정적 메시 인스턴싱)
        inline static const char* GBufferInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position   : POSITION;
    float3 Normal     : NORMAL;
    float2 TexCoord   : TEXCOORD0;
    float4 iWorld0    : INSTANCE_WORLD0;
    float4 iWorld1    : INSTANCE_WORLD1;
    float4 iWorld2    : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    // 인스턴스 월드 행렬 복원 (전치 행렬 기준)
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);
    
    float3 N = normalize(mul(world, float4(input.Normal, 0.0f)).xyz);
    
    float3 posOffset = (gOutlineWidth > 0.0f) ? (N * gOutlineWidth) : float3(0, 0, 0);
    float4 posW = mul(world, float4(input.Position + posOffset, 1.0f));
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal = N;
    
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Skinned Vertex Shader
        inline static const char* GBufferSkinnedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL; // 아웃라인용 스무스 노멀
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];
    
    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(input.Normal, M3));
    float3 skinnedT = normalize(mul(input.Tangent, M3));
    float3 skinnedB = normalize(mul(input.Binormal, M3));
    
    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    // 아웃라인: 스무스 노멀 방향으로 확장 (하드 엣지 모델의 아웃라인 끊김 방지)
    // 스무스 노멀도 스키닝 변환을 적용해야 함
    float3 skinnedSmoothN = normalize(mul(input.SmoothNormal, M3));
    float3 smoothN = normalize(mul(float4(skinnedSmoothN, 0.0f), gWorld).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;
    
    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;
    
    return output;
}
)";

        // G-Buffer Skinned Instanced Vertex Shader (본 없는 FBX 인스턴싱용)
        inline static const char* GBufferSkinnedInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL;

    // 인스턴스 월드 행렬 (행 3개)
    float4 iWorld0      : INSTANCE_WORLD0;
    float4 iWorld1      : INSTANCE_WORLD1;
    float4 iWorld2      : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // 인스턴스 월드 행렬 복원 (마지막 행은 (0,0,0,1))
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);

    //float3 N = normalize(mul(float4(input.Normal, 0.0f), world).xyz);

    // 아웃라인: 스무스 노멀 방향으로 확장
    //float3 smoothN = normalize(mul(float4(input.SmoothNormal, 0.0f), world).xyz);
    float3 N = normalize(mul(world, float4(input.Normal, 0.0f)).xyz);
    float3 smoothN = normalize(mul(world, float4(input.SmoothNormal, 0.0f)).xyz);

    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);

    //float4 posW = mul(float4(input.Position + posOffset, 1.0f), world);
    float4 posW = mul(world, float4(input.Position + posOffset, 1.0f));
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    //output.TangentW = normalize(mul(float4(input.Tangent, 0.0f), world).xyz);
    //output.BitanW   = normalize(mul(float4(input.Binormal, 0.0f), world).xyz);
    output.TangentW = normalize(mul(world, float4(input.Tangent, 0.0f)).xyz);
    output.BitanW   = normalize(mul(world, float4(input.Binormal, 0.0f)).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // G-Buffer Pixel Shader
        inline static const char* GBufferPS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VertexOut
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

struct GBufferOut
{
    float4 NormalRoughness : SV_Target0;
    float4 Metalness       : SV_Target1;
    float4 BaseColor       : SV_Target2;
    float4 ToonParams      : SV_Target3;
};

Texture2D  g_DiffuseMap : register(t0);
Texture2D  g_NormalMap  : register(t1);
SamplerState g_Sam : register(s0);

float DitherThreshold(float2 pos)
{
    // Interleaved gradient noise (per-pixel hash, less visible grid)
    float n = 0.06711056f * pos.x + 0.00583715f * pos.y;
    return frac(52.9829189f * frac(n));
}

float Pack2x8(float a, float b)
{
    a = saturate(a);
    b = saturate(b);
    float2 enc = floor(float2(a, b) * 255.0f + 0.5f);
    return (enc.x + enc.y * 256.0f) / 65535.0f;
}

GBufferOut main(VertexOut pIn)
{
    GBufferOut gOut;
    float ao = saturate(gAmbientOcclusion);
    float aoPacked = min(ao, 0.999f);
    float shadingEncoded = ((float)gShadingMode + aoPacked) / 8.0f;
    float outlineEncoded = (6.0f + aoPacked) / 8.0f;
    
    // 아웃라인 패스 감지: Width가 0보다 크면 아웃라인용 드로우콜임
    if (gOutlineWidth > 0.0f)
    {
        // 1. Normal/Roughness/Metalness: 조명 연산 방해 안 되게 더미 값
        gOut.NormalRoughness = float4(0.5f, 0.5f, 1.0f, 1.0f);
        gOut.Metalness  = float4(0.0f, 0.0f, 0.0f, 1.0f);
        gOut.ToonParams = float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        // 3. BaseColor: 아웃라인 색상
        // 4. Alpha (ShadingMode + AO) 인코딩: mode 6(OnlyTexture) + AO
        gOut.BaseColor  = float4(gOutlineColor, saturate(outlineEncoded));
        
        return gOut;
    }
    
    // --- 아래는 기존 원본 물체 렌더링 로직 (변화 없음) ---
    float4 textureColor = float4(1,1,1,1);
    if (gUseTexture != 0)
    {
        textureColor = g_DiffuseMap.Sample(g_Sam, pIn.TexCoord);
    }
    
    float alphaTex = textureColor.a * gMaterialColor.a;
    // 알파 테스트는 텍스처 알파에만 적용 (머티리얼 알파는 블렌딩으로 처리)
    if (gUseTexture != 0)
    {
        clip(textureColor.a - 0.1f);
    }
    // NDC 기반 디더링으로 투명도 처리 (알파 블렌딩 대신 화면 도트 컷아웃)
    float alpha = saturate(alphaTex);
    if (alpha < 1.0f)
    {
        float threshold = DitherThreshold(pIn.Position.xy);
        clip(alpha - threshold);
    }
    
    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
    {
        baseColor *= textureColor.rgb;
    }
    
    float3 N = normalize(pIn.Normal);
    if (gEnableNormalMap != 0)
    {
        float3 T = normalize(pIn.TangentW);
        float3 B = normalize(pIn.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = g_NormalMap.Sample(g_Sam, pIn.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y;
        // 노말맵 강도 조절: X, Y 성분에만 Strength를 곱하고 정규화
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }
    
    float metalness = saturate(gMetalness);
    float roughness = saturate(gRoughness);
    
    // Normal을 [0,1] 범위로 인코딩하여 저장 (LightPS에서 디코딩)
    float3 normalEncoded = N * 0.5f + 0.5f;
    
    gOut.NormalRoughness = float4(normalEncoded, roughness);
    gOut.Metalness  = float4(metalness, saturate(gToonPbrCuts.x), saturate(gToonPbrCuts.y), saturate(gToonPbrCuts.z));
    float toonStrength = saturate(gToonPbrCuts.w);
    float toonBlur = (gToonPbrLevels.w > 0.5f) ? 1.0f : 0.0f;
    float toonStrengthPacked = toonStrength * 0.5f + toonBlur * 0.5f;
    const bool isToonPbr = (gShadingMode == 5 || gShadingMode == 7);
    if (isToonPbr)
    {
        float packedLevels12 = Pack2x8(gToonPbrLevels.x, gToonPbrLevels.y);
        float packedEnv = Pack2x8(gEnvDiffuseStrength, gEnvSpecularStrength);
        gOut.ToonParams = float4(toonStrengthPacked, packedLevels12, saturate(gToonPbrLevels.z), packedEnv);
    }
    else
    {
        gOut.ToonParams = float4(saturate(gEnvDiffuseStrength), saturate(gEnvSpecularStrength), 0.0f, 0.0f);
    }
    // shadingMode + AO를 [0,1] 범위로 인코딩하여 저장
    gOut.BaseColor  = float4(baseColor, saturate(shadingEncoded));
    
    return gOut;
}
)";

        // Decal Pass Vertex Shader
        inline static const char* DecalVS = R"(
cbuffer DecalCB : register(b0)
{
    float4x4 g_DecalWorldViewProj;
    float4x4 g_WorldToDecal;
    float4x4 g_InvViewProj;
    float4   g_ColorOpacity;
    float4   g_UVScaleOffset;
    float2   g_ScreenSize;
    float2   g_Pad;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.Position = mul(float4(input.Position, 1.0f), g_DecalWorldViewProj);
    return o;
}
)";

        // Decal Pass Pixel Shader (DBuffer Albedo)
        inline static const char* DecalPS = R"(
cbuffer DecalCB : register(b0)
{
    float4x4 g_DecalWorldViewProj;
    float4x4 g_WorldToDecal;
    float4x4 g_InvViewProj;
    float4   g_ColorOpacity;
    float4   g_UVScaleOffset;
    float2   g_ScreenSize;
    float2   g_Pad;
};

Texture2D g_DecalAlbedo : register(t0);
Texture2D<float> g_SceneDepth : register(t1);
SamplerState g_Sam : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
};

struct DBufferOut
{
    float4 Albedo : SV_Target0;
};

DBufferOut main(PSInput input)
{
    DBufferOut o;

    float2 uv = input.Position.xy / g_ScreenSize;
    float depth = g_SceneDepth.Sample(g_Sam, uv);
    if (depth >= 0.9999f) discard;

    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = (1.0f - uv.y) * 2.0f - 1.0f;
    float4 clip = float4(ndc, depth, 1.0f);
    float4 posW4 = mul(clip, g_InvViewProj);
    float3 posW = posW4.xyz / max(posW4.w, 1e-6f);

    float3 localPos = mul(float4(posW, 1.0f), g_WorldToDecal).xyz;
    if (abs(localPos.x) > 1.0f || abs(localPos.y) > 1.0f || abs(localPos.z) > 1.0f)
        discard;

    float2 decalUV = localPos.xy * 0.5f + 0.5f;
    decalUV = decalUV * g_UVScaleOffset.xy + g_UVScaleOffset.zw;

    float4 tex = g_DecalAlbedo.Sample(g_Sam, decalUV);
    float alpha = tex.a * g_ColorOpacity.a;
    if (alpha <= 0.001f) discard;

    float3 color = tex.rgb * g_ColorOpacity.rgb;
    o.Albedo = float4(color * alpha, alpha);
    return o;
}
)";

        // Deferred Light Pixel Shader
        inline static const char* LightPS1 = R"(
// PBR 헬퍼 함수들
static const float PI = 3.14159265f;
static const float INV_PI = 0.31830988618f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0f) + 1.0f, 1e-4f);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float ToonLevel(float n)
{
    if (n > 0.95f) return 1.0f;
    if (n > 0.5f)  return 0.7f;
    if (n > 0.2f)  return 0.4f;
    return 0.1f;
}

float ToonStepEditable(float n, float3 cuts, float3 levels, float strength, float blur)
{
    float c1 = saturate(cuts.x);
    float c2 = saturate(cuts.y);
    float c3 = saturate(cuts.z);
    c2 = max(c2, c1 + 1e-4f);
    c3 = max(c3, c2 + 1e-4f);

    float l0 = saturate(levels.x);
    float l1 = saturate(levels.y);
    float l2 = saturate(levels.z);
    float l3 = 1.0f;

    float t = saturate(strength);
    if (blur > 0.5f)
    {
        float w = max(fwidth(n) * 2.0f, 0.02f);
        float s1 = smoothstep(c1 - w, c1 + w, n);
        float s2 = smoothstep(c2 - w, c2 + w, n);
        float s3 = smoothstep(c3 - w, c3 + w, n);

        float level = lerp(l0, l1, s1);
        level = lerp(level, l2, s2);
        level = lerp(level, l3, s3);
        return lerp(n, level, t);
    }

    float level = (n > c3) ? l3 :
                  (n > c2) ? l2 :
                  (n > c1) ? l1 :
                             l0;
    return lerp(n, level, t);
}

float2 Unpack2x8(float v)
{
    float raw = saturate(v) * 65535.0f;
    float hi = floor(raw / 256.0f);
    float lo = raw - hi * 256.0f;
    return float2(lo, hi) / 255.0f;
}


// ShadowCB (register b4)
cbuffer ShadowCB : register(b4)
{
    float4x4 g_ShadowLightViewProj;
    float    g_ShadowBias2;
    float    g_ShadowMapSize2;
    float    g_ShadowPCFRadius2;
    int      g_ShadowEnabled2;
    float3   g_ShadowPad2;
};

// 그림자 계산 함수 (PCF)
float CalcShadowFactorDeferred(float3 posW, Texture2D<float> shadowMap, SamplerComparisonState shadowSampler)
{
    if (g_ShadowEnabled2 == 0) return 1.0f;

    float4 shadowPos = mul(float4(posW, 1.0f), g_ShadowLightViewProj);
    shadowPos.xyz /= shadowPos.w;

    float2 shadowTex;
    shadowTex.x = shadowPos.x * 0.5f + 0.5f;
    shadowTex.y = -shadowPos.y * 0.5f + 0.5f;
    float depth = shadowPos.z;

    if (shadowTex.x < 0.0f || shadowTex.x > 1.0f || shadowTex.y < 0.0f || shadowTex.y > 1.0f)
        return 1.0f;

    const float2 texelSize = float2(1.0f, 1.0f) / max(g_ShadowMapSize2, 1.0f);
    const float2 pcfStep = max(g_ShadowPCFRadius2, 0.0f) * texelSize;

    float sum = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * pcfStep;
            sum += shadowMap.SampleCmpLevelZero(shadowSampler, shadowTex + offset, depth - g_ShadowBias2);
        }
    }
    return sum / 9.0f;
}

// 구조체 정의
struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// G-Buffer 텍스처 (압축)
Texture2D g_NormalRoughness : register(t0);
Texture2D g_Metalness : register(t1);
Texture2D g_BaseColor : register(t2);
Texture2D g_ToonParams : register(t3);
Texture2D<float> g_SceneDepth : register(t4);
TextureCube g_IBL_Diffuse : register(t5);
TextureCube g_IBL_Specular : register(t6);
Texture2D   g_IBL_BRDF_LUT : register(t7);
Texture2D<float> g_ShadowMap : register(t8);
Texture2D g_DecalAlbedo : register(t9);

SamplerState g_Sam : register(s0);
SamplerComparisonState g_ShadowSampler : register(s1);
SamplerState g_SamplerLinear : register(s2);

// 상수 버퍼
cbuffer ConstantBuffer : register(b0)
{
    float4x4 g_World;
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_InvViewProj;
    float4x4 g_WorldInvTranspose;
    float4 g_Material_ambient;
    float4 g_Material_diffuse;
    float4 g_Material_specular;
    float4 g_Material_reflect;
    float4 g_DirLight_ambient;
    float4 g_DirLight_diffuse;
    float4 g_DirLight_specular;
    float3 g_DirLight_direction;
    float  g_DirLight_intensity;
    float3 g_EyePosW;
    int    g_ShadingMode;
    int    g_EnableNormalMap;
    int    g_UseSpecularMap;
    int    g_UseDiffuseMap;
    float  g_Pad;
    int    g_UseTextureColor;
    float3 g_PBRPad;
    float4 g_PBRBaseColor;
    float  g_PBRMetalness;
    float  g_PBRRoughness;
    float  g_PBRAmbientOcclusion;
    float  g_PBRPad2;
    float  g_OutlineWidth;
    float  g_OutlinePow;
    float  g_OutlineThickness;
    float  g_OutlineStrength;
    float4 g_OutlineColor;
    float4x4 g_LightViewProj;
    float  g_ShadowBias;
    float  g_ShadowMapSize;
    float  g_ShadowPCFRadius;
    int    g_ShadowEnabled;
    int    g_BoundsBoneIndex;
    float3 g_BoundsPad;
};

cbuffer DirectionalLightBuffer : register(b3)
{
    float4 g_LightDirection;
    float4 g_LightColor;
    float g_intensity;
    float g_pad[3];
};

#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 16
#define MAX_RECT_LIGHTS 16

struct PointLight
{
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

struct SpotLight
{
    float3 position;
    float  range;
    float3 direction;
    float  innerCos;
    float3 color;
    float  outerCos;
    float  intensity;
    float  pad0;
};

struct RectLight
{
    float3 position;
    float  range;
    float3 direction;
    float  width;
    float3 color;
    float  height;
    float  intensity;
    float  pad0;
};

cbuffer ExtraLightsBuffer : register(b5)
{
    int g_PointLightCount;
    int g_SpotLightCount;
    int g_RectLightCount;
    int g_ExtraPad0;
    PointLight g_PointLights[MAX_POINT_LIGHTS];
    SpotLight  g_SpotLights[MAX_SPOT_LIGHTS];
    RectLight  g_RectLights[MAX_RECT_LIGHTS];
};

float ComputeAttenuation(float dist, float range)
{
    float r = max(range, 0.001f);
    float att = saturate(1.0f - dist / r);
    return att * att;
}

float ComputeSpotFactor(float3 L, float3 lightDir, float innerCos, float outerCos)
{
    float cosTheta = dot(-L, normalize(lightDir));
    float denom = max(innerCos - outerCos, 1e-4f);
    return saturate((cosTheta - outerCos) / denom);
}

float ComputeRectFactor(float3 L, float3 lightDir)
{
    return saturate(dot(-L, normalize(lightDir)));
}

float3 EvaluatePBRLight(float3 N, float3 V, float3 L, float3 albedoPBR, float metalness, float roughness, float3 lightColor)
{
    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denomSpec;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoPBR * INV_PI;

    return (diffuse + specular) * lightColor * NdotL;
}

void AccumulateLegacy(float3 N, float3 V, float3 L, float3 lightColor, float atten, int mode, float shininess,
                      inout float3 outDiffuse, inout float3 outSpecular)
{
    float NdotL = max(dot(N, L), 0.0f);
    if (mode == 3)
    {
        float level = ToonLevel(NdotL);
        outDiffuse += level * lightColor * atten;
        return;
    }

    outDiffuse += NdotL * lightColor * atten;

    if (mode == 0 || NdotL <= 0.0f)
        return;

    float specTerm = 0.0f;
    if (mode == 2) // Blinn-Phong
    {
        float3 H = normalize(L + V);
        specTerm = pow(max(dot(N, H), 0.0f), shininess);
    }
    else // Phong
    {
        float3 R = reflect(-L, N);
        specTerm = pow(max(dot(R, V), 0.0f), shininess);
    }
    outSpecular += specTerm * lightColor * atten;
}
)";

        inline static const char* LightPS2 = R"(
float4 main(PS_INPUT_QUAD pIn) : SV_Target
{
    // G-Buffer 가져오기
    float4 normalRoughness = g_NormalRoughness.Sample(g_Sam, pIn.uv);
    float4 metalness_packed = g_Metalness.Sample(g_Sam, pIn.uv);
    float4 baseColor = g_BaseColor.Sample(g_Sam, pIn.uv);
    float4 toonParams = g_ToonParams.Sample(g_Sam, pIn.uv);
    float4 decalAlbedo = g_DecalAlbedo.Sample(g_Sam, pIn.uv);
    float depth = g_SceneDepth.Sample(g_Sam, pIn.uv);
    
    // 배경 체크 (Depth가 1.0이면 배경)
    if (depth >= 0.9999f) discard;

    // 데이터 복원
    // Normal을 [0,1]에서 [-1,1]로 디코딩
    float3 N = normalize(normalRoughness.xyz * 2.0f - 1.0f);
    float metalness = metalness_packed.r;
    float3 toonCuts = float3(metalness_packed.g, metalness_packed.b, metalness_packed.a);
    float toonStrengthPacked = toonParams.r;
    float toonBlur = (toonStrengthPacked >= 0.5f) ? 1.0f : 0.0f;
    float toonStrength = saturate((toonStrengthPacked - toonBlur * 0.5f) * 2.0f);
    float3 toonLevels = toonParams.gba;
    float roughness = max(normalRoughness.w, 0.04f);
    
    // Depth에서 월드 포지션 복원
    float2 ndc;
    ndc.x = pIn.uv.x * 2.0f - 1.0f;
    ndc.y = (1.0f - pIn.uv.y) * 2.0f - 1.0f;
    float4 clip = float4(ndc, depth, 1.0f);
    float4 posW4 = mul(clip, g_InvViewProj);
    float3 posW = posW4.xyz / max(posW4.w, 1e-6f);
    // Decal (premultiplied)
    baseColor.rgb = baseColor.rgb * (1.0f - decalAlbedo.a) + decalAlbedo.rgb;
    float3 albedo = baseColor.rgb;
    float3 albedoLinear = pow(max(albedo, 0.0f), 2.2f);
    
    // shadingMode + AO 디코딩
    float modeAo = saturate(baseColor.a) * 8.0f;
    int shadingMode = (int)floor(modeAo + 1e-4f);
    shadingMode = clamp(shadingMode, 0, 7);
    float ao = saturate(modeAo - shadingMode);
    
    // shadingMode == 6: TextureOnly (빛의 영향을 받지 않는 텍스처만 반환)
    if (shadingMode == 6)
    {
        return float4(albedoLinear, 1.0f);
    }

    // 라이팅 벡터 계산
    float3 L = normalize(-g_LightDirection.xyz);
    float3 V = normalize(g_EyePosW - posW);
    
    float NdotV = saturate(dot(N, V));

    const bool usePbr = (shadingMode == 4 || shadingMode == 5 || shadingMode == 7);
    const bool toonPbr = (shadingMode == 5 || shadingMode == 7);
    const bool toonEditable = (shadingMode == 7);
    float envDiffuseStrength = 1.0f;
    float envSpecularStrength = 1.0f;
    if (toonPbr)
    {
        float2 levels12 = Unpack2x8(toonParams.g);
        toonLevels = float3(levels12.x, levels12.y, toonParams.b);
        float2 env = Unpack2x8(toonParams.a);
        envDiffuseStrength = env.x;
        envSpecularStrength = env.y;
    }
    else
    {
        envDiffuseStrength = toonParams.r;
        envSpecularStrength = toonParams.g;
    }

    float shadowVis = CalcShadowFactorDeferred(posW, g_ShadowMap, g_ShadowSampler);

    if (!usePbr)
    {
        float3 totalDiffuse = float3(0.0f, 0.0f, 0.0f);
        float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);
        float shininess = max(g_Material_specular.a, 1.0f);
        float3 lightColorDir = g_LightColor.rgb * g_intensity;

        AccumulateLegacy(N, V, L, lightColorDir, shadowVis, shadingMode, shininess, totalDiffuse, totalSpecular);

        [loop] for (int i = 0; i < g_PointLightCount; ++i)
        {
            PointLight pl = g_PointLights[i];
            float3 toLight = pl.position - posW;
            float dist = length(toLight);
            float3 Lp = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, pl.range);
            float3 lc = pl.color * pl.intensity * atten;
            AccumulateLegacy(N, V, Lp, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        [loop] for (int i = 0; i < g_SpotLightCount; ++i)
        {
            SpotLight sl = g_SpotLights[i];
            float3 toLight = sl.position - posW;
            float dist = length(toLight);
            float3 Ls = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, sl.range);
            float spot = ComputeSpotFactor(Ls, sl.direction, sl.innerCos, sl.outerCos);
            float3 lc = sl.color * sl.intensity * atten * spot;
            AccumulateLegacy(N, V, Ls, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        [loop] for (int i = 0; i < g_RectLightCount; ++i)
        {
            RectLight rl = g_RectLights[i];
            float3 toLight = rl.position - posW;
            float dist = length(toLight);
            float3 Lr = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, rl.range);
            float facing = ComputeRectFactor(Lr, rl.direction);
            float areaScale = max(rl.width * rl.height, 0.01f);
            float3 lc = rl.color * rl.intensity * atten * facing * areaScale;
            AccumulateLegacy(N, V, Lr, lc, 1.0f, shadingMode, shininess, totalDiffuse, totalSpecular);
        }

        float3 ambient = g_DirLight_ambient.rgb * albedoLinear;
        float3 color = ambient + totalDiffuse * albedoLinear + totalSpecular * g_Material_specular.rgb;
        return float4(color, 1.0f);
    }

    // PBR 연산
    float3 albedoPBR = albedoLinear;
    roughness = max(roughness, 0.04f);

    // IBL 계산을 위해 필요한 F0와 kD를 여기서 미리 계산해야 합니다.
    // --------------------------------------------------------------------------
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoPBR, metalness);
    float3 kS_IBL = FresnelSchlick(F0, NdotV);
    float3 kD = (1.0f - kS_IBL) * (1.0f - metalness);
    
    // Direct Light (Directional + Extra Lights)
    float3 lightColorDir = g_LightColor.rgb * g_intensity * PI;
    float3 directLighting = 0.0f;
    {
        float3 lit = EvaluatePBRLight(N, V, L, albedoPBR, metalness, roughness, lightColorDir);
        float ndotl = max(dot(N, L), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float toonNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonStrength, toonBlur) : ToonLevel(ndotl);
            lit *= toonNdotL / max(ndotl, 1e-4f);
        }
        directLighting += lit * shadowVis * ao;
    }

    float3 extraLighting = float3(0.0f, 0.0f, 0.0f);

    [loop] for (int i = 0; i < g_PointLightCount; ++i)
    {
        PointLight pl = g_PointLights[i];
        float3 toLight = pl.position - posW;
        float dist = length(toLight);
        float3 Lp = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, pl.range);
        float3 lc = pl.color * pl.intensity * atten * PI;
        float3 lit = EvaluatePBRLight(N, V, Lp, albedoPBR, metalness, roughness, lc);
        float ndotl = max(dot(N, Lp), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float toonNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonStrength, toonBlur) : ToonLevel(ndotl);
            lit *= toonNdotL / max(ndotl, 1e-4f);
        }
        extraLighting += lit * ao;
    }

    [loop] for (int i = 0; i < g_SpotLightCount; ++i)
    {
        SpotLight sl = g_SpotLights[i];
        float3 toLight = sl.position - posW;
        float dist = length(toLight);
        float3 Ls = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, sl.range);
        float spot = ComputeSpotFactor(Ls, sl.direction, sl.innerCos, sl.outerCos);
        float3 lc = sl.color * sl.intensity * atten * spot * PI;
        float3 lit = EvaluatePBRLight(N, V, Ls, albedoPBR, metalness, roughness, lc);
        float ndotl = max(dot(N, Ls), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float toonNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonStrength, toonBlur) : ToonLevel(ndotl);
            lit *= toonNdotL / max(ndotl, 1e-4f);
        }
        extraLighting += lit * ao;
    }

    [loop] for (int i = 0; i < g_RectLightCount; ++i)
    {
        RectLight rl = g_RectLights[i];
        float3 toLight = rl.position - posW;
        float dist = length(toLight);
        float3 Lr = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, rl.range);
        float facing = ComputeRectFactor(Lr, rl.direction);
        float areaScale = max(rl.width * rl.height, 0.01f);
        float3 lc = rl.color * rl.intensity * atten * facing * areaScale * PI;
        float3 lit = EvaluatePBRLight(N, V, Lr, albedoPBR, metalness, roughness, lc);
        float ndotl = max(dot(N, Lr), 0.0f);
        if (toonPbr && ndotl > 0.0f)
        {
            float toonNdotL = toonEditable ? ToonStepEditable(ndotl, toonCuts, toonLevels, toonStrength, toonBlur) : ToonLevel(ndotl);
            lit *= toonNdotL / max(ndotl, 1e-4f);
        }
        extraLighting += lit * ao;
    }
    
    // Indirect Light (IBL)
    float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoPBR;
    
    float3 Renv = reflect(-V, N);
    const float kMaxSpecularMip = 8.0f;
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_SamplerLinear, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);

    diffuseIBL *= envDiffuseStrength;
    specularIBL *= envSpecularStrength;
    
    float3 iblColor = (diffuseIBL + specularIBL) * ao;

    // 최종 색상 계산
    float3 color = directLighting + extraLighting + iblColor;
    
    return float4(color, 1.0f);
}
)";

        // Transparent Forward-Style Skinned VS
        inline static const char* TransparentSkinnedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    float2 TexCoord     : TEXCOORD0;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float3 SmoothNormal : SMOOTHNORMAL; // 아웃라인용 스무스 노멀
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];

    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(input.Normal, M3));
    float3 skinnedT = normalize(mul(input.Tangent, M3));
    float3 skinnedB = normalize(mul(input.Binormal, M3));

    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    // 아웃라인: 스무스 노멀 방향으로 확장 (하드 엣지 모델의 아웃라인 끊김 방지)
    // 스무스 노멀도 스키닝 변환을 적용해야 함
    float3 skinnedSmoothN = normalize(mul(input.SmoothNormal, M3));
    float3 smoothN = normalize(mul(float4(skinnedSmoothN, 0.0f), gWorld).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    
    float4 posW = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Transparent Forward-Style Skinned Instanced VS (본 없는 FBX 인스턴싱용)
        inline static const char* TransparentSkinnedInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL;

    // 인스턴스 월드 행렬 (행 3개)
    float4 iWorld0      : INSTANCE_WORLD0;
    float4 iWorld1      : INSTANCE_WORLD1;
    float4 iWorld2      : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    // 인스턴스 월드 행렬 복원
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);

    //float3 N = normalize(mul(float4(input.Normal, 0.0f), world).xyz);

    // 아웃라인: 스무스 노멀 방향으로 확장
    //float3 smoothN = normalize(mul(float4(input.SmoothNormal, 0.0f), world).xyz);
    //float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    //
    //float4 posW = mul(float4(input.Position + posOffset, 1.0f), world);
    //output.Position = mul(mul(posW, gView), gProj);
    //output.WorldPos = posW.xyz;
    
    //output.Normal   = N;
    //output.TangentW = normalize(mul(float4(input.Tangent, 0.0f), world).xyz);
    //output.BitanW   = normalize(mul(float4(input.Binormal, 0.0f), world).xyz);
    //output.TexCoord = input.TexCoord;

    float3 N = normalize(mul(world, float4(input.Normal, 0.0f)).xyz);
    float3 smoothN = normalize(mul(world, float4(input.SmoothNormal, 0.0f)).xyz);
    
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);

    float4 posW = mul(world, float4(input.Position + posOffset, 1.0f));
    
    output.Position = mul(mul(posW, gView), gProj);
    output.WorldPos = posW.xyz;

    output.Normal   = N;
    output.TangentW = normalize(mul(world, float4(input.Tangent, 0.0f)).xyz);
    output.BitanW   = normalize(mul(world, float4(input.Binormal, 0.0f)).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Transparent Forward-Style PS
        inline static const char* TransparentPS = R"(
static const float PI = 3.14159265f;
static const float INV_PI = 0.31830988618f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = max(NdotH * NdotH * (a2 - 1.0f) + 1.0f, 1e-4f);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float ToonLevel(float n)
{
    if (n > 0.95f) return 1.0f;
    if (n > 0.5f)  return 0.7f;
    if (n > 0.2f)  return 0.4f;
    return 0.1f;
}

float ToonStepEditable(float n, float3 cuts, float3 levels, float strength, float blur)
{
    float c1 = saturate(cuts.x);
    float c2 = saturate(cuts.y);
    float c3 = saturate(cuts.z);
    c2 = max(c2, c1 + 1e-4f);
    c3 = max(c3, c2 + 1e-4f);

    float l0 = saturate(levels.x);
    float l1 = saturate(levels.y);
    float l2 = saturate(levels.z);
    float l3 = 1.0f;

    float t = saturate(strength);
    if (blur > 0.5f)
    {
        float w = max(fwidth(n) * 2.0f, 0.02f);
        float s1 = smoothstep(c1 - w, c1 + w, n);
        float s2 = smoothstep(c2 - w, c2 + w, n);
        float s3 = smoothstep(c3 - w, c3 + w, n);

        float level = lerp(l0, l1, s1);
        level = lerp(level, l2, s2);
        level = lerp(level, l3, s3);
        return lerp(n, level, t);
    }

    float level = (n > c3) ? l3 :
                  (n > c2) ? l2 :
                  (n > c1) ? l1 :
                             l0;
    return lerp(n, level, t);
}

// 텍스처
Texture2D  g_DiffuseMap : register(t0);
Texture2D  g_NormalMap  : register(t1);

// IBL
TextureCube g_IBL_Diffuse : register(t5);
TextureCube g_IBL_Specular : register(t6);
Texture2D   g_IBL_BRDF_LUT : register(t7);

SamplerState g_Sam : register(s0);

cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBTransparentLight : register(b1)
{
    float3 g_LightDir;
    float  g_LightIntensity;
    float3 g_LightColor;
    float  _pad0;
    float3 g_CameraPosW;
    float  _pad1;
};

struct PSIn
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};

float DitherThreshold(float2 pos)
{
    // Interleaved gradient noise (per-pixel hash, less visible grid)
    float n = 0.06711056f * pos.x + 0.00583715f * pos.y;
    return frac(52.9829189f * frac(n));
}

float3 LinearToSRGB(float3 linearColor)
{
    return pow(max(linearColor, 0.0f), 1.0f / 2.2f);
}

float4 main(PSIn pIn) : SV_Target
{
    float4 tex = float4(1,1,1,1);
    if (gUseTexture != 0)
        tex = g_DiffuseMap.Sample(g_Sam, pIn.TexCoord);

    float alphaTex = tex.a * gMaterialColor.a;

    // 컷아웃은 텍스처 알파로만 처리 (머티리얼 알파는 블렌딩)
    if (gUseTexture != 0)
    {
        clip(tex.a - 0.1f);
    }
    // NDC 기반 디더링으로 투명도 처리 (알파 블렌딩 대신 화면 도트 컷아웃)
    float alpha = saturate(alphaTex);
    if (alpha < 1.0f)
    {
        float threshold = DitherThreshold(pIn.Position.xy);
        clip(alpha - threshold);
    }
    // 거의 불투명은 디퍼드에서 처리하므로 여기서는 제외
    if (alphaTex >= 0.99f) discard;
    float alphaOut = 1.0f;

    float3 baseColor = gMaterialColor.rgb;
    if (gUseTexture != 0)
        baseColor *= tex.rgb;

    // shadingMode == 6: TextureOnly (빛의 영향을 받지 않는 텍스처만 반환)
    if (gShadingMode == 6)
    {
        return float4(baseColor, alphaOut);
    }

    float3 albedoLinear = pow(max(baseColor, 0.0f), 2.2f);

    float3 N = normalize(pIn.Normal);
    if (gEnableNormalMap != 0)
    {
        float3 T = normalize(pIn.TangentW);
        float3 B = normalize(pIn.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = g_NormalMap.Sample(g_Sam, pIn.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y;
        // 노말맵 강도 조절: X, Y 성분에만 Strength를 곱하고 정규화
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }

    float metalness = saturate(gMetalness);
    float roughness = max(saturate(gRoughness), 0.04f);
    float ao = saturate(gAmbientOcclusion);

    float3 L = normalize(-g_LightDir);
    float3 V = normalize(g_CameraPosW - pIn.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoLinear, metalness);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(F0, VdotH);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denomSpec;

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    float3 diffuse = kD * albedoLinear * INV_PI;

    float3 radiance = g_LightColor.rgb * PI * g_LightIntensity;
    float3 direct = (diffuse + specular) * radiance * NdotL * ao;

    const bool toonPbr = (gShadingMode == 5 || gShadingMode == 7);
    const bool toonEditable = (gShadingMode == 7);
    if (toonPbr && NdotL > 0.0f)
    {
        float toonNdotL = toonEditable
            ? ToonStepEditable(NdotL, gToonPbrCuts.xyz, gToonPbrLevels.xyz, gToonPbrCuts.w, gToonPbrLevels.w)
            : ToonLevel(NdotL);
        direct *= toonNdotL / max(NdotL, 1e-4f);
    }

    // IBL
    float3 diffuseIBL = kD * g_IBL_Diffuse.Sample(g_Sam, N).rgb * albedoLinear;
    float3 Renv = reflect(-V, N);
    const float kMaxSpecularMip = 8.0f;
    float3 prefilteredColor = g_IBL_Specular.SampleLevel(g_Sam, Renv, roughness * kMaxSpecularMip).rgb;
    float2 specBRDF = g_IBL_BRDF_LUT.Sample(g_Sam, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);

    diffuseIBL *= gEnvDiffuseStrength;
    specularIBL *= gEnvSpecularStrength;
    float3 ibl = (diffuseIBL + specularIBL) * ao;

    float3 outLinear = direct + ibl;
    return float4(outLinear, alphaOut);
}
)";

        // Shadow VS (Static)
        inline static const char* ShadowVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    float4 posW = mul(float4(input.Position, 1.0f), gWorld);
    o.Position = mul(mul(posW, gView), gProj);
    return o;
}
)";

        // Shadow Instanced VS (Static)
        inline static const char* ShadowInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 iWorld0  : INSTANCE_WORLD0;
    float4 iWorld1  : INSTANCE_WORLD1;
    float4 iWorld2  : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);
    
    float4 posW = mul(world, float4(input.Position, 1.0f));
    o.Position = mul(mul(posW, gView), gProj);
    return o;
}
)";

        // Shadow Skinned VS
        inline static const char* ShadowSkinnedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBBones : register(b2)
{
    float4x4 gBones[1023];
    uint     gBoneCount;
    float3   _padBones;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0; 
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    
    // 본 인덱스와 가중치를 가져옴
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;
    
    // 스키닝 행렬 계산
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];
    
    // 위치 변환 (Local -> Skinned -> World -> View -> Proj)
    float4 posL = float4(input.Position, 1.0f);
    float4 skinnedPos = mul(posL, M);
    float4 posW = mul(skinnedPos, gWorld);
    
    o.Position = mul(mul(posW, gView), gProj);
    
    return o;
}
)";

        // Shadow Skinned Instanced VS (본 없는 FBX 인스턴싱용)
        inline static const char* ShadowSkinnedInstancedVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor;
    float    gRoughness;
    float    gMetalness;
    int      gUseTexture;
    int      gEnableNormalMap;
    int      gShadingMode;
    int      gPad0;
    
    // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
    float2   gPad1;
    
    // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
    float    gNormalStrength;
    float    gAmbientOcclusion; // 0~1 AO
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    float4   gToonPbrCuts;
    float4   gToonPbrLevels;
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float3 Tangent      : TANGENT;
    float3 Binormal     : BINORMAL;
    float4 Color        : COLOR;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
    float2 TexCoord     : TEXCOORD0;
    float3 SmoothNormal : SMOOTHNORMAL;

    // 인스턴스 월드 행렬 (행 3개)
    float4 iWorld0      : INSTANCE_WORLD0;
    float4 iWorld1      : INSTANCE_WORLD1;
    float4 iWorld2      : INSTANCE_WORLD2;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    // 인스턴스 월드 행렬 복원
    float4x4 world;
    world[0] = input.iWorld0;
    world[1] = input.iWorld1;
    world[2] = input.iWorld2;
    world[3] = float4(0, 0, 0, 1);

    //float4 posW = mul(float4(input.Position, 1.0f), world);
    float4 posW = mul(world, float4(input.Position, 1.0f));
    o.Position = mul(mul(posW, gView), gProj);

    return o;
}
)";

        // UI Render Vertex Shader
        inline static const char* UIRenderVS = R"(
cbuffer UICompositeCB : register(b0)
{
    uint isUseMetalness;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

PS_INPUT main(uint vid : SV_VertexID) 
{
    PS_INPUT o;
    uint tmp = isUseMetalness;
    
    //사각형 4점
    float2 p0 = float2(-1.0, -1.0); 
    float2 p1 = float2(-1.0, 1.0); 
    float2 p2 = float2(1.0, 1.0); 
    float2 p3 = float2(1.0, -1.0); 

    // UV좌표 4점
    float2 t0 = float2(0.0, 1.0);
    float2 t1 = float2(0.0, 0.0);
    float2 t2 = float2(1.0, 0.0);
    float2 t3 = float2(1.0, 1.0);

    // 삼각형 2개
    float2 pos[6] = { p0, p1, p2, p0, p2, p3 };
    float2 uv[6] = { t0, t1, t2, t0, t2, t3 };

    o.Pos = float4(pos[vid][0], pos[vid][1], 0.0, 1.0);
    o.Tex = uv[vid];
    return o;
}
)";

        // UI Render Pixel Shader
        inline static const char* UIRenderPS = R"(
    struct PS_INPUT
    {
        float4 Pos : SV_POSITION;
        float2 Tex : TEXCOORD0;
    };

    Texture2D UITexture : register(t101);
    SamplerState SamLinear : register(s0);

    float4 main(PS_INPUT input) : SV_Target
    {
        float4 UITex = UITexture.Sample(SamLinear, input.Tex);
    
        // UI 텍스처 알파값으로 UI, 화면 구분
        if (UITex.a <= 0.0f)
        {
            discard;
        }
        
        return UITex;
    }
)";
    };
}
