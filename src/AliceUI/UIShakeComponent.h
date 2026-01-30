#pragma once

#include <DirectXMath.h>

namespace Alice
{
    struct UIShakeComponent
    {
        bool playing{ false };
        float amplitude{ 10.0f }; // pixels for screen, world units for world UI
        float frequency{ 18.0f };
        float duration{ 0.3f };
        float elapsed{ 0.0f };
        DirectX::XMFLOAT2 offset{ 0.0f, 0.0f };

        void Start(float inAmplitude, float inDuration, float inFrequency)
        {
            amplitude = inAmplitude;
            duration = inDuration;
            frequency = inFrequency;
            elapsed = 0.0f;
            playing = true;
        }

        void Stop()
        {
            playing = false;
            elapsed = 0.0f;
            offset = DirectX::XMFLOAT2(0.0f, 0.0f);
        }
    };
}
