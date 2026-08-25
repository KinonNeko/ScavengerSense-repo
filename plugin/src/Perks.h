#pragma once

#include <string>

namespace RE
{
	class BGSPerk;
}

namespace SS
{
	// Gate the sense and the hunt behind a perk the player names.
	//
	// Nothing here writes to the save, adds a record, or needs a script: the
	// perk belongs to whatever mod the player already runs, and all we do is
	// ask whether they have it. Somebody on Ordinator names an Ordinator perk,
	// somebody on vanilla names a vanilla one, and neither needs a patch from
	// us. An empty setting means no requirement, which is how the mod ships.
	class Perks
	{
	public:
		[[nodiscard]] static Perks* GetSingleton();

		// After kDataLoaded, and again whenever the settings change. Resolving
		// once and remembering costs nothing and keeps the key handler clear of
		// a form lookup.
		void Resolve();

		[[nodiscard]] bool AllowsSense() const;
		[[nodiscard]] bool AllowsTracking() const;

		// What the menu shows under each box: the perk that was found, or why
		// nothing was. Already translated.
		[[nodiscard]] const std::string& SenseStatus() const { return _senseStatus; }
		[[nodiscard]] const std::string& TrackingStatus() const { return _trackingStatus; }

		// Says why the key did nothing, if the player asked to be told. Safe to
		// call when there is no requirement - it stays quiet.
		void ExplainSense() const;
		void ExplainTracking() const;

	private:
		Perks() = default;

		RE::BGSPerk* _sense{ nullptr };
		RE::BGSPerk* _tracking{ nullptr };
		std::string  _senseStatus;
		std::string  _trackingStatus;
	};
}
