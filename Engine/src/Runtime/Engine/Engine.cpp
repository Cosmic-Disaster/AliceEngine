#include "Runtime/Engine/EngineImpl.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Alice
{
	Engine::Engine(bool editorMode) : pImpl(std::make_unique<Impl>())
	{
		pImpl->m_editorMode = editorMode;
		pImpl->m_scriptSystem.SetEditorMode(editorMode);
	}

	Engine::~Engine()
	{
		Shutdown();
	}

	void Engine::Shutdown()
	{
		static bool s_isShutdown = false;
		if (s_isShutdown) return;
		s_isShutdown = true;

		// PVD 설정 저장 (엔진 종료 시)
		wchar_t pathBuf[MAX_PATH] = {}; 
		GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
		const std::filesystem::path exeDir = std::filesystem::path(pathBuf).parent_path();
		pImpl->SavePvdSettings(exeDir);
		pImpl->SaveLightingSettings(exeDir);

		// 1) 게임 루프/시스템이 물리 월드 참조 못 하게 먼저 끊기
		if (pImpl->m_physicsSystem)
		{
			pImpl->m_physicsSystem->SetPhysicsWorld(nullptr);
			pImpl->m_physicsSystem.reset();
		}

		// 2) World가 잡고 있는 physics world(shared_ptr) 해제
		pImpl->m_world.SetPhysicsWorld(nullptr);

		// 3) Editor/기타가 물리를 참조하면 여기서 먼저 정리
		pImpl->m_editorCore.Shutdown();
		pImpl->m_aliceUIRenderer.Shutdown();

		// 4) 마지막에 PhysX 컨텍스트 종료
		pImpl->m_physics.ShutdownContext();
		Sound::Shutdown();
	}

	bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
	{
		return pImpl->InitializeAll(*this, hInstance, nCmdShow);
	}

	int Engine::Run()
	{
		// 타이머 초기화
		pImpl->m_isRunning = true;
		pImpl->m_timer.Reset();
		pImpl->m_timer.Start();

		MSG msg = {}; 

		// 기본 게임 루프
		while (pImpl->m_isRunning)
		{
			// 윈도우 메시지는 바로바로 처리함
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					pImpl->m_isRunning = false;
					break;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			if (!pImpl->m_isRunning) break;

			pImpl->UpdateFrame();
			pImpl->RenderFrame();
		}

		// 종료할때 정리
		pImpl->m_scriptSystem.OnApplicationQuit(pImpl->m_world);
		return static_cast<int>(msg.wParam);
	}

	LRESULT Engine::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		return pImpl->HandleMessage(hWnd, message, wParam, lParam);
	}

	void Engine::EnsureSkinnedMeshesRegisteredForWorld()
	{
		pImpl->EnsureSkinnedMeshesRegisteredForWorld();
	}

	void Engine::TrimVideoMemory()
	{
		pImpl->TrimVideoMemory();
	}

	void Engine::RefreshPhysicsForCurrentWorld()
	{
		pImpl->RefreshPhysicsForCurrentWorld();
	}

	LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		// ============================================= ImGui 메시지 선처리 =============================================
		// 처리되었다면 즉시 종료)
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
			return true;

		// ============================================= DirectXTK 입력 처리 =============================================
		// Switch case로 메시지 호출
		switch (message)
		{
		case WM_ACTIVATEAPP:
			DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_INPUT:
			// Raw Input은 InputSystem에서 처리 (HandleMessage에서 처리됨)
			// DirectXTK에도 전달 (버튼 상태 등은 DirectXTK에서 관리)
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
		case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
		case WM_MOUSEWHEEL: case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_MOUSEHOVER:
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
			DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
			break;
		}

		// ============================================= Engine 인스턴스 연동 =============================================
		// 창 생성 시(WM_NCCREATE), CreateWindow에서 넘긴 'this' 포인터를 HWND에 저장
		if (message == WM_NCCREATE)
		{
			auto* const createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		}

		// 저장된 Engine 포인터를 가져와 멤버 함수로 넣어줌, 없으면 기본 윈도우 처리 반환
		auto* const engine = reinterpret_cast<Engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		return engine ? engine->HandleMessage(hWnd, message, wParam, lParam)
			: DefWindowProcW(hWnd, message, wParam, lParam);
	}
}
