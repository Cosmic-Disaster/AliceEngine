#pragma once

#include <string>
#include <DirectXMath.h>

namespace Alice
{
	struct UIImageComponent
	{
		// 텍스처 경로 (Assets/Resource/Cooked)
		std::string texturePath;

		// 색상
		DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

		// UV (u0, v0, u1, v1)
		DirectX::XMFLOAT4 uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };

		// 비율 유지
		bool preserveAspect{ false };
	};
}
