#include "../src/Config.h"
#include <iomanip>
#include <iostream>
#include <map>

// A headless adapter around the production loader for installer/preset checks.
// Its working directory is a temporary, isolated Data tree, never the live game.
int main(int argc, char** argv)
{
	spdlog::set_level(spdlog::level::off);  // keep stdout machine-readable
	SS::Settings s;
	if (argc == 3 && std::string_view(argv[1]) == "--preset") {
		if (!s.LoadFrom(argv[2])) { return 1; }
	} else if (argc == 1) { s.Load(); }
	else { return 2; }
	const auto boolean = [](bool value) { return value ? "true" : "false"; };
	const auto number = [](float value) { return std::to_string(value); };
	const auto combat = s.combatBarsWhen == SS::CombatBarsWhen::kStruck ? "struck" :
		s.combatBarsWhen == SS::CombatBarsWhen::kFighting ? "fighting" : "aimed";
	const auto track = s.trailMode == SS::TrailKeyMode::kSingle ? "single" :
		s.trailMode == SS::TrailKeyMode::kMulti ? "multi" : "allinone";
	const std::map<std::string, std::string> values{
		{"general.language",s.language}, {"general.hidegamehud",boolean(s.hideGameHud)},
		{"effects.screenenabled",boolean(s.screenEffectsEnabled)}, {"effects.screenstrength",number(s.screenEffectsStrength)},
		{"vision.glowenabled",boolean(s.glowEnabled)}, {"vision.glowstrength",number(s.glowStrength)},
		{"labels.smoothingactorsonly",boolean(s.anchorActorsOnly)}, {"labels.smoothing",number(s.anchorSmoothing)},
		{"labels.enable",boolean(s.labelsEnabled)}, {"labels.icons",boolean(s.labelIcons)},
		{"tint.enable",boolean(s.tintEnabled)}, {"tint.overlay",boolean(s.washEnabled)},
		{"tint.brightness",number(s.tintBrightness)}, {"tint.contrast",number(s.tintContrast)},
		{"tint.strength",number(s.tintStrength)}, {"tint.saturation",number(s.tintSaturation)},
		{"tint.overlaystrength",number(s.washStrength)}, {"categories.actor",boolean(s.categories[static_cast<unsigned>(SS::Category::kActor)].enabled)},
		{"categories.actorfilter",SS::ActorFilterName(s.actorFilter)}, {"tracks.enabled",boolean(s.trailsEnabled)},
		{"tracks.keymode",track}, {"hotkey.trigger",SS::TriggerName(s.trigger)}, {"hotkey.holdtime",number(s.holdTime)},
		{"self.overhead",boolean(s.selfBarsOverhead)}, {"vitals.actors",boolean(s.vitalsActors)},
		{"vitals.combat",boolean(s.combatBars)}, {"vitals.combatwhen",combat}, {"sound.enabled",boolean(s.soundEnabled)},
		{"player.weaponicons",boolean(s.weaponIcons)}, {"player.weaponenchant",boolean(s.weaponEnchant)},
		{"hud.hidecompass",boolean(s.hideCompass)}, {"hud.hidemessages",boolean(s.hideMessages)},
		{"hud.hidesubtitles",boolean(s.hideSubtitles)}, {"hud.hidequestupdates",boolean(s.hideQuestUpdates)}
	};
	std::cout << '{';
	bool first = true;
	for (const auto& [key,value] : values) {
		if (!first) { std::cout << ','; } first = false;
		std::cout << std::quoted(key) << ':' << std::quoted(value);
	}
	std::cout << "}\n";
	return 0;
}
