#include "Runtime/Engine/EngineImpl.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"

namespace Alice
{
	namespace
	{
		inline Alice::EntityId DecodeEntityId(void* p)
		{
			return static_cast<Alice::EntityId>(reinterpret_cast<std::uintptr_t>(p));
		}

		// 축 맞는지 확인해야함, 아니면 조율해줘야함
		inline DirectX::XMFLOAT3 QuatToEulerXYZ(const Quat& qIn)
		{
			// normalize
			Quat q = qIn;
			const float len2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (len2 > 0.0f)
			{
				const float inv = 1.0f / std::sqrt(len2);
				q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
			}

			// Tait–Bryan angles (X=pitch, Y=yaw, Z=roll) 근사
			const float sinp = 2.0f * (q.w * q.x + q.y * q.z);
			const float cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
			const float pitch = std::atan2(sinp, cosp);

			float siny = 2.0f * (q.w * q.y - q.z * q.x);
			siny = std::clamp(siny, -1.0f, 1.0f);
			const float yaw = std::asin(siny);

			const float sinr = 2.0f * (q.w * q.z + q.x * q.y);
			const float cosr = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
			const float roll = std::atan2(sinr, cosr);

			return { pitch, yaw, roll };
		}
	}

	void Engine::Impl::RenderFrame()
	{
		if (!m_renderDevice) return;

		RenderUpdateWorldTransformCache();
		RenderHandlePendingRenderSystemChange();

		if (!RenderValidateRenderSystems()) return;

		RenderBeginFrame();

		if (m_editorMode)
		{
			RenderEditorUI();
			RenderEditorDebugBuild();
		}

		RenderEnsureAnimationIfNotUpdated();
		RenderBuildSkinnedDrawList();
		RenderOnDemandSkinnedMeshLoading();

		// Update에서 애니메이션이 안 돈 프레임을 Render에서 보정하므로, 오디오 위치도 그 이후가 안전
		RenderAudioUpdate();

		RenderMainPass();
		RenderCameraPreview();
		RenderUnbindDepthOnly();

		RenderComputeEffects();
		RenderParticleOverlayComposite();
		RenderDebugOverlayComposite();
		RenderGameModeToneMappingAndUI();
		RenderOverlayEffects();
		if (m_editorMode)
			RenderEditorDraw();

		RenderEndFrame();
	}

	// =========================
	// Render helpers
	void Engine::Impl::RenderUpdateWorldTransformCache()
	{
		m_world.UpdateTransformMatrices();
	}

	void Engine::Impl::RenderHandlePendingRenderSystemChange()
	{
		if (!m_pendingRenderSystemChange) return;

		auto* context = m_renderDevice->GetImmediateContext();
		if (context)
		{
			ID3D11RenderTargetView* nullRTVs[8] = { nullptr };
			context->OMSetRenderTargets(8, nullRTVs, nullptr);

			ID3D11ShaderResourceView* nullSRVs[16] = { nullptr };
			context->VSSetShaderResources(0, 16, nullSRVs);
			context->PSSetShaderResources(0, 16, nullSRVs);

			ID3D11Buffer* nullCBs[16] = { nullptr };
			context->VSSetConstantBuffers(0, 16, nullCBs);
			context->PSSetConstantBuffers(0, 16, nullCBs);

			context->VSSetShader(nullptr, nullptr, 0);
			context->PSSetShader(nullptr, nullptr, 0);
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->CSSetShader(nullptr, nullptr, 0);

			context->Flush();
		}

		m_useForwardRendering = m_pendingUseForwardRendering;
		m_pendingRenderSystemChange = false;

		ALICE_LOG_INFO("Engine::Render: 렌더링 시스템 전환 완료 (Forward: %s)",
			m_useForwardRendering ? "true" : "false");
	}

	bool Engine::Impl::RenderValidateRenderSystems() const
	{
		if (m_useForwardRendering && !m_forwardRenderSystem) return false;
		if (!m_useForwardRendering && !m_deferredRenderSystem) return false;
		return true;
	}

	void Engine::Impl::RenderBeginFrame()
	{
		float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
		m_renderDevice->BeginFrame(clearColor);
	}

	void Engine::Impl::RenderEditorUI()
	{
		if (!m_editorMode) return;

		m_editorCore.BeginFrame();

		int shadingMode = static_cast<int>(m_shadingMode);
		m_editorCore.DrawEditorUI(
			m_world, m_camera, *m_forwardRenderSystem, *m_deferredRenderSystem, m_sceneManager.get(),
			m_timer.DeltaTime(), (m_timer.DeltaTime() > 0) ? (1.0f / m_timer.DeltaTime()) : 0.0f,
			m_isPlaying, shadingMode, m_useFillLight,
			m_selectedEntity, m_viewportPicker, m_cameraMoveSpeed,
			m_useForwardRendering,
			m_savedLightingParameters,
			m_skyboxChoice,
			m_skyboxCustomDir,
			m_skyboxCustomPrefix,
			m_skyboxResolution,
			m_pvdEnabled, m_pvdHost, m_pvdPort,
			m_debugDraw
		);

		if (static_cast<Impl::ShadingMode>(shadingMode) != m_shadingMode)
		{
			m_shadingMode = static_cast<Impl::ShadingMode>(shadingMode);
		}
	}

	void Engine::Impl::RenderEditorDebugBuild()
	{
		if (!m_editorMode) return;

		DebugDrawSystem* gizmo = m_gizmoDrawSystem.get();
		DebugDrawSystem* dbg = m_debugDrawSystem.get();
		if (gizmo) gizmo->Clear();
		if (dbg) dbg->Clear();

		// 디버그 축(XYZ) + 그리드 (깊이 테스트용)
		if (gizmo && m_debugDraw)
		{
			const float axisLen = 300.0f;
			const float axisRadius = 0.08f;

			// X축: 약간 다홍빛이 도는 레드 (순수 빨강보다 세련됨)
			const DirectX::XMFLOAT4 colorX = { 0.9f, 0.2f, 0.2f, 1.0f };
			// Y축: 형광 연두색 느낌을 약간 섞은 그린 (가시성 확보)
			const DirectX::XMFLOAT4 colorY = { 0.2f, 0.8f, 0.2f, 1.0f };
			// Z축: 깊이감 있는 스카이 블루/아주르 블루
			const DirectX::XMFLOAT4 colorZ = { 0.2f, 0.4f, 0.9f, 1.0f };

			gizmo->AddCylinder({ -axisLen, 0.f, 0.f }, { axisLen, 0.f, 0.f }, axisRadius, colorX); // X
			gizmo->AddCylinder({ 0.f, -axisLen, 0.f }, { 0.f, axisLen, 0.f }, axisRadius, colorY); // Y
			gizmo->AddCylinder({ 0.f, 0.0f, -axisLen }, { 0.f, 0.f, axisLen }, axisRadius, colorZ); // Z

			// 에디터 격자 (XZ 평면) - 카메라 높이에 따라 셀 크기를 키워 멀어질수록 합쳐 보이게 처리
			auto AddGridXZ = [&](float baseStep, int halfLines, float y,
				const DirectX::XMFLOAT4& minorCol, const DirectX::XMFLOAT4& majorCol)
			{
				const DirectX::XMFLOAT3 camPos = m_camera.GetPosition();
				float step = baseStep;
				const float height = std::fabsf(camPos.y);

				// 높이에 따라 셀 크기를 키움 (멀수록 덜 촘촘하게)
				while (height > step * 5.0f && step < baseStep * 128.0f)
				{
					step *= 2.0f;
				}

				const int majorEvery = 5;
				const float extent = step * static_cast<float>(halfLines);

				const float centerX = std::round(camPos.x / step) * step;
				const float centerZ = std::round(camPos.z / step) * step;

				for (int i = -halfLines; i <= halfLines; ++i)
				{
					const float x = centerX + static_cast<float>(i) * step;
					const float z = centerZ + static_cast<float>(i) * step;
					const DirectX::XMFLOAT4 col = (i % majorEvery == 0) ? majorCol : minorCol;

					gizmo->AddLine({ x, y, centerZ - extent }, { x, y, centerZ + extent }, col);
					gizmo->AddLine({ centerX - extent, y, z }, { centerX + extent, y, z }, col);
				}
			};

			AddGridXZ(
				1.0f, 20, 0.0f,
				{ 0.25f, 0.25f, 0.25f, 1.0f },
				{ 0.35f, 0.35f, 0.35f, 1.0f }
			);
		}

		m_debugDrawComponentSystem.Build(
			m_world,
			dbg,
			gizmo,
			m_selectedEntity,
			m_debugDraw,
			true
		);

		// 나머지 디버그 요소 (항상 보이도록 오버레이)
		if (dbg && m_debugDraw)
		{
			// === FBX/SkinnedMesh 디버그 AABB 박스 ===
			// - SkinnedMeshRegistry의 sourceModel(FbxModel)에서 로컬 AABB를 얻어,
			//   엔티티 Transform(S*R*T)을 적용한 OBB(로컬 AABB의 월드 변환)를 라인으로 표시합니다.
			auto AddBoxLines = [&](const DirectX::XMFLOAT3 corners[8], const DirectX::XMFLOAT4& col)
			{
				// bottom
				dbg->AddLine(corners[0], corners[1], col);
				dbg->AddLine(corners[1], corners[2], col);
				dbg->AddLine(corners[2], corners[3], col);
				dbg->AddLine(corners[3], corners[0], col);
				// top
				dbg->AddLine(corners[4], corners[5], col);
				dbg->AddLine(corners[5], corners[6], col);
				dbg->AddLine(corners[6], corners[7], col);
				dbg->AddLine(corners[7], corners[4], col);
				// sides
				dbg->AddLine(corners[0], corners[4], col);
				dbg->AddLine(corners[1], corners[5], col);
				dbg->AddLine(corners[2], corners[6], col);
				dbg->AddLine(corners[3], corners[7], col);
			};

			for (const auto& [entityId, skinned] : m_world.GetComponents<SkinnedMeshComponent>())
			{
				if (skinned.meshAssetPath.empty())
					continue;

				const auto* t = m_world.GetComponent<TransformComponent>(entityId);
				if (!t)
					continue;

				auto mesh = m_skinnedMeshRegistry.Find(skinned.meshAssetPath);
				if (!mesh || !mesh->sourceModel)
					continue;

				DirectX::XMFLOAT3 mn{}, mx{};
				if (!mesh->sourceModel->GetLocalBounds(mn, mx))
					continue;

				// 로컬 AABB 8 코너
				DirectX::XMFLOAT3 local[8] = {
					{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
					{mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}
				};

				// 월드 행렬 (렌더러/피커와 동일: S*R*T)
				using namespace DirectX;
				const XMVECTOR S = XMLoadFloat3(&t->scale);
				const XMVECTOR R = XMLoadFloat3(&t->rotation);
				const XMVECTOR T = XMLoadFloat3(&t->position);
				const XMMATRIX worldM =
					XMMatrixScalingFromVector(S) *
					XMMatrixRotationRollPitchYawFromVector(R) *
					XMMatrixTranslationFromVector(T);

				// 월드 코너로 변환
				DirectX::XMFLOAT3 worldCorners[8]{};
				for (int i = 0; i < 8; ++i)
				{
					const XMVECTOR p = XMVectorSet(local[i].x, local[i].y, local[i].z, 1.0f);
					const XMVECTOR pw = XMVector3TransformCoord(p, worldM);
					XMStoreFloat3(&worldCorners[i], pw);
				}

				// 선택된 엔티티는 빨강, 나머지는 노랑
				const DirectX::XMFLOAT4 col = (entityId == m_selectedEntity)
					? DirectX::XMFLOAT4(1.f, 0.f, 0.f, 1.f)
					: DirectX::XMFLOAT4(1.f, 1.f, 0.f, 1.f);

				AddBoxLines(worldCorners, col);
			}

			// AudioSource: 감쇠 반경 시각화
			auto DrawRing = [&](const DirectX::XMFLOAT3& center, float radius, const DirectX::XMFLOAT3& axisX, const DirectX::XMFLOAT3& axisZ, const DirectX::XMFLOAT4& color)
			{
				const int segments = 24;
				const float step = DirectX::XM_2PI / segments;

				DirectX::XMFLOAT3 prev;
				// 초기점: center + axisX * radius
				{
					using namespace DirectX;
					XMVECTOR c = XMLoadFloat3(&center);
					XMVECTOR ax = XMLoadFloat3(&axisX);
					XMVECTOR p = c + ax * radius;
					XMStoreFloat3(&prev, p);
				}

				for (int i = 1; i <= segments; ++i)
				{
					float angle = step * i;
					float c = cosf(angle);
					float s = sinf(angle);

					using namespace DirectX;
					XMVECTOR cent = XMLoadFloat3(&center);
					XMVECTOR ax = XMLoadFloat3(&axisX);
					XMVECTOR az = XMLoadFloat3(&axisZ);

					XMVECTOR currVec = cent + (ax * c * radius) + (az * s * radius);
					DirectX::XMFLOAT3 curr;
					XMStoreFloat3(&curr, currVec);

					dbg->AddLine(prev, curr, color);
					prev = curr;
				}
			};

			for (const auto& [entityId, src] : m_world.GetComponents<AudioSourceComponent>())
			{
				if (!src.is3D) continue;
				if (entityId != m_selectedEntity && !src.debugDraw) continue;

				const auto* t = m_world.GetComponent<TransformComponent>(entityId);
				if (!t) continue;

				// Min Distance (Green)
				DrawRing(t->position, src.minDistance, { 1,0,0 }, { 0,0,1 }, { 0,1,0,1 }); // XZ plane
				DrawRing(t->position, src.minDistance, { 0,1,0 }, { 1,0,0 }, { 0,1,0,1 }); // YX plane

				// Max Distance (Red)
				DrawRing(t->position, src.maxDistance, { 1,0,0 }, { 0,0,1 }, { 1,0,0,1 }); // XZ plane
				DrawRing(t->position, src.maxDistance, { 0,1,0 }, { 1,0,0 }, { 1,0,0,1 }); // YX plane
			}
		}
	}

	void Engine::Impl::RenderEnsureAnimationIfNotUpdated()
	{
		if (m_animUpdatedThisFrame) return;

		const double dtSec = static_cast<double>(m_timer.DeltaTime());
		m_attackDriverSystem.PreUpdate(m_world);
		m_advancedAnimSystem.Update(m_world, dtSec);
		m_skinnedAnimSystem.Update(m_world, dtSec);
		m_attackDriverSystem.PostUpdate(m_world);
		m_socketWorldUpdateSystem.Update(m_world);
		m_socketAttachmentSystem.Update(m_world);
		m_animUpdatedThisFrame = true;
	}

	void Engine::Impl::RenderBuildSkinnedDrawList()
	{
		m_skinnedMeshSystem.BuildDrawList(m_world, m_skinnedDrawCommands);
	}

	void Engine::Impl::RenderOnDemandSkinnedMeshLoading()
	{
		FbxImporter importer(m_resourceManager, &m_skinnedMeshRegistry);
		auto* device = m_renderDevice ? m_renderDevice->GetDevice() : nullptr;
		if (!device) return;

		const auto& skinnedMap = m_world.GetComponents<SkinnedMeshComponent>();
		for (const auto& [entityId, comp] : skinnedMap)
		{
			if (comp.meshAssetPath.empty()) continue;

			if (!m_skinnedMeshRegistry.Has(comp.meshAssetPath) && !comp.instanceAssetPath.empty())
			{
				ALICE_LOG_INFO("[Engine] On-demand loading mesh: meshKey=\"%s\" instanceAssetPath=\"%s\"",
					comp.meshAssetPath.c_str(), comp.instanceAssetPath.c_str());

				m_skinnedMeshRegistry.LoadFromFbxAsset(
					comp.meshAssetPath, comp.instanceAssetPath,
					m_resourceManager, importer, device
				);
			}
		}
	}

	void Engine::Impl::RenderAudioUpdate()
	{
		m_audioSystem.Update(m_world, static_cast<double>(m_timer.DeltaTime()));
	}

	void Engine::Impl::RenderMainPass()
	{
		EntityId renderEntity = (m_sceneManager) ? m_sceneManager->GetPrimaryRenderableEntity() : InvalidEntityId;

		std::unordered_set<EntityId> cameraIDs;
		for (const auto& [id, _] : m_world.GetComponents<CameraComponent>())
			cameraIDs.insert(id);

		const int finalShadingMode = m_editorMode
			? static_cast<int>(m_shadingMode)
			: static_cast<int>(Impl::ShadingMode::PBR);

		if (m_useForwardRendering)
		{
			m_forwardRenderSystem->Render(
				m_world, m_camera, renderEntity, cameraIDs,
				finalShadingMode, m_useFillLight, m_skinnedDrawCommands
			);
		}
		else
		{
			m_deferredRenderSystem->Render(
				m_world, m_camera, renderEntity, cameraIDs,
				finalShadingMode, m_useFillLight, m_skinnedDrawCommands,
				m_editorMode, m_isPlaying
			);
		}
	}

	void Engine::Impl::RenderCameraPreview()
	{
		if (!m_editorMode) return;
		if (m_selectedEntity == InvalidEntityId) return;

		auto* camComp = m_world.GetComponent<CameraComponent>(m_selectedEntity);
		if (!camComp) return;

		Camera previewCam = camComp->GetCamera();
		if (auto* camTr = m_world.GetComponent<TransformComponent>(m_selectedEntity))
		{
			previewCam.SetPosition(camTr->position);

			DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(
				camTr->rotation.x, camTr->rotation.y, camTr->rotation.z);
			DirectX::XMFLOAT4 quat{};
			DirectX::XMStoreFloat4(&quat, q);
			previewCam.SetRotation(quat);

			previewCam.SetScale(camTr->scale);
		}

		std::unordered_set<EntityId> cameraIDs;
		for (const auto& [id, _] : m_world.GetComponents<CameraComponent>())
			cameraIDs.insert(id);

		const int finalShadingMode = m_editorMode
			? static_cast<int>(m_shadingMode)
			: static_cast<int>(Impl::ShadingMode::PBR);

		if (m_deferredRenderSystem)
		{
			const float previewAspect = (camComp->useAspectOverride && camComp->aspectOverride > 0.0f)
				? camComp->aspectOverride
				: m_deferredRenderSystem->GetCameraPreviewAspect();

			previewCam.SetPerspective(
				previewCam.GetFovYRadians(),
				previewAspect,
				previewCam.GetNearPlane(),
				previewCam.GetFarPlane()
			);

            m_deferredRenderSystem->RenderCameraPreview(
                m_world,
                previewCam,
                cameraIDs,
                finalShadingMode,
                m_useFillLight,
                m_skinnedDrawCommands,
                m_editorMode,
                m_isPlaying,
                m_selectedEntity
            );
        }
        else if (m_forwardRenderSystem)
        {
			const float previewAspect = (camComp->useAspectOverride && camComp->aspectOverride > 0.0f)
				? camComp->aspectOverride
				: m_forwardRenderSystem->GetCameraPreviewAspect();

			previewCam.SetPerspective(
				previewCam.GetFovYRadians(),
				previewAspect,
				previewCam.GetNearPlane(),
				previewCam.GetFarPlane()
			);

            m_forwardRenderSystem->RenderCameraPreview(
                m_world,
                previewCam,
                cameraIDs,
                finalShadingMode,
                m_useFillLight,
                m_skinnedDrawCommands,
                m_selectedEntity
            );
        }
	}

	void Engine::Impl::RenderUnbindDepthOnly()
	{
		ID3D11RenderTargetView* currentRTV = nullptr;
		ID3D11DepthStencilView* currentDSV = nullptr;

		auto* ctx = m_renderDevice->GetImmediateContext();
		ctx->OMGetRenderTargets(1, &currentRTV, &currentDSV);

		if (currentRTV)
		{
			ctx->OMSetRenderTargets(1, &currentRTV, nullptr);
			currentRTV->Release();
		}
		if (currentDSV)
			currentDSV->Release();
	}

	void Engine::Impl::RenderComputeEffects()
	{
		if (m_computeEffectSystem &&
			((m_useForwardRendering && m_forwardRenderSystem) ||
				(!m_useForwardRendering && m_deferredRenderSystem)))
		{
			DirectX::XMMATRIX viewProj = DirectX::XMMatrixIdentity();
			DirectX::XMFLOAT3 cameraPos(0.0f, 0.0f, -5.0f);

			if (m_useForwardRendering && m_forwardRenderSystem)
			{
				viewProj = m_forwardRenderSystem->GetLastViewProj();
				cameraPos = m_forwardRenderSystem->GetLastCameraPos();
			}
			else if (!m_useForwardRendering && m_deferredRenderSystem)
			{
				viewProj = m_deferredRenderSystem->GetLastViewProj();
				cameraPos = m_deferredRenderSystem->GetLastCameraPos();
			}

			ID3D11ShaderResourceView* depthSRV = nullptr;
			if (m_useForwardRendering && m_forwardRenderSystem)
			{
				depthSRV = m_forwardRenderSystem->GetSceneDepthSRV();
			}
			else if (!m_useForwardRendering && m_deferredRenderSystem)
			{
				depthSRV = m_deferredRenderSystem->GetSceneDepthSRV();
			}

			float dtSec = m_timer.DeltaTime();
			float nearPlane = m_camera.GetNearPlane();
			float farPlane = m_camera.GetFarPlane();
			m_computeEffectSystem->Execute(m_world, viewProj, cameraPos, depthSRV, nearPlane, farPlane, dtSec);
		}
	}

	void Engine::Impl::RenderParticleOverlayComposite()
	{
		// 에디터 모드: 뷰포트 렌더 타겟에 파티클 오버레이 합성
		if (m_editorMode && m_computeEffectSystem && m_computeEffectSystem->HasActiveEffect())
		{
			ID3D11ShaderResourceView* particleSRV = m_computeEffectSystem->GetOutputSRV();
			if (particleSRV)
			{
				if (m_useForwardRendering && m_forwardRenderSystem)
				{
					m_forwardRenderSystem->RenderParticleOverlayToViewport(particleSRV);
				}
				else if (!m_useForwardRendering && m_deferredRenderSystem)
				{
					m_deferredRenderSystem->RenderParticleOverlayToViewport(particleSRV);
				}
			}
		}
	}

	void Engine::Impl::RenderDebugOverlayComposite()
	{
		if (!m_editorMode) return;

		auto RenderDebugOverlay = [&](DebugDrawSystem* system, bool depthTest)
		{
			if (!system) return;
			if (m_useForwardRendering && m_forwardRenderSystem)
			{
				m_forwardRenderSystem->RenderDebugOverlayToViewport(*system, m_camera, depthTest);
			}
			else if (!m_useForwardRendering && m_deferredRenderSystem)
			{
				m_deferredRenderSystem->RenderDebugOverlayToViewport(*system, m_camera, depthTest);
			}
		};

		RenderDebugOverlay(m_gizmoDrawSystem.get(), true);
		RenderDebugOverlay(m_debugDrawSystem.get(), false);
	}

	void Engine::Impl::RenderGameModeToneMappingAndUI()
	{
		if (m_editorMode) return;

		ID3D11RenderTargetView* backBufferRTV = m_renderDevice->GetBackBufferRTV();
		if (!backBufferRTV) return;

		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(m_width);
		viewport.Height = static_cast<float>(m_height);
		viewport.MaxDepth = 1.0f;

		if (m_useForwardRendering)
		{
			m_forwardRenderSystem->RenderToneMapping(backBufferRTV, viewport);
		}
		else
		{
			DeferredRenderSystem* deferred = m_deferredRenderSystem.get();
			deferred->RenderPostProcess(backBufferRTV, viewport);
		}

		// 파티클 오버레이 합성 (게임 모드)
		if (m_computeEffectSystem && m_computeEffectSystem->HasActiveEffect())
		{
			ID3D11ShaderResourceView* particleSRV = m_computeEffectSystem->GetOutputSRV();
			if (particleSRV)
			{
				if (m_useForwardRendering && m_forwardRenderSystem)
				{
					m_forwardRenderSystem->RenderParticleOverlay(particleSRV, backBufferRTV, viewport);
				}
				else if (!m_useForwardRendering && m_deferredRenderSystem)
				{
					m_deferredRenderSystem->RenderParticleOverlay(particleSRV, backBufferRTV, viewport);
				}
			}
		}

		m_aliceUIRenderer.RenderScreen(m_world, m_camera, backBufferRTV, viewport.Width, viewport.Height);
	}

	void Engine::Impl::RenderOverlayEffects()
	{
		ID3D11RenderTargetView* overlayRTV = nullptr;
		ID3D11DepthStencilView* overlayDSV = nullptr;

		if (m_editorMode)
		{
			if (m_useForwardRendering && m_forwardRenderSystem)
			{
				overlayRTV = m_forwardRenderSystem->GetViewportRTV();
				overlayDSV = nullptr;
			}
			else if (!m_useForwardRendering && m_deferredRenderSystem)
			{
				overlayRTV = m_deferredRenderSystem->GetViewportRTV();
				overlayDSV = nullptr;
			}
		}
		else
		{
			overlayRTV = m_renderDevice ? m_renderDevice->GetBackBufferRTV() : nullptr;
			overlayDSV = nullptr;
		}

		if (overlayRTV)
		{
			D3D11_VIEWPORT vp = {};
			if (m_editorMode)
			{
				if (m_useForwardRendering && m_forwardRenderSystem)
				{
					vp.Width = static_cast<float>(m_forwardRenderSystem->GetSceneWidth());
					vp.Height = static_cast<float>(m_forwardRenderSystem->GetSceneHeight());
				}
				else if (!m_useForwardRendering && m_deferredRenderSystem)
				{
					vp.Width = static_cast<float>(m_deferredRenderSystem->GetSceneWidth());
					vp.Height = static_cast<float>(m_deferredRenderSystem->GetSceneHeight());
				}
			}
			else
			{
				vp.Width = static_cast<float>(m_width);
				vp.Height = static_cast<float>(m_height);
			}
			vp.MaxDepth = 1.0f;

			auto* ctx = m_renderDevice ? m_renderDevice->GetImmediateContext() : nullptr;
			if (ctx && vp.Width > 0.0f && vp.Height > 0.0f)
			{
				ctx->RSSetViewports(1, &vp);
				ctx->OMSetRenderTargets(1, &overlayRTV, overlayDSV);
			}
		}

		if (m_effectSystem) m_effectSystem->Render(m_world, m_camera);
		if (m_unityVfxMeshRenderSystem) m_unityVfxMeshRenderSystem->Render(m_world, m_camera, m_timer.DeltaTime());
		if (m_trailRenderSystem) m_trailRenderSystem->Render(m_world, m_camera);

		// 에디터 모드에서는 뷰포트 RTV를 SRV로 읽어야 하므로 백버퍼로 복귀
		if (m_editorMode && m_renderDevice)
		{
			auto* ctx = m_renderDevice->GetImmediateContext();
			ID3D11RenderTargetView* backBufferRTV = m_renderDevice->GetBackBufferRTV();
			ID3D11DepthStencilView* backBufferDSV = m_renderDevice->GetBackBufferDSV();
			if (ctx && backBufferRTV)
			{
				ID3D11RenderTargetView* rtvs[] = { backBufferRTV };
				ctx->OMSetRenderTargets(1, rtvs, backBufferDSV);
			}
		}
	}

	void Engine::Impl::RenderEditorDraw()
	{
		if (m_editorMode)
			m_editorCore.RenderDrawData();
	}

	void Engine::Impl::RenderEndFrame()
	{
		m_renderDevice->EndFrame();
	}

	void Engine::Impl::EnsureSkinnedMeshesRegisteredForWorld()
	{
		auto* device = m_renderDevice ? m_renderDevice->GetDevice() : nullptr;
		if (!device || m_world.GetComponents<SkinnedMeshComponent>().empty()) return;

		for (const auto& [entityId, comp] : m_world.GetComponents<SkinnedMeshComponent>())
		{
			// 이미 등록되었거나 경로가 비어있으면 스킵
			if (comp.meshAssetPath.empty() || m_skinnedMeshRegistry.Find(comp.meshAssetPath)) continue;

			// 논리적 파일 경로 구성
			std::filesystem::path assetPath = comp.instanceAssetPath.empty()
				? std::filesystem::path("Assets/Fbx") / (comp.meshAssetPath + ".fbxasset")
				: std::filesystem::path(comp.instanceAssetPath);

			// 절대경로가 섞여 있다면 파일명만 추출하여 표준 경로로 보정
			if (assetPath.is_absolute()) assetPath = std::filesystem::path("Assets/Fbx") / assetPath.filename();

			// .fbxasset 로드 (메타데이터)
			Alice::FbxInstanceAsset instance{};
			if (!Alice::LoadFbxInstanceAssetAuto(m_resourceManager, assetPath, instance))
			{
				ALICE_LOG_WARN("Engine: Failed to load fbxasset '%s'", assetPath.string().c_str());
				continue;
			}

			// FBX 임포트 수행
			// GameMode: 논리 경로 유지 (Chunk 로딩), EditorMode: 물리 경로 변환 (파일 로딩)
			std::filesystem::path srcFbx = m_editorMode
				? m_resourceManager.Resolve(instance.sourceFbx)
				: std::filesystem::path(instance.sourceFbx);

			FbxImporter importer(m_resourceManager, &m_skinnedMeshRegistry);
			FbxImportResult res = importer.Import(device, srcFbx, FbxImportOptions{});

			ALICE_LOG_INFO("Engine: Registered Mesh '%s' -> '%s'", comp.meshAssetPath.c_str(), res.meshAssetPath.c_str());
		}
	}

	void Engine::Impl::TrimVideoMemory()
	{
		m_renderDevice->TrimVideoMemory();
	}

	void Engine::Impl::SetUseForwardRendering(bool useForward)
	{
		// 즉시 전환하지 않고, 다음 프레임 시작 시 전환하도록 플래그만 설정
		// 이렇게 하면 렌더링 중간에 리소스 상태가 꼬이는 것을 방지할 수 있습니다.
		if (m_useForwardRendering != useForward)
		{
			m_pendingRenderSystemChange = true;
			m_pendingUseForwardRendering = useForward;
		}
	}

	bool Engine::Impl::GetUseForwardRendering() const
	{
		return m_useForwardRendering;
	}

	void Engine::Impl::UpdateIblForScene()
	{
		if (!m_forwardRenderSystem) return;

		// 씬 파일에서 IBL 세트 정보를 읽어올 수 있도록 확장 가능하지만,
		// 현재는 기본적으로 "Bridge" IBL 세트를 사용합니다.
		// 향후 씬 파일에 IBL 세트 정보를 추가하면 여기서 읽어올 수 있습니다.
		const std::string iblSuffix = (m_skyboxResolution == 1) ? "MDR" : "HDR";
		m_forwardRenderSystem->SetIblSet("Bridge", "bridge", iblSuffix);
	}

}
