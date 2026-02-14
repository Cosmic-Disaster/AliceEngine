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
    class TrailEffectRenderSystem;
    class UIRenderer;
    /// 디퍼드 렌더링 시스템입니다.
    /// - G-Buffer 패스: 지오메트리 정보를 G-Buffer에 렌더링
    /// - Deferred Light 패스: G-Buffer를 읽어서 조명 계산
    /// - Post Process: Tone Mapping 등 포스트 프로세스 효과 적용
    class DeferredRenderSystem
    {
    public:
        explicit DeferredRenderSystem(ID3D11RenderDevice& renderDevice);
        ~DeferredRenderSystem() = default;

        /// 셰이더, G-Buffer 등 렌더링에 필요한 리소스를 생성합니다.
        bool Initialize(std::uint32_t width, std::uint32_t height);

        /// 뷰포트 크기가 변경되면 G-Buffer 텍스처도 함께 리사이즈합니다.
        void Resize(std::uint32_t width, std::uint32_t height);

        /// 리소스 매니저를 주입합니다.
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        /// 스키닝 메시 레지스트리를 주입합니다.
        void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }

        /// 디퍼드 렌더링을 수행합니다.
        /// @param world ECS 월드
        /// @param camera 카메라
        /// @param entity (현재는 사용하지 않지만, 향후 특정 엔티티만 선택 렌더링용으로 예약)
        /// @param cameraEntities 카메라 엔티티 집합
        /// @param shadingMode 셰이딩 모드
        /// @param enableFillLight 보조광 사용 여부
        /// @param skinnedCommands 스키닝 메시 드로우 커맨드 목록
        /// @param editorMode 에디터 모드 여부
        /// @param isPlaying 재생 중 여부
        void Render(const World& world,
                    const Camera& camera,
                    EntityId entity,
                    const std::unordered_set<EntityId>& cameraEntities,
                    int shadingMode,
                    bool enableFillLight,
                    const std::vector<SkinnedDrawCommand>& skinnedCommands,
                    bool editorMode = false,
                    bool isPlaying = false);

        /// 선택된 카메라 미리보기용 렌더링을 수행합니다 (에디터 전용).
        void RenderCameraPreview(const World& world,
                                 const Camera& camera,
                                 const std::unordered_set<EntityId>& cameraEntities,
                                 int shadingMode,
                                 bool enableFillLight,
                                 const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                 bool editorMode = false,
                                 bool isPlaying = false,
                                 EntityId hiddenCameraEntity = InvalidEntityId);

        /// 씬 컬러 텍스처 SRV를 반환합니다 (에디터에서 사용).
        ID3D11ShaderResourceView* GetSceneColorSRV() const { return m_sceneColorSRV.Get(); }
        ID3D11RenderTargetView* GetSceneRTV() const { return m_sceneRTV.Get(); }
        ID3D11DepthStencilView* GetSceneDSV() const { return m_sceneDSV.Get(); }
        std::uint32_t GetSceneWidth()  const { return m_sceneWidth; }
        std::uint32_t GetSceneHeight() const { return m_sceneHeight; }

        /// 에디터 뷰포트 표시용(톤매핑 완료) SRV
        ID3D11ShaderResourceView* GetViewportSRV() const { return m_viewportSRV.Get(); }
        ID3D11RenderTargetView* GetViewportRTV() const { return m_viewportRTV.Get(); }
        /// Decal D-Buffer SRV (디버그 뷰용)
        ID3D11ShaderResourceView* GetDecalDBufferSRV() const { return m_dBufferSRVs[0].Get(); }

        /// 선택 카메라 미리보기용 SRV
        ID3D11ShaderResourceView* GetCameraPreviewSRV() const { return m_cameraPreviewViewportSRV.Get(); }
        float GetCameraPreviewAspect() const { return (m_cameraPreviewHeight > 0) ? (float)m_cameraPreviewWidth / (float)m_cameraPreviewHeight : 1.0f; }
        
        /// Scene Depth SRV를 반환합니다 (depth test용)
        ID3D11ShaderResourceView* GetSceneDepthSRV() const { return m_sceneDepthSRV.Get(); }

        /// 이번 프레임에 실제로 사용한 카메라 View-Projection 행렬을 반환합니다 (ComputeEffect용)
        const DirectX::XMMATRIX& GetLastViewProj() const { return m_lastViewProj; }
        /// 이번 프레임에 실제로 사용한 카메라 월드 위치를 반환합니다 (ComputeEffect용)
        const DirectX::XMFLOAT3& GetLastCameraPos() const { return m_lastCameraPos; }

        /// IBL 세트를 변경합니다.
        bool SetIblSet(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge", const std::string& iblSuffix = "HDR");

        /// 스카이박스 활성화/비활성화를 설정합니다.
        void SetSkyboxEnabled(bool enabled);

        /// Shadow 업데이트 간격(프레임). 1이면 매 프레임 갱신.
        void SetShadowUpdateInterval(std::uint32_t frames);
        /// Shadow 해상도 스케일 (1=원본, 2=1/2, 4=1/4 등).
        void SetShadowResolutionScale(std::uint32_t scale);
        /// Shadow 맵 강제 갱신 플래그.
        void ForceShadowUpdate();

        /// 배경색을 설정합니다 (스카이박스가 Off일 때 사용).
        void SetBackgroundColor(const DirectX::XMFLOAT4& color) { m_backgroundColor = color; }
        const DirectX::XMFLOAT4& GetBackgroundColor() const { return m_backgroundColor; }

        /// 톤매핑을 적용하여 HDR 씬 텍스처를 백버퍼에 렌더링합니다.
        /// @param inputSRV 입력 HDR 텍스처 SRV (씬 컬러 또는 Bloom 합성 결과)
        /// @param targetRTV 백버퍼 RTV
        /// @param viewport 뷰포트 영역
        void RenderToneMapping(ID3D11ShaderResourceView* inputSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);

        /// Bloom 패스를 렌더링합니다.
        /// @param sourceSRV 입력 씬 텍스처 SRV
        /// @param hdrCompositeRTV HDR 합성 결과를 저장할 RTV (m_hdrAfterBloomRTV)
        /// @param viewport 뷰포트 영역
        /// @note 결과는 m_hdrAfterBloomSRV에 저장됩니다. ToneMapping은 별도로 호출해야 합니다.
        void RenderBloomPass(ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* hdrCompositeRTV, const D3D11_VIEWPORT& viewport);
        
        /// 포스트 프로세스 패스를 렌더링합니다 (Bloom + ToneMapping).
        /// @param backBufferRTV 최종 출력 백버퍼 RTV
        /// @param viewport 뷰포트 영역
        void RenderPostProcess(ID3D11RenderTargetView* backBufferRTV, const D3D11_VIEWPORT& viewport);
                
        /// 뷰포트 렌더 타겟에 파티클 오버레이 합성 (에디터 모드용)
        void RenderParticleOverlayToViewport(ID3D11ShaderResourceView* particleSRV);
        /// 파티클 오버레이 합성 (게임 모드용)
        void RenderParticleOverlay(ID3D11ShaderResourceView* particleSRV, ID3D11RenderTargetView* targetRTV, const D3D11_VIEWPORT& viewport);

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

        void SetPostProcessVolume(const World& world, const Camera& camera);

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

        /// Bloom 설정 가져오기
        const BloomSettings& GetBloomSettings() const { return m_bloomSettings; }
        
        /// Bloom 설정 설정하기
        void SetBloomSettings(const BloomSettings& settings);

        /// Default PostProcess Settings 설정 (EditorCore에서 호출)
        void SetDefaultPostProcessSettings(const PostProcessSettings& settings);

        /// PostProcessVolume 참조 대상 GameObject 이름 설정
        void SetPPVReferenceObjectName(const std::string& objectName);
        
        /// PostProcessVolume 참조 대상 GameObject 이름 가져오기
        const std::string& GetPPVReferenceObjectName() const;

        LightingParameters& GetLightingParameters() { return m_lightingParameters; }
        const LightingParameters& GetLightingParameters() const { return m_lightingParameters; }


		void SetSwordRenderSystem(TrailEffectRenderSystem* pSwordRenderSystem) { m_trailRenderSystem = pSwordRenderSystem; }

        /// AliceUI 렌더러 주입
        void SetUIRenderer(UIRenderer* renderer) { m_uiRenderer = renderer; }

    private:
        UIRenderer* m_uiRenderer{ nullptr };

        /// 백버퍼로 렌더 타겟을 복귀시킵니다 (ImGui 등 후처리를 위해).
        void RestoreBackBuffer();
        // G-Buffer 개수 (Normal+Roughness, Metalness+ToonCuts, BaseColor, ToonParams)
        static constexpr int GBufferCount = 4;
        // D-Buffer 개수 (Decal Albedo)
        static constexpr int DBufferCount = 1;

        // G-Buffer 생성
        bool CreateGBuffer(std::uint32_t width, std::uint32_t height);
        // D-Buffer 생성
        bool CreateDBuffer(std::uint32_t width, std::uint32_t height);
        
        // 셰이더 및 리소스 생성
        bool CreateShaders();
        bool CreateQuadGeometry();
        bool CreateCubeGeometry();
        bool CreateConstantBuffers();
        bool CreateSamplerStates();
        bool CreateBlendStates();
        bool CreateRasterizerStates();
        bool CreateDepthStencilStates();
        bool CreateInstanceBuffer(std::uint32_t initialCapacity);
        bool EnsureInstanceBufferCapacity(std::size_t requiredCount);
        bool CreateIblResources(const std::string& iblDir = "Bridge", const std::string& iblName = "bridge", const std::string& iblSuffix = "HDR");
        bool CreateShadowMapResources();
        bool CreateToneMappingResources(const std::uint32_t& width, const std::uint32_t& height);
        bool CreateCameraPreviewTargets(std::uint32_t width, std::uint32_t height);

        bool CreateBloomResources(const std::uint32_t& width, const std::uint32_t& height);
        bool CreatePostBloomResources(const std::uint32_t& width, const std::uint32_t& height);
        
        // 렌더링 패스
        DirectX::XMMATRIX RenderShadowPass(const World& world,
                                           const Camera& camera,
                                           const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                           const std::unordered_set<EntityId>& cameraEntities,
                                           bool editorMode = false,
                                           bool isPlaying = false);
        void PassGBuffer(const World& world, 
                        const Camera& camera,
                        const std::vector<SkinnedDrawCommand>& skinnedCommands,
                        const std::unordered_set<EntityId>& cameraEntities,
                        int shadingMode,
                        bool editorMode = false,
                        bool isPlaying = false,
                        EntityId hiddenCameraEntity = InvalidEntityId);
        void PassDecals(const World& world, const Camera& camera);
        void PassDeferredLight(const World& world,
                               const Camera& camera,
                               int shadingMode,
                               bool enableFillLight,
                               DirectX::CXMMATRIX lightViewProj);
        void RenderSkybox(const Camera& camera);
        // 반투명(알파 블렌딩) 오브젝트는 Deferred(GBuffer)로 정확히 합성하기 어렵기 때문에
        // 라이트 패스 이후 Forward-Style 패스로 별도 렌더링합니다.
        void PassTransparentForward(const Camera& camera,
                                    const std::vector<SkinnedDrawCommand>& skinnedCommands,
                                    int shadingMode);
        
        // 상수 버퍼 업데이트
        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
                               const DirectX::XMMATRIX& view,
                               const DirectX::XMMATRIX& projection);
        void UpdatePerObjectCB(const DirectX::XMMATRIX& world,
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
                                float envDiffuseStrength = 1.0f,
                                float envSpecularStrength = 1.0f,
                                const DirectX::XMFLOAT3& outlineColor = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
                                float outlineWidth = 0.00f);
        void UpdateLightingCB(const Camera& camera,
                              int shadingMode,
                              bool enableFillLight,
                              DirectX::CXMMATRIX lightViewProj);
        void UpdateExtraLightsCB(const World& world);
        void UpdateBonesCB(const DirectX::XMFLOAT4X4* boneMatrices, std::uint32_t boneCount);
        
        // 월드 행렬 구성
        DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& transform) const;
        DirectX::XMMATRIX BuildWorldMatrix(const World& world, EntityId entityId, const TransformComponent& transform) const;

        // Shadow 리소스 관리
        std::uint32_t GetShadowMapSizePx() const;
        bool EnsureShadowMapResources();
        
        // 텍스처 로딩
        ID3D11ShaderResourceView* GetOrCreateTexture(const std::string& path);
        
    private:
        ID3D11RenderDevice& m_renderDevice;
        ResourceManager*     m_resources { nullptr };
        SkinnedMeshRegistry* m_skinnedRegistry { nullptr };
        class TrailEffectRenderSystem* m_trailRenderSystem { nullptr };

        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_context;

        // ==== G-Buffer 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_gBufferTextures[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_gBufferRTVs[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_gBufferSRVs[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewGBufferTextures[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_cameraPreviewGBufferRTVs[GBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewGBufferSRVs[GBufferCount];

        // ==== D-Buffer 리소스 (Decal) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_dBufferTextures[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_dBufferRTVs[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_dBufferSRVs[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewDBufferTextures[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_cameraPreviewDBufferRTVs[DBufferCount];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewDBufferSRVs[DBufferCount];

        // ==== G-Buffer 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_gBufferVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_gBufferPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_gBufferInputLayout;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_gBufferInstancedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_gBufferInstancedInputLayout;

        // ==== Decal 패스 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_decalVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_decalPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_decalInputLayout;

        // ==== 스키닝용 G-Buffer 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_gBufferSkinnedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_gBufferSkinnedInputLayout;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_gBufferSkinnedInstancedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_gBufferSkinnedInstancedInputLayout;

        // ==== Deferred Light 패스 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_deferredLightPS;

        // ==== Transparent Forward-Style 패스 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_transparentVS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_transparentSkinnedVS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_transparentSkinnedInstancedVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_transparentPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_transparentInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_transparentSkinnedInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_transparentSkinnedInstancedInputLayout;

        // ==== Tone Mapping 셰이더 ====
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_toneMappingPS;

        // ==== Bloom 셰이더 ====
        // 5단계 레벨 (0~4), 각 레벨마다 ping-pong 텍스처 2장 (A/B)
        static constexpr int BLOOM_LEVEL_COUNT = 5;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_bloomLevelSRV[BLOOM_LEVEL_COUNT][2]; // [level][A=0/B=1]
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_bloomLevelRTV[BLOOM_LEVEL_COUNT][2];
        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_bloomLevelTex[BLOOM_LEVEL_COUNT][2];
        std::uint32_t m_bloomLevelWidth[BLOOM_LEVEL_COUNT];
        std::uint32_t m_bloomLevelHeight[BLOOM_LEVEL_COUNT];

		// HDR 합성 결과 텍스처 (Bloom ON일 때 Scene + Bloom 합성 결과 저장)
		Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_postBloomTex;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_postBloomRTV;      // 별칭: m_hdrAfterBloomRTV
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_postBloomSRV;   // 별칭: m_hdrAfterBloomSRV
       
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomBrightPassPS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomDownsamplePS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomBlurPassPS_H;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomBlurPassPS_V;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomUpsamplePS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>      m_bloomCompositePS;

        // ==== Shadow pass shaders ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_shadowVS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_shadowSkinnedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_shadowInputLayout; // POSITION only
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_shadowInstancedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_shadowInstancedInputLayout;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_shadowSkinnedInstancedVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_shadowSkinnedInstancedInputLayout;

        // ==== Quad (FullScreen) 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11VertexShader>     m_quadVS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>      m_quadInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_quadVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_quadIB;
        UINT                                           m_quadIndexCount = 0;
        UINT                                           m_quadStride = 0;
        UINT                                           m_quadOffset = 0;

        // ==== 큐브 지오메트리 (정적 메시) ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cubeVB;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cubeIB;
        UINT                                           m_cubeIndexCount = 0;

        // ==== 상수 버퍼 ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPerObject;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbLighting;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbDirectionalLight;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbExtraLights;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbBones;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbPostProcess;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbBloom;
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbDecal;
        // Transparent Forward-Style 패스용 최소 조명 CB
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbTransparentLight;
        // Shadow 전용 CB (정확한 패킹/행렬 전달용)
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_cbShadow;

        // ==== GPU 인스턴싱 버퍼 ====
        Microsoft::WRL::ComPtr<ID3D11Buffer>           m_instanceBuffer;
        std::uint32_t                                   m_instanceCapacity = 0;

        // ==== 씬 렌더 타겟 (최종 결과) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_sceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneColorSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_sceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_sceneDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneDepthSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewSceneColorTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_cameraPreviewSceneRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewSceneColorSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewSceneDepthTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_cameraPreviewSceneDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewSceneDepthSRV;

        // ==== 에디터 뷰포트 표시용 LDR 결과 텍스처 (ToneMapped) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_viewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_viewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_viewportSRV;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_cameraPreviewViewportTex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_cameraPreviewViewportRTV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cameraPreviewViewportSRV;

        std::uint32_t                                   m_sceneWidth  = 0;
        std::uint32_t                                   m_sceneHeight = 0;
        std::uint32_t                                   m_cameraPreviewWidth  = 320;
        std::uint32_t                                   m_cameraPreviewHeight = 180;

        // 이번 프레임에 실제로 사용한 카메라 정보 (ComputeEffect용)
        DirectX::XMMATRIX                               m_lastViewProj = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT3                                m_lastCameraPos{0, 0, 0};

        // ==== 샘플러 상태 ====
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_samplerState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_shadowSampler;
        
        // 파티클 오버레이용
        Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_particleOverlayPS;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendAdditive;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_samplerLinear;

        // ==== 블렌드 상태 ====
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_blendStateAdditive; // 라이트 패스용
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_alphaBlendState;    // 반투명 Forward 패스용
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_decalBlendState;    // 데칼 패스용

        // ==== 래스터라이저 상태 ====
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_rasterizerState;
        // Shadow depth bias RS
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_shadowRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_shadowRasterizerStateReversed;
        // 아웃라인용 (Cull Front) 래스터라이저
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_rsCullFront;

        // ==== 깊이/스텐실 상태 ====
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilStateReadOnly; // 라이트 패스용
        
        // ==== 톤매핑 전용 상태 객체 (Blend OFF, Depth OFF, Cull OFF) ====
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_ppDepthOff;
        Microsoft::WRL::ComPtr<ID3D11BlendState>        m_ppBlendOpaque;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_ppRasterNoCull;

        // ==== IBL 리소스 ====
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblDiffuseSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblSpecularSRV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_iblBrdfLutSRV;
        std::string                                      m_currentIblSet;
        std::string                                      m_currentIblSuffix{ "HDR" };

        // ==== 스카이박스 리소스 ====
        bool                                             m_skyboxEnabled { true };
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_skyboxSRV;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>       m_skyboxVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_skyboxPS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_skyboxInputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer>            m_cbSkybox;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>  m_skyboxDepthState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_skyboxRasterizerState;
        DirectX::XMFLOAT4                                m_backgroundColor { 0.1f, 0.1f, 0.1f, 1.0f };

        // ==== 섀도우 맵 리소스 (ForwardRenderSystem과 공유 가능하도록 설계) ====
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_shadowTex;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_shadowDSV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shadowSRV;
        D3D11_VIEWPORT                                  m_shadowViewport {};
        ShadowSettings                                  m_shadowSettings {};

        // ==== Shadow 캐시/갱신 설정 ====
        std::uint64_t                                   m_shadowFrameIndex = 0;
        std::uint64_t                                   m_shadowLastUpdateFrame = 0;
        DirectX::XMMATRIX                               m_lastShadowViewProj = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT3                               m_lastShadowLightDir{ 0.0f, 0.0f, 0.0f };
        bool                                            m_shadowCacheDirty = true;
        bool                                            m_shadowEnabledLast = true;
        std::uint32_t                                   m_shadowUpdateInterval = 2; // 1: 매 프레임
        std::uint32_t                                   m_shadowResolutionScale = 2; // 1: 원본, 2: 1/2
        std::uint32_t                                   m_shadowMapSizePxEffective = 0;

        // Forward와 동일한 조명/재질 파라미터 (에디터 UI 공유)
        LightingParameters                              m_lightingParameters {};
        
        // ==== 텍스처 캐시 ====
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textureCache;

        // ==== 포스트 프로세스 파라미터 ====
        PostProcessParams m_postProcessParams;
        PostProcessVolumeSystem m_postProcessVolumeSystem;  // Post Process Volume 시스템
        
        // ==== Bloom 파라미터 ====
        BloomSettings m_bloomSettings;

        // ==== Default PostProcess Settings (EditorCore에서 설정) ====
        PostProcessSettings m_defaultPostProcessSettings;
        bool m_hasDefaultPostProcessSettings = false;

        // ==== Camera Icon (Editor) ====
        std::string m_cameraIconMeshKey = "Camera";
        bool m_cameraIconLoadTried = false;
        float m_cameraIconScale = 0.5f;

        // ==== UI 합성 리소스 ====
    };
}
