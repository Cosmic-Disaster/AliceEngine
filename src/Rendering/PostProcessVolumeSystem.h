#pragma once

#include "Core/World.h"
#include "Rendering/PostProcessSettings.h"
#include "Components/PostProcessVolumeComponent.h"
#include "Components/TransformComponent.h"
#include <DirectXCollision.h>
#include <algorithm>
#include <vector>

namespace Alice
{
    /// Post Process Volume 후보 정보 (weight 계산 결과)
    struct PostProcessVolumeCandidate
    {
        EntityId entityId;
        PostProcessVolumeComponent* volume;
        TransformComponent* transform;
        float weight;  // 최종 블렌딩 가중치 (0~1)
        float signedDistance;  // 표면까지의 signed distance (내부: 음수, 외부: 양수)
    };

    /// 볼륨 트랜지션 상태 (진입/이탈 추적용)
    struct VolumeTransitionState
    {
        bool wasAffecting = false;  // 이전 프레임에 볼륨 영향이 있었는지 (w > 0)
        PostProcessSettings outsideSnapshot;  // 진입 직전의 최종값 저장
        bool hasOutsideSnapshot = false;  // 스냅샷이 유효한지
        EntityId activeVolumeId = InvalidEntityId;  // 현재 트랜지션 대상 볼륨
        PostProcessSettings previousFrameFinal;  // 이전 프레임의 최종값 (진입 감지용)
        bool hasPreviousFrameFinal = false;  // 이전 프레임 값이 유효한지
        float cachedBound = -1.0f;  // 이전 프레임의 bound 값 (변경 감지용)
    };

    /// Post Process Volume 시스템
    /// 카메라 위치 기준으로 볼륨을 수집하고 블렌딩하여 최종 PostProcessSettings를 계산합니다.
    class PostProcessVolumeSystem
    {
    public:
        PostProcessVolumeSystem() = default;

        /// 참조 위치 기준으로 최종 PostProcessSettings를 계산합니다.
        /// @param world World 객체
        /// @param cameraPosition 카메라 월드 위치 (fallback용, m_referenceObjectName이 설정되면 해당 오브젝트 위치 사용)
        /// @param defaultSettings 기본 설정 (모든 override = false)
        /// @return 블렌딩된 최종 PostProcessSettings
        PostProcessSettings CalculateFinalSettings(
            World& world,
            const DirectX::XMFLOAT3& cameraPosition,
            const PostProcessSettings& defaultSettings
        );

        /// PPV 참조 대상 GameObject 이름을 설정합니다.
        /// @param objectName GameObject 이름 (비어있으면 cameraPosition 사용)
        void SetReferenceObjectName(const std::string& objectName) { m_referenceObjectName = objectName; m_referenceEntityId = InvalidEntityId; m_referenceResolved = false; }

        /// 현재 설정된 PPV 참조 대상 GameObject 이름을 가져옵니다.
        const std::string& GetReferenceObjectName() const { return m_referenceObjectName; }

        /// 볼륨 밖에 있을 때의 설정을 저장합니다 (다음 프레임에서 복귀용)
        void SetOutsideSettings(const PostProcessSettings& settings) { m_outsideSettings = settings; }

        /// Box 볼륨의 표면까지 최소 거리를 계산합니다 (월드 공간).
        /// @param point 월드 공간 점
        /// @param boxCenter 월드 공간 박스 중심
        /// @param boxSize 월드 공간 박스 크기 (스케일 적용됨)
        /// @param boxRotation 월드 공간 박스 회전 (쿼터니언 또는 행렬)
        /// @return 표면까지의 최소 거리 (내부면 음수, 외부면 양수)
        static float DistanceToBoxSurface(
            const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& boxCenter,
            const DirectX::XMFLOAT3& boxSize,
            const DirectX::XMFLOAT3& boxRotationRad
        );

    private:
        /// 볼륨 후보 수집 및 weight 계산
        void CollectCandidates(
            World& world,
            const DirectX::XMFLOAT3& referencePosition,
            std::vector<PostProcessVolumeCandidate>& outCandidates
        );

        /// 단일 볼륨의 weight 계산
        float CalculateVolumeWeight(
            const PostProcessVolumeComponent& volume,
            const TransformComponent& transform,
            const DirectX::XMFLOAT3& referencePosition,
            float signedDistance
        );

        /// Box 내부 여부 확인 (월드 공간, 회전 고려)
        bool IsPointInsideBox(
            const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& boxCenter,
            const DirectX::XMFLOAT3& boxSize,
            const DirectX::XMFLOAT3& boxRotationRad
        );

        /// 볼륨 원점까지의 거리를 계산합니다
        float DistanceToBoxCenter(
            const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& boxCenter,
            const DirectX::XMFLOAT3& boxSize,
            const DirectX::XMFLOAT3& boxRotationRad
        );

        /// VolumeTarget을 계산합니다 (override 기반)
        PostProcessSettings CalculateVolumeTarget(
            const PostProcessSettings& baseSettings,
            const PostProcessSettings& volumeSettings
        );

        /// 볼륨 트랜지션 상태 (단일 카메라용, 필요시 확장 가능)
        VolumeTransitionState m_transitionState;

        // PostProcess 바깥에 있는 설정
        PostProcessSettings m_outsideSettings;

        // PPV 참조 대상 GameObject 설정
        std::string m_referenceObjectName;  // 참조 대상 GameObject 이름
        EntityId m_referenceEntityId = InvalidEntityId;  // 캐시된 EntityId
        bool m_referenceResolved = false;  // 참조가 해결되었는지
        bool m_fallbackToCamera = true;  // 기본값: 카메라로 fallback
        bool m_hasWarnedAboutMissingObject = false;  // 경고 스팸 방지
    };
}
