#pragma once

#include <vector>
#include <functional>

#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/ECS/Components/TransformComponent.h"

namespace Alice
{
    /// World의 SkinnedMeshComponent를 순회하여
    /// ForwardRenderSystem에서 사용할 SkinnedDrawCommand 리스트를 생성합니다.
    /// - 애니메이션/스켈레탈 메시의 boneMatrices를 전달하면
    ///   스킨드 메시가 해당 본에 따라 렌더링되도록 변환됩니다.
    class SkinnedMeshSystem
    {
    public:
        explicit SkinnedMeshSystem(SkinnedMeshRegistry& registry)
            : m_registry(registry)
        {
        }

        /// World + Registry를 순회하여 렌더링 드로우 커맨드 리스트를 구성합니다.
        void BuildDrawList(const World& world,
            std::vector<SkinnedDrawCommand>& outCommands) const
        {
            outCommands.clear();

            const auto& skinnedMap = world.GetComponents<SkinnedMeshComponent>();
            if (skinnedMap.empty())
            {
                // 빈 상태(렌더링 드로우 커맨드가 하나도 없을 때) 조기 반환을 수행합니다.
                return;
            }

            {
                //ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: skinnedComponents=%zu",
                //               skinnedMap.size());
            }

            for (const auto& [entityId, comp] : skinnedMap)
            {
                if (!comp.boneMatrices || comp.boneCount == 0)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no bones",
                    //               static_cast<unsigned>(entityId));
                    continue;
                }

                auto mesh = m_registry.Find(comp.meshAssetPath);
                if (!mesh)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: mesh not found for key=\"%s\"", comp.meshAssetPath.c_str());
                    continue;
                }

                const TransformComponent* t = world.GetComponent<TransformComponent>(entityId);
                if (!t)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no Transform", static_cast<unsigned>(entityId));
                    continue;
                }
                if (!t->enabled || !t->visible) continue;

                // 월드 행렬 계산 (c.txt 참조: 부모부터 루트까지 스택에 쌓고 역순으로 곱하기)
                using namespace DirectX;
                
                std::vector<XMMATRIX> matrixStack;
                EntityId currentId = entityId;
                
                // 부모부터 루트까지 로컬 행렬을 스택에 쌓음
                while (currentId != InvalidEntityId)
                {
                    const TransformComponent* tc = world.GetComponent<TransformComponent>(currentId);
                    if (tc && tc->enabled)
                    {
                        XMVECTOR scale = XMLoadFloat3(&tc->scale);
                        XMVECTOR rotation = XMLoadFloat3(&tc->rotation);
                        XMVECTOR translation = XMLoadFloat3(&tc->position);
                        
                        // 로컬 행렬: S * R * T 순서 (DirectXMath 행벡터 컨벤션)
                        XMMATRIX localMatrix = XMMatrixScalingFromVector(scale) *
                            XMMatrixRotationRollPitchYawFromVector(rotation) *
                            XMMatrixTranslationFromVector(translation);
                        
                        matrixStack.push_back(localMatrix);
                        currentId = tc->parent;
                    }
                    else
                    {
                        break;
                    }
                }
                
                // 행벡터 컨벤션: child * parent * ... * root 형태로 곱하기 (정순)
                XMMATRIX worldM = XMMatrixIdentity();
                for (const auto& m : matrixStack)  // child -> parent -> root 순서로
                {
                    worldM = worldM * m;  // I * child * parent * ... * root
                }

                SkinnedDrawCommand cmd = {};
                cmd.vertexBuffer = mesh->vertexBuffer.Get();
                cmd.indexBuffer = mesh->indexBuffer.Get();
                cmd.stride = mesh->stride;
                cmd.indexCount = mesh->indexCount;
                cmd.startIndex = mesh->startIndex;
                cmd.baseVertex = mesh->baseVertex;
                cmd.world = worldM;
                cmd.bones = comp.boneMatrices;
                cmd.boneCount = comp.boneCount;
                cmd.meshKey = comp.meshAssetPath;

                if (const MaterialComponent* mat = world.GetComponent<MaterialComponent>(entityId))
                {
                    cmd.color = mat->color;
                    cmd.alpha = mat->alpha;
                    cmd.roughness = mat->roughness;
                    cmd.metalness = mat->metalness;
                    cmd.ambientOcclusion = mat->ambientOcclusion;
                    cmd.envDiffuseStrength = mat->envDiffuseStrength;
                    cmd.envSpecularStrength = mat->envSpecularStrength;
                    cmd.normalStrength = mat->normalStrength;
                    cmd.shadingMode = mat->shadingMode;
                    cmd.transparent = mat->transparent;
                    cmd.outlineColor = mat->outlineColor;
                    cmd.outlineWidth = mat->outlineWidth;
                    cmd.albedoTexturePath = mat->albedoTexturePath;
                    cmd.toonPbrCuts = DirectX::XMFLOAT4(mat->toonPbrCut1, mat->toonPbrCut2, mat->toonPbrCut3, mat->toonPbrStrength);
                    cmd.toonPbrLevels = DirectX::XMFLOAT4(mat->toonPbrLevel1, mat->toonPbrLevel2, mat->toonPbrLevel3,
                                                          mat->toonPbrBlur ? 1.0f : 0.0f);

                    if (!mat->albedoTexturePath.empty())
                    {
                        //ALICE_LOG_INFO("[SkinnedMeshSystem] entity=%u mesh=\"%s\" albedoTex=\"%s\"",
                        //               static_cast<unsigned>(entityId),
                        //               comp.meshAssetPath.c_str(),
                        //               mat->albedoTexturePath.c_str());
                    }
                }
                else
                {
                    cmd.transparent = false;
                }

                outCommands.push_back(cmd);
            }

            //if (!outCommands.empty())
            //{
            //    // 실제로 렌더링될 스킨드 메시가 하나라도 있을 때만 로그를 출력합니다.
            //    ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: commands=%zu",
            //                   outCommands.size());
            //}
        }

    private:
        SkinnedMeshRegistry& m_registry;
    };
}
