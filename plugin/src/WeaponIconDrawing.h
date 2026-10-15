#pragma once

#include "EquipmentIcons.h"
#include "ImGuiCompat.h"
#include <algorithm>
#include <cmath>

namespace SS
{
		inline void DrawWeaponIcon(ImDrawList* a_draw, WeaponKind a_kind, ImVec2 a_c, float a_size, ImU32 a_colour)
		{
			const float r = a_size * 0.5f;
			const float stroke = std::max(a_size * 0.13f, 1.2f);

			const auto line = [&](float x1, float y1, float x2, float y2, float a_width) {
				ImDrawList_AddLine(a_draw, ImVec2{ a_c.x + x1 * r, a_c.y + y1 * r },
					ImVec2{ a_c.x + x2 * r, a_c.y + y2 * r }, a_colour, a_width);
			};

			switch (a_kind) {
			case WeaponKind::kFists:
				// A closed hand: a solid knuckle with a flat top.
				ImDrawList_AddCircleFilled(a_draw, ImVec2{ a_c.x, a_c.y + r * 0.15f },
					r * 0.55f, a_colour, 12);
				line(-0.55f, -0.30f, 0.55f, -0.30f, stroke);
				break;

			case WeaponKind::kDagger:
			case WeaponKind::kSword:
			case WeaponKind::kGreatsword:
				{
					// A blade on the diagonal; the guard says which weight.
					const float reach = a_kind == WeaponKind::kDagger ? 0.55f
										: a_kind == WeaponKind::kSword ? 0.85f
																	   : 1.0f;
					const float guard = a_kind == WeaponKind::kGreatsword ? 0.55f : 0.38f;
					line(-reach, reach, reach * 0.92f, -reach * 0.92f, stroke);
					// Crossguard, perpendicular to the blade near the grip.
					const float gx = -reach * 0.55f;
					const float gy = reach * 0.55f;
					line(gx - guard * 0.7f, gy - guard * 0.7f, gx + guard * 0.7f, gy + guard * 0.7f,
						stroke);
				}
				break;

			case WeaponKind::kAxe:
			case WeaponKind::kBattleaxe:
				{
					// Haft on the diagonal, a filled fan for the edge - two for
					// the double bit.
					line(-0.8f, 0.9f, 0.55f, -0.65f, stroke);
					const ImVec2 head[3]{
						ImVec2{ a_c.x + 0.25f * r, a_c.y - 0.95f * r },
						ImVec2{ a_c.x + 0.95f * r, a_c.y - 0.25f * r },
						ImVec2{ a_c.x + 0.45f * r, a_c.y - 0.55f * r }
					};
					ImDrawList_AddConvexPolyFilled(a_draw, head, 3, a_colour);
					if (a_kind == WeaponKind::kBattleaxe) {
						const ImVec2 back[3]{
							ImVec2{ a_c.x - 0.15f * r, a_c.y - 0.55f * r },
							ImVec2{ a_c.x + 0.15f * r, a_c.y - 1.0f * r },
							ImVec2{ a_c.x + 0.35f * r, a_c.y - 0.75f * r }
						};
						ImDrawList_AddConvexPolyFilled(a_draw, back, 3, a_colour);
					}
				}
				break;

			case WeaponKind::kMace:
				line(-0.6f, 0.9f, 0.35f, -0.35f, stroke);
				ImDrawList_AddCircleFilled(a_draw, ImVec2{ a_c.x + 0.5f * r, a_c.y - 0.55f * r },
					r * 0.42f, a_colour, 10);
				break;

			case WeaponKind::kBow:
				// The stave as an arc, the string straight across its ends.
				ImDrawList_PathArcTo(a_draw, a_c, r * 0.85f, -0.6f, 2.2f, 14);
				ImDrawList_PathStroke(a_draw, a_colour, 0, stroke);
				line(0.85f * std::cos(-0.6f), 0.85f * std::sin(-0.6f),
					0.85f * std::cos(2.2f), 0.85f * std::sin(2.2f), stroke * 0.7f);
				break;

			case WeaponKind::kCrossbow:
				// A bolt upright through a horizontal stave.
				line(0.0f, 0.95f, 0.0f, -0.85f, stroke);
				ImDrawList_PathArcTo(a_draw, ImVec2{ a_c.x, a_c.y - 0.15f * r }, r * 0.8f,
					3.6f, 5.8f, 12);
				ImDrawList_PathStroke(a_draw, a_colour, 0, stroke);
				break;

			case WeaponKind::kStaff:
				line(-0.1f, 0.95f, 0.25f, -0.55f, stroke);
				ImDrawList_AddCircle(a_draw, ImVec2{ a_c.x + 0.35f * r, a_c.y - 0.75f * r },
					r * 0.28f, a_colour, 10, stroke * 0.8f);
				break;

			case WeaponKind::kSpell:
				// A spark: crossing rays and a bright core.
				line(-0.7f, 0.0f, 0.7f, 0.0f, stroke * 0.8f);
				line(0.0f, -0.7f, 0.0f, 0.7f, stroke * 0.8f);
				line(-0.45f, -0.45f, 0.45f, 0.45f, stroke * 0.6f);
				line(-0.45f, 0.45f, 0.45f, -0.45f, stroke * 0.6f);
				ImDrawList_AddCircleFilled(a_draw, a_c, r * 0.22f, a_colour, 8);
				break;
			case WeaponKind::kShield:
				{
					const ImVec2 shield[]{ { a_c.x - .7f * r, a_c.y - .8f * r },
						{ a_c.x + .7f * r, a_c.y - .8f * r }, { a_c.x + .6f * r, a_c.y + .25f * r },
						{ a_c.x, a_c.y + .9f * r }, { a_c.x - .6f * r, a_c.y + .25f * r } };
					ImDrawList_AddPolyline(a_draw, shield, 5, a_colour, ImDrawFlags_Closed, stroke);
				}
				break;
			case WeaponKind::kTorch:
				line(0, .95f, 0, -.15f, stroke);
				ImDrawList_AddTriangleFilled(a_draw, { a_c.x, a_c.y - r },
					{ a_c.x + .5f * r, a_c.y - .15f * r }, { a_c.x - .5f * r, a_c.y - .15f * r }, a_colour);
				break;

			default:
				break;
			}
		}

}
