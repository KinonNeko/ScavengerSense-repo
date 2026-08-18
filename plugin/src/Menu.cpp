#include "Menu.h"

#include "Arousal.h"
#include "Config.h"
#include "Labels.h"
#include "Locale.h"
#include "Marks.h"
#include "Timing.h"
#include "PostFX.h"
#include "Sense.h"
#include "Titles.h"

// SKSE Menu Framework re-exports the whole cimgui C API from its own DLL, so a
// client plugin talks to the host's ImGui context directly - no separate imgui
// binary, no second context. CIMGUI_NO_EXPORT strips the dllexport attribute
// from the declarations; we link against an import library generated from the
// framework DLL's export table.
#define CIMGUI_NO_EXPORT
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

// The framework's own export. Declared here rather than including
// SKSEMenuFramework.h, which pulls in the C++ imgui.h we do not ship.
extern "C" __declspec(dllimport) void AddSectionItem(const char* a_path, void(__stdcall* a_render)());

namespace SS::Menu
{
	namespace
	{
		using Locale::T;

		constexpr auto kSectionPath = "Scavenger Sense/Settings";

		// Order must match SS::Trigger.
		constexpr const char* kTriggerLabels[] = {
			"Single press",
			"Double tap",
			"Hold"
		};

		// Order must match SKSE::InputMap::kGamepadButtonOffset_* (266..281),
		// with a leading "none" entry.
		constexpr const char* kGamepadNames[] = {
			"None",
			"D-Pad Up",
			"D-Pad Down",
			"D-Pad Left",
			"D-Pad Right",
			"Start / Menu",
			"Back / View",
			"Left Stick (L3)",
			"Right Stick (R3)",
			"Left Bumper (LB)",
			"Right Bumper (RB)",
			"A",
			"B",
			"X",
			"Y",
			"Left Trigger (LT)",
			"Right Trigger (RT)"
		};

		// The keys worth offering in a list, in the order a keyboard has them.
		// Codes are DirectInput scan codes, which is what the engine and the INI
		// speak; mouse buttons live at 256 and up in SKSE's macro space.
		//
		// Curated rather than generated: the full scan code space is full of
		// entries nobody can press, and half of them share a name.
		struct KeyChoice
		{
			const char*  label;
			std::int32_t code;
		};

		constexpr KeyChoice kKeyChoices[] = {
			{ "(none)", -1 },

			{ "A", 30 }, { "B", 48 }, { "C", 46 }, { "D", 32 }, { "E", 18 }, { "F", 33 },
			{ "G", 34 }, { "H", 35 }, { "I", 23 }, { "J", 36 }, { "K", 37 }, { "L", 38 },
			{ "M", 50 }, { "N", 49 }, { "O", 24 }, { "P", 25 }, { "Q", 16 }, { "R", 19 },
			{ "S", 31 }, { "T", 20 }, { "U", 22 }, { "V", 47 }, { "W", 17 }, { "X", 45 },
			{ "Y", 21 }, { "Z", 44 },

			{ "1", 2 }, { "2", 3 }, { "3", 4 }, { "4", 5 }, { "5", 6 },
			{ "6", 7 }, { "7", 8 }, { "8", 9 }, { "9", 10 }, { "0", 11 },

			{ "F1", 59 }, { "F2", 60 }, { "F3", 61 }, { "F4", 62 }, { "F5", 63 }, { "F6", 64 },
			{ "F7", 65 }, { "F8", 66 }, { "F9", 67 }, { "F10", 68 }, { "F11", 87 }, { "F12", 88 },

			{ "Tab", 15 }, { "Caps Lock", 58 }, { "Left Shift", 42 }, { "Left Ctrl", 29 },
			{ "Left Alt", 56 }, { "Space", 57 }, { "Enter", 28 }, { "Backspace", 14 },
			{ "Right Shift", 54 }, { "Right Ctrl", 157 }, { "Right Alt", 184 },

			{ "Up", 200 }, { "Down", 208 }, { "Left", 203 }, { "Right", 205 },
			{ "Insert", 210 }, { "Delete", 211 }, { "Home", 199 }, { "End", 207 },
			{ "Page Up", 201 }, { "Page Down", 209 },

			{ "- (minus)", 12 }, { "= (equals)", 13 }, { "[", 26 }, { "]", 27 },
			{ "\\", 43 }, { "; (semicolon)", 39 }, { "' (apostrophe)", 40 },
			{ "` (grave)", 41 }, { ", (comma)", 51 }, { ". (period)", 52 }, { "/ (slash)", 53 },

			{ "Numpad 0", 82 }, { "Numpad 1", 79 }, { "Numpad 2", 80 }, { "Numpad 3", 81 },
			{ "Numpad 4", 75 }, { "Numpad 5", 76 }, { "Numpad 6", 77 }, { "Numpad 7", 71 },
			{ "Numpad 8", 72 }, { "Numpad 9", 73 }, { "Numpad +", 78 }, { "Numpad -", 74 },
			{ "Numpad *", 55 }, { "Numpad /", 181 }, { "Numpad Enter", 156 },

			{ "Mouse 1 (left)", 256 }, { "Mouse 2 (right)", 257 }, { "Mouse 3 (middle)", 258 },
			{ "Mouse 4", 259 }, { "Mouse 5", 260 }
		};

		// Live key capture.
		//
		// Deliberately not routed through the plugin's own input sink: the menu
		// framework locks the game while it is open, and the sink stands down
		// when the game is paused - so the one moment you want to read a key is
		// the one moment that path is dead. Polling the OS key state sidesteps
		// it, and gives mouse buttons for free.
		struct KeyCapture
		{
			bool  active{ false };
			float startedAt{ 0.0f };
			bool  wasDown[256]{};
		};

		KeyCapture g_capture;

		// Windows says which key; DirectInput scan codes are what we store. The
		// extended byte matters - without it the arrow keys come out as the
		// numeric keypad, because they share a scan code and differ only by the
		// 0xE0 prefix.
		[[nodiscard]] std::int32_t ScanCodeFor(int a_vk)
		{
			switch (a_vk) {
			case VK_LBUTTON:
				return 256;
			case VK_RBUTTON:
				return 257;
			case VK_MBUTTON:
				return 258;
			case VK_XBUTTON1:
				return 259;
			case VK_XBUTTON2:
				return 260;
			default:
				break;
			}

			const auto mapped = ::MapVirtualKeyA(static_cast<UINT>(a_vk), MAPVK_VK_TO_VSC_EX);
			if (mapped == 0) {
				return -1;
			}

			auto scan = static_cast<std::int32_t>(mapped & 0xFF);
			if (((mapped >> 8) & 0xFF) == 0xE0) {
				scan |= 0x80;
			}
			return scan;
		}

		// Returns the captured code, or -1 while still waiting, or -2 if the
		// user pressed Escape to cancel.
		[[nodiscard]] std::int32_t PollCapture()
		{
			constexpr float kSettle = 0.30f;

			const auto now = RealNow();

			// Everything held at the moment capture began - including the mouse
			// button that clicked the button - is ignored until it is released.
			if (now - g_capture.startedAt < kSettle) {
				for (int vk = 1; vk < 256; ++vk) {
					g_capture.wasDown[vk] = (::GetAsyncKeyState(vk) & 0x8000) != 0;
				}
				return -1;
			}

			for (int vk = 1; vk < 256; ++vk) {
				const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
				const bool pressed = down && !g_capture.wasDown[vk];
				g_capture.wasDown[vk] = down;

				if (!pressed) {
					continue;
				}

				if (vk == VK_ESCAPE) {
					return -2;
				}

				if (const auto code = ScanCodeFor(vk); code > 0) {
					return code;
				}
			}

			return -1;
		}

		// ------------------------------------------------------------ helpers

		void Help(const char* a_text)
		{
			igSameLine(0.0f, -1.0f);
			igTextDisabled("(?)");
			if (igIsItemHovered(0)) {
				igSetTooltip("%s", T(a_text));
			}
		}

		// Combo boxes take a plain array of pointers, so the entries cannot be
		// wrapped in T() where they are declared. This builds the translated
		// array on the way in. The pointers come from the string table, which
		// outlives the call, and ImGui has finished with the array by the time
		// the combo returns - so one shared buffer is enough for every combo on
		// the page.
		const char* const* Translated(const char* const* a_items, int a_count)
		{
			static std::vector<const char*> buffer;
			buffer.clear();
			buffer.reserve(static_cast<std::size_t>(a_count));
			for (int i = 0; i < a_count; ++i) {
				buffer.push_back(T(a_items[i]));
			}
			return buffer.data();
		}

		// A one-slot colour clipboard shared by every picker in the menu. Build
		// a palette once and carry it around rather than reading six hex values
		// off one page and typing them into another.
		std::uint32_t g_clipColour{ 0xFFFFFF };
		bool          g_hasClip{ false };

		bool ColourPicker(const char* a_label, std::uint32_t& a_rgb)
		{
			bool changed = false;

			// Two pickers can share a label - "Colour" appears once per marker
			// rule - so scope the widget IDs even though most callers already
			// push an index of their own.
			igPushID_Str(a_label);

			float colour[3]{
				static_cast<float>((a_rgb >> 16) & 0xFF) / 255.0f,
				static_cast<float>((a_rgb >> 8) & 0xFF) / 255.0f,
				static_cast<float>(a_rgb & 0xFF) / 255.0f,
			};

			// NoInputs: the three float boxes are wide enough to push the
			// buttons off the row inside the categories table, and the popup you
			// get from clicking the swatch has the same fields in it anyway.
			if (igColorEdit3("##swatch", colour, ImGuiColorEditFlags_NoInputs)) {
				const auto channel = [](float a_value) {
					return static_cast<std::uint32_t>(std::clamp(a_value, 0.0f, 1.0f) * 255.0f + 0.5f);
				};
				a_rgb = (channel(colour[0]) << 16) | (channel(colour[1]) << 8) | channel(colour[2]);
				changed = true;
			}

			igSameLine(0.0f, 3.0f);
			if (igSmallButton(T("copy"))) {
				g_clipColour = a_rgb;
				g_hasClip = true;
			}
			if (igIsItemHovered(0)) {
				igSetTooltip("%s", T("Put this colour on the clipboard."));
			}

			igSameLine(0.0f, 3.0f);
			igBeginDisabled(!g_hasClip);
			if (igSmallButton(T("paste"))) {
				a_rgb = g_clipColour;
				changed = true;
			}
			igEndDisabled();
			if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				igSetTooltip("%s", g_hasClip ? T("Take the clipboard colour.")
											 : T("Copy a colour somewhere first."));
			}

			// ColorEdit3 draws its own label, but it draws it after the inputs -
			// which would put it past the buttons. The label is hidden above and
			// written by hand here so the row reads swatch, buttons, name.
			if (a_label[0] != '#') {
				igSameLine(0.0f, -1.0f);
				igText("%s", a_label);
			}

			igPopID();
			return changed;
		}

		// The clipboard swatch plus whatever bulk action the section offers.
		// Drawn at the top of a group of pickers so there is somewhere obvious
		// to look for "what am I about to paste".
		void ClipboardBar()
		{
			igTextDisabled("%s", T("clipboard"));
			igSameLine(0.0f, -1.0f);

			if (g_hasClip) {
				const auto swatch = ImVec4{
					static_cast<float>((g_clipColour >> 16) & 0xFF) / 255.0f,
					static_cast<float>((g_clipColour >> 8) & 0xFF) / 255.0f,
					static_cast<float>(g_clipColour & 0xFF) / 255.0f, 1.0f
				};
				igColorButton("##clip", swatch, ImGuiColorEditFlags_NoTooltip, ImVec2{ 0.0f, 0.0f });
				igSameLine(0.0f, -1.0f);
				igText("0x%06X", g_clipColour & 0xFFFFFFu);
			} else {
				igText("%s", T("empty - press copy beside any colour"));
			}
		}

		bool Header(const char* a_label)
		{
			return igCollapsingHeader_TreeNodeFlags(a_label, ImGuiTreeNodeFlags_DefaultOpen);
		}

		// Which binding on the Keys page is listening for a press. One slot
		// for the whole page, so two captures can never fight.
		int g_bindSlot = -1;

		// One key binding, drawn the same for every shortcut in the mod: pick
		// from the list, press the key itself, type the raw code, and choose
		// the gamepad button. The gesture, where one applies, is the caller's.
		void BindingControls(int a_slot, std::int32_t& a_key, std::int32_t& a_pad)
		{
			igPushID_Int(a_slot + 900);

			int current = -1;
			for (int i = 0; i < static_cast<int>(std::size(kKeyChoices)); ++i) {
				if (kKeyChoices[i].code == a_key) {
					current = i;
					break;
				}
			}
			const auto label = [&]() -> std::string {
				if (current >= 0) {
					return T(kKeyChoices[current].label);
				}
				auto name = SKSE::InputMap::GetKeyName(static_cast<std::uint32_t>(a_key));
				return name.empty() ? std::format("code {}", a_key)
									: std::format("{} ({})", name, a_key);
			}();
			if (igBeginCombo(T("Key"), label.c_str(), 0)) {
				for (int i = 0; i < static_cast<int>(std::size(kKeyChoices)); ++i) {
					const bool selected = i == current;
					if (igSelectable_Bool(T(kKeyChoices[i].label), selected, 0, ImVec2{ 0.0f, 0.0f })) {
						a_key = kKeyChoices[i].code;
					}
					if (selected) {
						igSetItemDefaultFocus();
					}
				}
				igEndCombo();
			}

			if (g_bindSlot == a_slot) {
				igTextColored(ImVec4{ 1.0f, 0.85f, 0.4f, 1.0f }, "%s",
					T("press any key or mouse button - Esc cancels"));
				const auto captured = PollCapture();
				if (captured == -2) {
					g_bindSlot = -1;
				} else if (captured > 0) {
					a_key = captured;
					g_bindSlot = -1;
				}
			} else if (igButton(T("Press a key to bind"), ImVec2{ 190.0f, 0.0f })) {
				g_bindSlot = a_slot;
				g_capture.active = true;
				g_capture.startedAt = RealNow();
			}
			igSameLine(0.0f, -1.0f);
			if (igButton(T("Clear"), ImVec2{ 80.0f, 0.0f })) {
				a_key = -1;
				if (g_bindSlot == a_slot) {
					g_bindSlot = -1;
				}
			}

			if (igTreeNodeEx_Str(T("Exact scan code"), 0)) {
				igInputInt("##scancode", &a_key, 1, 10, 0);
				Help(
					"For a key the list above does not have. Mouse buttons are 256 left,\n"
					"257 right, 258 middle. -1 unbinds.");
				igTreePop();
			}

			int pad = 0;
			if (a_pad >= SKSE::InputMap::kMacro_GamepadOffset &&
				a_pad < SKSE::InputMap::kMaxMacros) {
				pad = a_pad - SKSE::InputMap::kMacro_GamepadOffset + 1;
			}
			if (igCombo_Str_arr(T("Gamepad button"), &pad,
					Translated(kGamepadNames, static_cast<int>(std::size(kGamepadNames))),
					static_cast<int>(std::size(kGamepadNames)), -1)) {
				a_pad = pad == 0 ? -1 : SKSE::InputMap::kMacro_GamepadOffset + pad - 1;
			}

			igPopID();
		}

		// ------------------------------------------------------------ sections

		void DrawHotkey(Settings& a_settings)
		{
			if (!Header(T("Sweep key"))) {
				return;
			}

			BindingControls(0, a_settings.keyboard, a_settings.gamepad);
			Help(
				"The button keeps doing its normal job as well, so pick a spare one.");

			igSpacing();

			int trigger = static_cast<int>(a_settings.trigger);
			if (igCombo_Str_arr(T("Activate on"), &trigger, Translated(kTriggerLabels, static_cast<int>(std::size(kTriggerLabels))),
					static_cast<int>(std::size(kTriggerLabels)), -1)) {
				a_settings.trigger = static_cast<Trigger>(
					std::clamp(trigger, 0, static_cast<int>(Trigger::kCount) - 1));
			}
			Help(
				"Double tap and hold leave the ordinary press free for whatever else\n"
				"the key already does.");

			switch (a_settings.trigger) {
			case Trigger::kDoubleTap:
				igSliderFloat(T("Max gap between taps"), &a_settings.doubleTapWindow, 0.05f, 1.0f, "%.2f s", 0);
				Help(
					"How long you have to get the second tap in.");
				break;

			case Trigger::kHold:
				igSliderFloat(T("Hold for"), &a_settings.holdTime, 0.05f, 3.0f, "%.2f s", 0);
				Help(
					"How long to hold. It fires while you are still holding, no need to let go.");
				break;

			default:
				break;
			}

			igSpacing();
			igCheckbox(T("Press again to cancel"), &a_settings.toggle);
			igSliderFloat(T("Cooldown"), &a_settings.cooldown, 0.0f, 10.0f, "%.2f s", 0);

			igSpacing();
		}

		// Its own section rather than a line under the cooldown: a sound you
		// cannot find the volume control for might as well have none.
		void DrawChime(Settings& a_settings)
		{
			if (!Header(T("Chime"))) {
				return;
			}

			igCheckbox(T("Chime when the sweep starts"), &a_settings.soundEnabled);
			Help(
				"A short two-note chime. The sound itself is the file\n"
				"Sound\\FX\\ScavengerSense\\sweep.wav - overwrite it with any wav\n"
				"to make it yours. It plays through the game's UI audio, so the\n"
				"game's own volume sliders apply on top.");
			if (a_settings.soundEnabled) {
				igSliderFloat(T("Chime volume"), &a_settings.soundVolume, 0.0f, 1.0f, "%.2f", 0);
				Help(
					"1.00 plays the file as loud as it is. The game engine can only\n"
					"turn a sound down, not up - so if it is still too quiet at full,\n"
					"replace the wav with a louder one.");
			}

			igSpacing();
		}

		void DrawScan(Settings& a_settings)
		{
			if (!Header(T("Scan"))) {
				return;
			}

			igSliderFloat(T("Radius"), &a_settings.radius, 256.0f, 8000.0f, "%.0f units", 0);
			Help(
				"How far the sweep reaches. 1500 is about 23 metres, 2500 about 38.");

			int maxObjects = static_cast<int>(a_settings.maxObjects);
			if (igSliderInt(T("Max objects"), &maxObjects, 1, 128, "%d", 0)) {
				a_settings.maxObjects = static_cast<std::uint32_t>(std::clamp(maxObjects, 1, 128));
			}
			Help(
				"How many things one sweep can light at once. 128 is the maximum.");

			igCheckbox(T("Ignore unnamed objects"), &a_settings.ignoreUnnamed);
			Help(
				"Skips the invisible markers the game litters the world with.");

			igCheckbox(T("Ignore harvested plants"), &a_settings.ignoreHarvested);

			igCheckbox(T("Ignore placeholder names"), &a_settings.ignorePlaceholders);
			Help(
				"Some objects wear the engine's placeholder name - \"This should\n"
				"not be visible.\" on ore veins and the like. The English and\n"
				"Chinese strings are built in; if a mod or another language shows\n"
				"one, add what it says below - or aim at it and press the button.");
			if (a_settings.ignorePlaceholders) {
				static char placeholderBuf[256]{};
				static bool placeholderSynced = false;
				if (!placeholderSynced) {
					std::snprintf(placeholderBuf, sizeof(placeholderBuf), "%s",
						a_settings.placeholderNames.c_str());
					placeholderSynced = true;
				}
				if (igInputText(T("Names to skip"), placeholderBuf, sizeof(placeholderBuf), 0,
						nullptr, nullptr)) {
					a_settings.placeholderNames = placeholderBuf;
				}

				// The language-proof way in: whatever the crosshair rested on
				// before the menu opened donates its name to the list.
				if (igButton(T("Add what the crosshair sees"), ImVec2{ 0.0f, 0.0f })) {
					if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
						if (auto ref = pick->target.get(); ref) {
							if (const auto* name = ref->GetDisplayFullName(); name && name[0]) {
								std::string lowAll = a_settings.placeholderNames;
								std::string lowNew{ name };
								for (auto& c : lowAll) {
									c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
								}
								for (auto& c : lowNew) {
									c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
								}
								if (lowAll.find(lowNew) == std::string::npos) {
									if (!a_settings.placeholderNames.empty()) {
										a_settings.placeholderNames += ", ";
									}
									a_settings.placeholderNames += name;
									std::snprintf(placeholderBuf, sizeof(placeholderBuf), "%s",
										a_settings.placeholderNames.c_str());
								}
							}
						}
					}
				}
				Help(
					"Aim the crosshair at the offending object, open this menu and\n"
					"press. Its exact name - in your game's language - joins the\n"
					"list.");
			}

			igCheckbox(T("Spread picks across the radius"), &a_settings.spreadAcrossRadius);
			Help(
				"When there is more to light than there is room for, spread it over the\n"
				"whole radius. Off, the wave never gets past the first few metres.");
			if (a_settings.spreadAcrossRadius) {
				igSliderFloat(T("Favour nearby"), &a_settings.nearBias, 1.0f, 6.0f, "%.2f", 0);
				Help(
					"Higher keeps more of what is close to you and less of what is far away.\n"
					"1.0 treats near and far the same. 2.2 is a good middle.");
			}

			igSpacing();
		}

		void DrawPulse(Settings& a_settings)
		{
			if (!Header(T("Pulse"))) {
				return;
			}

			igSliderFloat(T("Sweep time"), &a_settings.sweepTime, 0.0f, 5.0f, "%.2f s", 0);
			Help(
				"How long the wave takes to reach the furthest thing it lights.\n"
				"0 lights everything at once.");

			igSliderFloat(T("Highlight duration"), &a_settings.duration, 0.5f, 30.0f, "%.1f s", 0);
			igSliderFloat(T("Fade in"), &a_settings.fadeIn, 0.0f, 2.0f, "%.2f s", 0);
			igSliderFloat(T("Fade out"), &a_settings.fadeOut, 0.0f, 4.0f, "%.2f s", 0);

			igSpacing();
		}

		void DrawVision(Settings& a_settings)
		{
			if (!Header(T("Vision"))) {
				return;
			}

			igCheckbox(T("See through walls"), &a_settings.throughWalls);
			Help(
				"Glows show through walls and other objects. Things behind a closed door\n"
				"still will not show - the game is not drawing them at all.");

			igSliderFloat(T("Edge falloff"), &a_settings.edgeFalloff, 0.2f, 6.0f, "%.2f", 0);
			Help(
				"Higher gives a thin rim. Lower fills more of the object with light.");

			igSliderFloat(T("Glow strength"), &a_settings.glowStrength, 0.0f, 3.0f, "%.2f", 0);
			Help(
				"Brightness of the glow. Above 1.0 the colours wash towards white.");

			igSliderFloat(T("Throb amount"), &a_settings.pulseAmplitude, 0.0f, 1.0f, "%.2f", 0);
			igSliderFloat(T("Throb speed"), &a_settings.pulseFrequency, 0.0f, 8.0f, "%.2f", 0);

			igSpacing();
		}

		void DrawLabels(Settings& a_settings)
		{
			if (!Header(T("Name tags"))) {
				return;
			}

			igCheckbox(T("Show names on lit objects"), &a_settings.labelsEnabled);
			Help(
				"Names appear and fade with the glow, so they arrive with the wave.");

			if (a_settings.labelsEnabled) {
				igCheckbox(T("Dark backdrop"), &a_settings.labelBackdrop);
				igSliderFloat(T("Label distance"), &a_settings.labelMaxDistance, 0.0f, 5000.0f, "%.0f units", 0);
				Help("Only name things closer than this. 0 names everything.");
				igSliderFloat(T("Text size"), &a_settings.labelFontSize, 8.0f, 48.0f, "%.0f px", 0);
				Help(
					"How big the name text is, in pixels.");
				igSliderFloat(T("People are this much bigger"), &a_settings.labelActorScale, 0.5f, 3.0f, "%.2fx", 0);
				Help(
					"People get bigger tags than objects, above the head rather than at the feet.");
				igSliderFloat(T("Merge identical tags within"), &a_settings.labelMergeDistance,
					0.0f, 1500.0f, "%.0f units", 0);
				Help(
					"Folds identical names that are close together into one, so a cellar reads\n"
					"\"Wine x40\" instead of forty overlapping words. 0 keeps them separate.\n"
					"People are never folded.");
				igCheckbox(T("Tags follow what they name"), &a_settings.labelsFollow);
				Help(
					"Tags stay over people as they walk about. Off, a tag stays where the\n"
					"person was standing when the wave reached them.");

				igSpacing();
				igCheckbox(T("Friend or foe marker"), &a_settings.labelIcons);
				Help(
					"A shape beside a person's name: triangle hostile, circle stranger,\n"
					"diamond ally, heart lover, cross body.");

				igSpacing();
				igCheckbox(T("Star your favourites"), &a_settings.favouriteHighlight);
				Help(
					"Anything you have favourited in your inventory gets a star, its own\n"
					"colour and a bigger tag. Handy for the alchemy ingredients you use.");

				if (a_settings.favouriteHighlight) {
					ColourPicker(T("Favourite colour"), a_settings.favouriteColour);
					igSliderFloat(T("Favourite size"), &a_settings.favouriteScale, 0.5f, 3.0f, "%.2fx", 0);
				}

				if (!Sense::GetSingleton()->LabelsAvailable()) {
					igTextColored(ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f }, "%s",
						T("needs SKSE Menu Framework 3.13 or newer"));
				} else if (auto* labels = Labels::GetSingleton(); labels->SawFirstFrame()) {
					if (labels->_hasCJK.load()) {
						igTextColored(ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f }, "%s",
							T("the menu font covers Chinese characters, so names will read correctly"));
					} else {
						igTextColored(ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f }, "%s",
							T("the menu font has no Chinese characters, so names will show as '?' - see the readme"));
					}
				}
			}

			igSpacing();
		}

		void DrawRing(Settings& a_settings)
		{
			if (!Header(T("Sonar ring"))) {
				return;
			}

			igCheckbox(T("Ring travels out from your feet"), &a_settings.ringEnabled);
			Help(
				"The ring follows the ground, climbing stairs and rolling over hills.");

			if (a_settings.ringEnabled) {
				ColourPicker(T("Ring colour"), a_settings.ringColour);
				igSliderFloat(T("Line width"), &a_settings.ringThickness, 0.5f, 12.0f, "%.1f px", 0);
				igSliderFloat(T("Halo width"), &a_settings.ringGlow, 0.0f, 32.0f, "%.1f px", 0);
				Help("A soft glow behind the ring. 0 leaves a clean thin line.");

				auto echoes = static_cast<int>(a_settings.ringEchoes);
				if (igSliderInt(T("Echoes"), &echoes, 1, 8, "%d", 0)) {
					a_settings.ringEchoes = static_cast<std::uint32_t>(std::clamp(echoes, 1, 8));
				}
				Help("How many rings trail behind the front one. 1 draws just the front.");
				igSliderFloat(T("Echo spacing"), &a_settings.ringEchoGap, 0.01f, 0.5f, "%.2f s", 0);
				igSliderFloat(T("Height off ground"), &a_settings.ringHeight, -32.0f, 128.0f, "%.0f units", 0);
				igSliderFloat(T("Overshoot"), &a_settings.ringLead, 0.5f, 4.0f, "%.2fx", 0);
				Help(
					"How far the ring carries on past the last thing it lit.");
			}

			igSpacing();
		}

		void DrawTint(Settings& a_settings)
		{
			if (!Header(T("Screen tint"))) {
				return;
			}

			igCheckbox(T("Desaturate while sensing"), &a_settings.tintEnabled);
			Help(
				"Drains the colour from the world while a sweep runs, so the highlights\n"
				"are the only strong colour on screen.");

			if (a_settings.tintEnabled) {
				igSliderFloat(T("Saturation"), &a_settings.tintSaturation, 0.0f, 1.5f, "%.2f", 0);
				Help("1.0 is normal colour. 0.0 is fully grey.");
				igSliderFloat(T("Brightness"), &a_settings.tintBrightness, 0.2f, 1.5f, "%.2f", 0);
				igSliderFloat(T("Contrast"), &a_settings.tintContrast, 0.5f, 2.0f, "%.2f", 0);
				igSliderFloat(T("Blend"), &a_settings.tintStrength, 0.0f, 1.0f, "%.2f", 0);

				igSpacing();
				ColourPicker(T("Colour cast"), a_settings.tintCastColour);
				igSliderFloat(T("Cast amount"), &a_settings.tintCastAmount, 0.0f, 1.0f, "%.2f", 0);
				Help(
					"Tints the world itself, rather than laying a sheet of colour over it.");

				igCheckbox(T("Use the old image space modifier instead"), &a_settings.tintUseImod);
				Help(
					"An older way of doing the same thing. Only worth trying if the tint does\n"
					"nothing under your ENB.");
			}

			igSpacing();
			igCheckbox(T("Community Shaders mode (own shader pass)"), &a_settings.postProcess);
			Help(
				"Turn this on if you use Community Shaders and the tint does nothing.\n"
				"It also draws a proper vignette. If it cannot run it turns itself off\n"
				"for the session and the overlay below takes over.");

			if (a_settings.postProcess) {
				igCheckbox(T("Grade the back buffer"), &a_settings.postProcessBackBuffer);
				Help(
					"Leave this on. Off is slightly cheaper but can tint the interface\n"
					"instead of the world.");

				const auto status = PostFX::GetSingleton()->Status();
				const bool bad = status.starts_with("unavailable");
				igTextColored(
					bad ? ImVec4{ 1.0f, 0.45f, 0.4f, 1.0f } : ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f },
					"%s: %s", T("shader pass"), status.c_str());
			}

			igSpacing();
			igCheckbox(T("Vignette overlay"), &a_settings.washEnabled);
			Help(
				"Darkens the edges of the screen and leaves the middle clear.");

			if (a_settings.washEnabled) {
				ColourPicker(T("Overlay colour"), a_settings.washColour);
				igSliderFloat(T("Overlay strength"), &a_settings.washStrength, 0.0f, 1.0f, "%.2f", 0);
				igSliderFloat(T("Flat share"), &a_settings.washFlat, 0.0f, 1.0f, "%.2f", 0);
				Help(
					"How much of the overlay covers the whole screen rather than just the\n"
					"edges. All the way up looks like coloured film.");
			}

			igSpacing();
		}

		void DrawCategories(Settings& a_settings)
		{
			if (!Header(T("Categories"))) {
				return;
			}

			igTextDisabled("%s", T("what lights up, in what colour, and how it is drawn"));

			igSpacing();
			igSeparatorText(T("People"));
			static const char* kActorFilters[] = { "all", "living only", "dead only" };
			int                filter = static_cast<int>(a_settings.actorFilter);
			if (igCombo_Str_arr(T("Actors to sense"), &filter, Translated(kActorFilters, static_cast<int>(std::size(kActorFilters))),
					static_cast<int>(std::size(kActorFilters)), -1)) {
				a_settings.actorFilter = static_cast<ActorFilter>(
					std::clamp(filter, 0, static_cast<int>(ActorFilter::kCount) - 1));
			}
			Help(
				"Bodies count as people - they can be looted. Dead only is for finding a\n"
				"kill in tall grass.");

			igCheckbox(T("Only enemies"), &a_settings.actorEnemiesOnly);
			Help(
				"Nothing lights or gets a tag unless it means you harm: hostile by\n"
				"nature, attacking you right now, or struck by you at any point this\n"
				"session. Monsters too, and corpses as well - a dead bandit shows,\n"
				"a dead shopkeeper does not.");

			igSpacing();
			igSeparatorText(T("Things"));
			igCheckbox(T("Skip empty containers"), &a_settings.hideEmptyContainers);
			Help(
				"A chest with nothing in it neither lights nor gets a tag. Saves\n"
				"walking to a barrel that holds air - but respawning containers\n"
				"you have cleared stay dark until they refill.");

			igCheckbox(T("Skip what I would be stealing"), &a_settings.hideStealing);
			Help(
				"Anything the red hand would appear on - somebody else's goods,\n"
				"an owned chest - stays dark. The sense only shows what is\n"
				"honestly yours to take.");

			igSpacing();
			igSeparatorText(T("Colours"));
			ClipboardBar();
			Help(
				"One colour per row, used for both the glow and the name tag.\n"
				"\n"
				"Copy and paste move a colour between rows. Dragging one swatch onto\n"
				"another does the same.");
			igSpacing();

			igCheckbox(T("Colour people by disposition"), &a_settings.actorByDisposition);
			Help(
				"Colours people by whether they are friendly or about to attack, instead\n"
				"of all the same colour. Replaces the People colour below.");

			if (a_settings.actorByDisposition) {
				igCheckbox(T("Read the relationship record too"), &a_settings.actorByRelationship);
				Help(
					"Also tells a rival from a stranger, and a friend from a lover, before\n"
					"anyone has drawn a weapon.");

				ColourPicker(T("Hostile"), a_settings.hostileColour);
				ColourPicker(T("Neutral"), a_settings.neutralColour);
				if (a_settings.actorByRelationship) {
					ColourPicker(T("Rival or foe"), a_settings.rivalColour);
					ColourPicker(T("Friend"), a_settings.friendlyColour);
					ColourPicker(T("Lover"), a_settings.loverColour);
				}
				ColourPicker(T("Ally or follower"), a_settings.allyColour);
				ColourPicker(T("Dead"), a_settings.deadColour);
			}

			igSpacing();

			// A table, not igSameLine with hand-picked pixel offsets: the label
			// widths change with the font size and with the language, so any
			// fixed offset is wrong for somebody. Columns size themselves.
			if (igBeginTable("categories", 3, ImGuiTableFlags_SizingFixedFit, ImVec2{ 0.0f, 0.0f }, 0.0f)) {
				for (std::size_t i = 0; i < kCategoryCount; ++i) {
					auto&       category = a_settings.categories[i];
					const auto* name = CategoryName(static_cast<Category>(i));

					igPushID_Int(static_cast<int>(i));
					igTableNextRow(0, 0.0f);

					igTableSetColumnIndex(0);
					igCheckbox(T(name), &category.enabled);

					igTableSetColumnIndex(1);
					ColourPicker("##colour", category.colour);

					igTableSetColumnIndex(2);
					igCheckbox(T("outline only"), &category.outlineOnly);

					igPopID();
				}
				igEndTable();
			}

			Help(
				"Outline draws just the rim, so two people standing one behind the other\n"
				"stay readable as two shapes.");

			igBeginDisabled(!g_hasClip);
			if (igSmallButton(T("Paste into every category"))) {
				for (auto& category : a_settings.categories) {
					category.colour = g_clipColour;
				}
			}
			igEndDisabled();
			igSameLine(0.0f, -1.0f);
			igBeginDisabled(!g_hasClip);
			if (igSmallButton(T("Paste into every tier"))) {
				a_settings.hostileColour = g_clipColour;
				a_settings.neutralColour = g_clipColour;
				a_settings.rivalColour = g_clipColour;
				a_settings.friendlyColour = g_clipColour;
				a_settings.loverColour = g_clipColour;
				a_settings.allyColour = g_clipColour;
				a_settings.deadColour = g_clipColour;
			}
			igEndDisabled();
			Help(
				"Sets every colour in the group at once. To undo, reload your INI or a preset.");

			igSpacing();
		}

		void DrawActions(Settings& a_settings)
		{
			igSeparatorText(T("Behaviour"));

			igCheckbox(T("Stand down while a menu is open"), &a_settings.menuAware);
			Help(
				"Hides everything while a menu is open, so nothing is drawn over it.\n"
				"Turn it off if the sweep ever refuses to run at all.");

			if (a_settings.menuAware) {
				igCheckbox(T("End the sweep, do not just hide it"), &a_settings.stopOnMenu);
				Help(
					"Ends the sweep instead of hiding it, clearing the glow off everything.");
				igCheckbox(T("Mute during conversation"), &a_settings.muteInDialogue);
				Help(
					"Keeps a sweep from going off in the middle of a conversation.");
			}

			igSpacing();
			igSeparatorText(T("Language"));

			// Two arrays on purpose. The first is what gets written to the INI
			// and used to find the translation file, so it has to stay in
			// ASCII whatever language the menu is in. The second is only ever
			// shown, and each name is written in its own script - a language
			// picker you cannot read is not much use for getting back out.
			static const char* kLanguages[] = { "english", "chinese" };
			static const char* kLanguageNames[] = { "English", "Chinese" };

			int language = a_settings.language == "chinese" ? 1 : 0;
			if (igCombo_Str_arr(T("Menu language"), &language,
					Translated(kLanguageNames, static_cast<int>(std::size(kLanguageNames))),
					static_cast<int>(std::size(kLanguageNames)), -1)) {
				a_settings.language = kLanguages[language];
				Locale::Load(a_settings.language);
			}
			Help(
				"Applies straight away.\n"
				"Chinese also needs EnableChinese = true in SKSEMenuFramework.ini, or\n"
				"every character shows as a box.");

			igSpacing();
			igSeparatorText(T("Actions"));

			auto* sense = Sense::GetSingleton();

			// Run / Cancel / Save live above the tabs, where they are reachable
			// from every page. These are the ones you only want occasionally.
			if (igButton(T("Reload INI"), ImVec2{ 130.0f, 0.0f })) {
				sense->Cancel();
				a_settings.Load();
			}

			igSameLine(0.0f, -1.0f);
			if (igButton(T("Restore defaults"), ImVec2{ 140.0f, 0.0f })) {
				sense->Cancel();
				a_settings.RestoreDefaults();
			}

			igSpacing();
			igCheckbox(T("Verbose logging"), &a_settings.debug);
			Help(
				"Records every object a sweep lights, for working out why something is\n"
				"missing.");
		}

		// ------------------------------------------------------------ the page

		// One rule's appearance. Shared by the markers list and the add-on
		// pages, because a rule that belongs to an add-on is edited in exactly
		// the same way as one you wrote yourself.
		void DrawRuleStyle(Settings& a_settings, std::size_t a_index)
		{
			static const char* kStyleLabels[] = { "number", "filling icon", "no tally" };

			auto*       marks = Marks::GetSingleton();
			const auto& rule = marks->Rules()[a_index];
			auto&       style = a_settings.StyleFor(rule.name, rule.colour);

			igPushID_Int(static_cast<int>(a_index));

			const auto swatch = ImVec4{
				static_cast<float>((style.colour >> 16) & 0xFF) / 255.0f,
				static_cast<float>((style.colour >> 8) & 0xFF) / 255.0f,
				static_cast<float>(style.colour & 0xFF) / 255.0f, 1.0f
			};

			igCheckbox("##on", &style.enabled);
			Help("Draw this marker at all.");
			igSameLine(0.0f, -1.0f);

			if (igTreeNodeEx_Str(rule.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
				igTextColored(swatch, "%s", rule.icon.empty()
												? T("built-in heart shape (no icon file)")
												: rule.icon.c_str());

				if (ColourPicker(T("Colour"), style.colour)) {
					style.overrideColour = true;
				}

				const bool hasTally = rule.countFaction || !rule.countGroups.empty();
				if (hasTally) {
					int chosen = static_cast<int>(style.countStyle);
					if (igCombo_Str_arr(T("Tally"), &chosen,
							Translated(kStyleLabels, static_cast<int>(std::size(kStyleLabels))),
							static_cast<int>(std::size(kStyleLabels)), -1)) {
						style.countStyle = static_cast<CountStyle>(
							std::clamp(chosen, 0, static_cast<int>(CountStyle::kCount) - 1));
					}
					Help(
						"number: the count is written beside the marker.\n"
						"filling icon: the marker fills up as the count rises.\n"
						"no tally: just the marker.");

					if (style.countStyle == CountStyle::kFill) {
						int full = static_cast<int>(style.countFull);
						if (igSliderInt(T("Full at"), &full, 1, 32, "%d", 0)) {
							style.countFull = static_cast<std::uint32_t>(std::clamp(full, 1, 64));
						}
						Help(
							"The count at which the symbol is completely full.");
					}

					igTextDisabled("%s", rule.countGroups.empty()
											 ? T("shows a faction rank as the number")
											 : T("counts the different things done together"));
				} else {
					igTextDisabled("%s", T("no number, just the marker"));
				}

				igTreePop();
			}

			igPopID();
		}

		// Put the crosshair on somebody and find out how a number was reached.
		// These tallies are read out of another mod's records, so when one looks
		// wrong the question worth answering is which of the two mods is not
		// writing - and that needs the working shown.
		void DrawExplain()
		{
			auto* marks = Marks::GetSingleton();

			static std::string explanation;
			static bool        onlyMatched = false;
			static std::string subject;

			if (igButton(T("Explain for my target"), ImVec2{ 200.0f, 0.0f })) {
				explanation.clear();
				subject.clear();

				RE::Actor* target = nullptr;
				if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
					if (auto handle = pick->target; handle) {
						if (auto ref = handle.get(); ref) {
							target = ref->As<RE::Actor>();
						}
					}
				}

				if (!target) {
					explanation = T("Nothing under the crosshair. Look at somebody and press it again.");
				} else {
					for (std::size_t i = 0; i < marks->RuleCount(); ++i) {
						explanation += marks->Explain(static_cast<int>(i), target);
					}
					logger::info("marker explanation:\n{}", explanation);
				}
			}
			Help(
				"Aim at somebody, open this menu and press it. Every line is listed and\n"
				"ticked if it applies to them.");

			igSameLine(0.0f, -1.0f);
			igCheckbox(T("only the ticked lines"), &onlyMatched);

			if (explanation.empty()) {
				return;
			}

			// The whole list is here, all of it, always - there are no further
			// pages. A line like "6 of 41" is a summary of the lines above it:
			// six of the forty-one things this rule can count have happened, and
			// six is the number drawn on the tag.
			igTextDisabled("%s", T("The whole list is below. \"6 of 41\" means six of the forty-one things this rule counts have happened, and six is the number on the tag."));

			if (igBeginChild_Str("explain", ImVec2{ 0.0f, 260.0f }, ImGuiChildFlags_Border,
					ImGuiWindowFlags_HorizontalScrollbar)) {
				std::size_t from = 0;
				while (from < explanation.size()) {
					auto to = explanation.find('\n', from);
					if (to == std::string::npos) {
						to = explanation.size();
					}
					const std::string_view row{ explanation.data() + from, to - from };

					const bool ticked = row.find("[x]") != std::string_view::npos;
					const bool unticked = row.find("[ ]") != std::string_view::npos;

					if (!onlyMatched || !unticked) {
						if (ticked) {
							igTextColored(ImVec4{ 0.55f, 0.9f, 0.55f, 1.0f }, "%.*s",
								static_cast<int>(row.size()), row.data());
						} else if (unticked) {
							igTextDisabled("%.*s", static_cast<int>(row.size()), row.data());
						} else {
							igText("%.*s", static_cast<int>(row.size()), row.data());
						}
					}

					from = to + 1;
				}
			}
			igEndChild();
		}

		// Who you are on the sweep. Just identity - everything about bars moved
		// to its own page, because two thirds of what used to be here was not
		// about you at all.
		void DrawSelf(Settings& a_settings)
		{
			if (!Header(T("You"))) {
				return;
			}

			igCheckbox(T("Tag me too"), &a_settings.scanPlayer);
			Help(
				"Puts a name tag on your own character too. You never get a glow.");

			if (a_settings.scanPlayer) {
				igCheckbox(T("Show what I am"), &a_settings.selfIcon);
				Help(
					"A shape beside your name: a standing figure while mortal, fangs while\n"
					"vampiric, three claw marks as a werewolf or in beast form.");

				ColourPicker(T("My colour"), a_settings.selfColour);
				igSliderFloat(T("My tag size"), &a_settings.selfScale, 0.5f, 3.0f, "%.2fx", 0);
				Help("On top of the size people's tags already get.");
			}

			igSpacing();
			igTextDisabled("%s", T("Health, magicka and stamina are on the Vitals page."));
			igSpacing();
		}

		// Breadcrumbs behind anyone the sense has touched.
		void DrawTracks(Settings& a_settings)
		{
			if (!Header(T("Tracks"))) {
				return;
			}

			igCheckbox(T("People leave trails"), &a_settings.trailsEnabled);
			Help(
				"Anyone the sense has touched - sweep-lit or fought - leaves\n"
				"chevrons on the ground pointing the way they went, fading as they\n"
				"age. Hostile trails draw in the hostile colour.\n"
				"\n"
				"Recording starts when the sense first sees somebody: nobody's\n"
				"past is known before that, and a trail ends where its owner\n"
				"left the loaded world.");

			if (a_settings.trailsEnabled) {
				igSpacing();
				igSeparatorText(T("Recording"));
				igSliderFloat(T("Trail lifetime"), &a_settings.trailLifetime, 10.0f, 300.0f, "%.0f s", 0);
				igCheckbox(T("Capture everyone around"), &a_settings.trailAutoCapture);
				Help(
					"Everyone loaded leaves a trail, no sweep needed - those already\n"
					"there when you arrive, and those the world streams in as you\n"
					"travel. The picture is complete but busier, and costs a little\n"
					"more memory.");

				igSpacing();
				igSeparatorText(T("Drawing"));
				static const char* const kTrailStyles[] = { "Chevrons", "Footprints" };
				int style = static_cast<int>(a_settings.trailStyle);
				if (igCombo_Str_arr(T("Drawn as"), &style, Translated(kTrailStyles, 2), 2, -1)) {
					a_settings.trailStyle = static_cast<TrailStyle>(std::clamp(style, 0, 1));
				}
				Help(
					"Chevrons point the way they went. Footprints alternate left and\n"
					"right like real tracks, with the toes ahead.");
				if (a_settings.trailStyle == TrailStyle::kFootprints) {
					igSliderFloat(T("Print size"), &a_settings.trailPrintScale, 0.5f, 2.5f, "%.2fx", 0);
					int printEvery = a_settings.trailPrintEvery;
					if (igSliderInt(T("Print spacing"), &printEvery, 1, 4, "%d", 0)) {
						a_settings.trailPrintEvery = std::clamp(printEvery, 1, 4);
					}
					Help(
						"How many recorded marks lie between drawn prints. 1 prints at\n"
						"every mark; higher spreads the prints out.");
				}

				igCheckbox(T("Name the trails"), &a_settings.trailNames);
				Help(
					"A small label at the fresh end of the trail - whose footprints\n"
					"these are.");

				static const char* const kReveals[] = { "Crouch and sense", "Any sweep",
					"While crouching", "Always" };
				int reveal = static_cast<int>(a_settings.trailReveal);
				if (igCombo_Str_arr(T("Trails show"), &reveal, Translated(kReveals, 4), 4, -1)) {
					a_settings.trailReveal = static_cast<TrailReveal>(std::clamp(reveal, 0, 3));
				}
				Help(
					"When trails show is also when the trail key works. Crouch and\n"
					"sense is the hunt: kneel, open the sense, and the ground stays\n"
					"readable - and markable - until that sweep ends, even if you\n"
					"stand; a sweep opened standing shows no footprints. While\n"
					"crouching needs no sweep at all. A marked quarry shows always.");

				igCheckbox(T("Tracking messages"), &a_settings.trailToasts);
				Help(
					"The corner notes - Tracking Hulda, All trails wiped. Untick for\n"
					"a silent hunt.");

				igCheckbox(T("Forget trails when I change place"), &a_settings.trailsClearOnTransition);
				Help(
					"An interior's marks are nonsense outside it, so trails are wiped\n"
					"when you pass a load door or change worldspace. A marked quarry\n"
					"is spared: their trail pauses, keeps recording wherever they\n"
					"are loaded, and each stretch shows in the place it was laid.");

				igCheckbox(T("Forget marked trails as well"), &a_settings.trailsForgetMarked);
				Help(
					"The hunt ends at the door: changing place releases every mark\n"
					"and wipes their trails with the rest. Leave off to track someone\n"
					"across cells.");

				igSpacing();
				igSeparatorText(T("Marking"));
				igSliderFloat(T("Marking reach"), &a_settings.trackRange, 500.0f, 10000.0f, "%.0f units", 0);
				Help(
					"How far the mark can pick somebody out. It aims by view - the\n"
					"creature nearest your screen centre within reach - so a deer\n"
					"across a valley is fair game.");

				static const char* const kAimStyles[] = { "Frame corners", "Ring at the feet",
					"Chevron overhead" };
				int aimStyle = static_cast<int>(a_settings.aimStyle);
				if (igCombo_Str_arr(T("Marking sign"), &aimStyle, Translated(kAimStyles, 3), 3, -1)) {
					a_settings.aimStyle = static_cast<AimStyle>(std::clamp(aimStyle, 0, 2));
				}
				Help(
					"The sign over whoever the key would take. On a trail it is\n"
					"always running dots marching toward the fresh end, in the same\n"
					"colour.");
				ColourPicker(T("Sign colour"), a_settings.aimColour);
				Help(
					"For targets not yet marked. One already marked shows their own\n"
					"mark colour instead, so you know a press would release them.");

				igCheckbox(T("Track several at once"), &a_settings.multiMark);
				Help(
					"Off, a new mark replaces the old - a hunt has one prey. On, each\n"
					"mark takes its own colour, and the sweep glow over a marked\n"
					"person agrees with their trail. Hold the key half a second to\n"
					"release everything at once.");

				igCheckbox(T("Release a dead quarry"), &a_settings.markDeathRelease);
				Help(
					"A hunt ends at the kill: once the target has been dead this\n"
					"long, the mark slips off and the trail ages away on its own.");
				if (a_settings.markDeathRelease) {
					igSliderFloat(T("Dead for"), &a_settings.markDeathDelay, 0.0f, 120.0f, "%.0f s", 0);
				}

				igTextDisabled("%s", T("A marked quarry glows in their mark colour on every sweep."));
				igTextDisabled("%s", T("The trail key lives on the Keys page."));
			}

			igSpacing();
		}

		// Every tracking shortcut, in the same dress as the sweep key above it.
		void DrawTrailKeys(Settings& a_settings)
		{
			if (!Header(T("Trail key"))) {
				return;
			}

			static const char* const kKeyModes[] = { "One key does it all",
				"A key per action", "All on the sweep key" };
			int keyMode = static_cast<int>(a_settings.trailMode);
			if (igCombo_Str_arr(T("Control layout"), &keyMode, Translated(kKeyModes, 3), 3, -1)) {
				a_settings.trailMode = static_cast<TrailKeyMode>(std::clamp(keyMode, 0, 2));
			}
			Help(
				"One key: a press marks or releases what the aim finds, and the\n"
				"wipe rides its own gesture. A key per action splits everything,\n"
				"hide and show included. All on the sweep key folds the whole\n"
				"hunt onto one button: double tap sweeps, a lone press marks,\n"
				"a hold wipes.");

			if (a_settings.trailMode == TrailKeyMode::kAllInOne) {
				igTextDisabled("%s", T("The sweep key above carries everything. Double tap to sweep; a lone press marks once the window rules a second tap out; hold to wipe."));
				Help(
					"The lone press waits out the double-tap window before it\n"
					"marks, so a sweep's first tap never takes anybody - marking\n"
					"gains that little delay in exchange for the single button.\n"
					"The sweep's Activate-on setting stands aside on this key,\n"
					"and stray presses outside the hunt are quietly ignored.");
				igSliderFloat(T("Hold for"), &a_settings.trailHoldTime, 0.2f, 2.0f, "%.1f s", 0);
				igSliderFloat(T("Max gap between taps"), &a_settings.doubleTapWindow, 0.05f, 1.0f, "%.2f s", 0);
				igSpacing();
				return;
			}

			// A trail key that collides with the sweep key starves the sweep:
			// the trail handler reads it first and eats the event. Say so
			// rather than let the sweep silently die.
			const auto clashes = [&](std::int32_t a_key, std::int32_t a_pad) {
				return (a_key >= 0 && a_key == a_settings.keyboard) ||
			           (a_pad >= 0 && a_pad == a_settings.gamepad);
			};
			const bool clash =
				a_settings.trailMode == TrailKeyMode::kSingle
					? clashes(a_settings.trailKey, a_settings.trailGamepad)
					: clashes(a_settings.trailMarkKey, a_settings.trailMarkGamepad) ||
					      clashes(a_settings.trailShowKey, a_settings.trailShowGamepad) ||
					      clashes(a_settings.trailWipeKey, a_settings.trailWipeGamepad);
			if (clash) {
				igTextColored(ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f }, "%s",
					T("This is also the sweep key - the sweep would never fire. Pick another key, or the All on the sweep key layout."));
			}

			const auto gestureCombo = [&](Trigger& a_gesture) {
				int gesture = static_cast<int>(a_gesture);
				if (igCombo_Str_arr(T("Gesture"), &gesture,
						Translated(kTriggerLabels, static_cast<int>(std::size(kTriggerLabels))),
						static_cast<int>(std::size(kTriggerLabels)), -1)) {
					a_gesture = static_cast<Trigger>(
						std::clamp(gesture, 0, static_cast<int>(Trigger::kCount) - 1));
				}
			};

			if (a_settings.trailMode == TrailKeyMode::kSingle) {
				igTextDisabled("%s", T("Aim at someone - or their footprints - and press to track them. Hold to wipe everything."));
				BindingControls(1, a_settings.trailKey, a_settings.trailGamepad);

				gestureCombo(a_settings.trailMarkGesture);
				Help(
					"The gesture that marks or releases what the aim finds. When it\n"
					"collides with the wipe below, the wipe wins the press.");

				static const char* const kWipeGestures[] = { "Hold the key", "Double-tap it",
					"Never" };
				int wipe = static_cast<int>(a_settings.trailWipe);
				if (igCombo_Str_arr(T("Wipe everything by"), &wipe, Translated(kWipeGestures, 3), 3, -1)) {
					a_settings.trailWipe = static_cast<TrailWipe>(std::clamp(wipe, 0, 2));
				}
				Help(
					"The clean slate: every mark, trail and open recording goes at\n"
					"once. Pick the gesture, or take it off the key entirely.");
				if (a_settings.trailWipe == TrailWipe::kHold ||
					a_settings.trailMarkGesture == Trigger::kHold) {
					igSliderFloat(T("Hold for"), &a_settings.trailHoldTime, 0.2f, 2.0f, "%.1f s", 0);
				}
			} else {
				igPushID_Int(1);
				igSeparatorText(T("Mark what I aim at"));
				BindingControls(2, a_settings.trailMarkKey, a_settings.trailMarkGamepad);
				gestureCombo(a_settings.trailMarkGesture);
				igPopID();

				igPushID_Int(2);
				igSeparatorText(T("Hide and show trails"));
				BindingControls(3, a_settings.trailShowKey, a_settings.trailShowGamepad);
				gestureCombo(a_settings.trailShowGesture);
				igPopID();

				igPushID_Int(3);
				igSeparatorText(T("Wipe everything"));
				BindingControls(4, a_settings.trailWipeKey, a_settings.trailWipeGamepad);
				gestureCombo(a_settings.trailWipeGesture);
				igPopID();

				igSliderFloat(T("Hold for"), &a_settings.trailHoldTime, 0.2f, 2.0f, "%.1f s", 0);
				Help(
					"Shared by every action whose gesture is a hold; the double\n"
					"tap shares the sweep key's window.");
			}

			igSpacing();
		}

		// Your stats row: what it says and which bar stack it rides.
		void DrawStatsRowCfg(Settings& a_settings)
		{
			if (Header(T("Stats row"))) {
				static const char* const kStatsPlaces[] = { "Riding my overhead bars",
					"Under the corner readout", "Both" };
				int place = static_cast<int>(a_settings.statsPlace);
				if (igCombo_Str_arr(T("Where the stats live"), &place, Translated(kStatsPlaces, 3), 3, -1)) {
					a_settings.statsPlace = static_cast<StatsPlace>(std::clamp(place, 0, 2));
				}
				Help(
					"Riding the overhead bars keeps everything about you in a single\n"
					"glance - the full-HUD way to play. The row follows whichever\n"
					"stack it is attached to.");
				igCheckbox(T("My level"), &a_settings.senseLevel);
				igCheckbox(T("My septims"), &a_settings.senseGold);
				igCheckbox(T("My carry weight"), &a_settings.senseWeight);
				Help(
					"Current over maximum. It turns hostile red when you are\n"
					"over-encumbered.");
				igCheckbox(T("How cold I am"), &a_settings.senseCold);
				Help(
					"Read from a survival mod's global variable - Survival Mode's out\n"
					"of the box. Point coldGlobal in the INI at another mod's variable\n"
					"to use that instead; the row hides itself when none exists.");
				igSpacing();
			}
		}

		// Levels, weapons and races on everyone's tags and chips.
		void DrawPeopleReading(Settings& a_settings)
		{
			if (Header(T("About everyone"))) {
				igCheckbox(T("Levels after names"), &a_settings.levelOthers);
				Help(
					"Their level in parentheses after the name, on tags and combat\n"
					"bars alike - enemies very much included. Knowing you are\n"
					"outmatched is the point.");

				igCheckbox(T("Icons follow the drawn weapon"), &a_settings.weaponIcons);
				Help(
					"The shape beside a name becomes what they are holding - sword,\n"
					"bow, spell - instead of the relationship marker. Yours too. The\n"
					"colour still says friend or foe.");

				igCheckbox(T("Race beside the level"), &a_settings.raceIcons);
				Help(
					"A racial emblem - a horned helm for a Nord, cat ears for a\n"
					"Khajiit - marks the level instead of repeating the weapon shape,\n"
					"with fangs or a paw after it for a vampire or werewolf. On other\n"
					"people it sits before their weapon icon.");
				igSpacing();
			}
		}

		// How a bar looks, wherever it is drawn. Stated once: the same shape and
		// timing serve the tag, the corner readout and other people.
		void DrawBarStyle(Settings& a_settings)
		{
			if (!Header(T("Bar style"))) {
				return;
			}

			igTextDisabled("%s", T("Applies to every bar below."));
			igSpacing();

			ColourPicker(T("Health"), a_settings.selfHealthColour);
			ColourPicker(T("Magicka"), a_settings.selfMagickaColour);
			ColourPicker(T("Stamina"), a_settings.selfStaminaColour);
			ColourPicker(T("Bar frame"), a_settings.selfBarFrameColour);

			igSliderFloat(T("Bar length"), &a_settings.selfBarWidth, 20.0f, 400.0f, "%.0f px", 0);
			igSliderFloat(T("Bar thickness"), &a_settings.selfBarHeight, 1.0f, 16.0f, "%.0f px", 0);

			igSliderFloat(T("Slant"), &a_settings.selfBarShear, -2.0f, 2.0f, "%.2f", 0);
			Help(
				"Leans the ends of the bar over. This is most of what stops it\n"
				"looking like a loading bar. 0 is a plain rectangle.");

			int segments = static_cast<int>(a_settings.selfBarSegments);
			if (igSliderInt(T("Notches"), &segments, 0, 30, "%d", 0)) {
				a_settings.selfBarSegments = segments;
			}
			Help("Divisions cut across the bar. 0 draws it smooth.");

			igSliderFloat(T("Depth"), &a_settings.selfBarPerspective, 0.0f, 0.5f, "%.2f", 0);
			Help(
				"Sets each row further back than the one above, shortening it to\n"
				"match, so the three read as a panel angled away rather than three\n"
				"flat stickers. 0 stacks them square.");

			igCheckbox(T("Glow"), &a_settings.selfBarGlow);
			Help("A soft halo under the filled part.");

			igSpacing();
			igSeparatorText(T("Fine placement"));
			// Drags, not sliders: one pixel of mouse travel is one pixel of
			// bar travel, and Ctrl+click types an exact number. In the open,
			// not behind a fold - a control nobody finds does not exist.
			igDragFloat(T("Sweep bars across"), &a_settings.selfBarOffsetX, 1.0f, -600.0f, 600.0f, "%.0f px", 0);
			igDragFloat(T("Sweep bars down"), &a_settings.selfBarOffsetY, 1.0f, -600.0f, 600.0f, "%.0f px", 0);
			Help(
				"The bars hanging off name tags during a sweep. Drag for single\n"
				"pixels, Ctrl+click to type a number.");
			igDragFloat(T("Overhead bars across"), &a_settings.overheadOffsetX, 1.0f, -600.0f, 600.0f, "%.0f px", 0);
			igDragFloat(T("Overhead bars down"), &a_settings.overheadOffsetY, 1.0f, -600.0f, 600.0f, "%.0f px", 0);
			Help(
				"The no-sweep stacks: combat bars and the ones over your own head.\n"
				"The corner readout has its own margins under Mine.");

			igSpacing();
			igCheckbox(T("Show what a survival mod has taken"), &a_settings.barsLostMax);
			Help(
				"When fatigue or cold pulls your maximum down, the bar keeps its\n"
				"full length and the missing span becomes a dead zone - instead of\n"
				"the bar quietly rescaling and pretending nothing happened.");
			if (a_settings.barsLostMax) {
				igCheckbox(T("Frost and smoke over the lost span"), &a_settings.barsLostFx);
				Help(
					"Frost creeps over lost health; lost magicka and stamina smoulder\n"
					"in their own colour. Off draws the dead zone plain.");
			}

			igSpacing();
			igSliderFloat(T("Stays up for"), &a_settings.selfHudLinger, 0.5f, 15.0f, "%.1f s", 0);
			igSliderFloat(T("Fading over"), &a_settings.selfHudFade, 0.0f, 5.0f, "%.1f s", 0);
			Help(
				"How long a bar set to \"when something changes\" stays visible after\n"
				"the value moves, and how much of that it spends fading out.\n"
				"\n"
				"Shared by all three, so the readout and the bars over other people\n"
				"never disagree about how long a change is worth showing.");

			igSpacing();
		}

		// Your own vitals, in the two places they can appear.
		void DrawMyVitals(Settings& a_settings)
		{
			if (!Header(T("Mine"))) {
				return;
			}

			static const char* const kWhen[] = { "Always", "When something changes",
				"Only what is not full" };

			igSeparatorText(T("On my name tag"));
			igCheckbox(T("Show them during a sweep"), &a_settings.selfBars);
			Help(
				"Three bars under your name for as long as a sweep lasts. Needs\n"
				"\"Tag me too\" on the What shows page, since they hang off the tag.");

			if (a_settings.selfBars) {
				int when = static_cast<int>(a_settings.selfBarsWhen);
				if (igCombo_Str_arr(T("Show them"), &when, Translated(kWhen, 3), 3, -1)) {
					a_settings.selfBarsWhen = static_cast<ShowWhen>(std::clamp(when, 0, 2));
				}

				static const char* const kPlaces[] = { "Below the name", "Above the name",
					"Left of the name", "Right of the name" };
				int place = static_cast<int>(a_settings.selfBarPlace);
				if (igCombo_Str_arr(T("Where"), &place, Translated(kPlaces, 4), 4, -1)) {
					a_settings.selfBarPlace = static_cast<BarPlace>(std::clamp(place, 0, 3));
				}
				Help(
					"Left and right stand the bars upright beside you and fill them from\n"
					"the bottom. Above and below lay them flat.");
			}

			igSpacing();
			igSeparatorText(T("Over my head"));
			igCheckbox(T("Bars over my head without a sweep"), &a_settings.selfBarsOverhead);
			Help(
				"The same stack the combat bars use, following you in third\n"
				"person. In first person there is no head to hang them over -\n"
				"that is what the corner readout below is for.");
			if (a_settings.selfBarsOverhead) {
				int overheadWhen = static_cast<int>(a_settings.selfBarsOverheadWhen);
				if (igCombo_Str_arr(T("Show these"), &overheadWhen, Translated(kWhen, 3), 3, -1)) {
					a_settings.selfBarsOverheadWhen = static_cast<ShowWhen>(std::clamp(overheadWhen, 0, 2));
				}
				Help(
					"\"When something changes\" uses the same linger and fade the other\n"
					"bars do, set under Bar style.");
			}

			igSpacing();
			igSeparatorText(T("Pinned to the screen"));

			static const char* const kCorners[] = { "Off", "Top left", "Top right",
				"Bottom left", "Bottom right" };
			int corner = static_cast<int>(a_settings.selfHudCorner);
			if (igCombo_Str_arr(T("Corner"), &corner, Translated(kCorners, 5), 5, -1)) {
				a_settings.selfHudCorner = static_cast<Corner>(std::clamp(corner, 0, 4));
			}
			Help(
				"The same bars pinned to a corner, sweep or no sweep - the one\n"
				"that works in first person. Pair it with hiding the game's own\n"
				"HUD on the Setup page.");

			if (a_settings.selfHudCorner != Corner::kOff) {
				int when = static_cast<int>(a_settings.selfHudShow);
				if (igCombo_Str_arr(T("Show it"), &when, Translated(kWhen, 3), 3, -1)) {
					a_settings.selfHudShow = static_cast<ShowWhen>(std::clamp(when, 0, 2));
				}
				Help(
					"\"When something changes\" keeps the screen clear until a value\n"
					"moves - taking a hit, casting, drinking, or regenerating - then\n"
					"shows the bars for a few seconds and fades them out again.");

				igSliderFloat(T("Corner size"), &a_settings.selfHudScale, 0.5f, 4.0f, "%.2fx", 0);
				igDragFloat(T("Margin across"), &a_settings.selfHudX, 1.0f, 0.0f, 600.0f, "%.0f px", 0);
				igDragFloat(T("Margin down"), &a_settings.selfHudY, 1.0f, 0.0f, 600.0f, "%.0f px", 0);
				Help("Drag for single pixels, Ctrl+click to type a number.");
			}

			igSpacing();
		}

		void DrawOtherVitals(Settings& a_settings)
		{
			if (!Header(T("Other people"))) {
				return;
			}

			igCheckbox(T("Bars over other people"), &a_settings.vitalsActors);
			Help(
				"For the length of a sweep only, everyone the wave reached gets\n"
				"bars - through walls, all at once. Deliberately not persistent:\n"
				"a bar over everybody all the time is what a combat HUD is for.");

			if (a_settings.vitalsActors) {
				igCheckbox(T("Only people hostile to me"), &a_settings.vitalsActorsHostileOnly);
				Help(
					"Bars only over people who actually want you dead - judged by\n"
					"the engine's own hostility check, so modded enemies count.\n"
					"Rivals who have not drawn a weapon do not.");

				igCheckbox(T("Their magicka and stamina too"), &a_settings.vitalsActorsAll);
				Help("Off draws health only, which is usually all you wanted to know.");

				static const char* const kWhen2[] = { "Always", "Only someone just hurt",
					"Only what is not full" };
				int when2 = static_cast<int>(a_settings.vitalsActorsWhen);
				if (igCombo_Str_arr(T("For whom"), &when2, Translated(kWhen2, 3), 3, -1)) {
					a_settings.vitalsActorsWhen = static_cast<ShowWhen>(std::clamp(when2, 0, 2));
				}

				igSpacing();
				igTextDisabled("%s", T("People still have to be lit for this to show."));
				Help(
					"Turn on the people category on the What shows page, or a sweep\n"
					"never reaches anybody and there is nothing to put a bar over.");
			}

			igSpacing();
			igSeparatorText(T("In combat"));
			igCheckbox(T("Bars over people I have hit"), &a_settings.combatBars);
			Help(
				"From your first hit until the fight ends, anyone you have struck\n"
				"keeps bars over their head - no sweep needed.\n"
				"\n"
				"If you run TrueHUD, its recent-damage bars already cover this -\n"
				"leave it off rather than drawing two.");

			if (a_settings.combatBars) {
				igCheckbox(T("Also their magicka and stamina"), &a_settings.combatBarsAll);
				Help(
					"Its own switch, separate from the sweep one above: health-only\n"
					"in a fight and all three on a sweep is a sensible pairing.");

				igSliderFloat(T("Linger after the fight"), &a_settings.combatLinger, 0.0f, 15.0f, "%.1f s", 0);
				Help(
					"How long the bars outlive the fight, and how long a fresh corpse\n"
					"keeps its drained bar so the kill reads.");
			}

			igSpacing();
			igCheckbox(T("Own the enemy bars"), &a_settings.pushTrueHUDAside);
			Help(
				"While the combat bars are on, this mod is the only enemy health\n"
				"on screen: TrueHUD's overlay is hidden and the vanilla bar is\n"
				"parked off screen. Both come back when either box is unticked.");

			igSpacing();
		}

		void DrawMarkers(Settings& a_settings)
		{
			if (!Header(T("Custom markers"))) {
				return;
			}

			auto* marks = Marks::GetSingleton();

			igTextWrapped("%s", T(
				"Who each marker applies to is set in the marker file. How it looks is set here, and your styling is saved with a preset."));
			igSpacing();

			std::size_t shown = 0;
			for (std::size_t i = 0; i < marks->RuleCount(); ++i) {
				// Rules belonging to an add-on are edited on the Add-ons page,
				// beside the switch that turns their mod on and off.
				if (!marks->Rules()[i].integration.empty()) {
					continue;
				}
				DrawRuleStyle(a_settings, i);
				++shown;
			}

			if (shown == 0) {
				igTextDisabled("%s", marks->RuleCount() == 0
										 ? T("no rules loaded")
										 : T("every rule here belongs to an add-on - see the Add-ons page"));
			}

			igSpacing();
			if (igButton(T("Reload rules"), ImVec2{ 150.0f, 0.0f })) {
				marks->Load();
			}
			Help(
				"Reads the marker files again, so you can edit them without restarting.");

			igTextDisabled("%s", marks->Status().c_str());
			igSpacing();
		}

		void DrawArousalOptions(Settings& a_settings)
		{
			igTextDisabled("%s", Arousal::GetSingleton()->Status().c_str());

			igCheckbox(T("Show a flame on the tag"), &a_settings.arousalShow);
			Help(
				"A small flame to the right of the name, empty at 0 and full at 100.");

			if (!a_settings.arousalShow) {
				return;
			}

			ColourPicker(T("Flame colour"), a_settings.arousalColour);
			igSliderInt(T("Hide below"), &a_settings.arousalMin, 0, 100, "%d", 0);
			Help(
				"Raise this so only people worth noticing carry a flame.");
			igCheckbox(T("Print the figure too"), &a_settings.arousalNumber);

			igTextDisabled("%s", T(
				"The figure comes from the other mod and only moves when that mod "
				"looks at somebody, roughly every half minute and only nearby - so a "
				"flame can be a little behind the moment."));
		}

		void DrawSubtitleOptions(Settings& a_settings)
		{
			static const char* kSpeakerModes[] = {
				"leave them alone", "hide their tag", "lift their tag"
			};
			igCombo_Str_arr(T("While somebody is speaking"), &a_settings.labelSpeakerMode,
				Translated(kSpeakerModes, static_cast<int>(std::size(kSpeakerModes))),
				static_cast<int>(std::size(kSpeakerModes)), -1);
			Help(
				"Their subtitle already shows their name, so ours is in the way.\n"
				"Hide is tidier. Lift keeps both, one above the other.");

			if (a_settings.labelSpeakerMode == 2) {
				igSliderFloat(T("Lift by"), &a_settings.labelSpeakerLift, 0.0f, 400.0f, "%.0f units", 0);
				Help("How far above the subtitle the name tag sits.");
			}
		}

		void DrawBondOptions(Settings& a_settings)
		{
			igCheckbox(T("Swap the symbol when the tally fills"), &a_settings.ostimMaraAtFull);
			Help(
				"Turns Dibella's sigil into Mara's once the count is full. Only a picture,\n"
				"nothing else changes.");

			igSpacing();
			igSeparatorText(T("Writes to your save"));

			igTextColored(ImVec4{ 1.0f, 0.75f, 0.35f, 1.0f }, "%s", T(
				"Everything else in this mod only changes what you see. What follows "
				"changes the game."));

			igCheckbox(T("Raise the relationship when the tally fills"), &a_settings.ostimPromote);
			Help(
				"This is saved in your game and stays there even if you uninstall this\n"
				"mod. There is no undo, though the old value is written to the log so you\n"
				"can put it back by hand.\n"
				"\n"
				"It only ever raises a relationship, never lowers one, and it skips\n"
				"generic NPCs where the change would not stick anyway.");

			if (a_settings.ostimPromote) {
				static const char* kRanks[] = { "Friend", "Confidant", "Ally", "Lover" };
				int                chosen = std::clamp(a_settings.ostimPromoteRank - 1, 0, 3);
				if (igCombo_Str_arr(T("Raise to"), &chosen,
						Translated(kRanks, static_cast<int>(std::size(kRanks))),
						static_cast<int>(std::size(kRanks)), -1)) {
					a_settings.ostimPromoteRank = chosen + 1;
				}
				Help(
					"Friend is the gentlest option and is enough for marriage. Lover also\n"
					"lets you take up to 500 gold of their belongings without it counting as\n"
					"theft, which you may not want.");

				igCheckbox(T("Also make them marriageable"), &a_settings.ostimPromoteMarriage);
				Help(
					"Makes them a marriage candidate. A wedding still needs a relationship of\n"
					"Friend or better and a voice the ceremony has lines for.");
			}
		}

		void DrawTitles(Settings& a_settings)
		{
			auto* titles = Titles::GetSingleton();

			igTextWrapped("%s", T(
				"A small word above the name saying what somebody is - Jarl, Blacksmith, Follower. Worked out from the game, and you can overrule it for anybody."));
			igSpacing();

			igCheckbox(T("Show titles"), &a_settings.titlesShow);
			if (a_settings.titlesShow) {
				igSliderFloat(T("Title size"), &a_settings.titleScale, 0.3f, 1.5f, "%.2fx", 0);
				Help("As a share of the name below it. 0.72 reads as a subtitle.");
				igCheckbox(T("Let each title pick its own colour"), &a_settings.titleRuleColour);
				Help(
					"Off makes every title the one colour below, which is calmer in a crowd.");
				if (!a_settings.titleRuleColour) {
					ColourPicker(T("Title colour"), a_settings.titleColour);
				}
			}

			igSpacing();
			igSeparatorText(T("Worked out from the game"));

			if (titles->Rules().empty()) {
				igTextDisabled("%s", T("no titles loaded"));
			}

			static char typed[64]{};

			if (igBeginTable("titles", 3, ImGuiTableFlags_SizingFixedFit, ImVec2{ 0.0f, 0.0f }, 0.0f)) {
				for (std::size_t i = 0; i < titles->Rules().size(); ++i) {
					auto& rule = titles->Rules()[i];
					igPushID_Int(static_cast<int>(i));
					igTableNextRow(0, 0.0f);

					igTableSetColumnIndex(0);
					igCheckbox(rule.name.c_str(), &rule.enabled);

					igTableSetColumnIndex(1);
					ColourPicker("##colour", rule.colour);

					igTableSetColumnIndex(2);
					if (igSmallButton(T("copy"))) {
						std::snprintf(typed, sizeof(typed), "%s", rule.text.c_str());
					}

					igPopID();
				}
				igEndTable();
			}
			Help(
				"Untick one and the next matching title takes over. \"copy\" puts the\n"
				"title's text in the box below, ready to give to somebody.");

			igSpacing();
			igSeparatorText(T("Titles you give out yourself"));

			static std::string message;

			igInputText(T("Title"), typed, sizeof(typed), 0, nullptr, nullptr);

			const auto target = []() -> RE::Actor* {
				if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
					if (auto handle = pick->target; handle) {
						if (auto ref = handle.get(); ref) {
							return ref->As<RE::Actor>();
						}
					}
				}
				return nullptr;
			};

			if (igButton(T("Give it to my target"), ImVec2{ 180.0f, 0.0f })) {
				auto* who = target();
				if (!who) {
					message = T("Nothing under the crosshair. Look at somebody and press it again.");
				} else if (typed[0] == '\0') {
					message = T("Type a title first.");
				} else {
					titles->Assign(who, typed);
					message = titles->SaveAssignments() ? T("saved") : T("could not write the title file");
				}
			}
			Help(
				"Gives whoever you are aiming at the title you typed. Yours wins over\n"
				"anything the mod worked out, and it is kept after a restart.");

			igSameLine(0.0f, -1.0f);
			if (igButton(T("Give it to me"), ImVec2{ 140.0f, 0.0f })) {
				if (typed[0] == '\0') {
					message = T("Type a title first.");
				} else {
					titles->Assign(RE::PlayerCharacter::GetSingleton(), typed);
					message = titles->SaveAssignments() ? T("saved") : T("could not write the title file");
				}
			}

			if (!message.empty()) {
				igTextDisabled("%s", message.c_str());
			}

			if (!titles->Assignments().empty()) {
				igSpacing();
				RE::FormID remove = 0;
				for (const auto& entry : titles->Assignments()) {
					igPushID_Int(static_cast<int>(entry.form));
					if (igSmallButton(T("forget"))) {
						remove = entry.form;
					}
					igSameLine(0.0f, -1.0f);
					if (igSmallButton(T("copy"))) {
						std::snprintf(typed, sizeof(typed), "%s", entry.title.c_str());
					}
					igSameLine(0.0f, -1.0f);
					igText("%s%s%s", entry.title.c_str(),
						entry.who.empty() ? "" : "  -  ", entry.who.c_str());
					igPopID();
				}
				if (remove != 0) {
					titles->Unassign(remove);
					titles->SaveAssignments();
				}
			}

			igSpacing();
			if (igTreeNode_Str(T("Make a new title"))) {
				static char        newTitle[64]{};
				static char        newMatch[128]{};
				static std::uint32_t newColour{ 0xC9C9C9 };
				static std::string newMessage;

				igInputText(T("New title"), newTitle, sizeof(newTitle), 0, nullptr, nullptr);
				igInputText(T("For names containing"), newMatch, sizeof(newMatch), 0, nullptr, nullptr);
				Help(
					"Who gets it: anybody whose name contains one of these pieces,\n"
					"comma separated - \"Guard, Watch\" catches both. Your own titles\n"
					"outrank the built-in ones.");
				ColourPicker(T("New title colour"), newColour);

				if (igButton(T("Add it"), ImVec2{ 120.0f, 0.0f })) {
					if (newTitle[0] == '\0' || newMatch[0] == '\0') {
						newMessage = T("It needs both a title and who it is for.");
					} else if (Titles::GetSingleton()->AddUserRule(newTitle, newColour, newMatch)) {
						newMessage = T("saved");
						newTitle[0] = '\0';
						newMatch[0] = '\0';
					} else {
						newMessage = T("could not write the title file");
					}
				}
				if (!newMessage.empty()) {
					igTextDisabled("%s", newMessage.c_str());
				}
				igTreePop();
			}

			igSpacing();
			if (igButton(T("Save switches and colours"), ImVec2{ 220.0f, 0.0f })) {
				titles->SaveUserTitles();
			}
			Help(
				"Keeps what you ticked or recoloured in the table across restarts.\n"
				"Written to its own file; the hand-written title file is never touched.");
			igSameLine(0.0f, -1.0f);
			if (igButton(T("Reload titles"), ImVec2{ 150.0f, 0.0f })) {
				titles->Load();
			}
			Help("Reads the title file again, so you can edit it without restarting.");
			igTextDisabled("%s", titles->Status().c_str());
			igSpacing();
		}

		// Bridges to other mods. A rule that reads OStim's records is not really
		// a feature of this mod, it is a bridge to somebody else's - so it gets
		// its own page, with the switch, the styling and the diagnostics for
		// each bridge kept together.
		void DrawAddons(Settings& a_settings)
		{
			auto* marks = Marks::GetSingleton();

			igTextWrapped("%s", T(
				"Add-ons show what other mods you have installed already know. None of them are required, and any of them can be left out of the install entirely - it simply will not appear here."));
			igSpacing();

			if (marks->Integrations().empty()) {
				igTextDisabled("%s", T("no add-ons found in the rule file"));
				igSpacing();
			}

			igCheckbox(T("Hide add-ons whose mod is missing"), &a_settings.hideMissingAddons);
			Help("Keeps this page to the add-ons you can actually use.");
			igSpacing();

			int hidden = 0;
			for (const auto& name : marks->Integrations()) {
				const auto presence = marks->IntegrationPresence(name);
				if (presence == 0 && a_settings.hideMissingAddons) {
					++hidden;
					continue;
				}

				igPushID_Str(name.c_str());
				igSeparatorText(name.c_str());

				bool on = a_settings.IntegrationEnabled(name);
				if (igCheckbox(T("Use this add-on"), &on)) {
					a_settings.SetIntegration(name, on);
				}

				if (presence == 1) {
					igSameLine(0.0f, -1.0f);
					igTextColored(ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f }, "%s", T("installed"));
				} else if (presence == 0) {
					igSameLine(0.0f, -1.0f);
					igTextColored(ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f }, "%s", T("not installed"));
					Help("You do not have the mod this add-on reads, so it does nothing.");
				}

				if (on) {
					for (std::size_t i = 0; i < marks->RuleCount(); ++i) {
						if (marks->Rules()[i].integration == name) {
							DrawRuleStyle(a_settings, i);
						}
					}

					if (name == Arousal::kName) {
						DrawArousalOptions(a_settings);
					} else if (name == "Floating subtitles") {
						DrawSubtitleOptions(a_settings);
					} else {
						// "When the tally fills" only means anything for an
						// add-on that has a tally, so the options only appear
						// for one that does.
						bool counts = false;
						for (const auto& rule : marks->Rules()) {
							if (rule.integration == name &&
								(rule.countFaction || !rule.countGroups.empty())) {
								counts = true;
								break;
							}
						}
						if (counts) {
							DrawBondOptions(a_settings);
						}
					}
				}

				igPopID();
				igSpacing();
			}

			if (hidden > 0) {
				igTextDisabled("%s", T("some add-ons are hidden because you do not have the mods they read"));
				igSpacing();
			}

			igSeparatorText(T("Why is the number what it is?"));
			DrawExplain();

			igSpacing();
			if (igButton(T("Reload rules"), ImVec2{ 150.0f, 0.0f })) {
				marks->Load();
			}
			Help(
				"Reads the marker files again, so you can edit them without restarting.");
			igTextDisabled("%s", marks->Status().c_str());
			igSpacing();
		}

		// Was buried under Actions, which is meant to be buttons. It is a
		// display setting and belongs with the other display settings.
		void DrawInterface(Settings& a_settings)
		{
			if (!Header(T("Hide the interface"))) {
				return;
			}

			igCheckbox(T("Hide the game's HUD"), &a_settings.hideGameHud);
			Help(
				"Hides the compass, health bars, crosshair, subtitles and the\n"
				"activation prompt. They are all one piece as far as the game is\n"
				"concerned, so they go together or not at all.\n"
				"\n"
				"Pairs with the corner readout on the People page: no permanent\n"
				"interface, and a sweep when you want to know how you are doing.");

			if (a_settings.hideGameHud) {
				static char buffer[256]{};
				if (buffer[0] == '\0') {
					std::snprintf(buffer, sizeof(buffer), "%s", a_settings.hideMenus.c_str());
				}
				if (igInputText(T("Menus to hide"), buffer, sizeof(buffer), 0, nullptr, nullptr)) {
					a_settings.hideMenus = buffer;
				}
				Help(
					"Comma separated, and case sensitive. \"HUD Menu\" is the game's own,\n"
					"and carries anything drawn into it - moreHUD among them.\n"
					"\n"
					"Other mods own their own menus, so add their names here to take\n"
					"those too. What cannot be reached this way at all is a mod drawing\n"
					"through ImGui rather than Scaleform - including this one. There is\n"
					"no general switch for those, by anyone.");

				igTextDisabled("%s", T("Currently hidden: see the log"));
			}

			igSpacing();

			igSpacing();
		}

		void DrawPresets(Settings& a_settings)
		{
			if (!Header(T("Presets"))) {
				return;
			}

			// The list is only re-read when something changes it, so this does
			// not walk the filesystem every frame the page is open.
			static std::vector<std::string> presets = Presets::List();
			static char                     nameBuffer[64]{};
			static std::string              selected;
			static std::string              message;

			const auto refresh = [] { presets = Presets::List(); };

			igTextWrapped("%s", T(
				"A preset is a copy of every setting on these pages under a name of your choosing. Loading one replaces what you have now."));

			igSpacing();

			if (igBeginListBox("##presets", ImVec2{ -1.0f, 120.0f })) {
				if (presets.empty()) {
					igTextDisabled("%s", T("none saved yet"));
				}
				for (const auto& name : presets) {
					const bool isSelected = name == selected;
					if (igSelectable_Bool(name.c_str(), isSelected, 0, ImVec2{ 0.0f, 0.0f })) {
						selected = name;
						std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", name.c_str());
					}
					if (isSelected) {
						igSetItemDefaultFocus();
					}
				}
				igEndListBox();
			}

			igInputText(T("Name"), nameBuffer, sizeof(nameBuffer), 0, nullptr, nullptr);

			const std::string typed{ nameBuffer };
			const bool        haveName = !Presets::PathFor(typed).empty();
			const bool        haveSelection = !selected.empty();

            if (igButton(T("Save"), ImVec2{ 100.0f, 0.0f })) {
				if (haveName) {
					message = a_settings.SaveAs(Presets::PathFor(typed))
								  ? std::format("saved '{}'", typed)
								  : std::format("could not write '{}'", typed);
					selected = typed;
					refresh();
				} else {
					message = "give it a name first";
				}
			}
			Help(
				"Saves every setting under the name in the box, overwriting one that\n"
				"already has it.");

			igSameLine(0.0f, -1.0f);
			if (igButton(T("Load"), ImVec2{ 100.0f, 0.0f })) {
				if (haveSelection) {
					Sense::GetSingleton()->Cancel();
					message = a_settings.LoadFrom(Presets::PathFor(selected))
								  ? std::format("loaded '{}'", selected)
								  : std::format("could not read '{}'", selected);
					Locale::Load(a_settings.language);
				} else {
					message = "pick one from the list first";
				}
			}

			igSameLine(0.0f, -1.0f);
			if (igButton(T("Rename"), ImVec2{ 100.0f, 0.0f })) {
				if (haveSelection && haveName && typed != selected) {
					if (Presets::Rename(selected, typed)) {
						message = std::format("renamed to '{}'", typed);
						selected = typed;
					} else {
						message = "rename failed - is that name already taken?";
					}
					refresh();
				} else {
					message = "select one, then type the new name";
				}
			}

			igSameLine(0.0f, -1.0f);
			if (igButton(T("Delete"), ImVec2{ 100.0f, 0.0f })) {
				if (haveSelection) {
					message = Presets::Delete(selected) ? std::format("deleted '{}'", selected)
														: "delete failed";
					selected.clear();
					nameBuffer[0] = '\0';
					refresh();
				}
			}

			igSameLine(0.0f, -1.0f);
			if (igButton(T("Refresh"), ImVec2{ 100.0f, 0.0f })) {
				refresh();
			}

			if (!message.empty()) {
				igTextDisabled("%s", message.c_str());
			}

			igSpacing();
		}

		// ------------------------------------------------------------ the page

		void DrawStatus()
		{
			auto* sense = Sense::GetSingleton();

			if (sense->Ready()) {
				igTextColored(ImVec4{ 0.45f, 0.85f, 0.45f, 1.0f }, "%s", sense->Status().c_str());
			} else {
				igTextColored(ImVec4{ 1.0f, 0.35f, 0.35f, 1.0f }, "%s: %s", T("not armed"), sense->Status().c_str());
				igTextWrapped("%s", T(
					"ScavengerSense.esp is not loading, and nothing can light up without it. Check that it is enabled in your mod manager."));
			}
		}

		void __stdcall Render()
		{
			auto& settings = *Settings::GetSingleton();

			// The status line and the two buttons you reach for most stay outside
			// the tabs. Everything else is one click away by subject, rather than
			// nine collapsing headers deep in a single column - the page had grown
			// past the point where scrolling it was a reasonable way to find
			// anything.
			DrawStatus();

			auto* sense = Sense::GetSingleton();
			if (igButton(T("Run a sweep"), ImVec2{ 120.0f, 0.0f })) {
				sense->OnHotkey();
			}
			Help("Starts a sweep from where you are standing. Close the menu to watch.");
			igSameLine(0.0f, -1.0f);
			if (igButton(T("Cancel"), ImVec2{ 90.0f, 0.0f })) {
				sense->Cancel();
			}
			igSameLine(0.0f, -1.0f);
			if (igButton(T("Save to INI"), ImVec2{ 120.0f, 0.0f })) {
				settings.Save();
			}
			Help(
				"Changes take effect straight away. Saving keeps them after you quit.");

			igSpacing();

			if (!igBeginTabBar("survivor-sense", ImGuiTabBarFlags_None)) {
				return;
			}

			// Labels are left wide enough for the longest translated string, and
			// the sliders take what is left, so nothing jumps around between
			// languages.
			igPushItemWidth(-260.0f);

			if (igBeginTabItem(T("Sweep"), nullptr, 0)) {
				igSpacing();
				DrawScan(settings);
				DrawPulse(settings);
				DrawChime(settings);
				igEndTabItem();
			}


			if (igBeginTabItem(T("Look"), nullptr, 0)) {
				igSpacing();
				DrawVision(settings);
				DrawRing(settings);
				DrawTint(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Tags"), nullptr, 0)) {
				igSpacing();
				DrawLabels(settings);
				DrawPeopleReading(settings);
				DrawMarkers(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("What shows"), nullptr, 0)) {
				igSpacing();
				DrawCategories(settings);
				DrawSelf(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Tracks"), nullptr, 0)) {
				igSpacing();
				DrawTracks(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Vitals"), nullptr, 0)) {
				igSpacing();
				DrawBarStyle(settings);
				DrawMyVitals(settings);
				DrawStatsRowCfg(settings);
				DrawOtherVitals(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Titles"), nullptr, 0)) {
				igSpacing();
				DrawTitles(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Add-ons"), nullptr, 0)) {
				igSpacing();
				DrawAddons(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Setup"), nullptr, 0)) {
				igSpacing();
				DrawInterface(settings);
				DrawPresets(settings);
				DrawActions(settings);
				igEndTabItem();
			}

			if (igBeginTabItem(T("Keys"), nullptr, 0)) {
				igSpacing();
				DrawHotkey(settings);
				DrawTrailKeys(settings);
				igTextDisabled("%s",
					T("The settings menu itself opens on SKSE Menu Framework's key - F1 out of the box, set in its own INI."));
				igEndTabItem();
			}

			igPopItemWidth();
			igEndTabBar();
		}
	}

	bool Register()
	{
		AddSectionItem(kSectionPath, Render);
		logger::info("registered menu page at {}", kSectionPath);
		return true;
	}
}
