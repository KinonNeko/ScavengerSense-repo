#include "GameMenus.h"

#include "Config.h"

#include <cmath>
#include <sstream>

namespace SS
{
	namespace
	{
		// Menus that are open all the time or belong to normal play. These are
		// checked first, before the flags, because a couple of them do carry
		// flags we would otherwise treat as blocking.
		constexpr std::string_view kNeverBlocking[] = {
			"HUD Menu",
			"Cursor Menu",
			"Fader Menu",
			"Loading Menu",
			"Overlay Menu",
			"Overlay Interaction Menu",
			"LoadWaitSpinner",
			"Top Menu",
			"Compass",
			"WSEnemyMeters",
			"WSDebugOverlay",
			"WSActivateRollover",
			"TitleSequence Menu"
		};

		[[nodiscard]] bool NeverBlocking(std::string_view a_name)
		{
			return std::ranges::find(kNeverBlocking, a_name) != std::ranges::end(kNeverBlocking);
		}

		// Talking to someone does not pause the game and carries none of the
		// flags we test, so it has to be named. It matters more than most of
		// them: a conversation is the one moment the game is asking you to look
		// at a face, and a sonar ring going off across the room during it is
		// exactly the wrong thing.
		constexpr std::string_view kDialogue = "Dialogue Menu";

		// Keep the parent movie alive: hiding it would also hide any compass,
		// messages or subtitles that the player chose to retain. Suppression
		// operates on individual child clips and is reasserted after menu changes.
		constexpr auto kHudBase = "_root.HUDMovieBaseInstance";

		// The activation prompt, in pieces: the name line, the key glyph, the
		// value/weight line and the bar drawn behind that line. The glyph's
		// across is not ours to set: the HUD's script re-pins it beside the
		// name on every crosshair update, so it follows the name instead.
		struct RolloverPart
		{
			const char* path;
			bool        acrossIsOurs;
		};
		constexpr RolloverPart kRolloverParts[] = {
			{ "_root.HUDMovieBaseInstance.RolloverNameInstance", true },
			{ "_root.HUDMovieBaseInstance.ActivateButton_tf", false },
			{ "_root.HUDMovieBaseInstance.RolloverInfoInstance", true },
			{ "_root.HUDMovieBaseInstance.GrayBarInstance", true },
		};
		constexpr std::size_t kName = 0, kButton = 1, kInfo = 2, kGrayBar = 3;

		[[nodiscard]] RE::GFxMovie* HudMovie()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			return menu && menu->uiMovie ? menu->uiMovie.get() : nullptr;
		}

	}

	GameMenus* GameMenus::GetSingleton()
	{
		static GameMenus singleton;
		return std::addressof(singleton);
	}

	void GameMenus::Install()
	{
		if (_installed) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			logger::warn("menus: no UI singleton, cannot watch menu state");
			return;
		}

		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
		_installed = true;
		logger::info("menus: watching menu open/close");
	}

	void GameMenus::LastWord()
	{
		if (!_installed || _lastWord) {
			return;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		// Remove and add again: the source appends, so this lands last.
		ui->RemoveEventSink<RE::MenuOpenCloseEvent>(this);
		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
		_lastWord = true;
		logger::info("menus: sink moved to the end of the list - the hide is the last word on a menu change");
	}

	bool GameMenus::Classify(std::string_view a_name, std::string& a_why) const
	{
		if (NeverBlocking(a_name)) {
			a_why = "always-open HUD menu";
			return false;
		}

		if (a_name == kDialogue) {
			const bool mute = Settings::GetSingleton()->muteInDialogue;
			a_why = mute ? "conversation" : "conversation, muting disabled";
			return mute;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			a_why = "no UI singleton";
			return false;
		}

		// Ask the menu what it is rather than guessing from its name. A name
		// blacklist is the wrong way round in a big load order: every HUD widget
		// mod that opens a permanent overlay menu would land on the blocking
		// side of it and pin us down forever. Flags cannot go stale that way.
		const auto menu = ui->GetMenu(a_name);
		if (!menu) {
			// The menu is not on the stack yet when the opening event fires for
			// some menus. Unknown means not blocking - failing open keeps a
			// mystery menu from disabling the mod.
			a_why = "not on the menu stack";
			return false;
		}

		const bool pauses = menu->PausesGame();
		const bool app = menu->ApplicationMenu();
		const bool modal = menu->Modal();

		a_why = std::format("pauses={} app={} modal={}", pauses, app, modal);
		return pauses || app || modal;
	}

	RE::BSEventNotifyControl GameMenus::ProcessEvent(
		const RE::MenuOpenCloseEvent*             a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const std::string name{ a_event->menuName.c_str() ? a_event->menuName.c_str() : "" };
		if (name.empty()) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto* settings = Settings::GetSingleton();

		std::string why;
		const bool  blocks = settings->menuAware && Classify(name, why);

		bool changed = false;
		bool nowBlocking = false;
		{
			std::scoped_lock guard{ _lock };
			const auto before = _open.size();
			if (a_event->opening && blocks) {
				_open.insert(name);
			} else if (!a_event->opening) {
				_open.erase(name);
			}
			changed = _open.size() != before;
			nowBlocking = !_open.empty();
			_blocking.store(nowBlocking, std::memory_order_relaxed);
		}

		// This is the line that answers "is LockpickingMenu opening at all, and
		// do we think it blocks us" without any guessing. The first few dozen go
		// in unconditionally: a misjudged menu disables the whole mod, and it is
		// no use if the evidence only exists once someone thinks to turn on
		// debug logging and reproduce it.
		if (settings->debug || _logged < 120) {
			++_logged;
			logger::info("menus: {} {} [{}] -> blocking {} ({} open)",
				name, a_event->opening ? "opened" : "closed", why, nowBlocking, OpenCount());
		}

		if (changed && a_event->opening && blocks && _onBlockingOpen) {
			_onBlockingOpen(name);
		}

		// Re-assert on the spot, before this frame draws. A menu closing is
		// when the game shows its HUD again and TrueHUD shows its own, and
		// the tick that would take them away again runs a frame later at
		// best - which is the flash people saw on leaving a container. All
		// three calls are idempotent and cheap, so opening gets them too.
		ApplyHudVisibility();
		ApplyHudLayout();
		if (_onAnyChange) {
			_onAnyChange();
		}

		return RE::BSEventNotifyControl::kContinue;
	}

	std::size_t GameMenus::OpenCount() const
	{
		std::scoped_lock guard{ _lock };
		return _open.size();
	}

	std::string GameMenus::Describe() const
	{
		std::scoped_lock guard{ _lock };
		if (_open.empty()) {
			return "none";
		}

		std::string out;
		for (const auto& name : _open) {
			if (!out.empty()) {
				out += ", ";
			}
			out += name;
		}
		return out;
	}

	void GameMenus::ApplyHudVisibility()
	{
		const auto* settings = Settings::GetSingleton();
		auto*       ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}

		// Turned off, or turned off since last time: put everything back first.
		if (!settings->hideGameHud) {
			RestoreHud(false);  // hiding and prompt positioning are independent settings
			return;
		}

		std::vector<std::string> wanted;
		{
			std::string_view list{ settings->hideMenus };
			std::size_t      start = 0;
			while (start <= list.size()) {
				const auto comma = list.find(',', start);
				auto piece = list.substr(start, comma == std::string_view::npos
													? std::string_view::npos
													: comma - start);
				// trim
				while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) {
					piece.remove_prefix(1);
				}
				while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t' || piece.back() == '\r')) {
					piece.remove_suffix(1);
				}
				if (!piece.empty()) {
					wanted.emplace_back(piece);
				}
				if (comma == std::string_view::npos) {
					break;
				}
				start = comma + 1;
			}
		}

		const bool hideHud = std::find(wanted.begin(), wanted.end(), RE::HUDMenu::MENU_NAME) != wanted.end();
		std::erase(wanted, std::string{ RE::HUDMenu::MENU_NAME });
		ApplyHudParts(hideHud);

		// Anything we hid that is no longer wanted goes back.
		for (const auto& name : _hidden) {
			if (std::find(wanted.begin(), wanted.end(), name) == wanted.end()) {
				if (auto menu = ui->GetMenu(name); menu && menu->uiMovie) {
					menu->uiMovie->SetVisible(true);
				}
			}
		}

		_hidden.clear();
		for (const auto& name : wanted) {
			auto menu = ui->GetMenu(name);
			if (!menu || !menu->uiMovie) {
				continue;  // not open right now - retried next time this runs
			}
			menu->uiMovie->SetVisible(false);
			_hidden.push_back(name);
		}
	}

	void GameMenus::ApplyHudParts(bool a_hide)
	{
		auto* movie = HudMovie();
		const auto* s = Settings::GetSingleton();
		std::set<std::string> wanted;
		const auto add = [&](bool hide, const char* part) {
			if (a_hide && hide) { wanted.emplace(std::string{ kHudBase } + "." + part + "._alpha"); }
		};
		add(s->hideCompass, "CompassShoutMeterHolder.Compass");
		add(s->hideMessages, "MessagesBlock");
		add(s->hideQuestUpdates, "QuestUpdateBaseInstance");
		add(s->hideHealth, "HealthMeterAnim");
		add(s->hideEnemyHealth, "EnemyHealth_mc");
		add(s->hideMagicka, "MagickaMeterAnim");
		add(s->hideStamina, "StaminaMeterAnim");
		add(s->hideShout, "CompassShoutMeterHolder.ShoutMeterInstance");
		add(s->hideShout, "CompassShoutMeterHolder.ShoutWarningInstance");
		add(s->hideCrosshair, "CrosshairInstance");
		add(s->hideCrosshair, "CrosshairAlert");
		add(s->hideStealth, "StealthMeterInstance");
		add(s->hideSubtitles, "SubtitleTextHolder");
		add(s->hideActivation, "RolloverNameInstance");
		add(s->hideActivation, "ActivateButton_tf");
		add(s->hideActivation, "RolloverInfoInstance");
		add(s->hideActivation, "GrayBarInstance");
		add(s->hideAmmo, "ArrowInfoInstance");
		add(s->hideCharge, "LeftChargeMeterAnim");
		add(s->hideCharge, "RightChargeMeterAnim");
		add(s->hideLocation, "LocationLockBase");
		add(s->hideTutorial, "TutorialLockInstance");
		add(s->hideQuestMarkers, "FloatingQuestMarkerInstance");
		add(s->hideFavorPrompt, "FavorBackButtonBase");
		// Additional relative clip paths for HUD replacers, no guessed memory offsets.
		std::istringstream extra{ s->hideHudParts };
		std::string part;
		while (std::getline(extra, part, ',')) {
			const auto first = part.find_first_not_of(" \t\r");
			if (first == std::string::npos) { continue; }
			part = part.substr(first, part.find_last_not_of(" \t\r") - first + 1);
			add(true, part.c_str());
		}
		_hudMask.Apply(movie, wanted,
			[&](const std::string& path) -> std::optional<double> {
				RE::GFxValue value;
				if (!movie->GetVariable(&value, path.c_str()) || !value.IsNumber()) { return std::nullopt; }
				return value.GetNumber();
			},
			[&](const std::string& path, double alpha) {
				movie->SetVariable(path.c_str(), RE::GFxValue{ alpha });
			});
	}

	void GameMenus::ApplyHudLayout()
	{
		const auto* settings = Settings::GetSingleton();
		const bool  moved = settings->ActivatePromptMoved();
		if (!moved && !_shifted) {
			return;  // never touched, nothing to do - the common case
		}

		auto* movie = HudMovie();
		if (!movie) {
			return;  // no HUD right now (main menu, loading) - retried next tick
		}

		// The whole prompt first, then each piece on top of that. The value
		// line and the bar behind it are one visual unit and take one offset.
		const auto offset = [&](std::size_t a_part, int a_axis) -> float {
			if (!moved) {
				return 0.0f;  // everything back to home
			}
			if (a_axis == 0) {
				return settings->activateShiftX + (a_part == kName ? settings->activateNameX : 0.0f);
			}
			float y = settings->activateShiftY;
			switch (a_part) {
			case kName:
				y += settings->activateNameY;
				break;
			case kButton:
				y += settings->activateButtonY;
				break;
			case kInfo:
			case kGrayBar:
				y += settings->activateInfoY;
				break;
			default:
				break;
			}
			return y;
		};

		bool anyFound = false;
		for (std::size_t i = 0; i < kRolloverCount; ++i) {
			const auto& part = kRolloverParts[i];
			const char* axes[2]{ "._x", "._y" };
			float       current[2]{};
			bool        readable = true;
			for (int axis = 0; axis < 2 && readable; ++axis) {
				RE::GFxValue value;
				const auto   path = std::string{ part.path } + axes[axis];
				if (!movie->GetVariable(&value, path.c_str()) || !value.IsNumber()) {
					readable = false;
				} else {
					current[axis] = static_cast<float>(value.GetNumber());
				}
			}
			if (!readable) {
				// A HUD replacer that renamed or dropped the piece. Said once,
				// and the other pieces still move.
				if (!_saidNoRollover) {
					_saidNoRollover = true;
					logger::warn("activation prompt: {} not found - cannot move it", part.path);
				}
				continue;
			}
			anyFound = true;

			// The first sighting is the HUD file's own placement: nothing but
			// this code ever writes these, so before we have, what is read is
			// home. Kept for the life of the process - a reload rebuilds the
			// element at the same place, and reading it again while shifted
			// would capture our own offset as home.
			if (!_rolloverKnown[i]) {
				_rolloverHome[i][0] = current[0];
				_rolloverHome[i][1] = current[1];
				_rolloverKnown[i] = true;
			}

			for (int axis = 0; axis < 2; ++axis) {
				if (axis == 0 && !part.acrossIsOurs) {
					continue;
				}
				const float target = _rolloverHome[i][axis] + offset(i, axis);
				if (std::abs(current[axis] - target) > 0.01f) {
					movie->SetVariable((std::string{ part.path } + axes[axis]).c_str(),
						RE::GFxValue{ static_cast<double>(target) });
				}
			}
		}

		const bool shifted = moved && anyFound;
		if (shifted && !_shifted) {
			logger::info("activation prompt moved: whole ({:+.0f}, {:+.0f}), name ({:+.0f}, {:+.0f}), "
						 "glyph {:+.0f}, value line {:+.0f}",
				settings->activateShiftX, settings->activateShiftY, settings->activateNameX,
				settings->activateNameY, settings->activateButtonY, settings->activateInfoY);
		} else if (!shifted && _shifted) {
			logger::info("activation prompt put back");
		}
		_shifted = shifted;
	}

	void GameMenus::RestoreHud(bool a_restoreLayout)
	{
		ApplyHudParts(false);
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			_hidden.clear();
			return;
		}
		for (const auto& name : _hidden) {
			if (auto menu = ui->GetMenu(name); menu && menu->uiMovie) {
				menu->uiMovie->SetVisible(true);
			}
		}
		if (!_hidden.empty()) {
			logger::info("menus: restored {} hidden menu(s)", _hidden.size());
		}
		_hidden.clear();

		if (_shifted && a_restoreLayout) {
			if (auto* movie = HudMovie()) {
				for (std::size_t i = 0; i < kRolloverCount; ++i) {
					if (!_rolloverKnown[i]) {
						continue;
					}
					if (kRolloverParts[i].acrossIsOurs) {
						movie->SetVariable((std::string{ kRolloverParts[i].path } + "._x").c_str(),
							RE::GFxValue{ static_cast<double>(_rolloverHome[i][0]) });
					}
					movie->SetVariable((std::string{ kRolloverParts[i].path } + "._y").c_str(),
						RE::GFxValue{ static_cast<double>(_rolloverHome[i][1]) });
				}
			}
			_shifted = false;
			logger::info("activation prompt put back");
		}
	}
}
