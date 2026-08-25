#include "Perks.h"

#include "Config.h"
#include "Locale.h"

#include <algorithm>
#include <cctype>

namespace SS
{
	namespace
	{
		[[nodiscard]] std::string Lower(std::string a_in)
		{
			for (auto& ch : a_in) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			return a_in;
		}

		[[nodiscard]] std::string Trim(std::string_view a_in)
		{
			std::size_t b = 0;
			std::size_t e = a_in.size();
			while (b < e && (a_in[b] == ' ' || a_in[b] == '\t')) {
				++b;
			}
			while (e > b && (a_in[e - 1] == ' ' || a_in[e - 1] == '\t')) {
				--e;
			}
			return std::string{ a_in.substr(b, e - b) };
		}

		// Two ways to name a perk, because people arrive with two different
		// things in hand.
		//
		// "Ordinator - Perks of the Nord.esp|0x1234" is exact and always works.
		// A bare "Treasure Hunter" is what somebody actually remembers, and is
		// resolved by walking every perk the load order defines and comparing
		// display names. That walk is a few thousand entries once, at load.
		//
		// The editor ID is tried too, but only as a courtesy: Skyrim SE throws
		// editor IDs away at runtime unless powerofthree's Tweaks is installed
		// to keep them, so it is the least dependable of the three and is never
		// the thing the menu suggests.
		[[nodiscard]] RE::BGSPerk* Find(const std::string& a_spec, std::string& a_status)
		{
			const auto spec = Trim(a_spec);
			if (spec.empty()) {
				a_status.clear();
				return nullptr;
			}

			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				a_status = Locale::T("game data not ready");
				return nullptr;
			}

			if (const auto bar = spec.find('|'); bar != std::string::npos) {
				const auto plugin = Trim(std::string_view{ spec }.substr(0, bar));
				const auto idText = Trim(std::string_view{ spec }.substr(bar + 1));
				std::uint32_t id = 0;
				try {
					id = static_cast<std::uint32_t>(std::stoul(idText, nullptr, 16));
				} catch (const std::exception&) {
					a_status = Locale::T("that is not a form ID");
					return nullptr;
				}
				if (auto* perk = handler->LookupForm<RE::BGSPerk>(id, plugin); perk) {
					a_status = std::format("{} - {}", Locale::T("required"),
						perk->GetFullName() ? perk->GetFullName() : plugin.c_str());
					return perk;
				}
				a_status = std::format("{} - {}", Locale::T("no such perk in"), plugin);
				return nullptr;
			}

			const auto wanted = Lower(spec);
			for (auto* perk : handler->GetFormArray<RE::BGSPerk>()) {
				if (!perk) {
					continue;
				}
				const char* full = perk->GetFullName();
				if (full && Lower(full) == wanted) {
					a_status = std::format("{} - {}", Locale::T("required"), full);
					return perk;
				}
			}
			for (auto* perk : handler->GetFormArray<RE::BGSPerk>()) {
				if (!perk) {
					continue;
				}
				const char* edid = perk->GetFormEditorID();
				if (edid && Lower(edid) == wanted) {
					a_status = std::format("{} - {}", Locale::T("required"), edid);
					return perk;
				}
			}

			a_status = Locale::T("no perk by that name - try Plugin.esp|0xID");
			return nullptr;
		}

		[[nodiscard]] bool Holds(RE::BGSPerk* a_perk)
		{
			// No perk named, or a name that resolved to nothing: the gate is
			// open. A typo must not lock somebody out of their own mod.
			if (!a_perk) {
				return true;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			return !player || player->HasPerk(a_perk);
		}
	}

	Perks* Perks::GetSingleton()
	{
		static Perks singleton;
		return std::addressof(singleton);
	}

	void Perks::Resolve()
	{
		const auto* settings = Settings::GetSingleton();
		_sense = Find(settings->perkSense, _senseStatus);
		_tracking = Find(settings->perkTracking, _trackingStatus);

		if (!settings->perkSense.empty()) {
			logger::info("perks: sense requires '{}' - {}", settings->perkSense,
				_sense ? "found" : "NOT FOUND, the gate stays open");
		}
		if (!settings->perkTracking.empty()) {
			logger::info("perks: tracking requires '{}' - {}", settings->perkTracking,
				_tracking ? "found" : "NOT FOUND, the gate stays open");
		}
	}

	bool Perks::AllowsSense() const
	{
		return Holds(_sense);
	}

	bool Perks::AllowsTracking() const
	{
		return Holds(_tracking);
	}

	void Perks::ExplainSense() const
	{
		// Without this a gated key is indistinguishable from a broken one, and
		// the report that comes back says the hotkey stopped working.
		if (!Settings::GetSingleton()->perkNotify || AllowsSense()) {
			return;
		}
		RE::SendHUDMessage::ShowHUDMessage(
			Locale::T("You have not learned to sense this way"));
	}

	void Perks::ExplainTracking() const
	{
		if (!Settings::GetSingleton()->perkNotify || AllowsTracking()) {
			return;
		}
		RE::SendHUDMessage::ShowHUDMessage(
			Locale::T("You have not learned to read tracks"));
	}
}
