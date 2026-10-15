#pragma once
#include "ImGuiCompat.h"
#include <array>

namespace SS
{
	struct FootprintImage
	{
		std::array<ImVec2, 4> points;
		std::array<ImVec2, 4> uv;
	};

	// Unit travel vector; a right-foot image points up. Preserve its square
	// aspect, orient the top toward travel, and mirror UVs only for the left.
	inline FootprintImage FootprintQuad(ImVec2 centre, ImVec2 travel, float size, bool left)
	{
		const ImVec2 across{ -travel.y, travel.x };
		const float half = size * 0.65f;
		const auto corner = [&](float along, float side) {
			return ImVec2{ centre.x + half * (travel.x * along + across.x * side),
				centre.y + half * (travel.y * along + across.y * side) };
		};
		const float u0 = left ? 1.0f : 0.0f, u1 = 1.0f - u0;
		return { { corner(1, -1), corner(1, 1), corner(-1, 1), corner(-1, -1) },
			{ ImVec2{ u0, 0 }, ImVec2{ u1, 0 }, ImVec2{ u1, 1 }, ImVec2{ u0, 1 } } };
	}
}
