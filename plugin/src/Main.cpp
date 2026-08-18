#include "Arousal.h"
#include "Config.h"
#include "GameMenus.h"
#include "Locale.h"
#include "Marks.h"
#include "Titles.h"
#include "Menu.h"
#include "PostFX.h"
#include "Sense.h"
#include "Timing.h"

namespace SS
{
	class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputHandler* GetSingleton()
		{
			static InputHandler singleton;
			return std::addressof(singleton);
		}

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const*             a_event,
			RE::BSTEventSource<RE::InputEvent*>*) override
		{
			// Deliberately not gated on Ready(): if the plugin is not armed we
			// still want the press to reach OnHotkey so it can say why in the log
			// instead of the key just doing nothing.
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* ui = RE::UI::GetSingleton();
			if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const auto* settings = Settings::GetSingleton();

			for (auto event = *a_event; event; event = event->next) {
				const auto* button = event->AsButtonEvent();
				if (!button) {
					continue;
				}

				const auto raw = button->GetIDCode();
				const auto device = button->device.get();
				const bool isGamepad = device == RE::INPUT_DEVICE::kGamepad;

				// Normalise to the unified macro space SKSE uses everywhere else:
				// 0-255 keyboard, 256+ mouse buttons, 266-281 gamepad. A gamepad
				// button arrives as a raw XInput bitmask, not an index, so it has
				// to be translated or nothing would ever match.
				std::int32_t code{};
				if (isGamepad) {
					code = static_cast<std::int32_t>(SKSE::InputMap::GamepadMaskToKeycode(raw));
				} else if (device == RE::INPUT_DEVICE::kMouse) {
					code = static_cast<std::int32_t>(raw) + SKSE::InputMap::kMacro_MouseButtonOffset;
				} else {
					code = static_cast<std::int32_t>(raw);
				}

				if (settings->trailMode == TrailKeyMode::kMulti) {
					// A key per tracking action, each with its own gesture. The
					// same physical key may back more than one action, so every
					// matching binding gets to see the event.
					bool consumed = false;
					const auto tryTrailAction = [&](std::int32_t a_key, std::int32_t a_pad,
						Trigger a_gesture, ButtonState& a_state, void (Sense::*a_action)()) {
						const auto want = isGamepad ? a_pad : a_key;
						if (want < 0 || code != want) {
							return;
						}
						consumed = true;
						if (Qualifies(*button, a_state, a_gesture, settings->doubleTapWindow,
								settings->trailHoldTime)) {
							(Sense::GetSingleton()->*a_action)();
						}
					};
					tryTrailAction(settings->trailMarkKey, settings->trailMarkGamepad,
						settings->trailMarkGesture, _trailMark, &Sense::OnTrailMark);
					tryTrailAction(settings->trailShowKey, settings->trailShowGamepad,
						settings->trailShowGesture, _trailShow, &Sense::OnTrailToggle);
					tryTrailAction(settings->trailWipeKey, settings->trailWipeGamepad,
						settings->trailWipeGesture, _trailWipe, &Sense::OnTrailLongPress);
					if (consumed) {
						continue;
					}
				} else {
					// The single trail key: the mark rides whichever gesture the
					// player chose, and so does the wipe - which goes first and
					// swallows the press that made it, so the two never both
					// fire. A single-press mark still acts on release, keeping
					// it distinct from a hold on the same key.
					const auto trailWanted =
						isGamepad ? settings->trailGamepad : settings->trailKey;
					if (trailWanted >= 0 && code == trailWanted) {
						if (button->IsDown()) {
							_trailLongFired = false;
							if (settings->trailWipe == TrailWipe::kDoubleTap) {
								const auto now = RealNow();
								if (now - _trailTapAt <= settings->doubleTapWindow) {
									_trailTapAt = -1000.0f;
									_trailLongFired = true;  // eat this press's release
									Sense::GetSingleton()->OnTrailLongPress();
								} else {
									_trailTapAt = now;
								}
							}
						} else if (button->IsPressed() &&
								   settings->trailWipe == TrailWipe::kHold &&
								   !_trailLongFired &&
								   button->HeldDuration() >= settings->trailHoldTime) {
							_trailLongFired = true;
							Sense::GetSingleton()->OnTrailLongPress();
						}

						if (settings->trailMarkGesture == Trigger::kPress) {
							if (button->IsUp() && !_trailLongFired) {
								Sense::GetSingleton()->OnTrailHotkey();
							}
						} else if (!_trailLongFired &&
								   Qualifies(*button, _trailMark, settings->trailMarkGesture,
									   settings->doubleTapWindow, settings->trailHoldTime)) {
							Sense::GetSingleton()->OnTrailHotkey();
						}
						continue;
					}
				}

				const auto wanted = isGamepad ? settings->gamepad : settings->keyboard;
				if (wanted < 0 || code != wanted) {
					continue;
				}

				if (Qualifies(*button, isGamepad ? _gamepad : _keyboard, settings->trigger,
						settings->doubleTapWindow, settings->holdTime)) {
					Sense::GetSingleton()->OnHotkey();
				}
			}

			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		struct ButtonState
		{
			float lastTapAt{ -1000.0f };  // when the previous press went down
			bool  firedThisHold{ false };  // hold mode already fired for this press
		};

		// Decides whether this particular button event completes a gesture.
		// Called once per event for the bound button only.
		static bool Qualifies(const RE::ButtonEvent& a_button, ButtonState& a_state,
			Trigger a_trigger, float a_doubleTapWindow, float a_holdTime)
		{
			switch (a_trigger) {
			case Trigger::kDoubleTap:
				{
					if (!a_button.IsDown()) {
						return false;
					}

					// Real time, not the sweep clock: that one stops behind menus,
					// which would read both taps at the same instant and make
					// every single press look like a double tap.
					const auto now = RealNow();
					if (now - a_state.lastTapAt <= a_doubleTapWindow) {
						// consume both taps so a third press starts a fresh pair
						a_state.lastTapAt = -1000.0f;
						return true;
					}

					a_state.lastTapAt = now;
					return false;
				}

			case Trigger::kHold:
				{
					// The engine re-sends the event every frame the button is down
					// with a running heldDownSecs, so we just watch that cross the
					// threshold and latch until release.
					if (!a_button.IsPressed()) {
						a_state.firedThisHold = false;
						return false;
					}

					if (!a_state.firedThisHold && a_button.HeldDuration() >= a_holdTime) {
						a_state.firedThisHold = true;
						return true;
					}

					return false;
				}

			case Trigger::kPress:
			default:
				return a_button.IsDown();
			}
		}

		ButtonState _keyboard;
		ButtonState _gamepad;
		bool        _trailLongFired{ false };
		float       _trailTapAt{ -1000.0f };
		// Multi-key mode: gesture state per tracking action.
		ButtonState _trailMark;
		ButtonState _trailShow;
		ButtonState _trailWipe;
	};

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Settings::GetSingleton()->Load();
			Locale::Load(Settings::GetSingleton()->language);
			Sense::GetSingleton()->OnDataLoaded();
			Marks::GetSingleton()->Load();
			Titles::GetSingleton()->Load();

			// Cancelling clears the effect shaders off every lit object as well
			// as everything we draw. That matters for menus that build their own
			// little 3D scene - lockpicking is the one to watch - where a
			// membrane drawn with the depth test disabled has no business being
			// anywhere near the camera.
			GameMenus::GetSingleton()->OnBlockingOpen([](const std::string& a_name) {
				if (!Settings::GetSingleton()->stopOnMenu) {
					return;
				}
				logger::info("{} opened, ending the sweep", a_name);
				Sense::GetSingleton()->Cancel();
			});
			GameMenus::GetSingleton()->Install();
			// Compile the post-process shaders now rather than on the first
			// sweep. Doing it lazily means a LoadLibrary and two shader compiles
			// inside Present, which is a visible hitch at the worst moment.
			PostFX::GetSingleton()->Warm();
			// Look for the arousal mod now. GetModuleHandle only sees a DLL that
			// is already in the process, and by kDataLoaded every SKSE plugin
			// has been loaded - so this is the first moment the answer is real.
			Arousal::GetSingleton()->Warm();
			Marks::GetSingleton()->DeclareIntegration(
				Arousal::kName, Arousal::GetSingleton()->Available() ? 1 : 0);
			// Not something we read, something we get out of the way of - so
			// there is nothing to detect and no badge to show.
			Marks::GetSingleton()->DeclareIntegration("Floating subtitles", -1);
			if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
				input->AddEventSink(InputHandler::GetSingleton());
				logger::info("input sink installed");
			}
			Menu::Register();
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			// Effect shaders and image space modifiers are transient; make sure a
			// wave that was in flight cannot leak across a load.
			Sense::GetSingleton()->Cancel();
			break;

		case SKSE::MessagingInterface::kPostLoadGame:
			Settings::GetSingleton()->Load();
			Locale::Load(Settings::GetSingleton()->language);
			break;

		default:
			break;
		}
	}

	void InitLogging()
	{
		std::vector<spdlog::sink_ptr> sinks;

		// The usual place.
		if (auto path = logger::log_directory(); path) {
			*path /= "ScavengerSense.log";
			try {
				sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
			} catch (...) {}
		}

		// A second copy next to the plugin. Under Mod Organizer this lands in the
		// overwrite folder, which is far easier to get at than Documents when
		// someone is helping you debug.
		try {
			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
				"Data/SKSE/Plugins/ScavengerSense-diag.log", true));
		} catch (...) {}

		if (sinks.empty()) {
			return;
		}

		auto log = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}
}

SKSEPluginInfo(
	.Version = REL::Version{ 0, 7, 2, 0 },
	.Name = "ScavengerSense"sv,
	.Author = "KShakes"sv)

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// SKSE::Init's second argument defaults to true, and that makes it call
	// log::init() - which REPLACES the default logger with its own single sink.
	// Initialising logging first and then calling Init silently throws our
	// logger away. Opt out, then install ours.
	SKSE::Init(a_skse, false);
	SS::InitLogging();

	logger::info("Scavenger Sense 0.7.2 beta loading");

	if (auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(SS::OnMessage);
	} else {
		logger::error("no messaging interface");
		return false;
	}

	return true;
}
