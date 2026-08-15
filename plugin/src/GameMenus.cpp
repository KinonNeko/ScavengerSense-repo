#include "GameMenus.h"

#include "Config.h"

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
			}
		}
		if (!_hidden.empty()) {
			logger::info("menus: restored {} hidden menu(s)", _hidden.size());
		}
		_hidden.clear();
	}
}
