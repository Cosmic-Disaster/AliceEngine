#include "Rendering/PostProcessVolumeSystem.h"
#include "Core/GameObject.h"
#include "Core/World.h"
#include <DirectXMath.h>
#include <algorithm>

using namespace DirectX;

namespace Alice
{
    /// UE 스타일 weight 계산 (연속적)
    static float ComputeUEWeight(float sd, float br, float bw)
    {
        bw = std::clamp(bw, 0.0f, 1.0f);
        br = std::max(0.0f, br);

        if (br <= 0.0f)
            return (sd < 0.0f) ? bw : 0.0f;

        if (sd < 0.0f) // inside
            return bw;

        // outside fade
        float t = 1.0f - (sd / br);
        t = std::clamp(t, 0.0f, 1.0f);
        return bw * t;
    }

    PostProcessSettings PostProcessVolumeSystem::CalculateFinalSettings(
        World& world,
        const XMFLOAT3& cameraPosition,
        const PostProcessSettings& defaultSettings)
    {
        // 참조 위치 결정: m_referenceObjectName이 설정되어 있으면 해당 오브젝트 위치 사용
        XMFLOAT3 referencePosition = cameraPosition;  // 기본값은 카메라 위치 (fallback)
        
        if (!m_referenceObjectName.empty())
        {
            bool needRefresh = false;
            
            // 캐시 유효성 확인
            if (m_referenceEntityId == InvalidEntityId || !m_referenceResolved)
            {
                needRefresh = true;
            }
            else
            {
                // 캐시된 EntityId가 여전히 유효한지 확인
                std::string cachedName = world.GetEntityName(m_referenceEntityId);
                if (cachedName != m_referenceObjectName)
                {
                    needRefresh = true;
                }
                else
                {
                    // TransformComponent가 여전히 존재하는지 확인
                    auto* transform = world.GetComponent<TransformComponent>(m_referenceEntityId);
                    if (!transform || !transform->enabled)
                    {
                        needRefresh = true;
                    }
                }
            }

            if (needRefresh)
            {
                // World에서 이름으로 GameObject 찾기
                GameObject refObj = world.FindGameObject(m_referenceObjectName);
                if (refObj.IsValid())
                {
                    m_referenceEntityId = refObj.id();
                    m_referenceResolved = true;
                    m_hasWarnedAboutMissingObject = false;  // 찾았으면 경고 플래그 리셋
                }
                else
                {
                    m_referenceEntityId = InvalidEntityId;
                    m_referenceResolved = false;
                    // 경고는 한 번만 출력 (스팸 방지)
                    if (!m_hasWarnedAboutMissingObject)
                    {
                        ALICE_LOG_WARN("PostProcessVolumeSystem: GameObject '%s' not found. Using camera position as fallback.", m_referenceObjectName.c_str());
                        m_hasWarnedAboutMissingObject = true;
                    }
                }
            }

            // TransformComponent에서 위치 가져오기
            if (m_referenceEntityId != InvalidEntityId && m_referenceResolved)
            {
                auto* transform = world.GetComponent<TransformComponent>(m_referenceEntityId);
                if (transform && transform->enabled)
                {
                    referencePosition = transform->position;
                }
                else
                {
                    // Transform이 없거나 비활성화면 fallback으로 cameraPosition 사용
                    m_referenceResolved = false;
                    if (!m_hasWarnedAboutMissingObject)
                    {
                        ALICE_LOG_WARN("PostProcessVolumeSystem: GameObject '%s' has no valid TransformComponent. Using camera position as fallback.", m_referenceObjectName.c_str());
                        m_hasWarnedAboutMissingObject = true;
                    }
                    referencePosition = cameraPosition;
                }
            }
            // Entity를 찾지 못했으면 이미 referencePosition = cameraPosition으로 설정됨
        }

        // 1. 후보 수집 및 signed distance 계산
        std::vector<PostProcessVolumeCandidate> candidates;
        CollectCandidates(world, referencePosition, candidates);

        // 2. Priority 기준 정렬 (낮은 priority가 먼저, 높은 priority가 나중에 블렌딩)
        std::sort(candidates.begin(), candidates.end(),
            [](const PostProcessVolumeCandidate& a, const PostProcessVolumeCandidate& b)
            {
                return a.volume->priority < b.volume->priority;
            });

        // 3. 가장 높은 Priority의 Bound 볼륨 찾기 (트랜지션 대상)
        PostProcessVolumeCandidate* activeCandidate = nullptr;
        int highestPriority = INT_MIN;
        for (auto& candidate : candidates)
        {
            if (!candidate.volume->unbound && candidate.volume->priority > highestPriority)
            {
                highestPriority = candidate.volume->priority;
                activeCandidate = &candidate;
            }
        }

        // Active 볼륨의 weight를 UE 방식으로 재계산
        if (activeCandidate && !activeCandidate->volume->unbound)
        {
            float br = std::max(0.0f, activeCandidate->volume->blendRadius);
            float bw = std::clamp(activeCandidate->volume->blendWeight, 0.0f, 1.0f);
            activeCandidate->weight = ComputeUEWeight(activeCandidate->signedDistance, br, bw);
        }

        // 3.5. Bound 변경 감지 및 보간 기준값 재설정
        if (activeCandidate && !activeCandidate->volume->unbound && 
            m_transitionState.activeVolumeId == activeCandidate->entityId)
        {
            float currentBound = activeCandidate->volume->bound;
            // Transform의 scale도 고려하여 월드 공간 bound 계산
            auto* transform = world.GetComponent<TransformComponent>(activeCandidate->entityId);
            if (transform)
            {
                // Scale의 평균값을 사용하여 bound 스케일링 (정육면체 가정)
                float avgScale = (transform->scale.x + transform->scale.y + transform->scale.z) / 3.0f;
                currentBound *= avgScale;
            }

            // Bound가 변경되었는지 확인
            if (m_transitionState.cachedBound >= 0.0f && 
                std::abs(m_transitionState.cachedBound - currentBound) > 0.001f)
            {
                // Bound가 변경되었으면 현재 상태를 outsideSnapshot으로 재설정
                // 이전 프레임의 final 값이 있으면 그것을 사용, 없으면 defaultSettings + Unbound만 적용
                if (m_transitionState.hasPreviousFrameFinal)
                {
                    m_transitionState.outsideSnapshot = m_transitionState.previousFrameFinal;
                }
                else
                {
                    // 이전 프레임 값이 없으면 defaultSettings + Unbound 볼륨들만 적용된 값
                    PostProcessSettings outsideFinal = defaultSettings;
                    for (const auto& candidate : candidates)
                    {
                        if (candidate.volume->unbound && candidate.weight > 0.0f)
                        {
                            PostProcessBlend::BlendSettings(outsideFinal, candidate.volume->settings, candidate.weight);
                        }
                    }
                    m_transitionState.outsideSnapshot = outsideFinal;
                }
                m_transitionState.hasOutsideSnapshot = true;
            }

            // 현재 bound 값 캐시
            m_transitionState.cachedBound = currentBound;
        }
        else if (!activeCandidate || activeCandidate->volume->unbound || 
                 m_transitionState.activeVolumeId != activeCandidate->entityId)
        {
            // Active 볼륨이 변경되었거나 없으면 캐시 초기화
            m_transitionState.cachedBound = -1.0f;
        }

        // 4. 영향 시작 감지 및 스냅샷 저장 (w > 0이 되는 순간)
        bool isAffectingNow = (activeCandidate != nullptr && !activeCandidate->volume->unbound && activeCandidate->weight > 0.0f);
        bool wasAffecting = m_transitionState.wasAffecting;
        bool isEntering = !wasAffecting && isAffectingNow;  // 영향 시작 전이

        if (isEntering && activeCandidate)
        {
            // 영향이 시작되는 순간 바깥값 저장
            // 이전 프레임의 최종값이 있으면 그것을 사용, 없으면 defaultSettings + Unbound만 적용
            if (m_transitionState.hasPreviousFrameFinal)
            {
                m_transitionState.outsideSnapshot = m_transitionState.previousFrameFinal;
            }
            else
            {
                // 이전 프레임 값이 없으면 defaultSettings + Unbound 볼륨들만 적용된 값
                PostProcessSettings outsideFinal = defaultSettings;
                for (const auto& candidate : candidates)
                {
                    if (candidate.volume->unbound && candidate.weight > 0.0f)
                    {
                        PostProcessBlend::BlendSettings(outsideFinal, candidate.volume->settings, candidate.weight);
                    }
                }
                m_transitionState.outsideSnapshot = outsideFinal;
            }
            m_transitionState.hasOutsideSnapshot = true;
            m_transitionState.activeVolumeId = activeCandidate->entityId;
            
            // Bound 캐시 초기화 (새로운 볼륨 진입 시)
            auto* transform = world.GetComponent<TransformComponent>(activeCandidate->entityId);
            if (transform)
            {
                float avgScale = (transform->scale.x + transform->scale.y + transform->scale.z) / 3.0f;
                m_transitionState.cachedBound = activeCandidate->volume->bound * avgScale;
            }
            else
            {
                m_transitionState.cachedBound = activeCandidate->volume->bound;
            }
        }

        // 5. OutsideSnapshot 결정
        PostProcessSettings outsideSnapshot;
        //if (m_transitionState.hasOutsideSnapshot)
        {
            outsideSnapshot = m_transitionState.outsideSnapshot;
        }
        //else
        //{
        //    // 스냅샷이 없으면 defaultSettings 사용
        //    outsideSnapshot = defaultSettings;
        //}

        // 6. Active 볼륨이 있으면 거리 기반 weight 계산 및 Final 계산
        PostProcessSettings final;
        if (activeCandidate && !activeCandidate->volume->unbound && activeCandidate->weight > 0.0f)
        {
            // Final = OutsideSnapshot에서 시작
            final = outsideSnapshot;

            // BlendSettings에 volume->settings 원본 사용 (override 플래그 포함)
            PostProcessBlend::BlendSettings(final, activeCandidate->volume->settings, activeCandidate->weight);
        }
        else
        {
            // Active 볼륨이 없거나 영향이 없으면 OutsideSnapshot 사용
            final = outsideSnapshot;

            // Unbound 볼륨들 적용
            for (const auto& candidate : candidates)
            {
                if (candidate.volume->unbound && candidate.weight > 0.0f)
                {
                    PostProcessBlend::BlendSettings(final, candidate.volume->settings, candidate.weight);
                }
            }
        }

        // 7. 상태 업데이트
        bool wasAffectingBefore = m_transitionState.wasAffecting;
        m_transitionState.wasAffecting = isAffectingNow;

        // 밖으로 완전히 벗어났을 때 (영향 종료)
        if (wasAffectingBefore && !isAffectingNow)
        {
            // activeVolumeId 무효화 (복귀 완료)
            m_transitionState.activeVolumeId = InvalidEntityId;
            // outsideSnapshot은 그대로 유지 (복귀 목표 값)
            // Bound 캐시도 초기화
            m_transitionState.cachedBound = -1.0f;
        }
        else if (!isAffectingNow && !m_transitionState.hasOutsideSnapshot)
        {
            // 처음 밖에 있고 스냅샷이 없으면 현재 값을 저장
            m_transitionState.outsideSnapshot = final;
            m_transitionState.hasOutsideSnapshot = true;
        }

        // 이전 프레임의 final 값 저장 (다음 프레임 진입 감지용)
        m_transitionState.previousFrameFinal = final;
        m_transitionState.hasPreviousFrameFinal = true;

        return final;
    }

    void PostProcessVolumeSystem::CollectCandidates(
        World& world,
        const XMFLOAT3& referencePosition,
        std::vector<PostProcessVolumeCandidate>& outCandidates)
    {
        outCandidates.clear();

        // 모든 PostProcessVolumeComponent를 순회
        for (const auto& [entityId, volume] : world.GetComponents<PostProcessVolumeComponent>())
        {
            // TransformComponent 필요
            auto* transform = world.GetComponent<TransformComponent>(entityId);
            if (!transform || !transform->enabled)
                continue;

            // Unbound는 항상 후보
            if (volume.unbound)
            {
                PostProcessVolumeCandidate candidate;
                candidate.entityId = entityId;
                candidate.volume = const_cast<PostProcessVolumeComponent*>(&volume);
                candidate.transform = transform;
                candidate.weight = std::clamp(volume.blendWeight, 0.0f, 1.0f);
                candidate.signedDistance = -1.0f;  // Unbound는 내부로 간주
                outCandidates.push_back(candidate);
                continue;
            }

            // Bound 볼륨: signed distance 계산
            // bound * scale을 사용하여 보간 시작 기준점을 설정 (DebugBoxDraw로 그려지는 면)
            XMFLOAT3 worldBoundSize;
            float boundScaled = volume.bound;
            worldBoundSize.x = boundScaled * transform->scale.x;
            worldBoundSize.y = boundScaled * transform->scale.y;
            worldBoundSize.z = boundScaled * transform->scale.z;

            float sd = DistanceToBoxSurface(referencePosition, transform->position, worldBoundSize, transform->rotation);

            // Weight 계산 (거리 기반)
            float weight = CalculateVolumeWeight(volume, *transform, referencePosition, sd);

            if (weight > 0.0f || sd < 0.0f)  // 내부이거나 weight > 0인 경우 후보로 추가
            {
                PostProcessVolumeCandidate candidate;
                candidate.entityId = entityId;
                candidate.volume = const_cast<PostProcessVolumeComponent*>(&volume);
                candidate.transform = transform;
                candidate.weight = weight;
                candidate.signedDistance = sd;
                outCandidates.push_back(candidate);
            }
        }
    }

    float PostProcessVolumeSystem::CalculateVolumeWeight(
        const PostProcessVolumeComponent& volume,
        const TransformComponent& transform,
        const XMFLOAT3& referencePosition,
        float signedDistance)
    {
        // Unbound면 항상 BlendWeight 반환 (전역 적용)
        if (volume.unbound)
        {
            return std::clamp(volume.blendWeight, 0.0f, 1.0f);
        }

        // Bound 볼륨: UE 방식 weight 계산
        float br = std::max(0.0f, volume.blendRadius);
        float bw = std::clamp(volume.blendWeight, 0.0f, 1.0f);
        return ComputeUEWeight(signedDistance, br, bw);
    }

    bool PostProcessVolumeSystem::IsPointInsideBox(
        const XMFLOAT3& point,
        const XMFLOAT3& boxCenter,
        const XMFLOAT3& boxSize,
        const XMFLOAT3& boxRotationRad)
    {
        // 회전 행렬 계산
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(
            boxRotationRad.x,  // Pitch
            boxRotationRad.y,  // Yaw
            boxRotationRad.z   // Roll
        );

        // 박스 중심을 원점으로 이동
        XMVECTOR localPoint = XMVectorSubtract(XMLoadFloat3(&point), XMLoadFloat3(&boxCenter));

        // 회전의 역행렬을 적용하여 로컬 공간으로 변환
        XMMATRIX invRotation = XMMatrixTranspose(rotationMatrix);  // 회전 행렬의 역행렬 = 전치 행렬
        localPoint = XMVector3Transform(localPoint, invRotation);

        // 로컬 공간에서 AABB 내부 여부 확인
        XMFLOAT3 local;
        XMStoreFloat3(&local, localPoint);

        float halfSizeX = boxSize.x * 0.5f;
        float halfSizeY = boxSize.y * 0.5f;
        float halfSizeZ = boxSize.z * 0.5f;

        return (local.x >= -halfSizeX && local.x <= halfSizeX &&
                local.y >= -halfSizeY && local.y <= halfSizeY &&
                local.z >= -halfSizeZ && local.z <= halfSizeZ);
    }

    float PostProcessVolumeSystem::DistanceToBoxSurface(
        const XMFLOAT3& point,
        const XMFLOAT3& boxCenter,
        const XMFLOAT3& boxSize,
        const XMFLOAT3& boxRotationRad)
    {
        // 회전 행렬 계산
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(
            boxRotationRad.x,  // Pitch
            boxRotationRad.y,  // Yaw
            boxRotationRad.z   // Roll
        );

        // 박스 중심을 원점으로 이동
        XMVECTOR localPoint = XMVectorSubtract(XMLoadFloat3(&point), XMLoadFloat3(&boxCenter));

        // 회전의 역행렬을 적용하여 로컬 공간으로 변환
        XMMATRIX invRotation = XMMatrixTranspose(rotationMatrix);
        localPoint = XMVector3Transform(localPoint, invRotation);

        // 로컬 공간 좌표
        XMFLOAT3 local;
        XMStoreFloat3(&local, localPoint);
        
        float halfSizeX = boxSize.x * 0.5f;
        float halfSizeY = boxSize.y * 0.5f;
        float halfSizeZ = boxSize.z * 0.5f;

        // AABB 내부 여부 확인
        bool inside = (local.x >= -halfSizeX && local.x <= halfSizeX &&
                       local.y >= -halfSizeY && local.y <= halfSizeY &&
                       local.z >= -halfSizeZ && local.z <= halfSizeZ);

        if (inside)
        {
            // 내부: 가장 가까운 면까지의 거리 (음수로 반환)
            float distX = std::min(local.x + halfSizeX, halfSizeX - local.x);
            float distY = std::min(local.y + halfSizeY, halfSizeY - local.y);
            float distZ = std::min(local.z + halfSizeZ, halfSizeZ - local.z);
            float minDist = std::min({ distX, distY, distZ });
            return -minDist;  // 음수 = 내부
        }
        else
        {
            // 외부: 가장 가까운 점까지의 거리
            
            DirectX::XMFLOAT3 limits = { std::max(0.01f,halfSizeX) ,std::max(0.01f,halfSizeY) , std::max(0.01f,halfSizeZ)};
            
			float closestX = std::clamp(local.x, -limits.x,  limits.x);
            float closestY = std::clamp(local.y, -limits.y,  limits.y);
            float closestZ = std::clamp(local.z, -limits.z,  limits.z);

            XMVECTOR closest = XMVectorSet(closestX, closestY, closestZ, 0.0f);
            XMVECTOR diff = XMVectorSubtract(localPoint, closest);
            float distance = XMVectorGetX(XMVector3Length(diff));
            return distance;  // 양수 = 외부 거리
        }
    }

    float PostProcessVolumeSystem::DistanceToBoxCenter(
        const XMFLOAT3& point,
        const XMFLOAT3& boxCenter,
        const XMFLOAT3& boxSize,
        const XMFLOAT3& boxRotationRad)
    {
        // 회전 행렬 계산
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(
            boxRotationRad.x,  // Pitch
            boxRotationRad.y,  // Yaw
            boxRotationRad.z   // Roll
        );

        // 박스 중심을 원점으로 이동
        XMVECTOR localPoint = XMVectorSubtract(XMLoadFloat3(&point), XMLoadFloat3(&boxCenter));

        // 회전의 역행렬을 적용하여 로컬 공간으로 변환
        XMMATRIX invRotation = XMMatrixTranspose(rotationMatrix);
        localPoint = XMVector3Transform(localPoint, invRotation);

        // 로컬 공간 좌표
        XMFLOAT3 local;
        XMStoreFloat3(&local, localPoint);

        // 원점까지의 거리 계산
        float distance = XMVectorGetX(XMVector3Length(localPoint));
        return distance;
    }

    PostProcessSettings PostProcessVolumeSystem::CalculateVolumeTarget(
        const PostProcessSettings& baseSettings,
        const PostProcessSettings& volumeSettings)
    {
        // VolumeTarget = baseSettings에서 override된 항목만 volumeSettings 값으로 교체
        PostProcessSettings target = baseSettings;

        // override된 항목만 볼륨값으로 교체
        if (volumeSettings.bOverride_Exposure)
            target.exposure = volumeSettings.exposure;
        if (volumeSettings.bOverride_MaxHDRNits)
            target.maxHDRNits = volumeSettings.maxHDRNits;
        if (volumeSettings.bOverride_ColorGradingSaturation)
            target.saturation = volumeSettings.saturation;
        if (volumeSettings.bOverride_ColorGradingContrast)
            target.contrast = volumeSettings.contrast;
        if (volumeSettings.bOverride_ColorGradingGamma)
            target.gamma = volumeSettings.gamma;
        if (volumeSettings.bOverride_ColorGradingGain)
            target.gain = volumeSettings.gain;
        if (volumeSettings.bOverride_BloomThreshold)
            target.bloomThreshold = volumeSettings.bloomThreshold;
        if (volumeSettings.bOverride_BloomKnee)
            target.bloomKnee = volumeSettings.bloomKnee;
        if (volumeSettings.bOverride_BloomIntensity)
            target.bloomIntensity = volumeSettings.bloomIntensity;
        if (volumeSettings.bOverride_BloomGaussianIntensity)
            target.bloomGaussianIntensity = volumeSettings.bloomGaussianIntensity;
        if (volumeSettings.bOverride_BloomRadius)
            target.bloomRadius = volumeSettings.bloomRadius;
        if (volumeSettings.bOverride_BloomDownsample)
            target.bloomDownsample = volumeSettings.bloomDownsample;

        return target;
    }
}
