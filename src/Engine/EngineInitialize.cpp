#include "Engine/EngineImpl.h"

namespace Alice
{
	namespace
	{

		// PVD 설정 저장/로드 함수
		std::filesystem::path GetEngineSettingsPath(const std::filesystem::path& exeDir)
		{
			namespace fs = std::filesystem;
			// 에디터 모드: 프로젝트 루트 / EngineSettings.json
			// 게임 모드: 실행 파일 위치 / EngineSettings.json
			fs::path cfg = exeDir / "EngineSettings.json";
			if (!fs::exists(cfg))
			{
				// 빌드 경로에도 확인
				cfg = exeDir.parent_path().parent_path().parent_path() / "EngineSettings.json";
			}
			return cfg;
		}

		void LoadPvdSettings(const std::filesystem::path& exeDir, bool& enabled, std::string& host, int& port)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			if (!fs::exists(cfg))
			{
				// 파일이 없으면 기본값 유지
				return;
			}

			std::ifstream ifs(cfg);
			if (!ifs.is_open()) return;

			nlohmann::json j;
			try
			{
				ifs >> j;
			}
			catch (...)
			{
				ALICE_LOG_WARN("EngineSettings.json parse error. Using defaults.");
				return;
			}

			if (j.contains("pvd"))
			{
				const auto& pvd = j["pvd"];
				if (pvd.contains("enabled") && pvd["enabled"].is_boolean())
					enabled = pvd["enabled"].get<bool>();
				if (pvd.contains("host") && pvd["host"].is_string())
					host = pvd["host"].get<std::string>();
				if (pvd.contains("port") && pvd["port"].is_number_integer())
					port = pvd["port"].get<int>();
			}
		}

		void SavePvdSettingsFile(const std::filesystem::path& exeDir, bool enabled, const std::string& host, int port)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			// 디렉토리 생성 (없으면)
			fs::create_directories(cfg.parent_path());

			nlohmann::json j;

			// 기존 파일이 있으면 읽어서 병합
			if (fs::exists(cfg))
			{
				std::ifstream ifs(cfg);
				if (ifs.is_open())
				{
					try
					{
						ifs >> j;
					}
					catch (...)
					{
						// 파싱 실패해도 계속 진행 (새 파일로 덮어쓰기)
					}
				}
			}

			// PVD 설정 업데이트
			j["pvd"] = nlohmann::json::object();
			j["pvd"]["enabled"] = enabled;
			j["pvd"]["host"] = host;
			j["pvd"]["port"] = port;

			// 저장
			std::ofstream ofs(cfg);
			if (!ofs.is_open())
			{
				ALICE_LOG_ERRORF("Failed to save EngineSettings.json");
				return;
			}

			ofs << j.dump(4); // 들여쓰기 4칸으로 포맷
			ALICE_LOG_INFO("PVD settings saved to EngineSettings.json");
		}

		// BuildSettings.txt 에서 시작 씬(.scene 파일)을 읽어와 World 에 로드합니다.
		// - scenes 섹션은 "index: path" 형식으로 저장되어 있다고 가정합니다.
		bool LoadStartupSceneFromBuildSettings(World& world, const ResourceManager& resources, const std::filesystem::path& exeDir, UIWorldManager* uiWorldManager = nullptr)
		{
			namespace fs = std::filesystem;

			// 경로 설정 (상수 없이 바로 대입)
			fs::path cfg = exeDir / "BuildSettings.json";
			if (!fs::exists(cfg)) // 빌드 경로 없으면 프로젝트 루트 확인
				cfg = exeDir.parent_path().parent_path().parent_path() / "Build/BuildSettings.json";

			std::ifstream ifs(cfg);
			if (!ifs.is_open()) return false;

			nlohmann::json j;
			try { ifs >> j; }
			catch (...) { return false; }

			std::string target = j.value("default", std::string{});
			std::vector<std::string> scenes;
			if (j.contains("scenes") && j["scenes"].is_array())
			{
				for (const auto& v : j["scenes"])
					if (v.is_string()) scenes.push_back(v.get<std::string>());
			}

			// 씬 결정 및 경로 보정
			if (target.empty() && !scenes.empty()) target = scenes[0];
			if (target.empty()) return false;

		const fs::path logicalScene = fs::path(target);
		ALICE_LOG_INFO("Loading Startup Scene: %s (uiWorldManager=%p)", logicalScene.string().c_str(), uiWorldManager);

		// gameMode에서는 Assets/... 가 Metas/Chunks 로 패킹되어 있으므로 LoadAuto를 사용합니다.
		if (!SceneFile::LoadAuto(world, resources, logicalScene, uiWorldManager))
		{
			ALICE_LOG_ERRORF("Scene Load Failed: %s", logicalScene.string().c_str());
			return false;
		}

		ALICE_LOG_INFO("Startup Scene loaded successfully: %s", logicalScene.string().c_str());
		return true;
		}
	}

	bool Engine::Impl::InitializeAll(Engine& owner, HINSTANCE hInstance, int nCmdShow)
	{
		m_hInstance = hInstance;

		InitializeMainThreadAndRegistry();

		const std::filesystem::path exeDir = InitializeResolveExeDir();

		if (!InitializeConfigureResourceManagers(exeDir)) return false;
		if (!InitializeValidateGameDataIfNeeded()) return false;

		InitializeLoadPvdSettings(exeDir);
		if (!InitializePhysicsContext()) return false;

		if (!InitializeWindowAndInput(owner, nCmdShow)) return false;
		if (!InitializeRenderDevice()) return false;

		if (!InitializeEditorCoreIfNeeded()) return false;

		InitializeAudio();

		if (!InitializeRenderSystems()) return false;
		if (!InitializeUI()) return false;
		if (!InitializeComputeEffectSystem()) return false;

		InitializeCameraAndScriptHotReload();

		if (!InitializeScene(exeDir)) return false;
		if (!InitializePhysicsSystemAndWorldCallbacks()) return false;

		InitializePostLoadBindings(owner);
		InitializeEnsureUIResourcesAfterSceneLoad();

		ALICE_LOG_INFO("Engine::Initialize: Success (Entities: %zu)",
			m_world.GetComponents<TransformComponent>().size());

		return true;
	}

	void Engine::Impl::InitializeMainThreadAndRegistry()
	{
		ThreadSafety::SetMainThreadId(std::this_thread::get_id());
		LinkComponentRegistry();
		ALICE_LOG_INFO("Engine::Initialize: Begin (EditorMode=%d)", m_editorMode);
	}

	std::filesystem::path Engine::Impl::InitializeResolveExeDir()
	{
		wchar_t pathBuf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
		return std::filesystem::path(pathBuf).parent_path();
	}

	bool Engine::Impl::InitializeConfigureResourceManagers(const std::filesystem::path& exeDir)
	{
		m_resourceManager.Configure(!m_editorMode, exeDir);

		if (m_editorMode)
			ResourceManager::Get().Configure(false, exeDir);

		return true;
	}

	bool Engine::Impl::InitializeValidateGameDataIfNeeded()
	{
		if (m_editorMode) return true;

		if (!m_resourceManager.ValidateGameData())
		{
			MessageBoxW(nullptr,
				L"Critical Error: Game Data is corrupted or missing.\nPlease reinstall the game.",
				L"Integrity Check Failed",
				MB_OK | MB_ICONERROR);
			ALICE_LOG_ERRORF("[Engine] Initialize FAILED: Data integrity check failed.");
			return false;
		}
		return true;
	}

	void Engine::Impl::InitializeLoadPvdSettings(const std::filesystem::path& exeDir)
	{
		LoadPvdSettings(exeDir, m_pvdEnabled, m_pvdHost, m_pvdPort);
		if (m_pvdEnabled)
		{
			ALICE_LOG_INFO("PVD settings loaded from EngineSettings.json: %s:%d",
				m_pvdHost.c_str(), m_pvdPort);
		}
	}

	bool Engine::Impl::InitializePhysicsContext()
	{
		PhysicsModule::ContextInitDesc ctx{};
		ctx.enablePvd = m_pvdEnabled;
		ctx.pvdHost = m_pvdHost.c_str();
		ctx.pvdPort = m_pvdPort;
		ctx.pvdTimeoutMs = 1000;

		if (!m_physics.InitializeContext(ctx))
		{
			const std::string& error = m_physics.GetLastError();
			ALICE_LOG_ERRORF("PhysicsModule::InitializeContext failed: %s", error.c_str());
			return false;
		}

		if (m_pvdEnabled)
		{
			ALICE_LOG_INFO("PVD enabled: %s:%d (connection may fail silently if PVD server is not running)",
				m_pvdHost.c_str(), m_pvdPort);
		}

		return true;
	}

	bool Engine::Impl::InitializeWindowAndInput(Engine& owner, int nCmdShow)
	{
		if (!CreateMainWindow(owner, nCmdShow)) return false;
		m_inputSystem.Initialize(m_hWnd);
		return true;
	}

	bool Engine::Impl::InitializeRenderDevice()
	{
		m_renderDevice = std::make_unique<D3D11RenderDevice>();
		if (!m_renderDevice->Initialize(m_hWnd, m_width, m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: RenderDevice failed.");
			return false;
		}
		return true;
	}

	bool Engine::Impl::InitializeEditorCoreIfNeeded()
	{
		if (!m_editorMode) return true;

		m_editorCore.SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);
		m_editorCore.SetInputSystem(&m_inputSystem);

		if (!m_editorCore.Initialize(m_hWnd, *m_renderDevice))
			return false;

		return true;
	}

	void Engine::Impl::InitializeAudio()
	{
		m_audioSystem.SetResourceManager(&m_resourceManager);
		Sound::Initialize();
	}

	bool Engine::Impl::InitializeRenderSystems()
	{
		m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*m_renderDevice);
		m_forwardRenderSystem->SetResourceManager(&m_resourceManager);
		m_forwardRenderSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		m_attackDriverSystem.SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		if (!m_forwardRenderSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("m_forwardRenderSystem->Initialize: fail...");
			return false;
		}

		m_deferredRenderSystem = std::make_unique<DeferredRenderSystem>(*m_renderDevice);
		m_deferredRenderSystem->SetResourceManager(&m_resourceManager);
		m_deferredRenderSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		if (!m_deferredRenderSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("m_deferredRenderSystem->Initialize: fail...");
			return false;
		}

		m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*m_renderDevice);
		if (!m_debugDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_debugDrawSystem->Initialize(): fail...");
			return false;
		}

		m_gizmoDrawSystem = std::make_unique<DebugDrawSystem>(*m_renderDevice);
		if (!m_gizmoDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_gizmoDrawSystem->Initialize(): fail...");
			return false;
		}

		m_effectSystem = std::make_unique<EffectSystem>(*m_renderDevice);
		if (!m_effectSystem->Initialize())
		{
			ALICE_LOG_ERRORF("m_effectSystem->Initialize(): fail...");
			return false;
		}

		m_trailRenderSystem = std::make_unique<TrailEffectRenderSystem>(*m_renderDevice);
		m_trailRenderSystem->SetResourceManager(&m_resourceManager);
		if (!m_trailRenderSystem->Initialize()) return false;

		if (m_deferredRenderSystem && m_trailRenderSystem)
			m_deferredRenderSystem->SetSwordRenderSystem(m_trailRenderSystem.get());

		return true;
	}

	bool Engine::Impl::InitializeUI()
	{
		auto* device = m_renderDevice->GetDevice();
		auto* context = m_renderDevice->GetImmediateContext();
		if (!device || !context)
		{
			ALICE_LOG_ERRORF("[Debug] Device or Context is NULL inside UI Block!");
			return false;
		}

		m_uiWorld.Initalize(device, context, m_width, m_height, m_inputSystem);

		if (!m_aliceUIRenderer.Initialize(device, context, &m_resourceManager))
			ALICE_LOG_ERRORF("[AliceUI] UIRenderer Initialize failed.");

		if (m_forwardRenderSystem)  m_forwardRenderSystem->SetUIRenderer(&m_aliceUIRenderer);
		if (m_deferredRenderSystem) m_deferredRenderSystem->SetUIRenderer(&m_aliceUIRenderer);
		if (m_editorMode)          m_editorCore.SetAliceUIRenderer(&m_aliceUIRenderer);

		return true;
	}

	bool Engine::Impl::InitializeComputeEffectSystem()
	{
		m_computeEffectSystem = std::make_unique<ComputeEffectSystem>(*m_renderDevice);
		if (!m_computeEffectSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("[Debug] ComputeEffectSystem Init Failed!");
			return false;
		}
		return true;
	}

	void Engine::Impl::InitializeCameraAndScriptHotReload()
	{
		m_cameraPosition = { 0.0f, 2.0f, -5.0f };
		m_camera.SetLookAt(m_cameraPosition, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		m_camera.SetPerspective(DirectX::XM_PIDIV4,
			static_cast<float>(m_width) / m_height, 0.1f, 5000.0f);

		ScriptHotReload_Load();
	}

	bool Engine::Impl::InitializeScene(const std::filesystem::path& exeDir)
	{
		m_resourceManager.Clear();
		m_sceneManager = std::make_unique<SceneManager>(m_world, m_resourceManager);

		bool isSceneLoaded = false;

		if (!m_editorMode)
		{
			isSceneLoaded = LoadStartupSceneFromBuildSettings(
				m_world, m_resourceManager, exeDir, &m_uiWorld);
		}

		if (!isSceneLoaded)
		{
			m_sceneManager->SwitchToImmediate("SampleScene");
			ALICE_LOG_INFO("Engine::Initialize: Loaded SampleScene (Fallback or Editor).");
		}

		return true;
	}

	bool Engine::Impl::InitializePhysicsSystemAndWorldCallbacks()
	{
		m_physicsSystem = std::make_unique<PhysicsSystem>(m_world);
		m_physicsSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

		m_world.SetOnBeforeClearCallback([this]() {
			if (m_physicsSystem)
			{
				if (auto pwShared = m_world.GetPhysicsWorldShared())
					pwShared->Flush();

				m_physicsSystem->SetPhysicsWorld(nullptr);
			}

			m_physAccum = 0.0f;
			m_physicsEventQueue.clear();
		});

		RefreshPhysicsForCurrentWorld();
		return true;
	}

	void Engine::Impl::InitializePostLoadBindings(Engine& owner)
	{
		EnsureSkinnedMeshesRegisteredForWorld();

		m_scriptSystem.SetServices(&m_inputSystem, m_sceneManager.get(),
			&m_resourceManager, &m_skinnedMeshRegistry);

		m_scriptSystem.onAfterSceneLoaded.BindObject(&owner, &Engine::EnsureSkinnedMeshesRegisteredForWorld);
		m_scriptSystem.onTrimVideoMemory.BindObject(&owner, &Engine::TrimVideoMemory);
		m_scriptSystem.onAfterSceneLoaded.BindObject(&owner, &Engine::RefreshPhysicsForCurrentWorld);
	}

	void Engine::Impl::InitializeEnsureUIResourcesAfterSceneLoad()
	{
		m_uiWorld.EnsureAllUIResources();
	}


	void Engine::Impl::SavePvdSettings(const std::filesystem::path& exeDir)
	{
		SavePvdSettingsFile(exeDir, m_pvdEnabled, m_pvdHost, m_pvdPort);
	}
}
