#pragma once

#include "Runtime/Engine/Engine.h"

#include "Runtime/Rendering/D3D11/D3D11RenderDevice.h"
#include "Runtime/Rendering/DebugDrawSystem.h"
#include "Runtime/Rendering/DebugDrawComponentSystem.h"
#include "Runtime/Rendering/EffectSystem.h"
#include "Runtime/Rendering/TrailEffectRenderSystem.h"
#include "Runtime/Rendering/UnityVfxMeshRenderSystem.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder API 사용
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Win32 메시지 헬퍼 (GET_X/Y_LPARAM)
#include <Windowsx.h>

// 표준 라이브러리
#include <filesystem>
#include <cfloat>      // FLT_MAX
#include <algorithm>   // std::max
#include <cmath>       // std::fabsf
#include <memory>
#include <fstream>
#include <sstream>
#include "ThirdParty/json/json.hpp"

// Core
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputSystem.h"
#include "Runtime/Engine/TimeSystem.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/Scene.h"
#include "Runtime/Scripting/ScriptSystem.h"
#include "Runtime/Foundation/Delegate.h"

#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"

#include "Runtime/UI/UIRenderer.h"
#include "Runtime/Rendering/ComputeEffectSystem.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Editor/Core/ViewportPicker.h"
#include "Editor/Core/EditorCore.h"
#include "Runtime/Gameplay/Animation/SkinnedMeshSystem.h"
#include "Runtime/Engine/AdvancedAnimSystem.h"
#include "Runtime/Gameplay/Animation/SkinnedAnimationSystem.h"
#include "Runtime/Gameplay/Sockets/SocketWorldUpdateSystem.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentSystem.h"
#include "Runtime/Gameplay/Combat/WeaponTraceSystem.h"
#include "Runtime/Gameplay/Combat/CombatHitEvent.h"
#include "Runtime/Gameplay/Combat/CombatSystem.h"
#include "Runtime/Gameplay/Combat/AttackDriverSystem.h"
#include "Runtime/Audio/AudioSystem.h"
#include "Runtime/Audio/SoundManager.h"

#include "Runtime/Physics/Module/PhysicsModule.h" // 물리 모듈
#include "Runtime/Physics/PhysicsSystem.h" // 물리 시스템

// 문자열 변환 / ImGui 래퍼
#include "Runtime/Foundation/StringUtils.h"
#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Foundation/ThreadSafety.h"
#include "Runtime/Engine/CameraSystem.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include <dxgi1_3.h>
#include <unordered_set>

#include "Runtime/Importing/FbxModel.h"



namespace Alice
{
	struct Engine::Impl
	{
		enum class ShadingMode
		{
			Lambert = 0,
			Phong = 1,
			BlinnPhong = 2,
			Toon = 3,
			PBR = 4,
			ToonPBR = 5,
			ToonPBREditable = 7
		};

		HINSTANCE m_hInstance = nullptr;
		HWND      m_hWnd = nullptr;

		std::uint32_t m_width = 1600;
		std::uint32_t m_height = 900;

		bool m_isRunning = false;            // 엔진 자체가 실행중인지 판단
		bool m_isPlaying = false;            // 재생 / 일시정지 상태 (에디터 모드에서만 사용)
		bool m_editorMode = true;             // true: 에디터, false: 게임 전용
		bool m_debugDraw = true;
		EntityId m_selectedEntity{ InvalidEntityId }; // 현재 선택된 엔티티 (하이러키)

		World          m_world;
		UIRenderer     m_aliceUIRenderer;
		Camera         m_camera;
		InputSystem    m_inputSystem;
		GameTimer      m_timer;
		ResourceManager m_resourceManager;
		std::unique_ptr<SceneManager> m_sceneManager;

		//===============================		
		PhysicsModule m_physics; // 물리 모듈
		std::unique_ptr<PhysicsSystem> m_physicsSystem; // 물리 시스템 (ECS 브릿지)

		float m_physAccum = 0.0f;
		float m_physFixedDt = 1.0f / 60.0f;
		int   m_physMaxSubsteps = 4;
		bool  m_skipPhysicsNextFrame = false;
		bool  m_prevIsPlaying = false;

		// 물리 이벤트 큐 (한 프레임 안전하게 처리하기 위함)
		std::vector<PhysicsEvent> m_physicsEventQueue;
		std::vector<CombatHitEvent> m_combatHitQueue;

		// PVD (PhysX Visual Debugger) 설정
		bool m_pvdEnabled = false;
		std::string m_pvdHost = "127.0.0.1";
		int m_pvdPort = 5425;
		//===============================

		ScriptSystem   m_scriptSystem;

		ViewportPicker m_viewportPicker;
		EditorCore     m_editorCore;

		ShadingMode m_shadingMode{ ShadingMode::PBR };
		bool        m_useFillLight{ true };
		LightingParameters m_savedLightingParameters{};
		int m_skyboxChoice = 3; // 0 Off, 1 Bridge, 2 Indoor, 3 Baker, 4 darkenv, 5 Custom
		std::string m_skyboxCustomDir;
		std::string m_skyboxCustomPrefix;
		int m_skyboxResolution = 0; // 0 HDR, 1 MDR

		// 카메라 이동/회전을 위한 내부 상태 값들
		DirectX::XMFLOAT3 m_cameraPosition{ 0.0f, 2.0f, -5.0f };
		float             m_cameraYawRadians = 0.0f;  // Yaw (좌우 회전)
		float             m_cameraPitchRadians = 0.0f;  // Pitch (상하 회전)

		float             m_cameraMoveSpeed = 8.0f;     // 초당 이동 속도
		float             m_cameraMouseSensitivity = 0.0025f; // 마우스 감도 (라디안/픽셀)

		std::unique_ptr<ID3D11RenderDevice>  m_renderDevice;
		std::unique_ptr<ForwardRenderSystem> m_forwardRenderSystem;
		std::unique_ptr<DeferredRenderSystem> m_deferredRenderSystem;
		std::unique_ptr<class DebugDrawSystem> m_debugDrawSystem;
		std::unique_ptr<class DebugDrawSystem> m_gizmoDrawSystem;
		DebugDrawComponentSystem m_debugDrawComponentSystem;
		std::unique_ptr<class EffectSystem> m_effectSystem;
		std::unique_ptr<class TrailEffectRenderSystem> m_trailRenderSystem;
		std::unique_ptr<class UnityVfxMeshRenderSystem> m_unityVfxMeshRenderSystem;
		std::unique_ptr<ComputeEffectSystem> m_computeEffectSystem;

		// 렌더링 모드 전환 (true: Forward, false: Deferred)
		bool m_useForwardRendering = false;

		// 렌더링 시스템 전환 지연 처리 (안전한 전환을 위해)
		bool m_pendingRenderSystemChange = false;
		bool m_pendingUseForwardRendering = true;

		CameraSystem m_cameraSystem;

		// Skinned FBX 메시 렌더링용 레지스트리/시스템
		SkinnedMeshRegistry m_skinnedMeshRegistry;
		SkinnedMeshSystem   m_skinnedMeshSystem{ m_skinnedMeshRegistry };
		AdvancedAnimSystem  m_advancedAnimSystem{ m_skinnedMeshRegistry };
		SkinnedAnimationSystem m_skinnedAnimSystem{ m_skinnedMeshRegistry };
		SocketWorldUpdateSystem m_socketWorldUpdateSystem{ m_skinnedMeshRegistry };
		SocketAttachmentSystem m_socketAttachmentSystem;
		WeaponTraceSystem m_weaponTraceSystem;
		CombatSystem m_combatSystem;
		AttackDriverSystem m_attackDriverSystem;
		AudioSystem m_audioSystem;
		std::vector<SkinnedDrawCommand> m_skinnedDrawCommands;

		bool m_animUpdatedThisFrame = false;

		// =========================
		// Initialize helpers
		bool InitializeAll(Engine& owner, HINSTANCE hInstance, int nCmdShow);
		void InitializeMainThreadAndRegistry();
		std::filesystem::path InitializeResolveExeDir();
		bool InitializeConfigureResourceManagers(const std::filesystem::path& exeDir);
		bool InitializeValidateGameDataIfNeeded();
		void InitializeLoadPvdSettings(const std::filesystem::path& exeDir);
		void InitializeLoadLightingSettings(const std::filesystem::path& exeDir);
		void SaveLightingSettings(const std::filesystem::path& exeDir);
		bool InitializePhysicsContext();
		bool InitializeWindowAndInput(Engine& owner, int nCmdShow);
		bool InitializeRenderDevice();
		bool InitializeEditorCoreIfNeeded();
		void InitializeAudio();
		bool InitializeRenderSystems();
		bool InitializeUI();
		bool InitializeComputeEffectSystem();
		void InitializeCameraAndScriptHotReload();
		bool InitializeScene(const std::filesystem::path& exeDir);
		bool InitializePhysicsSystemAndWorldCallbacks();
		void InitializePostLoadBindings(Engine& owner);
		void SavePvdSettings(const std::filesystem::path& exeDir);

		// =========================
		// Update helpers
		void UpdateFrame();
		void UpdateTimerAndInput(float& outDt);
		bool UpdateShouldUpdateFromScene() const;
		void UpdateSceneAndScript(float dt);
		bool UpdateCommitPendingSceneChanges(float dt);
		void UpdateAttackDriver();
		void UpdateEnsurePhysicsWorldIfNeeded();
		void UpdatePhysicsBridge(float dt);
		void UpdatePhysicsSim(float dt);
		void UpdateAnimationAndSockets(float dt);
		void UpdateCombat(float dt);
		void UpdateCameraSystems(float dt);
		void UpdateSyncPrimaryCameraFromWorld();
		void UpdateEditorFreeCam(float dt);
		void UpdateApplyFinalCameraLookAt();
		void UpdateUI(float dt);

		// =========================
		// Physics helpers
		void ClearWorldAndPhysics();
		void RefreshPhysicsForCurrentWorld();
		void TickPhysics(float dt);
		void ProcessPhysicsEvents();
		void ProcessCombatHits();

		// =========================
		// Render helpers
		void RenderFrame();
		void RenderUpdateWorldTransformCache();
		void RenderHandlePendingRenderSystemChange();
		bool RenderValidateRenderSystems() const;
		void RenderBeginFrame();
		void RenderEditorUI();
		void RenderEditorDebugBuild();
		void RenderEnsureAnimationIfNotUpdated();
		void RenderBuildSkinnedDrawList();
		void RenderOnDemandSkinnedMeshLoading();
		void RenderAudioUpdate();
		void RenderMainPass();
		void RenderCameraPreview();
		void RenderUnbindDepthOnly();
		void RenderComputeEffects();
		void RenderParticleOverlayComposite();
		void RenderDebugOverlayComposite();
		void RenderGameModeToneMappingAndUI();
		void RenderOverlayEffects();
		void RenderEditorDraw();
		void RenderEndFrame();
		void EnsureSkinnedMeshesRegisteredForWorld();
		void TrimVideoMemory();
		void SetUseForwardRendering(bool useForward);
		bool GetUseForwardRendering() const;
		void UpdateIblForScene();

		// =========================
		// Window helpers
		bool CreateMainWindow(Engine& owner, int nCmdShow);
		void OnResize(std::uint32_t width, std::uint32_t height);
		LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	};
}
