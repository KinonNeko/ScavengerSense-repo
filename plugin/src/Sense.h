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

		// Cancel everything immediately (used on load/exit as well as by the toggle).
		void Cancel();

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
			std::uint32_t deadActor{};
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
		void ApplyTo(RE::TESObjectREFR* a_ref, Category a_category);
		void ClearOurEffects();
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
			float               lastHitAt{ 0.0f };  // real time
		};
		std::unordered_map<RE::FormID, HitTrack> _combatHits;
		std::vector<Labels::Entry>               _combatBuffer;
		float                                    _combatLastActive{ -1000.0f };  // real time
		bool                                     _combatShown{ false };
		bool                                     _hitSinkRegistered{ false };
		float                          _selfLast[3]{ -1.0f, -1.0f, -1.0f };
		float                          _selfChangedAt{ -1000.0f };
		std::unordered_set<RE::FormID> _favourites;  // rebuilt at the start of each sweep
		float                _waveStart{ 0.0f };
		float                _waveEnd{ 0.0f };  // absolute time at which everything is done
		float                _startedRealAt{ 0.0f };  // real time, for the watchdog
		float                _lastFired{ -1000.0f };

		std::string      _status{ "not initialised" };
		std::atomic_bool _ready{ false };
		std::atomic_bool _active{ false };
		std::atomic_bool _threadRunning{ false };
		std::mutex       _lock;
	};
}
