#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <d3d11.h>
#include <wrl/client.h>

struct ID3D11ShaderResourceView;
struct ID3D11Device;

namespace Alice
{
	class ResourceManager;

	struct UIFontGlyph
	{
		int id = 0;
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;
		float xOffset = 0.0f;
		float yOffset = 0.0f;
		float xAdvance = 0.0f;
		float u0 = 0.0f;
		float v0 = 0.0f;
		float u1 = 0.0f;
		float v1 = 0.0f;
	};

	struct UIFont
	{
		float lineHeight = 0.0f;
		int texWidth = 0;
		int texHeight = 0;
		std::string texturePath;
		std::unordered_map<int, UIFontGlyph> glyphs;
	};

	class UIFontCache
	{
	public:
		const UIFont* Load(const ResourceManager& resources, ID3D11Device* device, const std::string& fontPath);
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetFontTexture(const std::string& fontPath) const;

	private:
		struct FontEntry
		{
			std::unique_ptr<UIFont> font;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
		};

		std::unordered_map<std::string, FontEntry> m_fonts;
	};
}
