#include "Arousal.h"

#include <Windows.h>

namespace SS
{
	namespace
	{
		// The mod's own DLL name. Spelled exactly as it ships: lower case l in
		// "Sexlab", capitals on NG.
		constexpr auto kModule = "SexlabArousedNG.dll";

		// The legacy mirror: rank of this faction is the arousal figure. Present
		// in every plugin in this family, because they all descend from the same
		// record set.
		constexpr auto kPluginName = "SexLabAroused.esm";
		constexpr RE::FormID kArousalFaction = 0x03FC36;
	}

	Arousal* Arousal::GetSingleton()
	{
		static Arousal singleton;
		return std::addressof(singleton);
	}

	void Arousal::Warm()
	{
		if (_warmed) {
			return;
		}
		_warmed = true;

		// GetModuleHandle rather than LoadLibrary: we want to know whether the
		// mod is here, not to bring it in. If SKSE has not loaded it, it is not
		// installed, and loading it ourselves would be a fine way to break it.
		if (auto* module = ::GetModuleHandleA(kModule); module) {
			_value = reinterpret_cast<GetArousalInt>(::GetProcAddress(module, "SLA_GetArousalInt"));

			std::uint32_t version = 0;
			if (auto* fn = reinterpret_cast<GetVersion>(::GetProcAddress(module, "SLA_GetVersion")); fn) {
				version = fn();
			}

			if (_value) {
				_status = version != 0
							  ? std::format("reading {} (version {})", kModule, version)
							  : std::format("reading {}", kModule);
				logger::info("arousal: {}", _status);
				return;
			}

			logger::warn("arousal: {} is loaded but has no SLA_GetArousalInt", kModule);
		}

		// No exports. Try the faction the older forks write into.
		if (auto* handler = RE::TESDataHandler::GetSingleton(); handler) {
			_faction = handler->LookupForm<RE::TESFaction>(kArousalFaction, kPluginName);
		}

		if (_faction) {
			_status = std::format("reading the faction rank in {} - coarser, and only "
								  "updated when the mod last looked at someone",
				kPluginName);
			logger::info("arousal: {}", _status);
			return;
		}

		_status = "not installed";
		logger::info("arousal: no SLO Aroused NG found, the add-on will stay quiet");
	}

	int Arousal::Value(RE::Actor* a_actor) const
	{
		if (!a_actor) {
			return -1;
		}

		// Note for anyone extending this: the mod's own map inserts on read, so
		// asking about somebody starts tracking them. That is why this is only
		// ever called for people a sweep has already lit, and never for every
		// actor in the cell.
		if (_value) {
			return std::clamp(_value(a_actor), 0, 100);
		}

		if (_faction) {
			// -1 from the engine means "not in the faction", which here means
			// the mod has never looked at this person - not that they are calm.
			const auto rank = a_actor->GetFactionRank(_faction, a_actor->IsPlayerRef());
			return rank < 0 ? -1 : std::clamp(static_cast<int>(rank), 0, 100);
		}

		return -1;
	}
}
