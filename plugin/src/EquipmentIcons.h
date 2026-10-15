#pragma once

#include "Config.h"
#include <array>

namespace SS
{
	struct HandIcon
	{
		WeaponKind kind{ WeaponKind::kNone };
		std::uint32_t colour{ 0 };
		float fill{ -1.f };
	};

	struct EquipmentIcons
	{
		// Screen order: left hand, right hand. A two-handed item occupies one slot.
		std::array<HandIcon, 2> icons{};
		unsigned count{ 0 };
		[[nodiscard]] float Width(float size) const { return count == 2 ? size * 1.85f : count ? size : 0.f; }
	};

	inline bool IsTwoHandedIcon(WeaponKind kind)
	{
		return kind == WeaponKind::kGreatsword || kind == WeaponKind::kBattleaxe ||
			kind == WeaponKind::kBow || kind == WeaponKind::kCrossbow;
	}

	inline EquipmentIcons ComposeEquipmentIcons(HandIcon left, HandIcon right)
	{
		EquipmentIcons out;
		if (IsTwoHandedIcon(right.kind)) { out.icons[0] = right; out.count = 1; }
		else if (IsTwoHandedIcon(left.kind)) { out.icons[0] = left; out.count = 1; }
		else {
			for (const auto& hand : { left, right }) {
				if (hand.kind != WeaponKind::kNone) { out.icons[out.count++] = hand; }
			}
			if (!out.count) { out.icons[0].kind = WeaponKind::kFists; out.count = 1; }
		}
		return out;
	}

	template <class Draw>
	void ForEachEquipmentIcon(const EquipmentIcons& equipment, float x, float y, float size, Draw&& draw)
	{
		for (unsigned i = 0; i < equipment.count; ++i) {
			const float offset = equipment.count == 2 ? (i == 0 ? -.5f : .5f) * size : 0.f;
			draw(equipment.icons[i], x + offset, y, equipment.count == 2 ? size * .85f : size);
		}
	}
}
