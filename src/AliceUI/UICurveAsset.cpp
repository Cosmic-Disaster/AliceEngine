#include "AliceUI/UICurveAsset.h"

#include <algorithm>
#include <fstream>

#include "json/json.hpp"

namespace Alice
{
    namespace
    {
        float Hermite(float t, float p0, float p1, float m0, float m1)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
        }
    }

    void UICurveAsset::Sort()
    {
        std::sort(keys.begin(), keys.end(), [](const UICurveKey& a, const UICurveKey& b)
        {
            return a.time < b.time;
        });
    }

    float UICurveAsset::GetDuration() const
    {
        if (keys.empty())
            return 0.0f;
        return keys.back().time;
    }

    void UICurveAsset::RecalcAutoTangents()
    {
        if (keys.size() < 2)
            return;

        Sort();

        for (size_t i = 0; i < keys.size(); ++i)
        {
            if (keys[i].tangentMode != UICurveTangentMode::Auto)
                continue;

            float prevTime = keys[i].time;
            float prevVal = keys[i].value;
            float nextTime = keys[i].time;
            float nextVal = keys[i].value;

            if (i > 0)
            {
                prevTime = keys[i - 1].time;
                prevVal = keys[i - 1].value;
            }
            if (i + 1 < keys.size())
            {
                nextTime = keys[i + 1].time;
                nextVal = keys[i + 1].value;
            }

            const float dt = (nextTime - prevTime);
            float slope = 0.0f;
            if (dt != 0.0f)
                slope = (nextVal - prevVal) / dt;

            keys[i].inTangent = slope;
            keys[i].outTangent = slope;
        }
    }

    float UICurveAsset::Evaluate(float t) const
    {
        if (keys.empty())
            return 0.0f;
        if (keys.size() == 1)
            return keys.front().value;

        if (t <= keys.front().time)
            return keys.front().value;
        if (t >= keys.back().time)
            return keys.back().value;

        for (size_t i = 0; i + 1 < keys.size(); ++i)
        {
            const UICurveKey& k0 = keys[i];
            const UICurveKey& k1 = keys[i + 1];
            if (t < k0.time || t > k1.time)
                continue;

            const float dt = k1.time - k0.time;
            if (dt <= 0.0f)
                return k0.value;
            const float u = (t - k0.time) / dt;

            if (k0.interp == UICurveInterp::Constant)
                return k0.value;
            if (k0.interp == UICurveInterp::Linear)
                return k0.value + (k1.value - k0.value) * u;

            const float m0 = k0.outTangent * dt;
            const float m1 = k1.inTangent * dt;
            return Hermite(u, k0.value, k1.value, m0, m1);
        }

        return keys.back().value;
    }

    static void ToJson(const UICurveAsset& asset, nlohmann::json& j)
    {
        j = nlohmann::json::object();
        j["name"] = asset.name;
        j["keys"] = nlohmann::json::array();
        for (const auto& k : asset.keys)
        {
            nlohmann::json jk;
            jk["time"] = k.time;
            jk["value"] = k.value;
            jk["inTangent"] = k.inTangent;
            jk["outTangent"] = k.outTangent;
            jk["interp"] = static_cast<int>(k.interp);
            jk["tangentMode"] = static_cast<int>(k.tangentMode);
            j["keys"].push_back(jk);
        }
    }

    static bool FromJson(const nlohmann::json& j, UICurveAsset& out)
    {
        if (!j.is_object())
            return false;
        out = UICurveAsset{};
        if (j.contains("name") && j["name"].is_string())
            out.name = j["name"].get<std::string>();
        if (!j.contains("keys") || !j["keys"].is_array())
            return true;

        for (const auto& jk : j["keys"])
        {
            if (!jk.is_object())
                continue;
            UICurveKey k{};
            if (jk.contains("time")) k.time = jk["time"].get<float>();
            if (jk.contains("value")) k.value = jk["value"].get<float>();
            if (jk.contains("inTangent")) k.inTangent = jk["inTangent"].get<float>();
            if (jk.contains("outTangent")) k.outTangent = jk["outTangent"].get<float>();
            if (jk.contains("interp")) k.interp = static_cast<UICurveInterp>(jk["interp"].get<int>());
            if (jk.contains("tangentMode")) k.tangentMode = static_cast<UICurveTangentMode>(jk["tangentMode"].get<int>());
            out.keys.push_back(k);
        }
        out.Sort();
        return true;
    }

    bool LoadUICurveAsset(const std::filesystem::path& path, UICurveAsset& outAsset)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open())
            return false;

        nlohmann::json j;
        try
        {
            ifs >> j;
        }
        catch (...)
        {
            return false;
        }

        return FromJson(j, outAsset);
    }

    bool SaveUICurveAsset(const std::filesystem::path& path, const UICurveAsset& asset)
    {
        nlohmann::json j;
        ToJson(asset, j);

        std::ofstream ofs(path);
        if (!ofs.is_open())
            return false;
        try
        {
            ofs << j.dump(4);
        }
        catch (...)
        {
            return false;
        }
        return true;
    }
}
