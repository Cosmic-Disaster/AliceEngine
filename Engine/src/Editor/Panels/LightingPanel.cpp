#include "Editor/Core/EditorCore.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <DirectXMath.h>
#include <ShlObj.h>
#include <Windows.h>

namespace Alice
{
	namespace
	{
		static int CALLBACK BrowseFolderCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
		{
			if (uMsg == BFFM_INITIALIZED && lpData != 0)
			{
				const wchar_t* initialPath = reinterpret_cast<const wchar_t*>(lpData);
				if (initialPath && initialPath[0] != L'\0')
				{
					SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, reinterpret_cast<LPARAM>(initialPath));
				}
			}
			return 0;
		}

		static bool TryReadDdsSize(const std::filesystem::path& absPath, int& outW, int& outH)
		{
			outW = 0;
			outH = 0;

			std::ifstream ifs(absPath, std::ios::binary);
			if (!ifs.is_open())
				return false;

			char magic[4] = {};
			ifs.read(magic, 4);
			if (ifs.gcount() != 4 || std::memcmp(magic, "DDS ", 4) != 0)
				return false;

			std::uint32_t header[5] = {};
			ifs.read(reinterpret_cast<char*>(header), sizeof(header));
			if (ifs.gcount() != sizeof(header))
				return false;

			outH = static_cast<int>(header[2]);
			outW = static_cast<int>(header[3]);
			return (outW > 0 && outH > 0);
		}

		static void ResolveSkyboxBaseAndPrefix(int skyboxChoice,
			const std::string& customDir,
			const std::string& customPrefix,
			std::filesystem::path& outBase,
			std::string& outPrefix)
		{
			outBase.clear();
			outPrefix.clear();

			switch (skyboxChoice)
			{
			case 1: outBase = std::filesystem::path("Resource/Skybox") / "Bridge"; outPrefix = "bridge"; break;
			case 2: outBase = std::filesystem::path("Resource/Skybox") / "Indoor"; outPrefix = "indoor"; break;
			case 3: outBase = std::filesystem::path("Resource/Skybox") / "Sample"; outPrefix = "BakerSample"; break;
			case 4: outBase = std::filesystem::path("Resource/Skybox") / "darkenv"; outPrefix = "darkenvDiffuseHDR"; break;
			case 5:
				if (!customDir.empty() && !customPrefix.empty())
				{
					outBase = std::filesystem::path("Resource/Skybox") / customDir;
					outPrefix = customPrefix;
				}
				break;
			default:
				break;
			}
		}
	}

	void EditorCore::DrawLightingWindow(World& world,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		int& shadingMode,
		bool& useFillLight,
		bool& useForwardRendering,
		LightingParameters& lightingParams,
		int& skyboxChoice,
		std::string& skyboxCustomDir,
		std::string& skyboxCustomPrefix,
		int& skyboxResolution)
	{
		// === Lighting ===
		if (ImGui::Begin("Lighting"))
		{
			bool lightingChanged = false;
			int mode = shadingMode;
			if (ImGui::RadioButton("Lambert", mode == 0))   mode = 0;
			ImGui::SameLine();
			if (ImGui::RadioButton("Phong", mode == 1))     mode = 1;
			ImGui::SameLine();
			if (ImGui::RadioButton("Blinn-Phong", mode == 2)) mode = 2;
			ImGui::SameLine();
			if (ImGui::RadioButton("Toon", mode == 3))      mode = 3;
			ImGui::SameLine();
			if (ImGui::RadioButton("PBR", mode == 4))       mode = 4;
			ImGui::SameLine();
			if (ImGui::RadioButton("ToonPBR", mode == 5))   mode = 5;
			ImGui::SameLine();
			if (ImGui::RadioButton("ToonPBREditable", mode == 7)) mode = 7;
			shadingMode = mode;

			lightingChanged |= Alice::ImGuiCheckbox(L"Fill Light (보조광)", &useFillLight);

			// Forward/Deferred 모드에 따라 조명 파라미터를 각 렌더러에 반영합니다.
			//auto& lighting = useForwardRendering ? forward.GetLightingParameters() : deferred.GetLightingParameters();
			//auto& lighting = forward.GetLightingParameters();
			auto& lighting = lightingParams;

			// PBR 모드일 때 PBR 파라미터 표시
			if (mode == 4 || mode == 5 || mode == 7)
			{
				ImGui::Separator();
				ImGui::Text("PBR Material Parameters");
				lightingChanged |= ImGui::ColorEdit3("Base Color", &lighting.baseColor.x);
				lightingChanged |= ImGui::SliderFloat("Metalness", &lighting.metalness, 0.0f, 1.0f);
				lightingChanged |= ImGui::SliderFloat("Roughness", &lighting.roughness, 0.0f, 1.0f);
				lightingChanged |= ImGui::SliderFloat("Ambient Occlusion", &lighting.ambientOcclusion, 0.0f, 1.0f);
				ImGui::Separator();
			}
			else
			{
				// 레거시 쉐이더 파라미터
				lightingChanged |= ImGui::SliderFloat("Shininess", &lighting.shininess, 2.0f, 128.0f);
				lightingChanged |= ImGui::ColorEdit3("Diffuse Color", &lighting.diffuseColor.x);
				lightingChanged |= ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);
			}

			// 공통 조명 파라미터
			lightingChanged |= Alice::ImGuiSliderFloat(L"Key Intensity (주광)",
				&lighting.keyIntensity,
				0.0f,
				3.0f);
			lightingChanged |= Alice::ImGuiSliderFloat3(L"Key Direction (주광)",
				&lighting.keyDirection.x,
				-1.0f,
				1.0f);

			if (lightingChanged)
			{
				forward.GetLightingParameters() = lighting;
				deferred.GetLightingParameters() = lighting;
			}

			// === Skybox ===
			ImGui::Separator();
			ImGui::TextUnformatted("Skybox");

			static int  lastSkyboxChoice = -1;
			static int  lastSkyboxResolution = -1;
			static bool lastForward = false;

			const char* skyboxItems[] = { "Off", "Bridge", "Indoor", "Baker", "darkenv", "Custom" };

			auto ApplySkybox = [&](auto& renderer)
				{
					const std::string iblSuffix = (skyboxResolution == 1) ? "MDR" : "HDR";
					if (skyboxChoice == 0)
					{
						renderer.SetSkyboxEnabled(false);
						return;
					}

					renderer.SetSkyboxEnabled(true);
					switch (skyboxChoice)
					{
					case 1: renderer.SetIblSet("Bridge", "bridge", iblSuffix);       break;
					case 2: renderer.SetIblSet("Indoor", "indoor", iblSuffix);       break;
					case 3: renderer.SetIblSet("Sample", "BakerSample", iblSuffix);  break;
					case 4: renderer.SetIblSet("darkenv", "darkenvDiffuseHDR", iblSuffix);  break;
					case 5:
						if (!skyboxCustomDir.empty() && !skyboxCustomPrefix.empty())
							renderer.SetIblSet(skyboxCustomDir, skyboxCustomPrefix, iblSuffix);
						else
							renderer.SetSkyboxEnabled(false);
						break;
					default: break;
					}
			};

			auto EditBgIfOff = [&](auto& renderer)
				{
					if (skyboxChoice != 0) return;

					DirectX::XMFLOAT4 bgColor = renderer.GetBackgroundColor();
					if (ImGui::ColorEdit4("Background Color", &bgColor.x))
						renderer.SetBackgroundColor(bgColor);
				};

			bool skyboxChanged = ImGui::Combo("Skybox Choice", &skyboxChoice, skyboxItems, IM_ARRAYSIZE(skyboxItems));

			std::string resLabelHdr = "HDR";
			std::string resLabelMdr = "MDR";
			{
				const auto& rm = ResourceManager::Get();
				std::filesystem::path base;
				std::string prefix;
				ResolveSkyboxBaseAndPrefix(skyboxChoice, skyboxCustomDir, skyboxCustomPrefix, base, prefix);
				if (!base.empty() && !prefix.empty())
				{
					int w = 0, h = 0;
					const std::filesystem::path hdrAbs = rm.Resolve(base / (prefix + "EnvHDR.dds"));
					if (TryReadDdsSize(hdrAbs, w, h))
					{
						resLabelHdr += " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
					}
					w = 0; h = 0;
					const std::filesystem::path mdrAbs = rm.Resolve(base / (prefix + "EnvMDR.dds"));
					if (TryReadDdsSize(mdrAbs, w, h))
					{
						resLabelMdr += " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
					}
				}
			}

			const char* skyboxResItems[] = { resLabelHdr.c_str(), resLabelMdr.c_str() };
			if (skyboxResolution < 0 || skyboxResolution > 1) skyboxResolution = 0;
			bool skyboxResChanged = ImGui::Combo("Skybox Resolution", &skyboxResolution, skyboxResItems, IM_ARRAYSIZE(skyboxResItems));
			bool rendererChanged = (lastForward != useForwardRendering);
			bool browsePressed = false;

			if (skyboxChoice == 5)
			{
				ImGui::SameLine();
				if (ImGui::Button("Browse..."))
				{
					const auto& rm = ResourceManager::Get();
					std::filesystem::path initialPath = rm.Resolve("Resource/Skybox");
					if (!skyboxCustomDir.empty())
					{
						initialPath = rm.Resolve(std::filesystem::path("Resource/Skybox") / skyboxCustomDir);
					}
					if (!std::filesystem::exists(initialPath))
					{
						initialPath = rm.Resolve("Resource/Skybox");
					}
					std::wstring initialPathW = initialPath.wstring();

					BROWSEINFOW bi{};
					bi.hwndOwner = m_hwnd;
					bi.lpszTitle = L"Select Skybox folder (Resource/Skybox/...)";
					bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
					bi.lpfn = BrowseFolderCallbackProc;
					bi.lParam = reinterpret_cast<LPARAM>(initialPathW.c_str());

					PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
					if (pidl)
					{
						wchar_t folderW[MAX_PATH] = {};
						if (SHGetPathFromIDListW(pidl, folderW))
						{
							std::filesystem::path selectedPath = folderW;
							auto logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(selectedPath);
							const std::string logicalStr = logical.generic_string();
							const std::string skyboxRoot = "Resource/Skybox";

							std::string logicalLower = logicalStr;
							for (char& c : logicalLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
							std::string rootLower = skyboxRoot;
							for (char& c : rootLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

							if (logicalLower.find(rootLower) == 0)
							{
								std::filesystem::path rel = std::filesystem::path(logicalStr).lexically_relative(skyboxRoot);
								if (rel == ".")
									skyboxCustomDir.clear();
								else
									skyboxCustomDir = rel.generic_string();

								auto EndsWithInsensitive = [](const std::string& value, const std::string& suffix)
									{
										if (value.size() < suffix.size()) return false;
										const size_t off = value.size() - suffix.size();
										for (size_t i = 0; i < suffix.size(); ++i)
										{
											char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[off + i])));
											char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
											if (a != b) return false;
										}
										return true;
									};

								std::string foundPrefix;
								try
								{
									for (const auto& entry : std::filesystem::directory_iterator(selectedPath))
									{
										if (!entry.is_regular_file())
											continue;
										const std::string name = entry.path().filename().string();
										const std::string suffix = "DiffuseHDR.dds";
										if (EndsWithInsensitive(name, suffix))
										{
											foundPrefix = name.substr(0, name.size() - suffix.size());
											break;
										}
									}
								}
								catch (...)
								{
									ALICE_LOG_WARN("Skybox Browse: failed to scan folder %s", selectedPath.string().c_str());
								}

								if (!foundPrefix.empty())
								{
									skyboxCustomPrefix = foundPrefix;
									browsePressed = true;
								}
								else
								{
									ALICE_LOG_WARN("Skybox Browse: DiffuseHDR.dds not found in %s", selectedPath.string().c_str());
								}
							}
							else
							{
								ALICE_LOG_WARN("Skybox Browse: folder must be inside Resource/Skybox. path=%s", selectedPath.string().c_str());
							}
						}
						CoTaskMemFree(pidl);
					}
				}

				if (!skyboxCustomDir.empty())
					ImGui::Text("Custom Dir: %s", skyboxCustomDir.c_str());
				if (!skyboxCustomPrefix.empty())
					ImGui::Text("Custom Prefix: %s", skyboxCustomPrefix.c_str());
			}

			// 선택 변경 or 렌더러 토글 변경 시 반영 (초기 1회 포함)
			if (skyboxChanged || skyboxResChanged || browsePressed || rendererChanged ||
				lastSkyboxChoice != skyboxChoice || lastSkyboxResolution != skyboxResolution)
			{
				ApplySkybox(forward);
				ApplySkybox(deferred);

				lastSkyboxChoice = skyboxChoice;
				lastSkyboxResolution = skyboxResolution;
				lastForward = useForwardRendering;
			}

			if (useForwardRendering) EditBgIfOff(forward);
			else                     EditBgIfOff(deferred);

		}
		ImGui::End();
	}
}
