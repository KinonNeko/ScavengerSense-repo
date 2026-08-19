#pragma once

#include "Config.h"
#include "Marks.h"

namespace SS
{
	// Everything drawn on the finished frame: name tags, the ground ring, and
	// the vignette.
	//
	// These cannot be drawn from a normal SKSE Menu Framework window: opening
	// one puts the game into its locked state and swallows input, which is fine
	// for a settings page and useless for a HUD. Framework 3.13 added
	// RegisterHudElement for exactly this, so that is what we use - resolved at
	// runtime, because 3.9 does not export it and we still want to load there.
	class Labels
	{
	public:
		struct Entry
		{
			std::string   text;
			RE::NiPoint3  world;
			std::uint32_t colour;   // 0xRRGGBB
			float         bornAt;   // seconds, shared clock with Sense
			float         diesAt;
			float         scale{ 1.0f };  // multiplies the configured text size
			Disposition   icon{ Disposition::kNone };
			// How many identical things this tag stands for. A cellar with forty
			// bottles of wine in it is forty tags stacked into an unreadable
			// smear; one tag reading "Wine x40" is the same information.
			std::uint32_t count{ 1 };
			// Index into the user's marker rules, or -1. When a rule matches and
			// supplies a picture, that picture replaces the built-in shape.
			int           mark{ -1 };
			std::uint32_t markColour{ 0xFFFFFF };
			// Drawn beside the marker when the rule carries a counter. -1 hides
			// it; countCap means "that many or more".
			int           markCount{ -1 };
			int           markCap{ 101 };
			// 0..1 when the marker is drawn as a filling icon; negative means
			// draw the tally as a number instead.
			float         markFill{ -1.0f };
			// Something the player has favourited. Drawn with a star and its own
			// colour, because "I marked this as worth having" beats every other
			// thing we could say about an object.
			bool          favourite{ false };
			// The marker has reached its full tally, so a rule that supplies a
			// second picture shows that one instead.
			bool          markAtFull{ false };
			// Arousal, 0 to 100, or -1 when nothing is known. Drawn on the far
			// side of the name from everything else, so it never crowds the
			// marker and you always know where to look for it.
			int           arousal{ -1 };
			// Drawn on its own line above the name, smaller. Empty for most
			// things - a barrel is not a Jarl.
			std::string   title;
			std::uint32_t titleColour{ 0xC9C9C9 };
			// The thing this names, so the tag can be kept over it while it
			// moves. Resolving a handle needs the main thread, so the position
			// is refreshed there and the render thread only ever reads world.
			RE::ObjectRefHandle owner;
			// Set on the main thread when this person is talking, for people
			// running a mod that floats subtitles over the speaker.
			bool          speaking{ false };
			// Health, magicka, stamina as fractions of their maximum. Negative
			// means "nothing known, draw no bar" - which is every entry except
			// the player's.
			float         vitals[3]{ -1.0f, -1.0f, -1.0f };
			// Which rule decides whether those are drawn: yours, or the one for
			// other people. Kept on the entry so the render thread does not have
			// to work out who anybody is.
			bool          vitalsSelf{ false };
			// When any of them last moved, for "only someone just hurt".
			float         vitalsAt{ -1000.0f };
			// How much of each bar's scale is actually available: 1.0 normally,
			// less when a survival mod has pulled the ceiling down. The span
			// above it is drawn as a dead zone. (Positional initialisation:
			// fields go at the END.)
			float         vitalsCap[3]{ 1.0f, 1.0f, 1.0f };
			// Their level, -1 to say nothing; and what is in their hands as a
			// WeaponKind, 0 to keep the relationship shape. Both at the END:
			// the aggregate initialisers count on it.
			std::int16_t  level{ -1 };
			std::uint8_t  weapon{ 0 };
			// The racial emblem as a RaceKind, and a RaceMark rider for
			// vampires and werewolves. Still the END.
			std::uint8_t  race{ 0 };
			std::uint8_t  raceMark{ 0 };
		};

		// The stats row under the corner readout. Values below zero hide the
		// entry they belong to.
		struct SelfStats
		{
			std::int32_t gold{ -1 };
			float        weight{ -1.0f };
			float        weightMax{ 0.0f };
			float        cold{ -1.0f };
			float        coldMax{ 0.0f };
			std::int16_t level{ -1 };
			std::uint8_t weapon{ 0 };
			std::uint8_t race{ 0 };      // RaceKind
			std::uint8_t raceMark{ 0 };  // RaceMark
		};

		// The sonar ring, sampled as a height field so the render thread never
		// has to touch game data.
		//
		// A circle drawn straight onto the screen looks pasted on, because the
		// ground is not a plane: it climbs stairs, drops off ledges and rolls
		// over hills. So the main thread samples the terrain height at a grid of
		// angles and radii when the wave starts, and the render thread just
		// interpolates that table and projects the points.
		struct Ring
		{
			RE::NiPoint3       centre;
			float              minRadius{ 0.0f };
			float              maxRadius{ 0.0f };
			float              startAt{ 0.0f };
			float              endAt{ 0.0f };
			std::uint32_t      angles{ 0 };
			std::uint32_t      steps{ 0 };
			std::vector<float> heights;  // angles * steps, world Z

			[[nodiscard]] bool Valid() const
			{
				return angles >= 8 && steps >= 2 && endAt > startAt && maxRadius > minRadius &&
				       heights.size() == static_cast<std::size_t>(angles) * steps;
			}
		};

		[[nodiscard]] static Labels* GetSingleton();

		// Registers the HUD callback. Safe to call once; reports what it found.
		void Install();

		void Clear();

		// Move tags that are already on screen, without rebuilding them. Called
		// every frame from the main thread while a wave is in flight; the vector
		// is positional, matching the order Replace was last given.
		// Vitals travel as {h, m, s, changedAt, capH, capM, capS} per entry.
		void MoveTo(const std::vector<RE::NiPoint3>& a_anchors, const std::vector<bool>& a_speaking,
			const std::vector<std::array<float, 7>>& a_vitals);
		void Replace(std::vector<Entry> a_entries);

		// The corner readout. Pushed from the main thread every frame while it
		// is switched on; a_changedAt is when any of the three last moved, which
		// is what the "only when it changes" rule is judged against.
		void SetSelfHud(const float (&a_vitals)[3], const float (&a_caps)[3], float a_changedAt);

		// Bars over people the player has hit, for as long as the fight lasts.
		// Pushed whole every frame from the main thread, like MoveTo; only the
		// vitals fields and world/owner/scale of each entry are used.
		void SetCombatBars(std::vector<Entry> a_entries);

		void SetSelfStats(const SelfStats& a_stats);

		// How much of the drawn ammunition is left. The main thread resolves the
		// anchor - body, head or bow - into a world point before handing it over,
		// because finding a skeleton node means touching game data and the render
		// thread must never do that. show=false puts it away.
		struct Ammo
		{
			bool         show{ false };
			std::int32_t count{ 0 };
			std::string  name;
			RE::NiPoint3 world;
			// Eased on the main thread, so the render side never has to reconcile
			// clocks to know how far through a fade it is.
			float        alpha{ 1.0f };
		};
		void SetAmmo(const Ammo& a_ammo);

		// Draws the stats row at a point: -1 grows right from it, +1 grows
		// left, 0 centres. Render thread; takes a snapshot, not the member.
		void DrawStatsRow(void* a_drawList, const SelfStats& stats, const Settings* settings,
			float a_x, float a_y, int a_align, float fontSize, float alpha);

		// Breadcrumb trails, pushed whole from the main thread whenever the
		// recorder runs. Fade is precomputed there, so the render side never
		// needs to reconcile clocks.
		struct TrailMark
		{
			RE::NiPoint3 world;
			float        fade{ 1.0f };
		};
		struct Trail
		{
			std::vector<TrailMark> marks;  // oldest first
			std::string            label;
			std::uint32_t          colour{ 0xCBCBCB };
			// Marked quarry draw at full strength with the scent line, sweep
			// or no sweep - the mark is what lets the person be found.
			bool                   bright{ false };
			// The owner's form id, so the aim preview can point at this trail.
			std::uint32_t          id{ 0 };
		};
		// a_lit says a sweep is running: trails brighten with everything else.
		void SetTrails(std::vector<Trail> a_trails, bool a_lit);

		// Winds the overlay down gracefully: every tag's deadline is pulled in
		// to now + a_fade, the wash and the ring with them.
		void Expire(float a_fade);

		// The aim highlight while a sweep runs: the marking sign over whoever
		// the trail key would mark, in the colour it would mark them.
		void SetAim(bool a_valid, const RE::NiPoint3& a_feet, const RE::NiPoint3& a_head,
			std::uint32_t a_colour);

		// The footprint half of the same preview: running dots along the
		// trail the key would take. 0 puts the sign away.
		void SetAimTrail(std::uint32_t a_id, std::uint32_t a_colour);

		void SetRing(Ring a_ring);
		void StopRing();

		// Composited overlay drawn under everything else. This is the part that
		// survives Community Shaders and ENB no matter what they do to the
		// tonemapper, because it lands on the finished image.
		void SetWash(float a_bornAt, float a_diesAt);
		void StopWash();

		[[nodiscard]] bool Available() const { return _installed; }

		// Invoked by the framework's HUD callback on the render thread.
		void Render();

	private:
		Labels() = default;

		void SubmitPostFX(void* a_drawList, float a_now);
		void DrawSelfHud(void* a_drawList, float a_width, float a_height, float a_now);
		void DrawAmmo(void* a_drawList, float a_width, float a_height);
		void DrawWash(void* a_drawList, float a_width, float a_height, float a_now);
		void DrawRing(void* a_drawList, float a_width, float a_height, float a_now);

		std::vector<Entry> _entries;
		std::vector<Entry> _combat;
		std::vector<Trail> _trails;
		bool               _trailsLit{ false };
		bool               _aimValid{ false };
		RE::NiPoint3       _aimFeet;
		RE::NiPoint3       _aimHead;
		std::uint32_t      _aimColour{ 0xFFA600 };
		std::uint32_t      _aimTrail{ 0 };
		std::uint32_t      _aimTrailColour{ 0xFFA600 };
		Ring               _ring;
		float              _selfHud[3]{ -1.0f, -1.0f, -1.0f };
		float              _selfHudCap[3]{ 1.0f, 1.0f, 1.0f };
		float              _selfHudAt{ -1000.0f };
		SelfStats          _selfStats;
		Ammo               _ammo;
		float              _washBorn{ 0.0f };
		float              _washDies{ 0.0f };
		std::mutex         _lock;
		bool               _installed{ false };
		bool               _sawFirstFrame{ false };
		bool               _saidSuppressed{ false };

	public:
		// Set on the first HUD frame. Read by the menu to explain itself.
		std::atomic_bool _hasCJK{ false };
		[[nodiscard]] bool SawFirstFrame() const { return _sawFirstFrame; }
	};
}
