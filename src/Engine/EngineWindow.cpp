#include "Engine/EngineImpl.h"

namespace Alice
{
	namespace
	{
		constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";
	}

	bool Engine::Impl::CreateMainWindow(Engine& owner, int nCmdShow)
	{
		// ============================================= 아이콘 로드 =============================================
		// 파일 로드 실패 시 기본 아이콘 사용
		// 경로는 한 번만 변환하여 사용
		const std::wstring iconPath = m_resourceManager.Resolve("Resource/Icon/Alice.ico").wstring();

		auto hIconBig = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
		auto hIconSmall = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));

		// ============================================= 윈도우 클래스 등록 =============================================
		// C++ 구조체 제로 초기화({})를 활용하여 불필요한 0 대입 생략
		WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = &Engine::WindowProc;
		wc.hInstance = m_hInstance;
		wc.hIcon = hIconBig ? hIconBig : LoadIcon(nullptr, IDI_APPLICATION);     // Fallback 처리
		wc.hIconSm = hIconSmall ? hIconSmall : LoadIcon(nullptr, IDI_APPLICATION);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wc.lpszClassName = kWindowClassName;

		if (!RegisterClassExW(&wc)) return false;

		// ============================================= 실제 윈도우 크기 계산 =============================================
		// Client Size -> Window Size
		RECT rc = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		// ============================================= 윈도우 생성 =============================================
		// Engine 포인터 전달
		m_hWnd = CreateWindowExW(
			0, kWindowClassName, L"AliceRenderer", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, // 계산된 너비/높이 바로 사용
			nullptr, nullptr, m_hInstance, &owner
		);

		if (!m_hWnd) return false;

		ShowWindow(m_hWnd, nCmdShow);
		UpdateWindow(m_hWnd);

		return true;
	}

	void Engine::Impl::OnResize(std::uint32_t width, std::uint32_t height)
	{
		m_width = width;
		m_height = height;

		// 디바이스 리사이즈 및 카메라 종횡비 갱신
		if (m_renderDevice)
		{
			m_renderDevice->Resize(width, height);

			// 높이가 0이어도 안전하게 1로 처리하여 계산
			const float aspect = static_cast<float>(width) / (std::max)(height, 1u);
			m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
		}

		// 렌더러 리사이즈 (텍스처 재생성 등)
		if (m_forwardRenderSystem)
		{
			m_forwardRenderSystem->Resize(width, height);
		}
		if (m_deferredRenderSystem)
		{
			m_deferredRenderSystem->Resize(width, height);
		}
		if (m_computeEffectSystem)
		{
			m_computeEffectSystem->Resize(width, height);
		}

		if(m_uiWorld.m_d3dDev)
		{
			// UI 시스템 리사이즈 (텍스처 재생성)
			//m_uiWorld.Create2DTex(width, height);
		}
		
	}

	LRESULT Engine::Impl::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_SIZE:
			// 리사이즈
			// lParam의 하위/상위 워드에서 해상도 추출 후 즉시 반영
			OnResize(static_cast<std::uint32_t>(LOWORD(lParam)), static_cast<std::uint32_t>(HIWORD(lParam)));
			return 0;

		case WM_DESTROY:
			// 종료
			// 메인 루프 플래그 해제 및 종료 메시지 전송
			m_isRunning = false;
			PostQuitMessage(0);
			return 0;

		case WM_INPUT:
			// Raw Input 처리
			m_inputSystem.ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
			return 0;
		}

		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

}
