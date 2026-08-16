#include "Labels.h"

#include "Config.h"
#include "GameMenus.h"
#include "Locale.h"
#include "PostFX.h"
#include "Timing.h"

#define CIMGUI_NO_EXPORT
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

namespace SS
{
	namespace
	{
		using RenderFunction = void(__stdcall*)();
		using RegisterHudElementFn = void*(*)(RenderFunction);

		// Projects a world point onto the screen. Returns false when the point is
		// behind the camera or the camera is not available.
		bool Project(const RE::NiPoint3& a_world, float a_width, float a_height, ImVec2& a_out,
			float* a_depth = nullptr)
		{
			auto* camera = RE::Main::WorldRootCamera();
			if (!camera) {
				return false;
			}

			float x{}, y{}, z{};
			RE::NiCamera::WorldPtToScreenPt3(
				camera->GetRuntimeData().worldToCam,
				camera->GetRuntimeData2().port,
				a_world, x, y, z, 1e-5f);

			if (z <= 0.0f) {
				return false;  // behind us
			}

			if (a_depth) {
				*a_depth = z;
			}

			// The engine hands back normalised coordinates with the origin in the
			// bottom left; ImGui wants pixels from the top left.
			a_out = ImVec2{ x * a_width, (1.0f - y) * a_height };
			return true;
		}

		[[nodiscard]] ImU32 PackColour(std::uint32_t a_rgb, float a_alpha)
		{
			const auto a = static_cast<ImU32>(std::clamp(a_alpha, 0.0f, 1.0f) * 255.0f);
			return (a << 24) | ((a_rgb & 0xFF) << 16) | (((a_rgb >> 8) & 0xFF) << 8) | ((a_rgb >> 16) & 0xFF);
		}

		// Disposition markers, drawn as geometry rather than glyphs. A font can
		// be missing an icon - that is the whole reason the name tags said "???"
		// for two versions - and these have to survive whatever font the menu
		// framework happened to bake.
		//
		//   hostile  filled triangle, point up      - a blade, reads as a threat
		//   neutral  hollow circle                  - present, not your problem
		//   friendly filled diamond                 - distinct at a glance
		//   dead     saltire                        - unmistakably not standing
		// A flame. Twelve points up one side and back down the other, pinched at
		// the tip and belled at the base - enough to read as fire at tag size,
		// and it needs no picture file, so the add-on stays code-only.
		void DrawFlame(ImDrawList* a_draw, ImVec2 a_centre, float a_size, ImU32 a_colour)
		{
			constexpr int kHalf = 9;
			ImVec2        pts[kHalf * 2]{};
			const auto    r = a_size * 0.5f;

			for (int i = 0; i < kHalf; ++i) {
				const auto t = static_cast<float>(i) / static_cast<float>(kHalf - 1);
				// y runs from the tip down to the base
				const auto y = -1.0f + 2.0f * t;
				// width swells low and pinches to nothing at the tip
				const auto w = std::sin(t * 3.14159265f * 0.92f) * (0.30f + 0.62f * t);
				pts[i] = ImVec2{ a_centre.x + w * r, a_centre.y + y * r };
				pts[kHalf * 2 - 1 - i] = ImVec2{ a_centre.x - w * r, a_centre.y + y * r };
			}

			for (const auto& p : pts) {
				ImDrawList_PathLineTo(a_draw, p);
			}
			ImDrawList_PathFillConvex(a_draw, a_colour);
		}

		// One vitals bar, drawn as a sheared quad rather than a rectangle.
		//
		// A rectangle is a progress widget; a slanted, notched, glowing sliver
		// reads as a piece of equipment, which is the whole difference. The
		// shape is built from four corners so the same code draws it lying flat
		// or standing on end, and so the fill can be clipped along the slant
		// instead of straight down.
		//
		//   a_origin  the corner the bar grows from
		//   a_length  along the bar
		//   a_thick   across it
		//   a_shear   how far the far edge leans, in pixels
		//   a_upright false runs left to right, true runs bottom to top
		//   a_cap     how much of the scale is available, 0..1; the span above it
		//             is drawn as a dead zone (a survival mod's lowered ceiling)
		//   a_vital   0 health, 1 magicka, 2 stamina - picks the dead zone's
		//             dressing (frost or smoke); -1 leaves it plain
		//   a_now     seconds, for the dressing's slow animation
		void DrawVitalBar(ImDrawList* a_draw, ImVec2 a_origin, float a_length, float a_thick,
			float a_shear, bool a_upright, float a_value, float a_cap, int a_vital, float a_now,
			std::uint32_t a_colour, std::uint32_t a_frame, float a_alpha, int a_segments, bool a_glow)
		{
			// Corner at distance d along the bar, offset t across it.
			//
			// The lean always goes on the ALONG axis, scaled by how far across
			// we are. Putting it on the across axis instead just makes the bar
			// fatter at one edge - which is what the upright case did until the
			// area of the quad was measured and came out nearly double.
			const auto corner = [&](float d, float t) {
				const float lean = a_shear * (t / std::max(a_thick, 0.001f));
				return a_upright
						   ? ImVec2{ a_origin.x + t, a_origin.y - d + lean }
						   : ImVec2{ a_origin.x + d + lean, a_origin.y + t };
			};

			const float cap = std::clamp(a_cap, 0.0f, 1.0f);
			const float filled = a_length * std::clamp(std::min(a_value, cap), 0.0f, 1.0f);

			const ImVec2 track[4]{ corner(0.0f, 0.0f), corner(a_length, 0.0f),
				corner(a_length, a_thick), corner(0.0f, a_thick) };

			// A wider, fainter copy underneath. Cheap bloom: it survives the
			// desaturation pass because it is drawn on the finished frame.
			if (a_glow && filled > 0.0f) {
				const float g = a_thick * 0.9f;
				const ImVec2 halo[4]{ corner(-g, -g), corner(filled + g, -g),
					corner(filled + g, a_thick + g), corner(-g, a_thick + g) };
				ImDrawList_AddConvexPolyFilled(a_draw, halo, 4, PackColour(a_colour, a_alpha * 0.13f));
			}

			// Empty channel, so a drained bar still says where full would be.
			ImDrawList_AddConvexPolyFilled(a_draw, track, 4, PackColour(0x000000, a_alpha * 0.5f));
			ImDrawList_AddConvexPolyFilled(a_draw, track, 4, PackColour(a_colour, a_alpha * 0.14f));

			// The ceiling a survival mod has taken away. The track keeps its full
			// length - the bar must not quietly rescale - and the unavailable span
			// reads as dead hardware: dimmed, and dressed when styling is on.
			if (cap < 0.995f) {
				const float  from = a_length * cap;
				const ImVec2 dead[4]{ corner(from, 0.0f), corner(a_length, 0.0f),
					corner(a_length, a_thick), corner(from, a_thick) };
				ImDrawList_AddConvexPolyFilled(a_draw, dead, 4, PackColour(a_colour, a_alpha * 0.10f));
				ImDrawList_AddConvexPolyFilled(a_draw, dead, 4, PackColour(0x000000, a_alpha * 0.30f));

				if (a_vital == 0) {
					// Frost over lost health: pale crystalline hatching over the
					// dim red channel, breathing very slowly.
					const float pulse = 0.22f + 0.06f * std::sin(a_now * 0.9f);
					const float gap = std::max(a_thick * 1.1f, 3.0f);
					for (float d = from + gap * 0.5f; d < a_length; d += gap) {
						ImDrawList_AddLine(a_draw, corner(d, 0.0f),
							corner(std::min(d + a_thick * 0.8f, a_length), a_thick),
							PackColour(0xD8ECFF, a_alpha * pulse), std::max(1.0f, a_thick * 0.18f));
					}
				} else if (a_vital > 0) {
					// Smoke over lost magicka and stamina: soft wisps in the bar's
					// own colour drifting through the dead span.
					const float lost = a_length - from;
					const float width = lost * 0.22f;
					if (lost > 2.0f && width > 0.5f) {
						for (int k = 0; k < 3; ++k) {
							const float speed = 4.0f + 2.6f * static_cast<float>(k);
							const float at = from + std::fmod(
								a_now * speed + static_cast<float>(k) * lost * 0.37f,
								std::max(lost - width, 0.001f));
							const float breathe =
								0.10f + 0.05f * std::sin(a_now * 1.7f + static_cast<float>(k) * 2.1f);
							const ImVec2 wisp[4]{ corner(at, 0.0f), corner(at + width, 0.0f),
								corner(at + width, a_thick), corner(at, a_thick) };
							ImDrawList_AddConvexPolyFilled(a_draw, wisp, 4,
								PackColour(a_colour, a_alpha * breathe));
						}
					}
				}
			}

			if (filled > 0.0f) {
				const ImVec2 fill[4]{ corner(0.0f, 0.0f), corner(filled, 0.0f),
					corner(filled, a_thick), corner(0.0f, a_thick) };
				ImDrawList_AddConvexPolyFilled(a_draw, fill, 4, PackColour(a_colour, a_alpha * 0.92f));

				// Bright head at the leading edge - the eye lands on it, and it
				// is what makes the bar look powered rather than painted.
				const float cap = std::max(1.5f, a_thick * 0.34f);
				const ImVec2 head[4]{ corner(std::max(0.0f, filled - cap), 0.0f), corner(filled, 0.0f),
					corner(filled, a_thick), corner(std::max(0.0f, filled - cap), a_thick) };
				ImDrawList_AddConvexPolyFilled(a_draw, head, 4, PackColour(0xFFFFFF, a_alpha * 0.55f));
			}

			// Notches, cut the whole way across on the same slant.
			if (a_segments > 1) {
				for (int i = 1; i < a_segments; ++i) {
					const float d = a_length * static_cast<float>(i) / static_cast<float>(a_segments);
					ImDrawList_AddLine(a_draw, corner(d, 0.0f), corner(d, a_thick),
						PackColour(0x000000, a_alpha * 0.55f), std::max(1.0f, a_thick * 0.14f));
				}
			}

			// Thin frame last, so it sits over the fill and the notches.
			ImDrawList_AddPolyline(a_draw, track, 4, PackColour(a_frame, a_alpha * 0.75f),
				ImDrawFlags_Closed, std::max(1.0f, a_thick * 0.16f));
		}

		void DrawIcon(ImDrawList* a_draw, Disposition a_icon, ImVec2 a_centre, float a_size, ImU32 a_colour)
		{
			const auto r = a_size * 0.5f;
			const auto stroke = std::max(a_size * 0.15f, 1.2f);

			switch (a_icon) {
			case Disposition::kHostile:
				{
					// Filled triangle, point up. Reads as a blade.
					const ImVec2 pts[3]{
						ImVec2{ a_centre.x, a_centre.y - r },
						ImVec2{ a_centre.x + r * 0.92f, a_centre.y + r * 0.75f },
						ImVec2{ a_centre.x - r * 0.92f, a_centre.y + r * 0.75f }
					};
					ImDrawList_AddConvexPolyFilled(a_draw, pts, 3, a_colour);
				}
				break;

			case Disposition::kRival:
				{
					// The same triangle inverted and hollow: the shape of a
					// threat, without the weight of one.
					const ImVec2 pts[3]{
						ImVec2{ a_centre.x, a_centre.y + r },
						ImVec2{ a_centre.x + r * 0.92f, a_centre.y - r * 0.75f },
						ImVec2{ a_centre.x - r * 0.92f, a_centre.y - r * 0.75f }
					};
					ImDrawList_AddPolyline(a_draw, pts, 3, a_colour, ImDrawFlags_Closed, stroke);
				}
				break;

			case Disposition::kNeutral:
				ImDrawList_AddCircle(a_draw, a_centre, r * 0.85f, a_colour, 14, stroke);
				break;

			case Disposition::kFriend:
				{
					const ImVec2 pts[4]{
						ImVec2{ a_centre.x, a_centre.y - r },
						ImVec2{ a_centre.x + r * 0.8f, a_centre.y },
						ImVec2{ a_centre.x, a_centre.y + r },
						ImVec2{ a_centre.x - r * 0.8f, a_centre.y }
					};
					ImDrawList_AddConvexPolyFilled(a_draw, pts, 4, a_colour);
				}
				break;

			case Disposition::kConfidant:
				{
					// Hexagon - more sides than a friend, fewer than a circle.
					ImVec2 pts[6]{};
					for (int i = 0; i < 6; ++i) {
						const auto t = 1.04719755f * static_cast<float>(i) - 1.57079633f;
						pts[i] = ImVec2{ a_centre.x + std::cos(t) * r * 0.92f,
							a_centre.y + std::sin(t) * r * 0.92f };
					}
					ImDrawList_AddConvexPolyFilled(a_draw, pts, 6, a_colour);
				}
				break;

			case Disposition::kAlly:
				{
					// A shield: square shoulders, pointed foot.
					const ImVec2 pts[5]{
						ImVec2{ a_centre.x - r * 0.82f, a_centre.y - r * 0.9f },
						ImVec2{ a_centre.x + r * 0.82f, a_centre.y - r * 0.9f },
						ImVec2{ a_centre.x + r * 0.82f, a_centre.y + r * 0.15f },
						ImVec2{ a_centre.x, a_centre.y + r },
						ImVec2{ a_centre.x - r * 0.82f, a_centre.y + r * 0.15f }
					};
					ImDrawList_AddConvexPolyFilled(a_draw, pts, 5, a_colour);
				}
				break;

			case Disposition::kLover:
				{
					// A heart, as a polygon. Two lobes swept from the top notch
					// down to the point, mirrored - cheaper and crisper at tag
					// size than a bezier, and it needs no font.
					constexpr int kHalf = 12;
					ImVec2        pts[kHalf * 2]{};
					for (int i = 0; i < kHalf; ++i) {
						const auto t = 3.14159265f * static_cast<float>(i) / static_cast<float>(kHalf - 1);
						const auto sin1 = std::sin(t);
						// classic heart curve, normalised to the icon box
						const auto x = 16.0f * sin1 * sin1 * sin1;
						const auto y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) -
						               2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
						pts[i] = ImVec2{ a_centre.x + x * r / 17.0f, a_centre.y - y * r / 15.0f };
						pts[kHalf * 2 - 1 - i] = ImVec2{ a_centre.x - x * r / 17.0f, a_centre.y - y * r / 15.0f };
					}
					// Concave at the notch, so it has to go through the general
					// path filler rather than the convex one.
					for (const auto& p : pts) {
						ImDrawList_PathLineTo(a_draw, p);
					}
					ImDrawList_PathFillConvex(a_draw, a_colour);
				}
				break;

			case Disposition::kSelfMortal:
				{
					// A standing figure: head and shoulders. Reads as "a person,
					// and nothing has happened to them".
					ImDrawList_AddCircleFilled(a_draw,
						ImVec2{ a_centre.x, a_centre.y - r * 0.45f }, r * 0.34f, a_colour, 12);
					ImDrawList_PathArcTo(a_draw,
						ImVec2{ a_centre.x, a_centre.y + r * 0.62f }, r * 0.72f,
						3.14159265f, 6.28318531f, 12);
					ImDrawList_PathFillConvex(a_draw, a_colour);
				}
				break;

			case Disposition::kSelfVampire:
				{
					// Two fangs hanging from a lip line.
					ImDrawList_AddLine(a_draw,
						ImVec2{ a_centre.x - r * 0.85f, a_centre.y - r * 0.45f },
						ImVec2{ a_centre.x + r * 0.85f, a_centre.y - r * 0.45f },
						a_colour, stroke);
					for (const float side : { -1.0f, 1.0f }) {
						const ImVec2 pts[3]{
							ImVec2{ a_centre.x + side * r * 0.62f, a_centre.y - r * 0.45f },
							ImVec2{ a_centre.x + side * r * 0.16f, a_centre.y - r * 0.45f },
							ImVec2{ a_centre.x + side * r * 0.39f, a_centre.y + r * 0.85f }
						};
						ImDrawList_AddConvexPolyFilled(a_draw, pts, 3, a_colour);
					}
				}
				break;

			case Disposition::kSelfWerewolf:
				{
					// Three claw marks, raked. Tapered, so they read as cuts
					// rather than as three tally strokes.
					const auto w = std::max(a_size * 0.16f, 1.4f);
					for (int i = 0; i < 3; ++i) {
						const auto x = a_centre.x + (static_cast<float>(i) - 1.0f) * r * 0.62f;
						ImDrawList_AddLine(a_draw,
							ImVec2{ x - r * 0.26f, a_centre.y - r * 0.9f },
							ImVec2{ x + r * 0.26f, a_centre.y + r * 0.9f },
							a_colour, w);
					}
				}
				break;

			case Disposition::kDead:
				{
					const auto d = r * 0.72f;
					const auto w = std::max(a_size * 0.18f, 1.5f);
					ImDrawList_AddLine(a_draw,
						ImVec2{ a_centre.x - d, a_centre.y - d }, ImVec2{ a_centre.x + d, a_centre.y + d },
						a_colour, w);
					ImDrawList_AddLine(a_draw,
						ImVec2{ a_centre.x + d, a_centre.y - d }, ImVec2{ a_centre.x - d, a_centre.y + d },
						a_colour, w);
				}
				break;

			default:
				break;
			}
		}

		// A five pointed star for favourites. Concave, so it goes through the
		// path filler rather than the convex one.
		void DrawStar(ImDrawList* a_draw, ImVec2 a_centre, float a_size, ImU32 a_colour)
		{
			constexpr float kTwoPi = 6.28318530718f;
			const auto      outer = a_size * 0.5f;
			const auto      inner = outer * 0.42f;

			for (int i = 0; i < 10; ++i) {
				const auto radius = (i % 2 == 0) ? outer : inner;
				const auto theta = kTwoPi * static_cast<float>(i) / 10.0f - 1.57079633f;
				ImDrawList_PathLineTo(a_draw,
					ImVec2{ a_centre.x + std::cos(theta) * radius, a_centre.y + std::sin(theta) * radius });
			}
			ImDrawList_PathFillConvex(a_draw, a_colour);
		}

		Labels* g_instance{ nullptr };

		void __stdcall RenderThunk();
	}

	Labels* Labels::GetSingleton()
	{
		static Labels singleton;
		g_instance = std::addressof(singleton);
		return g_instance;
	}

	void Labels::Install()
	{
		if (_installed) {
			return;
		}

		auto* module = GetModuleHandleA("SKSEMenuFramework.dll");
		if (!module) {
			logger::warn("labels: SKSEMenuFramework.dll not loaded, name tags disabled");
			return;
		}

		// Signature recovered from the shipped binary: the function reads only
		// rcx, so it takes a single pointer - the render callback - and returns
		// a handle we do not need.
		auto* fn = reinterpret_cast<RegisterHudElementFn>(
			GetProcAddress(module, "RegisterHudElement"));
		if (!fn) {
			logger::warn(
				"labels: this SKSE Menu Framework build has no RegisterHudElement "
				"(needs 3.13 or newer) - name tags disabled");
			return;
		}

		(void)fn(&RenderThunk);
		_installed = true;
		logger::info("labels: HUD element registered");
	}

	void Labels::SetWash(float a_bornAt, float a_diesAt)
	{
		std::scoped_lock guard{ _lock };
		_washBorn = a_bornAt;
		_washDies = a_diesAt;
	}

	void Labels::StopWash()
	{
		std::scoped_lock guard{ _lock };
		_washBorn = 0.0f;
		_washDies = 0.0f;
	}

	void Labels::SetSelfHud(const float (&a_vitals)[3], const float (&a_caps)[3], float a_changedAt)
	{
		std::scoped_lock guard{ _lock };
		for (int i = 0; i < 3; ++i) {
			_selfHud[i] = a_vitals[i];
			_selfHudCap[i] = a_caps[i];
		}
		_selfHudAt = a_changedAt;
	}

	// The corner readout: the same bars as the tag, pinned to the screen.
	//
	// Its own draw rather than a fake tag, because a tag needs somewhere in the
	// world to hang from and in first person the player is inside the camera.
	// Caller holds the lock.
	void Labels::DrawSelfHud(void* a_drawList, float a_width, float a_height, float a_now)
	{
		const auto* settings = Settings::GetSingleton();
		if (settings->selfHudCorner == Corner::kOff) {
			return;
		}

		// How visible the whole thing is, before any per-bar rule.
		float alpha = 1.0f;
		if (settings->selfHudShow == ShowWhen::kOnChange) {
			const float age = a_now - _selfHudAt;
			if (age > settings->selfHudLinger) {
				return;
			}
			const float fade = settings->selfHudFade;
			if (fade > 0.0f && age > settings->selfHudLinger - fade) {
				alpha = std::clamp((settings->selfHudLinger - age) / fade, 0.0f, 1.0f);
			}
		}
		if (alpha <= 0.01f) {
			return;
		}

		const std::uint32_t colours[3]{ settings->selfHealthColour,
			settings->selfMagickaColour, settings->selfStaminaColour };

		int rows[3]{};
		int shown = 0;
		for (int i = 0; i < 3; ++i) {
			const float v = _selfHud[i];
			if (v < 0.0f) {
				continue;
			}
			// "Only what is below maximum" applies per bar, so a full stamina
			// bar drops out while a hurt health bar stays.
			if (settings->selfHudShow == ShowWhen::kNotFull && v >= 0.999f) {
				continue;
			}
			rows[shown++] = i;
		}
		if (shown == 0) {
			return;
		}

		const float scale = settings->selfHudScale;
		const float thick = std::max(1.0f, settings->selfBarHeight * scale);
		const float span = settings->selfBarWidth * scale;
		const float shear = settings->selfBarShear * thick;
		const float step = thick + thick * 0.75f;

		const bool right = settings->selfHudCorner == Corner::kTopRight ||
		                   settings->selfHudCorner == Corner::kBottomRight;
		const bool bottom = settings->selfHudCorner == Corner::kBottomLeft ||
		                    settings->selfHudCorner == Corner::kBottomRight;

		auto* draw = static_cast<ImDrawList*>(a_drawList);
		for (int r = 0; r < shown; ++r) {
			const int   i = rows[r];
			const float away = static_cast<float>(r) * settings->selfBarPerspective;
			const float length = span * (1.0f - away);
			const float inset = (span - length) * 0.5f;

			// Stacks downward from a top corner and upward from a bottom one,
			// so the block always grows into the screen rather than off it.
			const float x = right ? a_width - settings->selfHudX - span + inset
			                      : settings->selfHudX + inset;
			const float y = bottom
			                    ? a_height - settings->selfHudY - step * static_cast<float>(r) - thick
			                    : settings->selfHudY + step * static_cast<float>(r);

			DrawVitalBar(draw, ImVec2{ x, y }, length, thick, shear, false, _selfHud[i],
				settings->barsLostMax ? _selfHudCap[i] : 1.0f,
				settings->barsLostFx ? i : -1, a_now,
				colours[i], settings->selfBarFrameColour, alpha,
				static_cast<int>(settings->selfBarSegments), settings->selfBarGlow);
		}
	}

	void Labels::SetRing(Ring a_ring)
	{
		std::scoped_lock guard{ _lock };
		_ring = std::move(a_ring);
	}

	void Labels::StopRing()
	{
		std::scoped_lock guard{ _lock };
		_ring = Ring{};
	}

	void Labels::SubmitPostFX(void* a_drawList, float a_now)
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->postProcess || _washDies <= _washBorn) {
			return;
		}
		if (a_now < _washBorn || a_now > _washDies) {
			return;
		}

		// Deliberately not gated on the overlay setting the way DrawWash is.
		// The shader does the desaturation as well as the vignette, and someone
		// who turned the flat overlay off still wants the world to go grey.
		const auto ramp = std::clamp(
			std::min({ (a_now - _washBorn) / 0.25f, (_washDies - a_now) / 1.0f, 1.0f }), 0.0f, 1.0f);

		PostFX::Params params{};

		if (settings->tintEnabled) {
			params.saturation = settings->tintSaturation;
			params.brightness = settings->tintBrightness;
			params.contrast = settings->tintContrast;
			params.strength = ramp * std::clamp(settings->tintStrength, 0.0f, 1.0f);

			const auto cast = settings->tintCastColour;
			params.tintR = static_cast<float>((cast >> 16) & 0xFF) / 255.0f;
			params.tintG = static_cast<float>((cast >> 8) & 0xFF) / 255.0f;
			params.tintB = static_cast<float>(cast & 0xFF) / 255.0f;
			params.tintAmount = settings->tintCastAmount;
		}

		if (settings->washEnabled) {
			const auto wash = settings->washColour;
			params.washR = static_cast<float>((wash >> 16) & 0xFF) / 255.0f;
			params.washG = static_cast<float>((wash >> 8) & 0xFF) / 255.0f;
			params.washB = static_cast<float>(wash & 0xFF) / 255.0f;
			params.vignetteInner = 0.30f;
			params.vignetteOuter = 1.05f;
			params.vignetteStrength = ramp * settings->washStrength;
		}

		PostFX::GetSingleton()->Submit(a_drawList, params);
	}

	void Labels::DrawWash(void* a_drawList, float a_width, float a_height, float a_now)
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->washEnabled || _washDies <= _washBorn) {
			return;
		}
		if (a_now < _washBorn || a_now > _washDies) {
			return;
		}

		// Ease in over a quarter second, out over the last second, so the wash
		// arrives with the wave rather than snapping on.
		const auto since = a_now - _washBorn;
		const auto until = _washDies - a_now;
		const auto ramp = std::min({ since / 0.25f, until / 1.0f, 1.0f });
		const auto strength = settings->washStrength * std::max(ramp, 0.0f);
		if (strength <= 0.01f) {
			return;
		}

		// The shader pass does the vignette properly when it is on. Only stand
		// the quads down once a pass has actually landed, though - if the shader
		// route failed we still want a vignette rather than nothing.
		if (settings->postProcess && PostFX::GetSingleton()->Working()) {
			return;
		}

		auto* draw = static_cast<ImDrawList*>(a_drawList);

		// An even sheet of colour over the whole frame is what made this look
		// flat: every pixel moves by the same amount, so nothing reads as depth.
		// Most of the strength goes into the vignette instead, and only a little
		// stays flat to hold the colour together.
		const auto flat = strength * std::clamp(settings->washFlat, 0.0f, 1.0f);
		if (flat > 0.004f) {
			ImDrawList_AddRectFilled(draw,
				ImVec2{ 0.0f, 0.0f }, ImVec2{ a_width, a_height },
				PackColour(settings->washColour, flat), 0.0f, 0);
		}

		const auto edge = strength - flat;
		if (edge <= 0.004f) {
			return;
		}

		// Four gradient bands, opaque at the screen edge and transparent inward.
		// Where two overlap - the corners - they compound, which is exactly the
		// falloff a vignette wants, and it costs four quads instead of a mesh.
		const auto opaque = PackColour(settings->washColour, edge);
		const auto clear = PackColour(settings->washColour, 0.0f);
		const auto bandY = a_height * 0.42f;
		const auto bandX = a_width * 0.30f;

		// top
		ImDrawList_AddRectFilledMultiColor(draw,
			ImVec2{ 0.0f, 0.0f }, ImVec2{ a_width, bandY },
			opaque, opaque, clear, clear);
		// bottom
		ImDrawList_AddRectFilledMultiColor(draw,
			ImVec2{ 0.0f, a_height - bandY }, ImVec2{ a_width, a_height },
			clear, clear, opaque, opaque);
		// left
		ImDrawList_AddRectFilledMultiColor(draw,
			ImVec2{ 0.0f, 0.0f }, ImVec2{ bandX, a_height },
			opaque, clear, clear, opaque);
		// right
		ImDrawList_AddRectFilledMultiColor(draw,
			ImVec2{ a_width - bandX, 0.0f }, ImVec2{ a_width, a_height },
			clear, opaque, opaque, clear);
	}

	void Labels::DrawRing(void* a_drawList, float a_width, float a_height, float a_now)
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->ringEnabled || !_ring.Valid()) {
			return;
		}

		auto* draw = static_cast<ImDrawList*>(a_drawList);

		const auto  span = _ring.endAt - _ring.startAt;
		const auto  reach = _ring.maxRadius - _ring.minRadius;
		const auto  angles = static_cast<int>(_ring.angles);
		const auto  steps = static_cast<int>(_ring.steps);
		const float twoPi = 6.28318530718f;

		// Interpolate the sampled height field at an arbitrary radius.
		const auto heightAt = [&](int a_angle, float a_radius) {
			const auto f = std::clamp((a_radius - _ring.minRadius) / reach, 0.0f, 1.0f) *
			               static_cast<float>(steps - 1);
			const auto i = std::clamp(static_cast<int>(f), 0, steps - 2);
			const auto frac = f - static_cast<float>(i);
			const auto base = static_cast<std::size_t>(a_angle) * steps;
			return _ring.heights[base + i] * (1.0f - frac) + _ring.heights[base + i + 1] * frac;
		};

		std::vector<ImVec2> run;
		run.reserve(static_cast<std::size_t>(angles) + 1);

		const auto echoes = static_cast<int>(std::max<std::uint32_t>(settings->ringEchoes, 1));

		for (int echo = 0; echo < echoes; ++echo) {
			const auto t = (a_now - _ring.startAt - static_cast<float>(echo) * settings->ringEchoGap) / span;
			if (t <= 0.0f || t >= 1.0f) {
				continue;
			}

			const auto radius = _ring.minRadius + reach * t;

			// The leading edge is the bright one; the echoes behind it are
			// dimmer, and the whole thing thins out as it gets further away.
			const auto distanceFade = 1.0f - t * 0.55f;
			const auto echoFade = echo == 0 ? 1.0f : 0.42f / static_cast<float>(echo);
			// Match the wave's own ends so it does not pop in or out.
			const auto ends = std::min({ t / 0.06f, (1.0f - t) / 0.20f, 1.0f });
			const auto alpha = std::clamp(distanceFade * echoFade * ends, 0.0f, 1.0f);
			if (alpha <= 0.02f) {
				continue;
			}

			run.clear();
			bool closed = true;

			// One extra step wraps the loop back onto its first point.
			for (int i = 0; i <= angles; ++i) {
				const auto index = i % angles;
				const auto theta = twoPi * static_cast<float>(index) / static_cast<float>(angles);

				const RE::NiPoint3 world{
					_ring.centre.x + std::cos(theta) * radius,
					_ring.centre.y + std::sin(theta) * radius,
					heightAt(index, radius) + settings->ringHeight
				};

				ImVec2 at{};
				if (Project(world, a_width, a_height, at) &&
					std::isfinite(at.x) && std::isfinite(at.y) &&
					std::abs(at.x) < a_width * 8.0f && std::abs(at.y) < a_height * 8.0f) {
					if (i < angles) {
						run.push_back(at);
					} else if (closed) {
						break;  // the whole circle is on screen, let ImGui close it
					} else {
						run.push_back(at);
					}
					continue;
				}

				// The arc went behind the camera. Flush what we have and start
				// a fresh open run, otherwise ImGui draws a chord straight
				// across the screen between the two ends.
				closed = false;
				if (run.size() >= 2) {
					if (settings->ringGlow > 0.0f) {
						ImDrawList_AddPolyline(draw, run.data(), static_cast<int>(run.size()),
							PackColour(settings->ringColour, alpha * 0.22f), 0,
							settings->ringThickness + settings->ringGlow);
					}
					ImDrawList_AddPolyline(draw, run.data(), static_cast<int>(run.size()),
						PackColour(settings->ringColour, alpha), 0, settings->ringThickness);
				}
				run.clear();
			}

			if (run.size() >= 2) {
				const ImDrawFlags flags = closed ? ImDrawFlags_Closed : 0;
				if (settings->ringGlow > 0.0f) {
					ImDrawList_AddPolyline(draw, run.data(), static_cast<int>(run.size()),
						PackColour(settings->ringColour, alpha * 0.22f), flags,
						settings->ringThickness + settings->ringGlow);
				}
				ImDrawList_AddPolyline(draw, run.data(), static_cast<int>(run.size()),
					PackColour(settings->ringColour, alpha), flags, settings->ringThickness);
			}
		}
	}

	void Labels::Clear()
	{
		std::scoped_lock guard{ _lock };
		_entries.clear();
	}

	void Labels::Replace(std::vector<Entry> a_entries)
	{
		std::scoped_lock guard{ _lock };
		_entries = std::move(a_entries);
	}

	void Labels::MoveTo(const std::vector<RE::NiPoint3>& a_anchors, const std::vector<bool>& a_speaking,
		const std::vector<std::array<float, 7>>& a_vitals)
	{
		std::scoped_lock guard{ _lock };

		// Positional, and deliberately tolerant: a wave can add a tag between
		// the caller building this list and taking the lock, and the right
		// answer then is to move the ones we have and leave the newcomer where
		// it was put a sixtieth of a second ago.
		const auto count = std::min(_entries.size(), a_anchors.size());
		for (std::size_t i = 0; i < count; ++i) {
			_entries[i].world = a_anchors[i];
			_entries[i].speaking = i < a_speaking.size() && a_speaking[i];
			if (i < a_vitals.size()) {
				_entries[i].vitals[0] = a_vitals[i][0];
				_entries[i].vitals[1] = a_vitals[i][1];
				_entries[i].vitals[2] = a_vitals[i][2];
				_entries[i].vitalsAt = a_vitals[i][3];
				_entries[i].vitalsCap[0] = a_vitals[i][4];
				_entries[i].vitalsCap[1] = a_vitals[i][5];
				_entries[i].vitalsCap[2] = a_vitals[i][6];
			}
		}
	}

	void Labels::Render()
	{
		const auto* settings = Settings::GetSingleton();

		std::vector<Entry> snapshot;
		{
			// The HUD callback runs on the render thread, so take a copy and get
			// off the lock rather than holding it across any drawing.
			std::scoped_lock guard{ _lock };
			snapshot = _entries;
		}

		auto* io = igGetIO();
		if (!io) {
			return;
		}

		const float width = io->DisplaySize.x;
		const float height = io->DisplaySize.y;

		// One line, once: proof that the framework is really calling us and that
		// the display size is sane. If this never appears, the HUD hook is not
		// running and nothing drawn here can ever show.
		if (!_sawFirstFrame) {
			_sawFirstFrame = true;
			logger::info("labels: first HUD frame, display {:.0f}x{:.0f}", width, height);

			// Say plainly whether the font in front of us can draw the game's
			// own item names, because the failure mode otherwise is a screen of
			// question marks with nothing anywhere explaining why. U+4E00 is the
			// first CJK ideograph; if the atlas has it, it has the rest.
			if (auto* font = igGetFont(); font) {
				_hasCJK = ImFont_FindGlyphNoFallback(font, static_cast<ImWchar>(0x4E00)) != nullptr;
				if (_hasCJK) {
					logger::info("labels: font has CJK glyphs, non-Latin item names will render");
				} else {
					logger::warn(
						"labels: the menu framework's font has no CJK glyphs - names in "
						"Chinese, Japanese or Korean will draw as '?'. Fix: put a CJK "
						"font at Data/SKSE/Plugins/fonts/MainFont.ttf (this mod ships "
						"one), set PrimaryFont to it in SKSEMenuFramework.ini, and set "
						"EnableChinese = true there.");
				}
			}
		}

		if (width <= 0.0f || height <= 0.0f) {
			return;
		}

		// Everything below composites onto the finished frame, which puts it on
		// top of any Scaleform menu the game has open - lockpicking, containers,
		// the map. Stand down entirely while a menu is up; the clock is stopped
		// at the same time, so the sweep resumes exactly where it paused.
		if (settings->menuAware && GameMenus::GetSingleton()->Blocking()) {
			if (!_saidSuppressed) {
				_saidSuppressed = true;
				logger::info("labels: standing down, menus open: {}",
					GameMenus::GetSingleton()->Describe());
			}
			return;
		}
		_saidSuppressed = false;

		const auto now = Now();

		// The wash goes on the background list so everything else stays on top
		// of it. The ring is held under the lock rather than copied: it carries
		// a few thousand height samples and the draw is quick.
		if (auto* background = igGetBackgroundDrawList_Nil(); background) {
			std::scoped_lock guard{ _lock };
			// Queued before anything else on the background list, so the shader
			// grades the finished game frame and not our own overlay.
			SubmitPostFX(background, now);
			DrawWash(background, width, height, now);
		}

		auto* draw = igGetForegroundDrawList_Nil();
		if (!draw) {
			return;
		}

		{
			std::scoped_lock guard{ _lock };
			DrawRing(draw, width, height, now);
			// Before the early-out below: the corner readout is the one thing
			// here that is not tied to a sweep, so it has to survive an empty
			// tag list.
			DrawSelfHud(draw, width, height, now);
		}

		if (!settings->labelsEnabled || snapshot.empty()) {
			return;
		}

		// The framework bakes exactly one font size - its FontSizeMedium, 32px
		// out of the box - so tags come out menu-sized unless we ask for the
		// same glyphs at a smaller scale.
		auto*      font = igGetFont();
		const auto menuSize = igGetFontSize();
		const auto fontSize = settings->labelFontSize > 0.0f ? settings->labelFontSize : menuSize;

		const auto fadeIn = std::max(settings->fadeIn, 0.01f);
		const auto fadeOut = std::max(settings->fadeOut, 0.01f);

		// Work out where every tag wants to go before drawing any of them, so
		// the ones nearest the camera can claim their spot first and the rest
		// step out of the way. Sorting by depth is what makes the result stable:
		// without it, whichever tag happened to be first in the list wins, and
		// that order changes as objects light up.
		struct Placement
		{
			const Entry* entry;
			ImVec2       anchor;
			float        depth;
			float        alpha;
		};

		std::vector<Placement> wanted;
		wanted.reserve(snapshot.size());

		for (const auto& entry : snapshot) {
			if (now < entry.bornAt || now > entry.diesAt) {
				continue;
			}

			// Match the highlight's own envelope so a tag appears with the wave
			// and leaves with the glow.
			const auto since = now - entry.bornAt;
			const auto until = entry.diesAt - now;
			const auto alpha = std::min({ since / fadeIn, until / fadeOut, 1.0f });
			if (alpha <= 0.02f) {
				continue;
			}

			// Lifted in world space rather than on screen, so the gap between
			// the tag and the subtitle stays the same however far away the
			// speaker is - a fixed number of pixels would swallow the tag at
			// distance and fling it into the sky up close.
			auto world = entry.world;
			if (entry.speaking && settings->labelSpeakerMode == 2) {
				world.z += settings->labelSpeakerLift;
			}

			ImVec2 at{};
			float  depth = 0.0f;
			if (!Project(world, width, height, at, &depth)) {
				continue;
			}
			if (at.x < 0.0f || at.y < 0.0f || at.x > width || at.y > height) {
				continue;
			}

			wanted.push_back({ std::addressof(entry), at, depth, alpha });
		}

		std::sort(wanted.begin(), wanted.end(), [](const Placement& a_lhs, const Placement& a_rhs) {
			return a_lhs.depth < a_rhs.depth;
		});

		// Rectangles already spoken for this frame.
		struct Box
		{
			float left, top, right, bottom;
		};
		std::vector<Box> taken;
		taken.reserve(wanted.size());

		for (const auto& placement : wanted) {
			const auto& entry = *placement.entry;

			// A subtitle mod already floats this person's name over their head
			// while they talk, so ours is either in the way or saying it twice.
			if (entry.speaking && settings->labelSpeakerMode == 1) {
				continue;
			}
			const auto& at = placement.anchor;
			const auto  alpha = placement.alpha;

			const auto thisSize = fontSize * entry.scale;

			// "x40" rather than a word, so it needs no translating and cannot
			// collide with an item that happens to be called "many something".
			const auto text = entry.count > 1
								  ? std::format("{} x{}", entry.text, entry.count)
								  : entry.text;

			ImVec2 size{};
			ImFont_CalcTextSizeA(&size, font, thisSize,
				std::numeric_limits<float>::max(), 0.0f, text.c_str(), nullptr, nullptr);

			// The icon sits on the same baseline as the text, and the pair is
			// centred together so a marked tag does not sit off to one side of
			// the thing it names.
			const bool  hasStar = settings->labelIcons && entry.favourite;
			const bool  hasIcon = settings->labelIcons &&
			                      (entry.icon != Disposition::kNone || entry.mark >= 0);
			const float iconSize = hasIcon ? thisSize * 0.78f : 0.0f;
			const float iconGap = hasIcon ? thisSize * 0.28f : 0.0f;

			// The tally a marker rule can carry, drawn small and tight against
			// its icon so it reads as part of the symbol rather than as part of
			// the name.
			std::string tally;
			ImVec2      tallySize{};
			const auto  tallyFontSize = thisSize * 0.72f;
			if (hasIcon && entry.markCount >= 0 && entry.markFill < 0.0f) {
				tally = entry.markCount >= entry.markCap
							? std::format("{}+", entry.markCap)
							: std::to_string(entry.markCount);
				ImFont_CalcTextSizeA(&tallySize, font, tallyFontSize,
					std::numeric_limits<float>::max(), 0.0f, tally.c_str(), nullptr, nullptr);
				tallySize.x += thisSize * 0.12f;
			}

			const float starSize = hasStar ? thisSize * 0.68f : 0.0f;
			const float starGap = hasStar ? thisSize * 0.18f : 0.0f;

			// Arousal rides on the right of the name. Everything else a tag can
			// carry is on the left, so there is one place to look for it and it
			// cannot be confused with a marker.
			const bool  hasHeat = settings->arousalShow && entry.arousal >= settings->arousalMin;
			const float heatSize = hasHeat ? thisSize * 0.74f : 0.0f;
			const float heatGap = hasHeat ? thisSize * 0.22f : 0.0f;

			std::string heat;
			ImVec2      heatTextSize{};
			const auto  heatFontSize = thisSize * 0.68f;
			if (hasHeat && settings->arousalNumber) {
				heat = std::to_string(entry.arousal);
				ImFont_CalcTextSizeA(&heatTextSize, font, heatFontSize,
					std::numeric_limits<float>::max(), 0.0f, heat.c_str(), nullptr, nullptr);
				heatTextSize.x += thisSize * 0.10f;
			}

			const float totalWidth = starSize + starGap + iconSize + tallySize.x + iconGap + size.x +
			                         heatGap + heatSize + heatTextSize.x;

			// The title sits on its own line above the name, smaller and
			// centred on it. Above rather than beside, because a long title
			// beside a long name doubles the width of the tag and pushes both
			// off the thing they belong to.
			const bool  hasTitle = settings->titlesShow && !entry.title.empty();
			const float titleFontSize = thisSize * settings->titleScale;
			ImVec2      titleSize{};
			if (hasTitle) {
				ImFont_CalcTextSizeA(&titleSize, font, titleFontSize,
					std::numeric_limits<float>::max(), 0.0f, entry.title.c_str(), nullptr, nullptr);
			}
			const float titleGap = hasTitle ? thisSize * 0.08f : 0.0f;
			const float titleHeight = hasTitle ? titleSize.y + titleGap : 0.0f;

			// Pushed down by the height of the title, so the pair together stays
			// centred on the thing being named rather than the name alone.
			ImVec2 topLeft{ at.x - totalWidth * 0.5f, at.y - size.y * 0.5f + titleHeight * 0.5f };

			// Step the tag upward until it stops landing on one already placed.
			// Upward rather than in any direction because a column of tags over
			// a pile of loot still reads as belonging to the pile, whereas one
			// shoved sideways reads as belonging to whatever it lands next to.
			float lifted = 0.0f;
			if (settings->labelDeclutter) {
				const auto step = size.y + titleHeight + 3.0f;
				const auto pad = 2.0f;

				for (int attempt = 0; attempt <= static_cast<int>(settings->labelDeclutterSteps); ++attempt) {
					const Box box{ topLeft.x - pad, topLeft.y - titleHeight - pad,
						topLeft.x + std::max(totalWidth, titleSize.x) + pad, topLeft.y + size.y + pad };

					const bool clear = std::none_of(taken.begin(), taken.end(), [&box](const Box& a_other) {
						return box.left < a_other.right && a_other.left < box.right &&
						       box.top < a_other.bottom && a_other.top < box.bottom;
					});

					if (clear) {
						taken.push_back(box);
						break;
					}

					// Out of attempts: leave it where it wanted to be. A tag
					// drawn on top of another is still better than a missing one,
					// and the alternative is a tag halfway up the sky.
					if (attempt == static_cast<int>(settings->labelDeclutterSteps)) {
						break;
					}

					topLeft.y -= step;
					lifted += step;
				}
			}

			// A hairline back to where the tag belongs, so a lifted one is not
			// mistaken for a label on whatever is behind it.
			if (lifted > 1.0f) {
				ImDrawList_AddLine(draw,
					ImVec2{ at.x, at.y },
					ImVec2{ at.x, topLeft.y + size.y + 2.0f },
					PackColour(entry.colour, alpha * 0.35f), 1.0f);
			}

			const ImVec2 textAt{ topLeft.x + starSize + starGap + iconSize + tallySize.x + iconGap, topLeft.y };

			if (settings->labelBackdrop) {
				const ImVec2 pad{ 4.0f, 2.0f };
				ImDrawList_AddRectFilled(draw,
					ImVec2{ topLeft.x - pad.x, topLeft.y - titleHeight - pad.y },
					ImVec2{ topLeft.x + std::max(totalWidth, titleSize.x) + pad.x,
						topLeft.y + size.y + pad.y },
					PackColour(0x000000, alpha * 0.45f), 3.0f, 0);
			}

			if (hasStar) {
				const ImVec2 centre{ topLeft.x + starSize * 0.5f, topLeft.y + size.y * 0.5f };
				DrawStar(draw, ImVec2{ centre.x + 1.0f, centre.y + 1.0f }, starSize,
					PackColour(0x000000, alpha * 0.7f));
				DrawStar(draw, centre, starSize, PackColour(settings->favouriteColour, alpha));
			}

			if (hasIcon) {
				const ImVec2 centre{ topLeft.x + starSize + starGap + iconSize * 0.5f,
					topLeft.y + size.y * 0.5f };

				// A user-supplied picture wins over the built-in shape. It is
				// tinted by the rule's colour, so a white glyph comes out in
				// whatever colour the rule asked for.
				void* texture = entry.mark >= 0
									? Marks::GetSingleton()->Texture(entry.mark, entry.markAtFull)
									: nullptr;
				if (texture) {
					const auto half = iconSize * 0.62f;
					const ImVec2 min{ centre.x - half, centre.y - half };
					const ImVec2 max{ centre.x + half, centre.y + half };
					const auto   id = static_cast<ImTextureID>(texture);

					if (entry.markFill >= 0.0f) {
						// Draw the whole symbol faint, then the same symbol at
						// full strength clipped to the bottom of the box, so it
						// reads as a vessel filling rather than two pictures.
						ImDrawList_AddImage(draw, id, min, max,
							ImVec2{ 0.0f, 0.0f }, ImVec2{ 1.0f, 1.0f },
							PackColour(entry.markColour, alpha * 0.25f));

						const auto fill = std::clamp(entry.markFill, 0.0f, 1.0f);
						if (fill > 0.001f) {
							const auto line = max.y - (max.y - min.y) * fill;
							ImDrawList_PushClipRect(draw,
								ImVec2{ min.x - 1.0f, line }, ImVec2{ max.x + 1.0f, max.y + 1.0f }, true);
							ImDrawList_AddImage(draw, id, min, max,
								ImVec2{ 0.0f, 0.0f }, ImVec2{ 1.0f, 1.0f },
								PackColour(entry.markColour, alpha));
							ImDrawList_PopClipRect(draw);
						}
					} else {
						ImDrawList_AddImage(draw, id, min, max,
							ImVec2{ 0.0f, 0.0f }, ImVec2{ 1.0f, 1.0f },
							PackColour(entry.markColour, alpha));
					}
				} else if (entry.mark >= 0 && entry.markFill >= 0.0f) {
					// Same treatment for the built-in heart when a rule has no
					// picture of its own.
					DrawIcon(draw, Disposition::kLover, centre, iconSize,
						PackColour(entry.markColour, alpha * 0.25f));

					const auto fill = std::clamp(entry.markFill, 0.0f, 1.0f);
					if (fill > 0.001f) {
						const auto half = iconSize * 0.5f;
						const auto line = centre.y + half - iconSize * fill;
						ImDrawList_PushClipRect(draw,
							ImVec2{ centre.x - half - 1.0f, line },
							ImVec2{ centre.x + half + 1.0f, centre.y + half + 1.0f }, true);
						DrawIcon(draw, Disposition::kLover, centre, iconSize,
							PackColour(entry.markColour, alpha));
						ImDrawList_PopClipRect(draw);
					}
				} else {
					DrawIcon(draw, entry.icon, ImVec2{ centre.x + 1.0f, centre.y + 1.0f }, iconSize,
						PackColour(0x000000, alpha * 0.7f));
					DrawIcon(draw, entry.icon, centre, iconSize, PackColour(entry.colour, alpha));
				}

				if (!tally.empty()) {
					const ImVec2 tallyAt{ topLeft.x + starSize + starGap + iconSize + thisSize * 0.06f,
						topLeft.y + (size.y - tallySize.y) * 0.5f };
					ImDrawList_AddText_FontPtr(draw, font, tallyFontSize,
						ImVec2{ tallyAt.x + 1.0f, tallyAt.y + 1.0f },
						PackColour(0x000000, alpha * 0.8f), tally.c_str(), nullptr, 0.0f, nullptr);
					ImDrawList_AddText_FontPtr(draw, font, tallyFontSize, tallyAt,
						PackColour(entry.markColour, alpha), tally.c_str(), nullptr, 0.0f, nullptr);
				}
			}

			if (hasTitle) {
				// Centred on the whole tag, not on the name, so a marker on the
				// left does not leave the title looking pushed to one side.
				const ImVec2 titleAt{ topLeft.x + (totalWidth - titleSize.x) * 0.5f,
					topLeft.y - titleHeight };
				const auto   tint = settings->titleRuleColour ? entry.titleColour : settings->titleColour;

				ImDrawList_AddText_FontPtr(draw, font, titleFontSize,
					ImVec2{ titleAt.x + 1.0f, titleAt.y + 1.0f },
					PackColour(0x000000, alpha * 0.8f), entry.title.c_str(), nullptr, 0.0f, nullptr);
				ImDrawList_AddText_FontPtr(draw, font, titleFontSize, titleAt,
					PackColour(tint, alpha * 0.92f), entry.title.c_str(), nullptr, 0.0f, nullptr);
			}

			// A cheap drop shadow keeps the text legible against bright scenery.
			ImDrawList_AddText_FontPtr(draw, font, thisSize,
				ImVec2{ textAt.x + 1.0f, textAt.y + 1.0f },
				PackColour(0x000000, alpha * 0.8f), text.c_str(), nullptr, 0.0f, nullptr);
			ImDrawList_AddText_FontPtr(draw, font, thisSize, textAt,
				PackColour(entry.colour, alpha), text.c_str(), nullptr, 0.0f, nullptr);

			// Vitals. Only the player's entry ever carries these.
			//
			// Bars rather than numbers because the question a sweep answers is
			// "am I in trouble", not "by how much". Each row is set back a
			// little from the one before and shortened to match, so the three
			// together read as a panel lying on a surface angled away rather
			// than three stickers on the glass.
			if (entry.vitals[0] >= 0.0f || entry.vitals[1] >= 0.0f || entry.vitals[2] >= 0.0f) {
				const std::uint32_t colours[3]{ settings->selfHealthColour,
					settings->selfMagickaColour, settings->selfStaminaColour };

				const bool  upright = settings->selfBarPlace == BarPlace::kLeft ||
				                      settings->selfBarPlace == BarPlace::kRight;
				const float thick = std::max(1.0f, settings->selfBarHeight * entry.scale);
				const float span = settings->selfBarWidth * entry.scale;
				const float shear = settings->selfBarShear * thick;
				const float gap = thick * 0.75f;
				const float step = thick + gap;

				// Rows that are actually drawn, so a hidden full bar does not
				// leave a hole in the stack.
				// Yours answer to one rule and everybody else's to another, and
				// the entry says which it is so the render thread never has to
				// work out who anybody is.
				const bool     mine = entry.vitalsSelf;
				const ShowWhen when = mine ? settings->selfBarsWhen : settings->vitalsActorsWhen;

				int rows[3]{};
				int shown = 0;
				for (int i = 0; i < 3; ++i) {
					const float v = entry.vitals[i];
					if (v < 0.0f) {
						continue;
					}
					if (when == ShowWhen::kNotFull && v >= 0.999f) {
						continue;
					}
					if (when == ShowWhen::kOnChange &&
						now - entry.vitalsAt > settings->selfHudLinger) {
						continue;
					}
					rows[shown++] = i;
				}

				for (int r = 0; r < shown; ++r) {
					const int   i = rows[r];
					const float away = static_cast<float>(r) * settings->selfBarPerspective;
					const float length = span * (1.0f - away);
					// Recentred as it shortens, so the stack tapers about its
					// middle instead of hanging off one end.
					const float inset = (span - length) * 0.5f;

					ImVec2 origin{};
					switch (settings->selfBarPlace) {
					case BarPlace::kAbove:
						// Above the title if there is one, climbing away.
						origin = ImVec2{ topLeft.x + (totalWidth - span) * 0.5f + inset,
							topLeft.y - titleHeight - step * static_cast<float>(r + 1) - thick * 0.5f };
						break;
					case BarPlace::kLeft:
						origin = ImVec2{ topLeft.x - step * static_cast<float>(r + 1) - thick,
							topLeft.y + size.y * 0.5f + span * 0.5f - inset };
						break;
					case BarPlace::kRight:
						origin = ImVec2{ topLeft.x + totalWidth + step * static_cast<float>(r),
							topLeft.y + size.y * 0.5f + span * 0.5f - inset };
						break;
					case BarPlace::kBelow:
					default:
						origin = ImVec2{ topLeft.x + (totalWidth - span) * 0.5f + inset,
							topLeft.y + size.y + thick * 0.6f + step * static_cast<float>(r) };
						break;
					}

					// The user's nudge comes last, on top of whatever side the
					// place rule chose, scaled like everything else on the tag.
					origin.x += settings->selfBarOffsetX * entry.scale;
					origin.y += settings->selfBarOffsetY * entry.scale;

					DrawVitalBar(draw, origin, length, thick, shear, upright, entry.vitals[i],
						settings->barsLostMax ? entry.vitalsCap[i] : 1.0f,
						settings->barsLostFx ? i : -1, now,
						colours[i], settings->selfBarFrameColour, alpha,
						static_cast<int>(settings->selfBarSegments), settings->selfBarGlow);
				}
			}

			if (hasHeat) {
				const ImVec2 centre{ textAt.x + size.x + heatGap + heatSize * 0.5f,
					topLeft.y + size.y * 0.5f };

				// Faint whole flame, then the same flame at full strength
				// clipped from the bottom - the same vessel-filling treatment
				// the marker uses, so the two read as one language.
				const auto fill = std::clamp(static_cast<float>(entry.arousal) / 100.0f, 0.0f, 1.0f);
				DrawFlame(draw, centre, heatSize, PackColour(settings->arousalColour, alpha * 0.22f));

				if (fill > 0.001f) {
					const auto half = heatSize * 0.5f;
					const auto line = centre.y + half - heatSize * fill;
					ImDrawList_PushClipRect(draw,
						ImVec2{ centre.x - half - 1.0f, line },
						ImVec2{ centre.x + half + 1.0f, centre.y + half + 1.0f }, true);
					DrawFlame(draw, centre, heatSize, PackColour(settings->arousalColour, alpha));
					ImDrawList_PopClipRect(draw);
				}

				if (!heat.empty()) {
					const ImVec2 heatAt{ centre.x + heatSize * 0.5f + thisSize * 0.06f,
						topLeft.y + (size.y - heatTextSize.y) * 0.5f };
					ImDrawList_AddText_FontPtr(draw, font, heatFontSize,
						ImVec2{ heatAt.x + 1.0f, heatAt.y + 1.0f },
						PackColour(0x000000, alpha * 0.8f), heat.c_str(), nullptr, 0.0f, nullptr);
					ImDrawList_AddText_FontPtr(draw, font, heatFontSize, heatAt,
						PackColour(settings->arousalColour, alpha), heat.c_str(), nullptr, 0.0f, nullptr);
				}
			}
		}
	}

	namespace
	{
		void __stdcall RenderThunk()
		{
			if (g_instance) {
				g_instance->Render();
			}
		}
	}
}
