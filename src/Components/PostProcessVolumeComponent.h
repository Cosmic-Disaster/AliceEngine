#pragma once

#include <DirectXMath.h>
#include "Rendering/PostProcessSettings.h"
#include <algorithm>

namespace Alice
{
    /// Post Process Volume의 Shape 타입
    enum class PostProcessVolumeShape
    {
        Box = 0    // AABB 또는 OBB 박스
    };

    /// Unreal Engine 스타일의 Post Process Volume 컴포넌트
    /// 씬에 배치하여 카메라 위치에 따라 Post Process 설정을 블렌딩합니다.
    class PostProcessVolumeComponent
    {
    public:
        PostProcessVolumeComponent() = default;
        virtual ~PostProcessVolumeComponent() = default;

        // ==== Shape 설정 ====
        PostProcessVolumeShape shape = PostProcessVolumeShape::Box;
        
        /// Unbound: true면 항상 활성화 (무한 범위)
        bool unbound = false;

        /// Box 크기 (월드 스케일 기준, TransformComponent의 scale과 곱해짐)
        DirectX::XMFLOAT3 boxSize = { 10.0f, 10.0f, 10.0f };

        /// Bound: 보간 시작 기준이 되는 박스 크기 (Scale과 곱해져서 DebugBoxDraw로 그려짐)
        float bound = 10.0f;

        // ==== 블렌딩 파라미터 ====
        /// BlendRadius: 볼륨 외부에서도 블렌딩되는 거리 (>= 0)
        /// 0이면 볼륨 내부에서만 적용
        float blendRadius = 0.0f;

        /// BlendWeight: 이 볼륨의 블렌딩 가중치 (0~1)
        float blendWeight = 1.0f;

        /// Priority: 우선순위 (높을수록 나중에 블렌딩되어 영향이 큼)
        /// UE와 동일: 높은 priority가 나중에 적용되어 최종값에 더 큰 영향
        int priority = 0;

        // ==== Post Process Settings ====
        PostProcessSettings settings;

        // ==== 참조 오브젝트 설정 ====
        /// 보간 기준이 될 GameObject 이름 (비어있으면 카메라 위치 사용)
        std::string referenceObjectName;

        /// 참조 오브젝트 사용 여부
        bool useReferenceObject = false;

        // ==== RTTR 연동용 Getter/Setter ====
        bool GetUnbound() const { return unbound; }
        void SetUnbound(bool val) { unbound = val; }

        float GetBlendRadius() const { return blendRadius; }
        void SetBlendRadius(float val) { blendRadius = std::max(0.0f, val); }

        float GetBlendWeight() const { return blendWeight; }
        void SetBlendWeight(float val) { blendWeight = std::clamp(val, 0.0f, 1.0f); }

        int GetPriority() const { return priority; }
        void SetPriority(int val) { priority = val; }

        PostProcessVolumeShape GetShape() const { return shape; }
        void SetShape(PostProcessVolumeShape val) { shape = val; }

        const DirectX::XMFLOAT3& GetBoxSize() const { return boxSize; }
        void SetBoxSize(const DirectX::XMFLOAT3& val) 
        { 
            boxSize.x = std::max(0.01f, val.x);
            boxSize.y = std::max(0.01f, val.y);
            boxSize.z = std::max(0.01f, val.z);
        }

        float GetBound() const { return bound; }
        void SetBound(float val) { bound = std::max(0.01f, val); }

        const std::string& GetReferenceObjectName() const { return referenceObjectName; }
        void SetReferenceObjectName(const std::string& val) { referenceObjectName = val; }

        bool GetUseReferenceObject() const { return useReferenceObject; }
        void SetUseReferenceObject(bool val) { useReferenceObject = val; }
    };
}
