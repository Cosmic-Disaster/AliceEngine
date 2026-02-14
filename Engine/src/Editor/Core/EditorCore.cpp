#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"
#include "Editor/Core/EditorUndoRedo.h"

#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Foundation/Logger.h"
#include "Editor/Core/ReflectionUI.h"
#include "Runtime/ECS/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Runtime/ECS/EditorComponentRegistry.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Resources/Serialization/SocketSerialization.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIRenderer.h"
#include "Runtime/UI/UICurveAsset.h"
#include <cstdint>
#include <cstdio>
#include <set>
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/PostProcessSettings.h"
#include "Editor/Tools/Blueprint/AnimBlueprintEditor.h"
#include "Runtime/Gameplay/Combat/CombatPhysicsLayers.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <iterator>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "ImGuizmo.h"

#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <sstream>
#include <Runtime/Resources/Prefab.h>
#include <Runtime/Scripting/IScript.h>
#include <Runtime/Scripting/ScriptSystem.h>
#include <Runtime/Scripting/ScriptFactory.h>
#include <Runtime/Rendering/Data/Material.h>
#include <Runtime/Resources/SceneFile.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ShlObj.h>   // 폴더 선택 다이얼로그 (SHBrowseForFolderW)
#include <Runtime/Importing/FbxAsset.h>
#include "ThirdParty/json/json.hpp"

// 텍스처 로딩용 DirectXTK
#include <DirectXTK/WICTextureLoader.h>
#include "Runtime/ECS/Components/TransformComponent.h"

using namespace DirectX;

namespace Alice
{
	// 씬 상태 전역 - 여러 네임스페이스에서 공유
	bool g_SceneDirty = false;
	bool g_HasCurrentScenePath = false;
	std::filesystem::path g_CurrentScenePath{};
	bool g_ShowBuildGameWindow = false;
	bool g_ShowPvdSettingsWindow = false;
	bool g_RequestSceneLoad = false;
	std::filesystem::path g_NextScenePath{};
	bool g_ShowSceneLoadError = false;
	std::string g_SceneLoadErrorMsg{};
	bool g_MaterialEditorOpen = false;
	std::filesystem::path g_MaterialEditorPath{};
	MaterialComponent g_MaterialEditorData{};
	bool g_UICurveEditorOpen = false;
	std::filesystem::path g_UICurveEditorPath{};
	UICurveAsset g_UICurveEditorData{};
	int g_UICurveEditorSelected = -1;

	// ICommand는 이제 EditorCore.h에 정의됨

	namespace
	{
		struct ScopedHandle
		{
			HANDLE h = nullptr;
			ScopedHandle() = default;
			explicit ScopedHandle(HANDLE handle) : h(handle) {}
			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;
			ScopedHandle(ScopedHandle&& other) noexcept : h(other.h) { other.h = nullptr; }
			ScopedHandle& operator=(ScopedHandle&& other) noexcept
			{
				if (this != &other)
				{
					if (h) CloseHandle(h);
					h = other.h;
					other.h = nullptr;
				}
				return *this;
			}
			~ScopedHandle() { if (h) CloseHandle(h); }
		};

	}

	EditorCore::~EditorCore()
	{
		Shutdown();
	}

	bool EditorCore::Initialize(HWND hwnd, ID3D11RenderDevice& renderDevice)
	{
		if (m_initialized)
			return true;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// 폰트 아틀라스를 모두 지우고, 한글/일본어를 포함한 폰트를 기본 폰트로 사용합니다.
		io.Fonts->Clear();

		ImFontConfig baseConfig{};
		baseConfig.MergeMode = false;
		const std::wstring fontKr =
			ResourceManager::Get().Resolve("Resource/Fonts/NotoSansKR-Regular.ttf").wstring();
		io.FontDefault = io.Fonts->AddFontFromFileTTF(
			Utf8FromWString(fontKr).c_str(),
			18.0f,
			&baseConfig,
			io.Fonts->GetGlyphRangesKorean());

		ImFontConfig jpConfig{};
		jpConfig.MergeMode = true;
		jpConfig.PixelSnapH = true;
		const std::wstring fontJp =
			ResourceManager::Get().Resolve("Resource/Fonts/meiryo.ttc").wstring();
		io.Fonts->AddFontFromFileTTF(
			Utf8FromWString(fontJp).c_str(),
			18.0f,
			&jpConfig,
			io.Fonts->GetGlyphRangesJapanese());

		m_hwnd = hwnd;
		m_renderDevice = &renderDevice;

		auto* d3dDevice = renderDevice.GetDevice();
		auto* d3dContext = renderDevice.GetImmediateContext();

		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(d3dDevice, d3dContext);
		ImGui_ImplDX11_CreateDeviceObjects();

		if (m_aliceUIRenderer && io.FontDefault && io.Fonts)
		{
			const ImTextureID texId = io.Fonts->TexID.GetTexID();
			if (texId != ImTextureID_Invalid)
			{
				m_aliceUIRenderer->SetDefaultImGuiFont(io.FontDefault,
					reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<uintptr_t>(texId)));
			}
		}

		// ImGuizmo 스타일 설정
		ImGuizmo::Style& style = ImGuizmo::GetStyle();
		style.RotationLineThickness = 3.0f;
		style.RotationOuterLineThickness = 2.0f;

		// Default PostProcess Settings 초기화 및 로드
		m_defaultPostProcessSettings = PostProcessSettings::FromDefaults();
		LoadDefaultPostProcessSettings();

		m_initialized = true;
		return true;
	}

	void EditorCore::SetAliceUIRenderer(UIRenderer* renderer)
	{
		m_aliceUIRenderer = renderer;
		if (!m_initialized || !m_aliceUIRenderer)
			return;

		ImGuiIO& io = ImGui::GetIO();

		if (io.FontDefault && io.Fonts)
		{
			const ImTextureID texId = io.Fonts->TexID.GetTexID();
			if (texId != ImTextureID_Invalid)
			{
				m_aliceUIRenderer->SetDefaultImGuiFont(io.FontDefault,
					reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<uintptr_t>(texId)));
			}
		}
	}

	void EditorCore::Shutdown()
	{
		if (!m_initialized)
			return;

		if (ImGui::GetCurrentContext() != nullptr)
		{
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}

		m_initialized = false;
	}

	void EditorCore::BeginFrame()
	{
		if (!m_initialized)
			return;

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void EditorCore::RenderDrawData()
	{
		if (!m_initialized)
			return;

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	void EditorCore::HandleGlobalUndoRedo(World& world, EntityId& selectedEntity, bool isPlaying)
	{
		ImGuiIO& io = ImGui::GetIO();
		const bool isTextInputActive = io.WantTextInput || ImGui::IsAnyItemActive();

		if (m_inputSystem && !isTextInputActive && !isPlaying)
		{
			const bool ctrlDown = m_inputSystem->IsKeyDown(Keyboard::Keys::LeftControl) ||
				m_inputSystem->IsKeyDown(Keyboard::Keys::RightControl);

			if (ctrlDown && m_inputSystem->IsKeyPressed(Keyboard::Keys::Z))
			{
				ExecuteUndo(world, selectedEntity);
			}
			else if (ctrlDown && m_inputSystem->IsKeyPressed(Keyboard::Keys::Y))
			{
				ExecuteRedo(world, selectedEntity);
			}
		}
	}

	void EditorCore::DrawEditorUI(World& world,
		Camera& camera,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		SceneManager* sceneManager,
		float deltaTime,
		float fps,
		bool& isPlaying,
		int& shadingMode,
		bool& useFillLight,
		EntityId& selectedEntity,
		ViewportPicker& picker,
		float& cameraMoveSpeed,
		bool& useForwardRendering,
		LightingParameters& lightingParams,
		int& skyboxChoice,
		std::string& skyboxCustomDir,
		std::string& skyboxCustomPrefix,
		int& skyboxResolution,
		bool& pvdEnabled,
		std::string& pvdHost,
		int& pvdPort,
		bool& isDebugDraw)
	{
		// 매 프레임 Default PostProcess Settings를 RenderSystem에 전달
		deferred.SetDefaultPostProcessSettings(m_defaultPostProcessSettings);
		// ForwardRenderSystem에도 동일한 함수가 필요하면 추가
		// forward.SetDefaultPostProcessSettings(m_defaultPostProcessSettings);

		// SceneManager에서 현재 씬 파일 경로를 조회하여 g_CurrentScenePath 업데이트
		if (sceneManager)
		{
			const auto& currentScenePath = sceneManager->GetCurrentSceneFilePath();
			if (!currentScenePath.empty() && currentScenePath != g_CurrentScenePath)
			{
				g_CurrentScenePath = currentScenePath;
				g_HasCurrentScenePath = true;
			}
		}

		HandleGlobalUndoRedo(world, selectedEntity, isPlaying);
		SetupDockSpaceAndDefaultLayout();
		DrawMainMenuBar(world, deltaTime, fps, isPlaying, selectedEntity, useForwardRendering, isDebugDraw);

		DrawPvdSettingsWindow(pvdEnabled, pvdHost, pvdPort);
		DrawBuildGameWindow();

		DrawHierarchyWindow(world, selectedEntity);
		DrawInspectorWindow(world, selectedEntity);
		DrawProjectWindow(world, selectedEntity);
		DrawGameViewportWindow(world, camera, forward, deferred, selectedEntity, picker, cameraMoveSpeed, useForwardRendering, isPlaying, shadingMode, useFillLight);
		DrawCameraWindow(world, camera, forward, deferred, cameraMoveSpeed, selectedEntity, useForwardRendering);
		DrawLightingWindow(world, forward, deferred, shadingMode, useFillLight, useForwardRendering,
			lightingParams, skyboxChoice, skyboxCustomDir, skyboxCustomPrefix, skyboxResolution);

		DrawMaterialAssetEditorWindow(world);
		DrawUICurveAssetEditorWindow();

		HandleSceneLoadFlow(world, sceneManager, isPlaying, selectedEntity);
	}

	// ComponentEditCommandRTTR 구현
	ComponentEditCommandRTTR::ComponentEditCommandRTTR(EntityId id,
		const EditorComponentDesc* d,
		JsonRttr::json oldJ,
		JsonRttr::json newJ)
		: entityId(id), desc(d), oldJson(std::move(oldJ)), newJson(std::move(newJ))
	{
		description = std::string("Edit ") + (desc ? desc->displayName : "Component");
	}

	void ComponentEditCommandRTTR::Execute(World& world, EntityId&)
	{
		if (!desc) return;
		rttr::instance inst = desc->getInstance(world, entityId);
		if (!inst.is_valid()) return;
		JsonRttr::FromJsonObject(inst, newJson);
	}

	void ComponentEditCommandRTTR::Undo(World& world, EntityId&)
	{
		if (!desc) return;
		rttr::instance inst = desc->getInstance(world, entityId);
		if (!inst.is_valid()) return;
		JsonRttr::FromJsonObject(inst, oldJson);
	}

} // namespace Alice
