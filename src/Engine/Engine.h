#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <memory>
#include <filesystem> // Initialize 분리용
#include <Core/ComponentRegistry.h>

namespace Alice
{
    /// 엔진 전체를 관리하는 가장 상위 레벨 클래스입니다.
    /// - 윈도우 생성 및 메시지 루프 관리
    /// - World 및 시스템 업데이트
    /// - 렌더 디바이스에게 렌더링을 요청
    class Engine
    {
    public:
        /// \param editorMode true 이면 에디터(도킹 UI) 모드, false 이면 게임 전용 모드
        Engine(bool editorMode = true);
        ~Engine();
        
        /// 엔진 종료 (명시적 종료 호출)
        void Shutdown();

        /// 엔진과 윈도우, 렌더 디바이스를 초기화합니다.
        bool Initialize(HINSTANCE hInstance, int nCmdShow);

        /// 메인 루프를 실행합니다.
        int Run();

        /// 윈도우 메시지를 처리하는 멤버 함수입니다.
        LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    private:
        // Win32 전역 윈도우 프로시저 → Engine 인스턴스로 위임
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        /// 한 프레임의 업데이트(게임 로직)를 수행합니다.
        void Update();

        /// 한 프레임의 렌더링을 수행합니다.
        void Render();

        /// 기본 윈도우를 생성합니다.
        bool CreateMainWindow(int nCmdShow);

        /// 윈도우 크기 변경 시 호출됩니다.
        void OnResize(std::uint32_t width, std::uint32_t height);

        /// 월드 안의 SkinnedMeshComponent 들에 대해,
        /// SkinnedMeshRegistry 에 GPU 메시가 등록되어 있는지 확인하고,
        /// 필요 시 FBX 를 다시 임포트해서 등록합니다.
        void EnsureSkinnedMeshesRegisteredForWorld();

        /// 씬 전환 시 IBL 세트를 업데이트합니다.
        /// - onAfterSceneLoaded 델리게이트에 연결되어 자동으로 호출됩니다.
        void UpdateIblForScene();

        void TrimVideoMemory();

        /// 렌더링 모드를 설정합니다 (true: Forward, false: Deferred)
        void SetUseForwardRendering(bool useForward);
        bool GetUseForwardRendering() const;
        //===========================================
        //물리
        void RefreshPhysicsForCurrentWorld();
        void TickPhysics(float dt);
        void ProcessPhysicsEvents();
        /// 월드와 물리 시스템을 함께 정리하는 안전한 진입점
        /// World::Clear()와 PhysicsSystem 정리를 함께 처리하여 누락을 방지
        void ClearWorldAndPhysics();
        //===========================================

        // =========================================================================================
        // Initialize / Update / Render 의미 단위 분리
        // - Engine::Initialize/Update/Render는 "흐름"만 남기고, 실제 블록은 아래 함수로 이동합니다.
        // =========================================================================================

        // ---- Initialize 분리 ----
        /// 실행 파일 디렉토리 경로를 반환합니다.
        std::filesystem::path InitializeGetExeDir() const;

        /// ResourceManager 초기화 및 게임 모드 데이터 무결성 검증을 수행합니다.
        bool InitializeResourceManagers(const std::filesystem::path& exeDir);

        /// PVD 설정 로드 및 PhysX 컨텍스트 초기화를 수행합니다.
        bool InitializePhysicsContext(const std::filesystem::path& exeDir);

        /// 윈도우 생성, 입력 시스템 및 렌더 디바이스 초기화를 수행합니다.
        bool InitializeWindowInputAndDevice(int nCmdShow);

        /// 에디터 모드일 경우 EditorCore 초기화를 수행합니다.
        bool InitializeEditorCoreIfNeeded();

        /// 오디오 시스템 초기화를 수행합니다.
        bool InitializeAudio();

        /// Forward/Deferred/DebugDraw/Effect/Trail/Compute/UI 등 렌더링 시스템 초기화를 수행합니다.
        bool InitializeRenderSystems();

        /// 기본 카메라 설정 및 스크립트 핫리로드를 수행합니다.
        void InitializeCameraAndHotReload();

        /// 씬 매니저 생성 및 초기 씬 로드를 수행합니다.
        void InitializeSceneManagerAndLoadFirstScene(const std::filesystem::path& exeDir);

        /// PhysicsSystem 생성 및 World Clear 콜백 설정을 수행합니다.
        void InitializePhysicsSystemAndCallbacks();

        /// ScriptSystem 델리게이트 바인딩 및 UI 리소스 복구를 수행합니다.
        void InitializeBindDelegatesAfterSceneLoad();

        /// World::Clear() 직전 호출되는 정리 루틴 (콜백이므로 접두사 규칙 대상 아님)
        void WorldOnBeforeClear();

        // ---- Update 분리 ----
        /// 타이머 및 입력 시스템을 갱신하고 delta time을 반환합니다.
        float UpdateTickTimerAndInput();

        /// 에디터 모드에서 Play/Stop 시 씬 스냅샷 저장 및 복원을 처리합니다.
        void UpdateHandleEditorPlayStopSnapshot();

        /// 씬 및 스크립트 시스템을 업데이트하고, 씬 변경 여부를 반환합니다.
        /// \return true면 "씬이 바뀐 프레임"
        bool UpdateSceneAndScripts(float dt);

        /// 애니메이션, 물리, 카메라 시스템 등 런타임 시스템을 업데이트합니다.
        void UpdateRuntimeSystems(float dt);

        /// 에디터 프리캠 입력 처리를 수행합니다.
        void UpdateEditorFreeCam(float dt);

        /// 최종 카메라 LookAt을 적용합니다.
        void UpdateApplyFinalCamera();

        /// UIWorld 업데이트를 수행합니다.
        void UpdateUIWorld();

        // ---- Render 분리 ----
        /// 렌더링 시스템 전환 요청이 있으면 안전하게 전환하고 GPU 바인딩을 해제합니다.
        void RenderApplyPendingRenderSystemChange();

        /// 에디터 모드일 경우 EditorCore 및 디버그 드로우를 렌더링합니다.
        void RenderEditorFrame();

        /// 스키닝 드로우 커맨드 빌드 및 온디맨드 메시 로딩을 수행합니다.
        void RenderBuildSkinnedDrawCommandsAndLoadMeshes();

        /// Forward/Deferred 렌더링 패스를 실행합니다.
        void RenderScenePass(int shadingMode);

        /// Depth Stencil View만 unbind합니다 (depth SRV 읽기 전 필수).
        void RenderUnbindDepthOnly();

        /// ComputeEffectSystem을 실행합니다.
        void RenderExecuteComputeEffects();

        /// 파티클 오버레이, 톤매핑, 블룸, UI 합성을 수행합니다.
        void RenderCompositeParticlesAndToneMap();

        /// 디버그 드로우, 이펙트, 트레일, ImGui 오버레이를 렌더링합니다.
        void RenderOverlays();
        // =========================================================================================

    private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
    };
}

