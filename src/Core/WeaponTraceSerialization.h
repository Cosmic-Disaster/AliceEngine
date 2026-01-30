#pragma once

#include <cstdint>
#include <string>

#include "Components/WeaponTraceComponent.h"
#include "Core/JsonRttr.h"

namespace Alice
{
    namespace WeaponTraceSerialization
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

        inline Json Float3ToJson(const DirectX::XMFLOAT3& v)
        {
            Json j = Json::object();
            j["x"] = v.x;
            j["y"] = v.y;
            j["z"] = v.z;
            return j;
        }

        inline void JsonToFloat3(const Json& j, DirectX::XMFLOAT3& out)
        {
            if (!j.is_object())
                return;

            if (auto it = j.find("x"); it != j.end() && it->is_number())
                out.x = static_cast<float>(it->get<double>());
            if (auto it = j.find("y"); it != j.end() && it->is_number())
                out.y = static_cast<float>(it->get<double>());
            if (auto it = j.find("z"); it != j.end() && it->is_number())
                out.z = static_cast<float>(it->get<double>());
        }

        inline const char* ShapeTypeToString(WeaponTraceShapeType type)
        {
            switch (type)
            {
            case WeaponTraceShapeType::Sphere:  return "Sphere";
            case WeaponTraceShapeType::Capsule: return "Capsule";
            case WeaponTraceShapeType::Box:     return "Box";
            default:                            return "Sphere";
            }
        }

        inline WeaponTraceShapeType ShapeTypeFromString(const std::string& s)
        {
            if (s == "Sphere")  return WeaponTraceShapeType::Sphere;
            if (s == "Capsule") return WeaponTraceShapeType::Capsule;
            if (s == "Box")     return WeaponTraceShapeType::Box;
            return WeaponTraceShapeType::Sphere;
        }

        inline void JsonToShapeType(const Json& j, WeaponTraceShapeType& out)
        {
            if (j.is_string())
            {
                out = ShapeTypeFromString(j.get<std::string>());
                return;
            }
            if (j.is_number_integer())
            {
                out = static_cast<WeaponTraceShapeType>(j.get<int>());
            }
        }

        inline Json WeaponTraceShapeToJson(const WeaponTraceShape& shape)
        {
            Json j = Json::object();
            j["name"] = shape.name;
            j["enabled"] = shape.enabled;
            j["type"] = ShapeTypeToString(shape.type);
            j["localPos"] = Float3ToJson(shape.localPos);
            j["localRotDeg"] = Float3ToJson(shape.localRotDeg);
            j["radius"] = shape.radius;
            j["capsuleHalfHeight"] = shape.capsuleHalfHeight;
            j["boxHalfExtents"] = Float3ToJson(shape.boxHalfExtents);
            return j;
        }

        inline bool JsonToWeaponTraceShape(const Json& j, WeaponTraceShape& out)
        {
            if (!j.is_object())
                return false;

            if (auto it = j.find("name"); it != j.end() && it->is_string())
                out.name = it->get<std::string>();
            if (auto it = j.find("enabled"); it != j.end())
            {
                if (it->is_boolean())
                    out.enabled = it->get<bool>();
                else if (it->is_number())
                    out.enabled = (it->get<double>() != 0.0);
            }
            if (auto it = j.find("type"); it != j.end())
                JsonToShapeType(*it, out.type);
            if (auto it = j.find("localPos"); it != j.end())
                JsonToFloat3(*it, out.localPos);
            if (auto it = j.find("localRotDeg"); it != j.end())
                JsonToFloat3(*it, out.localRotDeg);
            if (auto it = j.find("radius"); it != j.end() && it->is_number())
                out.radius = static_cast<float>(it->get<double>());
            if (auto it = j.find("capsuleHalfHeight"); it != j.end() && it->is_number())
                out.capsuleHalfHeight = static_cast<float>(it->get<double>());
            if (auto it = j.find("boxHalfExtents"); it != j.end())
                JsonToFloat3(*it, out.boxHalfExtents);

            return true;
        }

        inline Json WeaponTraceComponentToJson(const WeaponTraceComponent& wt)
        {
            Json j = Json::object();
            j["ownerGuid"] = std::to_string(wt.ownerGuid);
            j["ownerNameDebug"] = wt.ownerNameDebug;
            j["traceBasisGuid"] = std::to_string(wt.traceBasisGuid);
            j["active"] = wt.active;
            j["debugDraw"] = wt.debugDraw;
            j["baseDamage"] = wt.baseDamage;
            j["teamId"] = wt.teamId;
            j["attackInstanceId"] = wt.attackInstanceId;
            j["targetLayerBits"] = wt.targetLayerBits;
            j["queryLayerBits"] = wt.queryLayerBits;
            j["subSteps"] = wt.subSteps;

            Json shapes = Json::array();
            for (const auto& shape : wt.shapes)
                shapes.push_back(WeaponTraceShapeToJson(shape));
            j["shapes"] = std::move(shapes);

            return j;
        }

        inline bool JsonToWeaponTraceComponent(const Json& j, WeaponTraceComponent& wt)
        {
            if (!j.is_object())
                return false;

            if (auto it = j.find("ownerGuid"); it != j.end())
                wt.ownerGuid = ParseGuidOrZero(*it);
            if (auto it = j.find("ownerNameDebug"); it != j.end() && it->is_string())
                wt.ownerNameDebug = it->get<std::string>();
            if (auto it = j.find("traceBasisGuid"); it != j.end())
                wt.traceBasisGuid = ParseGuidOrZero(*it);

            if (auto it = j.find("active"); it != j.end())
            {
                if (it->is_boolean())
                    wt.active = it->get<bool>();
                else if (it->is_number())
                    wt.active = (it->get<double>() != 0.0);
            }
            if (auto it = j.find("debugDraw"); it != j.end())
            {
                if (it->is_boolean())
                    wt.debugDraw = it->get<bool>();
                else if (it->is_number())
                    wt.debugDraw = (it->get<double>() != 0.0);
            }
            if (auto it = j.find("baseDamage"); it != j.end() && it->is_number())
                wt.baseDamage = static_cast<float>(it->get<double>());
            if (auto it = j.find("teamId"); it != j.end() && it->is_number())
                wt.teamId = static_cast<std::uint32_t>(it->get<double>());
            if (auto it = j.find("attackInstanceId"); it != j.end() && it->is_number())
                wt.attackInstanceId = static_cast<std::uint32_t>(it->get<double>());
            if (auto it = j.find("targetLayerBits"); it != j.end() && it->is_number())
                wt.targetLayerBits = static_cast<std::uint32_t>(it->get<double>());
            if (auto it = j.find("queryLayerBits"); it != j.end() && it->is_number())
                wt.queryLayerBits = static_cast<std::uint32_t>(it->get<double>());
            if (auto it = j.find("subSteps"); it != j.end() && it->is_number())
                wt.subSteps = static_cast<std::uint32_t>(it->get<double>());

            wt.shapes.clear();
            auto itShapes = j.find("shapes");
            if (itShapes != j.end())
            {
                if (itShapes->is_array())
                {
                    for (const auto& item : *itShapes)
                    {
                        if (!item.is_object())
                            continue;
                        WeaponTraceShape shape{};
                        if (JsonToWeaponTraceShape(item, shape))
                            wt.shapes.push_back(std::move(shape));
                    }
                }
                else if (itShapes->is_object())
                {
                    WeaponTraceShape shape{};
                    if (JsonToWeaponTraceShape(*itShapes, shape))
                        wt.shapes.push_back(std::move(shape));
                }
            }

            return true;
        }
    }
}
