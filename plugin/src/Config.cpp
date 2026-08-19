#include "Config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <map>
#include <system_error>
#include <vector>

namespace SS
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/ScavengerSense.ini";

		constexpr const char* kCategoryNames[kCategoryCount] = {
			"container",
			"door",
			"flora",
			"gear",
			"alchemy",
			"book",
			"valuable",
			"activator",
			"furniture",
			"actor"
		};

		constexpr const char* kTriggerNames[] = { "press", "double", "hold" };

		constexpr const char* kActorFilterNames[] = { "all", "living", "dead" };

		constexpr const char* kTrailStyleNames[] = { "chevrons", "footprints" };

		constexpr const char* kAimStyleNames[] = { "corners", "ring", "chevron" };

		constexpr const char* kTrailWipeNames[] = { "hold", "doubletap", "off" };

		constexpr const char* kTrailKeyModeNames[] = { "single", "multi", "allinone" };

		constexpr const char* kTrailRevealNames[] = { "crouchsense", "sense", "crouch", "always" };

		constexpr const char* kStatsPlaceNames[] = { "bars", "corner", "both" };

		constexpr const char* kCountStyleNames[] = { "number", "fill", "hidden" };

		constexpr const char* kBarPlaceNames[] = { "below", "above", "left", "right" };

		constexpr const char* kCornerNames[] = { "off", "topleft", "topright", "bottomleft", "bottomright" };

		constexpr const char* kAmmoAnchorNames[] = { "body", "head", "bow" };

		constexpr const char* kAmmoWhenNames[] = { "always", "drawn", "sensing" };

		constexpr const char* kShowWhenNames[] = { "always", "change", "notfull" };

		constexpr auto kPresetDir = "Data/SKSE/Plugins/ScavengerSense/presets";

		// Where the FOMOD drops one small file per answer the installer asked.
		// Each holds only the handful of keys that answer changes; everything
		// else stays at the built-in default.
		constexpr auto kSetupDir = "Data/SKSE/Plugins/ScavengerSense_setup";

		// Every .ini in the setup folder, sorted by filename so the numeric
		// prefixes decide precedence and the order is the same on every machine.
		std::vector<std::string> SetupFragments()
		{
			std::vector<std::string> found;

			std::error_code ec;
			if (!std::filesystem::is_directory(kSetupDir, ec)) {
				return found;
			}

			for (const auto& entry : std::filesystem::directory_iterator{ kSetupDir, ec }) {
				if (ec) {
					break;
				}
				if (!entry.is_regular_file()) {
					continue;
				}
				auto ext = entry.path().extension().string();
				for (auto& c : ext) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				if (ext == ".ini") {
					found.push_back(entry.path().string());
				}
			}

			std::sort(found.begin(), found.end());
			return found;
		}

		// Only actors get an outline by default: a silhouette is the clearest
		// way to read a person, while loot is easier to spot as a solid glow.
		constexpr bool kCategoryDefaultOutline[kCategoryCount] = {
			false, false, false, false, false, false, false, false, false, true
		};

		// Defaults, matched 1:1 to kCategoryNames.
		constexpr bool kCategoryDefaultEnabled[kCategoryCount] = {
			true, true, true, true, true, true, true, true, true, false
		};

		constexpr std::uint32_t kCategoryDefaultColour[kCategoryCount] = {
			0x4FA3FF,  // container  - blue
			0x5CE65C,  // door       - green
			0x9BE04F,  // flora      - yellow-green
			0xFF7A45,  // gear       - orange
			0xFF6FD8,  // alchemy    - pink
			0xC08CFF,  // book       - violet
			0xFFD24A,  // valuable   - gold
			0xB6C0CC,  // activator  - pale steel
			0xD9B166,  // furniture  - oak
			0xFF4A4A   // actor      - red
		};

		std::string Trim(std::string_view a_in)
		{
			std::size_t b = 0;
			std::size_t e = a_in.size();
			while (b < e && std::isspace(static_cast<unsigned char>(a_in[b]))) {
				++b;
			}
			while (e > b && std::isspace(static_cast<unsigned char>(a_in[e - 1]))) {
				--e;
			}
			return std::string{ a_in.substr(b, e - b) };
		}

		std::string Lower(std::string a_in)
		{
			for (auto& c : a_in) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_in;
		}

		// section.key (both lower-cased) -> raw value
		using Table = std::map<std::string, std::string>;

		Table ReadIni(const char* a_path)
		{
			Table         table;
			std::ifstream file{ a_path };
			if (!file) {
				return table;
			}

			std::string section;
			std::string line;
			while (std::getline(file, line)) {
				// strip comments
				const auto cut = line.find_first_of(";#");
				if (cut != std::string::npos) {
					line.erase(cut);
				}

				auto trimmed = Trim(line);
				if (trimmed.empty()) {
					continue;
				}

				if (trimmed.front() == '[') {
					const auto close = trimmed.find(']');
					if (close != std::string::npos) {
						section = Lower(Trim(trimmed.substr(1, close - 1)));
					}
					continue;
				}

				const auto eq = trimmed.find('=');
				if (eq == std::string::npos) {
					continue;
				}

				auto key = Lower(Trim(trimmed.substr(0, eq)));
				auto val = Trim(trimmed.substr(eq + 1));
				if (!key.empty()) {
					table[section + "." + key] = val;
				}
			}

			return table;
		}

		bool Lookup(const Table& a_table, std::string_view a_section, std::string_view a_key, std::string& a_out)
		{
			const auto it = a_table.find(std::string{ a_section } + "." + Lower(std::string{ a_key }));
			if (it == a_table.end()) {
				return false;
			}
			a_out = it->second;
			return true;
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, bool& a_value)
		{
			std::string raw;
			if (Lookup(a_table, a_section, a_key, raw)) {
				raw = Lower(raw);
				a_value = (raw == "true" || raw == "1" || raw == "yes" || raw == "on");
			}
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, float& a_value)
		{
			std::string raw;
			if (Lookup(a_table, a_section, a_key, raw) && !raw.empty()) {
				a_value = std::strtof(raw.c_str(), nullptr);
			}
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, std::int32_t& a_value)
		{
			std::string raw;
			if (Lookup(a_table, a_section, a_key, raw) && !raw.empty()) {
				a_value = static_cast<std::int32_t>(std::strtol(raw.c_str(), nullptr, 0));
			}
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, std::string& a_value)
		{
			std::string raw;
			if (Lookup(a_table, a_section, a_key, raw) && !raw.empty()) {
				a_value = Lower(raw);
			}
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, Trigger& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}

			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kTriggerNames); ++i) {
				if (raw == kTriggerNames[i]) {
					a_value = static_cast<Trigger>(i);
					return;
				}
			}

			logger::warn("unrecognised trigger \"{}\" - keeping {}", raw, kTriggerNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, ActorFilter& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}

			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kActorFilterNames); ++i) {
				if (raw == kActorFilterNames[i]) {
					a_value = static_cast<ActorFilter>(i);
					return;
				}
			}

			logger::warn("unrecognised actor filter \"{}\" - keeping {}", raw,
				kActorFilterNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, TrailStyle& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kTrailStyleNames); ++i) {
				if (raw == kTrailStyleNames[i]) {
					a_value = static_cast<TrailStyle>(i);
					return;
				}
			}
			logger::warn("unrecognised trail style \"{}\" - keeping {}", raw,
				kTrailStyleNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, AimStyle& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kAimStyleNames); ++i) {
				if (raw == kAimStyleNames[i]) {
					a_value = static_cast<AimStyle>(i);
					return;
				}
			}
			logger::warn("unrecognised aim style \"{}\" - keeping {}", raw,
				kAimStyleNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, TrailWipe& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kTrailWipeNames); ++i) {
				if (raw == kTrailWipeNames[i]) {
					a_value = static_cast<TrailWipe>(i);
					return;
				}
			}
			logger::warn("unrecognised trail wipe gesture \"{}\" - keeping {}", raw,
				kTrailWipeNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, TrailKeyMode& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kTrailKeyModeNames); ++i) {
				if (raw == kTrailKeyModeNames[i]) {
					a_value = static_cast<TrailKeyMode>(i);
					return;
				}
			}
			logger::warn("unrecognised trail key mode \"{}\" - keeping {}", raw,
				kTrailKeyModeNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, TrailReveal& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kTrailRevealNames); ++i) {
				if (raw == kTrailRevealNames[i]) {
					a_value = static_cast<TrailReveal>(i);
					return;
				}
			}
			logger::warn("unrecognised trail reveal \"{}\" - keeping {}", raw,
				kTrailRevealNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, StatsPlace& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kStatsPlaceNames); ++i) {
				if (raw == kStatsPlaceNames[i]) {
					a_value = static_cast<StatsPlace>(i);
					return;
				}
			}
			logger::warn("unrecognised stats place \"{}\" - keeping {}", raw,
				kStatsPlaceNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, CountStyle& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}

			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kCountStyleNames); ++i) {
				if (raw == kCountStyleNames[i]) {
					a_value = static_cast<CountStyle>(i);
					return;
				}
			}
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, BarPlace& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}

			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kBarPlaceNames); ++i) {
				if (raw == kBarPlaceNames[i]) {
					a_value = static_cast<BarPlace>(i);
					return;
				}
			}

			logger::warn("unrecognised bar placement \"{}\" - keeping {}", raw,
				kBarPlaceNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, Corner& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kCornerNames); ++i) {
				if (raw == kCornerNames[i]) {
					a_value = static_cast<Corner>(i);
					return;
				}
			}
			logger::warn("unrecognised corner \"{}\" - keeping {}", raw,
				kCornerNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, AmmoAnchor& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kAmmoAnchorNames); ++i) {
				if (raw == kAmmoAnchorNames[i]) {
					a_value = static_cast<AmmoAnchor>(i);
					return;
				}
			}
			logger::warn("unrecognised ammo anchor \"{}\" - keeping {}", raw,
				kAmmoAnchorNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, AmmoWhen& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kAmmoWhenNames); ++i) {
				if (raw == kAmmoWhenNames[i]) {
					a_value = static_cast<AmmoWhen>(i);
					return;
				}
			}
			logger::warn("unrecognised ammo rule \"{}\" - keeping {}", raw,
				kAmmoWhenNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, ShowWhen& a_value)
		{
			std::string raw;
			if (!Lookup(a_table, a_section, a_key, raw) || raw.empty()) {
				return;
			}
			raw = Lower(raw);
			for (std::size_t i = 0; i < std::size(kShowWhenNames); ++i) {
				if (raw == kShowWhenNames[i]) {
					a_value = static_cast<ShowWhen>(i);
					return;
				}
			}
			logger::warn("unrecognised visibility rule \"{}\" - keeping {}", raw,
				kShowWhenNames[static_cast<std::size_t>(a_value)]);
		}

		void Get(const Table& a_table, std::string_view a_section, std::string_view a_key, std::uint32_t& a_value)
		{
			std::string raw;
			if (Lookup(a_table, a_section, a_key, raw) && !raw.empty()) {
				a_value = static_cast<std::uint32_t>(std::strtoul(raw.c_str(), nullptr, 0));
			}
		}
	}

	const char* CategoryName(Category a_category)
	{
		const auto index = static_cast<std::size_t>(a_category);
		return index < kCategoryCount ? kCategoryNames[index] : "unknown";
	}

	const char* TriggerName(Trigger a_trigger)
	{
		const auto index = static_cast<std::size_t>(a_trigger);
		return index < std::size(kTriggerNames) ? kTriggerNames[index] : "press";
	}

	const char* ActorFilterName(ActorFilter a_filter)
	{
		const auto index = static_cast<std::size_t>(a_filter);
		return index < std::size(kActorFilterNames) ? kActorFilterNames[index] : "all";
	}

	const char* CountStyleName(CountStyle a_style)
	{
		const auto index = static_cast<std::size_t>(a_style);
		return index < std::size(kCountStyleNames) ? kCountStyleNames[index] : "fill";
	}

	MarkStyle& Settings::StyleFor(const std::string& a_rule, std::uint32_t a_ruleColour)
	{
		// Case-insensitive: the INI reader lower-cases its keys, so a style
		// loaded from disk is "dibella" while the rule calls itself "Dibella".
		// Matching exactly would quietly create a second, empty style every
		// time the menu drew the rule.
		const auto wanted = Lower(a_rule);
		for (auto& style : markStyles) {
			if (Lower(style.name) == wanted) {
				return style;
			}
		}

		// First sight of this rule: seed it from what the rule file asked for,
		// so an untouched marker looks exactly as its author intended.
		MarkStyle fresh;
		fresh.name = a_rule;
		fresh.colour = a_ruleColour;
		markStyles.push_back(std::move(fresh));
		return markStyles.back();
	}

	bool Settings::IntegrationEnabled(std::string_view a_name) const
	{
		const auto wanted = Lower(std::string{ a_name });
		for (const auto& entry : integrations) {
			if (Lower(entry.name) == wanted) {
				return entry.enabled;
			}
		}
		// Never seen before, so nobody has turned it off. On.
		return true;
	}

	void Settings::SetIntegration(std::string_view a_name, bool a_enabled)
	{
		const auto wanted = Lower(std::string{ a_name });
		for (auto& entry : integrations) {
			if (Lower(entry.name) == wanted) {
				entry.enabled = a_enabled;
				return;
			}
		}
		integrations.push_back(IntegrationState{ std::string{ a_name }, a_enabled });
	}

	namespace Presets
	{
		std::string PathFor(std::string_view a_name)
		{
			// Names come from a text box, so strip anything that could walk out
			// of the folder before it reaches the filesystem.
			std::string safe;
			safe.reserve(a_name.size());
			for (const char c : a_name) {
				if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') {
					safe.push_back(c);
				}
			}
			while (!safe.empty() && safe.back() == ' ') {
				safe.pop_back();
			}

			return safe.empty() ? std::string{} : std::format("{}/{}.ini", kPresetDir, safe);
		}

		std::vector<std::string> List()
		{
			std::vector<std::string> names;
			std::error_code          ec;

			if (!std::filesystem::exists(kPresetDir, ec)) {
				return names;
			}

			for (const auto& entry : std::filesystem::directory_iterator{ kPresetDir, ec }) {
				if (entry.is_regular_file(ec) && entry.path().extension() == ".ini") {
					names.push_back(entry.path().stem().string());
				}
			}

			std::sort(names.begin(), names.end());
			return names;
		}

		bool Delete(std::string_view a_name)
		{
			const auto path = PathFor(a_name);
			if (path.empty()) {
				return false;
			}
			std::error_code ec;
			return std::filesystem::remove(path, ec);
		}

		bool Rename(std::string_view a_from, std::string_view a_to)
		{
			const auto from = PathFor(a_from);
			const auto to = PathFor(a_to);
			if (from.empty() || to.empty() || from == to) {
				return false;
			}

			std::error_code ec;
			if (std::filesystem::exists(to, ec)) {
				return false;  // never silently clobber another preset
			}
			std::filesystem::rename(from, to, ec);
			return !ec;
		}
	}

	Settings* Settings::GetSingleton()
	{
		static Settings singleton;
		return std::addressof(singleton);
	}

	void Settings::RestoreDefaults()
	{
		*this = Settings{};
		for (std::size_t i = 0; i < kCategoryCount; ++i) {
			categories[i].enabled = kCategoryDefaultEnabled[i];
			categories[i].colour = kCategoryDefaultColour[i];
			categories[i].outlineOnly = kCategoryDefaultOutline[i];
		}
	}

	void Settings::Load()
	{
		LoadFrom(kIniPath);
	}

	bool Settings::LoadFrom(const std::string& a_path)
	{
		// start from defaults every time so a reload cannot accumulate stale values
		RestoreDefaults();

		Table table;

		// Answers from the installer live as small fragments in their own
		// folder. They are read first, in filename order, so that anything the
		// player later saves to the real INI wins key by key. A preset loaded
		// from the menu is meant to stand on its own, so it skips this.
		const bool live = (a_path == kIniPath);
		if (live) {
			for (const auto& fragment : SetupFragments()) {
				const auto part = ReadIni(fragment.c_str());
				if (part.empty()) {
					continue;
				}
				logger::info("config: setup fragment {} ({} keys)",
					std::filesystem::path{ fragment }.filename().string(), part.size());
				for (const auto& [key, value] : part) {
					table[key] = value;
				}
			}
		}

		const auto own = ReadIni(a_path.c_str());
		for (const auto& [key, value] : own) {
			table[key] = value;
		}

		if (table.empty()) {
			logger::warn("no INI found at {} - running on defaults", a_path);
			return false;
		}
		if (own.empty() && live) {
			logger::info("config: no {} yet - running on defaults plus the installer's answers", kIniPath);
		}

		Get(table, "general", "debug", debug);
		Get(table, "general", "language", language);
		Get(table, "general", "menuAware", menuAware);
		Get(table, "general", "stopOnMenu", stopOnMenu);
		Get(table, "general", "muteInDialogue", muteInDialogue);
		Get(table, "general", "hideGameHud", hideGameHud);
		// NOT through Get: that overload lower-cases, and a Scaleform menu is
		// looked up by an exact, case-sensitive name. "hud menu" finds nothing.
		{
			std::string raw;
			if (Lookup(table, "general", "hideMenus", raw) && !raw.empty()) {
				hideMenus = raw;
			}
		}

		Get(table, "hotkey", "keyboard", keyboard);
		Get(table, "hotkey", "gamepad", gamepad);
		Get(table, "hotkey", "trigger", trigger);
		Get(table, "hotkey", "doubleTapWindow", doubleTapWindow);
		Get(table, "hotkey", "holdTime", holdTime);
		Get(table, "hotkey", "toggle", toggle);
		Get(table, "hotkey", "cooldown", cooldown);

		Get(table, "scan", "radius", radius);
		Get(table, "scan", "maxObjects", maxObjects);
		Get(table, "scan", "ignoreUnnamed", ignoreUnnamed);
		Get(table, "scan", "ignoreHarvested", ignoreHarvested);
		Get(table, "scan", "ignorePlaceholders", ignorePlaceholders);
		Get(table, "scan", "placeholderNames", placeholderNames);
		Get(table, "scan", "spreadAcrossRadius", spreadAcrossRadius);
		Get(table, "scan", "nearBias", nearBias);

		Get(table, "pulse", "sweepTime", sweepTime);
		Get(table, "pulse", "duration", duration);
		Get(table, "pulse", "fadeIn", fadeIn);
		Get(table, "pulse", "fadeOut", fadeOut);

		Get(table, "vision", "throughWalls", throughWalls);
		Get(table, "vision", "edgeFalloff", edgeFalloff);
		Get(table, "vision", "glowStrength", glowStrength);
		Get(table, "vision", "pulseAmplitude", pulseAmplitude);
		Get(table, "vision", "pulseFrequency", pulseFrequency);

		Get(table, "labels", "enable", labelsEnabled);
		Get(table, "labels", "backdrop", labelBackdrop);
		Get(table, "labels", "maxDistance", labelMaxDistance);
		Get(table, "labels", "fontSize", labelFontSize);
		Get(table, "labels", "actorScale", labelActorScale);
		Get(table, "labels", "icons", labelIcons);
		Get(table, "labels", "favourite", favouriteHighlight);
		Get(table, "labels", "favouriteColor", favouriteColour);
		Get(table, "labels", "favouriteScale", favouriteScale);
		Get(table, "labels", "mergeDistance", labelMergeDistance);
		Get(table, "labels", "declutter", labelDeclutter);
		Get(table, "labels", "declutterSteps", labelDeclutterSteps);

		Get(table, "self", "scan", scanPlayer);
		Get(table, "self", "icon", selfIcon);
		Get(table, "self", "color", selfColour);
		Get(table, "self", "scale", selfScale);
		Get(table, "self", "bars", selfBars);
		// Older INIs said this with a bool. Read it first so an existing file
		// keeps behaving, then let the newer key win if it is there.
		{
			bool legacyWhenFull = true;
			if (std::string raw; Lookup(table, "self", "barsWhenFull", raw)) {
				Get(table, "self", "barsWhenFull", legacyWhenFull);
				selfBarsWhen = legacyWhenFull ? ShowWhen::kAlways : ShowWhen::kNotFull;
			}
		}
		Get(table, "self", "barsWhen", selfBarsWhen);
		Get(table, "self", "barWidth", selfBarWidth);
		Get(table, "self", "barHeight", selfBarHeight);
		// The menu's Save used to write these three under [Vitals] while the
		// loader only ever looked in [Self], so a custom bar colour silently
		// reset every round trip. Read the old location first; [Self] wins
		// when both are present, and Save now writes [Self] only.
		Get(table, "vitals", "healthColor", selfHealthColour);
		Get(table, "vitals", "magickaColor", selfMagickaColour);
		Get(table, "vitals", "staminaColor", selfStaminaColour);
		Get(table, "self", "healthColor", selfHealthColour);
		Get(table, "self", "magickaColor", selfMagickaColour);
		Get(table, "self", "staminaColor", selfStaminaColour);
		Get(table, "self", "barPlace", selfBarPlace);
		Get(table, "self", "barShear", selfBarShear);
		Get(table, "self", "barSegments", selfBarSegments);
		Get(table, "self", "barOffsetX", selfBarOffsetX);
		Get(table, "self", "barOffsetY", selfBarOffsetY);
		Get(table, "self", "overheadOffsetX", overheadOffsetX);
		Get(table, "self", "overheadOffsetY", overheadOffsetY);
		Get(table, "self", "barPerspective", selfBarPerspective);
		Get(table, "self", "barGlow", selfBarGlow);
		Get(table, "self", "frameColor", selfBarFrameColour);
		Get(table, "self", "overhead", selfBarsOverhead);
		Get(table, "self", "overheadWhen", selfBarsOverheadWhen);
		Get(table, "player", "level", senseLevel);
		Get(table, "player", "gold", senseGold);
		Get(table, "player", "weight", senseWeight);
		Get(table, "player", "cold", senseCold);
		Get(table, "player", "coldGlobal", coldGlobal);
		Get(table, "player", "coldMax", coldMax);
		Get(table, "player", "levelOthers", levelOthers);
		Get(table, "player", "weaponIcons", weaponIcons);
		Get(table, "player", "raceIcons", raceIcons);
		Get(table, "tracks", "enabled", trailsEnabled);
		Get(table, "tracks", "lifetime", trailLifetime);
		Get(table, "tracks", "names", trailNames);
		Get(table, "tracks", "clearOnTransition", trailsClearOnTransition);
		Get(table, "tracks", "forgetMarked", trailsForgetMarked);
		Get(table, "tracks", "style", trailStyle);
		Get(table, "tracks", "key", trailKey);
		Get(table, "tracks", "gamepad", trailGamepad);
		Get(table, "tracks", "range", trackRange);
		// Legacy first: the two old toggles map onto the reveal rule, and an
		// explicit reveal key written by a newer save wins over both.
		{
			bool legacyOnly = true;
			bool legacySneak = false;
			Get(table, "tracks", "onlyWhileSensing", legacyOnly);
			Get(table, "tracks", "sneakReveals", legacySneak);
			if (!legacyOnly) {
				trailReveal = TrailReveal::kAlways;
			} else if (legacySneak) {
				// The old crouch toggle asked for the crouch to matter; the
				// crouch-and-sense default is that idea grown up.
				trailReveal = TrailReveal::kCrouchSense;
			}
		}
		Get(table, "tracks", "reveal", trailReveal);
		Get(table, "tracks", "toasts", trailToasts);
		Get(table, "tracks", "printScale", trailPrintScale);
		Get(table, "tracks", "printEvery", trailPrintEvery);
		Get(table, "tracks", "autoCapture", trailAutoCapture);
		Get(table, "tracks", "multiMark", multiMark);
		Get(table, "tracks", "aimStyle", aimStyle);
		Get(table, "tracks", "aimColor", aimColour);
		Get(table, "tracks", "wipe", trailWipe);
		Get(table, "tracks", "holdTime", trailHoldTime);
		Get(table, "tracks", "keyMode", trailMode);
		Get(table, "tracks", "markKey", trailMarkKey);
		Get(table, "tracks", "markGamepad", trailMarkGamepad);
		Get(table, "tracks", "markGesture", trailMarkGesture);
		Get(table, "tracks", "showKey", trailShowKey);
		Get(table, "tracks", "showGamepad", trailShowGamepad);
		Get(table, "tracks", "showGesture", trailShowGesture);
		Get(table, "tracks", "wipeKey", trailWipeKey);
		Get(table, "tracks", "wipeGamepad", trailWipeGamepad);
		Get(table, "tracks", "wipeGesture", trailWipeGesture);
		Get(table, "tracks", "deathRelease", markDeathRelease);
		Get(table, "tracks", "deathDelay", markDeathDelay);
		Get(table, "player", "statsPlace", statsPlace);
		Get(table, "self", "hudCorner", selfHudCorner);
		Get(table, "self", "hudShow", selfHudShow);
		Get(table, "self", "hudLinger", selfHudLinger);
		Get(table, "self", "hudFade", selfHudFade);
		Get(table, "self", "hudX", selfHudX);
		Get(table, "self", "hudY", selfHudY);
		Get(table, "self", "hudScale", selfHudScale);
		Get(table, "ammo", "counter", ammoCounter);
		Get(table, "ammo", "anchor", ammoAnchor);
		Get(table, "ammo", "offsetX", ammoOffsetX);
		Get(table, "ammo", "offsetY", ammoOffsetY);
		Get(table, "ammo", "scale", ammoScale);
		Get(table, "ammo", "color", ammoColour);
		Get(table, "ammo", "when", ammoWhen);
		Get(table, "ammo", "fadeIn", ammoFadeIn);
		Get(table, "ammo", "fadeOut", ammoFadeOut);

		Get(table, "vitals", "actors", vitalsActors);
		Get(table, "vitals", "actorsAll", vitalsActorsAll);
		Get(table, "vitals", "hostileOnly", vitalsActorsHostileOnly);
		Get(table, "vitals", "actorsWhen", vitalsActorsWhen);
		Get(table, "vitals", "lostMax", barsLostMax);
		Get(table, "vitals", "lostFx", barsLostFx);
		Get(table, "vitals", "combat", combatBars);
		Get(table, "vitals", "combatAll", combatBarsAll);
		Get(table, "vitals", "combatLinger", combatLinger);
		Get(table, "vitals", "pushTrueHUD", pushTrueHUDAside);

		Get(table, "labels", "follow", labelsFollow);
		Get(table, "labels", "speakerMode", labelSpeakerMode);
		Get(table, "labels", "speakerLift", labelSpeakerLift);

		Get(table, "titles", "show", titlesShow);
		Get(table, "titles", "scale", titleScale);
		Get(table, "titles", "color", titleColour);
		Get(table, "titles", "ruleColor", titleRuleColour);

		Get(table, "arousal", "show", arousalShow);
		Get(table, "arousal", "color", arousalColour);
		Get(table, "arousal", "min", arousalMin);
		Get(table, "arousal", "number", arousalNumber);

		Get(table, "bonds", "promote", ostimPromote);
		Get(table, "bonds", "promoteRank", ostimPromoteRank);
		Get(table, "bonds", "promoteMarriage", ostimPromoteMarriage);
		Get(table, "bonds", "maraAtFull", ostimMaraAtFull);

		Get(table, "ring", "enable", ringEnabled);
		Get(table, "ring", "color", ringColour);
		Get(table, "ring", "thickness", ringThickness);
		Get(table, "ring", "glow", ringGlow);
		Get(table, "ring", "echoes", ringEchoes);
		Get(table, "ring", "echoGap", ringEchoGap);
		Get(table, "ring", "height", ringHeight);
		Get(table, "ring", "lead", ringLead);

		Get(table, "tint", "enable", tintEnabled);
		Get(table, "tint", "useOverride", tintUseOverride);
		Get(table, "tint", "writeCurrent", tintWriteCurrent);
		Get(table, "tint", "useImageSpaceModifier", tintUseImod);
		Get(table, "tint", "postProcess", postProcess);
		Get(table, "tint", "postProcessBackBuffer", postProcessBackBuffer);
		Get(table, "tint", "saturation", tintSaturation);
		Get(table, "tint", "brightness", tintBrightness);
		Get(table, "tint", "contrast", tintContrast);
		Get(table, "tint", "strength", tintStrength);
		Get(table, "tint", "castColor", tintCastColour);
		Get(table, "tint", "castAmount", tintCastAmount);
		Get(table, "tint", "overlay", washEnabled);
		Get(table, "tint", "overlayColor", washColour);
		Get(table, "tint", "overlayStrength", washStrength);
		Get(table, "tint", "overlayFlat", washFlat);

		Get(table, "sound", "enabled", soundEnabled);
		Get(table, "sound", "volume", soundVolume);
		Get(table, "sound", "formID", soundFormID);

		// Older INIs only had a boolean here. Read it first so an existing file
		// keeps behaving, then let the new key win if it is present.
		bool legacyLivingOnly = false;
		Get(table, "categories", "actorLivingOnly", legacyLivingOnly);
		if (legacyLivingOnly) {
			actorFilter = ActorFilter::kLivingOnly;
		}
		Get(table, "categories", "actorFilter", actorFilter);
		Get(table, "categories", "enemiesOnly", actorEnemiesOnly);
		Get(table, "categories", "hideEmpty", hideEmptyContainers);
		// 0.8.1 split one switch into two. Anyone who had the old key on gets
		// both, so the setting they chose keeps meaning what it meant.
		bool legacyDepleted = false;
		Get(table, "categories", "hideDepleted", legacyDepleted);
		if (legacyDepleted) {
			hideDepletedOre = true;
			hideEmptyAsh = true;
		}
		Get(table, "categories", "hideDepletedOre", hideDepletedOre);
		Get(table, "categories", "hideEmptyAsh", hideEmptyAsh);
		Get(table, "categories", "hideStealing", hideStealing);
		Get(table, "categories", "actorByDisposition", actorByDisposition);
		Get(table, "categories", "actorByRelationship", actorByRelationship);
		Get(table, "categories", "rivalColor", rivalColour);
		Get(table, "categories", "loverColor", loverColour);
		Get(table, "categories", "allyColor", allyColour);
		Get(table, "categories", "hostileColor", hostileColour);
		Get(table, "categories", "neutralColor", neutralColour);
		Get(table, "categories", "friendlyColor", friendlyColour);
		Get(table, "categories", "deadColor", deadColour);

		for (std::size_t i = 0; i < kCategoryCount; ++i) {
			const std::string name = kCategoryNames[i];
			Get(table, "categories", name, categories[i].enabled);
			Get(table, "categories", name + "Color", categories[i].colour);
			Get(table, "categories", name + "OutlineOnly", categories[i].outlineOnly);
		}

		// sanity clamps - a bad INI should never be able to hang the game
		radius = std::clamp(radius, 128.0f, 20000.0f);
		maxObjects = std::clamp<std::uint32_t>(maxObjects, 1u, 128u);
		duration = std::clamp(duration, 0.2f, 120.0f);
		sweepTime = std::clamp(sweepTime, 0.0f, 10.0f);
		fadeIn = std::clamp(fadeIn, 0.0f, 10.0f);
		fadeOut = std::clamp(fadeOut, 0.0f, 10.0f);
		edgeFalloff = std::clamp(edgeFalloff, 0.1f, 8.0f);
		glowStrength = std::clamp(glowStrength, 0.0f, 4.0f);
		pulseAmplitude = std::clamp(pulseAmplitude, 0.0f, 1.0f);
		pulseFrequency = std::clamp(pulseFrequency, 0.0f, 20.0f);
		tintSaturation = std::clamp(tintSaturation, 0.0f, 2.0f);
		tintBrightness = std::clamp(tintBrightness, 0.0f, 2.0f);
		tintContrast = std::clamp(tintContrast, 0.0f, 4.0f);
		tintStrength = std::clamp(tintStrength, 0.0f, 1.0f);
		cooldown = std::clamp(cooldown, 0.0f, 30.0f);
		soundVolume = std::clamp(soundVolume, 0.0f, 1.0f);
		combatLinger = std::clamp(combatLinger, 0.0f, 30.0f);
		coldMax = std::clamp(coldMax, 1.0f, 100000.0f);
		trailLifetime = std::clamp(trailLifetime, 10.0f, 300.0f);
		trackRange = std::clamp(trackRange, 500.0f, 10000.0f);
		aimColour &= 0xFFFFFF;
		trailHoldTime = std::clamp(trailHoldTime, 0.2f, 2.0f);
		markDeathDelay = std::clamp(markDeathDelay, 0.0f, 300.0f);
		trailPrintScale = std::clamp(trailPrintScale, 0.5f, 2.5f);
		trailPrintEvery = std::clamp(trailPrintEvery, 1, 4);
		labelMaxDistance = std::clamp(labelMaxDistance, 0.0f, 20000.0f);
		washStrength = std::clamp(washStrength, 0.0f, 1.0f);
		washFlat = std::clamp(washFlat, 0.0f, 1.0f);
		labelFontSize = labelFontSize <= 0.0f ? 0.0f : std::clamp(labelFontSize, 8.0f, 64.0f);
		labelActorScale = std::clamp(labelActorScale, 0.5f, 3.0f);
		labelMergeDistance = std::clamp(labelMergeDistance, 0.0f, 4000.0f);
		labelDeclutterSteps = std::clamp<std::uint32_t>(labelDeclutterSteps, 0u, 24u);
		nearBias = std::clamp(nearBias, 1.0f, 6.0f);
		tintCastAmount = std::clamp(tintCastAmount, 0.0f, 1.0f);
		ringThickness = std::clamp(ringThickness, 0.5f, 12.0f);
		ringGlow = std::clamp(ringGlow, 0.0f, 32.0f);
		ringEchoes = std::clamp<std::uint32_t>(ringEchoes, 1u, 8u);
		ringEchoGap = std::clamp(ringEchoGap, 0.01f, 1.0f);
		ringHeight = std::clamp(ringHeight, -64.0f, 256.0f);
		ringLead = std::clamp(ringLead, 0.5f, 4.0f);
		doubleTapWindow = std::clamp(doubleTapWindow, 0.05f, 2.0f);
		holdTime = std::clamp(holdTime, 0.05f, 5.0f);

		favouriteScale = std::clamp(favouriteScale, 0.5f, 3.0f);
		selfScale = std::clamp(selfScale, 0.5f, 3.0f);
		selfBarWidth = std::clamp(selfBarWidth, 20.0f, 600.0f);
		selfBarHeight = std::clamp(selfBarHeight, 1.0f, 24.0f);
		selfBarShear = std::clamp(selfBarShear, -3.0f, 3.0f);
		selfBarOffsetX = std::clamp(selfBarOffsetX, -600.0f, 600.0f);
		selfBarOffsetY = std::clamp(selfBarOffsetY, -600.0f, 600.0f);
		overheadOffsetX = std::clamp(overheadOffsetX, -600.0f, 600.0f);
		overheadOffsetY = std::clamp(overheadOffsetY, -600.0f, 600.0f);
		selfBarSegments = std::clamp(selfBarSegments, 0, 40);
		selfBarPerspective = std::clamp(selfBarPerspective, 0.0f, 0.6f);
		selfHudLinger = std::clamp(selfHudLinger, 0.3f, 30.0f);
		selfHudFade = std::clamp(selfHudFade, 0.0f, selfHudLinger);
		selfHudScale = std::clamp(selfHudScale, 0.5f, 5.0f);
		ammoOffsetX = std::clamp(ammoOffsetX, -4000.0f, 4000.0f);
		ammoOffsetY = std::clamp(ammoOffsetY, -4000.0f, 4000.0f);
		ammoScale = std::clamp(ammoScale, 0.4f, 5.0f);
		ammoFadeIn = std::clamp(ammoFadeIn, 0.0f, 5.0f);
		ammoFadeOut = std::clamp(ammoFadeOut, 0.0f, 5.0f);
		selfHudX = std::clamp(selfHudX, 0.0f, 4000.0f);
		selfHudY = std::clamp(selfHudY, 0.0f, 4000.0f);
		arousalMin = std::clamp(arousalMin, 0, 100);
		titleScale = std::clamp(titleScale, 0.3f, 1.5f);
		labelSpeakerMode = std::clamp(labelSpeakerMode, 0, 2);
		labelSpeakerLift = std::clamp(labelSpeakerLift, 0.0f, 400.0f);
		ostimPromoteRank = std::clamp(ostimPromoteRank, -4, 4);

		// Marker appearance. Keys look like "Dibella.color", and the table has
		// already lower-cased them, so styles are matched case-insensitively
		// against rule names everywhere.
		markStyles.clear();
		for (const auto& [key, value] : table) {
			constexpr std::string_view kPrefix = "markers.";
			if (!key.starts_with(kPrefix)) {
				continue;
			}

			const auto rest = std::string_view{ key }.substr(kPrefix.size());
			const auto dot = rest.rfind('.');
			if (dot == std::string_view::npos) {
				continue;
			}

			const std::string name{ rest.substr(0, dot) };
			const auto        field = rest.substr(dot + 1);

			auto& style = StyleFor(name, 0xFFFFFF);
			if (field == "enabled") {
				style.enabled = value == "true" || value == "1" || value == "yes";
			} else if (field == "color" || field == "colour") {
				style.colour = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
				style.overrideColour = true;
			} else if (field == "full") {
				style.countFull = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
			} else if (field == "style") {
				const auto lowered = Lower(value);
				for (std::size_t i = 0; i < std::size(kCountStyleNames); ++i) {
					if (lowered == kCountStyleNames[i]) {
						style.countStyle = static_cast<CountStyle>(i);
					}
				}
			}
		}

		for (auto& style : markStyles) {
			style.countFull = std::clamp<std::uint32_t>(style.countFull, 1u, 64u);
		}

		Get(table, "integrations", "hideMissing", hideMissingAddons);

		// [Integrations] - one plain "Name = true" per mod the rule file reads.
		integrations.clear();
		for (const auto& [key, value] : table) {
			constexpr std::string_view kPrefix = "integrations.";
			if (!key.starts_with(kPrefix)) {
				continue;
			}
			const auto name = std::string_view{ key }.substr(kPrefix.size());
			if (name == "hidemissing") {
				continue;
			}
			SetIntegration(name, value == "true" || value == "1" || value == "yes");
		}

		logger::info(
			"config: key={} pad={} radius={} max={} sweepTime={} duration={} throughWalls={} tint={} markerStyles={}",
			keyboard, gamepad, radius, maxObjects, sweepTime, duration, throughWalls, tintEnabled,
			markStyles.size());
		return true;
	}

	bool Settings::Save() const
	{
		return SaveAs(kIniPath);
	}

	bool Settings::SaveAs(const std::string& a_path) const
	{
		{
			// Presets live in their own folder, which may not exist yet.
			std::error_code ec;
			const auto      parent = std::filesystem::path{ a_path }.parent_path();
			if (!parent.empty()) {
				std::filesystem::create_directories(parent, ec);
			}
		}

		std::ofstream file{ a_path, std::ios::trunc };
		if (!file) {
			logger::error("could not open {} for writing", a_path);
			return false;
		}

		const auto boolean = [](bool a_value) { return a_value ? "true" : "false"; };

		file << "; ============================================================================\n";
		file << ";  Scavenger Sense\n";
		file << ";  Written by the in-game menu. Hand edits are fine - anything you change\n";
		file << ";  here is picked up on the next game load, and the menu will show it.\n";
		file << "; ============================================================================\n\n";

		file << "[General]\n\n";
		file << "; Write per-object detail to\n";
		file << "; Documents\\My Games\\Skyrim Special Edition\\SKSE\\ScavengerSense.log\n";
		file << "debug = " << boolean(debug) << "\n\n";
		file << "; Menu language. \"english\", or any name with a matching\n";
		file << "; Data\\SKSE\\Plugins\\ScavengerSense_<name>.ini translation file.\n";
		file << "language = " << language << "\n\n";
		file << "; Stand down while a game menu is open, so nothing we draw or tint\n";
		file << "; ends up on top of it. Turn this off if the sweep ever refuses to\n";
		file << "; run - some HUD mods park a permanent menu on the stack.\n";
		file << "menuAware = " << boolean(menuAware) << "\n\n";
		file << "; End the sweep when a menu opens instead of just hiding it. This\n";
		file << "; also strips the glow off every lit object.\n";
		file << "stopOnMenu = " << boolean(stopOnMenu) << "\n\n";
		file << "; Treat a conversation the same way. Talking does not pause the game,\n";
		file << "; so this has to be asked for by name - but a pulse going off while\n";
		file << "; someone is mid-sentence breaks the scene more than a menu does.\n";
		file << "muteInDialogue = " << boolean(muteInDialogue) << "\n\n";
		file << "; Hide the game's own interface. \"HUD Menu\" is the vanilla one and\n";
		file << "; carries anything drawn into it, moreHUD included. Other mods own\n";
		file << "; their own menus, so add their names to the list below, comma\n";
		file << "; separated, and they go too. A mod drawing through ImGui rather than\n";
		file << "; Scaleform cannot be reached this way at all.\n";
		file << "hideGameHud = " << boolean(hideGameHud) << "\n";
		file << "hideMenus = " << hideMenus << "\n\n\n";

		file << "[Hotkey]\n\n";
		file << "; DirectX scan code of the key that triggers a sweep. Mouse buttons are\n";
		file << "; 256 + button index (256 = LMB, 257 = RMB, 258 = MMB). -1 disables.\n";
		file << "; Scan code list: https://ck.uesp.net/wiki/Input_Script\n";
		file << "keyboard = " << keyboard << "\n\n";
		file << "; Gamepad button, or -1 to disable. Xbox layout, SKSE macro codes:\n";
		file << ";   266 D-Pad Up      267 D-Pad Down   268 D-Pad Left   269 D-Pad Right\n";
		file << ";   270 Start/Menu    271 Back/View    272 Left Stick   273 Right Stick\n";
		file << ";   274 Left Bumper   275 Right Bumper 276 A            277 B\n";
		file << ";   278 X             279 Y            280 Left Trigger 281 Right Trigger\n";
		file << "; The press is not swallowed, so the button still does its vanilla job too.\n";
		file << "gamepad = " << gamepad << "\n\n";
		file << "; How the button has to be pressed:\n";
		file << ";   press  - fires the moment it goes down\n";
		file << ";   double - two taps inside doubleTapWindow\n";
		file << ";   hold   - held down for at least holdTime\n";
		file << "trigger = " << TriggerName(trigger) << "\n\n";
		file << "; Maximum seconds between the two taps in double mode.\n";
		file << "doubleTapWindow = " << doubleTapWindow << "\n\n";
		file << "; Seconds the button must be held in hold mode.\n";
		file << "holdTime = " << holdTime << "\n\n";
		file << "; Pressing the key again while a sweep is running cancels it.\n";
		file << "toggle = " << boolean(toggle) << "\n\n";
		file << "; Minimum seconds between sweeps.\n";
		file << "cooldown = " << cooldown << "\n\n\n";

		file << "[Scan]\n\n";
		file << "; Radius in game units. 1500 is roughly 23 m, 2500 roughly 38 m.\n";
		file << "radius = " << radius << "\n\n";
		file << "; Hard cap on objects per sweep, nearest first. Maximum 128.\n";
		file << "maxObjects = " << maxObjects << "\n\n";
		file << "; Skip objects with no display name (invisible marker activators).\n";
		file << "ignoreUnnamed = " << boolean(ignoreUnnamed) << "\n\n";
		file << "; Skip plants and ore veins you have already harvested.\n";
		file << "ignoreHarvested = " << boolean(ignoreHarvested) << "\n\n";
		file << "; Skip objects wearing the engine's placeholder name - \"This should\n";
		file << "; not be visible.\" and friends. Comma-separated substrings, matched\n";
		file << "; case-insensitively; add localised or modded placeholders here.\n";
		file << "ignorePlaceholders = " << boolean(ignorePlaceholders) << "\n";
		file << "placeholderNames = " << placeholderNames << "\n\n";
		file << "; When more objects qualify than there are slots, spread the picks\n";
		file << "; evenly across the whole radius instead of taking the nearest ones.\n";
		file << "; Without this the wave only covers the few metres around you.\n";
		file << "spreadAcrossRadius = " << boolean(spreadAcrossRadius) << "\n\n";
		file << "; How hard that spread leans toward what is near you.\n";
		file << ";   1.0 = even. Sounds fair, is not: with 450 candidates and 96 slots\n";
		file << ";         it keeps one object in five, and the crate at your feet is\n";
		file << ";         as likely to be dropped as one across the valley.\n";
		file << ";   2.2 = everything within a few paces survives, the far half is\n";
		file << ";         sampled. This is the default.\n";
		file << ";   4.0+ = almost everything near, only a token few at distance.\n";
		file << "nearBias = " << nearBias << "\n\n\n";

		file << "[Pulse]\n\n";
		file << "; Seconds for the wave to travel from the nearest lit object to the\n";
		file << "; furthest one. This is a time, not a speed, so the sweep reads the\n";
		file << "; same whether the objects are spread out or packed into one room.\n";
		file << ";   0 = no wave, everything lights on the same frame.\n";
		file << "sweepTime = " << sweepTime << "\n\n";
		file << "; How long each object stays lit once the wave reaches it.\n";
		file << "duration = " << duration << "\n\n";
		file << "; Seconds spent fading a highlight in and out.\n";
		file << "fadeIn = " << fadeIn << "\n";
		file << "fadeOut = " << fadeOut << "\n\n\n";

		file << "[Vision]\n\n";
		file << "; Draw the glow regardless of depth - silhouettes through geometry.\n";
		file << "; Note this defeats the depth test, not culling: an object the engine\n";
		file << "; never submits still will not show.\n";
		file << "throughWalls = " << boolean(throughWalls) << "\n\n";
		file << "; Higher = thinner rim, lower = the whole object glows.\n";
		file << "edgeFalloff = " << edgeFalloff << "\n\n";
		file << "; Multiplies the category colours. Above 1.0 pushes toward white.\n";
		file << "glowStrength = " << glowStrength << "\n\n";
		file << "; Throb. Amplitude 0 is a steady glow.\n";
		file << "pulseAmplitude = " << pulseAmplitude << "\n";
		file << "pulseFrequency = " << pulseFrequency << "\n\n\n";

		file << "[Labels]\n\n";
		file << "; Draw the object's name over it while it is lit.\n";
		file << "enable = " << boolean(labelsEnabled) << "\n\n";
		file << "; Dark plate behind the text so it stays readable.\n";
		file << "backdrop = " << boolean(labelBackdrop) << "\n\n";
		file << "; Only label objects closer than this, in game units. 0 = no limit.\n";
		file << "; A busy room can otherwise turn into a wall of text.\n";
		file << "maxDistance = " << labelMaxDistance << "\n\n";
		file << "; Text height in pixels. The menu framework only ever bakes one font\n";
		file << "; size and it is sized for menus, so tags are drawn scaled to this.\n";
		file << "; 0 uses the menu size unchanged.\n";
		file << "fontSize = " << labelFontSize << "\n\n";
		file << "; People's tags are drawn this much bigger than an object's, and are\n";
		file << "; placed over the head rather than at the base of the model.\n";
		file << "actorScale = " << labelActorScale << "\n\n";
		file << "; Draw a shape beside a person's name: a triangle for hostile, a\n";
		file << "; circle for neutral, a diamond for an ally, a cross for a corpse.\n";
		file << "; These are drawn as geometry, not glyphs, so no font can lose them.\n";
		file << "icons = " << boolean(labelIcons) << "\n\n";
		file << "; Give things you have favourited a colour of their own and a\n";
		file << "; slightly bigger tag. Favourites are a per-save inventory flag, so\n";
		file << "; this is read fresh from the player on every sweep.\n";
		file << "favourite = " << boolean(favouriteHighlight) << "\n";
		file << "favouriteColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (favouriteColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "favouriteScale = " << favouriteScale << "\n\n";
		file << "; Fold tags with the same name that sit within this many units of\n";
		file << "; each other into one, so a wine cellar reads \"Wine x40\" instead of\n";
		file << "; forty overlapping words. 0 disables. People are never folded -\n";
		file << "; two guards are two guards.\n";
		file << "mergeDistance = " << labelMergeDistance << "\n\n";
		file << "; Push tags that land on top of each other apart. The one nearest\n";
		file << "; the camera keeps its position and the rest step upward, each with\n";
		file << "; a hairline back down to what it names. declutterSteps caps how far\n";
		file << "; a tag may be lifted before it gives up and overlaps anyway.\n";
		file << "; Keep each tag over the thing it names while the sweep runs, instead\n";
		file << "; of leaving it where that thing was standing when the wave reached it.\n";
		file << "; Anchors are recomputed on the main thread about sixty times a second\n";
		file << "; for the hundred-odd things a wave lights, which costs nothing worth\n";
		file << "; measuring - but it is a switch in case you disagree.\n";
		file << "follow = " << boolean(labelsFollow) << "\n\n";
		file << "; What to do about somebody who is talking, for people running a mod\n";
		file << "; that floats subtitles over the speaker's head:\n";
		file << ";   0  nothing\n";
		file << ";   1  hide their tag while they speak - the subtitle already names them\n";
		file << ";   2  lift their tag by speakerLift units so both fit\n";
		file << "speakerMode = " << labelSpeakerMode << "\n";
		file << "speakerLift = " << labelSpeakerLift << "\n\n";
		file << "declutter = " << boolean(labelDeclutter) << "\n";
		file << "declutterSteps = " << labelDeclutterSteps << "\n\n\n";

		file << "[Ring]\n\n";
		file << "; The ground ring that travels outward from your feet with the wave.\n";
		file << "; It is drawn as real world-space points, so it climbs stairs and\n";
		file << "; follows hills rather than sitting flat on the screen.\n";
		file << "enable = " << boolean(ringEnabled) << "\n\n";
		file << "color = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (ringColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n\n";
		file << "; Line width in pixels, and the width of the soft halo behind it.\n";
		file << "thickness = " << ringThickness << "\n";
		file << "glow = " << ringGlow << "\n\n";
		file << "; Trailing rings behind the leading edge, and the gap between them.\n";
		file << "; 1 draws only the leading edge.\n";
		file << "echoes = " << ringEchoes << "\n";
		file << "echoGap = " << ringEchoGap << "\n\n";
		file << "; Lift above the ground, so the line does not fight with the floor.\n";
		file << "height = " << ringHeight << "\n\n";
		file << "; How much further than the furthest lit object the ring travels.\n";
		file << "; Above 1.0 it keeps going past the last highlight, which reads as\n";
		file << "; the pulse carrying on into the distance.\n";
		file << "lead = " << ringLead << "\n\n\n";

		file << "[Tint]\n\n";
		file << "; Desaturate and darken the world while the sweep runs.\n";
		file << "; This drives the image space override the tonemapper reads, so it is\n";
		file << "; a real desaturation and it survives Community Shaders.\n";
		file << "enable = " << boolean(tintEnabled) << "\n\n";
		file << "; Two ways in, both on by default. They carry identical values, so\n";
		file << "; there is no cost to pushing on both, and which one actually reaches\n";
		file << "; the frame depends on what owns the tonemapper.\n";
		file << ";   useOverride  - ImageSpaceManager's spare override pointer\n";
		file << ";   writeCurrent - write the live image space, restoring it after\n";
		file << "useOverride = " << boolean(tintUseOverride) << "\n";
		file << "writeCurrent = " << boolean(tintWriteCurrent) << "\n\n";
		file << "; Old route: trigger the IMAD record shipped in the esp instead.\n";
		file << "; Only useful under ENB presets that ignore the cinematic params.\n";
		file << "useImageSpaceModifier = " << boolean(tintUseImod) << "\n\n";
		file << "; Run our own full-screen shader pass after everything else in the\n";
		file << "; frame. This is the only route that works under Community Shaders,\n";
		file << "; because nothing runs after it to undo the result. It also takes\n";
		file << "; over the vignette, doing it properly rather than with four\n";
		file << "; gradient quads. Off until you ask for it: it is the one part of\n";
		file << "; this mod that talks to the graphics pipeline directly. Any failure\n";
		file << "; disables it for the session and says why in the log.\n";
		file << "postProcess = " << boolean(postProcess) << "\n\n";
		file << "; Multipliers applied to the current image space. 1.0 leaves alone.\n";
		file << "; saturation 0.0 is full greyscale.\n";
		file << "saturation = " << tintSaturation << "\n";
		file << "brightness = " << tintBrightness << "\n";
		file << "contrast = " << tintContrast << "\n\n";
		file << "; Overall blend of the effect, 0.0 to 1.0.\n";
		file << "strength = " << tintStrength << "\n\n";
		file << "; Colour graded into the image while sensing, and how much of it.\n";
		file << "castColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (tintCastColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "castAmount = " << tintCastAmount << "\n\n";
		file << "; Composited overlay drawn on the finished frame. Mostly a vignette:\n";
		file << "; overlayFlat is the share of the strength spent as an even sheet of\n";
		file << "; colour, the rest darkens the edges. All-flat looks like cellophane.\n";
		file << "overlay = " << boolean(washEnabled) << "\n";
		file << "overlayColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (washColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "overlayStrength = " << washStrength << "\n";
		file << "overlayFlat = " << washFlat << "\n\n\n";

		file << "[Self]\n\n";
		file << "; Tag yourself along with everything else. The sweep starts where you\n";
		file << "; are standing, so your own tag appears on the first frame of it.\n";
		file << "; You are never given a highlight - a glow around the camera is no use\n";
		file << "; to anybody, and it would spend one of the 128 shader slots.\n";
		file << "scan = " << boolean(scanPlayer) << "\n\n";
		file << "; A shape beside your name saying what you currently are: a standing\n";
		file << "; figure while mortal, a pair of fangs while vampiric, three claw marks\n";
		file << "; while a werewolf or in vampire lord form.\n";
		file << "icon = " << boolean(selfIcon) << "\n\n";
		file << "color = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (selfColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "scale = " << selfScale << "\n\n";
		file << "; Your health, magicka and stamina as three thin bars under your\n";
		file << "; name, for as long as a sweep lasts. Only yours - a bar over every\n";
		file << "; person in the radius is a wall of colour, and TrueHUD already\n";
		file << "; covers anyone you are fighting. Pairs with playing the game with\n";
		file << "; its own HUD hidden: sweep to check yourself.\n";
		file << "bars = " << boolean(selfBars) << "\n";
		file << "; always, change (a few seconds after a value moves) or notfull.\n";
		file << "barsWhen = " << kShowWhenNames[static_cast<std::size_t>(selfBarsWhen)] << "\n";
		file << "barWidth = " << selfBarWidth << "\n";
		file << "barHeight = " << selfBarHeight << "\n";
		file << "; below, above, left or right of your name. Left and right stand the\n";
		file << "; bars upright and fill them from the bottom.\n";
		file << "barPlace = " << kBarPlaceNames[static_cast<std::size_t>(selfBarPlace)] << "\n";
		file << "; Slant, as a multiple of thickness. 0 is a plain rectangle.\n";
		file << "barShear = " << selfBarShear << "\n";
		file << "; Notches cut across the bar. 0 draws it smooth.\n";
		file << "barSegments = " << selfBarSegments << "\n";
		file << "; Nudges in pixels, positive X right, positive Y down. barOffset\n";
		file << "; moves the bars hanging off a name tag; overheadOffset moves the\n";
		file << "; no-sweep stacks (combat bars and the over-your-head ones).\n";
		file << "barOffsetX = " << selfBarOffsetX << "\n";
		file << "barOffsetY = " << selfBarOffsetY << "\n";
		file << "overheadOffsetX = " << overheadOffsetX << "\n";
		file << "overheadOffsetY = " << overheadOffsetY << "\n";
		file << "; How far each row is set back from the one before, so the stack\n";
		file << "; reads as angled away rather than painted flat. 0 stacks them square.\n";
		file << "barPerspective = " << selfBarPerspective << "\n";
		file << "barGlow = " << boolean(selfBarGlow) << "\n";
		file << "; One set of colours for every bar drawn anywhere - yours, the corner\n";
		file << "; readout, other people's.\n";
		file << "healthColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (selfHealthColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "magickaColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (selfMagickaColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "staminaColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (selfStaminaColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		file << "frameColor = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (selfBarFrameColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";

		file << "; The same stack over your own head with no sweep running, third\n";
		file << "; person only - the player's answer to the combat bars.\n";
		file << "overhead = " << boolean(selfBarsOverhead) << "\n";
		file << "; always, change (a few seconds after a value moves) or notfull.\n";
		file << "overheadWhen = " << kShowWhenNames[static_cast<std::size_t>(selfBarsOverheadWhen)] << "\n";
		file << "; A readout pinned to a corner of the screen, drawn whether or not a\n";
		file << "; sweep is running: off, topleft, topright, bottomleft, bottomright.\n";
		file << "hudCorner = " << kCornerNames[static_cast<std::size_t>(selfHudCorner)] << "\n";
		file << "; always, change (appears for a few seconds when a value moves), or\n";
		file << "; notfull (only what is below maximum).\n";
		file << "hudShow = " << kShowWhenNames[static_cast<std::size_t>(selfHudShow)] << "\n";
		file << "hudLinger = " << selfHudLinger << "\n";
		file << "hudFade = " << selfHudFade << "\n";
		file << "hudX = " << selfHudX << "\n";
		file << "hudY = " << selfHudY << "\n";
		file << "hudScale = " << selfHudScale << "\n\n\n";

		file << "[Ammo]\n\n";
		file << "; How much of the drawn ammunition is left, hung in the world rather\n";
		file << "; than pinned to a corner. Shows only with a bow or crossbow out.\n";
		file << "counter = " << boolean(ammoCounter) << "\n";
		file << "; Where it hangs: body, head or bow.\n";
		file << "anchor = " << kAmmoAnchorNames[static_cast<std::size_t>(ammoAnchor)] << "\n";
		file << "; Nudge in screen pixels, applied after the anchor is projected.\n";
		file << "offsetX = " << ammoOffsetX << "\n";
		file << "offsetY = " << ammoOffsetY << "\n";
		file << "scale = " << ammoScale << "\n";
		file << "; When it is worth drawing: always, drawn (only while the bow is\n";
		file << "; actually being pulled), or sensing (only during a sweep).\n";
		file << "when = " << kAmmoWhenNames[static_cast<std::size_t>(ammoWhen)] << "\n";
		file << "; Its own fade, in seconds. Zero on either side snaps.\n";
		file << "fadeIn = " << ammoFadeIn << "\n";
		file << "fadeOut = " << ammoFadeOut << "\n";
		file << "color = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			<< (ammoColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n\n\n";


		file << "[Player]\n\n";
		file << "; What the sense reads off you beyond the three vitals, drawn as a\n";
		file << "; stats row under the corner readout.\n";
		file << "level = " << boolean(senseLevel) << "\n";
		file << "gold = " << boolean(senseGold) << "\n";
		file << "weight = " << boolean(senseWeight) << "\n";
		file << "cold = " << boolean(senseCold) << "\n";
		file << "; Editor ID of the global variable that says how cold you are, and\n";
		file << "; the value that counts as fully frozen. Survival Mode's global is\n";
		file << "; the default; the row hides itself when it does not exist.\n";
		file << "coldGlobal = " << coldGlobal << "\n";
		file << "coldMax = " << coldMax << "\n";
		file << "; Their level in parentheses after the name, enemies included.\n";
		file << "levelOthers = " << boolean(levelOthers) << "\n";
		file << "; Tag icons follow the drawn weapon - yours and theirs - instead of\n";
		file << "; the relationship shape. The colour still says friend or foe.\n";
		file << "weaponIcons = " << boolean(weaponIcons) << "\n";
		file << "; A racial emblem beside people's tags and on your level entry,\n";
		file << "; with a fang or paw mark after it for vampires and werewolves.\n";
		file << "raceIcons = " << boolean(raceIcons) << "\n";
		file << "; Where the stats row lives: bars (riding the over-head stack),\n";
		file << "; corner (under the corner readout), or both.\n";
		file << "statsPlace = " << kStatsPlaceNames[static_cast<std::size_t>(statsPlace)] << "\n\n\n";

		file << "[Tracks]\n\n";
		file << "; Breadcrumb trails behind anyone the sense has touched - sweep-lit\n";
		file << "; or fought - as chevrons on the ground pointing the way they went,\n";
		file << "; fading with age. Recording starts at first contact; nobody's past\n";
		file << "; is known before the sense saw them.\n";
		file << "enabled = " << boolean(trailsEnabled) << "\n";
		file << "; Seconds a mark survives.\n";
		file << "lifetime = " << trailLifetime << "\n";
		file << "; A small label at the fresh end - whose footprints these are.\n";
		file << "names = " << boolean(trailNames) << "\n";
		file << "; Marks from another place are nonsense in this one; cleared on\n";
		file << "; interior/worldspace transitions unless told otherwise.\n";
		file << "clearOnTransition = " << boolean(trailsClearOnTransition) << "\n";
		file << "; The wipe spares a marked quarry unless this says otherwise.\n";
		file << "forgetMarked = " << boolean(trailsForgetMarked) << "\n";
		file << "; chevrons or footprints.\n";
		file << "style = " << kTrailStyleNames[static_cast<std::size_t>(trailStyle)] << "\n";
		file << "; A key of its own: press to hide or show every trail; press while\n";
		file << "; aiming at somebody to start or stop tracking them.\n";
		file << "key = " << trailKey << "\n";
		file << "gamepad = " << trailGamepad << "\n";
		file << "; How far the mark reaches, in units - hunting picks a deer across\n";
		file << "; a valley, far beyond the crosshair's activate range.\n";
		file << "range = " << trackRange << "\n";
		file << "; When trails show, which is also when the key works: crouchsense\n";
		file << "; (a sweep opened while crouched reads the ground until it ends),\n";
		file << "; sense (any sweep), crouch (crouching alone), or always. Marked\n";
		file << "; quarry stay visible regardless; recording never stops.\n";
		file << "reveal = " << kTrailRevealNames[static_cast<std::size_t>(trailReveal)] << "\n";
		file << "; The corner notes - Tracking so-and-so, All trails wiped.\n";
		file << "toasts = " << boolean(trailToasts) << "\n";
		file << "; Footprint style only: pad size scale, and how many recorded\n";
		file << "; marks lie between drawn prints.\n";
		file << "printScale = " << trailPrintScale << "\n";
		file << "printEvery = " << trailPrintEvery << "\n";
		file << "; On arriving anywhere, open a recording window for everyone there\n";
		file << "; - the sense remembers the room from the moment you entered.\n";
		file << "autoCapture = " << boolean(trailAutoCapture) << "\n";
		file << "; One quarry at a time unless enabled; multiple marks each take\n";
		file << "; their own colour, trail and sweep glow agreeing.\n";
		file << "multiMark = " << boolean(multiMark) << "\n";
		file << "; The marking sign: corners, ring or chevron, and its colour when\n";
		file << "; the target is not already marked.\n";
		file << "aimStyle = " << kAimStyleNames[static_cast<std::size_t>(aimStyle)] << "\n";
		file << std::format("aimColor = 0x{:06X}\n", aimColour & 0xFFFFFF);
		file << "; Which gesture wipes everything: hold, doubletap or off - and how\n";
		file << "; long a hold must last.\n";
		file << "wipe = " << kTrailWipeNames[static_cast<std::size_t>(trailWipe)] << "\n";
		file << "holdTime = " << trailHoldTime << "\n";
		file << "; single = one trail key does it all by context; multi = a key per\n";
		file << "; action, each with its own gesture (press, double or hold);\n";
		file << "; allinone = everything on the sweep key - double tap sweeps, a\n";
		file << "; lone press marks, a hold wipes.\n";
		file << "keyMode = " << kTrailKeyModeNames[static_cast<std::size_t>(trailMode)] << "\n";
		file << "markKey = " << trailMarkKey << "\n";
		file << "markGamepad = " << trailMarkGamepad << "\n";
		file << "markGesture = " << kTriggerNames[static_cast<std::size_t>(trailMarkGesture)] << "\n";
		file << "showKey = " << trailShowKey << "\n";
		file << "showGamepad = " << trailShowGamepad << "\n";
		file << "showGesture = " << kTriggerNames[static_cast<std::size_t>(trailShowGesture)] << "\n";
		file << "wipeKey = " << trailWipeKey << "\n";
		file << "wipeGamepad = " << trailWipeGamepad << "\n";
		file << "wipeGesture = " << kTriggerNames[static_cast<std::size_t>(trailWipeGesture)] << "\n";
		file << "; A dead quarry slips the mark after this many seconds.\n";
		file << "deathRelease = " << boolean(markDeathRelease) << "\n";
		file << "deathDelay = " << markDeathDelay << "\n\n\n";

		file << "[Vitals]\n\n";
		file << "; The same bars over other people, for the length of a sweep only.\n";
		file << "; Never persistent - a bar over everyone all the time is what a\n";
		file << "; combat HUD mod is for. What this gives instead is every person a\n";
		file << "; wave reached, through walls, across the whole radius, at once.\n";
		file << "actors = " << boolean(vitalsActors) << "\n";
		file << "; false draws health only.\n";
		file << "actorsAll = " << boolean(vitalsActorsAll) << "\n";
		file << "; Only people hostile to you. Read from the engine's own hostility\n";
		file << "; check rather than a faction list, so a modded enemy counts too.\n";
		file << "; Rivals - people who dislike you but have not drawn - do not.\n";
		file << "hostileOnly = " << boolean(vitalsActorsHostileOnly) << "\n";
		file << "; always, change (only someone just hurt) or notfull.\n";
		file << "actorsWhen = " << kShowWhenNames[static_cast<std::size_t>(vitalsActorsWhen)] << "\n";
		file << "; Draw the ceiling a survival mod has taken away as a dead zone at\n";
		file << "; the end of the bar, instead of rescaling and pretending otherwise.\n";
		file << "lostMax = " << boolean(barsLostMax) << "\n";
		file << "; Frost over lost health, smoke over lost magicka and stamina.\n";
		file << "lostFx = " << boolean(barsLostFx) << "\n";
		file << "; In combat, anyone you have hit keeps bars over their head until\n";
		file << "; the fight ends. If you run TrueHUD, leave this off - its\n";
		file << "; recent-damage bars already cover it.\n";
		file << "combat = " << boolean(combatBars) << "\n";
		file << "; Magicka and stamina on the combat bars too. Its own switch, not\n";
		file << "; actorsAll: mid-fight can want different information than a sweep.\n";
		file << "combatAll = " << boolean(combatBarsAll) << "\n";
		file << "; Seconds the combat bars outlive the fight, and how long a fresh\n";
		file << "; corpse keeps its drained bar.\n";
		file << "combatLinger = " << combatLinger << "\n";
		file << "; Whenever this mod draws bars over somebody, ask TrueHUD to dismiss\n";
		file << "; its own bar for them, so the two never stack. Does nothing when\n";
		file << "; TrueHUD is not installed.\n";
		file << "pushTrueHUD = " << boolean(pushTrueHUDAside) << "\n\n\n";

		file << "[Titles]\n\n";
		file << "; A small word above the name - Jarl, Blacksmith, Thane of Whiterun -\n";
		file << "; saying what someone is to Skyrim rather than what they are to you.\n";
		file << "; Who gets which title is set in ScavengerSense_titles.ini.\n";
		file << "show = " << boolean(titlesShow) << "\n\n";
		file << "; Size of the title as a share of the name below it.\n";
		file << "scale = " << titleScale << "\n\n";
		file << "; Let each title use the colour its own rule asks for. Off makes them\n";
		file << "; all the one colour below, which is calmer in a crowd.\n";
		file << "ruleColor = " << boolean(titleRuleColour) << "\n";
		file << "color = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (titleColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n\n\n";

		file << "[Arousal]\n\n";
		file << "; Reads SLO Aroused NG, if you have it. A small flame on the right of\n";
		file << "; the tag, filling from nothing at 0 to full at 100.\n";
		file << "show = " << boolean(arousalShow) << "\n\n";
		file << "; Hide the flame below this figure, so people who are simply going\n";
		file << "; about their day do not all carry one.\n";
		file << "min = " << arousalMin << "\n\n";
		file << "; Print the figure beside the flame as well.\n";
		file << "number = " << boolean(arousalNumber) << "\n\n";
		file << "color = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
			 << (arousalColour & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n\n\n";

		file << "[Bonds]\n\n";
		file << "; THIS SECTION WRITES TO YOUR SAVE. Everything else in this file only\n";
		file << "; changes what you see; these change the game. A relationship rank is\n";
		file << "; kept by the engine on the NPC's base form and stored in the save, so\n";
		file << "; it survives uninstalling this mod and there is no undo. The previous\n";
		file << "; value is written to ScavengerSense.log before anything is changed.\n\n";
		file << "; Raise the relationship when a marker's tally reaches full.\n";
		file << "promote = " << boolean(ostimPromote) << "\n\n";
		file << "; Which rank to raise to. The game's own ladder:\n";
		file << ";   4 lover      also grants a 500 gold free-theft allowance\n";
		file << ";   3 ally       what a permanent follower gets\n";
		file << ";   2 confidant\n";
		file << ";   1 friend     the gentlest change that still meets the marriage gate\n";
		file << "; Only ever raised, never lowered - the rank is one shared byte per\n";
		file << "; pair and half the mods in your load order write to it.\n";
		file << "promoteRank = " << ostimPromoteRank << "\n\n";
		file << "; Also add them to the vanilla marriage candidate faction. This one is\n";
		file << "; held on the reference rather than on a record, so it is reversible.\n";
		file << "; Marriage needs this AND a rank of at least 1 AND a voice type the\n";
		file << "; wedding dialogue covers, so it is not a guarantee on its own.\n";
		file << "promoteMarriage = " << boolean(ostimPromoteMarriage) << "\n\n";
		file << "; Purely a picture: swap the marker for its `iconFull` once the tally\n";
		file << "; is full - a Dibella sigil becoming a Mara one. Writes nothing.\n";
		file << "maraAtFull = " << boolean(ostimMaraAtFull) << "\n\n\n";

		file << "[Sound]\n\n";
		file << "; The chime played when a sweep starts. The sound itself lives at\n";
		file << "; Sound\\FX\\ScavengerSense\\sweep.wav - overwrite that file with any\n";
		file << "; 16-bit PCM wav to make it yours.\n";
		file << "enabled = " << boolean(soundEnabled) << "\n";
		file << "volume = " << soundVolume << "\n\n";
		file << "; Raw form ID of a sound descriptor to play INSTEAD of the shipped\n";
		file << "; chime, 0 uses the chime.\n";
		file << "; Examples: 0x000C7A54 = MAGDetectLifeCast, 0x0003F925 = UIMenuOK\n";
		file << "formID = 0x" << std::hex << std::uppercase << soundFormID << std::dec << std::nouppercase << "\n\n\n";

		file << "[Categories]\n\n";
		file << "; Which kinds of object to light, and what colour each one glows (0xRRGGBB).\n\n";
		file << "; Which actors to sense:\n";
		file << ";   all    - anyone, corpses included. Corpses are lootable.\n";
		file << ";   living - only the ones still breathing\n";
		file << ";   dead   - only the bodies, for finding a kill in tall grass\n";
		file << "actorFilter = " << ActorFilterName(actorFilter) << "\n\n";
		file << "; Only sense people who mean you harm: hostile by nature, or hostile\n";
		file << "; because of something you did - anyone you have struck counts.\n";
		file << "; Corpses stay with actorFilter above.\n";
		file << "enemiesOnly = " << boolean(actorEnemiesOnly) << "\n\n";
		file << "; An empty chest is an answer, not a find.\n";
		file << "hideEmpty = " << boolean(hideEmptyContainers) << "\n";
		file << "; A mined-out ore vein is scenery wearing a resource's face.\n";
		file << "hideDepletedOre = " << boolean(hideDepletedOre) << "\n";
		file << "; An ash pile somebody has already picked clean.\n";
		file << "hideEmptyAsh = " << boolean(hideEmptyAsh) << "\n";
		file << "; Nothing whose taking would be theft lights or gets a tag.\n";
		file << "hideStealing = " << boolean(hideStealing) << "\n\n";
		file << "; Colour people by what they are to you rather than by the fact that\n";
		file << "; they are people. \"Someone is there\" is much less useful than\n";
		file << "; \"someone is there and they want to kill you\". This overrides the\n";
		file << "; actorColor below, for the glow and the name tag alike.\n";
		file << "actorByDisposition = " << boolean(actorByDisposition) << "\n";
		const auto hex = [&file](const char* a_key, std::uint32_t a_rgb) {
			file << a_key << " = 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
				 << (a_rgb & 0xFFFFFF) << std::dec << std::nouppercase << std::setfill(' ') << "\n";
		};
		file << "; Colour by the relationship record as well, so a rival reads\n";
		file << "; differently from a stranger even when nobody has drawn a weapon.\n";
		file << "actorByRelationship = " << boolean(actorByRelationship) << "\n";
		hex("hostileColor", hostileColour);
		hex("neutralColor", neutralColour);
		hex("friendlyColor", friendlyColour);
		hex("rivalColor", rivalColour);
		hex("loverColor", loverColour);
		hex("allyColor", allyColour);
		hex("deadColor", deadColour);
		file << "\n";
		file << "; <name>OutlineOnly draws just the rim with no interior glow, which\n";
		file << "; reads as a clean silhouette and keeps overlapping shapes separate.\n\n";
		for (std::size_t i = 0; i < kCategoryCount; ++i) {
			file << kCategoryNames[i] << " = " << boolean(categories[i].enabled) << "\n";
			file << kCategoryNames[i] << "Color = 0x"
				 << std::hex << std::uppercase << std::setfill('0') << std::setw(6)
				 << (categories[i].colour & 0xFFFFFF)
				 << std::dec << std::nouppercase << std::setfill(' ') << "\n";
			file << kCategoryNames[i] << "OutlineOnly = " << boolean(categories[i].outlineOnly) << "\n\n";
		}

		if (!markStyles.empty()) {
			file << "[Markers]\n\n";
			file << "; Appearance of the rules in ScavengerSense_marks.ini, matched by\n";
			file << "; name. The rule file decides who a marker applies to; this decides\n";
			file << "; what it looks like, so the menu can edit one without rewriting\n";
			file << "; the other - and so a preset carries your marker styling with it.\n";
			file << ";   style = number | fill | hidden\n";
			file << ";   full  = the tally at which a filling icon is completely full\n\n";
			for (const auto& style : markStyles) {
				file << style.name << ".enabled = " << boolean(style.enabled) << "\n";
				if (style.overrideColour) {
					file << style.name << ".color = 0x" << std::hex << std::uppercase
						 << std::setfill('0') << std::setw(6) << (style.colour & 0xFFFFFF)
						 << std::dec << std::nouppercase << std::setfill(' ') << "\n";
				}
				file << style.name << ".style = " << CountStyleName(style.countStyle) << "\n";
				file << style.name << ".full = " << style.countFull << "\n\n";
			}
		}

		if (!integrations.empty()) {
			file << "[Integrations]\n\n";
			file << "; One switch per mod the rule file reads. A rule that reads another\n";
			file << "; mod's records is a bridge to that mod, so it is turned off here by\n";
			file << "; the mod's name rather than one rule at a time. Anything not listed\n";
			file << "; is on, so a rule file that names a new integration works straight\n";
			file << "; away.\n\n";
			file << "hideMissing = " << boolean(hideMissingAddons) << "\n\n";
			for (const auto& entry : integrations) {
				file << entry.name << " = " << boolean(entry.enabled) << "\n";
			}
			file << "\n";
		}

		logger::info("settings written to {}", a_path);
		return true;
	}
}
