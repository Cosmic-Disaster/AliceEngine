#pragma once

#include <string>
#include <rttr/type.h>
#include <rttr/instance.h>
#include <rttr/variant.h>
#include <rttr/registration.h>

#include "Core/Entity.h"
#include "Core/World.h"
#include "AliceUI/UIWidgetComponent.h"
#include "AliceUI/UITransformComponent.h"
#include "AliceUI/UIImageComponent.h"
#include "AliceUI/UITextComponent.h"
#include "AliceUI/UIButtonComponent.h"
#include "AliceUI/UIGaugeComponent.h"
#include "AliceUI/UIEffectComponent.h"
#include "AliceUI/UIAnimationComponent.h"
#include "AliceUI/UIShakeComponent.h"
#include "AliceUI/UIHover3DComponent.h"
#include "AliceUI/UIVitalComponent.h"

#define UI_META_BIND_WIDGET "BindWidget"
#define UI_META_OPTIONAL    "Optional"
#define UI_META_WIDGET_NAME "WidgetName"

#define ALICE_BIND_WIDGET(Type, Name) \
private: \
    Type Name = nullptr; \
private: \
    struct Reflector_BindWidget_##Name { \
        Reflector_BindWidget_##Name() { \
            rttr::registration::class_<ThisType>(ThisType::_Refl_ClassName) \
                .property(#Name, &ThisType::Name) \
                ( rttr::metadata(UI_META_BIND_WIDGET, true), \
                  rttr::metadata(UI_META_OPTIONAL, false), \
                  rttr::metadata(UI_META_WIDGET_NAME, std::string(#Name)) ); \
        } \
    }; \
    inline static Reflector_BindWidget_##Name _reg_bindwidget_##Name;

#define ALICE_BIND_WIDGET_NAMED(Type, Name, WidgetNameStr) \
private: \
    Type Name = nullptr; \
private: \
    struct Reflector_BindWidget_##Name { \
        Reflector_BindWidget_##Name() { \
            rttr::registration::class_<ThisType>(ThisType::_Refl_ClassName) \
                .property(#Name, &ThisType::Name) \
                ( rttr::metadata(UI_META_BIND_WIDGET, true), \
                  rttr::metadata(UI_META_OPTIONAL, false), \
                  rttr::metadata(UI_META_WIDGET_NAME, std::string(WidgetNameStr)) ); \
        } \
    }; \
    inline static Reflector_BindWidget_##Name _reg_bindwidget_##Name;

#define ALICE_BIND_WIDGET_OPTIONAL(Type, Name) \
private: \
    Type Name = nullptr; \
private: \
    struct Reflector_BindWidget_##Name { \
        Reflector_BindWidget_##Name() { \
            rttr::registration::class_<ThisType>(ThisType::_Refl_ClassName) \
                .property(#Name, &ThisType::Name) \
                ( rttr::metadata(UI_META_BIND_WIDGET, true), \
                  rttr::metadata(UI_META_OPTIONAL, true), \
                  rttr::metadata(UI_META_WIDGET_NAME, std::string(#Name)) ); \
        } \
    }; \
    inline static Reflector_BindWidget_##Name _reg_bindwidget_##Name;

#define ALICE_BIND_WIDGET_OPTIONAL_NAMED(Type, Name, WidgetNameStr) \
private: \
    Type Name = nullptr; \
private: \
    struct Reflector_BindWidget_##Name { \
        Reflector_BindWidget_##Name() { \
            rttr::registration::class_<ThisType>(ThisType::_Refl_ClassName) \
                .property(#Name, &ThisType::Name) \
                ( rttr::metadata(UI_META_BIND_WIDGET, true), \
                  rttr::metadata(UI_META_OPTIONAL, true), \
                  rttr::metadata(UI_META_WIDGET_NAME, std::string(WidgetNameStr)) ); \
        } \
    }; \
    inline static Reflector_BindWidget_##Name _reg_bindwidget_##Name;

namespace Alice
{
	namespace AliceUI
	{
		inline bool IsBindWidgetProperty(const rttr::property& prop)
		{
			auto v = prop.get_metadata(UI_META_BIND_WIDGET);
			return v.is_valid() && v.to_bool();
		}

		inline bool IsOptionalBind(const rttr::property& prop)
		{
			auto v = prop.get_metadata(UI_META_OPTIONAL);
			return v.is_valid() && v.to_bool();
		}

		inline std::string GetWidgetBindName(const rttr::property& prop)
		{
			auto v = prop.get_metadata(UI_META_WIDGET_NAME);
			if (v.is_valid()) return v.to_string();
			return prop.get_name().to_string();
		}

		inline std::string GetWidgetNameForEntity(World& world, EntityId id)
		{
			if (const auto* widget = world.GetComponent<UIWidgetComponent>(id))
			{
				if (!widget->widgetName.empty())
					return widget->widgetName;
			}
			return world.GetEntityName(id);
		}

		inline EntityId FindWidgetByName(World& world, EntityId root, const std::string& name)
		{
			if (root == InvalidEntityId)
				return InvalidEntityId;

			const std::string widgetName = GetWidgetNameForEntity(world, root);
			if (!widgetName.empty() && widgetName == name)
				return root;

			for (EntityId child : world.GetChildren(root))
			{
				EntityId found = FindWidgetByName(world, child, name);
				if (found != InvalidEntityId)
					return found;
			}

			// 루트/자식 트리에 없으면, 전체 UIWidgetComponent에서 한 번 더 검색 (부모 관계가 없을 때 대비)
			for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
			{
				const std::string widgetName = GetWidgetNameForEntity(world, id);
				if (!widgetName.empty() && widgetName == name)
					return id;
			}

			return InvalidEntityId;
		}

		struct BindWidgetResult
		{
			int boundCount = 0;
			int missingRequired = 0;
		};

		inline rttr::variant ResolveWidgetPointer(World& world, EntityId id, const rttr::type& rawType)
		{
			if (rawType == rttr::type::get<UIWidgetComponent>())
			{
				auto* comp = world.GetComponent<UIWidgetComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UITransformComponent>())
			{
				auto* comp = world.GetComponent<UITransformComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIImageComponent>())
			{
				auto* comp = world.GetComponent<UIImageComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UITextComponent>())
			{
				auto* comp = world.GetComponent<UITextComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIButtonComponent>())
			{
				auto* comp = world.GetComponent<UIButtonComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIGaugeComponent>())
			{
				auto* comp = world.GetComponent<UIGaugeComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIEffectComponent>())
			{
				auto* comp = world.GetComponent<UIEffectComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIAnimationComponent>())
			{
				auto* comp = world.GetComponent<UIAnimationComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIShakeComponent>())
			{
				auto* comp = world.GetComponent<UIShakeComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIHover3DComponent>())
			{
				auto* comp = world.GetComponent<UIHover3DComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			if (rawType == rttr::type::get<UIVitalComponent>())
			{
				auto* comp = world.GetComponent<UIVitalComponent>(id);
				return comp ? rttr::variant{ comp } : rttr::variant{};
			}
			return rttr::variant{};
		}

		template<typename TOwner>
		BindWidgetResult BindWidgets(TOwner* owner, World& world, EntityId root)
		{
			BindWidgetResult res;
			if (!owner || root == InvalidEntityId)
				return res;

			rttr::instance inst = *owner;
			rttr::type t = inst.get_type();

			for (auto& prop : t.get_properties())
			{
				if (!IsBindWidgetProperty(prop))
					continue;

				const std::string widgetName = GetWidgetBindName(prop);
				EntityId found = FindWidgetByName(world, root, widgetName);
				const bool optional = IsOptionalBind(prop);

				if (found == InvalidEntityId)
				{
					if (!optional)
						res.missingRequired++;
					continue;
				}

				const rttr::type propType = prop.get_type();
				if (propType == rttr::type::get<EntityId>())
				{
					prop.set_value(inst, found);
					res.boundCount++;
					continue;
				}

				if (!propType.is_pointer())
					continue;

				const rttr::type raw = propType.get_raw_type();
				rttr::variant ptr = ResolveWidgetPointer(world, found, raw);
				if (!ptr.is_valid())
				{
					if (!optional)
						res.missingRequired++;
					continue;
				}

				if (prop.set_value(inst, ptr))
				{
					res.boundCount++;
				}
				else if (!optional)
				{
					res.missingRequired++;
				}
			}

			return res;
		}
	}
}
