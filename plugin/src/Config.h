#pragma once

namespace SS
{
	// Object categories. Order matters: it is the order used in the INI and in
	// Categorise(). kCount is the sentinel.
	enum class Category : std::uint32_t
	{
		kContainer = 0,
		kDoor,
		kFlora,
		kGear,
		kAlchemy,
		kBook,
		kValuable,
		kActivator,
		kActor,

		kCount
	};

	inline constexpr std::size_t kCategoryCount = static_cast<std::size_t>(Category::kCount);

	struct CategorySettings
	{
		bool          enabled{ true };
		std::uint32_t colour{ 0xFFD24A };  // 0xRRGGBB
		// Rim only, no interior wash. Reads as a clean silhouette, which suits
		// actors far better than a glowing body - and it is much easier to tell
		// two overlapping people apart.
		bool          outlineOnly{ false };
	};

	// What a person is to you, which is the only thing you actually want to know
	// about them at a glance.
	enum class Disposition : std::uint8_t
	{
		kNone = 0,  // not a person
		kHostile,   // wants you dead right now
		kRival,     // dislikes you, has not drawn a weapon
		kNeutral,   // a stranger
		kFriend,    // friend or acquaintance who likes you
		kConfidant, // trusted
		kAlly,      // ally, or following you
		kLover,     // lover
		kDead,

		// You. Not a relationship - what you currently are, which is the one
		// thing about the player worth a shape of its own.
		kSelfMortal,
		kSelfVampire,
		kSelfWerewolf,

		kCount
	};

	// Which actors are worth sensing. Corpses are lootable, so they count by
	// default; dead-only is the "where did that kill land" case.
	enum class ActorFilter : std::uint32_t
	{
		kAll = 0,
		kLivingOnly,
		kDeadOnly,

		kCount
	};

	// How a marker's tally is shown.
	enum class CountStyle : std::uint32_t
	{
		kNumber = 0,  // a small figure beside the icon
		kFill,        // the icon fills up as the tally rises
		kHidden,

		kCount
	};

	// Per-marker appearance, kept in the settings rather than in the rule file.
	//
	// The rule file says who a marker applies to, and is hand written with a
	// page of comments explaining the form IDs. Rewriting it from a UI would
	// throw all of that away, so what the menu edits lives here instead, keyed
	// by rule name - which also means it travels with a preset.
	struct MarkStyle
	{
		std::string   name;
		bool          enabled{ true };
		std::uint32_t colour{ 0xFFFFFF };
		CountStyle    countStyle{ CountStyle::kFill };
		std::uint32_t countFull{ 8 };  // tally that fills the icon completely
		bool          overrideColour{ false };
	};

	// One switch per mod that a rule file reads.
	//
	// A rule that reads OStim's records is not really our feature, it is a
	// bridge to somebody else's. Grouping those rules under the name of the mod
	// they read gives one obvious place to turn the whole bridge off, and one
	// obvious place to say whether the mod is even installed.
	struct IntegrationState
	{
		std::string name;
		bool        enabled{ true };
	};

	// Where the vitals bars sit relative to the tag they belong to.
	enum class BarPlace
	{
		kBelow = 0,
		kAbove,
		kLeft,
		kRight
	};

	// Where a persistent readout is pinned. kOff is not a corner - it is the
	// whole feature switched off, which keeps one setting where there would
	// otherwise be two.
	enum class Corner
	{
		kOff = 0,
		kTopLeft,
		kTopRight,
		kBottomLeft,
		kBottomRight
	};

	// When a bar is worth drawing at all.
	enum class ShowWhen
	{
		kAlways = 0,
		kOnChange,  // for a few seconds after the value moves
		kNotFull
	};

	// How the bound button has to be pressed before a sweep fires.
	enum class Trigger : std::uint32_t
	{
		kPress = 0,   // fires the moment the button goes down
		kDoubleTap,   // two presses inside doubleTapWindow
		kHold,        // held for at least holdTime

		kCount
	};

	struct Settings
	{
		// [General]
		bool        debug{ false };
		std::string language{ "english" };  // picks ScavengerSense_<language>.ini
		// Notice when a game menu opens and get out of its way. Kill switch: if
		// something in the load order parks a menu on the stack that we wrongly
		// judge as blocking, turning this off restores the old behaviour without
		// needing a new build.
		bool        menuAware{ true };
		// End the sweep outright when a menu opens, rather than just hiding the
		// overlay. This also clears the effect shaders off every lit object,
		// which is the only way to be certain none of them is in the way of what
		// the menu wants to draw.
		bool        stopOnMenu{ true };
		// Treat a conversation like a menu. It is not one as far as the engine's
		// flags go - talking does not pause the game - but a pulse going off
		// mid-dialogue breaks the scene more than any inventory screen does.
		bool        muteInDialogue{ true };
		// Hide the game's own interface. "HUD Menu" is the vanilla one and also
		// carries anything drawn into it, moreHUD included. Other mods own their
		// own menus, so the list is editable rather than hard coded - add a name
		// and it goes too. Menus drawn through ImGui rather than Scaleform
		// cannot be reached this way at all.
		bool        hideGameHud{ false };
		std::string hideMenus{ "HUD Menu" };

		// [Hotkey]
		std::int32_t keyboard{ 21 };  // DX scan code, 21 = Y
		std::int32_t gamepad{ 276 };  // A. -1 leaves the controller unbound
		Trigger      trigger{ Trigger::kDoubleTap };
		float        doubleTapWindow{ 0.30f };  // max gap between the two taps
		float        holdTime{ 0.45f };         // how long the button must be held
		bool         toggle{ false };
		float        cooldown{ 0.5f };

		// [Scan]
		float         radius{ 2500.0f };
		std::uint32_t maxObjects{ 128 };
		bool          ignoreUnnamed{ true };
		bool          ignoreHarvested{ true };
		// When there are more candidates than slots, sample evenly across the
		// whole distance range instead of taking the nearest N. Without this the
		// wave only ever covers the few metres nearest you.
		bool          spreadAcrossRadius{ true };
		// How hard the spread leans toward what is close to you. 1.0 samples the
		// sorted candidates evenly, which sounds fair and is not: with 450
		// candidates and 96 slots, evenly means keeping one object in five, and
		// the crate at your feet is as likely to be dropped as one across the
		// valley. Above 1.0 the picks bunch up at the near end, so everything
		// within arm's reach survives and the far half is sampled instead.
		float         nearBias{ 2.2f };

		// [Pulse]
		// Seconds for the wavefront to travel from the nearest lit object to the
		// furthest one. Expressed as a time rather than a speed because the
		// objects are almost never spread evenly - in a cluttered room the
		// nearest 96 hits can all sit inside a few metres, and a fixed speed
		// then crosses them in a tenth of a second and reads as instant.
		float sweepTime{ 1.2f };  // 0 lights everything on the same frame
		float duration{ 3.0f };      // how long each object stays lit
		float fadeIn{ 0.15f };
		float fadeOut{ 0.40f };

		// [Vision]
		bool  throughWalls{ true };
		float edgeFalloff{ 1.6f };
		float glowStrength{ 1.0f };
		float pulseAmplitude{ 0.35f };
		float pulseFrequency{ 1.2f };

		// [Labels]
		bool  labelsEnabled{ true };
		bool  labelBackdrop{ false };
		float labelMaxDistance{ 1800.0f };  // 0 = no limit
		// The framework rasterises exactly one font size (its FontSizeMedium,
		// 32px by default) and that is far too big for tags floating over loot.
		// We draw at this size instead by scaling the same glyphs. 0 = whatever
		// the menu font happens to be.
		float labelFontSize{ 20.0f };
		// People matter more than crates, so their tags are bigger and sit over
		// the head rather than at the feet.
		float labelActorScale{ 1.4f };
		bool  labelIcons{ true };
		// Highlight things you have favourited - alchemy ingredients above all.
		// Favourites are a per-save inventory flag, so this is read fresh from
		// the player each sweep rather than remembered anywhere.
		bool          favouriteHighlight{ true };
		std::uint32_t favouriteColour{ 0xFFE066 };
		float         favouriteScale{ 1.2f };
		// Fold tags with the same name that are close together into one, so a
		// wine cellar reads "Wine x40" rather than forty overlapping words.
		// Distance in game units; 0 disables. People are never folded.
		float labelMergeDistance{ 320.0f };
		// Push overlapping tags apart on screen. Nearest wins its position and
		// the rest step upward, with a hairline back to what they name.
		bool          labelDeclutter{ true };
		std::uint32_t labelDeclutterSteps{ 6 };

		// [Ring]
		// The sonar ring: a circle on the ground that travels out from your feet
		// in step with the highlight wave. Drawn in world space, one projected
		// point per segment, so it follows the terrain instead of being a flat
		// circle pasted on the screen.
		bool          ringEnabled{ true };
		std::uint32_t ringColour{ 0x8FE3FF };
		float         ringThickness{ 2.5f };
		float         ringGlow{ 6.0f };     // width of the soft pass behind the line
		std::uint32_t ringEchoes{ 3 };      // trailing rings, 1 = just the leading edge
		float         ringEchoGap{ 0.09f };  // seconds between them
		float         ringHeight{ 10.0f };   // lift off the ground so it does not z-fight
		float         ringLead{ 1.35f };     // ring travels this much further than the wave

		// [Tint]
		// Two completely different routes to "the world goes grey".
		//
		// tintEnabled drives ImageSpaceManager's override base data directly -
		// the same input the vanilla tonemapper reads, and the same one
		// Community Shaders' ISHDR replacement still consumes. That is a real
		// desaturation, not a colour laid on top.
		//
		// tintUseImod is the old route through the shipped IMAD record. It is
		// kept for ENB users whose preset ignores the cinematic params anyway,
		// and is off by default because the override is strictly better.
		bool  tintEnabled{ true };
		// Which doors to push on. Both by default: which one actually reaches
		// the frame depends on what owns the tonemapper, and pushing on both
		// costs nothing since they carry identical values.
		bool  tintUseOverride{ true };    // ImageSpaceManager::overrideBaseData
		bool  tintWriteCurrent{ true };   // write through currentBaseData
		bool  tintUseImod{ false };       // the IMAD record in the esp
		// Our own full-screen shader pass, run after everything else in the
		// frame. This is the only route that works under Community Shaders,
		// because it does not ask anything's permission - but it is also the
		// only part of the mod that touches the graphics pipeline directly, so
		// it is off until you ask for it.
		bool  postProcess{ false };
		// Which surface the pass grades.
		//
		//   backbuffer - the swap chain buffer, which is by definition what you
		//                are about to look at
		//   bound      - whatever render target the game left bound
		//
		// This is not a detail. The menu framework hooks the end of the frame
		// and draws onto whatever happens to still be bound, which need not be
		// the surface being presented - and if it is a UI compositing target,
		// grading it recolours the interface and leaves the world alone.
		bool  postProcessBackBuffer{ true };
		float tintSaturation{ 0.35f };
		float tintBrightness{ 0.82f };
		float tintContrast{ 1.18f };
		float tintStrength{ 1.0f };
		// Colour cast fed to the image space tint, which is a proper grade
		// rather than a sheet of colour over the frame.
		std::uint32_t tintCastColour{ 0x2E6BB0 };
		float         tintCastAmount{ 0.22f };

		// The composited overlay. Now mostly a vignette: the flat part is what
		// made it look like a sheet of cellophane, so most of the strength is
		// spent darkening the edges instead.
		bool          washEnabled{ true };
		std::uint32_t washColour{ 0x0A1526 };
		float         washStrength{ 0.28f };
		float         washFlat{ 0.12f };  // share of the strength used flat, 0..1

		// [Sound]
		// The chime shipped in the esp plays unless a formID override says
		// otherwise; the override exists for people who want a vanilla sound
		// (or another mod's) instead of editing the wav on disk.
		bool          soundEnabled{ true };
		// Full by default: the engine only attenuates, so anything below 1.0
		// is loudness the player cannot get back from the slider.
		float         soundVolume{ 1.0f };
		std::uint32_t soundFormID{ 0 };

		ActorFilter actorFilter{ ActorFilter::kAll };

		// Colour people by whether they want to kill you rather than by the fact
		// that they are people. "There is someone there" is much less useful
		// than "there is someone there and they are hostile".
		bool          actorByDisposition{ true };
		// Go further and read the relationship record, which is what the game
		// itself uses for "who is this to me". Not fighting you is not the same
		// as liking you.
		bool          actorByRelationship{ true };
		std::uint32_t hostileColour{ 0xFF3B30 };
		std::uint32_t neutralColour{ 0xFFC53D };
		std::uint32_t friendlyColour{ 0x4ADE6A };
		std::uint32_t rivalColour{ 0xE07B39 };
		std::uint32_t loverColour{ 0xFF7FBF };
		std::uint32_t allyColour{ 0x59D9E8 };
		std::uint32_t deadColour{ 0x9AA3AD };

		// [Self] - the player, tagged like anybody else
		bool          scanPlayer{ true };
		std::uint32_t selfColour{ 0xF2F2F2 };
		float         selfScale{ 1.25f };
		bool          selfIcon{ true };
		// Health, magicka and stamina under your own name for the length of a
		// sweep. Deliberately only yours: a bar over every person in the radius
		// is a wall of colour, and for anybody in combat TrueHUD already draws
		// one. This is for playing with the game's own HUD turned off.
		bool          selfBars{ true };
		// One rule, shared with the corner readout and with other people's bars.
		// This used to be a bool meaning "show bars that are full", which said
		// the same thing as ShowWhen in a second vocabulary.
		ShowWhen      selfBarsWhen{ ShowWhen::kAlways };
		float         selfBarWidth{ 120.0f };    // pixels at 1080p, scaled with the tag
		float         selfBarHeight{ 5.0f };
		BarPlace      selfBarPlace{ BarPlace::kBelow };
		// Slant, as a multiple of thickness. The whole reason the bars read as a
		// piece of hardware rather than a progress widget: a sheared quad has a
		// direction, and a rectangle does not.
		float         selfBarShear{ 0.9f };
		// Notches cut across the bar. 0 draws it smooth.
		std::int32_t  selfBarSegments{ 10 };
		// Fine placement of the over-head bar stack, added after the place rule
		// has chosen a side. Pixels at the tag's scale.
		float         selfBarOffsetX{ 0.0f };
		float         selfBarOffsetY{ 0.0f };
		// Each row set back a little from the one before, so the stack reads as
		// lying on a surface angled away rather than painted flat on the screen.
		float         selfBarPerspective{ 0.10f };
		bool          selfBarGlow{ true };
		// Muted to sit with Oathvein: brick between its hostile and rival reds,
		// then its own ally blue and flora green unchanged.
		std::uint32_t selfHealthColour{ 0xC4564A };
		std::uint32_t selfMagickaColour{ 0x7FA8C4 };
		std::uint32_t selfStaminaColour{ 0x8FA86B };
		std::uint32_t selfBarFrameColour{ 0x3A4A57 };

		// A persistent readout pinned to a corner of the screen, drawn whether
		// or not a sweep is running. Off by default: this mod is a sweep, and
		// turning it into an always-on HUD should be the player's decision.
		Corner   selfHudCorner{ Corner::kOff };
		ShowWhen selfHudShow{ ShowWhen::kOnChange };
		float    selfHudLinger{ 3.0f };  // seconds it stays up after a change
		float    selfHudFade{ 0.6f };    // of that, how long it spends fading
		float    selfHudX{ 48.0f };
		float    selfHudY{ 48.0f };
		float    selfHudScale{ 1.6f };

		// [Vitals] - the same bars over other people, for the length of a sweep.
		// Never persistent: a bar over everyone in the radius all the time is
		// what TrueHUD is for, and it does it better.
		bool     vitalsActors{ false };
		bool     vitalsActorsAll{ false };  // false draws health only
		// Only people actually hostile to you. Judged from the engine's own
		// hostility check, not from a faction list, so it covers anyone a mod
		// has made an enemy. Rivals - people who dislike you but have not
		// drawn a weapon - do not count.
		bool     vitalsActorsHostileOnly{ false };
		ShowWhen vitalsActorsWhen{ ShowWhen::kNotFull };
		// Draw the ceiling a survival mod has taken away. The bar keeps its full
		// length; the span above the reduced maximum becomes a dead zone instead
		// of quietly rescaling the bar and pretending nothing happened.
		bool     barsLostMax{ true };
		// Frost over lost health, smoke over lost magicka and stamina. Off draws
		// the dead zone plain.
		bool     barsLostFx{ true };
		// In combat, anyone you have hit keeps bars over their head until the
		// fight ends - no sweep needed. Off by default: TrueHUD's recent-damage
		// bars cover the same ground, so this is for load orders without a
		// combat HUD.
		bool     combatBars{ false };

		// [Labels] continued - behaviour that needs the main thread every frame
		bool labelsFollow{ true };
		// off / hide / lift, for getting out of the way of a subtitle mod
		int  labelSpeakerMode{ 1 };
		float labelSpeakerLift{ 90.0f };

		// [Titles] - a word above the name saying what someone is
		bool          titlesShow{ true };
		float         titleScale{ 0.72f };
		std::uint32_t titleColour{ 0xC9C9C9 };
		bool          titleRuleColour{ true };

		// [Arousal] - the SLO Aroused NG bridge
		bool          arousalShow{ true };
		std::uint32_t arousalColour{ 0xFF5A7A };
		int           arousalMin{ 20 };  // hide below this, so calm people stay clean
		bool          arousalNumber{ false };

		// [Bonds] - the only settings here that write to your save
		bool ostimPromote{ false };
		int  ostimPromoteRank{ 1 };  // 1 friend, 4 lover
		bool ostimPromoteMarriage{ false };
		bool ostimMaraAtFull{ true };

		// [Markers] - appearance overrides, matched to rules by name
		std::vector<MarkStyle> markStyles;

		// [Integrations] - one entry per mod named by the rule file
		bool hideMissingAddons{ true };
		std::vector<IntegrationState> integrations;

		// [Categories]
		CategorySettings categories[kCategoryCount]{};

		void Load();

		// Rewrites the INI from the current values, comments and all. With no
		// path this is the live settings file; with one it writes a preset.
		bool Save() const;
		bool SaveAs(const std::string& a_path) const;
		bool LoadFrom(const std::string& a_path);

		// The style block for a rule, created with sensible defaults the first
		// time a rule is seen.
		[[nodiscard]] MarkStyle& StyleFor(const std::string& a_rule, std::uint32_t a_ruleColour);

		// Integrations default to on, so a rule file that names a new one
		// starts working without anybody having to find a checkbox first.
		[[nodiscard]] bool IntegrationEnabled(std::string_view a_name) const;
		void               SetIntegration(std::string_view a_name, bool a_enabled);

		void RestoreDefaults();

		[[nodiscard]] static Settings* GetSingleton();
	};

	[[nodiscard]] const char* CategoryName(Category a_category);

	// "press" / "double" / "hold", as written in the INI.
	[[nodiscard]] const char* TriggerName(Trigger a_trigger);

	// "all" / "living" / "dead", as written in the INI.
	[[nodiscard]] const char* ActorFilterName(ActorFilter a_filter);

	// "number" / "fill" / "hidden", as written in the INI.
	[[nodiscard]] const char* CountStyleName(CountStyle a_style);

	// Named preset files under ScavengerSense/presets.
	namespace Presets
	{
		[[nodiscard]] std::vector<std::string> List();
		[[nodiscard]] std::string              PathFor(std::string_view a_name);
		bool                                   Delete(std::string_view a_name);
		bool                                   Rename(std::string_view a_from, std::string_view a_to);
	}
}
