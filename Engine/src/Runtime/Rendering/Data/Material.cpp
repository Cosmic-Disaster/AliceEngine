#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Resources/Serialization/ReflectionSerializer.h"
#include "Runtime/ECS/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <sstream>
#include <filesystem>

#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    namespace
    {
        /// 절대 경로를 논리 경로(Assets/, Resource/, Cooked/)로 변환. .mat 커밋 시 팀킬 방지.
        /// ResourceManager::RootDir()(프로젝트 루트) 우선 사용, 미설정 시 exe 기반 폴백(취약).
        static std::string NormalizePathToLogical(const std::string& path)
        {
            if (path.empty()) return path;
            std::filesystem::path p(path);
            if (!p.is_absolute()) {
                std::string s = p.generic_string();
                if (s.find("Assets/") == 0 || s.find("Resource/") == 0 || s.find("Cooked/") == 0)
                    return s;
                return path;
            }
            std::filesystem::path projectRoot;
            const std::filesystem::path& root = ResourceManager::Get().RootDir();
            if (!root.empty())
                projectRoot = root;
            else {
                wchar_t exeW[MAX_PATH] = {};
                if (GetModuleFileNameW(nullptr, exeW, MAX_PATH) != 0)
                    projectRoot = std::filesystem::path(exeW).parent_path().parent_path().parent_path();
            }
            if (projectRoot.empty()) return path;
            try {
                auto rel = std::filesystem::relative(p, projectRoot);
                if (!rel.empty()) {
                    std::string r = rel.generic_string();
                    if (r.find("Assets/") == 0 || r.find("Resource/") == 0 || r.find("Cooked/") == 0)
                        return r;
                }
            } catch (...) {}
            return path;
        }
    }

    bool MaterialFile::Load(const std::filesystem::path& path, MaterialComponent& outMaterial, ResourceManager* rm)
    {
        // RTTR 기반으로 자동 로드
        bool result = ReflectionSerializer::Load(path, outMaterial);

        // roughness, metalness 클램핑 (RTTR로는 기본값 처리만 하므로 여기서 보정)
        outMaterial.alpha = std::clamp(outMaterial.alpha, 0.0f, 1.0f);
        outMaterial.roughness = std::clamp(outMaterial.roughness, 0.0f, 1.0f);
        outMaterial.metalness = std::clamp(outMaterial.metalness, 0.0f, 1.0f);
        outMaterial.ambientOcclusion = std::clamp(outMaterial.ambientOcclusion, 0.0f, 1.0f);
        outMaterial.envDiffuseStrength = std::max(outMaterial.envDiffuseStrength, 0.0f);
        outMaterial.envSpecularStrength = std::max(outMaterial.envSpecularStrength, 0.0f);
        // 노말맵 강도는 0.0f 이상으로 제한
        outMaterial.normalStrength = std::max(outMaterial.normalStrength, 0.0f);
        // 아웃라인 두께는 음수 방지
        outMaterial.outlineWidth = std::max(outMaterial.outlineWidth, 0.0f);

        ALICE_LOG_INFO("[MaterialFile] Load: \"%s\" color=(%.3f, %.3f, %.3f) alpha=%.3f rough=%.3f metal=%.3f ao=%.3f normalStrength=%.3f | outline=(%.3f, %.3f, %.3f) width=%.3f tex=\"%s\"",
            path.string().c_str(),
            outMaterial.color.x, outMaterial.color.y, outMaterial.color.z,
            outMaterial.alpha,
            outMaterial.roughness,
            outMaterial.metalness,
            outMaterial.ambientOcclusion,
            outMaterial.normalStrength,
            outMaterial.outlineColor.x, outMaterial.outlineColor.y, outMaterial.outlineColor.z,
            outMaterial.outlineWidth,
            outMaterial.albedoTexturePath.c_str());

        return result;
    }

    bool MaterialFile::Save(const std::filesystem::path& path, const MaterialComponent& material)
    {
        // assetPath/albedoTexturePath는 논리 경로만 저장 (절대경로 커밋 시 팀킬 방지)
        MaterialComponent copy = material;
        copy.assetPath = NormalizePathToLogical(copy.assetPath);
        copy.albedoTexturePath = NormalizePathToLogical(copy.albedoTexturePath);
        copy.alpha = std::clamp(copy.alpha, 0.0f, 1.0f);
        copy.roughness = std::clamp(copy.roughness, 0.0f, 1.0f);
        copy.metalness = std::clamp(copy.metalness, 0.0f, 1.0f);
        copy.ambientOcclusion = std::clamp(copy.ambientOcclusion, 0.0f, 1.0f);
        copy.envDiffuseStrength = std::max(copy.envDiffuseStrength, 0.0f);
        copy.envSpecularStrength = std::max(copy.envSpecularStrength, 0.0f);
        copy.normalStrength = std::max(copy.normalStrength, 0.0f);
        copy.outlineWidth = std::max(copy.outlineWidth, 0.0f);

        bool result = ReflectionSerializer::Save(path, copy);

        ALICE_LOG_INFO("[MaterialFile] Save: \"%s\" color=(%.3f, %.3f, %.3f) alpha=%.3f rough=%.3f metal=%.3f ao=%.3f normalStrength=%.3f | outline=(%.3f, %.3f, %.3f) width=%.3f tex=\"%s\"",
            path.string().c_str(),
            copy.color.x, copy.color.y, copy.color.z,
            copy.alpha,
            copy.roughness,
            copy.metalness,
            copy.ambientOcclusion,
            copy.normalStrength,
            copy.outlineColor.x, copy.outlineColor.y, copy.outlineColor.z,
            copy.outlineWidth,
            copy.albedoTexturePath.c_str());

        return result;
    }
}


