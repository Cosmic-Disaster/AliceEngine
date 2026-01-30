#pragma once

#include <string>
#include <DirectXMath.h>
#include "AliceUI/UICommon.h"

namespace Alice
{
	struct UIGaugeComponent
	{
		float minValue{ 0.0f };
		float maxValue{ 1.0f };
		float value{ 1.0f };
		bool normalized{ false };

		AliceUI::UIGaugeDirection direction{ AliceUI::UIGaugeDirection::LeftToRight };

		std::string fillTexture;
		std::string backgroundTexture;

		DirectX::XMFLOAT4 fillColor{ 0.2f, 0.9f, 0.2f, 1.0f };
		DirectX::XMFLOAT4 backgroundColor{ 0.1f, 0.1f, 0.1f, 0.9f };

		// 보간 표시용
		float smoothing{ 0.0f };
		float displayedValue{ 1.0f };
	};
}
