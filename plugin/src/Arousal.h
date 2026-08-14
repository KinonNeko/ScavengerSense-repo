#pragma once

namespace SS
{
	// A bridge to SLO Aroused NG, which keeps an arousal figure per actor.
	//
	// Read-only, and optional in the strictest sense: nothing here is linked
	// against, so if the mod is absent every call simply says "no idea". The
	// mod publishes a small set of exported C functions for exactly this, and
	// resolving them by name at runtime is what keeps the dependency soft - our
	// DLL loads and works identically whether or not theirs is installed.
	//
	// There is a second route for the older forks in the same family, which
	// mirror the value into the rank of a faction. It is coarser and staler, so
	// it is only used when the exports are missing.
	class Arousal
	{
	public:
		// The name this appears under on the Add-ons page.
		static constexpr auto kName = "SLO Aroused";

		[[nodiscard]] static Arousal* GetSingleton();

		// Look for the mod. Must run after SKSE has loaded every plugin, since
		// GetModuleHandle only finds a DLL that is already in the process.
		void Warm();

		[[nodiscard]] bool Available() const { return _value != nullptr || _faction != nullptr; }

		// 0 to 100, or -1 when the mod is absent, switched off, or has never
		// looked at this person.
		[[nodiscard]] int Value(RE::Actor* a_actor) const;

		[[nodiscard]] const std::string& Status() const { return _status; }

	private:
		Arousal() = default;

		using GetArousalInt = std::int32_t(*)(RE::Actor*);
		using GetVersion = std::uint32_t(*)();

		GetArousalInt   _value{ nullptr };
		RE::TESFaction* _faction{ nullptr };
		bool            _warmed{ false };
		std::string     _status{ "not looked for yet" };
	};
}
