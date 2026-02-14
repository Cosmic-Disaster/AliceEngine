#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/Rendering/DebugDrawSystem.h"
#include "Runtime/Rendering/PostProcessSettings.h"

#include <d3dcompiler.h>
#include <DirectXTK/WICTextureLoader.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <filesystem>
#include <vector>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <cstring>
#include <DirectXMath.h>
#include <DirectXCollision.h>

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/DecalComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/TrailEffectComponent.h"
#include "Runtime/Rendering/Components/PointLightComponent.h"
#include "Runtime/Rendering/Components/SpotLightComponent.h"
#include "Runtime/Rendering/Components/RectLightComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/ShaderCode/CommonShaderCode.h"
#include "Runtime/Rendering/ShaderCode/DeferredShader.h"
#include "Runtime/Rendering/TrailEffectRenderSystem.h"
#include "Runtime/UI/UIRenderer.h"
#include <fstream>
#include <sstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    namespace
    {
        constexpr const char* kCameraIconFbxPath = "Resource/BasicMesh/Camera.fbx";
    }

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
            if (cmd.boneCount == 0)
                return true;

            if (!cmd.bones || cmd.boneCount != 1)
                return false;

            return IsIdentityBoneMatrix(cmd.bones[0]);
        }

        bool NearlyEqualFloat3(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float eps = 1e-4f)
        {
            return (std::fabs(a.x - b.x) <= eps) &&
                   (std::fabs(a.y - b.y) <= eps) &&
                   (std::fabs(a.z - b.z) <= eps);
        }
    }


    DeferredRenderSystem::DeferredRenderSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device = renderDevice.GetDevice();
        m_context = renderDevice.GetImmediateContext();
    }

    bool DeferredRenderSystem::Initialize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device || !m_context)
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: Device or Context is null.");
            return false;
        }

        // G-Buffer 생성
        if (!CreateGBuffer(width, height))
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateGBuffer failed.");
            return false;
        }

        // D-Buffer 생성 (Decal)
        if (!CreateDBuffer(width, height))
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateDBuffer failed.");
            return false;
        }

        // 셰이더 생성
        if (!CreateShaders())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateShaders failed.");
            return false;
        }

        // Quad 지오메트리 생성
        if (!CreateQuadGeometry())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateQuadGeometry failed.");
            return false;
        }

        // 큐브 지오메트리 생성
        if (!CreateCubeGeometry())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateCubeGeometry failed.");
            return false;
        }

        // 상수 버퍼 생성
        if (!CreateConstantBuffers())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateConstantBuffers failed.");
            return false;
        }

        // 샘플러 상태 생성
        if (!CreateSamplerStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateSamplerStates failed.");
            return false;
        }

        // 블렌드 상태 생성
        if (!CreateBlendStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateBlendStates failed.");
            return false;
        }

        // 래스터라이저 상태 생성
        if (!CreateRasterizerStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateRasterizerStates failed.");
            return false;
        }

        // 깊이/스텐실 상태 생성
        if (!CreateDepthStencilStates())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateDepthStencilStates failed.");
            return false;
        }

        // GPU 인스턴싱 버퍼 생성 (초기 용량)
        if (!CreateInstanceBuffer(2048))
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateInstanceBuffer failed.");
            return false;
        }

        // 섀도우 맵 리소스 생성 (Deferred Light에서 PCF로 사용)
        if (!CreateShadowMapResources())
        {
            ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateShadowMapResources failed.");
            return false;
        }

		if (!CreateToneMappingResources(width, height))
		{
			ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateToneMappingResources failed.");
			return false;
		}

		if (!CreateBloomResources(width, height))
		{
			ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreateBloomResources failed.");
			return false;
		}
        
        if (!CreatePostBloomResources(width, height))
        {
			ALICE_LOG_ERRORF("DeferredRenderSystem::Initialize: CreatePostBloomResources failed.");
			return false;
        }

        // IBL 리소스 생성
        if (!CreateIblResources())
        {
            ALICE_LOG_WARN("DeferredRenderSystem::Initialize: CreateIblResources failed (optional).");
        }

        ALICE_LOG_INFO("DeferredRenderSystem::Initialize: success.");
        return true;
    }

    void DeferredRenderSystem::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device) return;
        if (width == 0 || height == 0) return;

        // G-Buffer 리사이즈
        CreateGBuffer(width, height);
        CreateDBuffer(width, height);

        // 씬 렌더 타겟 리사이즈
        m_sceneColorTex.Reset();
        m_sceneRTV.Reset();
        m_sceneColorSRV.Reset();
        m_viewportTex.Reset();
        m_viewportRTV.Reset();
        m_viewportSRV.Reset();
        m_sceneDepthTex.Reset();
        m_sceneDSV.Reset();
        m_sceneDepthSRV.Reset();

        m_sceneWidth = width;
        m_sceneHeight = height;
        D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateRenderTargetView(m_sceneColorTex.Get(), nullptr, m_sceneRTV.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateShaderResourceView(m_sceneColorTex.Get(), nullptr, m_sceneColorSRV.ReleaseAndGetAddressOf()))) return;

        D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_viewportTex.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateRenderTargetView(m_viewportTex.Get(), nullptr, m_viewportRTV.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_device->CreateShaderResourceView(m_viewportTex.Get(), nullptr, m_viewportSRV.ReleaseAndGetAddressOf()))) return;

        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_sceneDepthTex.ReleaseAndGetAddressOf()))) return;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        if (FAILED(m_device->CreateDepthStencilView(m_sceneDepthTex.Get(), &dsvDesc, m_sceneDSV.ReleaseAndGetAddressOf()))) return;

        D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MostDetailedMip = 0;
        depthSrvDesc.Texture2D.MipLevels = 1;
        HRESULT hrDepthSRV = m_device->CreateShaderResourceView(m_sceneDepthTex.Get(), &depthSrvDesc, m_sceneDepthSRV.ReleaseAndGetAddressOf());
        if (FAILED(hrDepthSRV))
        {
            ALICE_LOG_WARN("DeferredRenderSystem::Resize: CreateShaderResourceView(depthSRV) failed (0x%08X)", (unsigned)hrDepthSRV);
            m_sceneDepthSRV.Reset();
        }

        // Bloom 리소스 리사이즈
        for (int level = 0; level < BLOOM_LEVEL_COUNT; ++level)
        {
            for (int pingPong = 0; pingPong < 2; ++pingPong)
            {
                m_bloomLevelTex[level][pingPong].Reset();
                m_bloomLevelRTV[level][pingPong].Reset();
                m_bloomLevelSRV[level][pingPong].Reset();
            }
        }
		CreateBloomResources(width, height);

        // Post-Bloom 합성 리소스 리사이즈
        m_postBloomTex.Reset();
        m_postBloomRTV.Reset();
        m_postBloomSRV.Reset();
        CreatePostBloomResources(width, height);
    }

    bool DeferredRenderSystem::CreateGBuffer(std::uint32_t width, std::uint32_t height)
    {
        // 기존 G-Buffer 해제
        for (int i = 0; i < GBufferCount; ++i)
        {
            m_gBufferSRVs[i].Reset();
            m_gBufferRTVs[i].Reset();
            m_gBufferTextures[i].Reset();
        }

        // G-Buffer 포맷 정의 (압축)
        // - Position은 Depth로 대체 (Scene Depth SRV에서 복원)
        // - Normal + Roughness를 하나로 묶고, Metalness + ToonCuts를 RGBA로 저장
        DXGI_FORMAT formats[GBufferCount] = {
            DXGI_FORMAT_R16G16B16A16_FLOAT,  // 0: NormalWS(encode) + Roughness(A)
            DXGI_FORMAT_R8G8B8A8_UNORM,      // 1: Metalness(R) + ToonCuts(GBA)
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // 2: BaseColor + ShadingMode(A)
            DXGI_FORMAT_R8G8B8A8_UNORM,      // 3: ToonParams (Strength/Levels)
        };

        // 각 G-Buffer 텍스처 생성
        for (int i = 0; i < GBufferCount; ++i)
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = width;
            td.Height = height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = formats[i];
            td.SampleDesc.Count = 1;
            td.SampleDesc.Quality = 0;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            td.CPUAccessFlags = 0;
            td.MiscFlags = 0;

            if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_gBufferTextures[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateRenderTargetView(m_gBufferTextures[i].Get(), nullptr, m_gBufferRTVs[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateShaderResourceView(m_gBufferTextures[i].Get(), nullptr, m_gBufferSRVs[i].ReleaseAndGetAddressOf())))
                return false;
        }

        return true;
    }

    bool DeferredRenderSystem::CreateDBuffer(std::uint32_t width, std::uint32_t height)
    {
        for (int i = 0; i < DBufferCount; ++i)
        {
            m_dBufferSRVs[i].Reset();
            m_dBufferRTVs[i].Reset();
            m_dBufferTextures[i].Reset();
        }

        DXGI_FORMAT formats[DBufferCount] = {
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB // 0: Decal Albedo (premultiplied) + Alpha
        };

        for (int i = 0; i < DBufferCount; ++i)
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = width;
            td.Height = height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = formats[i];
            td.SampleDesc.Count = 1;
            td.SampleDesc.Quality = 0;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            td.CPUAccessFlags = 0;
            td.MiscFlags = 0;

            if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_dBufferTextures[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateRenderTargetView(m_dBufferTextures[i].Get(), nullptr, m_dBufferRTVs[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateShaderResourceView(m_dBufferTextures[i].Get(), nullptr, m_dBufferSRVs[i].ReleaseAndGetAddressOf())))
                return false;
        }

        return true;
    }

    bool DeferredRenderSystem::CreateCameraPreviewTargets(std::uint32_t width, std::uint32_t height)
    {
        m_cameraPreviewWidth = width;
        m_cameraPreviewHeight = height;

        for (int i = 0; i < GBufferCount; ++i)
        {
            m_cameraPreviewGBufferSRVs[i].Reset();
            m_cameraPreviewGBufferRTVs[i].Reset();
            m_cameraPreviewGBufferTextures[i].Reset();
        }

        for (int i = 0; i < DBufferCount; ++i)
        {
            m_cameraPreviewDBufferSRVs[i].Reset();
            m_cameraPreviewDBufferRTVs[i].Reset();
            m_cameraPreviewDBufferTextures[i].Reset();
        }

        DXGI_FORMAT formats[GBufferCount] = {
            DXGI_FORMAT_R16G16B16A16_FLOAT,  // 0: NormalWS + Roughness
            DXGI_FORMAT_R8G8B8A8_UNORM,      // 1: Metalness + ToonCuts
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // 2: BaseColor + ShadingMode
            DXGI_FORMAT_R8G8B8A8_UNORM,      // 3: ToonParams
        };

        for (int i = 0; i < GBufferCount; ++i)
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = width;
            td.Height = height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = formats[i];
            td.SampleDesc.Count = 1;
            td.SampleDesc.Quality = 0;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            td.CPUAccessFlags = 0;
            td.MiscFlags = 0;

            if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_cameraPreviewGBufferTextures[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateRenderTargetView(m_cameraPreviewGBufferTextures[i].Get(), nullptr, m_cameraPreviewGBufferRTVs[i].ReleaseAndGetAddressOf())))
                return false;
            if (FAILED(m_device->CreateShaderResourceView(m_cameraPreviewGBufferTextures[i].Get(), nullptr, m_cameraPreviewGBufferSRVs[i].ReleaseAndGetAddressOf())))
                return false;
        }

        // D-Buffer (Decal) for camera preview
        {
            DXGI_FORMAT dFormats[DBufferCount] = {
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            };

            for (int i = 0; i < DBufferCount; ++i)
            {
                D3D11_TEXTURE2D_DESC td = {};
                td.Width = width;
                td.Height = height;
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = dFormats[i];
                td.SampleDesc.Count = 1;
                td.SampleDesc.Quality = 0;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                td.CPUAccessFlags = 0;
                td.MiscFlags = 0;

                if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_cameraPreviewDBufferTextures[i].ReleaseAndGetAddressOf())))
                    return false;
                if (FAILED(m_device->CreateRenderTargetView(m_cameraPreviewDBufferTextures[i].Get(), nullptr, m_cameraPreviewDBufferRTVs[i].ReleaseAndGetAddressOf())))
                    return false;
                if (FAILED(m_device->CreateShaderResourceView(m_cameraPreviewDBufferTextures[i].Get(), nullptr, m_cameraPreviewDBufferSRVs[i].ReleaseAndGetAddressOf())))
                    return false;
            }
        }

        m_cameraPreviewSceneColorTex.Reset();
        m_cameraPreviewSceneRTV.Reset();
        m_cameraPreviewSceneColorSRV.Reset();
        m_cameraPreviewViewportTex.Reset();
        m_cameraPreviewViewportRTV.Reset();
        m_cameraPreviewViewportSRV.Reset();
        m_cameraPreviewSceneDepthTex.Reset();
        m_cameraPreviewSceneDSV.Reset();
        m_cameraPreviewSceneDepthSRV.Reset();

        D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_cameraPreviewSceneColorTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_cameraPreviewSceneColorTex.Get(), nullptr, m_cameraPreviewSceneRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_cameraPreviewSceneColorTex.Get(), nullptr, m_cameraPreviewSceneColorSRV.ReleaseAndGetAddressOf()))) return false;

        D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_cameraPreviewViewportTex.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateRenderTargetView(m_cameraPreviewViewportTex.Get(), nullptr, m_cameraPreviewViewportRTV.ReleaseAndGetAddressOf()))) return false;
        if (FAILED(m_device->CreateShaderResourceView(m_cameraPreviewViewportTex.Get(), nullptr, m_cameraPreviewViewportSRV.ReleaseAndGetAddressOf()))) return false;

        D3D11_TEXTURE2D_DESC dDesc = { width, height, 1, 1, DXGI_FORMAT_R24G8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(m_device->CreateTexture2D(&dDesc, nullptr, m_cameraPreviewSceneDepthTex.ReleaseAndGetAddressOf()))) return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        if (FAILED(m_device->CreateDepthStencilView(m_cameraPreviewSceneDepthTex.Get(), &dsvDesc, m_cameraPreviewSceneDSV.ReleaseAndGetAddressOf()))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MostDetailedMip = 0;
        depthSrvDesc.Texture2D.MipLevels = 1;
        HRESULT hrDepthSRV = m_device->CreateShaderResourceView(m_cameraPreviewSceneDepthTex.Get(), &depthSrvDesc, m_cameraPreviewSceneDepthSRV.ReleaseAndGetAddressOf());
        if (FAILED(hrDepthSRV))
        {
            ALICE_LOG_WARN("DeferredRenderSystem::CreateCameraPreviewTargets: CreateShaderResourceView(depthSRV) failed (0x%08X)", (unsigned)hrDepthSRV);
            m_cameraPreviewSceneDepthSRV.Reset();
        }

        return true;
    }

    bool DeferredRenderSystem::CreateShaders()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        // G-Buffer Vertex Shader
        if (FAILED(D3DCompile(DeferredShader::GBufferVS, strlen(DeferredShader::GBufferVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_gBufferVS.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Input Layout
        D3D11_INPUT_ELEMENT_DESC gbufferLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(gbufferLayout, ARRAYSIZE(gbufferLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Instanced Vertex Shader (Static)
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::GBufferInstancedVS, strlen(DeferredShader::GBufferInstancedVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer Instanced VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_gBufferInstancedVS.ReleaseAndGetAddressOf())))
            return false;

        D3D11_INPUT_ELEMENT_DESC gbufferInstancedLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        if (FAILED(m_device->CreateInputLayout(gbufferInstancedLayout, ARRAYSIZE(gbufferInstancedLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferInstancedInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Skinned Vertex Shader
        vsBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::GBufferSkinnedVS, strlen(DeferredShader::GBufferSkinnedVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer Skinned VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_gBufferSkinnedVS.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Skinned Input Layout
        // - ForwardRenderSystem 과 동일한 정점 레이아웃/오프셋을 사용해야 본 인덱스/웨이트가 깨지지 않습니다.
        //   (Deferred 쪽이 COLOR를 누락하면 TEXCOORD 이후 오프셋이 밀려 애니메이션/UV가 전부 망가질 수 있음)
        D3D11_INPUT_ELEMENT_DESC skinnedLayout[] = {
            {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(skinnedLayout, ARRAYSIZE(skinnedLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferSkinnedInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Skinned Instanced Vertex Shader
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::GBufferSkinnedInstancedVS, strlen(DeferredShader::GBufferSkinnedInstancedVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer Skinned Instanced VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_gBufferSkinnedInstancedVS.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Skinned Instanced Input Layout
        D3D11_INPUT_ELEMENT_DESC skinnedInstancedLayout[] = {
            {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        if (FAILED(m_device->CreateInputLayout(skinnedInstancedLayout, ARRAYSIZE(skinnedInstancedLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_gBufferSkinnedInstancedInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // Quad Vertex Shader
        vsBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::QuadVS, strlen(CommonShaderCode::QuadVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Quad VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }

        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_quadVS.ReleaseAndGetAddressOf())))
            return false;

        // Quad Input Layout
        D3D11_INPUT_ELEMENT_DESC quadLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(quadLayout, ARRAYSIZE(quadLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_quadInputLayout.ReleaseAndGetAddressOf())))
            return false;

        // G-Buffer Pixel Shader 컴파일
        if (FAILED(D3DCompile(DeferredShader::GBufferPS, strlen(DeferredShader::GBufferPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("GBuffer PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_gBufferPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create G-Buffer PS");
            return false;
        }

        // Decal Shaders
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::DecalVS, strlen(DeferredShader::DecalVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Decal VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_decalVS.ReleaseAndGetAddressOf())))
            return false;

        D3D11_INPUT_ELEMENT_DESC decalLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(decalLayout, ARRAYSIZE(decalLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_decalInputLayout.ReleaseAndGetAddressOf())))
            return false;

        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::DecalPS, strlen(DeferredShader::DecalPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Decal PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_decalPS.ReleaseAndGetAddressOf())))
            return false;

        // Deferred Light Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        const std::string lightPs = std::string(DeferredShader::LightPS1) + DeferredShader::LightPS2;
        if (FAILED(D3DCompile(lightPs.c_str(), lightPs.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Deferred Light PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_deferredLightPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Deferred Light PS");
            return false;
        }

        // ===================== Transparent Forward-Style Shaders =====================
        // Skinned Transparent VS
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::TransparentSkinnedVS,
                              strlen(DeferredShader::TransparentSkinnedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Transparent Skinned VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_transparentSkinnedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Transparent Skinned VS");
            return false;
        }

        // Transparent Skinned Input Layout (Forward와 동일 오프셋)
        {
            D3D11_INPUT_ELEMENT_DESC skinnedLayoutT[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            if (FAILED(m_device->CreateInputLayout(skinnedLayoutT,
                                                   ARRAYSIZE(skinnedLayoutT),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_transparentSkinnedInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Transparent Skinned InputLayout");
                return false;
            }
        }

        // Transparent Skinned Instanced VS
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::TransparentSkinnedInstancedVS,
                              strlen(DeferredShader::TransparentSkinnedInstancedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Transparent Skinned Instanced VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_transparentSkinnedInstancedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Transparent Skinned Instanced VS");
            return false;
        }

        // Transparent Skinned Instanced Input Layout
        {
            D3D11_INPUT_ELEMENT_DESC skinnedInstancedLayoutT[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            };
            if (FAILED(m_device->CreateInputLayout(skinnedInstancedLayoutT,
                                                   ARRAYSIZE(skinnedInstancedLayoutT),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_transparentSkinnedInstancedInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Transparent Skinned Instanced InputLayout");
                return false;
            }
        }

        // Transparent PS
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::TransparentPS,
                              strlen(DeferredShader::TransparentPS),
                              nullptr, nullptr, nullptr,
                              "main", "ps_5_0",
                              0, 0,
                              psBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Transparent PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                               psBlob->GetBufferSize(),
                                               nullptr,
                                               m_transparentPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Transparent PS");
            return false;
        }

        // Skybox Vertex Shader 컴파일
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::SkyboxVS, strlen(CommonShaderCode::SkyboxVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Skybox VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_skyboxVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Skybox VS");
            return false;
        }
        D3D11_INPUT_ELEMENT_DESC skyboxLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        if (FAILED(m_device->CreateInputLayout(skyboxLayout, ARRAYSIZE(skyboxLayout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_skyboxInputLayout.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Skybox Input Layout");
            return false;
        }

        // Skybox Pixel Shader 컴파일
        psBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(CommonShaderCode::SkyboxPS, strlen(CommonShaderCode::SkyboxPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Skybox PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_skyboxPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Skybox PS");
            return false;
        }

        // Skybox Depth State 및 Rasterizer State 생성
        D3D11_DEPTH_STENCIL_DESC skyboxDsDesc = {};
        skyboxDsDesc.DepthEnable = TRUE;
        skyboxDsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        skyboxDsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        skyboxDsDesc.StencilEnable = FALSE;
        if (FAILED(m_device->CreateDepthStencilState(&skyboxDsDesc, m_skyboxDepthState.ReleaseAndGetAddressOf())))
            return false;

        D3D11_RASTERIZER_DESC skyboxRsDesc = {};
        skyboxRsDesc.FillMode = D3D11_FILL_SOLID;
        skyboxRsDesc.CullMode = D3D11_CULL_NONE;
        skyboxRsDesc.DepthClipEnable = TRUE;
        if (FAILED(m_device->CreateRasterizerState(&skyboxRsDesc, m_skyboxRasterizerState.ReleaseAndGetAddressOf())))
            return false;

        // Skybox Constant Buffer 생성
        D3D11_BUFFER_DESC skyboxCbDesc = {};
        skyboxCbDesc.ByteWidth = sizeof(XMMATRIX);
        skyboxCbDesc.Usage = D3D11_USAGE_DYNAMIC;
        skyboxCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        skyboxCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&skyboxCbDesc, nullptr, m_cbSkybox.ReleaseAndGetAddressOf())))
            return false;

        // 톤매핑 Pixel Shader는 CreateToneMappingResources에서 HDR 지원 여부에 따라 생성합니다.

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

        // ===================== Shadow Pass Shaders =====================
        // Static shadow VS + input layout (POSITION only)
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::ShadowVS,
                              strlen(DeferredShader::ShadowVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Shadow VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_shadowVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Shadow VS");
            return false;
        }
        {
            D3D11_INPUT_ELEMENT_DESC il[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            if (FAILED(m_device->CreateInputLayout(il,
                                                   ARRAYSIZE(il),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_shadowInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Shadow InputLayout");
                return false;
            }
        }

        // Static shadow instanced VS + input layout
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::ShadowInstancedVS,
                              strlen(DeferredShader::ShadowInstancedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Shadow Instanced VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_shadowInstancedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Shadow Instanced VS");
            return false;
        }
        {
            D3D11_INPUT_ELEMENT_DESC instancedLayout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0},
                {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            };
            if (FAILED(m_device->CreateInputLayout(instancedLayout,
                                                   ARRAYSIZE(instancedLayout),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_shadowInstancedInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Shadow Instanced InputLayout");
                return false;
            }
        }

        // Skinned shadow VS (input layout은 m_gBufferSkinnedInputLayout을 그대로 사용)
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::ShadowSkinnedVS,
                              strlen(DeferredShader::ShadowSkinnedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Shadow Skinned VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_shadowSkinnedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Shadow Skinned VS");
            return false;
        }

        // Skinned shadow instanced VS + input layout
        vsBlob.Reset();
        errorBlob.Reset();
        if (FAILED(D3DCompile(DeferredShader::ShadowSkinnedInstancedVS,
                              strlen(DeferredShader::ShadowSkinnedInstancedVS),
                              nullptr, nullptr, nullptr,
                              "main", "vs_5_0",
                              0, 0,
                              vsBlob.GetAddressOf(),
                              errorBlob.GetAddressOf())))
        {
            if (errorBlob)
                ALICE_LOG_ERRORF("Shadow Skinned Instanced VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
        }
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                vsBlob->GetBufferSize(),
                                                nullptr,
                                                m_shadowSkinnedInstancedVS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Shadow Skinned Instanced VS");
            return false;
        }
        {
            D3D11_INPUT_ELEMENT_DESC skinnedInstancedLayoutShadow[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,          0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,     0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"SMOOTHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            };
            if (FAILED(m_device->CreateInputLayout(skinnedInstancedLayoutShadow,
                                                   ARRAYSIZE(skinnedInstancedLayoutShadow),
                                                   vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(),
                                                   m_shadowSkinnedInstancedInputLayout.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Shadow Skinned Instanced InputLayout");
                return false;
            }
        }

        return true;
    }

    bool DeferredRenderSystem::CreateQuadGeometry()
    {
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
            return false;

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
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateConstantBuffers()
    {
        // PerObject CB (Forward와 동일한 구조: 행렬 + 재질 정보 + 셰이딩 모드)
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(CBPerObject);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbPerObject.ReleaseAndGetAddressOf())))
            return false;

        // Lighting CB (Deferred Light 패스용 - ConstantBuffer register(b0))
        // HLSL의 ConstantBuffer 구조체 크기에 맞춰야 함 (대략 512바이트 이상)
        cbDesc.ByteWidth = 4096; // 충분한 크기
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbLighting.ReleaseAndGetAddressOf())))
            return false;

        // Directional Light CB
        cbDesc.ByteWidth = sizeof(DirectX::XMFLOAT4) * 2 + sizeof(float) * 4; // dir, color, intensity, pad
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbDirectionalLight.ReleaseAndGetAddressOf())))
            return false;

        // Extra Lights CB (Point/Spot/Rect)
        cbDesc.ByteWidth = (sizeof(ExtraLightsCB) + 15u) & ~15u;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbExtraLights.ReleaseAndGetAddressOf())))
            return false;

        // Bones CB
        cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) * 1023 + sizeof(std::uint32_t) * 4;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbBones.ReleaseAndGetAddressOf())))
            return false;

        // PostProcess CB
        cbDesc.ByteWidth = sizeof(PostProcessCB) * 4; // exposure, maxHDRNits, padding
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbPostProcess.ReleaseAndGetAddressOf())))
            return false;

        // Bloom CB
        cbDesc.ByteWidth = sizeof(BloomCB);
        cbDesc.ByteWidth = (cbDesc.ByteWidth + 15u) & ~15u; // 16-byte alignment
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbBloom.ReleaseAndGetAddressOf())))
            return false;

        // Decal CB
        {
            cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) * 3 + sizeof(DirectX::XMFLOAT4) * 2 + sizeof(float) * 4;
            cbDesc.ByteWidth = (cbDesc.ByteWidth + 15u) & ~15u;
            if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbDecal.ReleaseAndGetAddressOf())))
                return false;
        }

        // Transparent Forward-Style Light CB (register(b1))
        // float3 dir + float intensity + float3 color + pad + float3 camPos + pad = 48 bytes (16B 정렬)
        cbDesc.ByteWidth = sizeof(float) * 12;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbTransparentLight.ReleaseAndGetAddressOf())))
            return false;

        // Shadow CB (register(b4))
        // float4x4(64) + float3(12) + int(4) + float3 pad(12) = 92 -> 96(16B align)
        {
            cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX) + sizeof(float) * 3 + sizeof(int) + sizeof(float) * 3;
            cbDesc.ByteWidth = (cbDesc.ByteWidth + 15u) & ~15u;
            if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_cbShadow.ReleaseAndGetAddressOf())))
                return false;
        }


        return true;
    }

    bool DeferredRenderSystem::CreateCubeGeometry()
    {
        // ForwardRenderSystem::SimpleVertex와 동일한 구조체
        struct SimpleVertex
        {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
            XMFLOAT2 TexCoord;
        };

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

        m_cubeIndexCount = (UINT)std::size(i);

        D3D11_BUFFER_DESC desc = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA data = { v, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&desc, &data, m_cubeVB.ReleaseAndGetAddressOf()))) return false;

        desc.ByteWidth = sizeof(i);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        data.pSysMem = i;
        if (FAILED(m_device->CreateBuffer(&desc, &data, m_cubeIB.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool DeferredRenderSystem::CreateSamplerStates()
    {
        D3D11_SAMPLER_DESC sDesc = {};
        sDesc.Filter = D3D11_FILTER_ANISOTROPIC;
        sDesc.MaxAnisotropy = 4;
        sDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_samplerState.ReleaseAndGetAddressOf())))
            return false;

        // Shadow Sampler
        sDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        sDesc.MaxAnisotropy = 1;
        sDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_shadowSampler.ReleaseAndGetAddressOf())))
            return false;

        // Linear Sampler
        sDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sDesc.MaxAnisotropy = 1;
        sDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&sDesc, m_samplerLinear.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateBlendStates()
    {
        // Additive Blend State (라이트 패스용)
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&blendDesc, m_blendStateAdditive.ReleaseAndGetAddressOf())))
            return false;

        // Alpha Blend State (반투명 Forward-Style 패스용)
        D3D11_BLEND_DESC alphaDesc = {};
        alphaDesc.RenderTarget[0].BlendEnable = TRUE;
        alphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        alphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        alphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        alphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        alphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        alphaDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        alphaDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&alphaDesc, m_alphaBlendState.ReleaseAndGetAddressOf())))
            return false;

        // Decal Blend State (premultiplied alpha)
        D3D11_BLEND_DESC decalDesc = {};
        decalDesc.RenderTarget[0].BlendEnable = TRUE;
        decalDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        decalDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        decalDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        decalDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        decalDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        decalDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        decalDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(m_device->CreateBlendState(&decalDesc, m_decalBlendState.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateRasterizerStates()
    {
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.CullMode = D3D11_CULL_NONE;
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthBias = 0;
        rsDesc.DepthBiasClamp = 0.0f;
        rsDesc.SlopeScaledDepthBias = 0.0f;
        rsDesc.DepthClipEnable = TRUE;
        rsDesc.ScissorEnable = FALSE;
        rsDesc.MultisampleEnable = FALSE;
        rsDesc.AntialiasedLineEnable = FALSE;
        if (FAILED(m_device->CreateRasterizerState(&rsDesc, m_rasterizerState.ReleaseAndGetAddressOf())))
            return false;

        // Shadow pass RS (Depth Bias)
        {
            D3D11_RASTERIZER_DESC s = rsDesc;
            s.CullMode = D3D11_CULL_BACK;
            s.DepthBias = 1000;
            s.SlopeScaledDepthBias = 1.0f;
            s.FrontCounterClockwise = TRUE;
            if (FAILED(m_device->CreateRasterizerState(&s, m_shadowRasterizerState.ReleaseAndGetAddressOf())))
                return false;

            s.FrontCounterClockwise = FALSE;
            if (FAILED(m_device->CreateRasterizerState(&s, m_shadowRasterizerStateReversed.ReleaseAndGetAddressOf())))
                return false;
        }

        // 아웃라인용 Rasterizer State (Cull Front)
        // - 정점을 법선 방향으로 확장한 뒤, 뒷면(Back Face)을 그리면 원본 물체 뒤로 테두리가 나타납니다.
        D3D11_RASTERIZER_DESC outlineDesc = rsDesc;
        outlineDesc.CullMode = D3D11_CULL_FRONT;    // 앞면을 제거하고 뒷면을 그림
        if (FAILED(m_device->CreateRasterizerState(&outlineDesc, m_rsCullFront.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateDepthStencilStates()
    {
        // 기본 Depth Stencil State
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
        dsDesc.StencilEnable = FALSE;
        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.ReleaseAndGetAddressOf())))
            return false;

        // Read Only Depth Stencil State (라이트 패스용)
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_depthStencilStateReadOnly.ReleaseAndGetAddressOf())))
            return false;

        return true;
    }

    bool DeferredRenderSystem::CreateInstanceBuffer(std::uint32_t initialCapacity)
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
            ALICE_LOG_ERRORF("DeferredRenderSystem::CreateInstanceBuffer: CreateBuffer failed. hr=0x%08X", (unsigned)hr);
            return false;
        }

        m_instanceCapacity = initialCapacity;
        return true;
    }

    bool DeferredRenderSystem::EnsureInstanceBufferCapacity(std::size_t requiredCount)
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

    std::uint32_t DeferredRenderSystem::GetShadowMapSizePx() const
    {
        std::uint32_t baseSize = m_shadowSettings.mapSizePx;
        std::uint32_t scale = (m_shadowResolutionScale == 0) ? 1u : m_shadowResolutionScale;
        std::uint32_t size = baseSize / scale;
        if (size < 256u) size = 256u;
        return size;
    }

    bool DeferredRenderSystem::EnsureShadowMapResources()
    {
        const std::uint32_t desiredSize = GetShadowMapSizePx();
        if (desiredSize == 0)
            return false;

        if (desiredSize == m_shadowMapSizePxEffective &&
            m_shadowTex && m_shadowDSV && m_shadowSRV)
        {
            return true;
        }

        m_shadowCacheDirty = true;  
        return CreateShadowMapResources();
    }

    bool DeferredRenderSystem::CreateShadowMapResources()
    {
        const UINT size = (UINT)GetShadowMapSizePx();
        if (size == 0) return false;

        m_shadowTex.Reset();
        m_shadowDSV.Reset();
        m_shadowSRV.Reset();

        // 1) Shadow map texture (typeless)
        D3D11_TEXTURE2D_DESC tDesc = { size, size, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0},
                                       D3D11_USAGE_DEFAULT,
                                       D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                                       0, 0 };
        if (FAILED(m_device->CreateTexture2D(&tDesc, nullptr, m_shadowTex.ReleaseAndGetAddressOf())))
            return false;

        // 2) DSV
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = { DXGI_FORMAT_D32_FLOAT, D3D11_DSV_DIMENSION_TEXTURE2D, 0 };
        if (FAILED(m_device->CreateDepthStencilView(m_shadowTex.Get(), &dsvDesc, m_shadowDSV.ReleaseAndGetAddressOf())))
            return false;

        // 3) SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = { DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2D, 0 };
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(m_device->CreateShaderResourceView(m_shadowTex.Get(), &srvDesc, m_shadowSRV.ReleaseAndGetAddressOf())))
            return false;

        // 4) Viewport
        m_shadowViewport = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };
        m_shadowMapSizePxEffective = size;

        return true;
    }

    bool DeferredRenderSystem::CreateToneMappingResources(const std::uint32_t& width, const std::uint32_t& height)
    {
		// 씬 렌더 타겟 생성 (HDR 포맷: 톤매핑을 위해 R16G16B16A16_FLOAT 사용)
		m_sceneWidth = width;
		m_sceneHeight = height;
		D3D11_TEXTURE2D_DESC cDesc = { width, height, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		if (FAILED(m_device->CreateTexture2D(&cDesc, nullptr, m_sceneColorTex.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateRenderTargetView(m_sceneColorTex.Get(), nullptr, m_sceneRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateShaderResourceView(m_sceneColorTex.Get(), nullptr, m_sceneColorSRV.ReleaseAndGetAddressOf()))) return false;

		// 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped)
		D3D11_TEXTURE2D_DESC vDesc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
		if (FAILED(m_device->CreateTexture2D(&vDesc, nullptr, m_viewportTex.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateRenderTargetView(m_viewportTex.Get(), nullptr, m_viewportRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateShaderResourceView(m_viewportTex.Get(), nullptr, m_viewportSRV.ReleaseAndGetAddressOf()))) return false;

		// Depth Texture & View (DSV, SRV) - SRV 생성을 위해 typeless 포맷 사용
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
			ALICE_LOG_WARN("DeferredRenderSystem::CreateToneMappingResources: CreateShaderResourceView(depthSRV) failed (0x%08X) - depth test disabled", (unsigned)hrDepthSRV);
			m_sceneDepthSRV.Reset();
			// 초기화는 성공으로 계속 진행 (depth test 없이 렌더링)
		}

        // HDR 지원 여부 확인 및 적절한 톤매핑 셰이더 선택
        ComPtr<ID3DBlob> psBlob, errorBlob;
        float maxNits = 100.0f;
        bool isHDRSupported = m_renderDevice.IsHDRSupported(maxNits);
        const char* toneMappingShaderSource = isHDRSupported ? CommonShaderCode::ToneMappingPS_HDR : CommonShaderCode::ToneMappingPS_LDR;
        const char* shaderName = isHDRSupported ? "HDR" : "LDR";

        // Tone Mapping Pixel Shader 컴파일
        if (FAILED(D3DCompile(toneMappingShaderSource, strlen(toneMappingShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
        {
            if (errorBlob)
            {
                ALICE_LOG_ERRORF("Tone Mapping PS (%s) compile error: %s", shaderName, (char*)errorBlob->GetBufferPointer());
            }
            return false;
        }
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_toneMappingPS.ReleaseAndGetAddressOf())))
        {
            ALICE_LOG_ERRORF("Failed to create Tone Mapping PS (%s)", shaderName);
            return false;
        }

        if (isHDRSupported)
        {
            ALICE_LOG_INFO("DeferredRenderSystem::CreateToneMappingResources: HDR 톤매핑 셰이더 사용. MaxNits: %.1f", maxNits);
        }
        else
        {
            ALICE_LOG_INFO("DeferredRenderSystem::CreateToneMappingResources: LDR 톤매핑 셰이더 사용.");
        }

        // 파티클 오버레이 Pixel Shader 생성
        {
            ComPtr<ID3DBlob> psBlob, errorBlob;
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
        }

        // 파티클 오버레이용 Additive Blend State 생성
        {
            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(m_device->CreateBlendState(&blendDesc, m_ppBlendAdditive.ReleaseAndGetAddressOf())))
            {
                ALICE_LOG_ERRORF("Failed to create Particle Overlay Additive Blend State");
                return false;
            }
        }

        return true;
    }

    bool DeferredRenderSystem::CreateBloomResources(const std::uint32_t& width, const std::uint32_t& height)
    {
        // 레벨 0: viewport/downsample
        std::uint32_t level0Width = width / m_bloomSettings.downsample;
        std::uint32_t level0Height = height / m_bloomSettings.downsample;
        
        // 최소 크기 보장
        if (level0Width == 0) level0Width = 1;
        if (level0Height == 0) level0Height = 1;
        
        // 각 레벨의 해상도 계산 및 텍스처 생성
        for (int level = 0; level < BLOOM_LEVEL_COUNT; ++level)
        {
            std::uint32_t levelWidth = level0Width;
            std::uint32_t levelHeight = level0Height;
            
            // 레벨 1~4는 이전 레벨의 절반
            for (int i = 0; i < level; ++i)
            {
                levelWidth = (levelWidth > 1) ? (levelWidth / 2) : 1;
                levelHeight = (levelHeight > 1) ? (levelHeight / 2) : 1;
            }
            
            // 최소 크기 보장
            if (levelWidth == 0) levelWidth = 1;
            if (levelHeight == 0) levelHeight = 1;
            
            m_bloomLevelWidth[level] = levelWidth;
            m_bloomLevelHeight[level] = levelHeight;
            
            // 각 레벨마다 ping-pong 텍스처 2장 (A/B) 생성
            D3D11_TEXTURE2D_DESC bloomDesc = { levelWidth, levelHeight, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
            
            for (int pingPong = 0; pingPong < 2; ++pingPong)
            {
                if (FAILED(m_device->CreateTexture2D(&bloomDesc, nullptr, m_bloomLevelTex[level][pingPong].ReleaseAndGetAddressOf()))) return false;
                if (FAILED(m_device->CreateRenderTargetView(m_bloomLevelTex[level][pingPong].Get(), nullptr, m_bloomLevelRTV[level][pingPong].ReleaseAndGetAddressOf()))) return false;
                if (FAILED(m_device->CreateShaderResourceView(m_bloomLevelTex[level][pingPong].Get(), nullptr, m_bloomLevelSRV[level][pingPong].ReleaseAndGetAddressOf()))) return false;
            }


        }

        // Bloom 셰이더 컴파일 (한 번만 컴파일하므로 이미 생성되어 있으면 스킵)
        if (!m_bloomBrightPassPS)
        {
            ComPtr<ID3DBlob> psBlob, errorBlob;

            // Bright Pass PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomBrightPassPS, strlen(CommonShaderCode::BloomBrightPassPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Bright Pass PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomBrightPassPS.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Blur Pass PS (Horizontal)
            if (FAILED(D3DCompile(CommonShaderCode::BloomBlurPassPS_H, strlen(CommonShaderCode::BloomBlurPassPS_H), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Blur Pass PS (H) compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomBlurPassPS_H.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Blur Pass PS (Vertical)
            if (FAILED(D3DCompile(CommonShaderCode::BloomBlurPassPS_V, strlen(CommonShaderCode::BloomBlurPassPS_V), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Blur Pass PS (V) compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomBlurPassPS_V.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Downsample PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomDownsamplePS, strlen(CommonShaderCode::BloomDownsamplePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Downsample PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomDownsamplePS.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Upsample PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomUpsamplePS, strlen(CommonShaderCode::BloomUpsamplePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Upsample PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomUpsamplePS.ReleaseAndGetAddressOf()))) return false;

            psBlob.Reset();
            errorBlob.Reset();

            // Composite PS
            if (FAILED(D3DCompile(CommonShaderCode::BloomCompositePS, strlen(CommonShaderCode::BloomCompositePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errorBlob.GetAddressOf())))
            {
                if (errorBlob) ALICE_LOG_ERRORF("Bloom Composite PS compile error: %s", (char*)errorBlob->GetBufferPointer());
                return false;
            }
            if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_bloomCompositePS.ReleaseAndGetAddressOf()))) return false;
        }

        ALICE_LOG_INFO("DeferredRenderSystem::CreateBloomResources: success. Level0: (%dx%d)", m_bloomLevelWidth[0], m_bloomLevelHeight[0]);
        return true;
    }

    bool DeferredRenderSystem::CreatePostBloomResources(const std::uint32_t& width, const std::uint32_t& height)
	{
		// Bloom 합성(HDR) -> ToneMapping 입력으로 쓸 중간 텍스처
		m_postBloomTex.Reset();
		m_postBloomRTV.Reset();
		m_postBloomSRV.Reset();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR 유지
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_postBloomTex.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateRenderTargetView(m_postBloomTex.Get(), nullptr, m_postBloomRTV.ReleaseAndGetAddressOf()))) return false;
		if (FAILED(m_device->CreateShaderResourceView(m_postBloomTex.Get(), nullptr, m_postBloomSRV.ReleaseAndGetAddressOf()))) return false;

        return true;
	}


    bool DeferredRenderSystem::CreateIblResources(const std::string& iblDir, const std::string& iblName, const std::string& iblSuffix)
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

    DirectX::XMMATRIX DeferredRenderSystem::RenderShadowPass(
        const World& world,
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& skinnedCommands,
        const std::unordered_set<EntityId>& cameraEntities,
        bool editorMode,
        bool isPlaying)
    {
        using namespace DirectX;

        if (!m_shadowDSV || !m_shadowVS || !m_shadowSkinnedVS) return XMMatrixIdentity();

       // 1) 라이트 방향: 에디터 UI에서 바뀌는 keyDirection을 그대로 반영
       auto GetSafeDir = [](const DirectX::XMFLOAT3& v) {
        DirectX::XMVECTOR vv = DirectX::XMLoadFloat3(&v);
        return DirectX::XMVector3Equal(vv, DirectX::XMVectorZero())
            ? DirectX::XMVectorSet(0, -1, 0, 0)
            : DirectX::XMVector3Normalize(vv);
        };
        XMVECTOR lightDir = GetSafeDir(m_lightingParameters.keyDirection);

        // 2) 씬 바운딩 계산 (카메라 엔티티 제외)
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

        if (!hasObjects)
        {
            minP = { -10.0f, -10.0f, -10.0f };
            maxP = { 10.0f, 10.0f, 10.0f };
        }

        // 3) Focus/Radius
        XMVECTOR vMin = XMLoadFloat3(&minP);
        XMVECTOR vMax = XMLoadFloat3(&maxP);
        XMVECTOR focus = (vMin + vMax) * 0.5f;

        XMVECTOR diagonal = XMVector3Length(vMax - vMin);
        float sceneRadius = XMVectorGetX(diagonal) * 0.5f;

        float r = (std::max)(m_shadowSettings.orthoRadius, sceneRadius);
        r *= 1.5f;

        // 4) lightView/lightProj
        float distFromCenter = r * 3.0f;
        XMVECTOR lightPos = focus - lightDir * distFromCenter;

        XMVECTOR up = (fabsf(XMVectorGetX(XMVector3Dot(XMVectorSet(0, 1, 0, 0), lightDir))) > 0.99f)
            ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);

        XMMATRIX lightView = XMMatrixLookToLH(lightPos, lightDir, up);

        float nearZ = 0.01f;
        float farZ = distFromCenter + r * 2.0f;
        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(-r, r, -r, r, nearZ, farZ);

        // 5) Texel snapping
        XMVECTOR focusLS = XMVector3TransformCoord(focus, lightView);
        const float shadowMapSize = (m_shadowMapSizePxEffective > 0) ? (float)m_shadowMapSizePxEffective
                                                                     : (float)m_shadowSettings.mapSizePx;
        float texelWorld = (2.0f * r) / shadowMapSize;
        float snapX = floorf(XMVectorGetX(focusLS) / texelWorld) * texelWorld;
        float snapY = floorf(XMVectorGetY(focusLS) / texelWorld) * texelWorld;
        lightView = XMMatrixTranslation(snapX - XMVectorGetX(focusLS), snapY - XMVectorGetY(focusLS), 0.0f) * lightView;

        XMMATRIX lightViewProj = lightView * lightProj;

        // --- Render Shadow Depth ---
        // SRV(t7) 바인딩 해제 (DSV 충돌 방지)
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        m_context->PSSetShaderResources(7, 1, nullSRV);

        m_context->RSSetViewports(1, &m_shadowViewport);
        m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
        m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

        // Depth-only: PS none
        m_context->PSSetShader(nullptr, nullptr, 0);

        // 프러스텀 컬링을 위한 카메라 절두체 계산 (성능 확보를 위해 카메라 프러스텀 사용)
        BoundingFrustum cameraFrustum = camera.GetWorldFrustum();

        // 1) Static meshes (cube)
        if (m_cubeVB && m_cubeIB && m_shadowInputLayout && m_shadowVS && m_cubeIndexCount > 0)
        {
            UINT stride = sizeof(DirectX::XMFLOAT3) * 2 + sizeof(DirectX::XMFLOAT2); // SimpleVertex(Position,Normal,Tex)
            UINT offset = 0;
            ID3D11Buffer* vb = m_cubeVB.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);
            m_context->IASetInputLayout(m_shadowInputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_shadowVS.Get(), nullptr, 0);

            struct ShadowStaticInstancedKey
            {
                ID3D11Buffer* vertexBuffer = nullptr;
                ID3D11Buffer* indexBuffer = nullptr;
                UINT stride = 0;
                UINT startIndex = 0;
                UINT indexCount = 0;
                INT baseVertex = 0;
                bool flipped = false;

                bool operator<(const ShadowStaticInstancedKey& rhs) const
                {
                    if (vertexBuffer != rhs.vertexBuffer) return vertexBuffer < rhs.vertexBuffer;
                    if (indexBuffer != rhs.indexBuffer) return indexBuffer < rhs.indexBuffer;
                    if (stride != rhs.stride) return stride < rhs.stride;
                    if (startIndex != rhs.startIndex) return startIndex < rhs.startIndex;
                    if (indexCount != rhs.indexCount) return indexCount < rhs.indexCount;
                    if (baseVertex != rhs.baseVertex) return baseVertex < rhs.baseVertex;
                    return flipped < rhs.flipped;
                }
            };

            struct ShadowStaticInstancedItem
            {
                ShadowStaticInstancedKey key;
                InstanceData instance;
                bool operator<(const ShadowStaticInstancedItem& rhs) const
                {
                    return key < rhs.key;
                }
            };

            std::vector<ShadowStaticInstancedItem> staticInstancedItems;
            staticInstancedItems.reserve(transforms.size());

            for (const auto& [id, tr] : transforms)
            {
                if (cameraEntities.contains(id)) continue;
                if (world.GetComponent<SkinnedMeshComponent>(id)) continue;
                if (!world.GetComponent<MaterialComponent>(id)) continue;
                if (!tr.enabled || !tr.visible) continue;

                // [프러스텀 컬링] 카메라 시야 밖 오브젝트는 건너뛰기
                float maxScale = std::max({ tr.scale.x, tr.scale.y, tr.scale.z });
                BoundingSphere bounds(tr.position, maxScale * 1.5f);
                if (cameraFrustum.Contains(bounds) == DISJOINT)
                {
                    continue; // 화면에 보이지 않으면 렌더링하지 않음
                }

                XMMATRIX worldM = BuildWorldMatrix(world, id, tr);

                const bool flipped = XMVectorGetX(XMMatrixDeterminant(worldM)) < 0.0f;
                const bool canStaticInstance = m_shadowInstancedVS &&
                                               m_shadowInstancedInputLayout &&
                                               m_instanceBuffer;

                if (canStaticInstance)
                {
                    ShadowStaticInstancedItem item{};
                    item.key.vertexBuffer = m_cubeVB.Get();
                    item.key.indexBuffer = m_cubeIB.Get();
                    item.key.stride = stride;
                    item.key.startIndex = 0;
                    item.key.indexCount = m_cubeIndexCount;
                    item.key.baseVertex = 0;
                    item.key.flipped = flipped;
                    item.instance = BuildInstanceData(worldM);

                    staticInstancedItems.push_back(item);
                    continue;
                }

                if (flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

                UpdatePerObjectCB(worldM, lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, 1.0f, false, false, 0,
                                   1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                   1.0f, 1.0f,
                                   XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
            }

            if (!staticInstancedItems.empty() && m_shadowInstancedVS && m_shadowInstancedInputLayout)
            {
                std::sort(staticInstancedItems.begin(), staticInstancedItems.end());

                if (EnsureInstanceBufferCapacity(staticInstancedItems.size()))
                {
                    m_context->IASetInputLayout(m_shadowInstancedInputLayout.Get());
                    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    m_context->VSSetShader(m_shadowInstancedVS.Get(), nullptr, 0);

                    std::vector<InstanceData> batchInstances;
                    batchInstances.reserve(staticInstancedItems.size());

                    ShadowStaticInstancedKey currentKey = staticInstancedItems.front().key;
                    batchInstances.clear();

                    for (const auto& item : staticInstancedItems)
                    {
                        const bool sameKey = !(currentKey < item.key) && !(item.key < currentKey);

                        if (!sameKey || batchInstances.size() >= m_instanceCapacity)
                        {
                            if (!batchInstances.empty())
                            {
                                if (currentKey.flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

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
                                m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R16_UINT, 0);

                                UpdatePerObjectCB(DirectX::XMMatrixIdentity(), lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, 1.0f, false, false, 0,
                                                   1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                                   1.0f, 1.0f,
                                                   XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                                m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(),
                                                                currentKey.startIndex, currentKey.baseVertex, 0);
                            }

                            currentKey = item.key;
                            batchInstances.clear();
                        }

                        batchInstances.push_back(item.instance);
                    }

                    if (!batchInstances.empty())
                    {
                        if (currentKey.flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                        else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

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
                        m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R16_UINT, 0);

                        UpdatePerObjectCB(DirectX::XMMatrixIdentity(), lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, 1.0f, false, false, 0,
                                           1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                           1.0f, 1.0f,
                                           XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                        m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(),
                                                        currentKey.startIndex, currentKey.baseVertex, 0);
                    }
                }
            }
        }

        // 2) Skinned meshes
        if (!skinnedCommands.empty() && m_gBufferSkinnedInputLayout && m_shadowSkinnedVS)
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
                bool flipped = false;

                bool operator<(const ShadowInstancedKey& rhs) const
                {
                    if (vertexBuffer != rhs.vertexBuffer) return vertexBuffer < rhs.vertexBuffer;
                    if (indexBuffer != rhs.indexBuffer) return indexBuffer < rhs.indexBuffer;
                    if (stride != rhs.stride) return stride < rhs.stride;
                    if (startIndex != rhs.startIndex) return startIndex < rhs.startIndex;
                    if (indexCount != rhs.indexCount) return indexCount < rhs.indexCount;
                    if (baseVertex != rhs.baseVertex) return baseVertex < rhs.baseVertex;
                    return flipped < rhs.flipped;
                }
            };

            struct ShadowInstancedItem
            {
                ShadowInstancedKey key;
                InstanceData instance;
                bool operator<(const ShadowInstancedItem& rhs) const
                {
                    return key < rhs.key; // 내부의 key끼리 비교
                }
            };

            std::vector<ShadowInstancedItem> instancedItems;
            instancedItems.reserve(skinnedCommands.size());

            UINT offset = 0;
            m_context->IASetInputLayout(m_gBufferSkinnedInputLayout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_shadowSkinnedVS.Get(), nullptr, 0);

            // 2-1) 인스턴싱 대상 수집 + 일반 렌더링
            for (const auto& cmd : skinnedCommands)
            {
                if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;
                if (cmd.transparent) continue;
                if (cmd.transparent) continue;

                // [프러스텀 컬링] 월드 행렬에서 위치 추출
                XMFLOAT4X4 worldMatrix;
                XMStoreFloat4x4(&worldMatrix, cmd.world);
                XMFLOAT3 position(worldMatrix._41, worldMatrix._42, worldMatrix._43);
                
                // 스케일 추정: 월드 행렬의 스케일 성분 추출 (간단한 근사)
                XMVECTOR scaleVec = XMVectorSet(
                    XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._11, worldMatrix._12, worldMatrix._13, 0.0f))),
                    XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._21, worldMatrix._22, worldMatrix._23, 0.0f))),
                    XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._31, worldMatrix._32, worldMatrix._33, 0.0f))),
                    0.0f
                );
                float maxScale = std::max({ XMVectorGetX(scaleVec), XMVectorGetY(scaleVec), XMVectorGetZ(scaleVec) });
                BoundingSphere bounds(position, maxScale * 1.5f);
                if (cameraFrustum.Contains(bounds) == DISJOINT)
                {
                    continue; // 화면에 보이지 않으면 렌더링하지 않음
                }

                // 본 1개 + Identity인 경우만 인스턴싱 대상으로 처리
                if (IsRigidSkinnedCommand(cmd) &&
                    m_shadowSkinnedInstancedVS &&
                    m_shadowSkinnedInstancedInputLayout &&
                    m_instanceBuffer)
                {
                    ShadowInstancedItem item{};
                    item.key.vertexBuffer = cmd.vertexBuffer;
                    item.key.indexBuffer = cmd.indexBuffer;
                    item.key.stride = cmd.stride;
                    item.key.startIndex = cmd.startIndex;
                    item.key.indexCount = cmd.indexCount;
                    item.key.baseVertex = cmd.baseVertex;
                    item.key.flipped = XMVectorGetX(XMMatrixDeterminant(cmd.world)) < 0.0f;
                    item.instance = BuildInstanceData(cmd.world);

                    instancedItems.push_back(item);
                    continue;
                }

                // 일반 스키닝 렌더링
                UINT sStride = cmd.stride;
                m_context->IASetVertexBuffers(0, 1, &cmd.vertexBuffer, &sStride, &offset);
                m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                const bool flipped = XMVectorGetX(XMMatrixDeterminant(cmd.world)) < 0.0f;
                if (flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

                UpdateBonesCB(cmd.bones, cmd.boneCount);
                UpdatePerObjectCB(cmd.world, lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, 1.0f, false, false, 0,
                                   1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                   1.0f, 1.0f,
                                   XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
            }

            // 2-2) 인스턴싱 배치 렌더링
            if (!instancedItems.empty() && m_shadowSkinnedInstancedVS && m_shadowSkinnedInstancedInputLayout)
            {
                std::sort(instancedItems.begin(), instancedItems.end());

                if (EnsureInstanceBufferCapacity(instancedItems.size()))
                {
                    m_context->IASetInputLayout(m_shadowSkinnedInstancedInputLayout.Get());
                    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    m_context->VSSetShader(m_shadowSkinnedInstancedVS.Get(), nullptr, 0);

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
                                // RS 설정 (뒤집힘 여부)
                                if (currentKey.flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                                else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

                                // 인스턴스 버퍼 업데이트
                                D3D11_MAPPED_SUBRESOURCE mapped{};
                                if (SUCCEEDED(m_context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                                {
                                    std::memcpy(mapped.pData, batchInstances.data(), sizeof(InstanceData) * batchInstances.size());
                                    m_context->Unmap(m_instanceBuffer.Get(), 0);
                                }

                                // 버퍼 바인딩 (Slot0: VB, Slot1: Instance)
                                UINT strides[2] = { currentKey.stride, sizeof(InstanceData) };
                                UINT offsets[2] = { 0, 0 };
                                ID3D11Buffer* bufs[2] = { currentKey.vertexBuffer, m_instanceBuffer.Get() };
                                m_context->IASetVertexBuffers(0, 2, bufs, strides, offsets);
                                m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                                // CB는 배치 단위로 1회만 갱신
                                UpdatePerObjectCB(DirectX::XMMatrixIdentity(), lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, 1.0f, false, false, 0,
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

                    // 마지막 배치 플러시
                    if (!batchInstances.empty())
                    {
                        if (currentKey.flipped && m_shadowRasterizerStateReversed) m_context->RSSetState(m_shadowRasterizerStateReversed.Get());
                        else if (m_shadowRasterizerState) m_context->RSSetState(m_shadowRasterizerState.Get());

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

                        UpdatePerObjectCB(DirectX::XMMatrixIdentity(), lightView, lightProj, XMFLOAT4(1, 1, 1, 1), 1.0f, 0.0f, 1.0f, false, false, 0,
                                           1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                           1.0f, 1.0f,
                                           XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                        m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(), currentKey.startIndex, currentKey.baseVertex, 0);
                    }
                }
            }
        }

        return lightViewProj;
    }

    void DeferredRenderSystem::Render(const World& world,
                                      const Camera& camera,
                                      EntityId entity,
                                      const std::unordered_set<EntityId>& cameraEntities,
                                      int shadingMode,
                                      bool enableFillLight,
                                      const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                      bool editorMode,
                                      bool isPlaying)
    {
        if (!m_device || !m_context) return;

        // 실제로 사용한 카메라 정보 저장 (ComputeEffect용)
        m_lastViewProj = camera.GetViewProjectionMatrix();
        m_lastCameraPos = camera.GetPosition();

        // Viewport 설정
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // Shadow pass (캐시 + N프레임 갱신)
        ++m_shadowFrameIndex;
        const bool shadowResourcesReady = EnsureShadowMapResources();
        const bool lightChanged = !NearlyEqualFloat3(m_lightingParameters.keyDirection, m_lastShadowLightDir);
        const bool enabledChanged = (m_shadowSettings.enabled != m_shadowEnabledLast);
        if (enabledChanged) m_shadowCacheDirty = true;

        const bool intervalHit = (m_shadowUpdateInterval <= 1) ||
                                 ((m_shadowFrameIndex % m_shadowUpdateInterval) == 0);

        bool shouldUpdateShadow = m_shadowSettings.enabled && shadowResourcesReady &&
                                  (m_shadowCacheDirty || lightChanged || intervalHit);

        DirectX::XMMATRIX lightViewProj = m_lastShadowViewProj;
        if (shouldUpdateShadow)
        {
            lightViewProj = RenderShadowPass(world, camera, skinnedCommands, cameraEntities, editorMode, isPlaying);
            m_lastShadowViewProj = lightViewProj;
            m_shadowLastUpdateFrame = m_shadowFrameIndex;
            m_shadowCacheDirty = false;
            m_lastShadowLightDir = m_lightingParameters.keyDirection;
        }
        m_shadowEnabledLast = m_shadowSettings.enabled;

        // ShadowPass에서 viewport가 섀도우맵 해상도로 바뀌므로, 씬 뷰포트를 다시 설정
        m_context->RSSetViewports(1, &vp);

        // G-Buffer 패스
        PassGBuffer(world, camera, skinnedCommands, cameraEntities, shadingMode, editorMode, isPlaying, InvalidEntityId);

        // Decal 패스 (DBuffer)
        PassDecals(world, camera);

        // Deferred Light 패스 (IBL 포함)
        PassDeferredLight(world, camera, shadingMode, enableFillLight, lightViewProj);


        // 스카이박스 렌더링
        if (m_skyboxEnabled)
        {
			m_context->RSSetViewports(1, &vp);
			RenderSkybox(camera);
        }


        // 반투명(알파 블렌딩) 오브젝트는 라이트 패스 이후 Forward-Style로 합성
        PassTransparentForward(camera, skinnedCommands, shadingMode);
        
        // TrailEffectRenderSystem 렌더링 (IBL 패스 이후)
        if (m_trailRenderSystem)
        {
            m_trailRenderSystem->Render(world, camera);
        }

        // Post Process Volume 블렌딩 (카메라 위치 기준)
        {
            // 기본 설정: EditorCore의 Default Settings 사용 (설정되지 않았으면 현재 m_postProcessParams 사용)
            PostProcessSettings defaultSettings = m_defaultPostProcessSettings;
            
            // EditorCore의 Default Settings가 설정되지 않았으면 현재 m_postProcessParams 사용
            if (!m_hasDefaultPostProcessSettings)
            {
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
                // Bloom 기본 설정
                defaultSettings.bloomThreshold = m_bloomSettings.threshold;
                defaultSettings.bloomKnee = m_bloomSettings.knee;
                defaultSettings.bloomIntensity = m_bloomSettings.intensity;
                defaultSettings.bloomGaussianIntensity = m_bloomSettings.gaussianIntensity;
                defaultSettings.bloomRadius = m_bloomSettings.radius;
                defaultSettings.bloomDownsample = m_bloomSettings.downsample;
            }

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
            m_postProcessParams.maxHDRNits = finalSettings.maxHDRNits;
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
            // Bloom 설정 적용
            m_bloomSettings.threshold = finalSettings.bloomThreshold;
            m_bloomSettings.knee = finalSettings.bloomKnee;
            m_bloomSettings.intensity = finalSettings.bloomIntensity;
            m_bloomSettings.gaussianIntensity = finalSettings.bloomGaussianIntensity;
            m_bloomSettings.radius = finalSettings.bloomRadius;
            // 다운샘플링 변경 시 리소스 재생성
            if (m_bloomSettings.downsample != finalSettings.bloomDownsample)
            {
                m_bloomSettings.downsample = finalSettings.bloomDownsample;
                if (m_sceneWidth > 0 && m_sceneHeight > 0)
                {
                    CreateBloomResources(m_sceneWidth, m_sceneHeight);
                }
            }
        }

        // 월드 UI 렌더링 (씬 컬러 + 깊이 위에 합성)
        if (m_uiRenderer)
        {
            m_uiRenderer->RenderWorld(world, camera, m_sceneRTV.Get(), m_sceneDSV.Get());
        }

        // 에디터 모드: 뷰포트 표시용 LDR 텍스처로 Bloom + 톤매핑 (ImGui::Image에서 사용)
        if (editorMode && m_viewportRTV)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(m_sceneWidth);
            viewport.Height = static_cast<float>(m_sceneHeight);
            viewport.MaxDepth = 1.0f;
            
            // 포스트 프로세스 패스 (Bloom ON/OFF에 따라 자동 분기)
            RenderPostProcess(m_viewportRTV.Get(), viewport);

            // UI 렌더링 (Post-processing 이후, 최상단에 렌더링)
            if (m_uiRenderer)
            {
                m_uiRenderer->RenderScreen(world, camera, m_viewportRTV.Get(), viewport.Width, viewport.Height);
            }

            // 최종 백버퍼 복귀 (ImGui 등 UI 렌더링을 위해)
            RestoreBackBuffer();
        }
    }

    void DeferredRenderSystem::RenderCameraPreview(const World& world,
                                                   const Camera& camera,
                                                   const std::unordered_set<EntityId>& cameraEntities,
                                                   int shadingMode,
                                                   bool enableFillLight,
                                                   const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                                   bool editorMode,
                                                   bool isPlaying,
                                                   EntityId hiddenCameraEntity)
    {
        if (!m_device || !m_context) return;

        const float desiredAspect = (camera.GetAspectRatio() > 0.0f) ? camera.GetAspectRatio() : (16.0f / 9.0f);
        const std::uint32_t desiredHeight = (m_cameraPreviewHeight > 0) ? m_cameraPreviewHeight : 180;
        const std::uint32_t desiredWidth = (std::uint32_t)(std::max)(1.0f, std::round(desiredHeight * desiredAspect));

        if (!m_cameraPreviewViewportRTV || desiredWidth != m_cameraPreviewWidth || desiredHeight != m_cameraPreviewHeight)
        {
            if (!CreateCameraPreviewTargets(desiredWidth, desiredHeight))
                return;
        }

        // Save current state/resources
        Microsoft::WRL::ComPtr<ID3D11Texture2D> savedGBufferTextures[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> savedGBufferRTVs[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> savedGBufferSRVs[GBufferCount];
        for (int i = 0; i < GBufferCount; ++i)
        {
            savedGBufferTextures[i] = m_gBufferTextures[i];
            savedGBufferRTVs[i] = m_gBufferRTVs[i];
            savedGBufferSRVs[i] = m_gBufferSRVs[i];
        }
        Microsoft::WRL::ComPtr<ID3D11Texture2D> savedDBufferTextures[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> savedDBufferRTVs[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> savedDBufferSRVs[DBufferCount];
        for (int i = 0; i < DBufferCount; ++i)
        {
            savedDBufferTextures[i] = m_dBufferTextures[i];
            savedDBufferRTVs[i] = m_dBufferRTVs[i];
            savedDBufferSRVs[i] = m_dBufferSRVs[i];
        }

        auto savedSceneColorTex = m_sceneColorTex;
        auto savedSceneRTV = m_sceneRTV;
        auto savedSceneSRV = m_sceneColorSRV;
        auto savedSceneDepthTex = m_sceneDepthTex;
        auto savedSceneDSV = m_sceneDSV;
        auto savedSceneDepthSRV = m_sceneDepthSRV;
        auto savedViewportTex = m_viewportTex;
        auto savedViewportRTV = m_viewportRTV;
        auto savedViewportSRV = m_viewportSRV;
        auto savedSceneWidth = m_sceneWidth;
        auto savedSceneHeight = m_sceneHeight;
        auto savedPostProcessParams = m_postProcessParams;
        auto savedLastViewProj = m_lastViewProj;
        auto savedLastCameraPos = m_lastCameraPos;

        // Swap to preview resources
        for (int i = 0; i < GBufferCount; ++i)
        {
            m_gBufferTextures[i] = m_cameraPreviewGBufferTextures[i];
            m_gBufferRTVs[i] = m_cameraPreviewGBufferRTVs[i];
            m_gBufferSRVs[i] = m_cameraPreviewGBufferSRVs[i];
        }
        for (int i = 0; i < DBufferCount; ++i)
        {
            m_dBufferTextures[i] = m_cameraPreviewDBufferTextures[i];
            m_dBufferRTVs[i] = m_cameraPreviewDBufferRTVs[i];
            m_dBufferSRVs[i] = m_cameraPreviewDBufferSRVs[i];
        }

        m_sceneColorTex = m_cameraPreviewSceneColorTex;
        m_sceneRTV = m_cameraPreviewSceneRTV;
        m_sceneColorSRV = m_cameraPreviewSceneColorSRV;
        m_sceneDepthTex = m_cameraPreviewSceneDepthTex;
        m_sceneDSV = m_cameraPreviewSceneDSV;
        m_sceneDepthSRV = m_cameraPreviewSceneDepthSRV;
        m_viewportTex = m_cameraPreviewViewportTex;
        m_viewportRTV = m_cameraPreviewViewportRTV;
        m_viewportSRV = m_cameraPreviewViewportSRV;
        m_sceneWidth = m_cameraPreviewWidth;
        m_sceneHeight = m_cameraPreviewHeight;

        m_lastViewProj = camera.GetViewProjectionMatrix();
        m_lastCameraPos = camera.GetPosition();

        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // Use cached shadow map from the main render
        DirectX::XMMATRIX lightViewProj = m_lastShadowViewProj;

        // G-Buffer + Deferred light
        PassGBuffer(world, camera, skinnedCommands, cameraEntities, shadingMode, editorMode, isPlaying, hiddenCameraEntity);
        PassDecals(world, camera);
        PassDeferredLight(world, camera, shadingMode, enableFillLight, lightViewProj);

        if (m_skyboxEnabled)
        {
            m_context->RSSetViewports(1, &vp);
            RenderSkybox(camera);
        }

        // Transparent forward pass
        PassTransparentForward(camera, skinnedCommands, shadingMode);

        // Post Process Volume 적용 (Exposure/Color Grading만)
        {
            PostProcessSettings defaultSettings = m_defaultPostProcessSettings;

            if (!m_hasDefaultPostProcessSettings)
            {
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
            }

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
            m_postProcessParams.maxHDRNits = finalSettings.maxHDRNits;
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
            RenderToneMapping(m_sceneColorSRV.Get(), m_viewportRTV.Get(), viewport);
        }

        RestoreBackBuffer();

        // Restore previous state/resources
        for (int i = 0; i < GBufferCount; ++i)
        {
            m_gBufferTextures[i] = savedGBufferTextures[i];
            m_gBufferRTVs[i] = savedGBufferRTVs[i];
            m_gBufferSRVs[i] = savedGBufferSRVs[i];
        }
        for (int i = 0; i < DBufferCount; ++i)
        {
            m_dBufferTextures[i] = savedDBufferTextures[i];
            m_dBufferRTVs[i] = savedDBufferRTVs[i];
            m_dBufferSRVs[i] = savedDBufferSRVs[i];
        }

        m_sceneColorTex = savedSceneColorTex;
        m_sceneRTV = savedSceneRTV;
        m_sceneColorSRV = savedSceneSRV;
        m_sceneDepthTex = savedSceneDepthTex;
        m_sceneDSV = savedSceneDSV;
        m_sceneDepthSRV = savedSceneDepthSRV;
        m_viewportTex = savedViewportTex;
        m_viewportRTV = savedViewportRTV;
        m_viewportSRV = savedViewportSRV;
        m_sceneWidth = savedSceneWidth;
        m_sceneHeight = savedSceneHeight;
        m_postProcessParams = savedPostProcessParams;
        m_lastViewProj = savedLastViewProj;
        m_lastCameraPos = savedLastCameraPos;
    }

    void DeferredRenderSystem::PassGBuffer(const World& world,
                                           const Camera& camera,
                                           const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                           const std::unordered_set<EntityId>& cameraEntities,
                                           int shadingMode,
                                           bool editorMode,
                                           bool isPlaying,
                                           EntityId hiddenCameraEntity)
    {
        // ShadowPass 등에서 viewport가 변경될 수 있으므로,
        // GBuffer 패스 시작 시 항상 씬 해상도 뷰포트를 재설정합니다.
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth;
        vp.Height = (float)m_sceneHeight;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // G-Buffer 클리어
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        // Normal 클리어 값: 평평한 노말 (0,0,1)을 [0,1] 인코딩하면 (0.5, 0.5, 1.0)
        float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };

        m_context->ClearRenderTargetView(m_gBufferRTVs[0].Get(), clearNormal); // Normal + Roughness
        m_context->ClearRenderTargetView(m_gBufferRTVs[1].Get(), clearColor);  // Metalness
        m_context->ClearRenderTargetView(m_gBufferRTVs[2].Get(), clearColor);  // BaseColor
        m_context->ClearRenderTargetView(m_gBufferRTVs[3].Get(), clearColor);  // ToonParams
        m_context->ClearDepthStencilView(m_sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        // G-Buffer 렌더 타겟 설정
        ID3D11RenderTargetView* rtvs[GBufferCount] = {
            m_gBufferRTVs[0].Get(),
            m_gBufferRTVs[1].Get(),
            m_gBufferRTVs[2].Get(),
            m_gBufferRTVs[3].Get()
        };
        m_context->OMSetRenderTargets(GBufferCount, rtvs, m_sceneDSV.Get());
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());

        // 파이프라인 설정
        m_context->VSSetShader(m_gBufferVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_gBufferPS.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_gBufferInputLayout.Get());
        
        // PS sampler 바인딩 (normal map 샘플링을 위해 필요)
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        // 상수 버퍼 업데이트
        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();

        // 프러스텀 컬링을 위한 카메라 절두체 계산 (루프 밖에서 미리 계산)
        BoundingFrustum cameraFrustum = camera.GetWorldFrustum();

        const auto& transforms = world.GetComponents<TransformComponent>();

        // 1. 정적 메시 (큐브) 렌더링
        // ForwardRenderSystem::SimpleVertex와 동일한 구조체 (private이므로 로컬 정의)
        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_cubeVB.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        struct StaticInstancedDrawItem
        {
            InstancedDrawKey key;
            InstanceData instance;

            bool operator<(const StaticInstancedDrawItem& rhs) const
            {
                return key < rhs.key;
            }
        };

        std::vector<StaticInstancedDrawItem> staticInstancedItems;
        staticInstancedItems.reserve(transforms.size());
        for (const auto& [id, transform] : transforms)
        {
            if (cameraEntities.contains(id)) continue;
            if (world.GetComponent<SkinnedMeshComponent>(id)) continue;
            if (!transform.enabled || !transform.visible) continue;
            const MaterialComponent* mat = world.GetComponent<MaterialComponent>(id);
            if (!mat) continue;

            // [프러스텀 컬링] 카메라 시야 밖 오브젝트는 건너뛰기
            float maxScale = std::max({ transform.scale.x, transform.scale.y, transform.scale.z });
            BoundingSphere bounds(transform.position, maxScale * 1.5f); // 1.5f는 안전 계수
            if (cameraFrustum.Contains(bounds) == DISJOINT)
            {
                continue; // 화면에 보이지 않으면 렌더링하지 않음
            }

            XMMATRIX worldM = BuildWorldMatrix(world, id, transform);
            
            // 재질 정보 가져오기
            XMFLOAT4 color = { 1, 1, 1, 1 };
            float rough = 0.5f, metal = 0.0f;
            float ao = m_lightingParameters.ambientOcclusion;
            float envDiffuseStrength = 1.0f;
            float envSpecularStrength = 1.0f;
            bool useTex = false;
            ID3D11ShaderResourceView* texSRV = nullptr;
            
            // MaterialComponent가 있으면 값 적용
            XMFLOAT3 outlineColor = {0,0,0};
            float outlineWidth = 0.0f;
            float normalStrength = 1.0f;
            XMFLOAT4 toonCuts = DefaultToonPbrCuts();
            XMFLOAT4 toonLevels = DefaultToonPbrLevels();
            int objectShadingMode = shadingMode;
            if (mat) {
                color = { mat->color.x, mat->color.y, mat->color.z, mat->alpha };
                rough = mat->roughness; 
                metal = mat->metalness;
                if (mat->shadingMode >= 0)
                    ao = mat->ambientOcclusion;
                envDiffuseStrength = mat->envDiffuseStrength;
                envSpecularStrength = mat->envSpecularStrength;
                normalStrength = mat->normalStrength;
                outlineColor = mat->outlineColor;
                outlineWidth = mat->outlineWidth;
                toonCuts = XMFLOAT4(mat->toonPbrCut1, mat->toonPbrCut2, mat->toonPbrCut3, mat->toonPbrStrength);
                toonLevels = XMFLOAT4(mat->toonPbrLevel1, mat->toonPbrLevel2, mat->toonPbrLevel3,
                    mat->toonPbrBlur ? 1.0f : 0.0f);
                if (mat->shadingMode >= 0) objectShadingMode = mat->shadingMode;
                if (!mat->albedoTexturePath.empty()) {
                    texSRV = GetOrCreateTexture(mat->albedoTexturePath);
                    useTex = (texSRV != nullptr);
                }
            }

            const bool canStaticInstance = (outlineWidth <= 0.0f) &&
                                           m_gBufferInstancedVS &&
                                           m_gBufferInstancedInputLayout &&
                                           m_instanceBuffer;

            if (canStaticInstance)
            {
                StaticInstancedDrawItem item{};
                item.key.vertexBuffer = m_cubeVB.Get();
                item.key.indexBuffer = m_cubeIB.Get();
                item.key.stride = stride;
                item.key.startIndex = 0;
                item.key.indexCount = m_cubeIndexCount;
                item.key.baseVertex = 0;
                item.key.diffuseSRV = texSRV;
                item.key.normalSRV = nullptr;
                item.key.color = color;
                item.key.roughness = rough;
                item.key.metalness = metal;
                item.key.ambientOcclusion = ao;
                item.key.envDiffuseStrength = envDiffuseStrength;
                item.key.envSpecularStrength = envSpecularStrength;
                item.key.normalStrength = normalStrength;
                item.key.toonPbrCuts = toonCuts;
                item.key.toonPbrLevels = toonLevels;
                item.key.shadingMode = objectShadingMode;
                item.key.useTexture = useTex ? 1 : 0;
                item.key.enableNormalMap = 0;
                item.instance = BuildInstanceData(worldM);

                staticInstancedItems.push_back(item);
                continue;
            }

            // 텍스처 바인딩 (t0: Diffuse, t1: Normal)
            ID3D11ShaderResourceView* srvs[] = { texSRV, nullptr }; // 정적 메시는 노말맵 현재 null
            m_context->PSSetShaderResources(0, 2, srvs);

            // Pass 1. 원본 물체 그리기 (아웃라인 두께 0으로 강제)
            UpdatePerObjectCB(worldM, view, proj, color, rough, metal, ao, useTex, false,
                               objectShadingMode, normalStrength, toonCuts, toonLevels,
                               envDiffuseStrength, envSpecularStrength,
                               outlineColor, 0.0f); // width = 0
            m_context->DrawIndexed(m_cubeIndexCount, 0, 0);

            // Pass 2. 아웃라인 그리기 (설정된 경우만)
            if (outlineWidth > 0.0f)
            {
                m_context->RSSetState(m_rsCullFront.Get()); // 뒷면 그리기
                
                // 아웃라인 값 적용
                UpdatePerObjectCB(worldM, view, proj, color, rough, metal, ao, useTex, false,
                                   objectShadingMode, normalStrength, toonCuts, toonLevels,
                                   envDiffuseStrength, envSpecularStrength,
                                   outlineColor, outlineWidth);
                m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
                
                m_context->RSSetState(m_rasterizerState.Get()); // 상태 복구
            }
        }

        // 정적 메시 인스턴싱 배치 렌더링 (outline 없는 오브젝트만)
        if (!staticInstancedItems.empty() && m_gBufferInstancedVS && m_gBufferInstancedInputLayout)
        {
            std::sort(staticInstancedItems.begin(), staticInstancedItems.end());

            if (EnsureInstanceBufferCapacity(staticInstancedItems.size()))
            {
                m_context->VSSetShader(m_gBufferInstancedVS.Get(), nullptr, 0);
                m_context->PSSetShader(m_gBufferPS.Get(), nullptr, 0);
                m_context->IASetInputLayout(m_gBufferInstancedInputLayout.Get());

                std::vector<InstanceData> batchInstances;
                batchInstances.reserve(staticInstancedItems.size());

                InstancedDrawKey currentKey = staticInstancedItems.front().key;
                batchInstances.clear();

                for (const auto& item : staticInstancedItems)
                {
                    if (!IsSameInstancedKey(currentKey, item.key) || batchInstances.size() >= m_instanceCapacity)
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
                            m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R16_UINT, 0);

                            ID3D11ShaderResourceView* srvs[] = { currentKey.diffuseSRV, currentKey.normalSRV };
                            m_context->PSSetShaderResources(0, 2, srvs);

                            UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, currentKey.color,
                                              currentKey.roughness, currentKey.metalness, currentKey.ambientOcclusion,
                                              (currentKey.useTexture != 0), (currentKey.enableNormalMap != 0),
                                              currentKey.shadingMode, currentKey.normalStrength,
                                            currentKey.toonPbrCuts, currentKey.toonPbrLevels,
                                            currentKey.envDiffuseStrength, currentKey.envSpecularStrength,
                                            DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                            m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(),
                                                            currentKey.startIndex, currentKey.baseVertex, 0);
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
                    m_context->IASetIndexBuffer(currentKey.indexBuffer, DXGI_FORMAT_R16_UINT, 0);

                    ID3D11ShaderResourceView* srvs[] = { currentKey.diffuseSRV, currentKey.normalSRV };
                    m_context->PSSetShaderResources(0, 2, srvs);

                    UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, currentKey.color,
                                      currentKey.roughness, currentKey.metalness, currentKey.ambientOcclusion,
                                      (currentKey.useTexture != 0), (currentKey.enableNormalMap != 0),
                                      currentKey.shadingMode, currentKey.normalStrength,
                                        currentKey.toonPbrCuts, currentKey.toonPbrLevels,
                                        currentKey.envDiffuseStrength, currentKey.envSpecularStrength,
                                        DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                    m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(),
                                                    currentKey.startIndex, currentKey.baseVertex, 0);
                }
            }
        }
        
        // 2. 스키닝 메시 렌더링
        // - ForwardRenderSystem과 동일하게, Registry의 서브셋 머티리얼 SRV를 우선 사용합니다.
        // - (cmd.albedoTexturePath는 에디터에서 오버라이드한 경우에만 사용)
        if (!skinnedCommands.empty() && m_gBufferSkinnedVS && m_gBufferPS)
        {
            // 인스턴싱 배치 아이템
            struct InstancedDrawItem
            {
                InstancedDrawKey key;
                InstanceData instance;

                bool operator<(const InstancedDrawItem& rhs) const
                {
                    return key < rhs.key;
                }
            };

            std::vector<InstancedDrawItem> instancedItems;
            instancedItems.reserve(skinnedCommands.size());

            m_context->VSSetShader(m_gBufferSkinnedVS.Get(), nullptr, 0);
            m_context->IASetInputLayout(m_gBufferSkinnedInputLayout.Get());

            for (const auto& cmd : skinnedCommands)
            {
                if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;

                // [프러스텀 컬링] 월드 행렬에서 위치 추출
                XMFLOAT4X4 worldMatrix;
                XMStoreFloat4x4(&worldMatrix, cmd.world);
                XMFLOAT3 position(worldMatrix._41, worldMatrix._42, worldMatrix._43);
                
                // 스케일 추정: 월드 행렬의 스케일 성분 추출 (간단한 근사)
                XMVECTOR scaleVec = XMVectorSet(
                    XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._11, worldMatrix._12, worldMatrix._13, 0.0f))),
                    XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._21, worldMatrix._22, worldMatrix._23, 0.0f))),
                    XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._31, worldMatrix._32, worldMatrix._33, 0.0f))),
                    0.0f
                );
                float maxScale = std::max({ XMVectorGetX(scaleVec), XMVectorGetY(scaleVec), XMVectorGetZ(scaleVec) });
                BoundingSphere bounds(position, maxScale * 1.5f);
                if (cameraFrustum.Contains(bounds) == DISJOINT)
                {
                    continue; // 화면에 보이지 않으면 렌더링하지 않음
                }

                const XMFLOAT4 color(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha);
                const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
                const float ao = (cmd.shadingMode >= 0) ? cmd.ambientOcclusion : m_lightingParameters.ambientOcclusion;

                std::shared_ptr<SkinnedMeshGPU> mesh =
                    (m_skinnedRegistry && !cmd.meshKey.empty()) ? m_skinnedRegistry->Find(cmd.meshKey) : nullptr;

                const bool canInstance = IsRigidSkinnedCommand(cmd) &&
                                         (cmd.outlineWidth <= 0.0f) &&
                                         m_gBufferSkinnedInstancedVS &&
                                         m_gBufferSkinnedInstancedInputLayout &&
                                         m_instanceBuffer;

                if (canInstance)
                {
                    // 인스턴싱 대상은 "배치 수집"만 하고, 여기서는 그리지 않습니다.
                    if (mesh && !mesh->subsets.empty())
                    {
                        for (const auto& sub : mesh->subsets)
                        {
                            if (sub.indexCount == 0) continue;

                            ID3D11ShaderResourceView* diff =
                                (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : nullptr;
                            ID3D11ShaderResourceView* norm =
                                (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : nullptr;

                            InstancedDrawItem item{};
                            item.key.vertexBuffer = cmd.vertexBuffer;
                            item.key.indexBuffer = cmd.indexBuffer;
                            item.key.stride = cmd.stride;
                            item.key.startIndex = sub.startIndex;
                            item.key.indexCount = sub.indexCount;
                            item.key.baseVertex = cmd.baseVertex;
                            item.key.diffuseSRV = diff;
                            item.key.normalSRV = norm;
                            item.key.color = color;
                            item.key.roughness = cmd.roughness;
                            item.key.metalness = cmd.metalness;
                            item.key.ambientOcclusion = ao;
                            item.key.envDiffuseStrength = cmd.envDiffuseStrength;
                            item.key.envSpecularStrength = cmd.envSpecularStrength;
                            item.key.normalStrength = cmd.normalStrength;
                            item.key.toonPbrCuts = cmd.toonPbrCuts;
                            item.key.toonPbrLevels = cmd.toonPbrLevels;
                            item.key.shadingMode = objectShadingMode;
                            item.key.useTexture = (diff != nullptr) ? 1 : 0;
                            item.key.enableNormalMap = (norm != nullptr) ? 1 : 0;
                            item.instance = BuildInstanceData(cmd.world);

                            instancedItems.push_back(item);
                        }
                    }
                    else
                    {
                        // 오버라이드 텍스처(또는 단일 텍스처)만 있는 경우
                        ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);

                        InstancedDrawItem item{};
                        item.key.vertexBuffer = cmd.vertexBuffer;
                        item.key.indexBuffer = cmd.indexBuffer;
                        item.key.stride = cmd.stride;
                        item.key.startIndex = cmd.startIndex;
                        item.key.indexCount = cmd.indexCount;
                        item.key.baseVertex = cmd.baseVertex;
                        item.key.diffuseSRV = diff;
                        item.key.normalSRV = nullptr;
                        item.key.color = color;
                        item.key.roughness = cmd.roughness;
                        item.key.metalness = cmd.metalness;
                        item.key.ambientOcclusion = ao;
                        item.key.envDiffuseStrength = cmd.envDiffuseStrength;
                        item.key.envSpecularStrength = cmd.envSpecularStrength;
                        item.key.normalStrength = cmd.normalStrength;
                        item.key.toonPbrCuts = cmd.toonPbrCuts;
                        item.key.toonPbrLevels = cmd.toonPbrLevels;
                        item.key.shadingMode = objectShadingMode;
                        item.key.useTexture = (diff != nullptr) ? 1 : 0;
                        item.key.enableNormalMap = 0;
                        item.instance = BuildInstanceData(cmd.world);

                        instancedItems.push_back(item);
                    }

                    continue;
                }

                // 일반 스키닝 렌더링
                UINT sStride = cmd.stride;
                m_context->IASetVertexBuffers(0, 1, &cmd.vertexBuffer, &sStride, &offset);
                m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                UpdateBonesCB(cmd.bones, cmd.boneCount);

                if (mesh && !mesh->subsets.empty())
                {
                    for (const auto& sub : mesh->subsets)
                    {
                        if (sub.indexCount == 0) continue;

                        ID3D11ShaderResourceView* diff =
                            (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : nullptr;
                        ID3D11ShaderResourceView* norm =
                            (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : nullptr;

                        ID3D11ShaderResourceView* srvs[] = { diff, norm };
                        m_context->PSSetShaderResources(0, 2, srvs);
                        
                        // Pass 1. 원본
                        UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                          (diff != nullptr), (norm != nullptr), objectShadingMode, 
                                          cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                          cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                          cmd.outlineColor, 0.0f); // width 0
                        m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);

                        // Pass 2. 아웃라인
                        if (cmd.outlineWidth > 0.0f)
                        {
                            m_context->RSSetState(m_rsCullFront.Get());
                            UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                              (diff != nullptr), (norm != nullptr), objectShadingMode, 
                                              cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                              cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                              cmd.outlineColor, cmd.outlineWidth);
                            m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                            m_context->RSSetState(m_rasterizerState.Get());
                        }
                    }
                }
                else
                {
                    // 오버라이드 텍스처 (또는 단일 텍스처)만 있는 경우
                    ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);
                    ID3D11ShaderResourceView* srvs[] = { diff, nullptr };
                    m_context->PSSetShaderResources(0, 2, srvs);
                    
                    // Pass 1. 원본
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                      (diff != nullptr), false, objectShadingMode, 
                                      cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                      cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                      cmd.outlineColor, 0.0f);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);

                    // Pass 2. 아웃라인
                    if (cmd.outlineWidth > 0.0f)
                    {
                        m_context->RSSetState(m_rsCullFront.Get());
                        UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                          (diff != nullptr), false, objectShadingMode, 
                                          cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                          cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                          cmd.outlineColor, cmd.outlineWidth);
                        m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                        m_context->RSSetState(m_rasterizerState.Get());
                    }
                }
            }

            // 인스턴싱 배치 렌더링 (Opaque이므로 정렬 가능)
            if (!instancedItems.empty() && m_gBufferSkinnedInstancedVS && m_gBufferSkinnedInstancedInputLayout)
            {
                std::sort(instancedItems.begin(), instancedItems.end());

                if (EnsureInstanceBufferCapacity(instancedItems.size()))
                {
                    m_context->VSSetShader(m_gBufferSkinnedInstancedVS.Get(), nullptr, 0);
                    m_context->IASetInputLayout(m_gBufferSkinnedInstancedInputLayout.Get());

                    std::vector<InstanceData> batchInstances;
                    batchInstances.reserve(instancedItems.size());

                    InstancedDrawKey currentKey = instancedItems.front().key;
                    batchInstances.clear();

                    for (const auto& item : instancedItems)
                    {
                        if (!IsSameInstancedKey(currentKey, item.key) || batchInstances.size() >= m_instanceCapacity)
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

                                ID3D11ShaderResourceView* srvs[] = { currentKey.diffuseSRV, currentKey.normalSRV };
                                m_context->PSSetShaderResources(0, 2, srvs);

                                UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, currentKey.color,
                                                  currentKey.roughness, currentKey.metalness, currentKey.ambientOcclusion,
                                                  (currentKey.useTexture != 0), (currentKey.enableNormalMap != 0),
                                                  currentKey.shadingMode, currentKey.normalStrength,
                                                  currentKey.toonPbrCuts, currentKey.toonPbrLevels,
                                                  currentKey.envDiffuseStrength, currentKey.envSpecularStrength,
                                                  DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                                m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(),
                                                                currentKey.startIndex, currentKey.baseVertex, 0);
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

                        ID3D11ShaderResourceView* srvs[] = { currentKey.diffuseSRV, currentKey.normalSRV };
                        m_context->PSSetShaderResources(0, 2, srvs);

                        UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, currentKey.color,
                                          currentKey.roughness, currentKey.metalness, currentKey.ambientOcclusion,
                                          (currentKey.useTexture != 0), (currentKey.enableNormalMap != 0),
                                          currentKey.shadingMode, currentKey.normalStrength,
                                          currentKey.toonPbrCuts, currentKey.toonPbrLevels,
                                          currentKey.envDiffuseStrength, currentKey.envSpecularStrength,
                                          DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                        m_context->DrawIndexedInstanced(currentKey.indexCount, (UINT)batchInstances.size(),
                                                        currentKey.startIndex, currentKey.baseVertex, 0);
                    }
                }
            }
        }

        // ============================== 카메라(FBX) 렌더링 ==================================
        // 3. 카메라를 FBX 모델로 렌더링 (에디터 모드이고 Play 중이 아닐 때만)
        if (editorMode && !isPlaying)
        {
            if (!m_cameraIconLoadTried)
            {
                m_cameraIconLoadTried = true;

                if (!m_resources || !m_skinnedRegistry || !m_device)
                {
                    ALICE_LOG_WARN("DeferredRenderSystem: Camera icon FBX load skipped (missing resources/registry/device).");
                }
                else
                {
                    if (!m_skinnedRegistry->Has(m_cameraIconMeshKey))
                    {
                        FbxImporter importer(*m_resources, m_skinnedRegistry);
                        const FbxImportResult result = importer.Import(m_device.Get(), kCameraIconFbxPath, FbxImportOptions{});
                        if (!result.meshAssetPath.empty())
                        {
                            m_cameraIconMeshKey = result.meshAssetPath;
                        }
                    }

                    if (!m_skinnedRegistry->Has(m_cameraIconMeshKey))
                    {
                        ALICE_LOG_WARN("DeferredRenderSystem: Failed to import camera icon FBX: %s", kCameraIconFbxPath);
                    }
                }
            }

            std::shared_ptr<SkinnedMeshGPU> cameraMesh =
                (m_skinnedRegistry && !m_cameraIconMeshKey.empty()) ? m_skinnedRegistry->Find(m_cameraIconMeshKey) : nullptr;

            if (cameraMesh && cameraMesh->vertexBuffer && cameraMesh->indexBuffer &&
                m_gBufferSkinnedVS && m_gBufferSkinnedInputLayout)
            {
                // Skinned FBX용 셰이더/레이아웃 (리짓 FBX도 동일 파이프라인 사용)
                m_context->VSSetShader(m_gBufferSkinnedVS.Get(), nullptr, 0);
                m_context->PSSetShader(m_gBufferPS.Get(), nullptr, 0);
                m_context->IASetInputLayout(m_gBufferSkinnedInputLayout.Get());
                m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                UINT stride = cameraMesh->stride;
                UINT offset = 0;
                ID3D11Buffer* vb = cameraMesh->vertexBuffer.Get();
                m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
                m_context->IASetIndexBuffer(cameraMesh->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

                // 리짓 FBX용 Identity 본 (1개)
                static DirectX::XMFLOAT4X4 identityBone;
                static bool identityInit = false;
                if (!identityInit)
                {
                    DirectX::XMStoreFloat4x4(&identityBone, DirectX::XMMatrixIdentity());
                    identityInit = true;
                }
                UpdateBonesCB(&identityBone, 1);

                const auto& cameraComponents = world.GetComponents<CameraComponent>();
                for (const auto& [camId, _] : cameraComponents)
                {
                    if (camId == hiddenCameraEntity)
                        continue;

                    const auto* camTr = world.GetComponent<TransformComponent>(camId);
                    if (!camTr) continue;

                    // 카메라 위치에 스케일 적용
                    TransformComponent cameraIconTr = *camTr;
                    cameraIconTr.scale = { m_cameraIconScale, m_cameraIconScale, m_cameraIconScale };
                    DirectX::XMMATRIX cameraIconWorld = BuildWorldMatrix(cameraIconTr);

                    DirectX::XMFLOAT4 cameraColor(1.0f, 1.0f, 1.0f, 1.0f);

                    const auto& subsets = cameraMesh->subsets;
                    if (!subsets.empty())
                    {
                        for (const auto& sub : subsets)
                        {
                            if (sub.indexCount == 0) continue;

                            ID3D11ShaderResourceView* diff =
                                (sub.materialIndex < cameraMesh->materialSRVs.size())
                                    ? cameraMesh->materialSRVs[sub.materialIndex].Get()
                                    : nullptr;
                            ID3D11ShaderResourceView* norm =
                                (sub.materialIndex < cameraMesh->normalSRVs.size())
                                    ? cameraMesh->normalSRVs[sub.materialIndex].Get()
                                    : nullptr;

                            ID3D11ShaderResourceView* srvs[] = { diff, norm };
                            m_context->PSSetShaderResources(0, 2, srvs);

                            UpdatePerObjectCB(cameraIconWorld, view, proj, cameraColor,
                                               0.5f, 0.0f, m_lightingParameters.ambientOcclusion,
                                               (diff != nullptr), (norm != nullptr),
                                               shadingMode,
                                               1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                               1.0f, 1.0f,
                                               DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                            m_context->DrawIndexed(sub.indexCount, sub.startIndex, cameraMesh->baseVertex);
                        }
                    }
                    else
                    {
                        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
                        m_context->PSSetShaderResources(0, 2, srvs);

                        UpdatePerObjectCB(cameraIconWorld, view, proj, cameraColor,
                                           0.5f, 0.0f, m_lightingParameters.ambientOcclusion,
                                           false, false, shadingMode,
                                           1.0f, DefaultToonPbrCuts(), DefaultToonPbrLevels(),
                                           1.0f, 1.0f,
                                           DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                        m_context->DrawIndexed(cameraMesh->indexCount, cameraMesh->startIndex, cameraMesh->baseVertex);
                    }
                }
            }
        }

        // RTV 해제
        ID3D11RenderTargetView* nullRTVs[GBufferCount] = { nullptr };
        m_context->OMSetRenderTargets(GBufferCount, nullRTVs, nullptr);
    }

    void DeferredRenderSystem::PassDecals(const World& world, const Camera& camera)
    {
        if (!m_device || !m_context) return;
        if (!m_decalVS || !m_decalPS || !m_decalInputLayout || !m_cbDecal) return;
        if (!m_sceneDepthSRV) return;
        if (!m_dBufferRTVs[0]) return;
        if (!m_decalBlendState) return;

        // 뷰포트 설정
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth;
        vp.Height = (float)m_sceneHeight;
        vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // D-Buffer 클리어
        float clear[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < DBufferCount; ++i)
        {
            m_context->ClearRenderTargetView(m_dBufferRTVs[i].Get(), clear);
        }

        // 렌더 타깃 설정 (Depth는 SRV로 읽어야 하므로 DSV는 바인딩하지 않음)
        ID3D11RenderTargetView* rtvs[DBufferCount] = { m_dBufferRTVs[0].Get() };
        m_context->OMSetRenderTargets(DBufferCount, rtvs, nullptr);

        float blendFactor[4] = { 0,0,0,0 };
        m_context->OMSetBlendState(m_decalBlendState.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());

        // 파이프라인 설정
        m_context->VSSetShader(m_decalVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_decalPS.Get(), nullptr, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_decalInputLayout.Get());

        UINT stride = sizeof(SimpleVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = m_cubeVB.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        ID3D11SamplerState* samplers[] = { m_samplerLinear.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        // 프러스텀 컬링
        BoundingFrustum cameraFrustum = camera.GetWorldFrustum();

        struct DecalDraw
        {
            int order = 0;
            EntityId id = InvalidEntityId;
        };
        std::vector<DecalDraw> decals;
        decals.reserve(world.GetComponents<DecalComponent>().size());

        for (const auto& [id, decal] : world.GetComponents<DecalComponent>())
        {
            if (!decal.enabled) continue;
            if (decal.albedoTexturePath.empty()) continue;
            if (decal.opacity <= 0.0f) continue;

            const auto* tr = world.GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled || !tr->visible) continue;

            float maxScale = std::max({ std::fabs(tr->scale.x), std::fabs(tr->scale.y), std::fabs(tr->scale.z) });
            BoundingSphere bounds(tr->position, maxScale * 1.8f);
            if (cameraFrustum.Contains(bounds) == DISJOINT)
                continue;

            decals.push_back({ decal.sortOrder, id });
        }

        if (decals.empty())
        {
            return;
        }

        std::sort(decals.begin(), decals.end(), [](const DecalDraw& a, const DecalDraw& b) {
            return a.order < b.order;
        });

        XMMATRIX view = camera.GetViewMatrix();
        XMMATRIX proj = camera.GetProjectionMatrix();
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, camera.GetViewProjectionMatrix());

        struct DecalCBData
        {
            XMMATRIX decalWorldViewProj;
            XMMATRIX worldToDecal;
            XMMATRIX invViewProj;
            XMFLOAT4 colorOpacity;
            XMFLOAT4 uvScaleOffset;
            XMFLOAT2 screenSize;
            XMFLOAT2 pad;
        };

        for (const auto& item : decals)
        {
            const auto* decal = world.GetComponent<DecalComponent>(item.id);
            const auto* tr = world.GetComponent<TransformComponent>(item.id);
            if (!decal || !tr) continue;

            ID3D11ShaderResourceView* decalSrv = GetOrCreateTexture(decal->albedoTexturePath);
            if (!decalSrv) continue;

            XMMATRIX worldM = BuildWorldMatrix(world, item.id, *tr);
            XMMATRIX worldToDecal = XMMatrixInverse(nullptr, worldM);

            DecalCBData cb{};
            cb.decalWorldViewProj = XMMatrixTranspose(worldM * view * proj);
            cb.worldToDecal = XMMatrixTranspose(worldToDecal);
            cb.invViewProj = XMMatrixTranspose(invViewProj);
            const float opacity = std::clamp(decal->opacity, 0.0f, 1.0f);
            cb.colorOpacity = XMFLOAT4(decal->color.x, decal->color.y, decal->color.z, opacity);
            cb.uvScaleOffset = XMFLOAT4(decal->uvScale.x, decal->uvScale.y, decal->uvOffset.x, decal->uvOffset.y);
            cb.screenSize = XMFLOAT2((float)m_sceneWidth, (float)m_sceneHeight);
            cb.pad = XMFLOAT2(0, 0);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(m_context->Map(m_cbDecal.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &cb, sizeof(cb));
                m_context->Unmap(m_cbDecal.Get(), 0);
            }

            ID3D11Buffer* cbuffers[] = { m_cbDecal.Get() };
            m_context->VSSetConstantBuffers(0, 1, cbuffers);
            m_context->PSSetConstantBuffers(0, 1, cbuffers);

            ID3D11ShaderResourceView* srvs[] = { decalSrv, m_sceneDepthSRV.Get() };
            m_context->PSSetShaderResources(0, 2, srvs);

            m_context->DrawIndexed(m_cubeIndexCount, 0, 0);
        }

        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        m_context->PSSetShaderResources(0, 2, nullSRVs);
    }

    void DeferredRenderSystem::PassDeferredLight(const World& world, const Camera& camera, int shadingMode, bool enableFillLight, DirectX::CXMMATRIX lightViewProj)
    {
        // 뷰포트 설정 (ForwardRenderSystem과 동일)
        D3D11_VIEWPORT vp{};
        vp.Width = (float)m_sceneWidth; vp.Height = (float)m_sceneHeight; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);

        // 씬 타겟 설정
        m_context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), nullptr);
        // FullScreen Quad 패스는 DSV를 사용하지 않으므로 Depth Test를 반드시 꺼야 합니다.
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        // 클리어 (배경색)
        float clearColor[4] = { m_backgroundColor.x, m_backgroundColor.y, m_backgroundColor.z, m_backgroundColor.w };
        m_context->ClearRenderTargetView(m_sceneRTV.Get(), clearColor);

        // G-Buffer 텍스처 바인딩
        std::vector<ID3D11ShaderResourceView*> srvs = {
            m_gBufferSRVs[0].Get(), // Normal + Roughness
            m_gBufferSRVs[1].Get(), // Metalness
            m_gBufferSRVs[2].Get(), // BaseColor
            m_gBufferSRVs[3].Get(), // ToonParams
            m_sceneDepthSRV.Get(),  // Scene Depth (Position 복원용)
            m_iblDiffuseSRV.Get(),   // IBL Diffuse
            m_iblSpecularSRV.Get(),  // IBL Specular
            m_iblBrdfLutSRV.Get(),   // IBL BRDF LUT
            m_shadowSRV.Get(),       // Shadow Map
            m_dBufferSRVs[0].Get()   // Decal D-Buffer
        };
        m_context->PSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());

        // 샘플러 설정
        ID3D11SamplerState* samplers[] = { m_samplerState.Get(), m_shadowSampler.Get(), m_samplerLinear.Get() };
        m_context->PSSetSamplers(0, 3, samplers);

        // 상수 버퍼 업데이트 (섀도우 파라미터 포함)
        UpdateLightingCB(camera, shadingMode, enableFillLight, lightViewProj);
        UpdateExtraLightsCB(world);

         // ShadowCB(b4) 업데이트 (패킹 안전)
        // - Shadow 행렬/파라미터는 ShadowCB에서만 읽도록(셰이더) 변경했습니다.
        if (m_cbShadow)
        {
            struct ShadowCBData
            {
                DirectX::XMMATRIX lightViewProjT;
                float bias;
                float mapSize;
                float pcfRadius;
                int   enabled;
                float pad[3];
            };

            ShadowCBData scb{};
            scb.lightViewProjT = DirectX::XMMatrixTranspose(lightViewProj);
            scb.bias = m_shadowSettings.bias;
            scb.mapSize = (float)((m_shadowMapSizePxEffective > 0) ? m_shadowMapSizePxEffective : m_shadowSettings.mapSizePx);
            scb.pcfRadius = m_shadowSettings.pcfRadius;
            scb.enabled = m_shadowSettings.enabled ? 1 : 0;

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(m_context->Map(m_cbShadow.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &scb, sizeof(scb));
                m_context->Unmap(m_cbShadow.Get(), 0);
            }

            ID3D11Buffer* cb = m_cbShadow.Get();
            m_context->PSSetConstantBuffers(4, 1, &cb); // b4
        }


        // FullScreen Quad 그리기
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &m_quadStride, &m_quadOffset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_deferredLightPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);

        // 리소스 해제
        ID3D11ShaderResourceView* nullSRVs[10] = { nullptr };
        m_context->PSSetShaderResources(0, 10, nullSRVs);
    }

    void DeferredRenderSystem::PassTransparentForward(
        const Camera& camera,
        const std::vector<SkinnedDrawCommand>& skinnedCommands,
        int shadingMode)
    {
        if (!m_device || !m_context) return;
        if (!m_sceneRTV || !m_sceneDSV) return;
        if (!m_alphaBlendState || !m_depthStencilStateReadOnly) return;
        if (!m_transparentSkinnedVS || !m_transparentPS || !m_transparentSkinnedInputLayout) return;
        if (!m_cbTransparentLight) return;

        // 현재는 "반투명 문제가 주로 FBX(스키닝) 쪽"에서 발생하므로 스키닝 커맨드만 처리합니다.
        if (skinnedCommands.empty()) return;
        bool hasTransparent = false;
        for (const auto& cmd : skinnedCommands)
        {
            if (cmd.transparent)
            {
                hasTransparent = true;
                break;
            }
        }
        if (!hasTransparent) return;

        // 렌더 타깃: HDR 씬 컬러 + (GBuffer에서 채운) 깊이 버퍼
        m_context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), m_sceneDSV.Get());

        // 블렌딩 ON, 깊이 테스트 ON(읽기 전용)
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_alphaBlendState.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_depthStencilStateReadOnly.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());

        // 파이프라인
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_transparentSkinnedInputLayout.Get());
        m_context->VSSetShader(m_transparentSkinnedVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_transparentPS.Get(), nullptr, 0);

        // 샘플러
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        m_context->PSSetSamplers(0, 1, samplers);

        // Transparent Light CB 업데이트 (register b1)
        struct TransparentLightCB
        {
            DirectX::XMFLOAT3 lightDir;
            float             intensity;
            DirectX::XMFLOAT3 lightColor;
            float             pad0;
            DirectX::XMFLOAT3 cameraPos;
            float             pad1;
        };

        TransparentLightCB tl{};
        tl.lightDir = m_lightingParameters.keyDirection;
        tl.intensity = m_lightingParameters.keyIntensity;
        tl.lightColor = m_lightingParameters.diffuseColor;
        tl.cameraPos = camera.GetPosition();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_cbTransparentLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &tl, sizeof(tl));
            m_context->Unmap(m_cbTransparentLight.Get(), 0);
        }
        ID3D11Buffer* tlCB = m_cbTransparentLight.Get();
        m_context->PSSetConstantBuffers(1, 1, &tlCB);

        // 프러스텀 컬링을 위한 카메라 절두체 계산 (루프 밖에서 미리 계산)
        BoundingFrustum cameraFrustum = camera.GetWorldFrustum();

        // IBL 리소스 바인딩 (t5~t7)
        ID3D11ShaderResourceView* iblDiffuse = m_iblDiffuseSRV.Get();
        ID3D11ShaderResourceView* iblSpec = m_iblSpecularSRV.Get();
        ID3D11ShaderResourceView* iblBrdf = m_iblBrdfLutSRV.Get();
        ID3D11ShaderResourceView* iblSrvs[] = { iblDiffuse, iblSpec, iblBrdf };
        m_context->PSSetShaderResources(5, 3, iblSrvs);

        // 공통 행렬
        DirectX::XMMATRIX view = camera.GetViewMatrix();
        DirectX::XMMATRIX proj = camera.GetProjectionMatrix();

        // 투명 패스는 순서가 중요하므로, "연속 구간"만 인스턴싱 처리합니다.
        InstancedDrawKey batchKey{};
        std::vector<InstanceData> batchInstances;
        bool hasBatch = false;

        for (const auto& cmd : skinnedCommands)
        {
            if (!cmd.vertexBuffer || !cmd.indexBuffer || cmd.indexCount == 0) continue;
            if (!cmd.transparent) continue;

            // [프러스텀 컬링] 월드 행렬에서 위치 추출
            XMFLOAT4X4 worldMatrix;
            XMStoreFloat4x4(&worldMatrix, cmd.world);
            XMFLOAT3 position(worldMatrix._41, worldMatrix._42, worldMatrix._43);
            
            // 스케일 추정: 월드 행렬의 스케일 성분 추출 (간단한 근사)
            XMVECTOR scaleVec = XMVectorSet(
                XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._11, worldMatrix._12, worldMatrix._13, 0.0f))),
                XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._21, worldMatrix._22, worldMatrix._23, 0.0f))),
                XMVectorGetX(XMVector3Length(XMVectorSet(worldMatrix._31, worldMatrix._32, worldMatrix._33, 0.0f))),
                0.0f
            );
            float maxScale = std::max({ XMVectorGetX(scaleVec), XMVectorGetY(scaleVec), XMVectorGetZ(scaleVec) });
            BoundingSphere bounds(position, maxScale * 1.5f);
            if (cameraFrustum.Contains(bounds) == DISJOINT)
            {
                continue; // 화면에 보이지 않으면 렌더링하지 않음
            }

            const DirectX::XMFLOAT4 color(cmd.color.x, cmd.color.y, cmd.color.z, cmd.alpha);
            const int objectShadingMode = (cmd.shadingMode >= 0) ? cmd.shadingMode : shadingMode;
            const float ao = (cmd.shadingMode >= 0) ? cmd.ambientOcclusion : m_lightingParameters.ambientOcclusion;
            const float outlineWidth = cmd.outlineWidth;

            // FBX 서브셋 머티리얼이 있으면 그걸 우선 사용 (Forward와 동일)
            std::shared_ptr<SkinnedMeshGPU> mesh =
                (m_skinnedRegistry && !cmd.meshKey.empty()) ? m_skinnedRegistry->Find(cmd.meshKey) : nullptr;

            // 인스턴싱 조건: 본 1개(Identity) + 아웃라인 없음 + 단일 서브셋
            const bool canInstance = IsRigidSkinnedCommand(cmd) &&
                                     (outlineWidth <= 0.0f) &&
                                     (!mesh || mesh->subsets.size() <= 1) &&
                                     m_transparentSkinnedInstancedVS &&
                                     m_transparentSkinnedInstancedInputLayout &&
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
                    diff = (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : nullptr;
                    norm = (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : nullptr;
                }
                else
                {
                    diff = GetOrCreateTexture(cmd.albedoTexturePath);
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
                key.color = color;
                key.roughness = cmd.roughness;
                key.metalness = cmd.metalness;
                key.ambientOcclusion = ao;
                key.envDiffuseStrength = cmd.envDiffuseStrength;
                key.envSpecularStrength = cmd.envSpecularStrength;
                key.normalStrength = cmd.normalStrength;
                key.toonPbrCuts = cmd.toonPbrCuts;
                key.toonPbrLevels = cmd.toonPbrLevels;
                key.shadingMode = objectShadingMode;
                key.useTexture = (diff != nullptr) ? 1 : 0;
                key.enableNormalMap = (norm != nullptr) ? 1 : 0;

                // 배치 키가 바뀌면 이전 배치 플러시
                if (hasBatch && (!IsSameInstancedKey(batchKey, key) || batchInstances.size() >= m_instanceCapacity))
                {
                    if (EnsureInstanceBufferCapacity(batchInstances.size()))
                    {
                        m_context->VSSetShader(m_transparentSkinnedInstancedVS.Get(), nullptr, 0);
                        m_context->IASetInputLayout(m_transparentSkinnedInstancedInputLayout.Get());

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

                        ID3D11ShaderResourceView* srvs01[2] = { batchKey.diffuseSRV, batchKey.normalSRV };
                        m_context->PSSetShaderResources(0, 2, srvs01);

                        UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, batchKey.color,
                                          batchKey.roughness, batchKey.metalness, batchKey.ambientOcclusion,
                                          (batchKey.useTexture != 0), (batchKey.enableNormalMap != 0),
                                          batchKey.shadingMode, batchKey.normalStrength,
                                          batchKey.toonPbrCuts, batchKey.toonPbrLevels,
                                          batchKey.envDiffuseStrength, batchKey.envSpecularStrength,
                                          DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                        m_context->DrawIndexedInstanced(batchKey.indexCount, (UINT)batchInstances.size(),
                                                        batchKey.startIndex, batchKey.baseVertex, 0);
                    }

                    batchInstances.clear();
                    hasBatch = false;

                    // 일반 스키닝 렌더링을 위해 파이프라인 복구
                    m_context->VSSetShader(m_transparentSkinnedVS.Get(), nullptr, 0);
                    m_context->IASetInputLayout(m_transparentSkinnedInputLayout.Get());
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
                    m_context->VSSetShader(m_transparentSkinnedInstancedVS.Get(), nullptr, 0);
                    m_context->IASetInputLayout(m_transparentSkinnedInstancedInputLayout.Get());

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

                    ID3D11ShaderResourceView* srvs01[2] = { batchKey.diffuseSRV, batchKey.normalSRV };
                    m_context->PSSetShaderResources(0, 2, srvs01);

                    UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, batchKey.color,
                                      batchKey.roughness, batchKey.metalness, batchKey.ambientOcclusion,
                                      (batchKey.useTexture != 0), (batchKey.enableNormalMap != 0),
                                      batchKey.shadingMode, batchKey.normalStrength,
                                      batchKey.toonPbrCuts, batchKey.toonPbrLevels,
                                      batchKey.envDiffuseStrength, batchKey.envSpecularStrength,
                                      DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                    m_context->DrawIndexedInstanced(batchKey.indexCount, (UINT)batchInstances.size(),
                                                    batchKey.startIndex, batchKey.baseVertex, 0);
                }

                batchInstances.clear();
                hasBatch = false;

                m_context->VSSetShader(m_transparentSkinnedVS.Get(), nullptr, 0);
                m_context->IASetInputLayout(m_transparentSkinnedInputLayout.Get());
            }

            // 일반 스키닝 렌더링
            UINT stride = cmd.stride;
            UINT offset = 0;
            ID3D11Buffer* vb = cmd.vertexBuffer;
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetIndexBuffer(cmd.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

            UpdateBonesCB(cmd.bones, cmd.boneCount);

            const XMFLOAT3 outlineColor = cmd.outlineColor;

            if (mesh && !mesh->subsets.empty())
            {
                for (const auto& sub : mesh->subsets)
                {
                    if (sub.indexCount == 0) continue;

                    ID3D11ShaderResourceView* diff =
                        (sub.materialIndex < mesh->materialSRVs.size()) ? mesh->materialSRVs[sub.materialIndex].Get() : nullptr;
                    ID3D11ShaderResourceView* norm =
                        (sub.materialIndex < mesh->normalSRVs.size()) ? mesh->normalSRVs[sub.materialIndex].Get() : nullptr;

                    ID3D11ShaderResourceView* srvs01[2] = { diff, norm };
                    m_context->PSSetShaderResources(0, 2, srvs01);

                    // Pass 1. 원본
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                      (diff != nullptr), (norm != nullptr), objectShadingMode,
                                      cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                      cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                      outlineColor, 0.0f);
                    m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                    
                    // Pass 2. 아웃라인
                    if (outlineWidth > 0.0f)
                    {
                        m_context->RSSetState(m_rsCullFront.Get());
                        UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                          (diff != nullptr), (norm != nullptr), objectShadingMode,
                                          cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                          cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                          outlineColor, outlineWidth);
                        m_context->DrawIndexed(sub.indexCount, sub.startIndex, cmd.baseVertex);
                        m_context->RSSetState(m_rasterizerState.Get());
                    }
                }
            }
            else
            {
                ID3D11ShaderResourceView* diff = GetOrCreateTexture(cmd.albedoTexturePath);
                ID3D11ShaderResourceView* srvs01[2] = { diff, nullptr };
                m_context->PSSetShaderResources(0, 2, srvs01);
                
                // [Pass 1] 원본
                UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                  (diff != nullptr), false, objectShadingMode,
                                  cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                  cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                  outlineColor, 0.0f);
                m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                
                // [Pass 2] 아웃라인
                if (outlineWidth > 0.0f)
                {
                    m_context->RSSetState(m_rsCullFront.Get());
                    UpdatePerObjectCB(cmd.world, view, proj, color, cmd.roughness, cmd.metalness, ao,
                                      (diff != nullptr), false, objectShadingMode,
                                      cmd.normalStrength, cmd.toonPbrCuts, cmd.toonPbrLevels,
                                      cmd.envDiffuseStrength, cmd.envSpecularStrength,
                                      outlineColor, outlineWidth);
                    m_context->DrawIndexed(cmd.indexCount, cmd.startIndex, cmd.baseVertex);
                    m_context->RSSetState(m_rasterizerState.Get());
                }
            }
        }

        // 마지막 배치 플러시
        if (hasBatch && !batchInstances.empty())
        {
            if (EnsureInstanceBufferCapacity(batchInstances.size()))
            {
                m_context->VSSetShader(m_transparentSkinnedInstancedVS.Get(), nullptr, 0);
                m_context->IASetInputLayout(m_transparentSkinnedInstancedInputLayout.Get());

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

                ID3D11ShaderResourceView* srvs01[2] = { batchKey.diffuseSRV, batchKey.normalSRV };
                m_context->PSSetShaderResources(0, 2, srvs01);

                UpdatePerObjectCB(DirectX::XMMatrixIdentity(), view, proj, batchKey.color,
                                  batchKey.roughness, batchKey.metalness, batchKey.ambientOcclusion,
                                  (batchKey.useTexture != 0), (batchKey.enableNormalMap != 0),
                                  batchKey.shadingMode, batchKey.normalStrength,
                                  batchKey.toonPbrCuts, batchKey.toonPbrLevels,
                                  batchKey.envDiffuseStrength, batchKey.envSpecularStrength,
                                  DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), 0.0f);

                m_context->DrawIndexedInstanced(batchKey.indexCount, (UINT)batchInstances.size(),
                                                batchKey.startIndex, batchKey.baseVertex, 0);
            }

            batchInstances.clear();
            hasBatch = false;

            m_context->VSSetShader(m_transparentSkinnedVS.Get(), nullptr, 0);
            m_context->IASetInputLayout(m_transparentSkinnedInputLayout.Get());
        }

        // SRV 정리 (D3D11 hazard 방지)
        ID3D11ShaderResourceView* nulls[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 8, nulls);
    }

    void DeferredRenderSystem::RenderSkybox(const Camera& camera)
    {
        // 유효성 체크 (ForwardRenderSystem과 동일)
        if (!m_skyboxEnabled || !m_skyboxSRV || !m_skyboxVS || !m_skyboxPS || !m_cbSkybox) return;

        // 씬 타겟에 렌더링 (깊이 버퍼 사용)
        m_context->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), m_sceneDSV.Get());
        m_context->OMSetDepthStencilState(m_skyboxDepthState.Get(), 0);
        m_context->RSSetState(m_skyboxRasterizerState.Get());

        // 큐브 지오메트리 설정 (ForwardRenderSystem의 큐브 사용 - 필요시 별도 생성)
        // 현재는 간단히 하기 위해 인라인 큐브 데이터 사용
        struct SkyboxVertex { XMFLOAT3 Position; };
        SkyboxVertex vertices[] = {
            {{-1,-1, 1}}, {{-1, 1, 1}}, {{ 1, 1, 1}}, {{ 1,-1, 1}},
            {{-1,-1,-1}}, {{ 1,-1,-1}}, {{ 1, 1,-1}}, {{-1, 1,-1}},
            {{-1, 1,-1}}, {{ 1, 1,-1}}, {{ 1, 1, 1}}, {{-1, 1, 1}},
            {{-1,-1,-1}}, {{-1,-1, 1}}, {{ 1,-1, 1}}, {{ 1,-1,-1}},
            {{-1,-1,-1}}, {{-1, 1,-1}}, {{-1, 1, 1}}, {{-1,-1, 1}},
            {{ 1,-1,-1}}, {{ 1,-1, 1}}, {{ 1, 1, 1}}, {{ 1, 1,-1}}
        };
        uint16_t indices[] = {
            0,1,2, 0,2,3,     4,5,6, 4,6,7,     8,9,10, 8,10,11,
            12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
        };

        // 임시 버퍼 생성 (최적화: 초기화 시 생성하는 것이 좋음)
        ComPtr<ID3D11Buffer> skyboxVB, skyboxIB;
        D3D11_BUFFER_DESC vbDesc = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, skyboxVB.GetAddressOf()))) return;

        D3D11_BUFFER_DESC ibDesc = { sizeof(indices), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA ibData = { indices, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, skyboxIB.GetAddressOf()))) return;

        // IA 설정
        UINT stride = sizeof(SkyboxVertex), offset = 0;
        ID3D11Buffer* vb = skyboxVB.Get();
        m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_context->IASetIndexBuffer(skyboxIB.Get(), DXGI_FORMAT_R16_UINT, 0);
        m_context->IASetInputLayout(m_skyboxInputLayout.Get());
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_context->VSSetShader(m_skyboxVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_skyboxPS.Get(), nullptr, 0);

        // 행렬 계산 (Translation 제거)
        XMMATRIX view = camera.GetViewMatrix();
        view.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);
        XMMATRIX wvpT = XMMatrixTranspose(view * camera.GetProjectionMatrix());

        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(m_context->Map(m_cbSkybox.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        {
            memcpy(map.pData, &wvpT, sizeof(XMMATRIX));
            m_context->Unmap(m_cbSkybox.Get(), 0);
        }

        // 리소스 바인딩
        ID3D11Buffer* cb = m_cbSkybox.Get();
        ID3D11ShaderResourceView* srv = m_skyboxSRV.Get();
        ID3D11SamplerState* sam = m_samplerState.Get();

        m_context->VSSetConstantBuffers(0, 1, &cb);
        m_context->PSSetShaderResources(0, 1, &srv);
        m_context->PSSetSamplers(0, 1, &sam);

        m_context->DrawIndexed(36, 0, 0);
    }

    void DeferredRenderSystem::UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                                                  const DirectX::XMMATRIX& view,
                                                  const DirectX::XMMATRIX& projection)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPerObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            XMMATRIX* data = (XMMATRIX*)mapped.pData;
            data[0] = XMMatrixTranspose(world);
            data[1] = XMMatrixTranspose(view);
            data[2] = XMMatrixTranspose(projection);
            data[3] = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
            m_context->Unmap(m_cbPerObject.Get(), 0);
        }

        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

void DeferredRenderSystem::UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                                             const DirectX::XMMATRIX& view,
                                             const DirectX::XMMATRIX& projection,
                                             const DirectX::XMFLOAT4& color,
                                             float roughness,
                                             float metalness,
                                             float ambientOcclusion,
                                             bool useTexture,
                                             bool enableNormalMap,
                                             int shadingMode,
                                             float normalStrength,
                                             const DirectX::XMFLOAT4& toonPbrCuts,
                                             const DirectX::XMFLOAT4& toonPbrLevels,
                                             float envDiffuseStrength,
                                             float envSpecularStrength,
                                             const XMFLOAT3& outlineColor,
                                             float outlineWidth)
    {
        struct CBPerObjectData
        {
            XMMATRIX gWorld;
            XMMATRIX gView;
            XMMATRIX gProj;
            XMFLOAT4 gMaterialColor;
            float    gRoughness;
            float    gMetalness;
            int      gUseTexture;
            int      gEnableNormalMap;
            int      gShadingMode;
            int      gPad0;
            // [Fixed] HLSL 패킹 규칙에 맞춰 8바이트 패딩 추가
            float    gPad1[2];        // Offset: 232 -> 240
            // 노말맵 강도 조절
            float    gNormalStrength; // Offset: 240 -> 244
            float    gAmbientOcclusion; // Offset: 244 -> 248
            float    gEnvDiffuseStrength; // Offset: 248 -> 252
            float    gEnvSpecularStrength; // Offset: 252 -> 256
            XMFLOAT4 gToonPbrCuts;    // Offset: 256 -> 272
            XMFLOAT4 gToonPbrLevels;  // Offset: 272 -> 288
            XMFLOAT3 gOutlineColor;   // Offset: 288 -> 300
            float    gOutlineWidth;   // Offset: 300 -> 304
        };

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPerObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            CBPerObjectData* data = (CBPerObjectData*)mapped.pData;
            data->gWorld = XMMatrixTranspose(world);
            data->gView  = XMMatrixTranspose(view);
            data->gProj  = XMMatrixTranspose(projection);
            data->gMaterialColor = color;
            data->gRoughness = roughness;
            data->gMetalness = metalness;
            data->gUseTexture = useTexture ? 1 : 0;
            data->gEnableNormalMap = enableNormalMap ? 1 : 0;
            data->gShadingMode = shadingMode;
            data->gPad0 = 0;
            // 패딩 초기화 (안전하게 0으로)
            data->gPad1[0] = 0.0f;
            data->gPad1[1] = 0.0f;
        data->gNormalStrength = normalStrength;
        data->gAmbientOcclusion = ambientOcclusion;
        data->gEnvDiffuseStrength = envDiffuseStrength;
        data->gEnvSpecularStrength = envSpecularStrength;
        data->gToonPbrCuts = toonPbrCuts;
            data->gToonPbrLevels = toonPbrLevels;
            data->gOutlineColor = outlineColor;
            data->gOutlineWidth = outlineWidth;
            m_context->Unmap(m_cbPerObject.Get(), 0);
        }

        m_context->VSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
        // PS에서도 재질 정보를 사용하므로 반드시 바인딩
        m_context->PSSetConstantBuffers(0, 1, m_cbPerObject.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateLightingCB(const Camera& camera, int shadingMode, bool /*enableFillLight*/, DirectX::CXMMATRIX lightViewProj)
    {
        // IMPORTANT:
        // - ConstantBufferData는 매우 큰 구조체이므로 "부분만 채우고 memcpy" 하면
        //   나머지 필드가 쓰레기 값이 되어 라이팅/파라미터가 랜덤하게 깨질 수 있습니다.
        // - 반드시 0 초기화 후 필요한 값을 모두 안정적으로 세팅합니다.
        ConstantBufferData cbData = {};

        // (1) 행렬: Deferred Light PS에서는 주로 g_EyePosW / PBR 파라미터 등을 사용하지만,
        //     구조체에 행렬 필드가 있으므로 안전하게 채웁니다.
        cbData.g_World = XMMatrixIdentity();
        cbData.g_View = XMMatrixTranspose(camera.GetViewMatrix());
        cbData.g_Proj = XMMatrixTranspose(camera.GetProjectionMatrix());
        const XMMATRIX viewProj = camera.GetViewProjectionMatrix();
        cbData.g_InvViewProj = XMMatrixTranspose(XMMatrixInverse(nullptr, viewProj));
        cbData.g_WorldInvTranspose = XMMatrixIdentity();
        cbData.g_LightViewProj = XMMatrixTranspose(lightViewProj); // (b0에도 보관: 디버그/호환용)

        // (2) 카메라
        cbData.g_EyePosW = camera.GetPosition();

        // (3) 셰이딩 모드
        cbData.g_ShadingMode = shadingMode;

        // (4) PBR/재질 파라미터 (Deferred PS가 참조하는 값 포함)
        cbData.g_PBRBaseColor = XMFLOAT4(m_lightingParameters.baseColor.x,
                                         m_lightingParameters.baseColor.y,
                                         m_lightingParameters.baseColor.z,
                                         1.0f);
        cbData.g_PBRMetalness = m_lightingParameters.metalness;
        cbData.g_PBRRoughness = m_lightingParameters.roughness;
        cbData.g_PBRAmbientOcclusion = m_lightingParameters.ambientOcclusion;
        cbData.g_UseTextureColor = 1;

        // (5) 섀도우 (PCF) - b4(ShadowCB)가 실제로 사용되지만, b0에도 안정적으로 채워둡니다.
        cbData.g_ShadowBias = m_shadowSettings.bias;
        cbData.g_ShadowMapSize = (float)((m_shadowMapSizePxEffective > 0) ? m_shadowMapSizePxEffective : m_shadowSettings.mapSizePx);
        cbData.g_ShadowPCFRadius = m_shadowSettings.pcfRadius;
        cbData.g_ShadowEnabled = m_shadowSettings.enabled ? 1 : 0;

        // (6) Directional light (b0에 있는 레거시 필드도 일관되게 세팅)
        cbData.g_DirLight_direction = m_lightingParameters.keyDirection;
        cbData.g_DirLight_intensity = m_lightingParameters.keyIntensity;
        cbData.g_DirLight_diffuse = XMFLOAT4(m_lightingParameters.diffuseColor.x,
                                             m_lightingParameters.diffuseColor.y,
                                             m_lightingParameters.diffuseColor.z,
                                             1.0f);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbLighting.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cbData, sizeof(ConstantBufferData));
            m_context->Unmap(m_cbLighting.Get(), 0);
        }

        // ConstantBuffer 바인딩 (register b0)
        m_context->PSSetConstantBuffers(0, 1, m_cbLighting.GetAddressOf());

        
        // DirectionalLightBuffer(b3)는 Deferred Light PS에서 직접 사용합니다.
        // ShadowPass의 라이트 방향/강도와 반드시 동일해야 섀도우 방향/세기가 일치합니다.
        DirectionalLightData lightData = {};
        {
            XMVECTOR dir = XMLoadFloat3(&m_lightingParameters.keyDirection);
            if (XMVector3Equal(dir, XMVectorZero()))
                dir = XMVectorSet(0, -1, 0, 0);
            dir = XMVector3Normalize(dir);

            XMFLOAT3 dirN{};
            XMStoreFloat3(&dirN, dir);
            lightData.direction = XMFLOAT4(dirN.x, dirN.y, dirN.z, 0.0f);
        }
        lightData.color = XMFLOAT4(m_lightingParameters.diffuseColor.x,
                                   m_lightingParameters.diffuseColor.y,
                                   m_lightingParameters.diffuseColor.z,
                                   0.0f);
        lightData.intensity = m_lightingParameters.keyIntensity;

        if (SUCCEEDED(m_context->Map(m_cbDirectionalLight.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &lightData, sizeof(DirectionalLightData));
            m_context->Unmap(m_cbDirectionalLight.Get(), 0);
        }

        m_context->PSSetConstantBuffers(3, 1, m_cbDirectionalLight.GetAddressOf());
    }

    void DeferredRenderSystem::UpdateExtraLightsCB(const World& world)
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

    void DeferredRenderSystem::UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices, std::uint32_t boneCount)
    {
        // ForwardRenderSystem과 동일한 구현
        if (!m_cbBones || !boneMatrices || boneCount == 0) return;

        static constexpr std::uint32_t MaxBones = 1023;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;


        auto* cb = reinterpret_cast<CBBones*>(mapped.pData);
        cb->boneCount = (std::min)(boneCount, MaxBones);

        // 유효한 본은 Transpose해서 넣고, 나머지는 Identity로 채움
        for (std::uint32_t i = 0; i < MaxBones; ++i)
        {
            if (i < cb->boneCount) cb->bones[i] = XMMatrixTranspose(XMLoadFloat4x4(&boneMatrices[i]));
            else cb->bones[i] = XMMatrixIdentity();
        }

        m_context->Unmap(m_cbBones.Get(), 0);
        m_context->VSSetConstantBuffers(2, 1, m_cbBones.GetAddressOf());
    }

    DirectX::XMMATRIX DeferredRenderSystem::BuildWorldMatrix(const TransformComponent& transform) const
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

    DirectX::XMMATRIX DeferredRenderSystem::BuildWorldMatrix(const World& world, EntityId entityId, const TransformComponent& transform) const
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

    ID3D11ShaderResourceView* DeferredRenderSystem::GetOrCreateTexture(const std::string& path)
    {
        // ForwardRenderSystem과 동일한 구현
        if (path.empty()) return nullptr;

        auto it = m_textureCache.find(path);
        if (it != m_textureCache.end()) return it->second.Get();

        if (!m_device || !m_resources) return nullptr;

        auto srv = m_resources->LoadData<ID3D11ShaderResourceView>(std::filesystem::path(path), m_device.Get());

        if (!srv)
        {
            ALICE_LOG_WARN("[DeferredRenderSystem] Texture load FAILED: \"%s\"", path.c_str());
            return nullptr;
        }

        m_textureCache.emplace(path, srv);
        ALICE_LOG_INFO("[DeferredRenderSystem] Texture loaded: \"%s\"", path.c_str());

        return srv.Get();
    }

    void DeferredRenderSystem::GetPostProcessParams(float& outExposure, float& outMaxHDRNits) const
    {
        outExposure = m_postProcessParams.exposure;
        
        // RenderDevice에서 HDR 지원 여부 및 최대 밝기 가져오기
        float maxNits = 100.0f;
        m_renderDevice.IsHDRSupported(maxNits);
        // 사용자가 설정한 값이 있으면 사용, 없으면 모니터 최대 밝기 사용
        outMaxHDRNits = (m_postProcessParams.maxHDRNits > 0.0f) ? m_postProcessParams.maxHDRNits : maxNits;
    }
    
    void DeferredRenderSystem::GetPostProcessParams(float& outExposure, float& outMaxHDRNits, float& outSaturation, float& outContrast, float& outGamma) const
    {
		outExposure = m_postProcessParams.exposure;

		// RenderDevice에서 HDR 지원 여부 및 최대 밝기 가져오기
		float maxNits = 100.0f;
		m_renderDevice.IsHDRSupported(maxNits);
		// 사용자가 설정한 값이 있으면 사용, 없으면 모니터 최대 밝기 사용
		outMaxHDRNits = (m_postProcessParams.maxHDRNits > 0.0f) ? m_postProcessParams.maxHDRNits : maxNits;
        // Vector4의 첫 번째 채널(R)을 반환 (하위 호환성)
        outSaturation = m_postProcessParams.colorGradingSaturation.x;
        outContrast = m_postProcessParams.colorGradingContrast.x;
        outGamma = m_postProcessParams.colorGradingGamma.x;
    }

    void DeferredRenderSystem::SetPostProcessParams(float exposure, float maxHDRNits)
    {
        m_postProcessParams.exposure = exposure;
        m_postProcessParams.maxHDRNits = maxHDRNits;
        // Color Grading은 기본값 유지 (하위 호환성)
    }
    
    void DeferredRenderSystem::SetPostProcessParams(float exposure, float maxHDRNits, float saturation, float contrast, float gamma)
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

	// Post Process Volume 블렌딩 (카메라 위치 기준)
    void DeferredRenderSystem::SetPostProcessVolume(const World& world, const Camera& camera)
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
		// Bloom 기본 설정
		defaultSettings.bloomThreshold = m_bloomSettings.threshold;
		defaultSettings.bloomKnee = m_bloomSettings.knee;
		defaultSettings.bloomIntensity = m_bloomSettings.intensity;
		defaultSettings.bloomGaussianIntensity = m_bloomSettings.gaussianIntensity;
		defaultSettings.bloomRadius = m_bloomSettings.radius;
		defaultSettings.bloomDownsample = m_bloomSettings.downsample;

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
        //DirectX::XMVectorLerp()
		m_postProcessParams.exposure = finalSettings.exposure;
		m_postProcessParams.maxHDRNits = finalSettings.maxHDRNits;
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
		// Bloom 설정 적용
		m_bloomSettings.threshold = finalSettings.bloomThreshold;
		m_bloomSettings.knee = finalSettings.bloomKnee;
		m_bloomSettings.intensity = finalSettings.bloomIntensity;
		m_bloomSettings.gaussianIntensity = finalSettings.bloomGaussianIntensity;
		m_bloomSettings.radius = finalSettings.bloomRadius;
	}

    void DeferredRenderSystem::ApplyColorGrading(const DirectX::XMFLOAT4& saturation, const DirectX::XMFLOAT4& contrast, const DirectX::XMFLOAT4& gamma, const DirectX::XMFLOAT4& gain)
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

    void DeferredRenderSystem::ApplyColorGrading(float saturation, float contrast, float gamma, float gain)
    {
        // 편의 함수: float을 Vector4로 확장
        DirectX::XMFLOAT4 satVec(saturation, saturation, saturation, 1.0f);
        DirectX::XMFLOAT4 contVec(contrast, contrast, contrast, 1.0f);
        DirectX::XMFLOAT4 gamVec(gamma, gamma, gamma, 1.0f);
        DirectX::XMFLOAT4 gainVec(gain, gain, gain, 1.0f);
        ApplyColorGrading(satVec, contVec, gamVec, gainVec);
    }

    void DeferredRenderSystem::GetColorGrading(DirectX::XMFLOAT4& outSaturation, DirectX::XMFLOAT4& outContrast, DirectX::XMFLOAT4& outGamma, DirectX::XMFLOAT4& outGain) const
    {
        outSaturation = m_postProcessParams.colorGradingSaturation;
        outContrast = m_postProcessParams.colorGradingContrast;
        outGamma = m_postProcessParams.colorGradingGamma;
        outGain = m_postProcessParams.colorGradingGain;
    }

    void DeferredRenderSystem::SetBloomSettings(const BloomSettings& settings)
    {
        bool downsampleChanged = (m_bloomSettings.downsample != settings.downsample);
        m_bloomSettings = settings;
        
        // 다운샘플링 변경 시 리소스 재생성
        if (downsampleChanged && m_sceneWidth > 0 && m_sceneHeight > 0)
        {
            CreateBloomResources(m_sceneWidth, m_sceneHeight);
        }
    }

    void DeferredRenderSystem::SetDefaultPostProcessSettings(const PostProcessSettings& settings)
    {
        m_defaultPostProcessSettings = settings;
        m_hasDefaultPostProcessSettings = true;
    }

    void DeferredRenderSystem::SetPPVReferenceObjectName(const std::string& objectName)
    {
        m_postProcessVolumeSystem.SetReferenceObjectName(objectName);
    }

    const std::string& DeferredRenderSystem::GetPPVReferenceObjectName() const
    {
        return m_postProcessVolumeSystem.GetReferenceObjectName();
    }

    bool DeferredRenderSystem::SetIblSet(const std::string& iblDir, const std::string& iblName, const std::string& iblSuffix)
    {
        return CreateIblResources(iblDir, iblName, iblSuffix);
    }

    void DeferredRenderSystem::SetSkyboxEnabled(bool enabled)
    {
        m_skyboxEnabled = enabled;

        if (!enabled)
        {
            m_iblDiffuseSRV.Reset();
            m_iblSpecularSRV.Reset();
            m_iblBrdfLutSRV.Reset();
            m_skyboxSRV.Reset();
        }
    }

    void DeferredRenderSystem::SetShadowUpdateInterval(std::uint32_t frames)
    {
        m_shadowUpdateInterval = (frames == 0) ? 1u : frames;
        m_shadowCacheDirty = true;
    }

    void DeferredRenderSystem::SetShadowResolutionScale(std::uint32_t scale)
    {
        m_shadowResolutionScale = (scale == 0) ? 1u : scale;
        m_shadowCacheDirty = true;
    }

    void DeferredRenderSystem::ForceShadowUpdate()
    {
        m_shadowCacheDirty = true;
    }

    void DeferredRenderSystem::RestoreBackBuffer()
    {
        // ForwardRenderSystem과 동일한 구현
        ID3D11RenderTargetView* backBufferRTV = m_renderDevice.GetBackBufferRTV();
        ID3D11DepthStencilView* backBufferDSV = m_renderDevice.GetBackBufferDSV();

        if (backBufferRTV)
        {
            ID3D11RenderTargetView* rtvs[] = { backBufferRTV };
            m_context->OMSetRenderTargets(1, rtvs, backBufferDSV);
        }
    }

    void DeferredRenderSystem::RenderToneMapping(ID3D11ShaderResourceView* inputSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!m_toneMappingPS || !m_quadVS || !inputSRV || !targetRTV) return;

        // 뷰포트 설정
        m_context->RSSetViewports(1, &viewport);

        // 렌더 타겟 설정
        m_context->OMSetRenderTargets(1, &targetRTV, nullptr);

        // 상태 정리 (이전 패스의 상태가 남아있으면 후처리가 이상해짐 조심하셈)
        float blendFactor[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
        m_context->RSSetState(m_ppRasterNoCull.Get());

        // 상수 버퍼 업데이트 (실제 노출값 적용)
        PostProcessCB cbData = {};
        GetPostProcessParams(cbData.exposure, cbData.maxHDRNits);
        cbData.colorGradingSaturation = m_postProcessParams.colorGradingSaturation;
        cbData.colorGradingContrast = m_postProcessParams.colorGradingContrast;
        cbData.colorGradingGamma = m_postProcessParams.colorGradingGamma;
        cbData.colorGradingGain = m_postProcessParams.colorGradingGain;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_cbPostProcess.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &cbData, sizeof(PostProcessCB));
            m_context->Unmap(m_cbPostProcess.Get(), 0);
        }

        // 리소스 바인딩 (입력 텍스처 사용)
        ID3D11SamplerState* sampler = m_samplerLinear.Get();
        ID3D11Buffer* cb = m_cbPostProcess.Get();

        m_context->PSSetShaderResources(0, 1, &inputSRV);
        m_context->PSSetSamplers(0, 1, &sampler);
        m_context->PSSetConstantBuffers(2, 1, &cb); // register(b2)에 맞춰 슬롯 2 사용

        // Quad 그리기
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->IASetInputLayout(m_quadInputLayout.Get());
        m_context->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &m_quadStride, &m_quadOffset);
        m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);
        m_context->PSSetShader(m_toneMappingPS.Get(), nullptr, 0);
        m_context->DrawIndexed(m_quadIndexCount, 0, 0);

        // 리소스 해제
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->PSSetShaderResources(0, 1, &nullSRV);
    }

    void DeferredRenderSystem::RenderParticleOverlay(ID3D11ShaderResourceView* particleSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!particleSRV || !targetRTV) return;
        if (!m_particleOverlayPS || !m_quadVS || !m_quadVB || !m_quadIB || !m_quadInputLayout) return;
        
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
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
        m_context->RSSetState(m_rasterizerState.Get());
    }

    void DeferredRenderSystem::RenderParticleOverlayToViewport(ID3D11ShaderResourceView* particleSRV)
    {
        // 씬 전환 중 리소스가 유효하지 않을 수 있으므로 모든 리소스 확인
        if (!particleSRV) return;
        ID3D11RenderTargetView* viewportRTV = m_viewportRTV.Get();
        if (!viewportRTV) return;
        if (m_sceneWidth == 0 || m_sceneHeight == 0) return;

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(m_sceneWidth);
        viewport.Height = static_cast<float>(m_sceneHeight);
        viewport.MaxDepth = 1.0f;

        RenderParticleOverlay(particleSRV, viewportRTV, viewport);
        
        // 뷰포트 RTV를 SRV로 읽을 수 있도록 BackBuffer로 복귀 (ImGui::Image가 viewportSRV를 읽기 위해 필수)
        // DirectX11에서는 같은 리소스를 RTV와 SRV로 동시에 바인딩할 수 없음
        RestoreBackBuffer();
    }

    void DeferredRenderSystem::RenderDebugOverlayToViewport(DebugDrawSystem& debugDraw, const Camera& camera, bool depthTest)
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

	void DeferredRenderSystem::RenderBloomPass(ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* hdrCompositeRTV, const D3D11_VIEWPORT& viewport)
	{
		if (!m_bloomSettings.enabled || !sourceSRV || !hdrCompositeRTV) return;
		if (!m_bloomBrightPassPS || !m_bloomDownsamplePS || !m_bloomBlurPassPS_H || !m_bloomBlurPassPS_V || !m_bloomUpsamplePS || !m_bloomCompositePS) return;

		// 상태 설정
		float blendFactor[4] = { 0, 0, 0, 0 };
		m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);
		m_context->OMSetDepthStencilState(m_ppDepthOff.Get(), 0);
		m_context->RSSetState(m_ppRasterNoCull.Get());

		// Quad 설정
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_context->IASetInputLayout(m_quadInputLayout.Get());
		m_context->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &m_quadStride, &m_quadOffset);
		m_context->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);
		m_context->VSSetShader(m_quadVS.Get(), nullptr, 0);

		ID3D11Buffer* cbBloom = m_cbBloom.Get();
		ID3D11SamplerState* sampler = m_samplerLinear.Get();
		ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

		// 블러 반복 횟수 (기본값: 1회, 필요시 BloomSettings에 추가 가능)
		const int blurIterations = 1;

		// ========== 1. Bright Pass: sourceSRV → level0 A ==========
		{
			std::uint32_t level0Width = m_bloomLevelWidth[0];
			std::uint32_t level0Height = m_bloomLevelHeight[0];
			D3D11_VIEWPORT level0Viewport = { 0.0f, 0.0f, (float)level0Width, (float)level0Height, 0.0f, 1.0f };

			float texelSizeX = (level0Width > 0) ? (1.0f / level0Width) : 1.0f;
			float texelSizeY = (level0Height > 0) ? (1.0f / level0Height) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.bloomIntensity = m_bloomSettings.intensity;
			bloomCB.gaussianIntensity = m_bloomSettings.gaussianIntensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			// [중요] RTV로 사용할 리소스의 SRV 언바인드 (충돌 방지)
			m_context->PSSetShaderResources(0, 8, nullSRVs);
			
			m_context->RSSetViewports(1, &level0Viewport);
			m_context->OMSetRenderTargets(1, m_bloomLevelRTV[0][0].GetAddressOf(), nullptr); // level0 A
			m_context->PSSetShaderResources(0, 1, &sourceSRV);
			m_context->PSSetSamplers(0, 1, &sampler);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomBrightPassPS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			m_context->PSSetShaderResources(0, 8, nullSRVs);
		}

		// ========== 2. Downsample Chain: level(i-1) A → level(i) A (i=1..4) ==========
		for (int level = 1; level < BLOOM_LEVEL_COUNT; ++level)
		{
			std::uint32_t prevWidth = m_bloomLevelWidth[level - 1];
			std::uint32_t prevHeight = m_bloomLevelHeight[level - 1];
			std::uint32_t currWidth = m_bloomLevelWidth[level];
			std::uint32_t currHeight = m_bloomLevelHeight[level];

			// 입력 텍스처의 텍셀 크기
			float inputTexelSizeX = (prevWidth > 0) ? (1.0f / prevWidth) : 1.0f;
			float inputTexelSizeY = (prevHeight > 0) ? (1.0f / prevHeight) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.bloomIntensity = m_bloomSettings.intensity;
			bloomCB.gaussianIntensity = m_bloomSettings.gaussianIntensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(inputTexelSizeX, inputTexelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			D3D11_VIEWPORT currViewport = { 0.0f, 0.0f, (float)currWidth, (float)currHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &currViewport);
			
			// [중요] RTV로 사용할 리소스의 SRV 언바인드 (충돌 방지)
			m_context->PSSetShaderResources(0, 8, nullSRVs);
			
			// level(i-1) A → level(i) A
			ID3D11ShaderResourceView* inputSRV = m_bloomLevelSRV[level - 1][0].Get(); // 이전 레벨 A
			m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level][0].GetAddressOf(), nullptr); // 현재 레벨 A

			m_context->PSSetShaderResources(0, 1, &inputSRV);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomDownsamplePS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			m_context->PSSetShaderResources(0, 8, nullSRVs);
		//}

		//// ========== 3. Blur per Level: level i에서 A↔B로 (H then V) * blurIterations ==========
		//for (int level = 0; level < BLOOM_LEVEL_COUNT; ++level)
		//{
			std::uint32_t levelWidth = m_bloomLevelWidth[level];
			std::uint32_t levelHeight = m_bloomLevelHeight[level];

			float texelSizeX = (levelWidth > 0) ? (1.0f / levelWidth) : 1.0f;
			float texelSizeY = (levelHeight > 0) ? (1.0f / levelHeight) : 1.0f;

			/*BloomCB */bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.bloomIntensity = m_bloomSettings.intensity;
			bloomCB.gaussianIntensity = m_bloomSettings.gaussianIntensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			//D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			D3D11_VIEWPORT levelViewport = { 0.0f, 0.0f, (float)levelWidth, (float)levelHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &levelViewport);

			int pingPongIndex = 0; // 0=A, 1=B

			for (int iteration = 0; iteration < blurIterations; ++iteration)
			{
				// Horizontal Blur: A → B
				ID3D11ShaderResourceView* inputSRV = m_bloomLevelSRV[level][pingPongIndex].Get();
				int outputPingPong = 1 - pingPongIndex;
				
				// [중요] RTV로 사용할 리소스의 SRV 언바인드 (충돌 방지)
				m_context->PSSetShaderResources(0, 8, nullSRVs);
				m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level][outputPingPong].GetAddressOf(), nullptr);
				
				m_context->PSSetShaderResources(0, 1, &inputSRV);
				m_context->PSSetConstantBuffers(3, 1, &cbBloom);
				m_context->PSSetShader(m_bloomBlurPassPS_H.Get(), nullptr, 0);
				m_context->DrawIndexed(m_quadIndexCount, 0, 0);
				
				m_context->PSSetShaderResources(0, 8, nullSRVs);
				
				// Vertical Blur: B → A
				pingPongIndex = outputPingPong;
				inputSRV = m_bloomLevelSRV[level][pingPongIndex].Get();
				outputPingPong = 1 - pingPongIndex;
				
				// [중요] RTV로 사용할 리소스의 SRV 언바인드 (충돌 방지)
				m_context->PSSetShaderResources(0, 8, nullSRVs);
				m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level][outputPingPong].GetAddressOf(), nullptr);
				
				m_context->PSSetShaderResources(0, 1, &inputSRV);
				m_context->PSSetConstantBuffers(3, 1, &cbBloom);
				m_context->PSSetShader(m_bloomBlurPassPS_V.Get(), nullptr, 0);
				m_context->DrawIndexed(m_quadIndexCount, 0, 0);
				
				m_context->PSSetShaderResources(0, 8, nullSRVs);
				
				pingPongIndex = outputPingPong;
			}
		}

		// ========== 4. Upsample+Add: level4 → level3 → ... → level0 (합성) ==========
		// Additive Blend State 사용 (기존 m_blendStateAdditive 사용)
		if (!m_blendStateAdditive)
		{
			ALICE_LOG_ERRORF("Additive blend state not available for bloom upsample");
			return;
		}

		// 레벨 4부터 레벨 0까지 역순으로 업샘플링+합성
		for (int level = BLOOM_LEVEL_COUNT - 1; level > 0; --level)
		{
			std::uint32_t lowResWidth = m_bloomLevelWidth[level];
			std::uint32_t lowResHeight = m_bloomLevelHeight[level];
			std::uint32_t highResWidth = m_bloomLevelWidth[level - 1];
			std::uint32_t highResHeight = m_bloomLevelHeight[level - 1];

			// 저해상도 텍스처의 텍셀 크기 (업샘플링용)
			float texelSizeX = (lowResWidth > 0) ? (1.0f / lowResWidth) : 1.0f;
			float texelSizeY = (lowResHeight > 0) ? (1.0f / lowResHeight) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.bloomIntensity = m_bloomSettings.intensity;
			bloomCB.gaussianIntensity = m_bloomSettings.gaussianIntensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			D3D11_VIEWPORT highResViewport = { 0.0f, 0.0f, (float)highResWidth, (float)highResHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &highResViewport);

			// Additive Blending 활성화 (고해상도 텍스처에 저해상도를 더하기)
			m_context->OMSetBlendState(m_blendStateAdditive.Get(), blendFactor, 0xFFFFFFFF);
			
			// [중요] RTV로 사용할 리소스의 SRV 언바인드 (충돌 방지)
			m_context->PSSetShaderResources(0, 8, nullSRVs);
			
			// 저해상도 텍스처만 바인딩 (업샘플링할 소스)
			// 고해상도 텍스처는 이미 RTV에 바인딩되어 있으므로 Additive Blending으로 자동 합성됨
			ID3D11ShaderResourceView* lowResSRV = m_bloomLevelSRV[level][0].Get(); // 현재 레벨의 블러 결과 (A)

			// 이전 레벨의 RTV에 업샘플링 결과를 렌더링 (Additive Blending으로 기존 값에 더하기)
			m_context->OMSetRenderTargets(1, m_bloomLevelRTV[level - 1][0].GetAddressOf(), nullptr);
			m_context->PSSetShaderResources(0, 1, &lowResSRV);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomUpsamplePS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			m_context->PSSetShaderResources(0, 8, nullSRVs);
		}

		// Additive Blending 비활성화
		m_context->OMSetBlendState(m_ppBlendOpaque.Get(), blendFactor, 0xFFFFFFFF);

		// ========== 5. Composite: Scene + Bloom → HDR Composite RT (HDR) ==========
		// 톤매핑과 합성을 분리합니다. 여기서는 HDR 상태로 합치기만 합니다.
		{
			// 뷰포트를 전체 씬 크기로 설정
			D3D11_VIEWPORT sceneViewport = { 0.0f, 0.0f, (float)m_sceneWidth, (float)m_sceneHeight, 0.0f, 1.0f };
			m_context->RSSetViewports(1, &sceneViewport);
			
			// [중요] 이전 패스의 SRV 언바인드 (RTV로 사용할 리소스와 충돌 방지)
			m_context->PSSetShaderResources(0, 8, nullSRVs);
			
			// 타겟을 HDR 합성 버퍼로 설정
			m_context->OMSetRenderTargets(1, &hdrCompositeRTV, nullptr);

			ID3D11ShaderResourceView* sceneSRV = sourceSRV;
			ID3D11ShaderResourceView* bloomSRV = m_bloomLevelSRV[0][0].Get(); // level0의 최종 bloom 결과
			ID3D11ShaderResourceView* compositeSRVs[2] = { sceneSRV, bloomSRV };

			m_context->PSSetShaderResources(0, 2, compositeSRVs);

			// [중요] 합성 단계에서는 Exposure를 1.0으로 강제하여 색상 변형 방지
			// 실제 노출 보정은 다음 단계인 RenderToneMapping에서 수행함
			PostProcessCB postProcessCB = {};
			float tempExposure;
			GetPostProcessParams(tempExposure, postProcessCB.maxHDRNits);
			postProcessCB.exposure = 1.0f; // 합성 단계에서는 노출 적용 안 함 (중립값)
			postProcessCB.colorGradingSaturation = m_postProcessParams.colorGradingSaturation;
			postProcessCB.colorGradingContrast = m_postProcessParams.colorGradingContrast;
			postProcessCB.colorGradingGamma = m_postProcessParams.colorGradingGamma;
			postProcessCB.colorGradingGain = m_postProcessParams.colorGradingGain;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(m_context->Map(m_cbPostProcess.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, &postProcessCB, sizeof(PostProcessCB));
				m_context->Unmap(m_cbPostProcess.Get(), 0);
			}

			// Bloom CB 업데이트 (최종 합성용)
			std::uint32_t level0Width = m_bloomLevelWidth[0];
			std::uint32_t level0Height = m_bloomLevelHeight[0];
			float texelSizeX = (level0Width > 0) ? (1.0f / level0Width) : 1.0f;
			float texelSizeY = (level0Height > 0) ? (1.0f / level0Height) : 1.0f;

			BloomCB bloomCB = {};
			bloomCB.threshold = m_bloomSettings.threshold;
			bloomCB.knee = m_bloomSettings.knee;
			bloomCB.bloomIntensity = m_bloomSettings.intensity;
			bloomCB.gaussianIntensity = m_bloomSettings.gaussianIntensity;
			bloomCB.radius = m_bloomSettings.radius;
			bloomCB.texelSize = DirectX::XMFLOAT2(texelSizeX, texelSizeY);
			bloomCB.downsample = m_bloomSettings.downsample;

			if (SUCCEEDED(m_context->Map(m_cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, &bloomCB, sizeof(BloomCB));
				m_context->Unmap(m_cbBloom.Get(), 0);
			}

			// 셰이더 및 버퍼 바인딩
			ID3D11Buffer* cbPostProcess = m_cbPostProcess.Get();
			ID3D11Buffer* cbBloom = m_cbBloom.Get();
			m_context->PSSetConstantBuffers(2, 1, &cbPostProcess);
			m_context->PSSetConstantBuffers(3, 1, &cbBloom);
			m_context->PSSetShader(m_bloomCompositePS.Get(), nullptr, 0);
			m_context->DrawIndexed(m_quadIndexCount, 0, 0);

			// [중요] SRV 언바인드 (다음 패스에서 RTV로 사용할 수 있도록)
			ID3D11ShaderResourceView* nullSRVs2[2] = { nullptr, nullptr };
			m_context->PSSetShaderResources(0, 2, nullSRVs2);
		}
		
		// RenderBloomPass는 여기서 종료. ToneMapping은 RenderPostProcess에서 호출됨.
	}

    void DeferredRenderSystem::RenderPostProcess(ID3D11RenderTargetView* backBufferRTV, const D3D11_VIEWPORT& viewport)
    {
        if (!backBufferRTV) return;

        // [중요] 이전 패스의 SRV 언바인드 (RTV로 사용할 리소스와 충돌 방지)
        ID3D11ShaderResourceView* nullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 8, nullSRVs);

        // ToneMapping 입력 SRV 결정
        ID3D11ShaderResourceView* toneMapInputSRV = m_sceneColorSRV.Get(); // 기본값: 씬 컬러

        // Bloom ON/OFF에 따른 흐름 분기
        if (m_bloomSettings.enabled)
        {
            // Bloom ON: Bloom 패스 실행 → HDR 합성 RT에 저장 → ToneMapping 입력으로 사용
            RenderBloomPass(m_sceneColorSRV.Get(), m_postBloomRTV.Get(), viewport);
            toneMapInputSRV = m_postBloomSRV.Get(); // HDR 합성 결과를 ToneMapping 입력으로
        }
        // Bloom OFF: 바로 ToneMapping으로 (toneMapInputSRV는 이미 m_sceneColorSRV)

        // [중요] ToneMapping 전에 SRV 언바인드 (backBufferRTV와 충돌 방지)
        m_context->PSSetShaderResources(0, 8, nullSRVs);

        // ToneMapping 패스: 항상 backBufferRTV로 렌더링
        RenderToneMapping(toneMapInputSRV, backBufferRTV, viewport);
    }

}
