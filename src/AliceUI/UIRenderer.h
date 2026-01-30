#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <wrl/client.h>
#include <DirectXMath.h>

#include "AliceUI/UICommon.h"
#include "AliceUI/UICurveAsset.h"
#include "AliceUI/UIFont.h"
#include "Core/Entity.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ImFont;
struct ImFontAtlas;

namespace Alice
{
	class World;
	class InputSystem;
	class Camera;
	class ResourceManager;

	struct UITransformComponent;
	struct UIWidgetComponent;

	class UIRenderer
	{
	public:
		bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, ResourceManager* resources);
		void Shutdown();

		void Update(World& world, InputSystem& input, const Camera& camera, float screenW, float screenH, float deltaTime);

		void RenderScreen(const World& world, const Camera& camera, ID3D11RenderTargetView* targetRTV, float screenW, float screenH);
		void RenderWorld(const World& world, const Camera& camera, ID3D11RenderTargetView* targetRTV, ID3D11DepthStencilView* dsv);

		// 셰이더 확장: 커스텀 픽셀 셰이더 등록
		bool RegisterShader(const std::string& name, const char* pixelShaderSource);
		void SetDefaultImGuiFont(ImFont* font, ID3D11ShaderResourceView* fontSRV);
		void SetScreenInputRect(float x, float y, float width, float height, float renderWidth, float renderHeight);
		void ClearScreenInputRect();
		void SetScreenMouseOverride(float x, float y);
		void ClearScreenMouseOverride();

	private:
		struct ScreenLayout
		{
			DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
			DirectX::XMFLOAT2 size{ 0.0f, 0.0f };
			DirectX::XMFLOAT2 pivot{ 0.5f, 0.5f };
			bool pivotBaked = true;
		};

		struct ScreenRect
		{
			float minX = 0.0f;
			float minY = 0.0f;
			float maxX = 0.0f;
			float maxY = 0.0f;
		};

		struct UIVertex
		{
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT2 uv;
			DirectX::XMFLOAT4 color;
		};

		struct UIConstants
		{
			DirectX::XMMATRIX viewProj;
		};

		struct UIPixelConstants
		{
			DirectX::XMFLOAT4 outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
			DirectX::XMFLOAT4 glowColor{ 1.0f, 1.0f, 1.0f, 1.0f };
			DirectX::XMFLOAT4 vitalColor{ 0.1f, 1.0f, 0.2f, 1.0f };
			DirectX::XMFLOAT4 vitalBgColor{ 0.0f, 0.0f, 0.0f, 0.0f };
			DirectX::XMFLOAT4 params0{ 0.0f, 0.0f, 0.0f, 1.0f };
			DirectX::XMFLOAT4 params1{ 0.0f, 0.5f, 0.01f, 1.0f };
			DirectX::XMFLOAT4 params2{ 0.0f, 0.35f, 0.0f, 0.0f };
			DirectX::XMFLOAT4 params3{ 0.25f, 1.0f, 0.0f, 0.0f };
			DirectX::XMFLOAT4 params4{ 0.0f, 0.25f, 2.0f, 1.5f };
			DirectX::XMFLOAT4 params5{ 0.02f, 0.0f, 0.0f, 0.0f };
			DirectX::XMFLOAT4 time{ 0.0f, 1.0f, 0.0f, 0.0f };
		};

		void BuildScreenLayout(const World& world, float screenW, float screenH);
		void BuildScreenLayoutRecursive(const World& world,
			EntityId id,
			const DirectX::XMMATRIX& parent,
			const DirectX::XMFLOAT2& parentSize);
		bool GetScreenLayout(EntityId id, ScreenLayout& out) const;
		bool GetScreenRect(EntityId id, ScreenRect& out) const;

		void UpdateButtonStates(World& world, InputSystem& input, float screenW, float screenH);

		void RenderImage(const World& world, EntityId id, const ScreenLayout& layout, const DirectX::XMFLOAT4& tintOverride, const std::string& overrideTexture);
		void RenderText(const World& world, EntityId id, const ScreenLayout& layout);
		void RenderGauge(const World& world, EntityId id, const ScreenLayout& layout);

		void DrawQuad(const UIVertex* verts, ID3D11ShaderResourceView* texture, ID3D11PixelShader* ps, const UIPixelConstants& pixel);
		void DrawGlyphs(const std::vector<UIVertex>& verts, ID3D11ShaderResourceView* texture, ID3D11PixelShader* ps, const UIPixelConstants& pixel);

		DirectX::XMMATRIX BuildScreenLocalMatrix(const UITransformComponent& t, const DirectX::XMFLOAT2& refSize, DirectX::XMFLOAT2& outSize, DirectX::XMFLOAT2& outPivot) const;
		DirectX::XMFLOAT2 ResolvePivot(const UITransformComponent& t) const;

		UIPixelConstants BuildPixelConstants(const World& world, EntityId id) const;
		const UICurveAsset* GetCurveAsset(const std::string& path);

		ID3D11ShaderResourceView* GetTexture(const std::string& path);
		ID3D11PixelShader* GetPixelShader(const std::string& name) const;
		bool ResolveUIFont(const std::string& fontPath, float fontSize, ImFont*& outFont, ID3D11ShaderResourceView*& outSrv);

		ID3D11Device* m_device = nullptr;
		ID3D11DeviceContext* m_context = nullptr;
		ResourceManager* m_resources = nullptr;

		std::unordered_map<EntityId, ScreenLayout> m_screenLayouts;
		std::unordered_map<EntityId, ScreenRect> m_screenRects;

		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11PixelShader>> m_customPS;
		UIFontCache m_fontCache;
		ImFont* m_imguiFont{ nullptr };
		ID3D11ShaderResourceView* m_imguiFontSRV{ nullptr };
		struct RuntimeUIFont
		{
			std::unique_ptr<ImFontAtlas> atlas;
			ImFont* font{ nullptr };
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			std::string fontPath;
			float baseSize{ 18.0f };
		};
		RuntimeUIFont m_runtimeUIFont;
		std::unordered_map<std::string, RuntimeUIFont> m_runtimeUIFontCache;
		bool m_inputRectActive{ false };
		float m_inputRectX{ 0.0f };
		float m_inputRectY{ 0.0f };
		float m_inputRectW{ 0.0f };
		float m_inputRectH{ 0.0f };
		float m_inputRenderW{ 0.0f };
		float m_inputRenderH{ 0.0f };
		bool m_mouseOverrideActive{ false };
		float m_mouseOverrideX{ 0.0f };
		float m_mouseOverrideY{ 0.0f };

		float m_timeSeconds{ 0.0f };
		bool m_initialized{ false };

		struct CurveCacheEntry
		{
			UICurveAsset asset{};
			std::filesystem::file_time_type timestamp{};
			bool valid{ false };
		};
		std::unordered_map<std::string, CurveCacheEntry> m_curveCache;

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whiteSRV;

		Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psDefault;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psGray;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbUI;

		Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbUIPixel;

		Microsoft::WRL::ComPtr<ID3D11Buffer> m_vb;
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_ib;
		UINT m_vbStride = 0;

		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendAlpha;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rsNoCull;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthOff;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthRead;
	};
}
