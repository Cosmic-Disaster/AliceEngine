#pragma once

#include <DirectXMath.h>
#include "AliceUI/UICommon.h"

namespace Alice
{
	struct UITransformComponent
	{
		// Anchor (0~1). Screen UI일 때 기준점/스트레치 범위
		DirectX::XMFLOAT2 anchorMin{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 anchorMax{ 0.0f, 0.0f };

		// Position/Size는 픽셀 단위 (Screen), 월드 단위 (World)
		DirectX::XMFLOAT2 position{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 size{ 100.0f, 100.0f };

		// Pivot/Scale/Rotation (pivot: centered range, (0,0)=center, (0.5,0.5)=right-top)
		DirectX::XMFLOAT2 pivot{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 scale{ 1.0f, 1.0f };
		float rotationRad{ 0.0f };

		// 정렬 옵션 (pivot 대체)
		AliceUI::UIAlignH alignH{ AliceUI::UIAlignH::Center };
		AliceUI::UIAlignV alignV{ AliceUI::UIAlignV::Center };
		bool useAlignment{ false };

		// 정렬/출력 순서
		int sortOrder{ 0 };
	};
}
