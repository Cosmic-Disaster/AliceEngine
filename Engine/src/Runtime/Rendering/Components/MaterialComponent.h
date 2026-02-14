#pragma once

#include <DirectXMath.h>
#include <string>

#ifndef ALICE_GET_SET
#define ALICE_GET_SET(name) \
    const decltype(name)& Get_##name() const { return name; } \
    void Set_##name(const decltype(name)& value) { name = value; }
#endif

#ifndef Alice_Get_Set
#define Alice_Get_Set(name) ALICE_GET_SET(name)
#endif

namespace Alice {
    /// 머티리얼 컴포넌트
    /// - 베이스 컬러 + 알파 + 러프니스/메탈니스 등을 포함합니다.
    /// - 추후 더 많은 파라미터를 확장할 수 있습니다.
    struct MaterialComponent 
    {
        DirectX::XMFLOAT3 color{ 0.7f, 0.7f, 0.7f }; // 베이스 색상 (albedo)
        float alpha{ 1.0f };                         // 0~1 알파 (투명도)
        float roughness{ 0.5f };                     // 0~1 러프니스 (PBR)
        float metalness{ 0.0f };                     // 0~1 메탈니스 (PBR)
        float ambientOcclusion{ 1.0f };              // 0~1 AO (Ambient Occlusion)
        float envDiffuseStrength{ 1.0f };            // IBL Diffuse 기여도 (0 = 없음)
        float envSpecularStrength{ 1.0f };           // IBL Specular 기여도 (0 = 없음)
        int shadingMode{ -1 };                       // -1: 전역, 0~7: 개별 셰이딩 모드, 6: OnlyTextureWithOutline, 7: ToonPBREditable
        std::string assetPath;                     // 선택된 머티리얼 에셋 경로 (옵션)
        std::string albedoTexturePath; // 알베도 텍스처 경로 (.alice 또는 원본)
        bool transparent{ false };     // 알파 블렌딩 여부 (투명 오브젝트)
        
        // 노말맵 강도 조절 (0.0: 평평, 1.0: 원본, >1.0: 과장)
        float normalStrength{ 1.0f };
        
        // 아웃라인 파라미터 (shadingMode == 6일 때 사용)
        DirectX::XMFLOAT3 outlineColor{ 0.0f, 0.0f, 0.0f }; // 아웃라인 색상 (기본값: 검정)
        float outlineWidth{ 0.0f };                        // 아웃라인 두께 (월드 단위, 기본값: 0)

        // ToonPBREditable 파라미터 (shadingMode == 7)
        // - cut: 단계 구간 분리 (NdotL 기준)
        // - level: 각 단계 밝기 (최상단은 1.0으로 고정)
        float toonPbrCut1{ 0.2f };
        float toonPbrCut2{ 0.5f };
        float toonPbrCut3{ 0.95f };
        float toonPbrLevel1{ 0.1f };
        float toonPbrLevel2{ 0.4f };
        float toonPbrLevel3{ 0.7f };
        float toonPbrStrength{ 1.0f }; // 0: 부드러운 PBR, 1: 완전 Toon
        bool  toonPbrBlur{ false };    // 계단 사이를 부드럽게 블러 처리

        Alice_Get_Set(color);
        Alice_Get_Set(alpha);
        Alice_Get_Set(roughness);
        Alice_Get_Set(metalness);
        Alice_Get_Set(ambientOcclusion);
        Alice_Get_Set(envDiffuseStrength);
        Alice_Get_Set(envSpecularStrength);
        Alice_Get_Set(shadingMode);
        Alice_Get_Set(assetPath);
        Alice_Get_Set(albedoTexturePath);
        Alice_Get_Set(transparent);
        Alice_Get_Set(normalStrength);
        Alice_Get_Set(outlineColor);
        Alice_Get_Set(outlineWidth);
        Alice_Get_Set(toonPbrCut1);
        Alice_Get_Set(toonPbrCut2);
        Alice_Get_Set(toonPbrCut3);
        Alice_Get_Set(toonPbrLevel1);
        Alice_Get_Set(toonPbrLevel2);
        Alice_Get_Set(toonPbrLevel3);
        Alice_Get_Set(toonPbrStrength);
        Alice_Get_Set(toonPbrBlur);
    };
}
