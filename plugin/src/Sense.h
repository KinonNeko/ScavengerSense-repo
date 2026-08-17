#pragma once

#include "Config.h"

#include <array>
#include <unordered_map>
#include "Labels.h"

namespace SS
{
	class Sense : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		[[nodiscard]] static Sense* GetSingleton();

		// Hits the player lands, feeding the in-combat bars.
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override;

		// Resolve the forms shipped in ScavengerSense.esp. Safe to call more than once.
		void OnDataLoaded();

		// Hotkey entry point. Honours Settings::toggle and Settings::cooldown.
		void OnHotkey();

		// The trail key: aimed at somebody it toggles tracking them, aimed at
		// nothing it hides or shows every trail. Gated to a live sweep when
		// the trail apparatus lives inside the sense.
		void OnTrailHotkey();

		// Holding the trail key wipes everything: marks, trails, quarry.
		void OnTrailLongPress();

		// The palette colour of a marked person, 0 when they are not marked.
		[[nodiscard]] std::uint32_t MarkColour(RE::FormID a_id) const;

		// Cancel everything immediately (used on load/exit and the menu button).
		void Cancel();

		// The hotkey's way out: everything fades over the sweep's own fade-out
		// instead of vanishing on a frame.
		void FadeOut();

		[[nodiscard]] bool Ready() const { return _ready; }
		[[nodiscard]] bool LabelsAvailable() const;

		// One line describing what OnDataLoaded found, shown in the menu.
		[[nodiscard]] const std::string& Status() const { return _status; }

	private:
		// Why each reference was or was not picked up, so a sweep that lights
		// nothing can say which filter ate everything.
		struct ScanStats
		{
			std::uint32_t visited{};
			std::uint32_t accepted{};
			std::uint32_t wrongType{};
			std::uint32_t categoryOff{};
			std::uint32_t disabled{};
			std::uint32_t noThreeD{};
			std::uint32_t harvested{};
			std::uint32_t unnamed{};
			std::uint32_t placeholder{};
			std::uint32_t emptyContainer{};
			std::uint32_t deadActor{};
			std::uint32_t notEnemy{};
			std::uint32_t actorsSeen{};
			std::uint32_t actorCastFailed{};
			std::uint32_t highActors{};
		};

		struct Pending
		{
			RE::ObjectRefHandle handle;
			float               triggerAt;  // seconds after the wave starts
			Category            category;
		};

		Sense() = default;

		void Start();
		void Tick();
		// Reads the player's vitals every frame for the corner readout.
		void PollSelf();
		// Keeps bars over people the player has hit while a fight is on.
		void PollCombat();
		// Records breadcrumbs behind anyone the sense has touched, and hands
		// the drawable trails to Labels.
		void PollTrails();
		void ApplyTo(RE::TESObjectREFR* a_ref, Category a_category);
		void ClearOurEffects();
		// Shortens our live shader effects so the engine plays their own
		// fade-out, rather than killing them.
		void SoftenOurEffects(float a_fade);
		void BeginTint(float a_duration);
		void EndTint();

		[[nodiscard]] static Category Categorise(const RE::TESBoundObject* a_base);
		[[nodiscard]] bool            Accept(RE::TESObjectREFR* a_ref, Category a_category, ScanStats& a_stats) const;

		std::vector<RE::TESEffectShader*>         _pool;
		std::unordered_set<RE::TESEffectShader*>  _poolSet;
		RE::TESImageSpaceModifier*                _imod{ nullptr };
		RE::BGSSoundDescriptorForm*               _sweepSound{ nullptr };

		std::vector<Pending> _pending;
		std::size_t          _next{ 0 };      // index of the next pending entry to fire
		std::size_t          _cursor{ 0 };    // round-robin cursor into _pool
		std::uint32_t        _appliedThisWave{ 0 };
		std::vector<Labels::Entry>     _labelBuffer;
		// Reused every frame by the follow pass, so it never allocates.
		std::vector<RE::NiPoint3>      _anchorBuffer;
		std::vector<bool>              _speakingBuffer;
		std::vector<std::array<float, 7>> _vitalsBuffer;  // three values, when they moved, three caps

		// People the player has hit this fight. Main thread only: the hit sink
		// and the poll both run there, so no lock is needed.
		struct HitTrack
		{
			RE::ObjectRefHandle handle;
			float               lastHitAt{ 0.0f };      // real time
			// Refreshed while they are alive and fighting the player; frozen
			// by death or disengagement, and the entry fades linger seconds
			// after it stops moving.
			float               lastEngagedAt{ 0.0f };
		};
		std::unordered_map<RE::FormID, HitTrack> _combatHits;
		std::vector<Labels::Entry>               _combatBuffer;
		// Everyone the player has ever struck this session. Never expires:
		// "hostile because of something you did" should not be forgotten the
		// moment the fight ends, and Only enemies reads it for corpses too.
		std::unordered_set<RE::FormID>           _struckEver;

		// Who currently earns a trail (sweep-lit or fought), and the trails
		// themselves. Main thread only, like everything around them.
		struct Quarry
		{
			RE::ObjectRefHandle handle;
			float               untilAt{ 0.0f };  // real time recording window
		};
		struct TrailPoint
		{
			RE::NiPoint3 pos;
			float        bornAt{ 0.0f };  // real time
			// Where the mark was laid: the interior cell, or the worldspace.
			// Drawn only when the player is in the same place - an interior's
			// coordinates are nonsense anywhere else.
			RE::FormID   place{ 0 };
		};
		struct Trail
		{
			std::string   name;
			std::uint32_t colour{ 0xCBCBCB };
			float         lastSampleAt{ -1000.0f };
			std::vector<TrailPoint> points;
		};
		std::unordered_map<RE::FormID, Quarry> _quarry;
		std::unordered_map<RE::FormID, Trail>  _trails;
		// Explicitly tracked people: their window never closes while they are
		// loaded, their trail and sweep glow share a palette colour, and their
		// label stays on. Session-only, deliberately - a hunt, not a save.
		struct Marked
		{
			RE::ObjectRefHandle handle;
			std::uint8_t        slot{ 0 };  // palette colour
		};
		std::unordered_map<RE::FormID, Marked> _marked;
		std::atomic_bool                       _trailsHidden{ false };
		// Where the player was last tick, for the transition wipe; _placeId is
		// the current place itself (interior cell or worldspace), stamped onto
		// every trail point laid there.
		bool        _placeKnown{ false };
		bool        _wasInterior{ false };
		RE::FormID  _lastWorldspace{ 0 };
		RE::FormID  _placeId{ 0 };
		float                                    _lastHudPush{ 0.0f };  // real time
		// True while we are actively keeping other HUDs' enemy bars down.
		// Atomic because the worker thread reads it to keep the tick alive
		// long enough to hand everything back when the feature turns off.
		std::atomic_bool                         _enemyHudOwned{ false };
		bool                                     _targetControlHeld{ false };
		bool                                     _saidControlRefused{ false };
		// Where the vanilla enemy health element normally sits. Captured the
		// first time we park it off screen - hiding by position, because the
		// HUD's own ActionScript re-drives visibility every target update and
		// wins any fight over _visible.
		float                                    _enemyHealthHomeY{ -100000.0f };
		bool                                     _combatShown{ false };
		bool                                     _hitSinkRegistered{ false };
		float                          _selfLast[3]{ -1.0f, -1.0f, -1.0f };
		float                          _selfChangedAt{ -1000.0f };
		std::unordered_set<RE::FormID> _favourites;  // rebuilt at the start of each sweep
		// Lowercased placeholder substrings, split once per sweep.
		std::vector<std::string>       _placeholderPieces;
		float                _waveStart{ 0.0f };
		float                _waveEnd{ 0.0f };  // absolute time at which everything is done
		float                _startedRealAt{ 0.0f };  // real time, for the watchdog
		float                _lastFired{ -1000.0f };

		RE::TESGlobal* _coldGlobal{ nullptr };
		bool           _coldLooked{ false };
		// Gold walks the whole inventory, so it is asked once a second.
		float          _goldAt{ -1000.0f };
		std::int32_t   _gold{ -1 };

		std::string      _status{ "not initialised" };
		std::atomic_bool _ready{ false };
		std::atomic_bool _active{ false };
		std::atomic_bool _threadRunning{ false };
		std::mutex       _lock;
	};
}
