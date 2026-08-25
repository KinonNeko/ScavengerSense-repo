#include "FontCheck.h"

#include "Config.h"
#include "Locale.h"

#include <cctype>
#include <fstream>
#include <string>
#include <string_view>

namespace SS
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/SKSEMenuFramework.ini";

		// Read at data load, said once the player is somewhere to read it.
		// Empty means there is nothing wrong, or nothing left to say.
		std::string g_pending;

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
			while (b < e && (a_in[b] == ' ' || a_in[b] == '\t' || a_in[b] == '\r')) {
				++b;
			}
			while (e > b && (a_in[e - 1] == ' ' || a_in[e - 1] == '\t' || a_in[e - 1] == '\r')) {
				--e;
			}
			return std::string{ a_in.substr(b, e - b) };
		}

		// Which script has to be drawn, and the switch that builds it.
		//
		// Both halves of "has to be" matter. Our own menu language covers
		// somebody who chose the translation; the game's own language covers
		// somebody running an English menu over a Chinese Skyrim, whose item
		// and NPC names arrive in their own script either way - which is the
		// case the first report came from.
		struct Script
		{
			const char* key;       // lowercased, for matching the INI
			const char* display;   // as it is written in the file
			const char* menu;      // our language setting that implies it
			const char* game;      // the game's sLanguage that implies it
		};

		constexpr Script kScripts[]{
			{ "enablechinese", "EnableChinese", "chinese", "chinese" },
			{ "enablejapanese", "EnableJapanese", "japanese", "japanese" },
			{ "enablekorean", "EnableKorean", "korean", "korean" },
			{ "enablecyrillic", "EnableCyrillic", "russian", "russian" },
			{ "enablethai", "EnableThai", "thai", "thai" },
			{ "enableturkish", "EnableTurkish", "turkish", "turkish" },
		};

		[[nodiscard]] std::string GameLanguage()
		{
			auto* collection = RE::INISettingCollection::GetSingleton();
			if (!collection) {
				return {};
			}
			auto* setting = collection->GetSetting("sLanguage:General");
			if (!setting || !setting->GetString()) {
				return {};
			}
			return Lower(setting->GetString());
		}
	}

	void CheckMenuFont()
	{
		g_pending.clear();

		std::ifstream file{ kPath };
		if (!file) {
			// Not worth shouting about: the framework has to be present for us
			// to have loaded at all, but its INI is written on first run and a
			// fresh install may not have one yet.
			logger::info("font check: no {} to read", kPath);
			return;
		}

		std::string primaryFont;
		bool        enabled[std::size(kScripts)]{};
		bool        seen[std::size(kScripts)]{};

		std::string line;
		while (std::getline(file, line)) {
			const auto text = Trim(line);
			if (text.empty() || text.front() == ';' || text.front() == '#' ||
				text.front() == '[') {
				continue;
			}
			const auto equals = text.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(std::string_view{ text }.substr(0, equals)));
			const auto value = Trim(std::string_view{ text }.substr(equals + 1));

			if (key == "primaryfont") {
				primaryFont = value;
			}
			for (std::size_t i = 0; i < std::size(kScripts); ++i) {
				if (key == kScripts[i].key) {
					seen[i] = true;
					enabled[i] = Lower(value) == "true" || value == "1";
				}
			}
		}

		const auto mine = Lower(Settings::GetSingleton()->language);
		const auto game = GameLanguage();
		logger::info("font check: menu language '{}', game language '{}'", mine,
			game.empty() ? "unknown" : game);

		for (std::size_t i = 0; i < std::size(kScripts); ++i) {
			if (mine != kScripts[i].menu && (game.empty() || game != kScripts[i].game)) {
				continue;
			}
			if (!seen[i] || enabled[i]) {
				logger::info("font check: {} is on, names should draw", kScripts[i].display);
				continue;
			}

			// From the outside this looks like a text-encoding fault, and the
			// hunt for it went through code pages, fonts and an input method
			// before reaching a switch in somebody else's config. So name the
			// line and the file, and say it where nobody has to open a log.
			logger::warn(
				"font check: {} = false in {}. The menu framework builds no glyphs for "
				"this script, so every name draws as mojibake however good the font is. "
				"Set it to true. (PrimaryFont = {})",
				kScripts[i].display, kPath,
				primaryFont.empty() ? "not set" : primaryFont.c_str());

			g_pending = std::vformat(
				Locale::T("Set {} = true in SKSEMenuFramework.ini, or names will be garbled"),
				std::make_format_args(kScripts[i].display));
			return;
		}
	}

	void SayMenuFontWarning()
	{
		if (g_pending.empty()) {
			return;
		}
		RE::SendHUDMessage::ShowHUDMessage(g_pending.c_str());
		// Once per session. It is a setting you change and restart for, not
		// something to be nagged about on every load.
		g_pending.clear();
	}
}
