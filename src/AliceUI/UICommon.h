#pragma once

#include <DirectXMath.h>

namespace Alice
{
	namespace AliceUI
	{
		enum class UISpace
		{
			Screen = 0,
			World = 1
		};

		enum class UIVisibility
		{
			Visible = 0,
			Hidden = 1,
			Collapsed = 2
		};

		enum class UIAlignH
		{
			Left = 0,
			Center = 1,
			Right = 2
		};

		enum class UIAlignV
		{
			Top = 0,
			Center = 1,
			Bottom = 2
		};

		enum class UIButtonState
		{
			Normal = 0,
			Hovered = 1,
			Pressed = 2,
			Disabled = 3
		};

		enum class UIGaugeDirection
		{
			LeftToRight = 0,
			RightToLeft = 1,
			BottomToTop = 2,
			TopToBottom = 3
		};

		inline DirectX::XMFLOAT2 AlignToPivot(UIAlignH h, UIAlignV v)
		{
			// centered pivot range: (-0.5,-0.5) bottom-left, (0,0) center, (0.5,0.5) top-right
			float px = 0.0f;
			float py = 0.0f;

			switch (h)
			{
			case UIAlignH::Left:   px = -0.5f; break;
			case UIAlignH::Center: px = 0.0f; break;
			case UIAlignH::Right:  px = 0.5f; break;
			}

			switch (v)
			{
			case UIAlignV::Top:    py = 0.5f; break;
			case UIAlignV::Center: py = 0.0f; break;
			case UIAlignV::Bottom: py = -0.5f; break;
			}

			return DirectX::XMFLOAT2(px, py);
		}

		inline float Clamp01(float v)
		{
			if (v < 0.0f) return 0.0f;
			if (v > 1.0f) return 1.0f;
			return v;
		}
	}
}
