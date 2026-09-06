#include "GameMenus.h"

#include "Config.h"

#include <cmath>

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

		// The vanilla HUD's main clip. Hiding the HUD Menu's movie is what the
		// setting asks for, and it is also what the game undoes: a menu that
		// pauses the game hides the HUD on the way in and shows it again on
		// the way out, at the movie level. Every element the HUD draws hangs
		// off this clip, its script never writes the clip's own _visible,
		// and so the clip stays hidden across that hand-back where the movie
		// did not. The movie is still hidden as well, for anything a mod has
		// attached to the movie beside the clip.
		constexpr auto kHudBase = "_root.HUDMovieBaseInstance";

		// The activation prompt, in pieces: the name line, the key glyph, the
		// value/weight line and the bar drawn behind that line. Moved
		// together so their layout relative to each other survives.
		constexpr const char* kRolloverParts[] = {
			"_root.HUDMovieBaseInstance.RolloverNameInstance",
			"_root.HUDMovieBaseInstance.ActivateButton_tf",
			"_root.HUDMovieBaseInstance.RolloverInfoInstance",
			"_root.HUDMovieBaseInstance.GrayBarInstance",
		};

		[[nodiscard]] RE::GFxMovie* HudMovie()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			return menu && menu->uiMovie ? menu->uiMovie.get() : nullptr;
		}

		void SetBaseVisible(bool a_visible)
		{
			if (auto* movie = HudMovie()) {
				movie->SetVariable((std::string{ kHudBase } + "._visible").c_str(),
					RE::GFxValue{ a_visible });
			}
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
		if (settings->debug || _logged < 60) {
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
			RestoreHud();
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

		// Anything we hid that is no longer wanted goes back.
		for (const auto& name : _hidden) {
			if (std::find(wanted.begin(), wanted.end(), name) == wanted.end()) {
				if (auto menu = ui->GetMenu(name); menu && menu->uiMovie) {
					menu->uiMovie->SetVisible(true);
					if (name == RE::HUDMenu::MENU_NAME) {
						SetBaseVisible(true);
					}
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
			if (name == RE::HUDMenu::MENU_NAME) {
				// The clip as well as the movie: see kHudBase.
				SetBaseVisible(false);
			}
			_hidden.push_back(name);
		}
	}

	void GameMenus::ApplyHudLayout()
	{
		const float shift = Settings::GetSingleton()->activateTextShift;
		if (shift == 0.0f && !_shifted) {
			return;  // never touched, nothing to do - the common case
		}

		auto* movie = HudMovie();
		if (!movie) {
			return;  // no HUD right now (main menu, loading) - retried next tick
		}

		bool anyMoved = false;
		for (std::size_t i = 0; i < kRolloverCount; ++i) {
			const auto   path = std::string{ kRolloverParts[i] } + "._y";
			RE::GFxValue y;
			if (!movie->GetVariable(&y, path.c_str()) || !y.IsNumber()) {
				// A HUD replacer that renamed or dropped the piece. Said once,
				// and the other pieces still move.
				if (!_saidNoRollover) {
					_saidNoRollover = true;
					logger::warn("activation prompt: {} not found - cannot move it",
						kRolloverParts[i]);
				}
				continue;
			}
			const auto current = static_cast<float>(y.GetNumber());

			// The first sighting is the HUD file's own placement: nothing but
			// this code ever writes these, so before we have, what is read is
			// home. Kept for the life of the process - a reload rebuilds the
			// element at the same place, and reading it again while shifted
			// would capture our own offset as home.
			if (!_rolloverKnown[i]) {
				_rolloverHome[i] = current;
				_rolloverKnown[i] = true;
			}

			const float target = _rolloverHome[i] + shift;
			if (std::abs(current - target) > 0.01f) {
				movie->SetVariable(path.c_str(), RE::GFxValue{ static_cast<double>(target) });
			}
			anyMoved = anyMoved || shift != 0.0f;
		}

		if (anyMoved && !_shifted) {
			logger::info("activation prompt moved {:+.0f}", shift);
		} else if (!anyMoved && _shifted) {
			logger::info("activation prompt put back");
		}
		_shifted = anyMoved;
	}

	void GameMenus::RestoreHud()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			_hidden.clear();
			return;
		}
		for (const auto& name : _hidden) {
			if (auto menu = ui->GetMenu(name); menu && menu->uiMovie) {
				menu->uiMovie->SetVisible(true);
				if (name == RE::HUDMenu::MENU_NAME) {
					SetBaseVisible(true);
				}
			}
		}
		if (!_hidden.empty()) {
			logger::info("menus: restored {} hidden menu(s)", _hidden.size());
		}
		_hidden.clear();

		if (_shifted) {
			if (auto* movie = HudMovie()) {
				for (std::size_t i = 0; i < kRolloverCount; ++i) {
					if (_rolloverKnown[i]) {
						movie->SetVariable((std::string{ kRolloverParts[i] } + "._y").c_str(),
							RE::GFxValue{ static_cast<double>(_rolloverHome[i]) });
					}
				}
			}
			_shifted = false;
			logger::info("activation prompt put back");
		}
	}
}
