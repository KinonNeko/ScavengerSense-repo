#pragma once

namespace SS
{
	// Watches which game menus are open, so a sweep can get out of the way.
	//
	// Whether a menu counts is decided from the menu's own flags, not from its
	// name. A name blacklist is the wrong way round: in a large load order every
	// HUD widget mod that parks a permanent overlay menu on the stack lands on
	// the blocking side of it, and the sweep is pinned down forever. Unknown
	// menus fail open for the same reason.
	class GameMenus : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		[[nodiscard]] static GameMenus* GetSingleton();

		void Install();

		// Moves this sink behind everyone who registered at data load. Sinks
		// run in registration order, SKSE loads plugins alphabetically, and
		// S comes before T: TrueHUD's sink ran after ours on every menu
		// change and showed its movie again a step after we hid it. Called
		// once the game is running, when every plugin's sink is in place.
		void LastWord();

		// Called on the main thread the moment a blocking menu opens.
		void OnBlockingOpen(std::function<void(const std::string&)> a_callback)
		{
			_onBlockingOpen = std::move(a_callback);
		}

		// Called on the main thread on every menu open or close, blocking or
		// not, after this class has re-applied its own hiding. For whatever
		// else keeps a piece of somebody's interface down and needs to put
		// it back down the moment a closing menu hands it back visible.
		void OnAnyChange(std::function<void()> a_callback)
		{
			_onAnyChange = std::move(a_callback);
		}

		// Cheap enough to read from the render thread every frame.
		[[nodiscard]] bool Blocking() const { return _blocking.load(std::memory_order_relaxed); }

		[[nodiscard]] std::size_t OpenCount() const;

		// Comma separated list of the blocking menus currently up, for the log.
		[[nodiscard]] std::string Describe() const;

		// Hide or restore the Scaleform menus named in the settings.
		//
		// Reaches only Scaleform. A mod drawing its HUD through ImGui - which
		// includes this one - owns its own frame and cannot be reached from
		// here, so this is honest about covering the game's interface and
		// whatever other Scaleform menus are named, and nothing more.
		void ApplyHudVisibility();

		// Moves the game's activation prompt - the "Talk  Lydia" that sits
		// on the crosshair, over the face of whoever you are about to talk
		// to - by the configured amounts, as a whole and then piece by
		// piece. The vanilla HUD script never writes those positions (bar
		// the glyph's across, which is left alone), so the offsets hold; they
		// are re-checked every tick because a HUD reload rebuilds the
		// elements where the file put them. All zero puts them back and
		// touches nothing after.
		void ApplyHudLayout();

		// Puts back anything we hid or moved. Called on unload so a crash or
		// a disabled setting never leaves somebody with no interface.
		void RestoreHud();

		// True while anything is still hidden or moved, so the tick keeps
		// running long enough to put it back after the setting is switched
		// off.
		[[nodiscard]] bool HasHidden() const { return !_hidden.empty() || _shifted; }

		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent*             a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

	private:
		GameMenus() = default;

		// Menus we actually hid, so only those get put back.
		std::vector<std::string> _hidden;

		// Where the activation prompt's pieces sit in the HUD file, captured
		// the first time we move them, so they can be put back exactly. One
		// (x, y) per element in kRolloverParts; unset until seen.
		static constexpr std::size_t kRolloverCount = 4;
		float                        _rolloverHome[kRolloverCount][2]{};
		bool                         _rolloverKnown[kRolloverCount]{};
		bool                         _shifted{ false };
		bool                         _saidNoRollover{ false };

		// Returns whether the menu takes over the screen, and fills a_why with
		// what it was judged on so the log can explain itself.
		[[nodiscard]] bool Classify(std::string_view a_name, std::string& a_why) const;

		std::function<void(const std::string&)> _onBlockingOpen;
		std::function<void()>                   _onAnyChange;
		std::atomic_bool                        _blocking{ false };
		mutable std::mutex                      _lock;
		std::set<std::string>                   _open;
		std::uint32_t                           _logged{ 0 };
		bool                                    _installed{ false };
		bool                                    _lastWord{ false };
	};
}
