#pragma once

namespace Alice
{
    struct UIHover3DComponent
    {
        bool enabled{ true };
        float maxAngle{ 0.7f };    // radians
        float speed{ 8.0f };       // smoothing
        float perspective{ 0.003f };

        bool hovered{ false };
        float angleX{ 0.0f };
        float angleY{ 0.0f };
    };
}
