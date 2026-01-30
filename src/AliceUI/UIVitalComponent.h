#pragma once

#include <DirectXMath.h>

namespace Alice
{
    struct UIVitalComponent
    {
        DirectX::XMFLOAT4 color{ 0.1f, 1.0f, 0.2f, 1.0f };
        DirectX::XMFLOAT4 backgroundColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        float amplitude{ 0.25f };
        float frequency{ 2.0f };
        float speed{ 1.5f };
        float thickness{ 0.02f };
    };
}
