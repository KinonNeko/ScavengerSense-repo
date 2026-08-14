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

		// Called on the main thread the moment a blocking menu opens.
		void OnBlockingOpen(std::function<void(const std::string&)> a_callback)
		{
			_onBlockingOpen = std::move(a_callback);
		}

		// Cheap enough to read from the render thread every frame.
		[[nodiscard]] bool Blocking() const { return _blocking.load(std::memory_order_relaxed); }

		[[nodiscard]] std::size_t OpenCount() const;

		// Comma separated list of the blocking menus currently up, for the log.
		[[nodiscard]] std::string Describe() const;

		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent*             a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

	private:
		GameMenus() = default;

		// Returns whether the menu takes over the screen, and fills a_why with
		// what it was judged on so the log can explain itself.
		[[nodiscard]] bool Classify(std::string_view a_name, std::string& a_why) const;

		std::function<void(const std::string&)> _onBlockingOpen;
		std::atomic_bool                        _blocking{ false };
		mutable std::mutex                      _lock;
		std::set<std::string>                   _open;
		std::uint32_t                           _logged{ 0 };
		bool                                    _installed{ false };
	};
}
