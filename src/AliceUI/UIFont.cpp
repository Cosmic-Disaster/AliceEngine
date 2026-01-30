#include "AliceUI/UIFont.h"

#include <sstream>
#include <filesystem>
#include "Core/ResourceManager.h"
#include "Core/Logger.h"

namespace Alice
{
	namespace
	{
		bool ParseKeyValue(const std::string& token, std::string& outKey, std::string& outValue)
		{
			const std::size_t eq = token.find('=');
			if (eq == std::string::npos)
				return false;

			outKey = token.substr(0, eq);
			outValue = token.substr(eq + 1);

			if (!outValue.empty() && outValue.front() == '"')
			{
				if (outValue.back() == '"')
					outValue = outValue.substr(1, outValue.size() - 2);
			}

			return true;
		}

		void ParseFontLine(const std::string& line, std::unordered_map<std::string, std::string>& out)
		{
			out.clear();
			std::istringstream iss(line);
			std::string token;
			iss >> token; // 첫 토큰은 키워드

			while (iss >> token)
			{
				std::string key;
				std::string value;
				if (ParseKeyValue(token, key, value))
				{
					out[key] = value;
				}
			}
		}
	}

	const UIFont* UIFontCache::Load(const ResourceManager& resources, ID3D11Device* device, const std::string& fontPath)
	{
		auto it = m_fonts.find(fontPath);
		if (it != m_fonts.end())
			return it->second.font.get();

		auto text = resources.Load<std::string>(fontPath);
		if (!text)
		{
			ALICE_LOG_WARN("[AliceUI] UIFont load failed: %s", fontPath.c_str());
			return nullptr;
		}

		auto font = std::make_unique<UIFont>();
		std::unordered_map<std::string, std::string> kv;

		std::istringstream ss(*text);
		std::string line;
		while (std::getline(ss, line))
		{
			if (line.rfind("common ", 0) == 0)
			{
				ParseFontLine(line, kv);
				if (kv.count("lineHeight")) font->lineHeight = std::stof(kv["lineHeight"]);
				if (kv.count("scaleW")) font->texWidth = std::stoi(kv["scaleW"]);
				if (kv.count("scaleH")) font->texHeight = std::stoi(kv["scaleH"]);
			}
			else if (line.rfind("page ", 0) == 0)
			{
				ParseFontLine(line, kv);
				auto itFile = kv.find("file");
				if (itFile != kv.end())
				{
					std::filesystem::path base(fontPath);
					base = base.parent_path() / itFile->second;
					font->texturePath = base.generic_string();
				}
			}
			else if (line.rfind("char ", 0) == 0)
			{
				ParseFontLine(line, kv);
				if (!kv.count("id"))
					continue;

				UIFontGlyph glyph{};
				glyph.id = std::stoi(kv["id"]);
				glyph.x = kv.count("x") ? std::stof(kv["x"]) : 0.0f;
				glyph.y = kv.count("y") ? std::stof(kv["y"]) : 0.0f;
				glyph.w = kv.count("width") ? std::stof(kv["width"]) : 0.0f;
				glyph.h = kv.count("height") ? std::stof(kv["height"]) : 0.0f;
				glyph.xOffset = kv.count("xoffset") ? std::stof(kv["xoffset"]) : 0.0f;
				glyph.yOffset = kv.count("yoffset") ? std::stof(kv["yoffset"]) : 0.0f;
				glyph.xAdvance = kv.count("xadvance") ? std::stof(kv["xadvance"]) : 0.0f;

				if (font->texWidth > 0 && font->texHeight > 0)
				{
					glyph.u0 = glyph.x / static_cast<float>(font->texWidth);
					glyph.v0 = glyph.y / static_cast<float>(font->texHeight);
					glyph.u1 = (glyph.x + glyph.w) / static_cast<float>(font->texWidth);
					glyph.v1 = (glyph.y + glyph.h) / static_cast<float>(font->texHeight);
				}

				font->glyphs[glyph.id] = glyph;
			}
		}

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tex;
		if (!font->texturePath.empty())
		{
			tex = resources.Load<ID3D11ShaderResourceView>(font->texturePath, device);
		}

		FontEntry entry{};
		entry.font = std::move(font);
		entry.texture = tex;
		m_fonts.emplace(fontPath, std::move(entry));
		return m_fonts[fontPath].font.get();
	}

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> UIFontCache::GetFontTexture(const std::string& fontPath) const
	{
		auto it = m_fonts.find(fontPath);
		if (it == m_fonts.end())
			return {};
		return it->second.texture;
	}
}
