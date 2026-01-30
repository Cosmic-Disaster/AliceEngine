#include "Engine/EngineImpl.h"

namespace Alice
{
	void Engine::Impl::UpdateFrame()
	{
		float dt = 0.0f;
		UpdateTimerAndInput(dt);

		const bool updateFromScene = UpdateShouldUpdateFromScene();
		bool sceneChangedThisFrame = false;

		if (updateFromScene)
		{
			UpdateSceneAndScript(dt);
			sceneChangedThisFrame = UpdateCommitPendingSceneChanges(dt);

			if (!sceneChangedThisFrame)
			{
				UpdateAttackDriver();

				UpdateEnsurePhysicsWorldIfNeeded();
				UpdatePhysicsBridge(dt);
				UpdatePhysicsSim(dt);

				UpdateAnimationAndSockets(dt);
				UpdateCombat(dt);

				UpdateCameraSystems(dt);
				UpdateSyncPrimaryCameraFromWorld();
			}
		}
		else
		{
			UpdateEditorFreeCam(dt);
		}

		UpdateApplyFinalCameraLookAt();
		UpdateUI(dt);
	}

	// =========================
	// Update helpers
	void Engine::Impl::UpdateTimerAndInput(float& outDt)
	{
		m_timer.Tick();
		outDt = m_timer.DeltaTime();
		m_inputSystem.Update(outDt);
		m_animUpdatedThisFrame = false;
	}

	bool Engine::Impl::UpdateShouldUpdateFromScene() const
	{
		return (!m_editorMode || m_isPlaying);
	}

	void Engine::Impl::UpdateSceneAndScript(float dt)
	{
		if (m_sceneManager) m_sceneManager->Update(dt);
		m_scriptSystem.Tick(m_world, dt);
	}

	bool Engine::Impl::UpdateCommitPendingSceneChanges(float /*dt*/)
	{
		bool sceneChangedThisFrame = false;

		if (m_scriptSystem.HasPendingSceneRequests() ||
			(m_sceneManager && m_sceneManager->HasPendingSceneChange()))
		{
			if (m_physicsSystem)
				m_physicsSystem->SetPhysicsWorld(nullptr);

			if (auto pwShared = m_world.GetPhysicsWorldShared())
				pwShared->Flush();

			m_physAccum = 0.0f;
			m_physicsEventQueue.clear();

			if (m_scriptSystem.HasPendingSceneRequests())
				m_scriptSystem.CommitSceneRequests(m_world, &m_uiWorld);

			if (m_sceneManager && m_sceneManager->HasPendingSceneChange())
				m_sceneManager->CommitPendingSceneChange(m_world, &m_uiWorld);

			sceneChangedThisFrame = true;
		}

		return sceneChangedThisFrame;
	}

	void Engine::Impl::UpdateAttackDriver()
	{
		m_attackDriverSystem.Update(m_world);
	}

	void Engine::Impl::UpdateEnsurePhysicsWorldIfNeeded()
	{
		if (m_physicsSystem && !m_world.GetPhysicsWorld())
		{
			const auto& settingsMap = m_world.GetComponents<Phy_SettingsComponent>();
			if (!settingsMap.empty())
			{
				const auto& settings = settingsMap.begin()->second;
				if (settings.enablePhysics)
					RefreshPhysicsForCurrentWorld();
			}
		}
	}

	void Engine::Impl::UpdatePhysicsBridge(float dt)
	{
		if (m_physicsSystem)
			m_physicsSystem->Update(dt);
	}

	void Engine::Impl::UpdatePhysicsSim(float dt)
	{
		TickPhysics(dt);
	}

	void Engine::Impl::UpdateAnimationAndSockets(float dt)
	{
		m_advancedAnimSystem.Update(m_world, static_cast<double>(dt));
		m_skinnedAnimSystem.Update(m_world, static_cast<double>(dt));
		m_socketWorldUpdateSystem.Update(m_world);
		m_socketAttachmentSystem.Update(m_world);
		m_animUpdatedThisFrame = true;

		m_weaponTraceSystem.Update(m_world, dt, &m_combatHitQueue);
	}

	void Engine::Impl::UpdateCombat(float dt)
	{
		ProcessPhysicsEvents();
		ProcessCombatHits();
		m_combatSystem.Update(m_world, dt);
	}

	void Engine::Impl::UpdateCameraSystems(float dt)
	{
		m_cameraSystem.Update(m_world, m_inputSystem, dt);
	}

	void Engine::Impl::UpdateSyncPrimaryCameraFromWorld()
	{
		EntityId camId = InvalidEntityId;
		for (const auto& [id, cam] : m_world.GetComponents<CameraComponent>())
		{
			if (cam.GetPrimary()) { camId = id; break; }
			if (camId == InvalidEntityId) camId = id;
		}

		if (camId == InvalidEntityId) return;

		auto* camComp = m_world.GetComponent<CameraComponent>(camId);
		if (!camComp) return;

		const Camera& sourceCamera = camComp->GetCamera();

		const float defaultAspect = static_cast<float>(m_width) / m_height;
		const float aspect = (camComp->useAspectOverride && camComp->aspectOverride > 0.0f)
			? camComp->aspectOverride : defaultAspect;

		m_camera.SetPerspective(
			sourceCamera.GetFovYRadians(), aspect,
			sourceCamera.GetNearPlane(), sourceCamera.GetFarPlane());

		m_camera.SetPosition(sourceCamera.GetPosition());
		m_camera.SetRotation(sourceCamera.GetRotationQuat());
		m_camera.SetScale(sourceCamera.GetScale());

		m_cameraPosition = sourceCamera.GetPosition();
		const DirectX::XMFLOAT3 rot = sourceCamera.GetRotation();
		m_cameraYawRadians = rot.y;
		m_cameraPitchRadians = rot.x;
	}

	void Engine::Impl::UpdateEditorFreeCam(float dt)
	{
		using namespace DirectX;

		if (!m_inputSystem.IsRightButtonDown())
			return;

		XMVECTOR moveDir = XMVectorZero();
		auto& input = m_inputSystem;

		if (input.IsKeyDown(Keyboard::W)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 0, 1, 0));
		if (input.IsKeyDown(Keyboard::S)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 0, -1, 0));
		if (input.IsKeyDown(Keyboard::D)) moveDir = XMVectorAdd(moveDir, XMVectorSet(1, 0, 0, 0));
		if (input.IsKeyDown(Keyboard::A)) moveDir = XMVectorAdd(moveDir, XMVectorSet(-1, 0, 0, 0));
		if (input.IsKeyDown(Keyboard::E)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 1, 0, 0));
		if (input.IsKeyDown(Keyboard::Q)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, -1, 0, 0));

		if (!XMVector3Equal(moveDir, XMVectorZero()))
		{
			const XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0);
			const XMVECTOR worldDir = XMVector3Normalize(XMVector3TransformNormal(moveDir, rotMat));
			const XMVECTOR currentPos = XMLoadFloat3(&m_cameraPosition);

			XMStoreFloat3(&m_cameraPosition,
				XMVectorAdd(currentPos, XMVectorScale(worldDir, m_cameraMoveSpeed * dt)));
		}

		const POINT mouseDelta = input.GetMouseDelta();
		m_cameraYawRadians += mouseDelta.x * m_cameraMouseSensitivity;
		m_cameraPitchRadians += mouseDelta.y * m_cameraMouseSensitivity;
	}

	void Engine::Impl::UpdateApplyFinalCameraLookAt()
	{
		using namespace DirectX;

		const float pitchLimit = XMConvertToRadians(89.0f);
		m_cameraPitchRadians = std::clamp(m_cameraPitchRadians, -pitchLimit, pitchLimit);

		const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0);
		const XMVECTOR camForward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), camRot);
		const XMVECTOR camPos = XMLoadFloat3(&m_cameraPosition);

		XMFLOAT3 targetPos;
		XMStoreFloat3(&targetPos, XMVectorAdd(camPos, camForward));

		m_camera.SetLookAt(m_cameraPosition, targetPos, XMFLOAT3(0, 1, 0));
	}

	void Engine::Impl::UpdateUI(float /*dt*/)
	{
		//m_uiWorld.Update(m_width, m_height);
		m_aliceUIRenderer.Update(m_world, m_inputSystem, m_camera,
			static_cast<float>(m_width), static_cast<float>(m_height), m_timer.DeltaTime());
	}

	//=========================================================
	// 물리 시스템
}
