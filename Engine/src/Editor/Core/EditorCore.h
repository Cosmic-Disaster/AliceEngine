#pragma once

// Windows.h의 min/max 매크로 충돌 방지 (RTTR 헤더와의 충돌 방지)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>

#include "Runtime/ECS/Entity.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Resources/Scene.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Rendering/Components/PointLightComponent.h"
#include "Runtime/Rendering/Components/SpotLightComponent.h"
#include "Runtime/Rendering/Components/RectLightComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_MeshColliderComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/Components/Phy_TerrainHeightFieldComponent.h"
#include "Runtime/Physics/Components/Phy_JointComponent.h"
#include "Editor/Core/ViewportPicker.h"
#include "Runtime/Input/InputSystem.h"
#include "Editor/Core/ReflectionUI.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/ECS/ComponentRegistry.h"
#include "Runtime/ECS/EditorComponentRegistry.h"
#include "Runtime/Rendering/PostProcessSettings.h"
#include "imgui.h"
#include <functional>
#include <string>
#include <memory>

// Forward declaration
class UIRenderer;

namespace Alice
{
	struct ID3D11RenderDevice;
	class SkinnedMeshRegistry;
	class DeferredRenderSystem;

	// Undo/Redo 시스템
	struct ICommand
	{
		virtual ~ICommand() = default;
		virtual void Execute(World& world, EntityId& selectedEntity) = 0;
		virtual void Undo(World& world, EntityId& selectedEntity) = 0;
		virtual const char* GetDescription() const = 0;
		virtual bool SupportsRedo() const { return true; }
	};

	// 컴포넌트 편집 명령 (템플릿) - 레거시 호환용
	template<typename T>
	struct ComponentEditCommand : ICommand
	{
		EntityId entityId;
		std::string componentTypeName;
		JsonRttr::json oldJson;
		JsonRttr::json newJson;
		mutable std::string description;

		ComponentEditCommand(EntityId id, const T& oldComp, const T& newComp)
			: entityId(id), componentTypeName(rttr::type::get<T>().get_name().to_string())
		{
			rttr::instance oldInst = const_cast<T&>(oldComp);
			rttr::instance newInst = const_cast<T&>(newComp);
			oldJson = JsonRttr::ToJsonObject(oldInst);
			newJson = JsonRttr::ToJsonObject(newInst);
			description = "Edit " + componentTypeName;
		}

		void Execute(World& world, EntityId& selectedEntity) override
		{
			if (auto* comp = world.GetComponent<T>(entityId))
			{
				rttr::instance inst = *comp;
				JsonRttr::FromJsonObject(inst, newJson);
			}
		}

		void Undo(World& world, EntityId& selectedEntity) override
		{
			if (auto* comp = world.GetComponent<T>(entityId))
			{
				rttr::instance inst = *comp;
				JsonRttr::FromJsonObject(inst, oldJson);
			}
		}

		const char* GetDescription() const override
		{
			return description.c_str();
		}
	};

	// RTTR 기반 컴포넌트 편집 명령 (템플릿 제거)
	struct ComponentEditCommandRTTR : ICommand
	{
		EntityId entityId{};
		const EditorComponentDesc* desc{};
		JsonRttr::json oldJson;
		JsonRttr::json newJson;
		std::string description;

		ComponentEditCommandRTTR(EntityId id,
			const EditorComponentDesc* d,
			JsonRttr::json oldJ,
			JsonRttr::json newJ);

		void Execute(World& world, EntityId&) override;
		void Undo(World& world, EntityId&) override;
		const char* GetDescription() const override { return description.c_str(); }
	};

	/// ImGui 컨텍스트 수명과 기본 에디터 유틸(도킹, 디렉터리 뷰, 에디터 패널 등)을 관리하는
	/// 간단한 코어 클래스입니다.
	class EditorCore
	{
	public:
		EditorCore() = default;
		~EditorCore();

		/// ImGui 컨텍스트와 백엔드(Win32 + DX11)를 초기화합니다.
		bool Initialize(HWND hwnd, ID3D11RenderDevice& renderDevice);

		/// ImGui 리소스를 정리합니다.
		void Shutdown();

		/// 새 ImGui 프레임을 시작합니다.
		void BeginFrame();

		/// ImGui 드로우 데이터를 렌더링합니다.
		void RenderDrawData();

		/// 에디터 전체 UI(Hierarchy, Inspector, Game, Project 등)를 그립니다.
		/// - 상태값(재생 여부, 셰이딩 모드, 선택된 엔티티 등)은 참조로 받아 직접 갱신합니다.
		void DrawEditorUI(World& world,
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
			bool& isDebugDraw
		);

		template<typename T>
		void DrawEngineComponent(const char* label, T* comp, std::function<void()> removeFn, const EntityId& _selectedEntity, const std::string& compTypeName)
		{
			if (!comp) return;
			if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
				std::string removeId = std::string("Remove##") + label;
				if (ImGui::Button(removeId.c_str())) {
					removeFn();
					extern bool g_SceneDirty;
					g_SceneDirty = true;
					return;
				}

				// 컴포넌트 편집 이벤트 처리
				static EntityId lastEditedEntity = InvalidEntityId;
				static std::string lastEditedComponentType;
				static JsonRttr::json editStartJson;

				ReflectionUI::UIEditEvent event = ReflectionUI::RenderInspector(*comp);

				// 편집 시작: oldJson 스냅샷 저장
				if (event.activated && (_selectedEntity != lastEditedEntity || lastEditedComponentType != compTypeName))
				{
					rttr::instance inst = *comp;
					editStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedEntity = _selectedEntity;
					lastEditedComponentType = compTypeName;
				}

				// 편집 종료: newJson 저장하고 커맨드 푸시
				if (event.deactivatedAfterEdit && _selectedEntity == lastEditedEntity && lastEditedComponentType == compTypeName)
				{
					rttr::instance inst = *comp;
					JsonRttr::json editEndJson = JsonRttr::ToJsonObject(inst);

					// 변경사항이 있으면 커맨드 푸시
					if (editStartJson != editEndJson)
					{
						// 타입별로 적절한 커맨드 생성
#define PUSH_COMPONENT_EDIT_CMD(T) \
							if (compTypeName == rttr::type::get<T>().get_name().to_string()) \
							{ \
								T oldComp, newComp; \
								rttr::instance oldInst = oldComp; \
								rttr::instance newInst = newComp; \
								JsonRttr::FromJsonObject(oldInst, editStartJson); \
								JsonRttr::FromJsonObject(newInst, editEndJson); \
								PushCommand(std::make_unique<ComponentEditCommand<T>>(_selectedEntity, oldComp, newComp)); \
							}

						// 주요 컴포넌트 타입들 처리
						PUSH_COMPONENT_EDIT_CMD(MaterialComponent)
							else PUSH_COMPONENT_EDIT_CMD(SkinnedMeshComponent)
					else PUSH_COMPONENT_EDIT_CMD(SkinnedAnimationComponent)
				else PUSH_COMPONENT_EDIT_CMD(CameraComponent)
				else PUSH_COMPONENT_EDIT_CMD(CameraFollowComponent)
				else PUSH_COMPONENT_EDIT_CMD(CameraSpringArmComponent)
			else PUSH_COMPONENT_EDIT_CMD(CameraLookAtComponent)
			else PUSH_COMPONENT_EDIT_CMD(CameraShakeComponent)
						else PUSH_COMPONENT_EDIT_CMD(CameraBlendComponent)
						else PUSH_COMPONENT_EDIT_CMD(CameraInputComponent)
						else PUSH_COMPONENT_EDIT_CMD(PointLightComponent)
						else PUSH_COMPONENT_EDIT_CMD(SpotLightComponent)
						else PUSH_COMPONENT_EDIT_CMD(RectLightComponent)
						else PUSH_COMPONENT_EDIT_CMD(ComputeEffectComponent)
						else PUSH_COMPONENT_EDIT_CMD(Phy_RigidBodyComponent)
						else PUSH_COMPONENT_EDIT_CMD(Phy_ColliderComponent)
						else PUSH_COMPONENT_EDIT_CMD(Phy_MeshColliderComponent)
						else PUSH_COMPONENT_EDIT_CMD(Phy_CCTComponent)
						else PUSH_COMPONENT_EDIT_CMD(Phy_TerrainHeightFieldComponent)
						else PUSH_COMPONENT_EDIT_CMD(Phy_JointComponent)

#undef PUSH_COMPONENT_EDIT_CMD

							extern bool g_SceneDirty;
							g_SceneDirty = true;
					}

					lastEditedEntity = InvalidEntityId;
					lastEditedComponentType.clear();
				}

				if (event.changed) {
					extern bool g_SceneDirty;
					g_SceneDirty = true;
				}
			}
		}

		void DrawInspectorTransform(World& world, const EntityId& _selectedEntity);
		void DrawInspectorAnimationStatus(World& world, const EntityId& _selectedEntity);
		void DrawInspectorScripts(World& world, const EntityId& _selectedEntity);
		void DrawInspectorComputeEffect(World& world, const EntityId& _selectedEntity);
		void DrawInspectorUnityVfx(World& world, const EntityId& _selectedEntity);
		void DrawInspectorMaterial(World& world, const EntityId& _selectedEntity);
		void DrawInspectorDecal(World& world, const EntityId& _selectedEntity);
		void DrawInspectorPointLight(World& world, const EntityId& _selectedEntity);
		void DrawInspectorSpotLight(World& world, const EntityId& _selectedEntity);
		void DrawInspectorRectLight(World& world, const EntityId& _selectedEntity);
		void DrawInspectorPostProcessVolume(World& world, const EntityId& _selectedEntity);

		// Default Post Process Settings UI
		void SaveDefaultPostProcessSettings();
		void LoadDefaultPostProcessSettings();

		// Camera 컴포넌트 인스펙터
		void DrawInspectorCameraSpringArm(World& world, const EntityId& _selectedEntity);
		void DrawInspectorCameraLookAt(World& world, const EntityId& _selectedEntity);
		void DrawInspectorCameraFollow(World& world, const EntityId& _selectedEntity);
		void DrawInspectorCameraShake(World& world, const EntityId& _selectedEntity);
		void DrawInspectorCameraInput(World& world, const EntityId& _selectedEntity);
		void DrawInspectorCameraBlend(World& world, const EntityId& _selectedEntity);

		// 물리
		bool DrawLayerMaskEditor(const char* label, uint32_t& mask, const std::array<std::string, 32>& layerNames);
		bool DrawLayerMaskChipEditor(const char* label, uint32_t& mask, const std::array<std::string, 32>& layerNames);
		bool DrawIgnoreLayersChipEditor(const char* label, uint32_t& ignoreLayers, const std::array<std::string, 32>& layerNames);
		void DrawInspectorCollider(World& world, const EntityId& _selectedEntity);
		void DrawInspectorMeshCollider(World& world, const EntityId& _selectedEntity);
		void DrawInspectorCharacterController(World& world, const EntityId& _selectedEntity);
		void DrawInspectorPhysicsSceneSettings(World& world, const EntityId& _selectedEntity);
		void DrawInspectorTerrainHeightField(World& world, const EntityId& _selectedEntity);
		void DrawInspectorJoint(World& world, const EntityId& _selectedEntity);
		void DrawInspectorAttackDriver(World& world, const EntityId& _selectedEntity);
		void DrawInspectorHurtbox(World& world, const EntityId& _selectedEntity);
		void DrawInspectorWeaponTrace(World& world, const EntityId& _selectedEntity);
		void DrawInspectorSocketAttachment(World& world, const EntityId& _selectedEntity);
		void DrawInspectorSocketComponent(World& world, const EntityId& _selectedEntity);

		/// 프로젝트 뷰에서 사용할 간단한 디렉터리 트리 그리기 함수입니다.
		void DrawDirectoryNode(World& world,
			EntityId& selectedEntity,
			const std::filesystem::path& path);

		/// FBX 에셋을 월드에 인스턴스화합니다.
		EntityId InstantiateFbxAssetToWorld(World& world,
			const std::filesystem::path& fbxAssetPath,
			std::string_view entityName);

	public:
		void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }
		void SetInputSystem(InputSystem* inputSystem) { m_inputSystem = inputSystem; }
		void SetAliceUIRenderer(UIRenderer* renderer);

		/// Default PostProcess Settings를 가져옵니다 (PostProcessVolumeSystem에서 사용)
		const PostProcessSettings& GetDefaultPostProcessSettings() const { return m_defaultPostProcessSettings; }

	private:
		/// 씬을 로드한 뒤, World 에 존재하는 SkinnedMeshComponent 들이
		/// SkinnedMeshRegistry 에도 등록되어 있는지 확인하고,
		/// 누락된 경우 .fbxasset / FBX 원본을 통해 간단히 재-임포트합니다.
		void EnsureSkinnedMeshesRegistered(World& world);
		void SaveScene(World&);
		void LoadScene(World&);

		// DrawEditorUI 분해용 헬퍼
		void HandleGlobalUndoRedo(World& world, EntityId& selectedEntity, bool isPlaying);
		void SetupDockSpaceAndDefaultLayout();
		void DrawMainMenuBar(World& world,
			float deltaTime,
			float fps,
			bool& isPlaying,
			EntityId& selectedEntity,
			bool& useForwardRendering,
			bool& isDebugDraw);
		void DrawPvdSettingsWindow(bool& pvdEnabled, std::string& pvdHost, int& pvdPort);
		void DrawBuildGameWindow();
		void DrawHierarchyWindow(World& world, EntityId& selectedEntity);
		void DrawInspectorWindow(World& world, EntityId& selectedEntity);
		void DrawProjectWindow(World& world, EntityId& selectedEntity);
		void DrawGameViewportWindow(World& world,
			Camera& camera,
			ForwardRenderSystem& forward,
			DeferredRenderSystem& deferred,
			EntityId& selectedEntity,
			ViewportPicker& picker,
			float& cameraMoveSpeed,
			bool& useForwardRendering,
			bool& isPlaying,
			int& shadingMode,
			bool& useFillLight);
		void DrawCameraWindow(World& world,
			Camera& camera,
			ForwardRenderSystem& forward,
			DeferredRenderSystem& deferred,
			float& cameraMoveSpeed,
			EntityId& selectedEntity,
			bool& useForwardRendering);
		void DrawLightingWindow(World& world,
			ForwardRenderSystem& forward,
			DeferredRenderSystem& deferred,
			int& shadingMode,
			bool& useFillLight,
			bool& useForwardRendering,
			LightingParameters& lightingParams,
			int& skyboxChoice,
			std::string& skyboxCustomDir,
			std::string& skyboxCustomPrefix,
			int& skyboxResolution);
		void DrawMaterialAssetEditorWindow(World& world);
		void DrawUICurveAssetEditorWindow();
		void HandleSceneLoadFlow(World& world, SceneManager* sceneManager, bool& isPlaying, EntityId& selectedEntity);

		// UI
		EntityId CreateAliceUIRoot(World& world, std::string_view name);
		EntityId CreateAliceUIImage(World& world);
		EntityId CreateAliceUIText(World& world);
		EntityId CreateAliceUIButton(World& world);
		EntityId CreateAliceUIGauge(World& world);
		EntityId CreateAliceUIWorldImage(World& world);

		// Undo 시스템
		void PushCommand(std::unique_ptr<struct ICommand> cmd);

	private:
		bool               m_initialized = false;
		HWND               m_hwnd = nullptr;
		ID3D11RenderDevice* m_renderDevice = nullptr;
		SkinnedMeshRegistry* m_skinnedRegistry = nullptr;
		InputSystem* m_inputSystem = nullptr;
		UIRenderer* m_aliceUIRenderer = nullptr;

		bool               m_scriptBuilded = false;

		// Default PostProcess Settings (Inspector에서 설정하고 저장)
		PostProcessSettings m_defaultPostProcessSettings;
	};
}


