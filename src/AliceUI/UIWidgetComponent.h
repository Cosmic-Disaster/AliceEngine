#pragma once

#include <string>
#include "AliceUI/UICommon.h"

namespace Alice
{
	struct UIWidgetComponent
	{
		// 위젯 이름 (BindWidget용)
		std::string widgetName;

		// Screen / World
		AliceUI::UISpace space{ AliceUI::UISpace::Screen };

		// Visible / Hidden / Collapsed
		AliceUI::UIVisibility visibility{ AliceUI::UIVisibility::Visible };

		// 입력/상호작용
		bool raycastTarget{ true };
		bool interactable{ true };

		// World UI 옵션
		bool billboard{ false };

		// 셰이더 이름 (기본: Default)
		std::string shaderName{ "Default" };
	};
}
