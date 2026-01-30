#pragma once

#include <cstdint>
#include <string>

#include "Components/AttackDriverComponent.h"
#include "Core/JsonRttr.h"

namespace Alice
{
    namespace AttackDriverSerialization
    {
        using Json = JsonRttr::json;

        inline std::uint64_t ParseGuidOrZero(const Json& j)
        {
            if (j.is_string())
            {
                try
                {
                    return std::stoull(j.get<std::string>());
                }
                catch (...)
                {
                    return 0;
                }
            }
            if (j.is_number_unsigned() || j.is_number_integer())
                return j.get<std::uint64_t>();
            return 0;
        }

        inline Json AttackDriverClipToJson(const AttackDriverClip& clip)
        {
            Json j = Json::object();
            switch (clip.source)
            {
            case AttackDriverClipSource::BaseA: j["source"] = "BaseA"; break;
            case AttackDriverClipSource::BaseB: j["source"] = "BaseB"; break;
            case AttackDriverClipSource::UpperA: j["source"] = "UpperA"; break;
            case AttackDriverClipSource::UpperB: j["source"] = "UpperB"; break;
            case AttackDriverClipSource::Additive: j["source"] = "Additive"; break;
            default: j["source"] = "Explicit"; break;
            }
            j["clipName"] = clip.clipName;
            j["startTimeSec"] = clip.startTimeSec;
            j["endTimeSec"] = clip.endTimeSec;
            j["enabled"] = clip.enabled;
            return j;
        }

        inline bool JsonToAttackDriverClip(const Json& j, AttackDriverClip& out)
        {
            if (!j.is_object())
                return false;

            if (auto it = j.find("source"); it != j.end())
            {
                if (it->is_string())
                {
                    const std::string s = it->get<std::string>();
                    if (s == "BaseA") out.source = AttackDriverClipSource::BaseA;
                    else if (s == "BaseB") out.source = AttackDriverClipSource::BaseB;
                    else if (s == "UpperA") out.source = AttackDriverClipSource::UpperA;
                    else if (s == "UpperB") out.source = AttackDriverClipSource::UpperB;
                    else if (s == "Additive") out.source = AttackDriverClipSource::Additive;
                    else out.source = AttackDriverClipSource::Explicit;
                }
                else if (it->is_number_integer())
                {
                    out.source = static_cast<AttackDriverClipSource>(it->get<int>());
                }
            }
            if (auto it = j.find("clipName"); it != j.end() && it->is_string())
                out.clipName = it->get<std::string>();
            if (auto it = j.find("startTimeSec"); it != j.end() && it->is_number())
                out.startTimeSec = static_cast<float>(it->get<double>());
            if (auto it = j.find("endTimeSec"); it != j.end() && it->is_number())
                out.endTimeSec = static_cast<float>(it->get<double>());
            if (auto it = j.find("enabled"); it != j.end())
            {
                if (it->is_boolean())
                    out.enabled = it->get<bool>();
                else if (it->is_number())
                    out.enabled = (it->get<double>() != 0.0);
            }

            return true;
        }

        inline Json AttackDriverComponentToJson(const AttackDriverComponent& ad)
        {
            Json j = Json::object();
            j["traceGuid"] = std::to_string(ad.traceGuid);

            Json clips = Json::array();
            for (const auto& clip : ad.clips)
                clips.push_back(AttackDriverClipToJson(clip));
            j["clips"] = std::move(clips);

            return j;
        }

        inline bool JsonToAttackDriverComponent(const Json& j, AttackDriverComponent& ad)
        {
            if (!j.is_object())
                return false;

            if (auto it = j.find("traceGuid"); it != j.end())
                ad.traceGuid = ParseGuidOrZero(*it);

            ad.clips.clear();
            if (auto itClips = j.find("clips"); itClips != j.end())
            {
                if (itClips->is_array())
                {
                    for (const auto& item : *itClips)
                    {
                        if (!item.is_object())
                            continue;
                        AttackDriverClip clip{};
                        if (JsonToAttackDriverClip(item, clip))
                            ad.clips.push_back(std::move(clip));
                    }
                }
                else if (itClips->is_object())
                {
                    AttackDriverClip clip{};
                    if (JsonToAttackDriverClip(*itClips, clip))
                        ad.clips.push_back(std::move(clip));
                }
            }
            else
            {
                // Backward compatibility: legacy single-clip fields
                AttackDriverClip clip{};
                if (auto it = j.find("clipName"); it != j.end() && it->is_string())
                    clip.clipName = it->get<std::string>();
                if (auto it = j.find("startTimeSec"); it != j.end() && it->is_number())
                    clip.startTimeSec = static_cast<float>(it->get<double>());
                if (auto it = j.find("endTimeSec"); it != j.end() && it->is_number())
                    clip.endTimeSec = static_cast<float>(it->get<double>());

                if (!clip.clipName.empty())
                    ad.clips.push_back(std::move(clip));
            }

            return true;
        }
    }
}
