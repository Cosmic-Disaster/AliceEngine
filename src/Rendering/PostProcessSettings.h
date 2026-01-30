#pragma once

#include <DirectXMath.h>
#include "Rendering/RenderTypes.h"
#include <algorithm>

namespace Alice
{
    /// Unreal Engine 스타일의 Post Process Settings 구조체
    /// 각 항목마다 override 플래그를 가져서, 볼륨이 해당 항목에 관여하는지 제어합니다.
    struct PostProcessSettings
    {
        // ==== Exposure ====
        bool bOverride_Exposure = false;
        float exposure = 0.0f;  // 기본값: 0 = 1.0배
        
        bool bOverride_MaxHDRNits = false;
        float maxHDRNits = 1000.0f;  // HDR 모니터 최대 밝기 (nits)

        // ==== Color Grading ====
        bool bOverride_ColorGradingSaturation = false;
        DirectX::XMFLOAT3 saturation = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        bool bOverride_ColorGradingContrast = false;
        DirectX::XMFLOAT3 contrast = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        bool bOverride_ColorGradingGamma = false;
        DirectX::XMFLOAT3 gamma = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        bool bOverride_ColorGradingGain = false;
        DirectX::XMFLOAT3 gain = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        // ==== Bloom ====
        bool bOverride_BloomThreshold = false;
        float bloomThreshold = 1.0f;

        bool bOverride_BloomKnee = false;
        float bloomKnee = 0.5f;

        bool bOverride_BloomIntensity = false;
        float bloomIntensity = 0.5f;

        bool bOverride_BloomGaussianIntensity = false;
        float bloomGaussianIntensity = 1.0f;

        bool bOverride_BloomRadius = false;
        float bloomRadius = 1.0f;

        bool bOverride_BloomDownsample = false;
        int bloomDownsample = 2;

        /// 기본 설정으로 초기화 (모든 override = false)
        PostProcessSettings() = default;

        /// PostProcessParams에서 기본값 복사 (override는 false로 유지)
        static PostProcessSettings FromDefaults()
        {
            PostProcessSettings settings;
            settings.exposure = 0.0f;
            settings.maxHDRNits = 1000.0f;
            settings.saturation = DirectX::XMFLOAT3(
                ColorGradingLimits::SaturationDefault,
                ColorGradingLimits::SaturationDefault,
                ColorGradingLimits::SaturationDefault
            );
            settings.contrast = DirectX::XMFLOAT3(
                ColorGradingLimits::ContrastDefault,
                ColorGradingLimits::ContrastDefault,
                ColorGradingLimits::ContrastDefault
            );
            settings.gamma = DirectX::XMFLOAT3(
                ColorGradingLimits::GammaDefault,
                ColorGradingLimits::GammaDefault,
                ColorGradingLimits::GammaDefault
            );
            settings.gain = DirectX::XMFLOAT3(
                ColorGradingLimits::GainDefault,
                ColorGradingLimits::GainDefault,
                ColorGradingLimits::GainDefault
            );
            settings.bloomThreshold = 1.0f;
            settings.bloomKnee = 0.5f;
            settings.bloomIntensity = 0.5f;
            settings.bloomGaussianIntensity = 1.0f;
            settings.bloomRadius = 1.0f;
            settings.bloomDownsample = 2;
            return settings;
        }
    };

    /// PostProcessSettings 블렌딩 헬퍼 함수
    namespace PostProcessBlend
    {
        /// float 값 블렌딩: lerp(Final, Volume, weight)
        /// weight에 비례하여 점진적으로 블렌딩 (UE 스타일)
        inline void BlendFloat(float& final, float volume, float weight, bool override)
        {
            if (override && weight > 0.0f)
            {
                // lerp: final = final + (volume - final) * weight
                final = final + (volume - final) * weight;
            }
        }

        /// float3 값 블렌딩: lerp(Final, Volume, weight)
        /// weight에 비례하여 점진적으로 블렌딩 (UE 스타일)
        inline void BlendFloat3(DirectX::XMFLOAT3& final, const DirectX::XMFLOAT3& volume, float weight, bool override)
        {
            if (override && weight > 0.0f)
            {
                // lerp: final = final + (volume - final) * weight
                final.x = final.x + (volume.x - final.x) * weight;
                final.y = final.y + (volume.y - final.y) * weight;
                final.z = final.z + (volume.z - final.z) * weight;
            }
        }

        /// int 값 블렌딩: lerp(Final, Volume, weight)
        inline void BlendInt(int& final, int volume, float weight, bool override)
        {
            if (override && weight > 0.0f)
            {
                float finalF = static_cast<float>(final);
                float volumeF = static_cast<float>(volume);
                finalF = finalF + (volumeF - finalF) * weight;
                final = static_cast<int>(finalF + 0.5f);  // 반올림
            }
        }

        /// PostProcessSettings 전체 블렌딩 (maxHDRNits 포함)
        inline void BlendSettings(PostProcessSettings& final, const PostProcessSettings& volume, float weight)
        {
            weight = std::clamp(weight, 0.0f, 1.0f);
            BlendFloat(final.exposure, volume.exposure, weight, volume.bOverride_Exposure);
            BlendFloat(final.maxHDRNits, volume.maxHDRNits, weight, volume.bOverride_MaxHDRNits);
            BlendFloat3(final.saturation, volume.saturation, weight, volume.bOverride_ColorGradingSaturation);
            BlendFloat3(final.contrast, volume.contrast, weight, volume.bOverride_ColorGradingContrast);
            BlendFloat3(final.gamma, volume.gamma, weight, volume.bOverride_ColorGradingGamma);
            BlendFloat3(final.gain, volume.gain, weight, volume.bOverride_ColorGradingGain);
            BlendFloat(final.bloomThreshold, volume.bloomThreshold, weight, volume.bOverride_BloomThreshold);
            BlendFloat(final.bloomKnee, volume.bloomKnee, weight, volume.bOverride_BloomKnee);
            BlendFloat(final.bloomIntensity, volume.bloomIntensity, weight, volume.bOverride_BloomIntensity);
            BlendFloat(final.bloomGaussianIntensity, volume.bloomGaussianIntensity, weight, volume.bOverride_BloomGaussianIntensity);
            BlendFloat(final.bloomRadius, volume.bloomRadius, weight, volume.bOverride_BloomRadius);
            BlendInt(final.bloomDownsample, volume.bloomDownsample, weight, volume.bOverride_BloomDownsample);
        }

    }
}
