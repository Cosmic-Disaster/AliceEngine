#pragma once

namespace Alice
{
    /// 포워드 렌더링 전용 셰이더 코드
    class ForwardShader
    {
    public:
        // Basic Phong Vertex Shader
        inline static const char* PhongVS = R"(
cbuffer CBPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
    float4   gMaterialColor; // per-object 머티리얼 색상

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
    float    gAmbientOcclusion;
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    // ToonPBREditable 파라미터
    float4   gToonPbrCuts;   // (cut1, cut2, cut3, strength)
    float4   gToonPbrLevels; // (level1, level2, level3, unused)
    
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
    
    float4 worldPos = mul(float4(input.Position + posOffset, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal = N;
    // 정적 지오메트리(큐브 등)는 탄젠트/바이탄젠트가 없으므로
    // 노말에서 임의의 직교 기저를 만들어 노말맵(TBN) 계산이 가능하게 합니다.
    float3 up = (abs(N.y) > 0.999f) ? float3(1,0,0) : float3(0,1,0);
    float3 T = normalize(cross(up, N));
    float3 B = normalize(cross(N, T));
    output.TangentW = T;
    output.BitanW = B;
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Skinned Vertex Shader
        inline static const char* SkinnedVS = R"(
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
    float    gAmbientOcclusion;
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    // ToonPBREditable 파라미터
    float4   gToonPbrCuts;   // (cut1, cut2, cut3, strength)
    float4   gToonPbrLevels; // (level1, level2, level3, unused)
    
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

    // D3D11-AliceTutorial/31_IBL 방식으로 스키닝
    // - CPU에서 전치 업로드된 본 팔레트에 대해 row-vector 곱(mul(v, M))을 사용합니다.
    uint4 bi = input.BoneIndices;
    float4 bw = input.BoneWeights;

    // DirectX11(행벡터) 기준: v' = v * (Σ w_i * M_i)
    matrix M = bw.x * gBones[bi.x]
             + bw.y * gBones[bi.y]
             + bw.z * gBones[bi.z]
             + bw.w * gBones[bi.w];

    float4 posL = float4(input.Position, 1.0f);
    float3 nL = input.Normal;
    float3 tL = input.Tangent;
    float3 bL = input.Binormal;

    float4 skinnedPos = mul(posL, M);
    float3x3 M3 = (float3x3)M;
    float3 skinnedN = normalize(mul(nL, M3));
    float3 skinnedT = normalize(mul(tL, M3));
    float3 skinnedB = normalize(mul(bL, M3));

    float3 N = normalize(mul(float4(skinnedN, 0.0f), gWorld).xyz);
    
    // 아웃라인: 스무스 노멀 방향으로 확장 (하드 엣지 모델의 아웃라인 끊김 방지)
    // 스무스 노멀도 스키닝 변환을 적용해야 함
    float3 skinnedSmoothN = normalize(mul(input.SmoothNormal, M3));
    float3 smoothN = normalize(mul(float4(skinnedSmoothN, 0.0f), gWorld).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);
    
    float4 worldPos = mul(float4(skinnedPos.xyz + posOffset, 1.0f), gWorld);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal   = N;
    output.TangentW = normalize(mul(float4(skinnedT, 0.0f), gWorld).xyz);
    output.BitanW   = normalize(mul(float4(skinnedB, 0.0f), gWorld).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        // Skinned Instanced Vertex Shader (본 없는 FBX 인스턴싱용)
        inline static const char* SkinnedInstancedVS = R"(
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
    float    gAmbientOcclusion;
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    // ToonPBREditable 파라미터
    float4   gToonPbrCuts;   // (cut1, cut2, cut3, strength)
    float4   gToonPbrLevels; // (level1, level2, level3, unused)
    
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

    float3 N = normalize(mul(float4(input.Normal, 0.0f), world).xyz);

    // 아웃라인: 스무스 노멀 방향으로 확장
    float3 smoothN = normalize(mul(float4(input.SmoothNormal, 0.0f), world).xyz);
    float3 posOffset = (gOutlineWidth > 0.0f) ? (smoothN * gOutlineWidth) : float3(0, 0, 0);

    float4 worldPos = mul(float4(input.Position + posOffset, 1.0f), world);
    float4 viewPos  = mul(worldPos, gView);
    output.Position = mul(viewPos, gProj);

    output.WorldPos = worldPos.xyz;
    output.Normal   = N;
    output.TangentW = normalize(mul(float4(input.Tangent, 0.0f), world).xyz);
    output.BitanW   = normalize(mul(float4(input.Binormal, 0.0f), world).xyz);
    output.TexCoord = input.TexCoord;

    return output;
}
)";

        inline static const char* PBRPS_Part1 = R"(
Texture2D gDiffuseMap  : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gSpecularMap : register(t2);
TextureCube gSkybox    : register(t3);
SamplerState gSampler  : register(s0);

// 섀도우 맵 (Depth 텍스처)
Texture2D<float>        gShadowMap     : register(t4);
SamplerComparisonState  gShadowSampler : register(s1);

// IBL (Image-Based Lighting) 텍스처들
TextureCube gIBL_Diffuse  : register(t5);
TextureCube gIBL_Specular : register(t6);
Texture2D   gIBL_BRDF_LUT : register(t7);

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
    float    gAmbientOcclusion;
    float    gEnvDiffuseStrength;
    float    gEnvSpecularStrength;

    // ToonPBREditable 파라미터
    float4   gToonPbrCuts;   // (cut1, cut2, cut3, strength)
    float4   gToonPbrLevels; // (level1, level2, level3, unused)
    
    // 아웃라인 파라미터 (모든 쉐이딩 모드에서 사용 가능, 16바이트 경계에서 시작)
    float3   gOutlineColor;
    float    gOutlineWidth;
};

cbuffer CBLighting : register(b1)
{
    // Key Light
    float3 gKeyLightDir;
    float  gKeyLightPad0;

    float3 gKeyLightColor;
    float  gKeyLightIntensity;

    // Fill Light
    float3 gFillLightDir;
    float  gFillLightPad0;

    float3 gFillLightColor;
    float  gFillLightIntensity;

    float3 gCameraPos;
    float  gLightingPad0;

    float4 gMaterialDiffuse;   // rgb: diffuse color
    float4 gMaterialSpecular;  // rgb: specular color, a: shininess

    int    gShadingMode2;       // 0: Lambert, 1: Phong, 2: Blinn-Phong, 3: Toon, 4: PBR, 5: ToonPBR, 6: OnlyTextureWithOutline, 7: ToonPBREditable
    int3   gPad3;

    float4x4 gLightViewProj;   // 섀도우 맵 계산용 라이트 뷰-프로젝션

    // Shadow params (34_ToneMapping 방식)
    float  gShadowBias;
    float  gShadowMapSize;
    float  gShadowPCFRadius;
    int    gShadowEnabled;
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

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal   : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float3 BitanW   : TEXCOORD4;
};
)";

	// Part2: 유틸 함수들까지만 (main 시작 전까지)
	inline static const char* PBRPS_Part2 = R"(
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

void AccumulateBlinnPhong(float3 N, float3 V, float3 L, float3 lightColor, float atten,
                          inout float3 outDiffuse, inout float3 outSpecular)
{
    float NdotL = max(dot(N, L), 0.0f);
    outDiffuse += NdotL * lightColor * atten;

    if (gShadingMode != 0 && NdotL > 0.0f)
    {
        float specularTerm = 0.0f;
        if (gShadingMode == 2) // Blinn-Phong
        {
            float3 H = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0f);
            specularTerm = pow(NdotH, gMaterialSpecular.a);
        }
        else // Phong
        {
            float3 R = reflect(-L, N);
            float RdotV = max(dot(R, V), 0.0f);
            specularTerm = pow(RdotV, gMaterialSpecular.a);
        }

        outSpecular += specularTerm * lightColor * atten;
    }
}

float3 EvaluatePBRLight(float3 N, float3 V, float3 L, float3 albedo, float metalness, float roughness, float3 lightColor)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
    float a = roughness * roughness;
    float a2 = a * a;
    float denomD = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    float D = a2 / max(3.14159f * denomD * denomD, 1e-4f);

    float k = (roughness + 1.0f);
    k = (k * k) / 8.0f;
    float Gv = NdotV / (NdotV * (1.0f - k) + k);
    float Gl = NdotL / (NdotL * (1.0f - k) + k);
    float G = Gv * Gl;

    float3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

    float3 numerator = D * G * F;
    float denomSpec = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specularTerm = numerator / denomSpec;

    float3 kd = (1.0f - F) * (1.0f - metalness);
    float3 diffuseTerm = kd * albedo / 3.14159f;

    return (diffuseTerm + specularTerm) * lightColor * NdotL;
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

float ToonPbrNdotL(float n)
{
    if (gShadingMode == 7)
    {
         return ToonStepEditable(n, gToonPbrCuts.xyz, gToonPbrLevels.xyz, gToonPbrCuts.w, gToonPbrLevels.w);
    }
    return ToonLevel(n);
}

float DitherThreshold(float2 pos)
{
    // Interleaved gradient noise (per-pixel hash, less visible grid)
    float n = 0.06711056f * pos.x + 0.00583715f * pos.y;
    return frac(52.9829189f * frac(n));
}
)";

	// Part3: main부터 끝까지
	inline static const char* PBRPS_Part3 = R"(
float4 main(PSInput input) : SV_TARGET
{
    // 아웃라인 패스 감지: Width가 0보다 크면 아웃라인용 드로우콜임
    if (gOutlineWidth > 0.0f)
    {
        // 아웃라인 색상 반환 (Unlit)
        return float4(gOutlineColor, 1.0f);
    }
    
	float4 textureColor = gDiffuseMap.Sample(gSampler, input.TexCoord);
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
        float threshold = DitherThreshold(input.Position.xy);
        clip(alpha - threshold);
    }
    float alphaOut = 1.0f;

    // shadingMode == 6: TextureOnly (빛의 영향을 받지 않는 텍스처만 반환)
    if (gShadingMode == 6)
    {
        float3 albedo = gMaterialColor.rgb;
        if (gUseTexture != 0)
        {
            float3 texSample = textureColor.rgb;
            albedo *= texSample;
        }
        return float4(albedo, alphaOut);
    }

    float3 N = normalize(input.Normal);
    if (gEnableNormalMap != 0)
    {
        // D3D11-AliceTutorial/31_IBL/31_BasicPS.hlsl 의 방식으로 TBN 기반 노말맵 적용
        float3 T = normalize(input.TangentW);
        float3 B = normalize(input.BitanW);
        float handed = dot(cross(T, B), N);
        if (handed < 0.0f) B = -B;
        float3x3 TBN = float3x3(T, B, N);
        float3 N_ts = gNormalMap.Sample(gSampler, input.TexCoord).xyz * 2.0f - 1.0f;
        N_ts.y = -N_ts.y; // 그린 채널 반전 보정
        N_ts.xy *= gNormalStrength;
        N_ts = normalize(N_ts);
        N = normalize(mul(N_ts, TBN));
    }

    float3 V = normalize(gCameraPos - input.WorldPos);

    float3 totalDiffuse  = float3(0.0f, 0.0f, 0.0f);
    float3 totalSpecular = float3(0.0f, 0.0f, 0.0f);
    float3 extraDiffuse  = float3(0.0f, 0.0f, 0.0f);
    float3 extraSpecular = float3(0.0f, 0.0f, 0.0f);

    // Key Light
    {
        float3 L = normalize(-gKeyLightDir);
        float  NdotL = max(dot(N, L), 0.0f);
        float3 lightColor = gKeyLightColor * gKeyLightIntensity;

        totalDiffuse += NdotL * lightColor;

        if (gShadingMode != 0 && NdotL > 0.0f)
        {
            float specularTerm = 0.0f;
            if (gShadingMode == 2) // Blinn-Phong
            {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                specularTerm = pow(NdotH, gMaterialSpecular.a);
            }
            else // Phong
            {
                float3 R = reflect(-L, N);
                float RdotV = max(dot(R, V), 0.0f);
                specularTerm = pow(RdotV, gMaterialSpecular.a);
            }

            totalSpecular += specularTerm * lightColor;
        }
    }

    // Fill Light (옵션)
    {
        float3 L = normalize(-gFillLightDir);
        float  NdotL = max(dot(N, L), 0.0f);
        float3 lightColor = gFillLightColor * gFillLightIntensity;

        totalDiffuse += NdotL * lightColor;

        if (gShadingMode != 0 && NdotL > 0.0f)
        {
            float specularTerm = 0.0f;
            if (gShadingMode == 2) // Blinn-Phong
            {
                float3 H = normalize(L + V);
                float NdotH = max(dot(N, H), 0.0f);
                specularTerm = pow(NdotH, gMaterialSpecular.a);
            }
            else // Phong
            {
                float3 R = reflect(-L, N);
                float RdotV = max(dot(R, V), 0.0f);
                specularTerm = pow(RdotV, gMaterialSpecular.a);
            }

            totalSpecular += specularTerm * lightColor;
        }
    }

    // Point Lights
    [loop] for (int i = 0; i < g_PointLightCount; ++i)
    {
        PointLight pl = g_PointLights[i];
        float3 toLight = pl.position - input.WorldPos;
        float dist = length(toLight);
        float3 L = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, pl.range);
        float3 lightColor = pl.color * pl.intensity;
        AccumulateBlinnPhong(N, V, L, lightColor, atten, extraDiffuse, extraSpecular);
    }

    // Spot Lights
    [loop] for (int i = 0; i < g_SpotLightCount; ++i)
    {
        SpotLight sl = g_SpotLights[i];
        float3 toLight = sl.position - input.WorldPos;
        float dist = length(toLight);
        float3 L = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, sl.range);
        float spot = ComputeSpotFactor(L, sl.direction, sl.innerCos, sl.outerCos);
        float3 lightColor = sl.color * sl.intensity;
        AccumulateBlinnPhong(N, V, L, lightColor, atten * spot, extraDiffuse, extraSpecular);
    }

    // Rect Lights (simple approximation)
    [loop] for (int i = 0; i < g_RectLightCount; ++i)
    {
        RectLight rl = g_RectLights[i];
        float3 toLight = rl.position - input.WorldPos;
        float dist = length(toLight);
        float3 L = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
        float atten = ComputeAttenuation(dist, rl.range);
        float facing = ComputeRectFactor(L, rl.direction);
        float areaScale = max(rl.width * rl.height, 0.01f);
        float3 lightColor = rl.color * rl.intensity * areaScale;
        AccumulateBlinnPhong(N, V, L, lightColor, atten * facing, extraDiffuse, extraSpecular);
    }

    // 섀도우 팩터 (PCF)
    float shadow = 1.0f;
    {
        if (gShadowEnabled != 0)
        {
        float4 shadowPos = mul(float4(input.WorldPos, 1.0f), gLightViewProj);
        shadowPos.xyz /= shadowPos.w;

        float2 shadowTex;
        shadowTex.x = shadowPos.x * 0.5f + 0.5f;
        shadowTex.y = -shadowPos.y * 0.5f + 0.5f;
        float depth = shadowPos.z;

        // Shadow map texel 크기 및 PCF 반경(텍셀 단위)
        const float2 texelSize = float2(1.0f, 1.0f) / max(gShadowMapSize, 1.0f);
        const float2 pcfStep = max(gShadowPCFRadius, 0.0f) * texelSize;

        if (shadowTex.x >= 0.0f && shadowTex.x <= 1.0f &&
            shadowTex.y >= 0.0f && shadowTex.y <= 1.0f)
        {
            float sum = 0.0f;
            [unroll] for (int y = -1; y <= 1; ++y)
            {
                [unroll] for (int x = -1; x <= 1; ++x)
                {
                    float2 offset = float2(x, y) * pcfStep;
                    sum += gShadowMap.SampleCmpLevelZero(
                        gShadowSampler,
                        shadowTex + offset,
                        depth - gShadowBias);
                }
            }
            shadow = sum / 9.0f;
        }
        }
    }

    totalDiffuse  *= shadow;
    totalSpecular *= shadow;
    totalDiffuse  += extraDiffuse;
    totalSpecular += extraSpecular;

    // 머티리얼 베이스 컬러
    float3 albedo = gMaterialColor.rgb;
    if (gUseTexture != 0)
    {
        float3 texSample = gDiffuseMap.Sample(gSampler, input.TexCoord).rgb;
        albedo *= texSample;
    }
    float3 specColor = float3(1.0f, 1.0f, 1.0f);

    float3 ambient = 0.1f * gKeyLightColor;

    // === Toon Shading (shadingMode == 3) ===
    if (gShadingMode == 3)
    {
        float3 Lmain = normalize(-gKeyLightDir);
        float  NdotL = max(dot(N, Lmain), 0.0f);

        float level = 0.0f;
        if (NdotL > 0.95f)      level = 1.0f;
        else if (NdotL > 0.5f)  level = 0.7f;
        else if (NdotL > 0.2f)  level = 0.4f;
        else                    level = 0.1f;

        // Toon도 PCF shadow를 반영해야 Phong/Blinn과 동일하게 그림자가 보입니다.
        float3 toonColor = albedo * (level * shadow) + 0.1f * albedo;
        return float4(toonColor, alphaOut);
    }

    // === PBR 경로 (shadingMode == 4, 5, 7) ===
    if (gShadingMode == 4 || gShadingMode == 5 || gShadingMode == 7)
    {
        const bool toonPbr = (gShadingMode == 5 || gShadingMode == 7);
        float roughness = saturate(gRoughness);
        float metalness = saturate(gMetalness);
        float ao = saturate(gAmbientOcclusion);

        float3 Np = N;
        float3 Vp = V;
        float3 Lp = normalize(-gKeyLightDir);
        float3 Lo = 0.0f;

        float3 lightColor = gKeyLightColor * gKeyLightIntensity;
        {
            float NdotL = max(dot(Np, Lp), 0.0f);
            float3 lit = EvaluatePBRLight(Np, Vp, Lp, albedo, metalness, roughness, lightColor);
            if (toonPbr && NdotL > 0.0f)
            {
                float toonNdotL = ToonPbrNdotL(NdotL);
                lit *= toonNdotL / max(NdotL, 1e-4f);
            }
            Lo += lit * shadow;
        }

        [loop] for (int i = 0; i < g_PointLightCount; ++i)
        {
            PointLight pl = g_PointLights[i];
            float3 toLight = pl.position - input.WorldPos;
            float dist = length(toLight);
            float3 L = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, pl.range);
            float3 lc = pl.color * pl.intensity * atten;
            float NdotL = max(dot(Np, L), 0.0f);
            float3 lit = EvaluatePBRLight(Np, Vp, L, albedo, metalness, roughness, lc);
            if (toonPbr && NdotL > 0.0f)
            {
                float toonNdotL = ToonPbrNdotL(NdotL);
                lit *= toonNdotL / max(NdotL, 1e-4f);
            }
            Lo += lit;
        }

        [loop] for (int i = 0; i < g_SpotLightCount; ++i)
        {
            SpotLight sl = g_SpotLights[i];
            float3 toLight = sl.position - input.WorldPos;
            float dist = length(toLight);
            float3 L = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, sl.range);
            float spot = ComputeSpotFactor(L, sl.direction, sl.innerCos, sl.outerCos);
            float3 lc = sl.color * sl.intensity * atten * spot;
            float NdotL = max(dot(Np, L), 0.0f);
            float3 lit = EvaluatePBRLight(Np, Vp, L, albedo, metalness, roughness, lc);
            if (toonPbr && NdotL > 0.0f)
            {
                float toonNdotL = ToonPbrNdotL(NdotL);
                lit *= toonNdotL / max(NdotL, 1e-4f);
            }
            Lo += lit;
        }

        [loop] for (int i = 0; i < g_RectLightCount; ++i)
        {
            RectLight rl = g_RectLights[i];
            float3 toLight = rl.position - input.WorldPos;
            float dist = length(toLight);
            float3 L = (dist > 0.0001f) ? (toLight / dist) : float3(0, 0, 1);
            float atten = ComputeAttenuation(dist, rl.range);
            float facing = ComputeRectFactor(L, rl.direction);
            float areaScale = max(rl.width * rl.height, 0.01f);
            float3 lc = rl.color * rl.intensity * atten * facing * areaScale;
            float NdotL = max(dot(Np, L), 0.0f);
            float3 lit = EvaluatePBRLight(Np, Vp, L, albedo, metalness, roughness, lc);
            if (toonPbr && NdotL > 0.0f)
            {
                float toonNdotL = ToonPbrNdotL(NdotL);
                lit *= toonNdotL / max(NdotL, 1e-4f);
            }
            Lo += lit;
        }

        float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
        float NdotV = max(dot(Np, Vp), 0.0f);
        float3 F = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);
        float3 kd = (1.0f - F) * (1.0f - metalness);

        // === IBL (Image-Based Lighting) 계산 ===
        float3 diffuseIBL = kd * gIBL_Diffuse.Sample(gSampler, Np).rgb * albedo;

        float3 Renv = reflect(-Vp, Np);
        const float kMaxSpecularMip = 8.0f;
        float3 prefilteredColor = gIBL_Specular.SampleLevel(gSampler, Renv, roughness * kMaxSpecularMip).rgb;
        float2 specBRDF = gIBL_BRDF_LUT.Sample(gSampler, float2(NdotV, roughness)).rg;
        float3 specularIBL = prefilteredColor * (F0 * specBRDF.x + specBRDF.y);

        diffuseIBL *= gEnvDiffuseStrength;
        specularIBL *= gEnvSpecularStrength;

        // 최종 색상 = 직접광 + 간접광(IBL)
        float shadowIBL = lerp(0.35f, 1.0f, shadow);
        float3 colorPbr = Lo + (diffuseIBL * shadowIBL + specularIBL) * ao;

        return float4(colorPbr, alphaOut);
    }

    // 기본 Phong/Blinn-Phong/Lambert 경로
    float3 baseColor =
        ambient * albedo +
        totalDiffuse * albedo +
        totalSpecular * specColor;

    return float4(baseColor, alphaOut);
}
)";


        // Tone Mapping Pixel Shader - HDR (포워드 전용)
        inline static const char* ToneMappingPS_HDR = R"(
Texture2D g_SceneHDR : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer PostProcessConstantBuffer : register(b2)
{
    float g_Exposure;
    float g_MaxHDRNits;
    float2 g_Padding;
};

struct PS_INPUT_QUAD
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate(x * (a * x + b) / (x * (c * x + d) + e));
}

// Rec709 to Rec2020 색공간 변환
float3 Rec709ToRec2020(float3 color)
{
    static const float3x3 conversion =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(conversion, color);
}

// Linear to ST2084 (PQ 인코딩)
float3 LinearToST2084(float3 color)
{
    // g_MaxHDRNits를 반영하여 HDR 스케일링 (10000 nits 기준으로 정규화)
    const float st2084max = 10000.0;
    float hdrScalar = g_MaxHDRNits / st2084max;
    float3 scaledColor = color * hdrScalar;
    
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 cp = pow(abs(scaledColor), m1);
    return pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
}

float4 main(PS_INPUT_QUAD input) : SV_Target
{
    // 예제 프로젝트 36_ToneMappingPS_HDR.hlsl와 동일한 로직
    float3 C_linear709 = g_SceneHDR.Sample(g_SamplerLinear, input.uv).rgb;
    float3 C_exposure = C_linear709 * pow(2.0f, g_Exposure);
    float3 C_tonemapped = ACESFilm(C_exposure);
    
    // Rec709 → Rec2020 색공간 변환 (LinearToST2084 내부에서 g_MaxHDRNits 처리)
    float3 C_Rec2020 = Rec709ToRec2020(C_tonemapped);
    float3 C_ST2084 = LinearToST2084(C_Rec2020);
    
    // 최종 PQ 인코딩된 값 [0.0, 1.0]을 R10G10B10A2_UNORM 백버퍼에 출력
    return float4(C_ST2084, 1.0);
}
)";

        // ==== UI 합성 셰이더 ====
        // SV_VertexID를 사용하여 풀스크린 쿼드 생성 (VB 불필요)
        inline static const char* UIQuadVS = R"(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;
    
    // 6개의 버텍스로 풀스크린 쿼드 생성 (2개의 삼각형)
    
    float2 positions[6] = {
        float2(-1.0,  1.0),  // 0: 좌상
        float2( 1.0,  1.0),  // 1: 우상
        float2(-1.0, -1.0),  // 2: 좌하
        float2(-1.0, -1.0),  // 3: 좌하
        float2( 1.0,  1.0),  // 4: 우상
        float2( 1.0, -1.0)   // 5: 우하
    };
    
    float2 texcoords[6] = {
        float2(0.0, 0.0),  // 0
        float2(1.0, 0.0),  // 1
        float2(0.0, 1.0),  // 2
        float2(0.0, 1.0),  // 3
        float2(1.0, 0.0),  // 4
        float2(1.0, 1.0)   // 5
    };
    
    output.Position = float4(positions[vertexId], 0.0, 1.0);
    output.TexCoord = texcoords[vertexId];
    
    return output;
}
)";

        // UI 텍스처를 알파 블렌딩으로 합성
        inline static const char* UICompositePS = R"(
Texture2D g_UITexture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    float4 uiColor = g_UITexture.Sample(g_Sampler, input.TexCoord);
    
    // Premultiplied Alpha 처리 (D2D 출력은 premultiplied alpha)
    // 알파가 0이면 완전 투명
    if (uiColor.a < 0.001)
        discard;
    
    return uiColor;
}
)";
    };
}
