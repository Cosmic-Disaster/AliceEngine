#pragma once

#include <DirectXMath.h>

namespace Alice
{
    struct UIEffectComponent
    {
        // Outline
        bool outlineEnabled{ false };
        DirectX::XMFLOAT4 outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float outlineThickness{ 1.0f }; // pixels

        // Radial cooldown / mask
        bool radialEnabled{ false };
        float radialFill{ 1.0f };        // 0..1
        float radialInner{ 0.0f };       // 0..0.5 (uv radius)
        float radialOuter{ 0.5f };       // 0..0.5 (uv radius)
        float radialSoftness{ 0.01f };   // 0..0.1
        float radialAngleOffset{ 0.0f }; // radians, 0 = top
        bool radialClockwise{ true };
        float radialDim{ 0.35f };        // dim amount for masked area

        // Sweep / glow
        bool glowEnabled{ false };
        DirectX::XMFLOAT4 glowColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float glowStrength{ 0.8f };
        float glowWidth{ 0.25f };
        float glowSpeed{ 1.0f };
        float glowAngle{ 0.0f }; // radians

        // Vital sign (line graph)
        bool vitalEnabled{ false };
        DirectX::XMFLOAT4 vitalColor{ 0.1f, 1.0f, 0.2f, 1.0f };
        DirectX::XMFLOAT4 vitalBgColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        float vitalAmplitude{ 0.25f };
        float vitalFrequency{ 2.0f };
        float vitalSpeed{ 1.5f };
        float vitalThickness{ 0.02f };

        // Misc
        float globalAlpha{ 1.0f };
        float grayscale{ 0.0f }; // 0..1
    };
}
