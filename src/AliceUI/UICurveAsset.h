#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace Alice
{
    enum class UICurveInterp
    {
        Constant = 0,
        Linear = 1,
        Cubic = 2
    };

    enum class UICurveTangentMode
    {
        Auto = 0,
        User = 1,
        Break = 2
    };

    struct UICurveKey
    {
        float time{ 0.0f };
        float value{ 0.0f };
        float inTangent{ 0.0f };
        float outTangent{ 0.0f };
        UICurveInterp interp{ UICurveInterp::Cubic };
        UICurveTangentMode tangentMode{ UICurveTangentMode::Auto };
    };

    struct UICurveAsset
    {
        std::string name;
        std::vector<UICurveKey> keys;

        void Sort();
        void RecalcAutoTangents();
        float Evaluate(float t) const;
        float GetDuration() const;
    };

    bool LoadUICurveAsset(const std::filesystem::path& path, UICurveAsset& outAsset);
    bool SaveUICurveAsset(const std::filesystem::path& path, const UICurveAsset& asset);
}
