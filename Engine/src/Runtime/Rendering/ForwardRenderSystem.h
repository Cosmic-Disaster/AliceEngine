#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Rendering/RenderTypes.h"
#include "Runtime/Rendering/PostProcessVolumeSystem.h"
#include "Runtime/ECS/Components/TransformComponent.h"

namespace Alice
{
    class ResourceManager;
    class DebugDrawSystem;
    class UIRenderer;
    /// 간단한 Forward 렌더 시스템입니다.
    /// - 큐브 1개를 그려서 Phong / Blinn-Phong 라이트를 확인할 수 있습니다.
    /// - World의 TransformComponent를 읽어와 월드 행렬을 구성합니다.
    class ForwardRenderSystem
    {
    public:
        explicit ForwardRenderSystem(ID3D11RenderDevice& renderDevice);
        ~ForwardRenderSystem() = default;

        /// 셰이더, 버퍼 등 렌더링에 필요한 리소스를 생성합니다.
        bool Initialize(std::uint32_t width, std::uint32_t height);

        /// 뷰포트 크기가 변경되면 렌더 타깃 텍스처도 함께 리사이즈합니다.
        void Resize(std::uint32_t width, std::uint32_t height);

        /// 리소스 매니저를 주입합니다.
        /// - 텍스처 등의 로딩/쿠킹에 사용됩니다.
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        /// 스키닝 메시 메타데이터(서브셋/스켈레톤)를 조회하기 위한 레지스트리를 주입합니다.
        void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }
        
    public:
        /// 단일 엔티티(예: 큐브)와 스키닝 메시들을 함께 렌더링합니다.
        /// \param world        ECS 월드 (Transform 정보 조회)
        /// \param camera       카메라 (뷰/투영 행렬 및 카메라 위치)
        /// \param entity       (현재는 사용하지 않지만, 향후 특정 엔티티만 선택 렌더링용으로 예약)
        /// \param shadingMode  0: Lambert, 1: Phong, 2: Blinn-Phong, 3: Toon, 4: PBR, 5: ToonPBR, 6: OnlyTextureWithOutline, 7: ToonPBREditable
        /// \param enableFillLight 보조광 사용 여부
        /// \param skinnedCommands 스키닝 메시 드로우 커맨드 목록
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    const std::unordered_set<EntityId>& cameraEntities,
                    int shadingMode,
                    bool enableFillLight,
                    const std::vector<SkinnedDrawCommand>& skinnedCommands);

        /// 선택된 카메라 미리보기용 렌더링을 수행합니다 (에디터 전용).
        void RenderCameraPreview(const World& world,
                                 const Camera& camera,
                                 const std::unordered_set<EntityId>& cameraEntities,
                                 int shadingMode,
                                 bool enableFillLight,
                                 const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                 EntityId hiddenCameraEntity = InvalidEntityId);
    private:


    private:
        bool CreateCubeGeometry();
        bool CreateShadersAndInputLayout();
        bool CreateConstantBuffers();
        bool CreateTextures();
        bool CreateSamplerState();
		bool CreateBlendStates();
        bool CreateRasterizerStates();
        bool CreateDepthStencilStates();
        bool CreateInstanceBuffer(std::uint32_t initialCapacity);
        bool EnsureInstanceBufferCapacity(std::size_t requiredCount);

        bool CreateSkyboxResources();
        bool CreateIblResources(const std::string& iblDir = "Bridge",  const std::string& iblName = "bridge", const std::string& iblSuffix = "HDR");
        bool CreateSkinnedResources();
        bool CreateToneMappingResources();

        void RenderSkybox(const Camera& camera);

        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection,
                               const DirectX::XMFLOAT4& materialColor,
                               const float& roughness,
                               const float& metalness,
                               float ambientOcclusion,
                               const bool& useTexture,
                               const bool& enableNormalMap,
                               int shadingMode,
                               float normalStrength,
                               const DirectX::XMFLOAT4& toonPbrCuts,
                               const DirectX::XMFLOAT4& toonPbrLevels,
                               float envDiffuseStrength = 1.0f,
                               float envSpecularStrength = 1.0f,
                               const DirectX::XMFLOAT3& outlineColor = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
                               float outlineWidth = 0.01f);

        void UpdateLightingCB(const Camera& camera,
                              int shadingMode,
                              bool enableFillLight,
                              const DirectX::XMMATRIX& lightViewProj);

        void UpdateExtraLightsCB(const World& world);

        void UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices,
                           std::uint32_t boneCount);

        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& transform) const;
        DirectX::XMMATRIX BuildWorldMatrix(const World& world, EntityId entityId, const TransformComponent& transform) const;

        //void GetSceneBounds(const World& world, DirectX::XMVECTOR& outFocus, float& outRadius);
       // void SetCullState(DirectX::CXMMATRIX worldM, bool isShadowPass);

        DirectX::XMMATRIX RenderShadowPass(const World& world, const std::vector<SkinnedDrawCommand>& skinnedCommands, const std::unordered_set<EntityId>& cameraEntities);
        void RenderMainPass(const World& world, const Camera& camera, int shadingMode, bool enableFillLight, DirectX::CXMMATRIX lightViewProj);

        bool IsValidPipeline() const;
        void RestoreBackBuffer();

        ID3D11ShaderResourceView* GetOrCreateTexture(const std::string& path);

    private:
        ID3D11RenderDevice& m_renderDevice;
        ResourceManager*     m_resources { nullptr };
        SkinnedMeshRegistry* m_skinnedRegistry { nullptr };

        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;

        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_indexBuffer;
        UINT                                           m_indexCount = 0;

        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_inputLayout;

        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPerObject;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbLighting;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbExtraLights;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbSkybox; // 스카이박스 전용 CB (DYNAMIC)
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPostProcess; // 톤매핑용 PostProcess CB

        // 텍스처 / 샘플러
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_diffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_normalSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_flatNormalSRV; // (0.5,0.5,1) 기본 노말맵
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_specularSRV;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_samplerState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_samplerLinear; // 톤매핑용 Linear Sampler

        // 알파 블렌드용 State
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_alphaBlendState;

        // 음수 스케일(반전 스케일)을 위한 컬링 모드 제어용 래스터라이저 상태
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rasterizerStateReversed;
        // 섀도우 맵 깊이 바이어스 전용 RS
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_shadowRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_shadowRasterizerStateReversed;
        // 아웃라인용 (Cull Front) 래스터라이저
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_rsCullFront;

        // 머티리얼 전용 텍스처 캐시 (경로 -> SRV)
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;

        LightingParameters                              m_lightingParameters;

        // ==== 스카이박스 리소스 ====
        bool                                            m_skyboxEnabled { true };
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_skyboxSRV;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>       m_skyboxVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_skyboxPS;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>  m_skyboxDepthState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_skyboxRasterizerState;
        
        // 배경색 (스카이박스가 Off일 때 사용)
        DirectX::XMFLOAT4                                m_backgroundColor { 0.1f, 0.1f, 0.1f, 1.0f };

        // ==== 포스트 프로세스 파라미터 ====
        PostProcessParams m_postProcessParams;
        PostProcessVolumeSystem m_postProcessVolumeSystem;  // Post Process Volume 시스템

        // ==== IBL (Image-Based Lighting) 리소스 ====
        // - Diffuse IBL: Irradiance map (간접 난반사)
        // - Specular IBL: Prefiltered env map (거칠기별 반사)
        // - BRDF LUT: (NdotV, Roughness)에 대한 F,G 적분 평균값
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblDiffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblSpecularSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblBrdfLutSRV;
        std::string                                      m_currentIblSet; // 현재 IBL 세트 이름 (Bridge/Indoor/Sample)
        std::string                                      m_currentIblSuffix{ "HDR" };

        // ==== 게임 뷰포트 렌더 타깃 (Scene Color) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_sceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;

        // ==== 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped) ====
        // - ImGui::Image는 HDR/톤매핑/감마를 처리하지 않으므로, 표시 전용 텍스처를 별도로 생성합니다.
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_viewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_viewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_viewportSRV;

        // ==== 게임 뷰포트용 깊이/스텐실 ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_sceneDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneDepthSRV;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilStateReadOnly;

        std::uint32_t                                   m_sceneWidth  = 0;
        std::uint32_t                                   m_sceneHeight = 0;

        // ==== 선택 카메라 미리보기 렌더 타깃 ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewSceneTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_cameraPreviewSceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewSceneSRV;

        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewViewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_cameraPreviewViewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewViewportSRV;

        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_cameraPreviewDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewDepthSRV;

        std::uint32_t                                   m_cameraPreviewWidth  = 320;
        std::uint32_t                                   m_cameraPreviewHeight = 180;

        // 이번 프레임에 실제로 사용한 카메라 정보 (ComputeEffect용)
        DirectX::XMMATRIX                               m_lastViewProj = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT3                                m_lastCameraPos{0, 0, 0};

        bool CreateSceneRenderTarget(std::uint32_t width, std::uint32_t height);
        bool CreateCameraPreviewRenderTarget(std::uint32_t width, std::uint32_t height);

        // ==== 섀도우 맵 리소스 (단일 Directional Light) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_shadowTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_shadowDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shadowSRV;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_shadowSampler;
        D3D11_VIEWPORT                                  m_shadowViewport {};
        ShadowSettings                                  m_shadowSettings {};

        bool CreateShadowMapResources();

        // ==== 스키닝 전용 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_skinnedVertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_skinnedInstancedVertexShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayoutSkinned;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayoutSkinnedInstanced;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_cbBones;

        // ==== GPU 인스턴싱 버퍼 ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_instanceBuffer;
        std::uint32_t                                   m_instanceCapacity = 0;

        // ==== 톤매핑 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_quadVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_toneMappingPS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_particleOverlayPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_quadInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_quadVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_quadIB;
        UINT                                           m_quadIndexCount = 0;
        UINT                                           m_quadStride = 0;
        UINT                                           m_quadOffset = 0;
        // 톤매핑 전용 상태 객체 (Blend OFF, Depth OFF, Cull OFF)
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_ppDepthOff;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendOpaque;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendAdditive;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_ppRasterNoCull;

    public:
        /// 스키닝 메시를 렌더링합니다.
        /// - AliceGame 의 SkinnedMeshSystem 이 만들어 준 DrawCommand 리스트를 사용합니다.
        void RenderSkinnedMeshes(const Camera& camera,
                                 const std::vector<SkinnedDrawCommand>& commands,
                                 int shadingMode,
                                 bool enableFillLight,
                                 DirectX::CXMMATRIX lightViewProj);

        /// 현재 조명 파라미터(색상, 강도, Shininess 등)를 반환합니다.
        /// ImGui 등에서 이 값을 직접 수정해도 됩니다.
        LightingParameters& GetLightingParameters() { return m_lightingParameters; }
        const LightingParameters& GetLightingParameters() const { return m_lightingParameters; }

        /// Game 창에서 사용할 씬 컬러 텍스처 SRV
        ID3D11ShaderResourceView* GetSceneColorSRV() const { return m_sceneSRV.Get(); }
        ID3D11RenderTargetView* GetSceneRTV() const { return m_sceneRTV.Get(); }
        ID3D11DepthStencilView* GetSceneDSV() const { return m_sceneDSV.Get(); }
        std::uint32_t GetSceneWidth()  const { return m_sceneWidth; }
        std::uint32_t GetSceneHeight() const { return m_sceneHeight; }

		ID3D11ShaderResourceView* GetSceneSRV() const { return m_sceneSRV.Get(); }

        /// 에디터 뷰포트 표시용(톤매핑 완료) SRV
        ID3D11ShaderResourceView* GetViewportSRV() const { return m_viewportSRV.Get(); }
        ID3D11RenderTargetView* GetViewportRTV() const { return m_viewportRTV.Get(); }
        
        /// Scene Depth SRV (depth test용)
        ID3D11ShaderResourceView* GetSceneDepthSRV() const { return m_sceneDepthSRV.Get(); }

        /// 선택 카메라 미리보기용 SRV
        ID3D11ShaderResourceView* GetCameraPreviewSRV() const { return m_cameraPreviewViewportSRV.Get(); }
        float GetCameraPreviewAspect() const { return (m_cameraPreviewHeight > 0) ? (float)m_cameraPreviewWidth / (float)m_cameraPreviewHeight : 1.0f; }

        /// 이번 프레임에 실제로 사용한 카메라 View-Projection 행렬을 반환합니다 (ComputeEffect용)
        const DirectX::XMMATRIX& GetLastViewProj() const { return m_lastViewProj; }
        /// 이번 프레임에 실제로 사용한 카메라 월드 위치를 반환합니다 (ComputeEffect용)
        const DirectX::XMFLOAT3& GetLastCameraPos() const { return m_lastCameraPos; }

        /// IBL 세트를 변경합니다 (Bridge/Indoor/Sample)
        /// - 씬 전환 시 호출하여 환경에 맞는 IBL을 로드합니다.
        bool SetIblSet(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge", const std::string& iblSuffix = "HDR");

        /// 스카이박스 활성화/비활성화를 설정합니다.
        /// - enabled가 false이면 IBL도 함께 비활성화됩니다.
        void SetSkyboxEnabled(bool enabled);

        /// 배경색을 설정합니다 (스카이박스가 Off일 때 사용).
        void SetBackgroundColor(const DirectX::XMFLOAT4& color) { m_backgroundColor = color; }
        const DirectX::XMFLOAT4& GetBackgroundColor() const { return m_backgroundColor; }

        /// 톤매핑을 적용하여 HDR 씬 텍스처를 백버퍼에 렌더링합니다.
        /// @param targetRTV 백버퍼 RTV
        /// @param viewport 뷰포트 영역
        void RenderToneMapping(ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);

        /// 파티클 텍스처를 오버레이로 합성합니다 (additive blending)
        void RenderParticleOverlay(ID3D11ShaderResourceView* particleSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);
        
        /// 뷰포트 렌더 타겟에 파티클 오버레이 합성 (에디터 모드용)
        void RenderParticleOverlayToViewport(ID3D11ShaderResourceView* particleSRV);

        /// 에디터 뷰포트에 DebugDraw 라인을 합성합니다.
        void RenderDebugOverlayToViewport(DebugDrawSystem& debugDraw, const Camera& camera, bool depthTest);

        /// 포스트 프로세스 파라미터 가져오기
        void GetPostProcessParams(float& outExposure, float& outMaxHDRNits) const;
        
        /// 포스트 프로세스 파라미터 가져오기 (Color Grading 포함)
        void GetPostProcessParams(float& outExposure, float& outMaxHDRNits, float& outSaturation, float& outContrast, float& outGamma) const;
        
        /// 포스트 프로세스 파라미터 설정하기
        void SetPostProcessParams(float exposure, float maxHDRNits);
        
        /// 포스트 프로세스 파라미터 설정하기 (Color Grading 포함)
        void SetPostProcessParams(float exposure, float maxHDRNits, float saturation, float contrast, float gamma);

        /// Color Grading 파라미터만 설정하기 (Unreal Engine 스타일 - RGB 채널별 제어)
        /// @param saturation 채도 (R,G,B 채널별, 0.0 = 흑백, 1.0 = 원본, 2.0+ = 과포화, W=1.0)
        /// @param contrast 대비 (R,G,B 채널별, 0.0 = 회색, 1.0 = 원본, 2.0 = 고대비, W=1.0)
        /// @param gamma 감마 보정 (R,G,B 채널별, 0.1~3.0, 1.0 = 원본, <1 = 밝게, >1 = 어둡게, W=1.0)
        /// @param gain Multiply 스케일 (R,G,B 채널별, 0.0 = 검정, 1.0 = 원본, >1.0 = 밝게, W=1.0)
        /// 값은 자동으로 안전 범위로 클램프됩니다.
        void ApplyColorGrading(const DirectX::XMFLOAT4& saturation, const DirectX::XMFLOAT4& contrast, const DirectX::XMFLOAT4& gamma, const DirectX::XMFLOAT4& gain);
        
        /// Color Grading 파라미터만 설정하기 (편의 함수 - float을 Vector4로 확장)
        /// @param saturation 채도 (모든 채널에 동일 적용)
        /// @param contrast 대비 (모든 채널에 동일 적용)
        /// @param gamma 감마 보정 (모든 채널에 동일 적용)
        /// @param gain Multiply 스케일 (모든 채널에 동일 적용)
        void ApplyColorGrading(float saturation, float contrast, float gamma, float gain);
        
        /// Color Grading 파라미터 가져오기
        /// @param outSaturation 채도 출력 (Vector4)
        /// @param outContrast 대비 출력 (Vector4)
        /// @param outGamma 감마 출력 (Vector4)
        /// @param outGain Gain 출력 (Vector4)
        void GetColorGrading(DirectX::XMFLOAT4& outSaturation, DirectX::XMFLOAT4& outContrast, DirectX::XMFLOAT4& outGamma, DirectX::XMFLOAT4& outGain) const;

        /// AliceUI 렌더러 주입
        void SetUIRenderer(UIRenderer* renderer) { m_uiRenderer = renderer; }

    private:
        // ==== UI 합성 리소스 ====
        UIRenderer*                                    m_uiRenderer{ nullptr };
        
    };
}


