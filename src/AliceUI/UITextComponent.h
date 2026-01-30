#pragma once

#include <string>
#include <DirectXMath.h>
#include "AliceUI/UICommon.h"

namespace Alice
{
	struct UITextComponent
	{
		// .fnt 폰트 파일 경로
		std::string fontPath;

		// 텍스트
		std::string text;

		// 크기/색상
		float fontSize{ 24.0f };
		DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

		// 정렬
		AliceUI::UIAlignH alignH{ AliceUI::UIAlignH::Left };
		AliceUI::UIAlignV alignV{ AliceUI::UIAlignV::Top };

		// 줄바꿈/폭 제한
		bool wrap{ false };
		float maxWidth{ 0.0f };
		float lineSpacing{ 0.0f };
	};
}
