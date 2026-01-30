#include "Engine/EngineImpl.h"

namespace Alice
{
	void Engine::Impl::ClearWorldAndPhysics()
	{
		// 월드와 물리 시스템을 함께 정리하는 안전한 진입점
		// World::Clear()가 호출되면 OnBeforeClear 콜백이 자동으로 PhysicsSystem을 정리하므로,
		// 이 함수는 단순히 World::Clear()를 호출하면 됨
		m_world.Clear();

		// World::Clear()에서 이미 물리 월드를 reset했지만, 명시적으로도 해제
		m_world.SetPhysicsWorld(nullptr);
	}

	void Engine::Impl::RefreshPhysicsForCurrentWorld()
	{
		ThreadSafety::AssertMainThread();
		// 현재 씬의 물리 월드 설정을 갱신
		// Phy_SettingsComponent를 기반으로 물리 월드를 생성/재사용
		// settings가 없으면, 물리월드 제거(비물리 씬)
		const auto& settingsMap = m_world.GetComponents<Phy_SettingsComponent>();

		if (settingsMap.empty())
		{
			// 물리월드 끄기 직전
			if (auto pwShared = m_world.GetPhysicsWorldShared())
			{
				pwShared->Flush();            // pending add/remove/release 처리
			}

			m_physAccum = 0.0f;
			m_physicsEventQueue.clear();

			// 안전한 파괴 순서: PhysicsSystem 먼저 정리 (액터 Destroy) → 월드 해제
			if (m_physicsSystem) { m_physicsSystem->SetPhysicsWorld(nullptr); }
			m_world.SetPhysicsWorld(nullptr);
			return;
		}

		// 여러 settings 컴포넌트가 있을 경우 첫 번째 것만 사용 (명확하게)
		const auto& settings = settingsMap.begin()->second;
		if (!settings.enablePhysics)
		{
			// 물리월드 끄기 직전
			if (auto pwShared = m_world.GetPhysicsWorldShared())
			{
				pwShared->Flush();            // pending add/remove/release 처리
			}

			m_physAccum = 0.0f;
			m_physicsEventQueue.clear();

			// 안전한 파괴 순서: PhysicsSystem 먼저 정리 (액터 Destroy) → 월드 해제
			if (m_physicsSystem) { m_physicsSystem->SetPhysicsWorld(nullptr); }
			m_world.SetPhysicsWorld(nullptr);
			return;
		}

		IPhysicsWorld* existingWorld = m_world.GetPhysicsWorld();
		Vec3 newGravity = Vec3(settings.gravity.x, settings.gravity.y, settings.gravity.z);

		// 기존 월드가 있고 설정이 변경되지 않았다면 그대로 사용
		if (existingWorld)
		{
			Vec3 currentGravity = existingWorld->GetGravity();
			// 중력이 변경되었으면 업데이트
			if (currentGravity.x != newGravity.x || currentGravity.y != newGravity.y || currentGravity.z != newGravity.z)
			{
				existingWorld->SetGravity(newGravity);
				ALICE_LOG_INFO("PhysicsWorld gravity updated: (%.2f, %.2f, %.2f)", newGravity.x, newGravity.y, newGravity.z);
			}

			// fixedDt/maxSubsteps는 매 프레임 업데이트 (에디터에서 변경 가능)
			m_physFixedDt = settings.fixedDt;
			m_physMaxSubsteps = settings.maxSubsteps;
			// accum은 유지 (프레임 드롭 방지)

			// PhysicsSystem에 물리 월드 설정 (이미 같은 월드면 재설정 생략 - 불필요한 전체 재초기화 방지)
			if (m_physicsSystem && m_physicsSystem->GetPhysicsWorld() != existingWorld)
			{
				m_physicsSystem->SetPhysicsWorld(existingWorld);
			}

			// Phy_SettingsComponent의 layerCollideMatrix와 layerQueryMatrix 변경은
			// 런타임에 적용할 수 없으므로 (FilterShader는 씬 생성 시 설정됨),
			// 변경 시 물리 월드를 재생성해야 합니다.
			// 하지만 매 프레임 체크하는 것은 비효율적이므로, 에디터에서 변경 시 씬 재로드를 권장합니다.

			return;
		}

		// 새 월드 생성
		PhysicsModule::WorldDesc desc{};
		desc.gravity = newGravity;
		// 필요하면 여기서 CCD / 이벤트 옵션들 설정

		std::shared_ptr<IPhysicsWorld> world = m_physics.CreateWorld(desc);
		if (!world)
		{
			ALICE_LOG_ERRORF("Failed to create PhysicsWorld");
			return;
		}

		ALICE_LOG_INFO("PhysicsWorld created: %p", world.get());
		m_world.SetPhysicsWorld(world);

		// PhysicsSystem에 물리 월드 설정
		if (m_physicsSystem)
		{
			m_physicsSystem->SetPhysicsWorld(world.get());
		}

		// settings 반영
		m_physFixedDt = settings.fixedDt;
		m_physMaxSubsteps = settings.maxSubsteps;
		m_physAccum = 0.0f;
	}

	void Engine::Impl::TickPhysics(float dt)
	{
		// 물리 시뮬레이션 수행 (고정 시간 스텝)
		// Physics → Game 동기화 및 이벤트 수집
		auto pwShared = m_world.GetPhysicsWorldShared(); // 로컬로 수명을 고정시킴
		IPhysicsWorld* pw = pwShared.get();
		if (!pw) {
			if (m_physicsSystem && m_physicsSystem->GetPhysicsWorld() != nullptr)
				m_physicsSystem->SetPhysicsWorld(nullptr);
			return;
		}

		dt = std::min(dt, 0.25f);

		m_physAccum += dt;
		int steps = 0;

		std::vector<ActiveTransform> moved;

		std::vector<PhysicsEvent> events;

		while (m_physAccum >= m_physFixedDt && steps < m_physMaxSubsteps)
		{
			pw->Step(m_physFixedDt);

			moved.clear();
			pw->DrainActiveTransforms(moved);

			// Physics → Game 동기화 (PhysicsSystem을 통해 처리)
			if (m_physicsSystem)
			{
				for (const auto& at : moved)
				{
					m_physicsSystem->SyncPhysicsToGame(at);
				}
			}
			else
			{
				// Fallback: PhysicsSystem이 없을 때 직접 동기화
				for (const auto& at : moved)
				{
					if (!at.userData) continue;

					// worldEpoch 검증 포함하여 EntityId 추출 (이전 씬의 userData는 무시)
					const EntityId id = m_world.ExtractEntityIdFromUserData(at.userData);
					if (id == InvalidEntityId) continue;

					auto* tr = m_world.GetComponent<TransformComponent>(id);
					if (!tr) continue;

					tr->position = { at.position.x, at.position.y, at.position.z };
					// 회전도 동기화 (static 메서드이므로 PhysicsSystem 인스턴스 없이도 호출 가능)
					DirectX::XMFLOAT3 euler = PhysicsSystem::ToEulerRadians(at.rotation);
					tr->rotation = euler;
				}
			}

			// 이벤트 드레인 및 큐에 누적 (한 프레임 안전하게 처리)
			events.clear();
			pw->DrainEvents(events);

			// 이벤트를 큐에 추가 (다음 프레임 게임 로직에서 처리)
			m_physicsEventQueue.insert(
				m_physicsEventQueue.end(),
				events.begin(),
				events.end()
			);

			m_physAccum -= m_physFixedDt;
			++steps;
		}

		if (steps == m_physMaxSubsteps)
			m_physAccum = 0.0f;

		// 로그로 떨어지는지 확인 (1초에 1번만)
	}

	void Engine::Impl::ProcessPhysicsEvents()
	{
		// 물리 이벤트 큐 처리 (한 프레임 안전하게 처리)
		// 물리 시뮬레이션에서 발생한 충돌/트리거 이벤트를 게임 로직으로 전달
		for (const auto& e : m_physicsEventQueue)
		{
			if (!e.userDataA || !e.userDataB) continue;

			// worldEpoch 검증 포함하여 EntityId 추출 (이전 씬의 userData는 무시)
			EntityId entityA = m_world.ExtractEntityIdFromUserData(e.userDataA);
			EntityId entityB = m_world.ExtractEntityIdFromUserData(e.userDataB);

			// 유효하지 않은 EntityId면 무시 (이전 씬의 이벤트)
			if (entityA == InvalidEntityId || entityB == InvalidEntityId) continue;

			// 현재 물리 시스템이 추적하는 엔티티만 처리 (씬 전환 중 stale userData 방지)
			if (m_physicsSystem)
			{
				// PhysicsSystem의 IsTrackedEntity를 사용하여 현재 추적 중인 엔티티만 처리
				// 이는 씬 전환 중 파괴된 액터의 userData가 새 엔티티를 오염시키는 것을 방지
				if (!m_physicsSystem->IsTrackedEntity(entityA) ||
					!m_physicsSystem->IsTrackedEntity(entityB))
				{
					continue; // 둘 중 하나라도 추적 중이 아니면 이벤트 무시
				}
			}

			// 이벤트 타입에 따른 처리
			switch (e.type)
			{
			case PhysicsEventType::ContactBegin:
				// TODO: 게임 시스템으로 전달 (예: 스크립트 이벤트, 컴포넌트 갱신 등)
				// ALICE_LOG_INFO("ContactBegin: Entity %llu <-> %llu", 
				//     (unsigned long long)entityA, (unsigned long long)entityB);
				break;
			case PhysicsEventType::ContactEnd:
				// TODO: 게임 시스템으로 전달
				break;
			case PhysicsEventType::TriggerEnter:
				// TODO: 게임 시스템으로 전달
				// ALICE_LOG_INFO("TriggerEnter: Entity %llu <-> %llu", 
				//     (unsigned long long)entityA, (unsigned long long)entityB);
				break;
			case PhysicsEventType::TriggerExit:
				// TODO: 게임 시스템으로 전달
				break;
			case PhysicsEventType::JointBreak:
			{
				// jointUserData는 PhysicsSystem이 MakeUserData(epoch, entityId)로 넣었음
				// 조인트를 소유한 엔티티 (조인트 컴포넌트가 붙어있는 엔티티)
				EntityId jointOwner = InvalidEntityId;
				if (e.jointUserData)
				{
					jointOwner = m_world.ExtractEntityIdFromUserData(e.jointUserData);
				}

				// 연결된 두 액터의 엔티티
				EntityId actorAEntity = InvalidEntityId;
				EntityId actorBEntity = InvalidEntityId;
				if (e.userDataA)
				{
					actorAEntity = m_world.ExtractEntityIdFromUserData(e.userDataA);
				}
				if (e.userDataB)
				{
					actorBEntity = m_world.ExtractEntityIdFromUserData(e.userDataB);
				}

				// 로그 출력 (필요하면 나중에 게임 시스템/스크립트 이벤트로 전달 가능)
				if (jointOwner != InvalidEntityId)
				{
					ALICE_LOG_INFO("[Physics] JointBreak: jointOwner=%llu, ActorA=%llu, ActorB=%llu",
						(unsigned long long)jointOwner,
						(unsigned long long)actorAEntity,
						(unsigned long long)actorBEntity);

					// PhysicsSystem에 조인트가 부러졌음을 알려서 컴포넌트의 jointHandle을 null로 설정
					// (다음 Update에서 감지하여 재생성하거나 정리 가능)
					if (m_physicsSystem)
					{
						// PhysicsSystem에 조인트 정리 요청 (필요시 구현)
						// 현재는 로그만 남기고, 다음 Update에서 컴포넌트 변경 감지로 자동 정리됨
					}
				}
				break;
			}
			}
		}
		
		// 큐 비우기
		m_physicsEventQueue.clear();
	}

	void Engine::Impl::ProcessCombatHits()
	{
		if (m_combatHitQueue.empty())
			return;

		m_combatSystem.ProcessHits(m_world, m_combatHitQueue);
		m_combatHitQueue.clear();
	}

	//=========================================================


}
