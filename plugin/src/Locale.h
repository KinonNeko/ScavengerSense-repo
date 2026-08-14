#pragma once

namespace SS
{
	// Menu text and object names, in whatever language the game is in.
	//
	// Translation files are keyed by the English string itself, so a missing
	// entry degrades to English rather than to a raw key. Drop
	// Data\SKSE\Plugins\ScavengerSense_<language>.ini next to the plugin, one
	// "English text=translated text" pair per line, UTF-8.
	namespace Locale
	{
		void Load(std::string_view a_language);

		// Returns the translation for a_english, or a_english unchanged.
		[[nodiscard]] const char* T(const char* a_english);

		[[nodiscard]] std::size_t Count();
	}

	// Converts a game string to UTF-8, which is what ImGui expects. Chinese and
	// other non-Latin localisations hand back text in the system code page, and
	// feeding those bytes to ImGui produces a row of replacement glyphs.
	[[nodiscard]] std::string ToUtf8(const char* a_text);
}
