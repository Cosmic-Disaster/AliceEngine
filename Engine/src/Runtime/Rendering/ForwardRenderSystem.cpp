#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/DebugDrawSystem.h"
#include "Runtime/Rendering/PostProcessSettings.h"

#include <d3dcompiler.h>
// 텍스처 로더 (vcpkg의 DirectXTK 사용)
#include <DirectXTK/WICTextureLoader.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <filesystem>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cstring>

#include <Runtime/Resources/ResourceManager.h>
#include <Runtime/Foundation/Logger.h>
#include <Runtime/ECS/World.h>
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/PointLightComponent.h"
#include "Runtime/Rendering/Components/SpotLightComponent.h"
#include "Runtime/Rendering/Components/RectLightComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/ShaderCode/CommonShaderCode.h"
#include "Runtime/Rendering/ShaderCode/ForwardShader.h"
#include "Runtime/UI/UIRenderer.h"

#include <unordered_set>
#include "Runtime/ECS/Components/TransformComponent.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    namespace
    {
        static std::string ResolvePPVReferenceName(const World& world)
        {
            // 첫 번째 활성화된 PostProcessVolumeComponent의 referenceObjectName 사용
            for (const auto& [entityId, volume] : world.GetComponents<PostProcessVolumeComponent>())
            {
                if (volume.useReferenceObject && !volume.referenceObjectName.empty())
                {
                    return volume.referenceObjectName;
                }
            }
            return {};
        }

        // 인스턴싱 배치 키 (재질/메시 기준)
        struct InstancedDrawKey
        {
            ID3D11Buffer* vertexBuffer = nullptr;
            ID3D11Buffer* indexBuffer = nullptr;
            UINT stride = 0;
            UINT startIndex = 0;
            UINT indexCount = 0;
            INT baseVertex = 0;
            ID3D11ShaderResourceView* diffuseSRV = nullptr;
            ID3D11ShaderResourceView* normalSRV = nullptr;
            DirectX::XMFLOAT4 color { 1.0f, 1.0f, 1.0f, 1.0f };
            float roughness = 0.5f;
            float metalness = 0.0f;
            float ambientOcclusion = 1.0f;
            float envDiffuseStrength = 1.0f;
            float envSpecularStrength = 1.0f;
            float normalStrength = 1.0f;
            DirectX::XMFLOAT4 toonPbrCuts { 0.2f, 0.5f, 0.95f, 1.0f };
            DirectX::XMFLOAT4 toonPbrLevels { 0.1f, 0.4f, 0.7f, 0.0f };
            int shadingMode = 0;
            int useTexture = 0;
            int enableNormalMap = 0;
            bool reversedWinding = false;

            bool operator<(const InstancedDrawKey& rhs) const
            {
                if (vertexBuffer != rhs.vertexBuffer) return vertexBuffer < rhs.vertexBuffer;
                if (indexBuffer != rhs.indexBuffer) return indexBuffer < rhs.indexBuffer;
                if (stride != rhs.stride) return stride < rhs.stride;
                if (startIndex != rhs.startIndex) return startIndex < rhs.startIndex;
                if (indexCount != rhs.indexCount) return indexCount < rhs.indexCount;
                if (baseVertex != rhs.baseVertex) return baseVertex < rhs.baseVertex;
                if (diffuseSRV != rhs.diffuseSRV) return diffuseSRV < rhs.diffuseSRV;
                if (normalSRV != rhs.normalSRV) return normalSRV < rhs.normalSRV;

                if (color.x != rhs.color.x) return color.x < rhs.color.x;
                if (color.y != rhs.color.y) return color.y < rhs.color.y;
                if (color.z != rhs.color.z) return color.z < rhs.color.z;
                if (color.w != rhs.color.w) return color.w < rhs.color.w;

                if (roughness != rhs.roughness) return roughness < rhs.roughness;
                if (metalness != rhs.metalness) return metalness < rhs.metalness;
            if (ambientOcclusion != rhs.ambientOcclusion) return ambientOcclusion < rhs.ambientOcclusion;
            if (envDiffuseStrength != rhs.envDiffuseStrength) return envDiffuseStrength < rhs.envDiffuseStrength;
            if (envSpecularStrength != rhs.envSpecularStrength) return envSpecularStrength < rhs.envSpecularStrength;
            if (normalStrength != rhs.normalStrength) return normalStrength < rhs.normalStrength;
                if (toonPbrCuts.x != rhs.toonPbrCuts.x) return toonPbrCuts.x < rhs.toonPbrCuts.x;
                if (toonPbrCuts.y != rhs.toonPbrCuts.y) return toonPbrCuts.y < rhs.toonPbrCuts.y;
                if (toonPbrCuts.z != rhs.toonPbrCuts.z) return toonPbrCuts.z < rhs.toonPbrCuts.z;
                if (toonPbrCuts.w != rhs.toonPbrCuts.w) return toonPbrCuts.w < rhs.toonPbrCuts.w;
                if (toonPbrLevels.x != rhs.toonPbrLevels.x) return toonPbrLevels.x < rhs.toonPbrLevels.x;
                if (toonPbrLevels.y != rhs.toonPbrLevels.y) return toonPbrLevels.y < rhs.toonPbrLevels.y;
                if (toonPbrLevels.z != rhs.toonPbrLevels.z) return toonPbrLevels.z < rhs.toonPbrLevels.z;
                if (toonPbrLevels.w != rhs.toonPbrLevels.w) return toonPbrLevels.w < rhs.toonPbrLevels.w;
                if (shadingMode != rhs.shadingMode) return shadingMode < rhs.shadingMode;
                if (useTexture != rhs.useTexture) return useTexture < rhs.useTexture;
                if (enableNormalMap != rhs.enableNormalMap) return enableNormalMap < rhs.enableNormalMap;
                if (reversedWinding != rhs.reversedWinding) return reversedWinding < rhs.reversedWinding;

                return false;
            }
        };

        bool IsSameInstancedKey(const InstancedDrawKey& a, const InstancedDrawKey& b)
        {
            if (a.vertexBuffer != b.vertexBuffer) return false;
            if (a.indexBuffer != b.indexBuffer) return false;
            if (a.stride != b.stride) return false;
            if (a.startIndex != b.startIndex) return false;
            if (a.indexCount != b.indexCount) return false;
            if (a.baseVertex != b.baseVertex) return false;
            if (a.diffuseSRV != b.diffuseSRV) return false;
            if (a.normalSRV != b.normalSRV) return false;
            if (a.color.x != b.color.x) return false;
            if (a.color.y != b.color.y) return false;
            if (a.color.z != b.color.z) return false;
            if (a.color.w != b.color.w) return false;
            if (a.roughness != b.roughness) return false;
            if (a.metalness != b.metalness) return false;
            if (a.ambientOcclusion != b.ambientOcclusion) return false;
            if (a.envDiffuseStrength != b.envDiffuseStrength) return false;
            if (a.envSpecularStrength != b.envSpecularStrength) return false;
            if (a.normalStrength != b.normalStrength) return false;
            if (a.toonPbrCuts.x != b.toonPbrCuts.x) return false;
            if (a.toonPbrCuts.y != b.toonPbrCuts.y) return false;
            if (a.toonPbrCuts.z != b.toonPbrCuts.z) return false;
            if (a.toonPbrCuts.w != b.toonPbrCuts.w) return false;
            if (a.toonPbrLevels.x != b.toonPbrLevels.x) return false;
            if (a.toonPbrLevels.y != b.toonPbrLevels.y) return false;
            if (a.toonPbrLevels.z != b.toonPbrLevels.z) return false;
            if (a.toonPbrLevels.w != b.toonPbrLevels.w) return false;
            if (a.shadingMode != b.shadingMode) return false;
            if (a.useTexture != b.useTexture) return false;
            if (a.enableNormalMap != b.enableNormalMap) return false;
            if (a.reversedWinding != b.reversedWinding) return false;
            return true;
        }

        inline DirectX::XMFLOAT4 DefaultToonPbrCuts()
        {
            return DirectX::XMFLOAT4(0.2f, 0.5f, 0.95f, 1.0f);
        }

        inline DirectX::XMFLOAT4 DefaultToonPbrLevels()
        {
            return DirectX::XMFLOAT4(0.1f, 0.4f, 0.7f, 0.0f);
        }

        // 인스턴스 월드 행렬(3x4) 생성용 헬퍼
        InstanceData BuildInstanceData(const DirectX::XMMATRIX& world)
        {
            InstanceData data{};
            DirectX::XMMATRIX worldT = DirectX::XMMatrixTranspose(world);
            DirectX::XMStoreFloat4(&data.worldRow0, worldT.r[0]);
            DirectX::XMStoreFloat4(&data.worldRow1, worldT.r[1]);
            DirectX::XMStoreFloat4(&data.worldRow2, worldT.r[2]);
            return data;
        }

        // 단일 본(Identity)인지 확인하는 함수
        bool IsIdentityBoneMatrix(const DirectX::XMFLOAT4X4& m)
        {
            const float eps = 1e-4f;
            if (std::fabs(m._11 - 1.0f) > eps) return false;
            if (std::fabs(m._22 - 1.0f) > eps) return false;
            if (std::fabs(m._33 - 1.0f) > eps) return false;
            if (std::fabs(m._44 - 1.0f) > eps) return false;

            if (std::fabs(m._12) > eps) return false;
            if (std::fabs(m._13) > eps) return false;
            if (std::fabs(m._14) > eps) return false;
            if (std::fabs(m._21) > eps) return false;
            if (std::fabs(m._23) > eps) return false;
            if (std::fabs(m._24) > eps) return false;
            if (std::fabs(m._31) > eps) return false;
            if (std::fabs(m._32) > eps) return false;
            if (std::fabs(m._34) > eps) return false;
            if (std::fabs(m._41) > eps) return false;
            if (std::fabs(m._42) > eps) return false;
            if (std::fabs(m._43) > eps) return false;

            return true;
        }

        // 본 없는(Identity 1개) FBX 여부 판단
        bool IsRigidSkinnedCommand(const SkinnedDrawCommand& cmd)
        {
            if (!cmd.bones || cmd.boneCount != 1)
                return false;

            return IsIdentityBoneMatrix(cmd.bones[0]);
        }
    }


    ForwardRenderSystem::ForwardRenderSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

    bool ForwardRenderSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        ALICE_LOG_INFO("ForwardRenderSystem::Initialize: begin (width=%u, height=%u)", width, height);

        if (!m_device || !m_context)
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: invalid device/context.");
            return false;
        }
        if (!CreateSceneRenderTarget(width, height))
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSceneRenderTarget failed.");
            return false;
        }
        if (!CreateShadowMapResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateShadowMapResources failed.");
            return false;
        }
        if (!CreateCubeGeometry())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateCubeGeometry failed.");
            return false;
        }
        if (!CreateShadersAndInputLayout())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateShadersAndInputLayout failed.");
            return false;
        }
        if (!CreateSkinnedResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSkinnedResources failed.");
            return false;
        }
        if (!CreateConstantBuffers())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateConstantBuffers failed.");
            return false;
        }
        if (!CreateTextures())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateTextures failed.");
            return false;
        }
        if(!CreateBlendStates())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateBlendStates failed.");
            return false;
		}
        if (!CreateSamplerState())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSamplerState failed.");
            return false;
        }
        if (!CreateRasterizerStates())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateRasterizerStates failed.");
            return false;
        }
        if (!CreateDepthStencilStates())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateDepthStencilStates failed.");
            return false;
        }
        if (!CreateInstanceBuffer(2048))
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateInstanceBuffer failed.");
            return false;
        }
        if (!CreateSkyboxResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateSkyboxResources failed.");
            return false;
        }
        if (!CreateIblResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateIblResources failed.");
            return false;
        }
        if (!CreateToneMappingResources())
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::Initialize: CreateToneMappingResources failed.");
            return false;
        }
        ALICE_LOG_INFO("ForwardRenderSystem::Initialize: success.");
        return true;
    }

    void ForwardRenderSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device) return;
        if (width == 0 || height == 0) return;
        // 기존 리소스 해제 후 새로 생성
        m_sceneColorTex.Reset();
        m_sceneRTV.Reset();
        m_sceneSRV.Reset();
        m_viewportTex.Reset();
        m_viewportRTV.Reset();
        m_viewportSRV.Reset();
        m_sceneDepthTex.Reset();
        m_sceneDSV.Reset();

        CreateSceneRenderTarget(width, height);
    }

    bool ForwardRenderSystem::CreateSceneRenderTarget(std::uint32_t width, std::uint32_t height)
    {
        m_sceneWidth = width; m_sceneHeight = height;
        if (width == 0 || height == 0) return false;

        // 1. Scene Color Texture & Views (RTV, SRV) - HDR 포맷: 톤매핑을 위해 R16G16B16A16_FLOAT 사용
        // 순서: Width, Height, MipLevels, ArraySize, Format, SampleDesc{Count, Quality}, Usage, BindFlags, CPUAccess, Misc
        D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_sceneColorTex.Get(), nullptr, m_sceneRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_sceneColorTex.Get(), nullptr, m_sceneSRV.ReleaseAndGetAddressOf()))) return false;

        // 1-1. Editor Viewport Output (ToneMapped LDR)
        // - ImGui::Image 표시용 (UNORM, 감마 적용된 값이 들어갈 예정)
        D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_viewportTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_viewportTex.Get(), nullptr, m_viewportRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_viewportTex.Get(), nullptr, m_viewportSRV.ReleaseAndGetAddressOf()))) return false;

        // 2. Depth Texture & View (DSV, SRV)
        // SRV 생성을 위해 typeless 포맷 사용
        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf()))) return false;

        // DSV 설정 (D24_UNORM_S8_UINT 포맷으로 뷰 생성)
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        if (FAILED(m_device->CreateDepthStencilView(m_sceneDepthTex.Get(), &dsvDesc, m_sceneDSV.ReleaseAndGetAddressOf()))) return false;

        // Depth SRV 생성 (depth test용) - 실패해도 계속 진행 (선택적)
        D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;  // R24G8_TYPELESS의 SRV 포맷
        depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MostDetailedMip = 0;
        depthSrvDesc.Texture2D.MipLevels = 1;
        HRESULT hrDepthSRV = m_device->CreateShaderResourceView(m_sceneDepthTex.Get(), &depthSrvDesc, m_sceneDepthSRV.ReleaseAndGetAddressOf());
        if (FAILED(hrDepthSRV))
        {
            ALICE_LOG_WARN("ForwardRenderSystem::CreateSceneRenderTarget: CreateShaderResourceView(depthSRV) failed (0x%08X) - depth test disabled", (unsigned)hrDepthSRV);
            m_sceneDepthSRV.Reset();
            // 초기화는 성공으로 계속 진행 (depth test 없이 렌더링)
        }

        return true;
    }

    bool ForwardRenderSystem::CreateCameraPreviewRenderTarget(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device) return false;
        if (width == 0 || height == 0) return false;

        m_cameraPreviewWidth = width;
        m_cameraPreviewHeight = height;

        m_cameraPreviewSceneTex.Reset();
        m_cameraPreviewSceneRTV.Reset();
        m_cameraPreviewSceneSRV.Reset();
        m_cameraPreviewViewportTex.Reset();
        m_cameraPreviewViewportRTV.Reset();
        m_cameraPreviewViewportSRV.Reset();
        m_cameraPreviewDepthTex.Reset();
        m_cameraPreviewDSV.Reset();
        m_cameraPreviewDepthSRV.Reset();

        // HDR Scene Color
        D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_cameraPreviewSceneTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_cameraPreviewSceneTex.Get(), nullptr, m_cameraPreviewSceneRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_cameraPreviewSceneTex.Get(), nullptr, m_cameraPreviewSceneSRV.ReleaseAndGetAddressOf()))) return false;

        // LDR Viewport (ToneMapped)
        D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_cameraPreviewViewportTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_cameraPreviewViewportTex.Get(), nullptr, m_cameraPreviewViewportRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_cameraPreviewViewportTex.Get(), nullptr, m_cameraPreviewViewportSRV.ReleaseAndGetAddressOf()))) return false;

        // Depth
        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_cameraPreviewDepthTex.ReleaseAndGetAddressOf()))) return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        if (FAILED(m_device->CreateDepthStencilView(m_cameraPreviewDepthTex.Get(), &dsvDesc, m_cameraPreviewDSV.ReleaseAndGetAddressOf()))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MostDetailedMip = 0;
        depthSrvDesc.Texture2D.MipLevels = 1;
        HRESULT hrDepthSRV = m_device->CreateShaderResourceView(m_cameraPreviewDepthTex.Get(), &depthSrvDesc, m_cameraPreviewDepthSRV.ReleaseAndGetAddressOf());
        if (FAILED(hrDepthSRV))
        {
            m_cameraPreviewDepthSRV.Reset();
        }

        return true;
    }

    bool ForwardRenderSystem::CreateShadowMapResources()
    {
        UINT size = (UINT)m_shadowSettings.mapSizePx;

        // 1. Shadow Map Texture (Typeless)
        D3D11_TEXTURE2D_DESC tDesc = { size, size, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&tDesc, nullptr, m_shadowTex.ReleaseAndGetAddressOf()))) return false;

        // 2. DSV (Depth Write)
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { DXGI_FORMAT_D32_FLOAT, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_shadowTex.Get(), &dsvDesc, m_shadowDSV.ReleaseAndGetAddressOf()))) return false;

        // 3. SRV (Shader Read)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D, 0 };
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateShaderResourceView(m_shadowTex.Get(), &srvDesc, m_shadowSRV.ReleaseAndGetAddressOf()))) return false;

        // 4. Viewport & Sampler (PCF)
        m_shadowViewport = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };

        // Filter, AddressU/V/W, MipLODBias, MaxAniso, ComparisonFunc, BorderColor, MinLOD, MaxLOD
        D3D11_SAMPLER_DESC sDesc = { D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.0f, 1, D3D11_COMPARISON_LESS_EQUAL, {0}, 0.0f, D3D11_FLOAT32_MAX };
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_shadowSampler.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSkyboxResources()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob;

        // 1. 셰이더 컴파일 및 생성 (ErrorBlob 생략)
        if (FAILED(D3DCompile(CommonShaderCode::SkyboxVS, strlen(CommonShaderCode::SkyboxVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
        if (FAILED(D3DCompile(CommonShaderCode::SkyboxPS, strlen(CommonShaderCode::SkyboxPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_skyboxVS.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_skyboxPS.ReleaseAndGetAddressOf()))) return false;

        // 2. Depth State (LessEqual, Write Off)
        D3D11_DEPTH_STENCIL_DESC dsDesc = { TRUE, D3D11_DEPTH_WRITE_MASK_ZERO, D3D11_COMPARISON_LESS_EQUAL, FALSE, D3D11_DEFAULT_STENCIL_READ_MASK, D3D11_DEFAULT_STENCIL_WRITE_MASK, {}, {} };
        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_skyboxDepthState.ReleaseAndGetAddressOf()))) return false;

        // 3. Rasterizer State (Cull None)
        D3D11_RASTERIZER_DESC rsDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };
        if (FAILED(m_device->CreateRasterizerState(&rsDesc, m_skyboxRasterizerState.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateIblResources(const std::string& iblDir, const std::string& iblName, const std::string& iblSuffix)
    {
        if (!m_resources) return false;

        namespace fs = std::filesystem;
        // 경로 및 이름 설정 (Sample -> BakerSample, 그 외 소문자 변환)
        fs::path base = fs::path("Resource/Skybox") / iblDir;
        const std::string suffix = iblSuffix.empty() ? "HDR" : iblSuffix;
        const bool allowFallback = (suffix != "HDR");

        // Diffuse, Specular, Brdf 로드
        if (!(m_iblDiffuseSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "Diffuse" + suffix + ".dds"), m_device.Get())))
        {
            if (allowFallback)
                m_iblDiffuseSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "DiffuseHDR.dds"), m_device.Get());
            if (!m_iblDiffuseSRV)
                ALICE_LOG_WARN("Failed IBL Diffuse: %s", (base / iblName).string().c_str());
        }

        if (!(m_iblSpecularSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "Specular" + suffix + ".dds"), m_device.Get())))
        {
            if (allowFallback)
                m_iblSpecularSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "SpecularHDR.dds"), m_device.Get());
            if (!m_iblSpecularSRV)
                ALICE_LOG_WARN("Failed IBL Specular %s", (base / iblName).string().c_str());
        }

        if (!(m_iblBrdfLutSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "Brdf.dds"), m_device.Get())))
            ALICE_LOG_WARN("Failed IBL BRDF %s", (base / iblName).string().c_str());

        // Skybox Env 로드 및 상태 설정
        m_skyboxSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "Env" + suffix + ".dds"), m_device.Get());
        if (!m_skyboxSRV && allowFallback)
            m_skyboxSRV = m_resources->LoadData<ID3D11ShaderResourceView>(base / (iblName + "EnvHDR.dds"), m_device.Get());
        m_skyboxEnabled = (m_skyboxSRV != nullptr);
        if (!m_skyboxEnabled) ALICE_LOG_WARN("Failed Skybox Env");

        m_currentIblSet = iblName;
        m_currentIblSuffix = suffix;
        return true;
    }

    bool ForwardRenderSystem::SetIblSet(const std::string& iblDir, const std::string& iblName, const std::string& iblSuffix)
    {
        // 기존 리소스 해제
        m_iblDiffuseSRV.Reset();
        m_iblSpecularSRV.Reset();
        m_iblBrdfLutSRV.Reset();
        m_skyboxSRV.Reset();

        // 새 IBL 세트 로드
        return CreateIblResources(iblDir, iblName, iblSuffix);
    }

    void ForwardRenderSystem::SetSkyboxEnabled(bool enabled)
    {
        m_skyboxEnabled = enabled;
        
        // 스카이박스를 끄면 IBL도 함께 끕니다
        if (!enabled)
        {
            m_iblDiffuseSRV.Reset();
            m_iblSpecularSRV.Reset();
            m_iblBrdfLutSRV.Reset();
            m_skyboxSRV.Reset();
        }
    }

    bool ForwardRenderSystem::CreateCubeGeometry()
    {
        // 1. 큐브 데이터 정의 (Pos, Normal, UV) - 중괄호 초기화로 타입명 생략
        SimpleVertex v[] = {
            // Front (+Z)
            { {-1,-1, 1}, { 0, 0, 1}, {0,1} }, { {-1, 1, 1}, { 0, 0, 1}, {0,0} }, { { 1, 1, 1}, { 0, 0, 1}, {1,0} }, { { 1,-1, 1}, { 0, 0, 1}, {1,1} },
            // Back (-Z)
            { {-1,-1,-1}, { 0, 0,-1}, {1,1} }, { { 1,-1,-1}, { 0, 0,-1}, {0,1} }, { { 1, 1,-1}, { 0, 0,-1}, {0,0} }, { {-1, 1,-1}, { 0, 0,-1}, {1,0} },
            // Top (+Y)
            { {-1, 1,-1}, { 0, 1, 0}, {0,1} }, { { 1, 1,-1}, { 0, 1, 0}, {1,1} }, { { 1, 1, 1}, { 0, 1, 0}, {1,0} }, { {-1, 1, 1}, { 0, 1, 0}, {0,0} },
            // Bottom (-Y)
            { {-1,-1,-1}, { 0,-1, 0}, {0,1} }, { {-1,-1, 1}, { 0,-1, 0}, {0,0} }, { { 1,-1, 1}, { 0,-1, 0}, {1,0} }, { { 1,-1,-1}, { 0,-1, 0}, {1,1} },
            // Left (-X)
            { {-1,-1,-1}, {-1, 0, 0}, {1,1} }, { {-1, 1,-1}, {-1, 0, 0}, {1,0} }, { {-1, 1, 1}, {-1, 0, 0}, {0,0} }, { {-1,-1, 1}, {-1, 0, 0}, {0,1} },
            // Right (+X)
            { { 1,-1,-1}, { 1, 0, 0}, {0,1} }, { { 1,-1, 1}, { 1, 0, 0}, {0,0} }, { { 1, 1, 1}, { 1, 0, 0}, {1,0} }, { { 1, 1,-1}, { 1, 0, 0}, {1,1} }
        };

        uint16_t i[] = {
            0,1,2, 0,2,3,     4,5,6, 4,6,7,     8,9,10, 8,10,11,
            12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
        };

        m_indexCount = (UINT)std::size(i);

        // 2. Vertex Buffer 생성
        D3D11_BUFFER_DESC desc = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA data = { v, 0, 0 };

        if (FAILED(m_device->CreateBuffer(&desc, &data, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

        // 3. Index Buffer 생성 (구조체 재사용)
        desc.ByteWidth = sizeof(i);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        data.pSysMem = i;
        if (FAILED(m_device->CreateBuffer(&desc, &data, m_indexBuffer.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob;

        // 1. Vertex Shader 컴파일 및 생성
        if (FAILED(D3DCompile(ForwardShader::PhongVS, strlen(ForwardShader::PhongVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr)))
            return false;
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;


        // 2. Pixel Shader 컴파일 및 생성
        /*std::string finalPBRPS = std::string(ForwardShader::PBRPS_Part1) + "\n" + ForwardShader::PBRPS_Part2;
        if (FAILED(D3DCompile(finalPBRPS.c_str(), finalPBRPS.length(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr)))
            return false;*/


			// 1. 에러 메시지를 담을 Blob 선언
		ComPtr<ID3DBlob> errorBlob;

		// 2. 문자열 병합 (안전하게 줄바꿈 문자 추가 추천)
		std::string finalPBRPS =
			std::string(ForwardShader::PBRPS_Part1) + "\n" +
			ForwardShader::PBRPS_Part2 + "\n" +
			ForwardShader::PBRPS_Part3;


		// 3. D3DCompile 호출 (마지막 인자에 &errorBlob 전달)
		HRESULT hr = D3DCompile(
			finalPBRPS.c_str(),
			finalPBRPS.length(),
			nullptr, nullptr, nullptr,
			"main", "ps_5_0",
			0, 0,
			psBlob.GetAddressOf(),
			errorBlob.GetAddressOf() // [중요] 여기에 에러 메시지가 담깁니다.
		);

		if (FAILED(hr))
		{
			// 에러 메시지가 있다면 출력창에 띄우기
			if (errorBlob)
			{
				const char* compileErrors = (const char*)errorBlob->GetBufferPointer();
				OutputDebugStringA("--------------------------------------------------\n");
				OutputDebugStringA("[Shader Compile Error]:\n");
				OutputDebugStringA(compileErrors); // 비주얼 스튜디오 '출력' 창에서 확인 가능
				OutputDebugStringA("--------------------------------------------------\n");
			}
			return false;
		}

        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

        // 3. Input Layout 생성 (오프셋 자동 정렬: D3D11_APPEND_ALIGNED_ELEMENT)
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateConstantBuffers()
    {
        // 1. 공통 설정 (기본 정적 버퍼용)
        // 순서: ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride
        D3D11_BUFFER_DESC desc = { sizeof(CBPerObject), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

        // 2. PerObject 및 Lighting 버퍼 생성 (실패 시 즉시 반환)
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf()))) return false;

        desc.ByteWidth = sizeof(CBLighting);
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbLighting.ReleaseAndGetAddressOf()))) return false;

        // Extra lights buffer (Point/Spot/Rect)
        desc.ByteWidth = (sizeof(ExtraLightsCB) + 15) / 16 * 16;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbExtraLights.ReleaseAndGetAddressOf()))) return false;

        // 3. 스카이박스용 동적 버퍼 설정 변경 및 생성
        desc.ByteWidth = sizeof(XMMATRIX);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbSkybox.ReleaseAndGetAddressOf()))) return false;

        // 4. PostProcess 상수 버퍼 생성 (톤매핑용)
        desc.ByteWidth = sizeof(float) * 4; // exposure, maxHDRNits, padding[2]
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbPostProcess.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateSkinnedResources()
    {
        ComPtr<ID3DBlob> vsBlob;
        if (FAILED(D3DCompile(ForwardShader::SkinnedVS, strlen(ForwardShader::SkinnedVS),
            nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr))) return false;

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_skinnedVertexShader))) return false;

        // 2. 입력 레이아웃 (Color(offset 48)는 건너뛰고 UV(64)부터 매핑)
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayoutSkinned))) return false;

        // 2-1. Skinned Instanced VS 생성
        vsBlob.Reset();
        if (FAILED(D3DCompile(ForwardShader::SkinnedInstancedVS, strlen(ForwardShader::SkinnedInstancedVS),
            nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr))) return false;

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_skinnedInstancedVertexShader))) return false;

        // 2-2. Skinned Instanced Input Layout
        D3D11_INPUT_ELEMENT_DESC instancedDesc[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };

        if (FAILED(m_device->CreateInputLayout(instancedDesc, (UINT)std::size(instancedDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayoutSkinnedInstanced))) return false;

        // 3. 본 상수 버퍼 (Dynamic/WriteDiscard)
        D3D11_BUFFER_DESC bd = { sizeof(CBBones), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
        if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_cbBones.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateTextures()
    {
        if (!m_resources) return false;

        // 1. 기본 텍스처 로드
        m_diffuseSRV = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Bricks059_1K-JPG_Color.jpg", m_device.Get());
        m_normalSRV = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Bricks059_1K-JPG_NormalDX.jpg", m_device.Get());
        m_specularSRV = m_resources->LoadData<ID3D11ShaderResourceView>("Resource/Image/Bricks059_Specular.png", m_device.Get());

        if (!m_diffuseSRV || !m_normalSRV || !m_specularSRV) ALICE_LOG_WARN("[ForwardRenderSystem] Default textures incomplete.");

        // 2. Flat Normal (1x1, RGBA = 128,128,255,255) 생성
        // D3D11_TEXTURE2D_DESC를 한 줄로 초기화 (Width, Height, Mips, Array, Format, Sample(Cnt,Q), Usage, Bind, CPU, Misc)
        D3D11_TEXTURE2D_DESC desc = { 1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE };
        const uint8_t color[] = { 128, 128, 255, 255 };
        D3D11_SUBRESOURCE_DATA sd = { color, 4, 0 };

        ComPtr<ID3D11Texture2D> tex;
        // 2번째 인자에 nullptr를 넣으면 텍스처의 포맷과 전체 범위를 사용하는 기본 뷰가 생성됨
        {
            const HRESULT hr = m_device->CreateTexture2D(&desc, &sd, tex.GetAddressOf());
            if (FAILED(hr))
            {
                ALICE_LOG_WARN("[ForwardRenderSystem] FAILED to create flat normal texture. (hr=0x%08X)", static_cast<unsigned>(hr));
            }
            else
            {
                m_device->CreateShaderResourceView(tex.Get(), nullptr, m_flatNormalSRV.ReleaseAndGetAddressOf());
            }
        }

        return true;
    }

    bool ForwardRenderSystem::CreateSamplerState()
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
        samplerDesc.MaxAnisotropy = 16;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        if (FAILED(m_device->CreateSamplerState(&samplerDesc, m_samplerState.ReleaseAndGetAddressOf()))) return false;

        // Linear Sampler (톤매핑용)
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&samplerDesc, m_samplerLinear.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateBlendStates()
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;

        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        if (FAILED(m_device->CreateBlendState(&blendDesc, m_alphaBlendState.ReleaseAndGetAddressOf()))) return false;
        return true;
    }

    bool ForwardRenderSystem::CreateRasterizerStates()
    {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_BACK;
        desc.DepthClipEnable = TRUE;

        // 1. 일반 렌더링 (CCW: 기본, CW: 반전/거울)
        desc.FrontCounterClockwise = TRUE;
        desc.DepthBias = 0;
        desc.SlopeScaledDepthBias = 0.0f;
        if (FAILED(m_device->CreateRasterizerState(&desc, m_rasterizerState.ReleaseAndGetAddressOf()))) return false;

        desc.FrontCounterClockwise = FALSE;
        desc.DepthBias = 0;
        desc.SlopeScaledDepthBias = 0.0f;
        if (FAILED(m_device->CreateRasterizerState(&desc, m_rasterizerStateReversed.ReleaseAndGetAddressOf()))) return false;

        // 2. 섀도우 패스 (DepthBias 적용)
        desc.FrontCounterClockwise = TRUE;
        desc.DepthBias = 1000;
        desc.SlopeScaledDepthBias = 1.0f;
        if (FAILED(m_device->CreateRasterizerState(&desc, m_shadowRasterizerState.ReleaseAndGetAddressOf()))) return false;

        desc.FrontCounterClockwise = FALSE;
        desc.DepthBias = 1000;
        desc.SlopeScaledDepthBias = 1.0f;
        if (FAILED(m_device->CreateRasterizerState(&desc, m_shadowRasterizerStateReversed.ReleaseAndGetAddressOf()))) return false;

        // 3. 아웃라인용 Rasterizer State (Cull Front)
        // - 정점을 법선 방향으로 확장한 뒤, 뒷면(Back Face)을 그리면 원본 물체 뒤로 테두리가 나타납니다.
        desc.FrontCounterClockwise = TRUE;
        desc.DepthBias = 0;
        desc.SlopeScaledDepthBias = 0.0f;
        desc.CullMode = D3D11_CULL_FRONT;    // 앞면을 제거하고 뒷면을 그림
        if (FAILED(m_device->CreateRasterizerState(&desc, m_rsCullFront.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool ForwardRenderSystem::CreateDepthStencilStates()
    {
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dsDesc.StencilEnable = FALSE;

        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_depthStencilStateReadOnly.ReleaseAndGetAddressOf())))
        {
            return false;
        }

        return true;
    }

    bool ForwardRenderSystem::CreateInstanceBuffer(std::uint32_t initialCapacity)
    {
        if (!m_device) return false;

        m_instanceBuffer.Reset();
        m_instanceCapacity = 0;

        if (initialCapacity == 0)
            initialCapacity = 1;

        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = static_cast<UINT>(sizeof(InstanceData) * initialCapacity);
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_instanceBuffer.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ForwardRenderSystem::CreateInstanceBuffer: CreateBuffer failed. hr=0x%08X", (unsigned)hr);
            return false;
        }

        m_instanceCapacity = initialCapacity;
        return true;
    }

    bool ForwardRenderSystem::EnsureInstanceBufferCapacity(std::size_t requiredCount)
    {
        if (requiredCount == 0)
            return true;

        if (m_instanceBuffer && requiredCount <= m_instanceCapacity)
            return true;

        std::uint32_t newCapacity = (m_instanceCapacity == 0) ? 1u : m_instanceCapacity;
        while (newCapacity < requiredCount)
        {
            newCapacity *= 2u;
        }

        return CreateInstanceBuffer(newCapacity);
    }

    // 경로 문자열을 기반으로 머티리얼 전용 텍스처 SRV 를 가져오거나 생성합니다.
    // - LoadData를 통해 경로 처리, 암호화 해독, 리소스 생성이 모두 내부에서 처리됩니다.
    ID3D11ShaderResourceView* ForwardRenderSystem::GetOrCreateTexture(const std::string& path)
    {
        if (path.empty()) return nullptr;

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end()) return it->second.Get();

        if (!m_device || !m_resources) return nullptr;

        auto srv = m_resources->LoadData<ID3D11ShaderResourceView>(std::filesystem::path(path), m_device.Get());

        if (!srv)
        {
            ALICE_LOG_WARN("[ForwardRenderSystem] Texture load FAILED: \"%s\"", path.c_str());
            return nullptr;
        }

        m_textureCache.emplace(path, srv);
        ALICE_LOG_INFO("[ForwardRenderSystem] Texture loaded: \"%s\"", path.c_str());

        return srv.Get();
    }

void ForwardRenderSystem::UpdatePerObjectCB(const XMMATRIX& world,
                                            const XMMATRIX& view,
                                            const XMMATRIX& projection,
                                            const XMFLOAT4& materialColor,
                                            const float& roughness,
                                            const float& metalness,
                                            float ambientOcclusion,
                                            const bool& useTexture,
                                            const bool& enableNormalMap,
                                            int shadingMode,
                                            float normalStrength,
                                            const XMFLOAT4& toonPbrCuts,
                                            const XMFLOAT4& toonPbrLevels,
                                            float envDiffuseStrength,
                                            float envSpecularStrength,
                                            const XMFLOAT3& outlineColor,
                                            float outlineWidth)
{
        CBPerObject data = {};
        // HLSL에서 row-major로 사용할 수 있도록 전치 행렬 사용
        data.world         = XMMatrixTranspose(world);
        data.view          = XMMatrixTranspose(view);
        data.projection    = XMMatrixTranspose(projection);
        data.materialColor = materialColor;
        data.roughness     = roughness;
        data.metalness     = metalness;
    data.ambientOcclusion = ambientOcclusion;
    data.envDiffuseStrength = envDiffuseStrength;
    data.envSpecularStrength = envSpecularStrength;
    data.useTexture    = useTexture ? 1 : 0;
        data.enableNormalMap = enableNormalMap ? 1 : 0;
        data.shadingMode   = shadingMode;
        data.pad0          = 0;
        data.normalStrength = normalStrength;
        data.toonPbrCuts = toonPbrCuts;
        data.toonPbrLevels = toonPbrLevels;
        data.outlineColor  = outlineColor;
        data.outlineWidth  = outlineWidth;

        m_context->UpdateSubresource(m_cbPerObject.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
        m_context->PSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices,
                                            std::uint32_t boneCount)
    {
        if (!m_cbBones || !boneMatrices || boneCount == 0) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

        auto* cb = reinterpret_cast<CBBones*>(mapped.pData);
        cb->boneCount = (std::min)(boneCount, (uint32_t)MaxBones);

        // 유효한 본은 Transpose해서 넣고, 나머지는 Identity로 채움
        for (std::uint32_t i = 0; i < MaxBones; ++i)
        {
            if (i < cb->boneCount) cb->bones[i] = XMMatrixTranspose(XMLoadFloat4x4(&boneMatrices[i]));
            else cb->bones[i] = XMMatrixIdentity();
        }

        m_context->Unmap(m_cbBones.Get(), 0);
        m_context->VSSetConstantBuffers(2, 1, m_cbBones.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateLightingCB(const Camera& camera,
                                               int shadingMode,
                                               bool enableFillLight,
                                               const XMMATRIX& lightViewProj)
    {
        CBLighting data = {}; // 0으로 초기화 (FillLight 미사용 시 자동 처리)

        // 방향 벡터 안전하게 정규화하는 람다
        auto GetSafeDir = [](const XMFLOAT3& val) {
            XMVECTOR v = XMLoadFloat3(&val);
            return XMVector3Equal(v, XMVectorZero()) ? XMVectorSet(0, -1, 0, 0) : XMVector3Normalize(v);
        };

        // Key Light
        XMStoreFloat3(&data.keyLight.direction, GetSafeDir(m_lightingParameters.keyDirection));

        data.keyLight.color = m_lightingParameters.diffuseColor;
        data.keyLight.intensity = m_lightingParameters.keyIntensity;

        // Fill Light (켜져 있을 때만 값 설정)
        if (enableFillLight)
        {
            XMStoreFloat3(&data.fillLight.direction, GetSafeDir(m_lightingParameters.fillDirection));
            data.fillLight.color = m_lightingParameters.diffuseColor;
            data.fillLight.intensity = m_lightingParameters.fillIntensity;
        }

        // 카메라 및 재질 (Brace Init 활용)
        data.cameraPosition = camera.GetPosition();
        const auto& diff = m_lightingParameters.diffuseColor;
        const auto& spec = m_lightingParameters.specularColor;

        data.materialDiffuse = { diff.x, diff.y, diff.z, 1.0f };
        data.materialSpecular = { spec.x, spec.y, spec.z, m_lightingParameters.shininess };

        // 섀도우 및 기타 파라미터
        data.shadingMode = shadingMode;
        data.lightViewProj = XMMatrixTranspose(lightViewProj);
        data.shadowBias = m_shadowSettings.bias;
        data.shadowMapSize = (float)m_shadowSettings.mapSizePx;
        data.shadowPcfRadius = m_shadowSettings.pcfRadius;
        data.shadowEnabled = m_shadowSettings.enabled;

        // GPU 업데이트 및 바인딩 (VS/PS 슬롯 1번)
        m_context->UpdateSubresource(m_cbLighting.Get(), 0, nullptr, &data, 0, 0);
        m_context->VSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
        m_context->PSSetConstantBuffers(1, 1, m_cbLighting.GetAddressOf());
    }

    void ForwardRenderSystem::UpdateExtraLightsCB(const World& world)
    {
        if (!m_cbExtraLights) return;

        ExtraLightsCB data = {};

        // Point lights
        for (const auto& [id, light] : world.GetComponents<PointLightComponent>())
        {
            if (!light.enabled) continue;
            if (data.pointCount >= MaxPointLights) break;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled || !tr->visible) continue;

            auto& dst = data.pointLights[data.pointCount++];
            dst.position = tr->position;
            dst.range = (std::max)(light.range, 0.01f);
            dst.color = light.color;
            dst.intensity = light.intensity;
        }

        // Spot lights
        for (const auto& [id, light] : world.GetComponents<SpotLightComponent>())
        {
            if (!light.enabled) continue;
            if (data.spotCount >= MaxSpotLights) break;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled || !tr->visible) continue;

            XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
            XMMATRIX rot = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&tr->rotation));
            XMVECTOR dirW = XMVector3Normalize(XMVector3TransformNormal(forward, rot));
            XMFLOAT3 dir{};
            XMStoreFloat3(&dir, dirW);

            float innerRad = DirectX::XMConvertToRadians((std::max)(0.0f, light.innerAngleDeg));
            float outerRad = DirectX::XMConvertToRadians((std::max)(light.innerAngleDeg, light.outerAngleDeg));

            auto& dst = data.spotLights[data.spotCount++];
            dst.position = tr->position;
            dst.range = (std::max)(light.range, 0.01f);
            dst.direction = dir;
            dst.innerCos = std::cosf(innerRad);
            dst.outerCos = std::cosf(outerRad);
            dst.color = light.color;
            dst.intensity = light.intensity;
        }

        // Rect lights
        for (const auto& [id, light] : world.GetComponents<RectLightComponent>())
        {
            if (!light.enabled) continue;
            if (data.rectCount >= MaxRectLights) break;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled || !tr->visible) continue;

            XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
            XMMATRIX rot = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&tr->rotation));
            XMVECTOR dirW = XMVector3Normalize(XMVector3TransformNormal(forward, rot));
            XMFLOAT3 dir{};
            XMStoreFloat3(&dir, dirW);

            auto& dst = data.rectLights[data.rectCount++];
            dst.position = tr->position;
            dst.range = (std::max)(light.range, 0.01f);
            dst.direction = dir;
            dst.width = (std::max)(light.width, 0.01f);
            dst.height = (std::max)(light.height, 0.01f);
            dst.color = light.color;
            dst.intensity = light.intensity;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_cbExtraLights.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &data, sizeof(ExtraLightsCB));
            m_context->Unmap(m_cbExtraLights.Get(), 0);
        }

        m_context->PSSetConstantBuffers(5, 1, m_cbExtraLights.GetAddressOf());
    }

    void ForwardRenderSystem::RenderSkybox(const Camera& camera)
    {
        // 유효성 체크
        if (!m_skyboxEnabled || !m_skyboxSRV || !m_skyboxVS || !m_skyboxPS || !m_cbSkybox) return;

        // 이전 상태 백업
        ID3D11RasterizerState* pRS = nullptr; 
        ID3D11DepthStencilState* pDS = nullptr; 
        ID3D11ShaderResourceView* pSRV = nullptr;
        ID3D11BlendState* pBS = nullptr; // 블렌드 상태 백업
        UINT ref = 0;
        float blendFactor[4] = { 0.0f };
        UINT sampleMask = 0;

        m_context->RSGetState(&pRS);
        m_context->OMGetDepthStencilState(&pDS, &ref);
        m_context->PSGetShaderResources(0, 1, &pSRV);
        m_context->OMGetBlendState(&pBS, blendFactor, &sampleMask); // 현재 블렌드 상태 저장

        // IA 및 셰이더 설정
        UINT stride = sizeof(SimpleVertex), offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);
        
        if (m_skyboxDepthState) m_context->OMSetDepthStencilState(m_skyboxDepthState.Get(), 0);
        if (m_skyboxRasterizerState) m_context->RSSetState(m_skyboxRasterizerState.Get());

        // 스카이박스는 배경과 섞이면 안 되므로 블렌딩을 끕니다. (Opaque)
        // DeferredRenderSystem과 동일한 m_ppBlendOpaque(Blend Disable) 사용
        float zeroFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), zeroFactor, 0xFFFFFFFF);

        // 행렬 계산 (Translation 제거) 및 CB 업데이트
        XMMATRIX view = camera.GetViewMatrix();
        view.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);
        XMMATRIX wvpT = XMMatrixTranspose(view * camera.GetProjectionMatrix());

        D3D11_MAPPED_SUBRESOURCE map;
        const HRESULT hr = m_context->Map(m_cbSkybox.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        if (FAILED(hr))
        {
            // 실패 시 상태 복원
            m_context->OMSetDepthStencilState(pDS, ref);
            m_context->RSSetState(pRS);
            m_context->OMSetBlendState(pBS, blendFactor, sampleMask);
            if (pSRV) pSRV->Release();
            if (pDS) pDS->Release();
            if (pRS) pRS->Release();
            if (pBS) pBS->Release();
            return;
        }

        memcpy(map.pData, &wvpT, sizeof(XMMATRIX));
        m_context->Unmap(m_cbSkybox.Get(), 0);

        // 리소스 바인딩 및 드로우
        ID3D11Buffer* cb = m_cbSkybox.Get();
        ID3D11ShaderResourceView* srv = m_skyboxSRV.Get();
        ID3D11SamplerState* sam = m_samplerState.Get();

        m_context->VSSetConstantBuffers(0, 1, &cb);
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sam);

        m_context->DrawIndexed(m_indexCount, 0, 0);

        // 상태 복원 및 릴리즈
        m_context->OMSetDepthStencilState(pDS, ref);
        m_context->RSSetState(pRS);
        m_context->OMSetBlendState(pBS, blendFactor, sampleMask); // 블렌드 상태 복원
        
        // 리소스 해제
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSRV);

        if (pSRV) pSRV->Release();
        if (pDS) pDS->Release();
        if (pRS) pRS->Release();
        if (pBS) pBS->Release();
    }

    void ForwardRenderSystem::RenderSkinnedMeshes(
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& commands,
        int shadingMode,
        bool enableFillLight,
        CXMMATRIX lightViewProj)
    {
        if (commands.empty()) return;
        if (!m_skinnedVertexShader || !m_pixelShader || !m_inputLayoutSkinned) {
            ALICE_LOG_ERRORF("[ForwardRenderSystem] Missing shaders");
            return;
        }

        // 1. 공통 파이프라인 설정
        m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_inputLayoutSkinned.Get());

        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        int lastShadingMode = shadingMode;
        UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);

        InstancedDrawKey batchKey{};
        std::vector<InstanceData> batchInstances;
        bool hasBatch = false;
        batchInstances.reserve(commands.size());

        for (const auto& cmd : commands)
        {
            if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

            // 2. 컬링 및 RS 설정 (FBX 와인딩 보정)
            // det >= 0이면 뒤집히지 않았으므로(!flipped) -> useCWFront -> Reversed State 사용
            bool isPositiveDet = XMVectorGetX(XMMatrixDeterminant(cmd.world)) >= 0.0f;
            m_context->RSSetState(isPositiveDet ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());

            float r = (cmd.roughness != 0.0f) ? cmd.roughness : m_lightingParameters.roughness;
            float m = (cmd.metalness != 0.0f) ? cmd.metalness : m_lightingParameters.metalness;
            float ao = (cmd.shadingMode >= 0) ? cmd.ambientOcclusion : m_lightingParameters.ambientOcclusion;
            const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
            // 아웃라인 파라미터
            XMFLOAT3 outlineColor = cmd.outlineColor;
            float outlineWidth = cmd.outlineWidth;
            
            // 6. 메쉬/서브셋 조회 및 렌더링
            auto mesh = (m_skinnedRegistry && !cmd.meshKey.empty()) ? m_skinnedRegistry->Find(cmd.meshKey) : nullptr;
            ID3D11ShaderResourceView* baseNormal = m_flatNormalSRV ? m_flatNormalSRV.Get() : m_normalSRV.Get();

            // 인스턴싱 조건: 본 1개(Identity) + 아웃라인 없음 + 단일 서브셋
            const bool canInstance = IsRigidSkinnedCommand(cmd) &&
                                     (outlineWidth <= 0.0f) &&
                                     (!mesh || mesh->subsets.size() <= 1) &&
                                     m_skinnedInstancedVertexShader &&
                                     m_inputLayoutSkinnedInstanced &&
                                     m_instanceBuffer;

            if (canInstance)
            {
                ID3D11ShaderResourceView* diff = nullptr;
                ID3D11ShaderResourceView* norm = nullptr;
                UINT startIndex = cmd.startIndex;
                UINT indexCount = cmd.indexCount;

                if (mesh && !mesh->subsets.empty())
                {
                    const auto& sub = mesh->subsets.front();
                    if (sub.indexCount == 0)
                    {
                        continue;
                    }
                    startIndex = sub.startIndex;
                    indexCount = sub.indexCount;
                    diff = (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : m_diffuseSRV.Get();
                    norm = (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : baseNormal;
                }
                else
                {
                    auto texSRV = GetOrCreateTexture(cmd.albedoTexturePath);
                    diff = texSRV ? texSRV : m_diffuseSRV.Get();
                    norm = baseNormal;
                }

                InstancedDrawKey key{};
                key.vertexBuffer = cmd.vertexBuffer;
                key.indexBuffer = cmd.indexBuffer;
                key.stride = cmd.stride;
                key.startIndex = startIndex;
                key.indexCount = indexCount;
                key.baseVertex = cmd.baseVertex;
                key.diffuseSRV = diff;
                key.normalSRV = norm;
                key.color = XMFLOAT4(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha);
                key.roughness = r;
                key.metalness = m;
                key.ambientOcclusion = ao;
                key.envDiffuseStrength = cmd.envDiffuseStrength;
                key.envSpecularStrength = cmd.envSpecularStrength;
                key.normalStrength = cmd.normalStrength;
                key.toonPbrCuts = cmd.toonPbrCuts;
                key.toonPbrLevels = cmd.toonPbrLevels;
                key.shadingMode = objectShadingMode;
                key.useTexture = 1;
                key.enableNormalMap = (norm != nullptr) ? 1 : 0;
                key.reversedWinding = isPositiveDet;

                // 배치 키가 바뀌면 이전 배치 플러시
                if (hasBatch && (!IsSameInstancedKey(batchKey, key) || batchInstances.size() >= m_instanceCapacity))
                {
                    if (EnsureInstanceBufferCapacity(batchInstances.size()))
                    {
                        if (batchKey.shadingMode != lastShadingMode)
                        {
                            UpdateLightingCB(camera, batchKey.shadingMode, enableFillLight, lightViewProj);
                            lastShadingMode = batchKey.shadingMode;
                        }

                        m_context->RSSetState(batchKey.reversedWinding ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());
                        m_context->VSSetShader(m_skinnedInstancedVertexShader.Get(), nullptr, 0);
                        m_context->IASetInputLayout(m_inputLayoutSkinnedInstanced.Get());

                        D3D11_MAPPED_SUBRESOURCE mapped{};
                        if (SUCCEEDED(m_context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                        {
                            std::memcpy(mapped.pData, batchInstances.data(), sizeof(InstanceData) * batchInstances.size());
                            m_context->Unmap(m_instanceBuffer.Get(), 0);
                        }

                        UINT strides[2] = { batchKey.stride, sizeof(InstanceData) };
                        UINT offsets[2] = { 0, 0 };
                        ID3D11Buffer* bufs[2] = { batchKey.vertexBuffer, m_instanceBuffer.Get() };
                        m_context->IASetVertexBuffers(0, 2, bufs, strides, offsets);
                        m_context->IASetIndexBuffer(batchKey.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                        ID3D11ShaderResourceView* srvs[] = {
                            batchKey.diffuseSRV, batchKey.normalSRV, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                            m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                        };
                        m_context->PSSetShaderResources(0, 8, srvs);

                        UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj,
                                          batchKey.color, batchKey.roughness, batchKey.metalness, batchKey.ambientOcclusion,
                                          true, (batchKey.enableNormalMap != 0),
                                          batchKey.shadingMode, batchKey.normalStrength,
                                           batchKey.toonPbrCuts, batchKey.toonPbrLevels,
                                           batchKey.envDiffuseStrength, batchKey.envSpecularStrength,
                                           XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                        m_context->DrawIndexedInstanced(batchKey.indexCount, (UINT)batchInstances.size(),
                                                        batchKey.startIndex, batchKey.baseVertex, 0);
                    }

                    batchInstances.clear();
                    hasBatch = false;

                    // 일반 스키닝 렌더링을 위해 파이프라인 복구
                    m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
                    m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
                }

                if (!hasBatch)
                {
                    batchKey = key;
                    hasBatch = true;
                }

                batchInstances.push_back(BuildInstanceData(cmd.world));
                continue;
            }

            // 인스턴싱 배치가 있다면 먼저 플러시
            if (hasBatch && !batchInstances.empty())
            {
                if (EnsureInstanceBufferCapacity(batchInstances.size()))
                {
                    if (batchKey.shadingMode != lastShadingMode)
                    {
                        UpdateLightingCB(camera, batchKey.shadingMode, enableFillLight, lightViewProj);
                        lastShadingMode = batchKey.shadingMode;
                    }

                    m_context->RSSetState(batchKey.reversedWinding ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());
                    m_context->VSSetShader(m_skinnedInstancedVertexShader.Get(), nullptr, 0);
                    m_context->IASetInputLayout(m_inputLayoutSkinnedInstanced.Get());

                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (SUCCEEDED(m_context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                    {
                        std::memcpy(mapped.pData, batchInstances.data(), sizeof(InstanceData) * batchInstances.size());
                        m_context->Unmap(m_instanceBuffer.Get(), 0);
                    }

                    UINT strides[2] = { batchKey.stride, sizeof(InstanceData) };
                    UINT offsets[2] = { 0, 0 };
                    ID3D11Buffer* bufs[2] = { batchKey.vertexBuffer, m_instanceBuffer.Get() };
                    m_context->IASetVertexBuffers(0, 2, bufs, strides, offsets);
                    m_context->IASetIndexBuffer(batchKey.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                    ID3D11ShaderResourceView* srvs[] = {
                        batchKey.diffuseSRV, batchKey.normalSRV, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                        m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                    };
                    m_context->PSSetShaderResources(0, 8, srvs);

                    UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj,
                                      batchKey.color, batchKey.roughness, batchKey.metalness, batchKey.ambientOcclusion,
                                      true, (batchKey.enableNormalMap != 0),
                                      batchKey.shadingMode, batchKey.normalStrength,
                                        batchKey.toonPbrCuts, batchKey.toonPbrLevels,
                                        batchKey.envDiffuseStrength, batchKey.envSpecularStrength,
                                        XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                    m_context->DrawIndexedInstanced(batchKey.indexCount, (UINT)batchInstances.size(),
                                                    batchKey.startIndex, batchKey.baseVertex, 0);
                }

                batchInstances.clear();
                hasBatch = false;

                m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
                m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
            }

            // 3. 버퍼 설정 (일반 스키닝)
            UINT stride = cmd.stride, offset = 0;
            ID3D11Buffer* vb = cmd.vertexBuffer;
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            // 4. 상수 버퍼 업데이트
            UpdateBonesCB(cmd.bones, cmd.boneCount);

            if (objectShadingMode != lastShadingMode)
            {
                UpdateLightingCB(camera, objectShadingMode, enableFillLight, lightViewProj);
                lastShadingMode = objectShadingMode;
            }

            if (mesh && !mesh->subsets.empty())
            {
                for (const auto& sub : mesh->subsets)
                {
                    if (sub.indexCount == 0) continue;
                    auto diff = (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : m_diffuseSRV.Get();
                    auto norm = (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : baseNormal;

                    ID3D11ShaderResourceView* srvs[] = {
                    diff, norm, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                    m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                    };
                    m_context->PSSetShaderResources(0, 8, srvs);
                    
                    // [Pass 1] 원본
                    UpdatePerObjectCB(cmd.world, view, proj,
                        XMFLOAT4(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha), r, m, ao, true, (m_flatNormalSRV != nullptr),
                        objectShadingMode, cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                        cmd.envDiffuseStrength, cmd.envSpecularStrength,
                        outlineColor, 0.0f);
                    m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                    
                    // [Pass 2] 아웃라인
                    if (outlineWidth > 0.0f)
                    {
                        m_context->RSSetState(m_rsCullFront.Get());
                        UpdatePerObjectCB(cmd.world, view, proj,
                            XMFLOAT4(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha), r, m, ao, true, (m_flatNormalSRV != nullptr),
                            objectShadingMode, cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                            cmd.envDiffuseStrength, cmd.envSpecularStrength,
                            outlineColor, outlineWidth);
                        m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                        // 상태 복구
                        m_context->RSSetState(isPositiveDet ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());
                    }
                }
            }
            else
            {
                auto texSRV = GetOrCreateTexture(cmd.albedoTexturePath);
                ID3D11ShaderResourceView* srvs[] = {
                    texSRV ? texSRV : m_diffuseSRV.Get(), baseNormal, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                    m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                };
                m_context->PSSetShaderResources(0, 8, srvs);
                
                // [Pass 1] 원본
                UpdatePerObjectCB(cmd.world, view, proj,
                    XMFLOAT4(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha), r, m, ao, true, (m_flatNormalSRV != nullptr),
                    objectShadingMode, cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                    cmd.envDiffuseStrength, cmd.envSpecularStrength,
                    outlineColor, 0.0f);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                
                // [Pass 2] 아웃라인
                if (outlineWidth > 0.0f)
                {
                    m_context->RSSetState(m_rsCullFront.Get());
                    UpdatePerObjectCB(cmd.world, view, proj,
                        XMFLOAT4(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha), r, m, ao, true, (m_flatNormalSRV != nullptr),
                        objectShadingMode, cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                        cmd.envDiffuseStrength, cmd.envSpecularStrength,
                        outlineColor, outlineWidth);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                    // 상태 복구
                    m_context->RSSetState(isPositiveDet ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());
                }
            }
        }

        // 마지막 배치 플러시
        if (hasBatch && !batchInstances.empty())
        {
            if (EnsureInstanceBufferCapacity(batchInstances.size()))
            {
                if (batchKey.shadingMode != lastShadingMode)
                {
                    UpdateLightingCB(camera, batchKey.shadingMode, enableFillLight, lightViewProj);
                    lastShadingMode = batchKey.shadingMode;
                }

                m_context->RSSetState(batchKey.reversedWinding ? m_rasterizerStateReversed.Get() : m_rasterizerState.Get());
                m_context->VSSetShader(m_skinnedInstancedVertexShader.Get(), nullptr, 0);
                m_context->IASetInputLayout(m_inputLayoutSkinnedInstanced.Get());

                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(m_context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    std::memcpy(mapped.pData, batchInstances.data(), sizeof(InstanceData) * batchInstances.size());
                    m_context->Unmap(m_instanceBuffer.Get(), 0);
                }

                UINT strides[2] = { batchKey.stride, sizeof(InstanceData) };
                UINT offsets[2] = { 0, 0 };
                ID3D11Buffer* bufs[2] = { batchKey.vertexBuffer, m_instanceBuffer.Get() };
                m_context->IASetVertexBuffers(0, 2, bufs, strides, offsets);
                m_context->IASetIndexBuffer(batchKey.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                ID3D11ShaderResourceView* srvs[] = {
                    batchKey.diffuseSRV, batchKey.normalSRV, m_specularSRV.Get(), m_skyboxSRV.Get(), m_shadowSRV.Get(),
                    m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
                };
                m_context->PSSetShaderResources(0, 8, srvs);

                UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj,
                                  batchKey.color, batchKey.roughness, batchKey.metalness, batchKey.ambientOcclusion,
                                  true, (batchKey.enableNormalMap != 0),
                                  batchKey.shadingMode, batchKey.normalStrength,
                                    batchKey.toonPbrCuts, batchKey.toonPbrLevels,
                                    batchKey.envDiffuseStrength, batchKey.envSpecularStrength,
                                    XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                m_context->DrawIndexedInstanced(batchKey.indexCount, (UINT)batchInstances.size(),
                                                batchKey.startIndex, batchKey.baseVertex, 0);
            }

            batchInstances.clear();
            hasBatch = false;

            m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
            m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
        }
        if (lastShadingMode != shadingMode)
        {
            UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);
        }
    }

    XMMATRIX ForwardRenderSystem::BuildWorldMatrix(const TransformComponent& transform) const
    {
        XMVECTOR scale = XMLoadFloat3(&transform.scale);
        XMVECTOR rotation = XMLoadFloat3(&transform.rotation);
        XMVECTOR translation = XMLoadFloat3(&transform.position);

        XMMATRIX S = XMMatrixScalingFromVector(scale);
        XMMATRIX R = XMMatrixRotationRollPitchYawFromVector(rotation);
        XMMATRIX T = XMMatrixTranslationFromVector(translation);

        // DirectXMath 행벡터 컨벤션: S * R * T
        return S * R * T;
    }

    XMMATRIX ForwardRenderSystem::BuildWorldMatrix(const World& world, EntityId entityId, const TransformComponent& transform) const
    {
        // c.txt 참조: 부모부터 루트까지 로컬 행렬을 스택에 쌓고, 루트에서 자식으로 내려가면서 행렬 곱하기
        std::vector<XMMATRIX> matrixStack;
        EntityId currentId = entityId;
        
        // 부모부터 루트까지 로컬 행렬을 스택에 쌓음
        while (currentId != InvalidEntityId)
        {
            const TransformComponent* t = world.GetComponent<TransformComponent>(currentId);
            if (t)
            {
                XMVECTOR scale = XMLoadFloat3(&t->scale);
                XMVECTOR rotation = XMLoadFloat3(&t->rotation);
                XMVECTOR translation = XMLoadFloat3(&t->position);
                
                // 로컬 행렬: S * R * T 순서 (DirectXMath 행벡터 컨벤션)
                XMMATRIX localMatrix = XMMatrixScalingFromVector(scale) *
                    XMMatrixRotationRollPitchYawFromVector(rotation) *
                    XMMatrixTranslationFromVector(translation);
                
                matrixStack.push_back(localMatrix);
                currentId = t->parent;
            }
            else
            {
                break;
            }
        }
        
        // 행벡터 컨벤션: child * parent * ... * root 형태로 곱하기 (정순)
        XMMATRIX worldMatrix = XMMatrixIdentity();
        for (const auto& m : matrixStack)  // child -> parent -> root 순서
        {
            worldMatrix = worldMatrix * m;  // I * child * parent * ... * root
        }
        
        return worldMatrix;
    }

    XMMATRIX ForwardRenderSystem::RenderShadowPass(const World& world, const std::vector<SkinnedDrawCommand>& skinnedCommands, const std::unordered_set<EntityId>& cameraEntities)
    {
        if (!m_sceneRTV || !m_sceneDSV) return XMMatrixIdentity();

        // 1. 조명 방향 설정
        XMVECTOR lightDir = XMLoadFloat3(&m_lightingParameters.keyDirection);
        if (XMVector3Equal(lightDir, XMVectorZero())) lightDir = XMVectorSet(0.5f, -1.0f, 0.5f, 0.0f);
        lightDir = XMVector3Normalize(lightDir);

        // 2. 씬 바운딩 박스 계산 (Set을 이용한 O(1) 제외 처리)
        XMFLOAT3 minP{ FLT_MAX, FLT_MAX, FLT_MAX };
        XMFLOAT3 maxP{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool hasObjects = false;

        const auto& transforms = world.GetComponents<TransformComponent>();
        for (const auto& [id, tr] : transforms)
        {
            if (cameraEntities.contains(id)) continue;
            if (!tr.enabled || !tr.visible) continue;
            const bool hasSkinned = (world.GetComponent<SkinnedMeshComponent>(id) != nullptr);
            const bool hasMaterial = (world.GetComponent<MaterialComponent>(id) != nullptr);
            if (!hasSkinned && !hasMaterial) continue;

            hasObjects = true;
            minP.x = (std::min)(minP.x, tr.position.x); minP.y = (std::min)(minP.y, tr.position.y); minP.z = (std::min)(minP.z, tr.position.z);
            maxP.x = (std::max)(maxP.x, tr.position.x); maxP.y = (std::max)(maxP.y, tr.position.y); maxP.z = (std::max)(maxP.z, tr.position.z);
        }

        // 오브젝트가 없으면 기본값 처리
        if (!hasObjects)
        {
            minP = { -10.0f, -10.0f, -10.0f };
            maxP = { 10.0f, 10.0f, 10.0f };
        }

        // 3. 씬의 중심(Focus) 계산
        XMVECTOR vMin = XMLoadFloat3(&minP);
        XMVECTOR vMax = XMLoadFloat3(&maxP);
        XMVECTOR focus = (vMin + vMax) * 0.5f;

        // 4. 그림자 범위(Radius)를 더 크게 만듬
        // 씬의 대각선 길이를 구해서 회전해도 잘리지 않도록 함
        XMVECTOR diagonal = XMVector3Length(vMax - vMin);
        float sceneRadius = XMVectorGetX(diagonal) * 0.5f;

        // 설정값과 계산된 반지름 중 큰 값을 사용하고, 추가 여유분(Multiplier)을 줌
        float r = (std::max)(m_shadowSettings.orthoRadius, sceneRadius);
        r *= 1.5f; // 1.5배 여유를 둬서 경계면 그림자 잘림을 방지

        // 5. 뷰 행렬 생성 (가상의 광원 위치를 멀리 이동)
        // 조명을 반지름의 3배만큼 뒤로 당겨서, 중심 앞쪽의 물체도 Near Plane에 안 잘리게 함
        float distFromCenter = r * 3.0f;
        XMVECTOR lightPos = focus - lightDir * distFromCenter;

        // Up 벡터 보정
        XMVECTOR up = (fabsf(XMVectorGetX(XMVector3Dot(XMVectorSet(0, 1, 0, 0), lightDir))) > 0.99f)
            ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
        XMMATRIX lightView = XMMatrixLookToLH(lightPos, lightDir, up);

        // 6. 투영 행렬 생성 (Z 범위 대폭 확장)
        // Near는 0에 가깝게, Far는 광원 거리 + 반지름 뒤쪽까지 커버
        float nearZ = 0.01f;
        float farZ = distFromCenter + r * 2.0f; // Far Plane을 충분히 깊게 설정

        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-r, r, -r, r, nearZ, farZ);

        // 7. 텍셀 스냅 (Texel Snapping) - 깜빡임 방지
        XMVECTOR focusLS = XMVector3TransformCoord(focus, lightView);
        float texelWorld = (2.0f * r) / static_cast<float>(m_shadowSettings.mapSizePx);

        float snapX = floorf(XMVectorGetX(focusLS) / texelWorld) * texelWorld;
        float snapY = floorf(XMVectorGetY(focusLS) / texelWorld) * texelWorld;

        // 스냅 적용을 위해 뷰 행렬 미세 조정
        lightView = XMMatrixTranslation(snapX - XMVectorGetX(focusLS), snapY - XMVectorGetY(focusLS), 0.0f) * lightView;

        XMMATRIX lightViewProj = lightView * lightProj;

        // --- 렌더링 파이프라인 설정 ---
        if (m_shadowDSV)
        {
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_context->PSSetShaderResources(4, 1, nullSRV);

            m_context->RSSetViewports(1, &m_shadowViewport);
            m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
            m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            m_context->IASetInputLayout(m_inputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            m_context->PSSetShader(nullptr, nullptr, 0);

            if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

            // 정적 메시 그리기
            UINT stride = sizeof(SimpleVertex), offset = 0;
            ID3D11Buffer* vb = m_vertexBuffer.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

            for (const auto& [id, transform] : transforms)
            {
                if (cameraEntities.contains(id)) continue;
                if (world.GetComponent<SkinnedMeshComponent>(id)) continue;
                if (!world.GetComponent<MaterialComponent>(id)) continue;
                if (!transform.enabled || !transform.visible) continue;

                XMMATRIX worldM = BuildWorldMatrix(world, id, transform);

                // 그림자 맵은 보통 Back-Face Culling을 하거나, Peter Panning 방지를 위해 Front-Face Culling을 하기도 함
                // 설정에 따라 상태 변경
                bool flipped = XMVectorGetX(XMMatrixDeterminant(worldM)) < 0.0f;
                if (flipped && m_shadowRasterizerStateReversed)
                    m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                else if (m_shadowRasterizerState)
                    m_context->RSSetState(m_shadowRasterizerState.Get());

                XMFLOAT4 dummy(1, 1, 1, 1);
                UpdatePerObjectCB(worldM, lightView, lightProj, dummy, 1, 0, 1.0f, false, false, 0,
                                   1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                   1.0f, 1.0f,
                                   XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                //m_context->DrawIndexed(m_indexCount, 0, 0);
            }

            // 스키닝 메시 그리기
            if (!skinnedCommands.empty() && m_skinnedVertexShader && m_inputLayoutSkinned)
            {
                // Shadow 전용 인스턴싱 배치 키
                struct ShadowInstancedKey
                {
                    ID3D11Buffer* vertexBuffer = nullptr;
                    ID3D11Buffer* indexBuffer = nullptr;
                    UINT stride = 0;
                    UINT startIndex = 0;
                    UINT indexCount = 0;
                    INT baseVertex = 0;

                    bool operator<(const ShadowInstancedKey& rhs) const
                    {
                        if (vertexBuffer != rhs.vertexBuffer) return vertexBuffer < rhs.vertexBuffer;
                        if (indexBuffer != rhs.indexBuffer) return indexBuffer < rhs.indexBuffer;
                        if (stride != rhs.stride) return stride < rhs.stride;
                        if (startIndex != rhs.startIndex) return startIndex < rhs.startIndex;
                        if (indexCount != rhs.indexCount) return indexCount < rhs.indexCount;
                        if (baseVertex != rhs.baseVertex) return baseVertex < rhs.baseVertex;
                        return false;
                    }
                };

                struct ShadowInstancedItem
                {
                    ShadowInstancedKey key;
                    InstanceData instance;
                    bool operator<(const ShadowInstancedItem& rhs) const { return key < rhs.key; }
                };

                std::vector<ShadowInstancedItem> instancedItems;
                instancedItems.reserve(skinnedCommands.size());

                m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
                m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);

                for (const auto& cmd : skinnedCommands)
                {
                    if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

                    if (IsRigidSkinnedCommand(cmd) &&
                        m_skinnedInstancedVertexShader &&
                        m_inputLayoutSkinnedInstanced &&
                        m_instanceBuffer)
                    {
                        ShadowInstancedItem item{};
                        item.key.vertexBuffer = cmd.vertexBuffer;
                        item.key.indexBuffer = cmd.indexBuffer;
                        item.key.stride = cmd.stride;
                        item.key.startIndex = cmd.startIndex;
                        item.key.indexCount = cmd.indexCount;
                        item.key.baseVertex = cmd.baseVertex;
                        item.instance = BuildInstanceData(cmd.world);
                        instancedItems.push_back(item);
                        continue;
                    }

                    // 일반 스키닝 렌더링
                    UINT sStride = cmd.stride;
                    m_context->IASetVertexBuffers(0, 1, &cmd.vertexBuffer, &sStride, &offset);
                    m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                    UpdateBonesCB(cmd.bones, cmd.boneCount);
                    XMFLOAT4 dummy(1, 1, 1, 1);
                    UpdatePerObjectCB(cmd.world, lightView, lightProj, dummy, 1, 0, 1.0f, false, false, 0,
                                       1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                       1.0f, 1.0f,
                                       XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                }

                if (!instancedItems.empty())
                {
                    std::sort(instancedItems.begin(), instancedItems.end());

                    if (EnsureInstanceBufferCapacity(instancedItems.size()))
                    {
                        m_context->IASetInputLayout(m_inputLayoutSkinnedInstanced.Get());
                        m_context->VSSetShader(m_skinnedInstancedVertexShader.Get(), nullptr, 0);

                        std::vector<InstanceData> batchInstances;
                        batchInstances.reserve(instancedItems.size());

                        ShadowInstancedKey currentKey = instancedItems.front().key;
                        batchInstances.clear();

                        for (const auto& item : instancedItems)
                        {
                            const bool sameKey = !(currentKey < item.key) && !(item.key < currentKey);

                            if (!sameKey || batchInstances.size() >= m_instanceCapacity)
                            {
                                if (!batchInstances.empty())
                                {
                                    D3D11_MAPPED_SUBRESOURCE mapped{};
                                    if (SUCCEEDED(m_context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                                    {
                                        std::memcpy(mapped.pData, batchInstances.data(), sizeof(InstanceData) * batchInstances.size());
                                        m_context->Unmap(m_instanceBuffer.Get(), 0);
                                    }

                                    UINT strides[2] = { currentKey.stride, sizeof(InstanceData) };
                                    UINT offsets[2] = { 0, 0 };
                                    ID3D11Buffer* bufs[2] = { currentKey.vertexBuffer, m_instanceBuffer.Get() };
                                    m_context->IASetVertexBuffers(0, 2, bufs, strides, offsets);
                                    m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                                    XMFLOAT4 dummy(1, 1, 1, 1);
                                    UpdatePerObjectCB(DirectX::XMMatrixIdentity(), lightView, lightProj, dummy, 1, 0, 1.0f, false, false, 0,
                                                       1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                                       1.0f, 1.0f,
                                                       XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                                    m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(), currentKey.startIndex, currentKey.baseVertex, 0);
                                }

                                currentKey = item.key;
                                batchInstances.clear();
                            }

                            batchInstances.push_back(item.instance);
                        }

                        if (!batchInstances.empty())
                        {
                            D3D11_MAPPED_SUBRESOURCE mapped{};
                            if (SUCCEEDED(m_context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                            {
                                std::memcpy(mapped.pData, batchInstances.data(), sizeof(InstanceData) * batchInstances.size());
                                m_context->Unmap(m_instanceBuffer.Get(), 0);
                            }

                            UINT strides[2] = { currentKey.stride, sizeof(InstanceData) };
                            UINT offsets[2] = { 0, 0 };
                            ID3D11Buffer* bufs[2] = { currentKey.vertexBuffer, m_instanceBuffer.Get() };
                            m_context->IASetVertexBuffers(0, 2, bufs, strides, offsets);
                            m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                            XMFLOAT4 dummy(1, 1, 1, 1);
                            UpdatePerObjectCB(DirectX::XMMatrixIdentity(), lightView, lightProj, dummy, 1, 0, 1.0f, false, false, 0,
                                               1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                               1.0f, 1.0f,
                                               XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                            m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(), currentKey.startIndex, currentKey.baseVertex, 0);
                        }

                        // 파이프라인 복구
                        m_context->IASetInputLayout(m_inputLayoutSkinned.Get());
                        m_context->VSSetShader(m_skinnedVertexShader.Get(), nullptr, 0);
                    }
                }
            }
            m_context->IASetInputLayout(m_inputLayout.Get());
        }

        return lightViewProj;
    }

    void ForwardRenderSystem::RenderMainPass(const World& world,
        const Camera& camera,
        int shadingMode,
        bool enableFillLight,
        CXMMATRIX lightViewProj)
    {
        // --- 렌더 타겟 설정 및 클리어 ---
        float clearColor[4] = { m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z, m_backgroundColor.w };

        // 뷰포트 설정
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // Shadow Map SRV(t4) 해제 (매우 중요: DSV 충돌 방지)
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(4, 1, nullSRV);

        // RTV/DSV 바인딩 및 클리어
        ID3D11RenderTargetView* rtvs[] = { m_sceneRTV.Get() };
        m_context->OMSetRenderTargets(1, rtvs, m_sceneDSV.Get());
        m_context->ClearRenderTargetView(m_sceneRTV.Get(), clearColor);
        m_context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // --- 전역 상태 및 리소스 바인딩 ---
        XMMATRIX viewM = camera.GetViewMatrix();
        XMMATRIX projM = camera.GetProjectionMatrix();
        UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);
        UpdateExtraLightsCB(world);

        // IA & Shaders
        UINT stride = sizeof(SimpleVertex), offset = 0;
        ID3D11Buffer* vb = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // SRV 바인딩 (t0 ~ t7)
        ID3D11ShaderResourceView* srvs[8] = {
            m_diffuseSRV.Get(), m_normalSRV.Get(), m_specularSRV.Get(), m_skyboxSRV.Get(),
            m_shadowSRV.Get(),  m_iblDiffuseSRV.Get(), m_iblSpecularSRV.Get(), m_iblBrdfLutSRV.Get()
        };
        m_context->PSSetShaderResources(0, 8, srvs);

        // Sampler 바인딩
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);
        ID3D11SamplerState* shadowSamplers[] = { m_shadowSampler.Get() };
        m_context->PSSetSamplers(1, 1, shadowSamplers);
		// Blend State (알파 블렌딩)
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_context->OMSetBlendState(m_alphaBlendState.Get(), blendFactor, 0xffffffff);

        // --- 정적 메시 루프 (Static Meshes) ---
        const auto& transforms = world.GetComponents<TransformComponent>();
        int lastShadingMode = shadingMode;
        for (const auto& [id, transform] : transforms)
        {
            if (world.GetComponent<SkinnedMeshComponent>(id)) continue; // 스키닝 메시는 제외
            if (!transform.enabled || !transform.visible) continue;
            const MaterialComponent* mat = world.GetComponent<MaterialComponent>(id);
            if (!mat) continue;

            XMMATRIX worldM = BuildWorldMatrix(world, id, transform);

            // Material 설정
            XMFLOAT4 color = { m_lightingParameters.baseColor.x, m_lightingParameters.baseColor.y, m_lightingParameters.baseColor.z, 1.0f };
            float rough = m_lightingParameters.roughness;
            float metal = m_lightingParameters.metalness;
            float ao = m_lightingParameters.ambientOcclusion;
            float envDiffuseStrength = 1.0f;
            float envSpecularStrength = 1.0f;
            bool useTex = false;
            float normalStrength = 1.0f;
            XMFLOAT4 toonCuts = DefaultToonPbrCuts();
            XMFLOAT4 toonLevels = DefaultToonPbrLevels();

            if (mat) {
                color = { mat->color.x, mat->color.y, mat->color.z, mat->alpha };
                rough = mat->roughness; metal = mat->metalness;
                if (mat->shadingMode >= 0)
                    ao = mat->ambientOcclusion;
                normalStrength = mat->normalStrength;
                envDiffuseStrength = mat->envDiffuseStrength;
                envSpecularStrength = mat->envSpecularStrength;
                toonCuts = XMFLOAT4(mat->toonPbrCut1, mat->toonPbrCut2, mat->toonPbrCut3, mat->toonPbrStrength);
                toonLevels = XMFLOAT4(mat->toonPbrLevel1, mat->toonPbrLevel2, mat->toonPbrLevel3,
                    mat->toonPbrBlur ? 1.0f : 0.0f);
                useTex = !mat->albedoTexturePath.empty();
            }
            const int objectShadingMode = (mat && mat->shadingMode >= 0) ? mat->shadingMode : shadingMode;
            if (objectShadingMode != lastShadingMode)
            {
                UpdateLightingCB(camera, objectShadingMode, enableFillLight, lightViewProj);
                lastShadingMode = objectShadingMode;
            }

            // 아웃라인 파라미터
            XMFLOAT3 outlineColor = mat ? mat->outlineColor : XMFLOAT3(0.0f, 0.0f, 0.0f);
            float outlineWidth = mat ? mat->outlineWidth : 0.0f;

            // Rasterizer State (Culling)
            bool flipped = XMVectorGetX(XMMatrixDeterminant(worldM)) < 0.0f;
            ID3D11RasterizerState* originalRS = nullptr;
            if (flipped && m_rasterizerStateReversed) 
            {
                originalRS = m_rasterizerStateReversed.Get();
                m_context->RSSetState(originalRS);
            }
            else if (m_rasterizerState) 
            {
                originalRS = m_rasterizerState.Get();
                m_context->RSSetState(originalRS);
            }

            bool useNormalMap = (m_normalSRV != nullptr) && useTex;
            
            // [Pass 1] 원본 물체 그리기 (아웃라인 두께 0으로 강제)
            UpdatePerObjectCB(worldM, viewM, projM, color, rough, metal, ao, useTex, useNormalMap,
                             objectShadingMode, normalStrength, toonCuts, toonLevels,
                             envDiffuseStrength, envSpecularStrength,
                             outlineColor, 0.0f);
            m_context->DrawIndexed(m_indexCount, 0, 0);

            // [Pass 2] 아웃라인 그리기 (설정된 경우만)
            if (outlineWidth > 0.0f)
            {
                m_context->RSSetState(m_rsCullFront.Get()); // 뒷면 그리기
                
                // 아웃라인 값 적용
                UpdatePerObjectCB(worldM, viewM, projM, color, rough, metal, ao, useTex, useNormalMap,
                                 objectShadingMode, normalStrength, toonCuts, toonLevels,
                                 envDiffuseStrength, envSpecularStrength,
                                 outlineColor, outlineWidth);
                m_context->DrawIndexed(m_indexCount, 0, 0);
                
                // 상태 복구
                if (originalRS) m_context->RSSetState(originalRS);
            }
        }
        if (lastShadingMode != shadingMode)
        {
            UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);
        }
    }

    bool ForwardRenderSystem::IsValidPipeline() const
    {
        return (m_vertexBuffer && m_indexBuffer && m_vertexShader && m_pixelShader && m_sceneRTV && m_sceneDSV);
    }

    void ForwardRenderSystem::RestoreBackBuffer()
    {
        ID3D11RenderTargetView* backBufferRTV = m_renderDevice.GetBackBufferRTV();
        ID3D11DepthStencilView* backBufferDSV = m_renderDevice.GetBackBufferDSV();

        if (backBufferRTV)
        {
            ID3D11RenderTargetView* rtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, rtvs, backBufferDSV);
        }
    }

    void ForwardRenderSystem::Render(const World& world,
                                     const Camera& camera,
                                     EntityId /*entity*/,
                                     const std::unordered_set<EntityId>& cameraEntities,
                                     int shadingMode,
                                     bool enableFillLight,
                                     const std::vector<SkinnedDrawCommand>& skinnedCommands)
    {
        // 0. 초기화 및 유효성 검사
        if (!IsValidPipeline()) return;

        // 실제로 사용한 카메라 정보 저장 (ComputeEffect용)
        m_lastViewProj = camera.GetViewProjectionMatrix();
        m_lastCameraPos = camera.GetPosition();

        // 1. 섀도우 맵 패스 (Shadow Map Generation) - 반환값: Main Pass에서 사용할 Light View-Projection 행렬
        XMMATRIX lightViewProj = RenderShadowPass(world, skinnedCommands, cameraEntities);

        // 2. 메인 컬러 패스 & 정적 메시 렌더링 (Main Color Pass & Static Meshes) - 씬 RTV 클리어, 공통 리소스 바인딩, 정적 오브젝트 그리기
        RenderMainPass(world, camera, shadingMode, enableFillLight, lightViewProj);

        // 3. 스키닝 메시 패스 (Skinned Meshes) - 이미 Main Pass에서 RTV가 설정되어 있으므로 바로 그립니다.
        if (!skinnedCommands.empty()) RenderSkinnedMeshes(camera, skinnedCommands, shadingMode, enableFillLight, lightViewProj);

        // 4. 스카이박스 렌더링 (Skybox)
        RenderSkybox(camera);

        // 4.5 월드 UI 렌더링 (씬 컬러 + 깊이 위에 합성)
        if (m_uiRenderer)
        {
            m_uiRenderer->RenderWorld(world, camera, m_sceneRTV.Get(), m_sceneDSV.Get());
        }

        // 5. 에디터 뷰포트 표시용 LDR 텍스처로 톤매핑 (ImGui::Image에서 사용)
        // 5. Post Process Volume 블렌딩 (카메라 위치 기준)
        {
            // 기본 설정 생성
            PostProcessSettings defaultSettings = PostProcessSettings::FromDefaults();
            defaultSettings.exposure = m_postProcessParams.exposure;
            defaultSettings.maxHDRNits = m_postProcessParams.maxHDRNits;
            defaultSettings.saturation = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingSaturation.x,
                m_postProcessParams.colorGradingSaturation.y,
                m_postProcessParams.colorGradingSaturation.z
            );
            defaultSettings.contrast = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingContrast.x,
                m_postProcessParams.colorGradingContrast.y,
                m_postProcessParams.colorGradingContrast.z
            );
            defaultSettings.gamma = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingGamma.x,
                m_postProcessParams.colorGradingGamma.y,
                m_postProcessParams.colorGradingGamma.z
            );
            defaultSettings.gain = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingGain.x,
                m_postProcessParams.colorGradingGain.y,
                m_postProcessParams.colorGradingGain.z
            );

            const std::string referenceName = ResolvePPVReferenceName(world);
            if (referenceName != m_postProcessVolumeSystem.GetReferenceObjectName())
            {
                m_postProcessVolumeSystem.SetReferenceObjectName(referenceName);
            }

            // Post Process Volume 블렌딩 계산
            PostProcessSettings finalSettings = m_postProcessVolumeSystem.CalculateFinalSettings(
                const_cast<World&>(world),  // CalculateFinalSettings는 수정하지 않으므로 안전
                camera.GetPosition(),
                defaultSettings
            );

            // 최종 설정을 m_postProcessParams에 적용
            m_postProcessParams.exposure = finalSettings.exposure;
            m_postProcessParams.colorGradingSaturation = DirectX::XMFLOAT4(
                finalSettings.saturation.x,
                finalSettings.saturation.y,
                finalSettings.saturation.z,
                1.0f
            );
            m_postProcessParams.colorGradingContrast = DirectX::XMFLOAT4(
                finalSettings.contrast.x,
                finalSettings.contrast.y,
                finalSettings.contrast.z,
                1.0f
            );
            m_postProcessParams.colorGradingGamma = DirectX::XMFLOAT4(
                finalSettings.gamma.x,
                finalSettings.gamma.y,
                finalSettings.gamma.z,
                1.0f
            );
            m_postProcessParams.colorGradingGain = DirectX::XMFLOAT4(
                finalSettings.gain.x,
                finalSettings.gain.y,
                finalSettings.gain.z,
                1.0f
            );
        }

        // 6. 에디터 뷰포트 표시용 LDR 텍스처로 톤매핑 (ImGui::Image에서 사용)
        if (m_viewportRTV)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(m_sceneWidth);
            viewport.Height = static_cast<float>(m_sceneHeight);
            viewport.MaxDepth = 1.0f;
            RenderToneMapping(m_viewportRTV.Get(), viewport);

            // UI 렌더링 (Post-processing 이후, 최상단에 렌더링)
            if (m_uiRenderer)
            {
                m_uiRenderer->RenderScreen(world, camera, m_viewportRTV.Get(), viewport.Width, viewport.Height);
            }
        }

        // 7. 최종 백버퍼 복귀 (ImGui 등 UI 렌더링을 위해)
        RestoreBackBuffer();
    }

    void ForwardRenderSystem::RenderCameraPreview(const World& world,
                                                  const Camera& camera,
                                                  const std::unordered_set<EntityId>& cameraEntities,
                                                  int shadingMode,
                                                  bool enableFillLight,
                                                  const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                                  EntityId hiddenCameraEntity)
    {
        (void)hiddenCameraEntity;
        if (!IsValidPipeline()) return;

        const float desiredAspect = (camera.GetAspectRatio() > 0.0f) ? camera.GetAspectRatio() : (16.0f / 9.0f);
        const std::uint32_t desiredHeight = (m_cameraPreviewHeight > 0) ? m_cameraPreviewHeight : 180;
        const std::uint32_t desiredWidth = (std::uint32_t)(std::max)(1.0f, std::round(desiredHeight * desiredAspect));

        if (!m_cameraPreviewViewportRTV || desiredWidth != m_cameraPreviewWidth || desiredHeight != m_cameraPreviewHeight)
        {
            if (!CreateCameraPreviewRenderTarget(desiredWidth, desiredHeight))
                return;
        }

        // 백업: 현재 렌더 타깃/깊이/포스트프로세스 상태
        auto savedSceneColorTex = m_sceneColorTex;
        auto savedSceneRTV = m_sceneRTV;
        auto savedSceneSRV = m_sceneSRV;
        auto savedViewportTex = m_viewportTex;
        auto savedViewportRTV = m_viewportRTV;
        auto savedViewportSRV = m_viewportSRV;
        auto savedDepthTex = m_sceneDepthTex;
        auto savedDSV = m_sceneDSV;
        auto savedDepthSRV = m_sceneDepthSRV;
        auto savedSceneWidth = m_sceneWidth;
        auto savedSceneHeight = m_sceneHeight;
        auto savedPostProcessParams = m_postProcessParams;
        auto savedLastViewProj = m_lastViewProj;
        auto savedLastCameraPos = m_lastCameraPos;

        // 미리보기용 렌더 타깃으로 스왑
        m_sceneColorTex = m_cameraPreviewSceneTex;
        m_sceneRTV = m_cameraPreviewSceneRTV;
        m_sceneSRV = m_cameraPreviewSceneSRV;
        m_viewportTex = m_cameraPreviewViewportTex;
        m_viewportRTV = m_cameraPreviewViewportRTV;
        m_viewportSRV = m_cameraPreviewViewportSRV;
        m_sceneDepthTex = m_cameraPreviewDepthTex;
        m_sceneDSV = m_cameraPreviewDSV;
        m_sceneDepthSRV = m_cameraPreviewDepthSRV;
        m_sceneWidth = m_cameraPreviewWidth;
        m_sceneHeight = m_cameraPreviewHeight;

        // Shadow + Main + Skinned + Skybox
        XMMATRIX lightViewProj = RenderShadowPass(world, skinnedCommands, cameraEntities);
        RenderMainPass(world, camera, shadingMode, enableFillLight, lightViewProj);
        if (!skinnedCommands.empty()) RenderSkinnedMeshes(camera, skinnedCommands, shadingMode, enableFillLight, lightViewProj);
        RenderSkybox(camera);

        // Post Process Volume 적용 (미리보기용)
        {
            PostProcessSettings defaultSettings = PostProcessSettings::FromDefaults();
            defaultSettings.exposure = m_postProcessParams.exposure;
            defaultSettings.maxHDRNits = m_postProcessParams.maxHDRNits;
            defaultSettings.saturation = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingSaturation.x,
                m_postProcessParams.colorGradingSaturation.y,
                m_postProcessParams.colorGradingSaturation.z
            );
            defaultSettings.contrast = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingContrast.x,
                m_postProcessParams.colorGradingContrast.y,
                m_postProcessParams.colorGradingContrast.z
            );
            defaultSettings.gamma = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingGamma.x,
                m_postProcessParams.colorGradingGamma.y,
                m_postProcessParams.colorGradingGamma.z
            );
            defaultSettings.gain = DirectX::XMFLOAT3(
                m_postProcessParams.colorGradingGain.x,
                m_postProcessParams.colorGradingGain.y,
                m_postProcessParams.colorGradingGain.z
            );

            const std::string referenceName = ResolvePPVReferenceName(world);
            if (referenceName != m_postProcessVolumeSystem.GetReferenceObjectName())
            {
                m_postProcessVolumeSystem.SetReferenceObjectName(referenceName);
            }

            PostProcessSettings finalSettings = m_postProcessVolumeSystem.CalculateFinalSettings(
                const_cast<World&>(world),
                camera.GetPosition(),
                defaultSettings
            );

            m_postProcessParams.exposure = finalSettings.exposure;
            m_postProcessParams.colorGradingSaturation = DirectX::XMFLOAT4(
                finalSettings.saturation.x,
                finalSettings.saturation.y,
                finalSettings.saturation.z,
                1.0f
            );
            m_postProcessParams.colorGradingContrast = DirectX::XMFLOAT4(
                finalSettings.contrast.x,
                finalSettings.contrast.y,
                finalSettings.contrast.z,
                1.0f
            );
            m_postProcessParams.colorGradingGamma = DirectX::XMFLOAT4(
                finalSettings.gamma.x,
                finalSettings.gamma.y,
                finalSettings.gamma.z,
                1.0f
            );
            m_postProcessParams.colorGradingGain = DirectX::XMFLOAT4(
                finalSettings.gain.x,
                finalSettings.gain.y,
                finalSettings.gain.z,
                1.0f
            );
        }

        if (m_viewportRTV)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(m_sceneWidth);
            viewport.Height = static_cast<float>(m_sceneHeight);
            viewport.MaxDepth = 1.0f;
            RenderToneMapping(m_viewportRTV.Get(), viewport);
        }

        RestoreBackBuffer();

        // 상태 복구
        m_sceneColorTex = savedSceneColorTex;
        m_sceneRTV = savedSceneRTV;
        m_sceneSRV = savedSceneSRV;
        m_viewportTex = savedViewportTex;
        m_viewportRTV = savedViewportRTV;
        m_viewportSRV = savedViewportSRV;
        m_sceneDepthTex = savedDepthTex;
        m_sceneDSV = savedDSV;
        m_sceneDepthSRV = savedDepthSRV;
        m_sceneWidth = savedSceneWidth;
        m_sceneHeight = savedSceneHeight;
        m_postProcessParams = savedPostProcessParams;
        m_lastViewProj = savedLastViewProj;
        m_lastCameraPos = savedLastCameraPos;
    }

    bool ForwardRenderSystem::CreateToneMappingResources()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        // Quad Vertex Shader 컴파일
        if (FAILED(D3DCompile(CommonShaderCode::QuadVS, strlen(CommonShaderCode::QuadVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Quad VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_quadVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Quad VS");
            return false;
        }

        // Quad Input Layout
        D3D11_INPUT_ELEMENT_DESC quadLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(quadLayout, ARRAYSIZE(quadLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_quadInputLayout.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Quad Input Layout");
            return false;
        }

        // HDR 지원 여부 확인 및 적절한 셰이더 선택
        float maxNits = 100.0f;
        bool isHDRSupported = m_renderDevice.IsHDRSupported(maxNits);
        const char* toneMappingShaderSource = isHDRSupported ? CommonShaderCode::ToneMappingPS_HDR : CommonShaderCode::ToneMappingPS_LDR;
        const char* shaderName = isHDRSupported ? "HDR" : "LDR";

        // Tone Mapping Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(toneMappingShaderSource, strlen(toneMappingShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                std::string errorMsg = (char*)errorBlob->GetBufferPointer();
                ALICE_LOG_ERRORF("Tone Mapping PS (%s) compile error: %s", shaderName, (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_toneMappingPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Tone Mapping PS (%s)", shaderName);
            return false;
        }

        // Particle Overlay Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::ParticleOverlayPS, strlen(CommonShaderCode::ParticleOverlayPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Particle Overlay PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_particleOverlayPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Particle Overlay PS");
            return false;
        }

        if (isHDRSupported)
        {
            ALICE_LOG_INFO("ForwardRenderSystem::CreateToneMappingResources: HDR 톤매핑 셰이더 사용. MaxNits: %.1f", maxNits);
        }
        else
        {
            ALICE_LOG_INFO("ForwardRenderSystem::CreateToneMappingResources: LDR 톤매핑 셰이더 사용.");
        }

        // 톤매핑 전용 상태 객체 생성 (Blend OFF, Depth OFF, Cull OFF)
        // Depth OFF
        {
            D3D11_DEPTH_STENCIL_DESC ds = {};
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
            ds.StencilEnable = FALSE;
            if (FAILED(m_device->CreateDepthStencilState(&ds, m_ppDepthOff.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create PostProcess Depth State");
                return false;
            }
        }

        // Blend OFF (opaque)
        {
            D3D11_BLEND_DESC bd = {};
            bd.AlphaToCoverageEnable = FALSE;
            bd.IndependentBlendEnable = FALSE;
            auto& rt = bd.RenderTarget[0];
            rt.BlendEnable = FALSE;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(m_device->CreateBlendState(&bd, m_ppBlendOpaque.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create PostProcess Blend State");
                return false;
            }
        }

        // Blend Additive (파티클 오버레이용)
        {
            D3D11_BLEND_DESC bd = {};
            bd.AlphaToCoverageEnable = FALSE;
            bd.IndependentBlendEnable = FALSE;
            auto& rt = bd.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_ONE;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_ONE;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(m_device->CreateBlendState(&bd, m_ppBlendAdditive.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Additive Blend State");
                return false;
            }
        }

        // Rasterizer: cull off, scissor off
        {
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            rd.ScissorEnable = FALSE;
            if (FAILED(m_device->CreateRasterizerState(&rd, m_ppRasterNoCull.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create PostProcess Rasterizer State");
                return false;
            }
        }

        // Quad 지오메트리 생성
        struct QuadVertex
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMFLOAT2 uv;
        };

        QuadVertex vertices[] = {
            { DirectX::XMFLOAT3(-1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },  // Left Top
            { DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) },   // Right Top
            { DirectX::XMFLOAT3(-1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) }, // Left Bottom
            { DirectX::XMFLOAT3(1.0f, -1.0f, 1.0f), DirectX::XMFLOAT2(1.0f, 1.0f) }   // Right Bottom
        };

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = sizeof(QuadVertex) * 4;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA vbData = {};
        vbData.pSysMem = vertices;
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, m_quadVB.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Quad VB");
            return false;
        }

        m_quadStride = sizeof(QuadVertex);
        m_quadOffset = 0;

        WORD indices[] = { 0, 1, 2, 2, 1, 3 };
        m_quadIndexCount = 6;
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = sizeof(WORD) * 6;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = indices;
        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, m_quadIB.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Quad IB");
            return false;
        }

        return true;
    }

    void ForwardRenderSystem::RenderToneMapping(ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!m_toneMappingPS || !m_quadVS || !m_sceneSRV || !targetRTV) return;

        // 뷰포트 설정
        m_context->RSSetViewports(1, &viewport);

        // 렌더 타겟 설정
        m_context->OMSetRenderTargets(1, &targetRTV, nullptr);

        // 상태 정리 (중요: 이전 패스의 상태가 남아있으면 후처리가 오염됨)
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        PostProcessCB cbData = {};
        GetPostProcessParams(cbData.exposure, cbData.maxHDRNits);
        cbData.colorGradingSaturation = m_postProcessParams.colorGradingSaturation;
        cbData.colorGradingContrast = m_postProcessParams.colorGradingContrast;
        cbData.colorGradingGamma = m_postProcessParams.colorGradingGamma;
        cbData.colorGradingGain = m_postProcessParams.colorGradingGain;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPostProcess.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cbData, sizeof(PostProcessCB));
            m_context->Unmap(m_cbPostProcess.Get(), 0);
        }

        // 리소스 바인딩
        ID3D11ShaderResourceView* srv = m_sceneSRV.Get();
        ID3D11SamplerState* sampler = m_samplerLinear.Get();
        ID3D11Buffer* cb = m_cbPostProcess.Get();

        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sampler);
        m_context->PSSetConstantBuffers(2, 1, &cb); // register(b2)에 맞춰 슬롯 2 사용

        // Quad 그리기 (VB 바인딩은 로컬 변수로 안전하게)
        UINT stride = m_quadStride, offset = m_quadOffset;
        ID3D11Buffer* vb = m_quadVB.Get();

        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_toneMappingPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);

        // 리소스 해제
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSRV);
    }


    void ForwardRenderSystem::GetPostProcessParams(float& outExposure, float& outMaxHDRNits) const
    {
        outExposure = m_postProcessParams.exposure;
        
        // RenderDevice에서 HDR 지원 여부 및 최대 밝기 가져오기
        float maxNits = 100.0f;
        m_renderDevice.IsHDRSupported(maxNits);
        // 사용자가 설정한 값이 있으면 사용, 없으면 모니터 최대 밝기 사용
        outMaxHDRNits = (m_postProcessParams.maxHDRNits > 0.0f) ? m_postProcessParams.maxHDRNits : maxNits;
    }
    
    void ForwardRenderSystem::GetPostProcessParams(float& outExposure, float& outMaxHDRNits, float& outSaturation, float& outContrast, float& outGamma) const
    {
        GetPostProcessParams(outExposure, outMaxHDRNits);
        // Vector4의 첫 번째 채널(R)을 반환 (하위 호환성)
        outSaturation = m_postProcessParams.colorGradingSaturation.x;
        outContrast = m_postProcessParams.colorGradingContrast.x;
        outGamma = m_postProcessParams.colorGradingGamma.x;
    }

    void ForwardRenderSystem::SetPostProcessParams(float exposure, float maxHDRNits)
    {
        m_postProcessParams.exposure = exposure;
        m_postProcessParams.maxHDRNits = maxHDRNits;
        // Color Grading은 기본값 유지 (하위 호환성)
    }
    
    void ForwardRenderSystem::SetPostProcessParams(float exposure, float maxHDRNits, float saturation, float contrast, float gamma)
    {
        m_postProcessParams.exposure = exposure;
        m_postProcessParams.maxHDRNits = maxHDRNits;
        // Color Grading 파라미터 클램프 및 설정 (float을 Vector4로 확장)
        float satClamped = std::clamp(saturation, ColorGradingLimits::SaturationMin, ColorGradingLimits::SaturationMax);
        float contClamped = std::clamp(contrast, ColorGradingLimits::ContrastMin, ColorGradingLimits::ContrastMax);
        float gamClamped = std::clamp(gamma, ColorGradingLimits::GammaMin, ColorGradingLimits::GammaMax);
        m_postProcessParams.colorGradingSaturation = DirectX::XMFLOAT4(satClamped, satClamped, satClamped, 1.0f);
        m_postProcessParams.colorGradingContrast = DirectX::XMFLOAT4(contClamped, contClamped, contClamped, 1.0f);
        m_postProcessParams.colorGradingGamma = DirectX::XMFLOAT4(gamClamped, gamClamped, gamClamped, 1.0f);
        // Gain은 기본값 유지 (하위 호환성)
        m_postProcessParams.colorGradingGain = DirectX::XMFLOAT4(
            ColorGradingLimits::GainDefault, 
            ColorGradingLimits::GainDefault, 
            ColorGradingLimits::GainDefault, 
            1.0f
        );
    }

    void ForwardRenderSystem::ApplyColorGrading(const DirectX::XMFLOAT4& saturation, const DirectX::XMFLOAT4& contrast, const DirectX::XMFLOAT4& gamma, const DirectX::XMFLOAT4& gain)
    {
        // Color Grading 파라미터만 설정 (Exposure와 MaxHDRNits는 유지)
        // 각 채널별로 클램프 적용
        m_postProcessParams.colorGradingSaturation = DirectX::XMFLOAT4(
            std::clamp(saturation.x, ColorGradingLimits::SaturationMin, ColorGradingLimits::SaturationMax),
            std::clamp(saturation.y, ColorGradingLimits::SaturationMin, ColorGradingLimits::SaturationMax),
            std::clamp(saturation.z, ColorGradingLimits::SaturationMin, ColorGradingLimits::SaturationMax),
            1.0f
        );
        m_postProcessParams.colorGradingContrast = DirectX::XMFLOAT4(
            std::clamp(contrast.x, ColorGradingLimits::ContrastMin, ColorGradingLimits::ContrastMax),
            std::clamp(contrast.y, ColorGradingLimits::ContrastMin, ColorGradingLimits::ContrastMax),
            std::clamp(contrast.z, ColorGradingLimits::ContrastMin, ColorGradingLimits::ContrastMax),
            1.0f
        );
        m_postProcessParams.colorGradingGamma = DirectX::XMFLOAT4(
            std::clamp(gamma.x, ColorGradingLimits::GammaMin, ColorGradingLimits::GammaMax),
            std::clamp(gamma.y, ColorGradingLimits::GammaMin, ColorGradingLimits::GammaMax),
            std::clamp(gamma.z, ColorGradingLimits::GammaMin, ColorGradingLimits::GammaMax),
            1.0f
        );
        m_postProcessParams.colorGradingGain = DirectX::XMFLOAT4(
            std::clamp(gain.x, ColorGradingLimits::GainMin, ColorGradingLimits::GainMax),
            std::clamp(gain.y, ColorGradingLimits::GainMin, ColorGradingLimits::GainMax),
            std::clamp(gain.z, ColorGradingLimits::GainMin, ColorGradingLimits::GainMax),
            1.0f
        );
    }

    void ForwardRenderSystem::ApplyColorGrading(float saturation, float contrast, float gamma, float gain)
    {
        // 편의 함수: float을 Vector4로 확장
        DirectX::XMFLOAT4 satVec(saturation, saturation, saturation, 1.0f);
        DirectX::XMFLOAT4 contVec(contrast, contrast, contrast, 1.0f);
        DirectX::XMFLOAT4 gamVec(gamma, gamma, gamma, 1.0f);
        DirectX::XMFLOAT4 gainVec(gain, gain, gain, 1.0f);
        ApplyColorGrading(satVec, contVec, gamVec, gainVec);
    }

    void ForwardRenderSystem::GetColorGrading(DirectX::XMFLOAT4& outSaturation, DirectX::XMFLOAT4& outContrast, DirectX::XMFLOAT4& outGamma, DirectX::XMFLOAT4& outGain) const
    {
        outSaturation = m_postProcessParams.colorGradingSaturation;
        outContrast = m_postProcessParams.colorGradingContrast;
        outGamma = m_postProcessParams.colorGradingGamma;
        outGain = m_postProcessParams.colorGradingGain;
    }

    void ForwardRenderSystem::RenderParticleOverlay(ID3D11ShaderResourceView* particleSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!m_particleOverlayPS || !m_quadVS || !particleSRV || !targetRTV) return;

        // UAV와 SRV 동시 바인딩 충돌 방지: Compute Shader에서 사용한 UAV/SRV를 명시적으로 unbind
        // DirectX11에서는 같은 리소스를 UAV와 SRV로 동시에 바인딩할 수 없음
        // Compute Shader는 CS stage에서 UAV를 사용하므로, CS stage의 UAV/SRV만 unbind하면 충분
        ID3D11UnorderedAccessView* nullUAVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        UINT uavInitialCounts[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        m_context->CSSetUnorderedAccessViews(0, 8, nullUAVs, uavInitialCounts);
        
        // CS에서 SRV로도 바인딩되어 있을 수 있으므로 CS SRV도 unbind
        ID3D11ShaderResourceView* nullCSsrvs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->CSSetShaderResources(0, 8, nullCSsrvs);
        
        // PS의 SRV도 먼저 unbind한 후에 다시 바인딩 (안전을 위해)
        ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 8, nullSRVs);

        // 뷰포트 설정
        m_context->RSSetViewports(1, &viewport);

        // 렌더 타겟 설정
        m_context->OMSetRenderTargets(1, &targetRTV, nullptr);

        // Additive blending 활성화
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendAdditive.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        // 리소스 바인딩 (UAV unbind 후에 SRV 바인딩)
        ID3D11ShaderResourceView* srv = particleSRV;
        ID3D11SamplerState* sampler = m_samplerLinear.Get();

        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sampler);

        // Quad 그리기
        UINT stride = m_quadStride, offset = m_quadOffset;
        ID3D11Buffer* vb = m_quadVB.Get();

        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_particleOverlayPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);

        // 리소스 해제
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSRV);

        // 파이프라인 상태 복원 (다음 렌더링을 위해)
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());
    }

    void ForwardRenderSystem::RenderParticleOverlayToViewport(ID3D11ShaderResourceView* particleSRV)
    {
        // 씬 전환 중 리소스가 유효하지 않을 수 있으므로 모든 리소스 확인
        if (!particleSRV) return;
        ID3D11RenderTargetView* viewportRTV = m_viewportRTV.Get();
        if (!viewportRTV) return;
        if (m_sceneWidth == 0 || m_sceneHeight == 0) return;
        if (!m_particleOverlayPS || !m_quadVS || !m_quadVB || !m_quadIB || !m_quadInputLayout) return;
        
        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(m_sceneWidth);
        viewport.Height = static_cast<float>(m_sceneHeight);
        viewport.MaxDepth = 1.0f;
        
        RenderParticleOverlay(particleSRV, viewportRTV, viewport);
        
        // 뷰포트 RTV를 SRV로 읽을 수 있도록 BackBuffer로 복귀 (ImGui::Image가 viewportSRV를 읽기 위해 필수)
        // DirectX11에서는 같은 리소스를 RTV와 SRV로 동시에 바인딩할 수 없음
        RestoreBackBuffer();
    }

    void ForwardRenderSystem::RenderDebugOverlayToViewport(DebugDrawSystem& debugDraw, const Camera& camera, bool depthTest)
    {
        ID3D11RenderTargetView* viewportRTV = m_viewportRTV.Get();
        if (!viewportRTV || m_sceneWidth == 0 || m_sceneHeight == 0)
        {
            return;
        }

        // Depth SRV 충돌 방지
        ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 8, nullSRVs);
        m_context->VSSetShaderResources(0, 8, nullSRVs);
        m_context->CSSetShaderResources(0, 8, nullSRVs);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(m_sceneWidth);
        viewport.Height = static_cast<float>(m_sceneHeight);
        viewport.MaxDepth = 1.0f;

        m_context->RSSetViewports(1, &viewport);
        if (depthTest)
        {
            m_context->OMSetRenderTargets(1, &viewportRTV, m_sceneDSV.Get());
        }
        else
        {
            m_context->OMSetRenderTargets(1, &viewportRTV, nullptr);
        }

        float blendFactor[4] = { 0, 0, 0, 0 };
        if (m_ppBlendOpaque) m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        if (depthTest)
        {
            m_context->OMSetDepthStencilState(m_depthStencilStateReadOnly.Get(), 0);
        }
        else
        {
            if (m_ppDepthOff) m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        }
        if (m_ppRasterNoCull) m_context->RSSetState(m_ppRasterNoCull.Get());

        debugDraw.Render(camera);

        // SRV로 읽을 수 있도록 백버퍼 복귀
        RestoreBackBuffer();
    }


}


