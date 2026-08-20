#include "Sense.h"

#include "Arousal.h"
#include "Relations.h"
#include "Titles.h"

#include "GameMenus.h"
#include "Locale.h"
#include "Marks.h"
#include "PostFX.h"
#include "Timing.h"
#include "Vision.h"

#include <Windows.h>

#include "TrueHUDAPI.h"

namespace SS
{
	namespace
	{
		constexpr auto  kPluginFile = "ScavengerSense.esp"sv;
		constexpr auto  kShaderBase = static_cast<RE::FormID>(0x000800);
		constexpr auto  kShaderCount = std::size_t{ 128 };
		constexpr auto  kImodFormID = static_cast<RE::FormID>(0x000880);
		constexpr auto  kSweepSoundFormID = static_cast<RE::FormID>(0x000890);
		constexpr float kTickSeconds = 1.0f / 60.0f;

		// TrueHUD's plugin API, when TrueHUD is in the load order. Only used
		// to ask it to dismiss its own bar over somebody ours is covering.
		TRUEHUD_API::IVTrueHUD1* g_trueHud = nullptr;

		[[nodiscard]] RE::Color ToColor(std::uint32_t a_rgb, float a_scale)
		{
			const auto channel = [a_scale](std::uint32_t a_value) {
				const auto scaled = static_cast<float>(a_value) * a_scale;
				return static_cast<std::uint8_t>(std::clamp(scaled, 0.0f, 255.0f));
			};

			RE::Color colour;
			colour.red = channel((a_rgb >> 16) & 0xFF);
			colour.green = channel((a_rgb >> 8) & 0xFF);
			colour.blue = channel(a_rgb & 0xFF);
			colour.alpha = 0;
			return colour;
		}

		// The plugin ships each cinematic channel of the image space modifier as a
		// four key envelope: neutral -> target -> target -> neutral. Rewriting the
		// keys in place is what lets the INI drive the tint without rebuilding the
		// esp, and it is also how the tint gets stretched to match the wave length.
		//
		// Everything is guarded: if the interpolator does not look exactly like
		// what make_esp.py wrote, we leave it alone and the baked-in values apply.
		void RewriteEnvelope(RE::NiFloatInterpolator* a_interpolator, float a_duration, float a_target)
		{
			if (!a_interpolator) {
				return;
			}

			auto* data = a_interpolator->floatData.get();
			if (!data || !data->keys || data->numKeys != 4 || data->keySize != 8) {
				return;
			}

			// NiFloatKey (linear) is { float time; float value; }
			auto* keys = reinterpret_cast<float*>(data->keys);

			keys[0] = 0.0f;
			keys[1] = 1.0f;
			keys[2] = 0.10f * a_duration;
			keys[3] = a_target;
			keys[4] = 0.88f * a_duration;
			keys[5] = a_target;
			keys[6] = a_duration;
			keys[7] = 1.0f;
		}

		// Sample the ground under a ring of points around the player so the
		// render thread can draw a circle that follows the terrain.
		//
		// Sampling happens once, here, on the main thread: GetLandHeight touches
		// the loaded cell grid and the render thread has no business doing that.
		// A few thousand lookups once per sweep is nothing.
		Labels::Ring BuildRing(
			const RE::NiPoint3& a_centre, float a_maxRadius, float a_startsAt, float a_endsAt)
		{
			constexpr std::uint32_t kAngles = 72;
			constexpr std::uint32_t kSteps = 28;
			constexpr float         kMaxStepClimb = 220.0f;  // per radial step
			constexpr float         kTwoPi = 6.28318530718f;

			Labels::Ring ring;
			ring.centre = a_centre;
			ring.minRadius = 0.0f;
			ring.maxRadius = a_maxRadius;
			ring.startAt = a_startsAt;
			ring.endAt = a_endsAt;
			ring.angles = kAngles;
			ring.steps = kSteps;
			ring.heights.assign(static_cast<std::size_t>(kAngles) * kSteps, a_centre.z);

			auto* tes = RE::TES::GetSingleton();
			if (!tes) {
				return ring;
			}

			for (std::uint32_t a = 0; a < kAngles; ++a) {
				const auto theta = kTwoPi * static_cast<float>(a) / static_cast<float>(kAngles);
				const auto dx = std::cos(theta);
				const auto dy = std::sin(theta);

				// Walk outward and carry the last known height forward. Indoors
				// there is no terrain at all and every lookup fails, which lands
				// us on a flat ring at floor level - which is right.
				float last = a_centre.z;
				for (std::uint32_t s = 0; s < kSteps; ++s) {
					const auto radius =
						a_maxRadius * static_cast<float>(s) / static_cast<float>(kSteps - 1);
					RE::NiPoint3 probe{ a_centre.x + dx * radius, a_centre.y + dy * radius, a_centre.z };

					float height{};
					if (tes->GetLandHeight(probe, height)) {
						// A cliff edge would otherwise fling one segment of the
						// ring hundreds of feet and draw a spike across the screen.
						last = std::clamp(height, last - kMaxStepClimb, last + kMaxStepClimb);
					}

					ring.heights[static_cast<std::size_t>(a) * kSteps + s] = last;
				}
			}

			return ring;
		}

		// Everything the player has flagged as a favourite, by base form.
		//
		// Favourite is a per-item flag inside the player's own inventory, not a
		// property of the thing in the world - so the only way to know a jazbay
		// on a bush is one you care about is to ask whether the jazbay in your
		// pack is favourited. Gathered once per sweep; the inventory walk is far
		// too expensive to repeat per object.
		[[nodiscard]] std::unordered_set<RE::FormID> FavouritedBases()
		{
			std::unordered_set<RE::FormID> favourites;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return favourites;
			}

			for (const auto& [object, entry] : player->GetInventory()) {
				if (object && entry.second && entry.second->IsFavorited()) {
					favourites.insert(object->GetFormID());
				}
			}

			return favourites;
		}

		// Whether the shader pass is carrying the grade this sweep. Deliberately
		// not "has it run yet": at the moment a wave starts it has not, and we
		// still must not engage the image space route only to fight it a frame
		// later.
		[[nodiscard]] bool UsingPostFX()
		{
			return Settings::GetSingleton()->postProcess && !PostFX::GetSingleton()->Failed();
		}

		// What this person is to the player: an icon shape and a colour.
		//
		// Deliberately not Actor::IsDead(). That is a virtual taking a
		// defaulted "not essential" argument whose meaning is not obvious, and
		// it was reporting every living NPC as a corpse. The life state is a
		// four bit field on the actor with named values and no ambiguity - if
		// it says kAlive, they are alive.
		struct Reading
		{
			Disposition   icon{ Disposition::kNone };
			std::uint32_t colour{ 0 };
		};

		// Health, magicka and stamina as fractions of their maximum, plus how
		// much of that maximum is actually available right now.
		//
		// The scale of the bar is the PEAK: the permanent value, widened by a
		// fortify effect when one is up. A survival mod's fatigue lands as a
		// negative temporary modifier, and that shrinks the available ceiling
		// (a_cap) without shrinking the bar - so the span the mod has taken
		// shows as a dead zone instead of the bar quietly rescaling.
		void ReadVitals(RE::Actor* a_actor, float (&a_out)[3], float (&a_cap)[3],
			float* a_peak = nullptr)
		{
			auto* values = a_actor ? a_actor->AsActorValueOwner() : nullptr;
			if (!values) {
				return;
			}
			constexpr RE::ActorValue kWanted[3] = { RE::ActorValue::kHealth,
				RE::ActorValue::kMagicka, RE::ActorValue::kStamina };
			for (int i = 0; i < 3; ++i) {
				const float now = values->GetActorValue(kWanted[i]);
				const float temp = a_actor->GetActorValueModifier(
					RE::ACTOR_VALUE_MODIFIER::kTemporary, kWanted[i]);
				const float permanent = values->GetPermanentActorValue(kWanted[i]);
				const float peak = permanent + std::max(temp, 0.0f);
				const float ceiling = permanent + temp;
				a_out[i] = peak > 0.0f ? std::clamp(now / peak, 0.0f, 1.0f) : -1.0f;
				a_cap[i] = peak > 0.0f ? std::clamp(ceiling / peak, 0.0f, 1.0f) : 1.0f;
				if (a_peak) {
					a_peak[i] = peak > 0.0f ? peak : -1.0f;
				}
			}
		}

		// What is in someone's hands, as a drawable shape. Equipped, not
		// necessarily drawn: the sense reads what they carry ready, the same
		// way it reads everything else.
		[[nodiscard]] WeaponKind ClassifyWeapon(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return WeaponKind::kNone;
			}

			const auto kindOf = [](RE::TESForm* a_form) -> WeaponKind {
				if (!a_form) {
					return WeaponKind::kNone;
				}
				if (const auto* weapon = a_form->As<RE::TESObjectWEAP>(); weapon) {
					switch (weapon->GetWeaponType()) {
					case RE::WEAPON_TYPE::kHandToHandMelee:
						return WeaponKind::kFists;
					case RE::WEAPON_TYPE::kOneHandSword:
						return WeaponKind::kSword;
					case RE::WEAPON_TYPE::kOneHandDagger:
						return WeaponKind::kDagger;
					case RE::WEAPON_TYPE::kOneHandAxe:
						return WeaponKind::kAxe;
					case RE::WEAPON_TYPE::kOneHandMace:
						return WeaponKind::kMace;
					case RE::WEAPON_TYPE::kTwoHandSword:
						return WeaponKind::kGreatsword;
					case RE::WEAPON_TYPE::kTwoHandAxe:
						return WeaponKind::kBattleaxe;
					case RE::WEAPON_TYPE::kBow:
						return WeaponKind::kBow;
					case RE::WEAPON_TYPE::kCrossbow:
						return WeaponKind::kCrossbow;
					case RE::WEAPON_TYPE::kStaff:
						return WeaponKind::kStaff;
					default:
						return WeaponKind::kNone;
					}
				}
				return a_form->Is(RE::FormType::Spell) ? WeaponKind::kSpell : WeaponKind::kNone;
			};

			if (const auto right = kindOf(a_actor->GetEquippedObject(false)); right != WeaponKind::kNone) {
				return right;
			}
			if (const auto left = kindOf(a_actor->GetEquippedObject(true)); left != WeaponKind::kNone) {
				return left;
			}
			return WeaponKind::kFists;
		}

		// The racial emblem: classified from the race's editor id, which
		// survives localisation, with the vampire and werewolf riders read
		// from the keywords the game itself conditions on - so a modded
		// strain that copied them counts too. Creatures come out kNone;
		// their name already says what they are.
		[[nodiscard]] std::pair<RaceKind, RaceMark> ClassifyRace(RE::Actor* a_actor)
		{
			auto* race = a_actor ? a_actor->GetRace() : nullptr;
			if (!race) {
				return { RaceKind::kNone, RaceMark::kNone };
			}

			std::string id;
			if (const char* edid = race->GetFormEditorID(); edid) {
				id = edid;
			}
			std::transform(id.begin(), id.end(), id.begin(),
				[](unsigned char a_ch) { return static_cast<char>(std::tolower(a_ch)); });

			auto mark = RaceMark::kNone;
			if (a_actor->HasKeywordString("WerewolfKeyword") || id.contains("werewolf")) {
				mark = RaceMark::kWerewolf;
			} else if (a_actor->HasKeywordString("Vampire") || id.contains("vampire")) {
				mark = RaceMark::kVampire;
			}

			auto kind = RaceKind::kNone;
			if (id.contains("nord")) {
				kind = RaceKind::kNord;
			} else if (id.contains("imperial")) {
				kind = RaceKind::kImperial;
			} else if (id.contains("breton")) {
				kind = RaceKind::kBreton;
			} else if (id.contains("redguard")) {
				kind = RaceKind::kRedguard;
			} else if (id.contains("highelf") || id.contains("altmer")) {
				kind = RaceKind::kAltmer;
			} else if (id.contains("woodelf") || id.contains("bosmer")) {
				kind = RaceKind::kBosmer;
			} else if (id.contains("darkelf") || id.contains("dunmer")) {
				kind = RaceKind::kDunmer;
			} else if (id.contains("orc")) {
				kind = RaceKind::kOrc;
			} else if (id.contains("khajiit")) {
				kind = RaceKind::kKhajiit;
			} else if (id.contains("argonian")) {
				kind = RaceKind::kArgonian;
			} else if (mark != RaceMark::kNone || race->HasKeywordString("ActorTypeNPC")) {
				// A humanoid we cannot place - Elder, Dremora, a mod race -
				// still deserves a bust rather than silence.
				kind = RaceKind::kMan;
			}

			return { kind, mark };
		}

		// What the player currently is. Three states, in the order that matters:
		// a beast form overrides everything, then vampirism, then nothing has
		// happened to you.
		//
		// Deliberately not driven by form IDs. A race pointer would have to be
		// looked up per DLC and would miss every mod that adds its own beast
		// race, whereas the keywords are what the game itself conditions on and
		// what mod authors copy - so a modded vampire race carries them too.
		[[nodiscard]] Disposition SelfState(RE::Actor* a_player)
		{
			if (!a_player) {
				return Disposition::kSelfMortal;
			}

			// In beast form the race is swapped wholesale, and both beast races
			// carry a keyword saying so.
			if (a_player->HasKeywordString("ActorTypeAnimal") ||
				a_player->HasKeywordString("WerewolfKeyword")) {
				return Disposition::kSelfWerewolf;
			}

			if (a_player->HasKeywordString("Vampire") ||
				a_player->HasKeywordString("ActorTypeUndead")) {
				return Disposition::kSelfVampire;
			}

			// Out of beast form there is no race to read, so fall back to the
			// werewolf faction the game puts you in when you take the blood.
			if (auto* handler = RE::TESDataHandler::GetSingleton(); handler) {
				if (auto* wolf = handler->LookupForm<RE::TESFaction>(0x0C6CE0, "Skyrim.esm"); wolf) {
					// Read the list rather than asking the engine, for the same reason
					// the title and marker rules do.
					bool beast = false;
					a_player->VisitFactions([&](RE::TESFaction* a_it, std::int8_t a_rank) {
						if (a_it == wolf) {
							beast = a_rank >= 0;
							return true;  // stop
						}
						return false;
					});
					if (beast) {
						return Disposition::kSelfWerewolf;
					}
				}
			}

			return Disposition::kSelfMortal;
		}

		// Raise a relationship once, and only once per session per person.
		//
		// The tally that triggers this is recomputed on every sweep, so without
		// the set below a full heart would try to write the same rank on every
		// pulse for the rest of the game. It also keeps the log honest: one line
		// per person, at the moment it actually happened.
		std::unordered_set<RE::FormID> g_promoted;

		void Promote(RE::Actor* a_actor, const Settings& a_settings)
		{
			if (!a_settings.ostimPromote || !a_actor) {
				return;
			}

			if (!g_promoted.insert(a_actor->GetFormID()).second) {
				return;
			}

			Relations::Raise(a_actor, a_settings.ostimPromoteRank);
			if (a_settings.ostimPromoteMarriage) {
				Relations::MakeMarriageable(a_actor);
			}
		}

		[[nodiscard]] Reading Judge(RE::TESObjectREFR* a_ref, const Settings& a_settings)
		{
			const auto fallback = a_settings.categories[static_cast<std::size_t>(Category::kActor)].colour;

			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return { Disposition::kNone, fallback };
			}

			switch (actor->AsActorState()->GetLifeState()) {
			case RE::ACTOR_LIFE_STATE::kDying:
			case RE::ACTOR_LIFE_STATE::kDead:
				return { Disposition::kDead, a_settings.deadColour };
			default:
				break;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && actor->IsHostileToActor(player)) {
				return { Disposition::kHostile, a_settings.hostileColour };
			}

			if (actor->IsPlayerTeammate()) {
				return { Disposition::kAlly, a_settings.allyColour };
			}

			// Not fighting you is not the same as liking you. The relationship
			// record is what the game itself uses for "who is this to me", so a
			// rival reads differently from a stranger even when nobody has drawn
			// a weapon.
			if (a_settings.actorByRelationship && player) {
				auto* them = actor->GetActorBase();
				auto* you = player->GetActorBase();
				if (them && you) {
					if (const auto* bond = RE::BGSRelationship::GetRelationship(them, you); bond) {
						// The game's own ladder, kept as its own ladder. Folding
						// lover and friend into one "likes you" throws away the
						// distinction the record exists to make.
						using Level = RE::BGSRelationship::RELATIONSHIP_LEVEL;
						switch (bond->level.get()) {
						case Level::kLover:
							return { Disposition::kLover, a_settings.loverColour };
						case Level::kAlly:
							return { Disposition::kAlly, a_settings.allyColour };
						case Level::kConfidant:
							return { Disposition::kConfidant, a_settings.friendlyColour };
						case Level::kFriend:
							return { Disposition::kFriend, a_settings.friendlyColour };
						case Level::kRival:
						case Level::kFoe:
						case Level::kEnemy:
						case Level::kArchnemesis:
							return { Disposition::kRival, a_settings.rivalColour };
						default:
							break;
						}
					}
				}
			}

			return { Disposition::kNeutral, a_settings.neutralColour };
		}

		// Where a name tag should float. GetLookingAtLocation is the engine's own
		// notion of where a reference's "eyes" are, which for a person is the
		// head and for a barrel is the top of the barrel - far better than the
		// origin, which sits on the floor under whatever it belongs to.
		[[nodiscard]] RE::NiPoint3 TagAnchor(RE::TESObjectREFR* a_ref, bool a_isActor)
		{
			const auto base = a_ref->GetPosition();

			if (a_isActor) {
				auto eyes = a_ref->GetLookingAtLocation();
				// Guard against the engine handing back the origin for something
				// with no 3D yet; a tag at the feet is worse than a guess.
				if (eyes.z - base.z > 8.0f) {
					return eyes + RE::NiPoint3{ 0.0f, 0.0f, 26.0f };
				}
				return base + RE::NiPoint3{ 0.0f, 0.0f, 128.0f };
			}

			if (auto* three = a_ref->Get3D(); three) {
				const auto& bound = three->worldBound;
				if (bound.radius > 0.0f && std::isfinite(bound.radius)) {
					return RE::NiPoint3{ base.x, base.y, bound.center.z + bound.radius * 0.6f + 8.0f };
				}
			}

			return base + RE::NiPoint3{ 0.0f, 0.0f, 24.0f };
		}
	}

	Sense* Sense::GetSingleton()
	{
		static Sense singleton;
		return std::addressof(singleton);
	}

	void Sense::OnDataLoaded()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			logger::error("no data handler");
			_status = "no data handler";
			return;
		}

		// Report exactly how the plugin file resolved before touching any forms.
		// When this goes wrong it is almost always the load order, and guessing
		// from "nothing lights up" is miserable.
		if (const auto light = handler->GetLoadedLightModIndex(kPluginFile); light.has_value()) {
			logger::info("{} loaded as light plugin index 0x{:03X} (FE{:03X}xxx)",
				kPluginFile, *light, *light);
		} else if (const auto full = handler->GetModIndex(kPluginFile); full.has_value() && *full != 0xFF) {
			logger::info("{} loaded as full plugin index 0x{:02X}", kPluginFile, *full);
		} else {
			logger::error("{} is not in the load order at all", kPluginFile);
			_status = "ScavengerSense.esp is not in the load order";
			_ready = false;
			return;
		}

		logger::info("first shader resolves to runtime FormID 0x{:08X}",
			handler->LookupFormID(kShaderBase, kPluginFile));

		_pool.clear();
		_poolSet.clear();
		_pool.reserve(kShaderCount);

		for (std::size_t i = 0; i < kShaderCount; ++i) {
			auto* shader = handler->LookupForm<RE::TESEffectShader>(
				static_cast<RE::FormID>(kShaderBase + i), kPluginFile);
			if (shader) {
				_pool.push_back(shader);
				_poolSet.insert(shader);
			}
		}

		_imod = handler->LookupForm<RE::TESImageSpaceModifier>(kImodFormID, kPluginFile);

		// The cold source, looked up once by editor ID so any survival mod can
		// be pointed at through the INI. Globals keep their editor IDs at
		// runtime, which is what makes this workable at all.
		if (!_coldLooked) {
			_coldLooked = true;
			const auto& name = Settings::GetSingleton()->coldGlobal;
			if (!name.empty()) {
				_coldGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>(name);
				logger::info("cold source '{}' -> {}", name, _coldGlobal ? "found" : "not found");
			}
		}

		// TrueHUD, if present. kDataLoaded is comfortably after kPostLoad, so
		// the dll is loaded by now or not installed at all.
		if (!g_trueHud) {
			g_trueHud = static_cast<TRUEHUD_API::IVTrueHUD1*>(
				TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V1));
			logger::info("TrueHUD API: {}", g_trueHud ? "acquired" : "not present");
		}

		// Hits feed the in-combat bars. Registered once even though
		// OnDataLoaded may run again.
		if (!_hitSinkRegistered) {
			if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
				events->AddEventSink<RE::TESHitEvent>(this);
				_hitSinkRegistered = true;
				logger::info("combat bars: hit sink registered");
			} else {
				logger::warn("combat bars: no script event source - hits will not be seen");
			}
		}

		// Absent from esps made before the chime existed; everything else works
		// without it, so a warning is all it rates.
		_sweepSound = handler->LookupForm<RE::BGSSoundDescriptorForm>(kSweepSoundFormID, kPluginFile);
		if (!_sweepSound) {
			// Say which of the two it is. "Not found" covered both a record the
			// engine never loaded and one it loaded as something else, and those
			// want opposite fixes - the old wording blamed a stale esp for both.
			if (auto* raw = handler->LookupForm(kSweepSoundFormID, kPluginFile)) {
				logger::warn(
					"sweep chime {:08X} loaded as form type {} (\"{}\"), not a sound "
					"descriptor - the SNDR record is being rejected, not missing",
					kSweepSoundFormID, static_cast<int>(raw->GetFormType()),
					raw->GetFormEditorID() ? raw->GetFormEditorID() : "");
			} else {
				logger::warn("sweep chime {:08X} is not in {} at all - no form with that id",
					kSweepSoundFormID, kPluginFile);
			}
		}

		if (_pool.empty()) {
			logger::error(
				"{} is in the load order but none of its {} effect shaders resolved - "
				"the plugin file is probably a stale copy",
				kPluginFile, kShaderCount);
			_status = "esp loaded but its effect shaders did not resolve";
			_ready = false;
			return;
		}

		if (!_imod) {
			logger::warn("image space modifier {:08X} not found - screen tint disabled", kImodFormID);
		} else {
			// The tint only does anything if the engine actually built the three
			// cinematic interpolators out of the record. If these report missing,
			// the IMAD is being parsed differently than intended.
			const auto report = [](const char* a_name, RE::NiFloatInterpolator* a_interp) {
				if (!a_interp) {
					logger::warn("  tint channel {}: interpolator MISSING", a_name);
					return;
				}
				auto* data = a_interp->floatData.get();
				if (!data || !data->keys) {
					logger::warn("  tint channel {}: no key data (constant {:.3f})", a_name, a_interp->floatValue);
					return;
				}
				const auto* keys = reinterpret_cast<const float*>(data->keys);
				logger::info("  tint channel {}: {} keys, stride {}, first value {:.3f}, mid value {:.3f}",
					a_name, data->numKeys, data->keySize, keys[1],
					data->numKeys >= 2 && data->keySize == 8 ? keys[3] : -1.0f);
			};

			report("saturation", _imod->cinematic.saturation.mult.get());
			report("brightness", _imod->cinematic.brightness.mult.get());
			report("contrast", _imod->cinematic.contrast.mult.get());
		}

		Labels::GetSingleton()->Install();

		logger::info("resolved {}/{} effect shaders, imod={}", _pool.size(), kShaderCount, _imod ? "yes" : "no");

		// Dump the fields that decide whether the membrane draws at all. Texture
		// counts are integers in the record but floats in CommonLibSSE's struct,
		// so print the raw bits - a zero there means invisible.
		{
			const auto& d = _pool.front()->data;
			logger::info(
				"shader[0] {:08X}: zTest={} srcBlend={} dstBlend={} falloff={:.2f} "
				"texCountU={} texCountV={} colorScale={:.2f} keyScales={:.2f}/{:.2f}/{:.2f}",
				_pool.front()->GetFormID(),
				std::to_underlying(d.membraneShaderZTestFunction),
				std::to_underlying(d.membraneShaderSourceBlendMode),
				std::to_underlying(d.membraneShaderDestBlendMode),
				d.edgeEffectFallOff,
				std::bit_cast<std::uint32_t>(d.textureCountU),
				std::bit_cast<std::uint32_t>(d.textureCountV),
				d.colorScale,
				d.fillTextureEffectColorKeyScaleTimeColorKey1Scale,
				d.fillTextureEffectColorKeyScaleTimeColorKey2Scale,
				d.fillTextureEffectColorKeyScaleTimeColorKey3Scale);
		}
		_status = std::format("{} of {} shaders ready{}", _pool.size(), kShaderCount, _imod ? ", tint ready" : ", no tint");
		_ready = true;

		// One long-lived worker. It only does anything while a wave is in flight,
		// and it never touches game state directly - all work is marshalled onto
		// the main thread through the SKSE task interface.
		bool expected = false;
		if (_threadRunning.compare_exchange_strong(expected, true)) {
			std::thread([this]() {
				for (;;) {
					std::this_thread::sleep_for(std::chrono::milliseconds(16));

					// The corner readout and the hidden interface both have to
					// keep working with no wave in flight, so the tick runs
					// whenever either is asked for. Tick() itself still returns
					// immediately when there is no sweep.
					const auto* settings = Settings::GetSingleton();
					// HasHidden keeps the tick alive for one more pass after the
					// setting is turned off, which is what actually puts the
					// interface back. Without it the loop stops first and the
					// player is left with no HUD.
					const bool  idleWork = settings->selfHudCorner != Corner::kOff ||
					                      settings->hideGameHud ||
					                      settings->combatBars ||
					                      settings->selfBarsOverhead ||
					                      settings->ammoCounter ||
					                      settings->trailsEnabled ||
					                      _enemyHudOwned.load() ||
					                      GameMenus::GetSingleton()->HasHidden();
					if (!_active && !idleWork) {
						continue;
					}
					if (auto* task = SKSE::GetTaskInterface()) {
						task->AddTask([this]() {
							PollSelf();
							PollCombat();
							PollTrails();
							GameMenus::GetSingleton()->ApplyHudVisibility();
							Tick();
						});
					}
				}
			}).detach();
		}
	}

	bool Sense::LabelsAvailable() const
	{
		return Labels::GetSingleton()->Available();
	}

	Category Sense::Categorise(const RE::TESBoundObject* a_base)
	{
		if (!a_base) {
			return Category::kCount;
		}

		switch (a_base->GetFormType()) {
		case RE::FormType::Container:
			return Category::kContainer;
		case RE::FormType::Door:
			return Category::kDoor;
		case RE::FormType::Flora:
		case RE::FormType::Tree:
			return Category::kFlora;
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
			return Category::kGear;
		case RE::FormType::Ingredient:
		case RE::FormType::AlchemyItem:
			return Category::kAlchemy;
		case RE::FormType::Book:
		case RE::FormType::Scroll:
			return Category::kBook;
		case RE::FormType::Misc:
		case RE::FormType::KeyMaster:
		case RE::FormType::SoulGem:
			return Category::kValuable;
		case RE::FormType::Activator:
			return Category::kActivator;
		case RE::FormType::Furniture:
			return Category::kFurniture;
		case RE::FormType::NPC:
			return Category::kActor;
		default:
			return Category::kCount;
		}
	}

	// Whether a reference holds anything a player would actually find. Two
	// regimes, because the inventory API folds the BASE container's entries
	// into every count: a looted chest keeps its base "LItemChest..." leveled
	// entry at +1 forever, which held every cleaned-out container at "full"
	// and the counter at zero. So: one never yet initialised is judged by its
	// base contents, where a leveled list still holding its promise counts as
	// something; one with inventory changes has had its lists rolled into
	// concrete entries, and is judged by what a player would actually find -
	// playable items with a positive count.
	bool Sense::HoldsAnything(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref->GetInventoryChanges(true)) {
			bool found = false;
			if (const auto* container = a_ref->GetContainer()) {
				container->ForEachContainerObject([&](RE::ContainerObject& a_entry) {
					if (a_entry.obj && a_entry.count > 0) {
						found = true;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return found;
		}

		const auto counts = a_ref->GetInventoryCounts(
			[](RE::TESBoundObject& a_obj) {
				return !a_obj.Is(RE::FormType::LeveledItem) && a_obj.GetPlayable();
			},
			true);
		return std::ranges::any_of(
			counts, [](const auto& a_pair) { return a_pair.second > 0; });
	}

	bool Sense::Accept(RE::TESObjectREFR* a_ref, Category a_category, ScanStats& a_stats) const
	{
		const auto* settings = Settings::GetSingleton();

		if (a_category == Category::kCount) {
			++a_stats.wrongType;
			return false;
		}
		if (!settings->categories[static_cast<std::size_t>(a_category)].enabled) {
			++a_stats.categoryOff;
			return false;
		}
		if (a_ref->IsDisabled() || a_ref->IsDeleted() || a_ref->IsMarkedForDeletion()) {
			++a_stats.disabled;
			return false;
		}
		// An empty chest is an answer, not a find.
		if (a_category == Category::kContainer && settings->hideEmptyContainers) {
			if (!HoldsAnything(a_ref)) {
				++a_stats.emptyContainer;
				return false;
			}
		}

		// A plant with nothing to give. TESFlora and TESObjectTREE both carry a
		// produce item, and a null one means no activate prompt and nothing to
		// take - the shrub is set dressing. Judged from the base object, so it
		// costs nothing per reference.
		if (a_category == Category::kFlora && settings->hideBarrenFlora) {
			const auto* base = a_ref->GetBaseObject();
			const RE::TESProduceForm* produce = nullptr;
			if (const auto* flora = base ? base->As<RE::TESFlora>() : nullptr) {
				produce = flora;
			} else if (const auto* tree = base ? base->As<RE::TESObjectTREE>() : nullptr) {
				produce = tree;
			}
			if (!produce || !produce->produceItem) {
				++a_stats.barren;
				return false;
			}
		}

		// A spent resource node wears the same face as a full one, and the two
		// kinds go quiet differently. An ash pile carries its loot as inventory
		// changes, so it is judged like a chest. A vein keeps its count inside a
		// Papyrus script we cannot read from here, but a spent one stops
		// accepting activation. Separate switches: wanting the veins gone is not
		// the same as wanting the ash gone.
		if (a_category == Category::kActivator) {
			// Two reports say these switches do not do what they claim: the vein
			// one hides every vein, mined or not, and the ash one does nothing at
			// all. Rather than guess at a third signal, say what each activator
			// actually looks like and let one mined vein settle it.
			if (settings->debug) {
				const auto* base = a_ref->GetBaseObject();
				logger::info("ORE_ASH: {:08X} edid='{}' base={} blocked={} inv={} flags={:08X}",
					a_ref->GetFormID(),
					base && base->GetFormEditorID() ? base->GetFormEditorID() : "",
					base ? static_cast<int>(base->GetFormType()) : -1,
					a_ref->IsActivationBlocked(),
					a_ref->GetInventoryChanges(true) != nullptr,
					a_ref->GetFormFlags());
			}
			if (a_ref->GetInventoryChanges(true)) {
				if (settings->hideEmptyAsh && !HoldsAnything(a_ref)) {
					++a_stats.emptyAsh;
					return false;
				}
			} else if (settings->hideDepletedOre && a_ref->IsActivationBlocked()) {
				++a_stats.depletedOre;
				return false;
			}
		}

		// Taking it would be theft: the red hand. For players who only want
		// what is honestly theirs to find.
		if (settings->hideStealing && a_category != Category::kActor &&
			a_ref->IsCrimeToActivate()) {
			++a_stats.owned;
			return false;
		}
		if (!a_ref->Is3DLoaded()) {
			++a_stats.noThreeD;
			return false;
		}
		if (settings->ignoreHarvested &&
			(a_ref->GetFormFlags() & RE::TESObjectREFR::RecordFlags::kHarvested) != 0) {
			++a_stats.harvested;
			return false;
		}

		if (a_category == Category::kActor) {
			++a_stats.actorsSeen;

			const auto* actor = a_ref->As<RE::Actor>();
			if (!actor) {
				// Worth knowing about separately: a failed cast is a bug on our
				// side, a corpse is just a corpse.
				++a_stats.actorCastFailed;
				return false;
			}
			const bool dead = actor->IsDead();
			if ((settings->actorFilter == ActorFilter::kLivingOnly && dead) ||
				(settings->actorFilter == ActorFilter::kDeadOnly && !dead)) {
				++a_stats.deadActor;
				return false;
			}

			// Only people who mean you harm - all of them, corpses included.
			// Three ways to qualify: the engine's own hostility check (covers
			// hostile-by-design and modded enemies), actively fighting the
			// player right now (hostility can lag a first strike), or having
			// been struck by the player at any point this session - "hostile
			// because of something you did" is not forgotten when they die.
			if (settings->actorEnemiesOnly) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* whom = a_ref->As<RE::Actor>();  // IsHostileToActor is non-const
				bool  enemy = false;
				if (player && whom) {
					enemy = _struckEver.contains(whom->GetFormID()) ||
					        whom->IsHostileToActor(player);
					if (!enemy && !dead) {
						enemy = whom->GetActorRuntimeData().currentCombatTarget ==
						        player->CreateRefHandle();
					}
				}
				if (!enemy) {
					++a_stats.notEnemy;
					return false;
				}
			}
		}

		if (settings->ignoreUnnamed) {
			const auto* name = a_ref->GetDisplayFullName();
			if (!name || name[0] == '\0') {
				++a_stats.unnamed;
				return false;
			}
		}

		// The engine's placeholder names - "This should not be visible." on
		// ore veins and the like. The game says so itself; believe it. Both
		// the reference's display name and the base record's own name are
		// checked - some references dress the one up and not the other.
		if (settings->ignorePlaceholders && !_placeholderPieces.empty()) {
			const auto matchesPlaceholder = [&](const char* a_name) {
				if (!a_name || !a_name[0]) {
					return false;
				}
				std::string low{ a_name };
				for (auto& c : low) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				for (const auto& piece : _placeholderPieces) {
					if (low.find(piece) != std::string::npos) {
						return true;
					}
				}
				return false;
			};
			const auto* base = a_ref->GetBaseObject();
			if (matchesPlaceholder(a_ref->GetDisplayFullName()) ||
				(base && matchesPlaceholder(base->GetName()))) {
				++a_stats.placeholder;
				return false;
			}
		}

		++a_stats.accepted;
		return true;
	}

	void Sense::OnHotkey()
	{
		if (!_ready) {
			// Say something rather than swallowing the press - a silent hotkey is
			// impossible to tell apart from a broken binding.
			logger::warn("hotkey pressed but the plugin is not ready: {}", _status);
			return;
		}

		const auto* settings = Settings::GetSingleton();

		logger::info("sweep requested (active={}, trigger={})", _active.load(), TriggerName(settings->trigger));

		if (_active && settings->toggle) {
			logger::info("a wave was already running - fading it out instead");
			FadeOut();
			return;
		}

		// Real time, deliberately. The sweep clock can be stopped, and a hotkey
		// that measures its own cooldown against a stopped clock locks itself
		// out permanently - which is exactly what happened in 1.13: every press
		// came back "still inside the 0.50s cooldown" forty-six seconds later.
		// Nothing that decides whether the player may act should be timed
		// against a clock the plugin is allowed to stop.
		const auto now = SS::RealNow();
		if (now - _lastFired < settings->cooldown) {
			logger::info("ignored: still inside the {:.2f}s cooldown", settings->cooldown);
			return;
		}
		_lastFired = now;

		if (_active) {
			// Toggle is off, so this press starts a fresh wave over the old
			// one. The old wave's shaders are softened rather than killed -
			// Start() below owns that - so the handoff reads as one motion.
			logger::info("a wave was still running - starting a fresh one over its fade");
		}

		Start();
	}

	void Sense::Start()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) {
			return;
		}

		const auto* settings = Settings::GetSingleton();
		const auto  origin = player->GetPosition();

		struct Hit
		{
			RE::TESObjectREFR* ref;
			float              distance;
			Category           category;
		};

		std::vector<Hit> hits;
		hits.reserve(256);

		// The placeholder list, split and lowercased once per sweep so Accept
		// only ever does substring finds. The engine's own placeholder in
		// every language we know it by is built in and always filtered - a
		// saved INI carries the old user list forward forever, so a new
		// default would never reach anyone who had ever pressed Save.
		_placeholderPieces.clear();
		if (settings->ignorePlaceholders) {
			for (const char* known : { "should not be visible", "此物应不可见" }) {
				_placeholderPieces.emplace_back(known);
			}
			std::string piece;
			for (const char c : settings->placeholderNames + ",") {
				if (c == ',') {
					while (!piece.empty() && piece.front() == ' ') {
						piece.erase(piece.begin());
					}
					while (!piece.empty() && piece.back() == ' ') {
						piece.pop_back();
					}
					if (!piece.empty()) {
						_placeholderPieces.push_back(piece);
					}
					piece.clear();
				} else {
					piece.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
				}
			}
		}

		ScanStats stats{};
		tes->ForEachReferenceInRange(player, settings->radius, [&](RE::TESObjectREFR* a_ref) {
			++stats.visited;
			if (!a_ref || a_ref == player) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const auto category = Categorise(a_ref->GetBaseObject());
			if (!Accept(a_ref, category, stats)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			hits.push_back({ a_ref, origin.GetDistance(a_ref->GetPosition()), category });
			return RE::BSContainer::ForEachResult::kContinue;
		});

		// Actors live in the high process list as well as the cell's reference
		// set, and the cell set is not dependable for them - sweep both and let
		// the formID set drop the duplicates.
		if (settings->categories[static_cast<std::size_t>(Category::kActor)].enabled) {
			if (auto* processLists = RE::ProcessLists::GetSingleton()) {
				std::unordered_set<RE::FormID> already;
				already.reserve(hits.size());
				for (const auto& hit : hits) {
					already.insert(hit.ref->GetFormID());
				}

				processLists->ForEachHighActor([&](RE::Actor* a_actor) {
					if (!a_actor || a_actor == player) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					if (already.contains(a_actor->GetFormID())) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					const auto distance = origin.GetDistance(a_actor->GetPosition());
					if (distance > settings->radius) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					++stats.visited;
					if (Accept(a_actor, Category::kActor, stats)) {
						hits.push_back({ a_actor, distance, Category::kActor });
						++stats.highActors;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		}

		logger::info(
			"scan @ radius {}: visited={} accepted={} highActors={} | rejected: wrongType={} categoryOff={} "
			"disabled={} no3D={} harvested={} unnamed={} placeholder={} emptyChest={} ore={} ash={} barren={} owned={} | actors seen={} castFailed={} dead={} notEnemy={}",
			settings->radius, stats.visited, stats.accepted, stats.highActors, stats.wrongType, stats.categoryOff,
			stats.disabled, stats.noThreeD, stats.harvested, stats.unnamed, stats.placeholder, stats.emptyContainer, stats.depletedOre, stats.emptyAsh, stats.barren,
			stats.owned, stats.actorsSeen, stats.actorCastFailed, stats.deadActor, stats.notEnemy);

		if (hits.empty()) {
			logger::warn("scan found nothing to light");
			return;
		}

		std::sort(hits.begin(), hits.end(), [](const Hit& a_lhs, const Hit& a_rhs) {
			return a_lhs.distance < a_rhs.distance;
		});

		const auto budget = std::min<std::size_t>(
			std::min<std::size_t>(hits.size(), settings->maxObjects), _pool.size());

		// Taking the nearest N packs every highlight into the first few metres,
		// which is why the wave had nothing to travel across. Sampling evenly
		// over the sorted list keeps the far objects in and gives the front a
		// real distance to cover.
		std::vector<Hit> chosen;
		chosen.reserve(budget);
		if (settings->spreadAcrossRadius && hits.size() > budget && budget > 1) {
			// A straight even spread across the sorted list is the obvious thing
			// and it is wrong: it keeps one candidate in five, so the barrel two
			// paces away is dropped exactly as readily as one at the far edge of
			// the radius. Bending the sample position by a power instead bunches
			// the picks at the near end - everything close survives, and the far
			// half gets sampled - while still reaching the furthest object so
			// the wave has something to travel across.
			const auto gamma = static_cast<double>(std::max(settings->nearBias, 1.0f));
			const auto span = static_cast<double>(hits.size() - 1);

			std::size_t previous = 0;
			bool        first = true;
			for (std::size_t i = 0; i < budget; ++i) {
				const auto t = static_cast<double>(i) / static_cast<double>(budget - 1);
				auto index = static_cast<std::size_t>(std::pow(t, gamma) * span + 0.5);

				// Keep it strictly increasing. Near the start the curve is flat
				// enough that several slots round to the same index; stepping
				// forward instead of duplicating is what makes every one of the
				// closest objects survive.
				if (!first && index <= previous) {
					index = previous + 1;
				}
				if (index >= hits.size()) {
					break;
				}

				chosen.push_back(hits[index]);
				previous = index;
				first = false;
			}
		} else {
			chosen.assign(hits.begin(), hits.begin() + budget);
		}

		const auto keepAlso = chosen.size();

		_favourites = settings->favouriteHighlight ? FavouritedBases() : std::unordered_set<RE::FormID>{};
		if (settings->favouriteHighlight && settings->debug) {
			logger::info("{} favourited item types in the player's inventory", _favourites.size());
		}

		Labels::Ring ringShape;

		{
			std::scoped_lock guard{ _lock };

			// A previous wave's glow fades under the new one instead of being
			// killed on the frame the key lands - the re-press reads as one
			// continuous motion. The pool is 128 shaders round-robin, so a
			// fading straggler only ever shares a shader after a full lap.
			SoftenOurEffects(std::max(settings->fadeOut, 0.15f));

			_pending.clear();
			_pending.reserve(keepAlso);
			_next = 0;

			// Spread the wave over the span the chosen objects actually occupy,
			// not over the scan radius. hits is sorted, so the span is simply
			// first to last. A fixed units-per-second speed looks instant
			// whenever the nearest N objects happen to be clustered together.
			const float nearest = chosen.front().distance;
			const float furthest = chosen.back().distance;
			const float span = std::max(furthest - nearest, 1.0f);

			float last = 0.0f;
			for (std::size_t i = 0; i < keepAlso; ++i) {
				const float at = settings->sweepTime > 0.0f
									 ? (chosen[i].distance - nearest) / span * settings->sweepTime
									 : 0.0f;
				last = std::max(last, at);
				_pending.push_back({ chosen[i].ref->CreateRefHandle(), at, chosen[i].category });
			}

			logger::info(
				"wave spans {:.0f} to {:.0f} units ({:.0f} wide), sweeping it over {:.2f}s",
				nearest, furthest, span, last);

			_appliedThisWave = 0;
			_labelBuffer.clear();
			_labelBuffer.reserve(keepAlso);
			_waveStart = SS::Now();
			_waveEnd = _waveStart + last + settings->duration;
			_startedRealAt = SS::RealNow();
			// The stance the sweep opened in decides whether it reads the
			// ground, and holds for the whole wave - kneel, sweep, stand.
			_sweepCrouched = player->IsSneaking();
			_active = true;

			// You, tagged like anybody else. Pushed straight into the buffer
			// rather than routed through the candidate list: the sweep starts
			// where you are standing, so your own tag belongs on its first
			// frame, and a glow around the camera would be both useless and one
			// shader slot poorer.
			if (settings->scanPlayer && settings->labelsEnabled) {
				const auto* name = player->GetDisplayFullName();
				_labelBuffer.push_back(Labels::Entry{
					name && name[0] ? ToUtf8(name) : std::string{ "You" },
					TagAnchor(player, true),
					settings->selfColour,
					_waveStart,
					_waveEnd,
					settings->selfScale * settings->labelActorScale,
					settings->selfIcon ? SelfState(player) : Disposition::kNone,
					1,
					-1,
					0xFFFFFF,
					-1,
					101,
					-1.0f,
					false,
					false,
					-1,
					settings->titlesShow ? ToUtf8(Titles::GetSingleton()->For(player).c_str())
										 : std::string{},
					Titles::GetSingleton()->ColourFor(player),
					player->CreateRefHandle() });

				// Your own vitals, read once as the wave starts rather than
				// polled per frame - a sweep is three seconds and this is a
				// glance, not a combat readout.
				if (settings->selfBars) {
					ReadVitals(player, _labelBuffer.back().vitals, _labelBuffer.back().vitalsCap,
					_labelBuffer.back().vitalsPeak);
					_labelBuffer.back().vitalsSelf = true;
				}
				if (settings->weaponIcons) {
					_labelBuffer.back().weapon = static_cast<std::uint8_t>(ClassifyWeapon(player));
				}
				if (settings->raceIcons) {
					const auto [kind, mod] = ClassifyRace(player);
					_labelBuffer.back().race = static_cast<std::uint8_t>(kind);
					_labelBuffer.back().raceMark = static_cast<std::uint8_t>(mod);
				}

				Labels::GetSingleton()->Replace(_labelBuffer);
			}

			if (settings->ringEnabled) {
				// The ring starts at the player's feet and runs a little past
				// the furthest thing the wave lit, so the pulse reads as
				// carrying on into the dark rather than stopping at the last
				// crate. Its travel time is scaled the same way, so it moves at
				// the wave's speed.
				const auto travel = std::max(last * settings->ringLead, 0.4f);
				ringShape = BuildRing(
					RE::PlayerCharacter::GetSingleton()->GetPosition(),
					furthest * settings->ringLead,
					_waveStart,
					_waveStart + travel);
			}
		}

		if (ringShape.Valid()) {
			Labels::GetSingleton()->SetRing(std::move(ringShape));
		} else {
			Labels::GetSingleton()->StopRing();
		}

		BeginTint(_waveEnd - _waveStart);
		Labels::GetSingleton()->SetWash(_waveStart, _waveEnd);

		if (settings->soundEnabled) {
			auto* audio = RE::BSAudioManager::GetSingleton();
			if (!audio) {
				logger::warn("chime: no audio manager");
			} else if (settings->soundFormID != 0) {
				// A user override is somebody else's descriptor: it brings
				// its own volume, we just fire it.
				const bool played = audio->Play(settings->soundFormID);
				if (!played || settings->debug) {
					logger::info("chime: override {:08X} -> {}", settings->soundFormID,
						played ? "played" : "DID NOT PLAY");
				}
			} else if (!_sweepSound) {
				logger::warn("chime: no descriptor - the esp in the load order predates it");
			} else {
				// Our own chime goes through a handle so the volume slider
				// works; the descriptor already routes it through the UI
				// audio category, so the game's sliders stack on top.
				RE::BSSoundHandle handle;
				bool              played = false;
				if (audio->GetSoundHandle(handle, _sweepSound) && handle.IsValid()) {
					handle.SetVolume(std::clamp(settings->soundVolume, 0.0f, 1.0f));
					played = handle.Play();
				}
				if (!played) {
					// The handle route can be starved by audio overhauls; the
					// manager's own play path is the second try, at the cost of
					// the volume slider.
					played = audio->Play(static_cast<RE::BSISoundDescriptor*>(_sweepSound));
					logger::info("chime: handle route failed, direct play -> {}",
						played ? "ok" : "ALSO FAILED");
				} else if (settings->debug) {
					logger::info("chime: played at volume {:.2f}", settings->soundVolume);
				}
			}
		}

		logger::info("wave started: lighting {} of {} hits, ends at t+{:.2f}s | menus open: {}",
			keepAlso, hits.size(), _waveEnd - _waveStart, GameMenus::GetSingleton()->Describe());
	}
	// Turns the chosen anchor into a world point.
	//
	// Deliberately the third-person skeleton even in first person: the body is
	// still posed there whether or not it is drawn, where the first-person one
	// sits inside the camera and projects to nonsense. A missing node falls
	// back to the reference's own position, which is at the feet, lifted to
	// something like chest height.
	bool Sense::AmmoAnchorPoint(RE::Actor* a_actor, AmmoAnchor a_anchor, RE::NiPoint3& a_out)
	{
		auto* root = a_actor->Get3D(false);
		if (!root) {
			root = a_actor->Get3D();
		}

		if (root) {
			const auto node = [root](const char* a_name) -> RE::NiAVObject* {
				return root->GetObjectByName(RE::BSFixedString{ a_name });
			};

			RE::NiAVObject* found = nullptr;
			switch (a_anchor) {
			case AmmoAnchor::kHead:
				found = node("NPC Head [Head]");
				break;
			case AmmoAnchor::kBow:
				// Whichever the skeleton actually carries. A bow in the hands rides
				// the weapon node; slung, it sits on the back. The hand is the last
				// resort so the setting still means something either way.
				for (const char* name : { "WEAPON", "WeaponBow", "NPC L Hand [LHnd]" }) {
					found = node(name);
					if (found) {
						break;
					}
				}
				break;
			case AmmoAnchor::kBody:
			default:
				found = node("NPC Spine2 [Spn2]");
				break;
			}

			if (found) {
				a_out = found->world.translate;
				return true;
			}
		}

		a_out = a_actor->GetPosition();
		a_out.z += 90.0f;
		return true;
	}


	// The corner readout, refreshed whether or not a sweep is running.
	//
	// Deliberately a poll rather than a hook on damage: the interesting cases
	// are regeneration, a potion, a spell cost and a fortify effect wearing
	// off, and no single event covers all of them. Three actor value reads at
	// 60 Hz is nothing.
	void Sense::PollSelf()
	{
		const auto* settings = Settings::GetSingleton();
		// The over-head stack leans on the change tracking done here, so it
		// keeps this alive even with the corner readout off.
		if (settings->selfHudCorner == Corner::kOff && !settings->selfBarsOverhead &&
			!settings->ammoCounter) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// The ammo readout. Only with a bow or crossbow actually out: a count of
		// arrows means nothing while you are holding a sword.
		if (settings->ammoCounter) {
			Labels::Ammo ammo;
			const auto   kind = ClassifyWeapon(player);
			const bool   ranged = kind == WeaponKind::kBow || kind == WeaponKind::kCrossbow;

			if (ranged) {
				// One throttled inventory walk gives the worn ammunition, its count
				// and its name together. GetCurrentAmmo is only a fallback: it
				// reports what is nocked, which is nothing for most of the time a
				// bow is merely in hand.
				const auto real = SS::RealNow();
				if (real - _ammoAt > 0.2f) {
					_ammoAt = real;
					_ammoCount = 0;
					_ammoName.clear();
					for (const auto& [object, entry] : player->GetInventory()) {
						if (object && object->Is(RE::FormType::Ammo) && entry.second &&
							entry.second->IsWorn()) {
							_ammoCount = entry.first;
							if (const char* full = object->GetName(); full && *full) {
								_ammoName = full;
							}
							break;
						}
					}
					if (_ammoCount == 0 && _ammoName.empty()) {
						if (auto* nocked = player->GetCurrentAmmo()) {
							if (const char* full = nocked->GetName(); full && *full) {
								_ammoName = full;
							}
						}
					}
				}
			}

			// Whether it wants to be on screen at all, before the fade has its say.
			bool wanted = ranged && (_ammoCount > 0 || !_ammoName.empty());
			if (wanted) {
				switch (settings->ammoWhen) {
				case AmmoWhen::kDrawn:
					{
						// Every stage of a shot, from starting the pull to the end of
						// the follow-through, so it does not blink out mid-loose.
						const auto state = player->AsActorState()->GetAttackState();
						wanted = state >= RE::ATTACK_STATE_ENUM::kBowDraw &&
							state <= RE::ATTACK_STATE_ENUM::kBowFollowThrough;
					}
					break;
				case AmmoWhen::kSensing:
					wanted = _active.load();
					break;
				case AmmoWhen::kAlways:
				default:
					break;
				}
			}

			// Its own fade, eased here rather than on the render thread, which has
			// no business knowing what time it is. A long frame is capped so a
			// stutter or a loading screen cannot jump the whole way at once.
			const auto real = SS::RealNow();
			const float dt = _ammoTickAt < 0.0f ? 0.0f : std::clamp(real - _ammoTickAt, 0.0f, 0.25f);
			_ammoTickAt = real;
			const float span = wanted ? settings->ammoFadeIn : settings->ammoFadeOut;
			if (span <= 0.001f) {
				_ammoAlpha = wanted ? 1.0f : 0.0f;
			} else {
				_ammoAlpha = std::clamp(_ammoAlpha + (wanted ? dt : -dt) / span, 0.0f, 1.0f);
			}

			// Still placed while fading out, so it dies where it stood rather than
			// snapping to the last anchor it happened to be given.
			if (_ammoAlpha > 0.004f) {
				ammo.count = _ammoCount;
				ammo.name = _ammoName;
				ammo.alpha = _ammoAlpha;
				// The screen anchor has nowhere in the world to be, so it skips the
				// skeleton walk entirely and is simply on.
				ammo.show = settings->ammoAnchor == AmmoAnchor::kScreen ||
					AmmoAnchorPoint(player, settings->ammoAnchor, ammo.world);
			}

			Labels::GetSingleton()->SetAmmo(ammo);
		}

		float now[3]{ -1.0f, -1.0f, -1.0f };
		float caps[3]{ 1.0f, 1.0f, 1.0f };
		ReadVitals(player, now, caps);

		// A hair of slack, so floating point noise in regeneration does not
		// count as a change and hold the readout up forever.
		constexpr float kEpsilon = 0.0005f;
		bool            moved = false;
		for (int i = 0; i < 3; ++i) {
			if (now[i] >= 0.0f && std::abs(now[i] - _selfLast[i]) > kEpsilon) {
				moved = true;
			}
			_selfLast[i] = now[i];
		}

		if (moved) {
			_selfChangedAt = SS::RealNow();
		}

		Labels::GetSingleton()->SetSelfHud(now, caps, _selfChangedAt);

		// The stats row: level, septims, weight, cold, and what is in hand.
		// All cheap main-thread reads except gold, which walks the inventory
		// and is asked once a second.
		Labels::SelfStats stats;
		if (settings->senseLevel) {
			stats.level = static_cast<std::int16_t>(player->GetLevel());
		}
		if (settings->weaponIcons) {
			stats.weapon = static_cast<std::uint8_t>(ClassifyWeapon(player));
		}
		if (settings->senseWeight) {
			if (auto* values = player->AsActorValueOwner()) {
				stats.weight = values->GetActorValue(RE::ActorValue::kInventoryWeight);
				stats.weightMax = values->GetActorValue(RE::ActorValue::kCarryWeight);
			}
		}
		if (settings->senseGold) {
			const auto real = SS::RealNow();
			if (real - _goldAt > 1.0f) {
				_goldAt = real;
				_gold = player->GetGoldAmount();
			}
			stats.gold = _gold;
		}
		if (settings->senseCold && _coldGlobal) {
			stats.cold = _coldGlobal->value;
			stats.coldMax = settings->coldMax;
		}
		if (settings->raceIcons) {
			const auto [kind, mod] = ClassifyRace(player);
			stats.race = static_cast<std::uint8_t>(kind);
			stats.raceMark = static_cast<std::uint8_t>(mod);
		}
		Labels::GetSingleton()->SetSelfStats(stats);
	}

	RE::BSEventNotifyControl Sense::ProcessEvent(
		const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*)
	{
		// Cheap gates first: this fires for every hit anybody lands anywhere.
		if (!a_event || !Settings::GetSingleton()->combatBars) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (!a_event->cause || !a_event->cause->IsPlayerRef()) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* target = a_event->target ? a_event->target->As<RE::Actor>() : nullptr;
		if (!target || target->IsPlayerRef()) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// Whacking a corpse fires this event too. A killing blow still tracks:
		// it lands while the target is alive, the death comes after.
		if (const auto life = target->AsActorState()->GetLifeState();
			(life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead) &&
			!_combatHits.contains(target->GetFormID())) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// Newest hits win the space. 24 bars is already an unreadable fight.
		constexpr std::size_t kMaxTracked = 24;
		if (_combatHits.size() >= kMaxTracked && !_combatHits.contains(target->GetFormID())) {
			const auto oldest = std::min_element(_combatHits.begin(), _combatHits.end(),
				[](const auto& a_lhs, const auto& a_rhs) {
					return a_lhs.second.lastHitAt < a_rhs.second.lastHitAt;
				});
			_combatHits.erase(oldest);
		}

		const float real = SS::RealNow();
		_combatHits[target->GetFormID()] = { target->CreateRefHandle(), real, real };
		_struckEver.insert(target->GetFormID());
		if (Settings::GetSingleton()->debug) {
			const auto* name = target->GetDisplayFullName();
			logger::info("combat bars: hit {} ({:08X}), {} tracked, player inCombat={}",
				name && name[0] ? name : "?", target->GetFormID(), _combatHits.size(),
				RE::PlayerCharacter::GetSingleton()->IsInCombat());
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void Sense::PollCombat()
	{
		const auto* settings = Settings::GetSingleton();
		auto*       player = RE::PlayerCharacter::GetSingleton();

		const float real = SS::RealNow();
		const bool  wantSelf = settings->selfBarsOverhead && player;
		const bool  wantCombat = settings->combatBars && player;

		// Owning the enemy bars outright: TrueHUD's target widget is claimed
		// through its API so it never appears, its per-actor widgets are
		// dismissed on a half-second pulse, and the vanilla enemy health
		// element is parked off screen - by POSITION, not visibility, because
		// the HUD's own ActionScript re-drives _visible on every target update
		// and wins that fight. Before the early returns below, so everything
		// is handed back the moment the feature turns off.
		constexpr auto  kEnemyHealth = "_root.HUDMovieBaseInstance.EnemyHealth_mc";
		constexpr float kUnset = -100000.0f;
		constexpr float kParkedY = -4000.0f;
		const bool      wantOwn = wantCombat && settings->pushTrueHUDAside;

		const auto hudMovie = [&]() -> RE::GFxMovie* {
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			return menu && menu->uiMovie ? menu->uiMovie.get() : nullptr;
		};

		if (!wantOwn && _enemyHudOwned.load()) {
			if (auto* movie = hudMovie(); movie && _enemyHealthHomeY > kUnset + 1.0f) {
				movie->SetVariable((std::string{ kEnemyHealth } + "._y").c_str(),
					RE::GFxValue{ static_cast<double>(_enemyHealthHomeY) });
			}
			_enemyHealthHomeY = kUnset;
			if (auto* ui = RE::UI::GetSingleton()) {
				if (auto menu = ui->GetMenu("TrueHUD"); menu && menu->uiMovie) {
					menu->uiMovie->SetVisible(true);
				}
			}
			if (g_trueHud && _targetControlHeld) {
				g_trueHud->ReleaseTargetControl(SKSE::GetPluginHandle());
				_targetControlHeld = false;
			}
			_enemyHudOwned.store(false);
			logger::info("enemy bars: handed back to the game and TrueHUD");
		}
		if (wantOwn && real - _lastHudPush > 0.5f) {
			_lastHudPush = real;
			_enemyHudOwned.store(true);

			if (g_trueHud && !_targetControlHeld) {
				const auto result = g_trueHud->RequestTargetControl(SKSE::GetPluginHandle());
				_targetControlHeld = result == TRUEHUD_API::APIResult::OK ||
				                     result == TRUEHUD_API::APIResult::AlreadyGiven;
				if (_targetControlHeld) {
					logger::info("TrueHUD target control -> held");
				} else if (!_saidControlRefused) {
					_saidControlRefused = true;
					logger::info(
						"TrueHUD target control refused - another mod owns it (TDM's "
						"target lock does this); hiding TrueHUD's overlay instead");
				}
			}

			// The decisive stroke: TrueHUD draws everything in its own menu
			// movie, and nothing re-drives a movie's visibility. Hiding it
			// silences the target widget even when another mod - TDM's target
			// lock, typically - owns target control and keeps feeding it.
			if (auto* ui = RE::UI::GetSingleton()) {
				if (auto menu = ui->GetMenu("TrueHUD"); menu && menu->uiMovie) {
					menu->uiMovie->SetVisible(false);
				}
			}

			// Re-applied on a pulse because a HUD reload rebuilds the element
			// at its home position. The home Y is captured once, before the
			// first parking, and only from a sane-looking value so a pulse can
			// never capture our own -4000.
			if (auto* movie = hudMovie()) {
				RE::GFxValue y;
				const auto   yPath = std::string{ kEnemyHealth } + "._y";
				if (!movie->GetVariable(&y, yPath.c_str()) || !y.IsNumber()) {
					static bool saidMissing = false;
					if (!saidMissing) {
						saidMissing = true;
						logger::warn("vanilla enemy health element not found at {} - "
									 "cannot hide it", kEnemyHealth);
					}
				} else {
					const auto current = static_cast<float>(y.GetNumber());
					if (current > kParkedY + 1.0f && _enemyHealthHomeY < kUnset + 1.0f) {
						_enemyHealthHomeY = current;
						logger::info("vanilla enemy health parked (home y={:.0f})", current);
					}
					movie->SetVariable(yPath.c_str(), RE::GFxValue{ static_cast<double>(kParkedY) });
				}
			}

			// Every combatant, not just the ones fighting the player: a bar
			// over somebody attacking your follower is still a bar we own.
			if (g_trueHud) {
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					lists->ForEachHighActor([&](RE::Actor* a_actor) {
						if (a_actor && !a_actor->IsPlayerRef() && a_actor->IsInCombat()) {
							g_trueHud->RemoveActorInfoBar(a_actor->GetHandle(),
								TRUEHUD_API::WidgetRemovalMode::Immediate);
							g_trueHud->RemoveBoss(a_actor->GetHandle(),
								TRUEHUD_API::WidgetRemovalMode::Immediate);
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
				}
			}
		}

		if (!wantCombat && !_combatHits.empty()) {
			_combatHits.clear();
		}
		if (!wantCombat && !wantSelf) {
			if (_combatShown) {
				_combatBuffer.clear();
				Labels::GetSingleton()->SetCombatBars({});
				_combatShown = false;
			}
			return;
		}

		// Step aside for menus exactly the way the sweep does, but keep the
		// tracked set - the fight is still there behind the inventory screen.
		auto*      ui = RE::UI::GetSingleton();
		const bool suspended = (settings->menuAware && GameMenus::GetSingleton()->Blocking()) ||
		                       (ui && ui->GameIsPaused());
		if (suspended) {
			if (_combatShown) {
				Labels::GetSingleton()->SetCombatBars({});
				_combatShown = false;
			}
			return;
		}

		const auto now = SS::Now();
		_combatBuffer.clear();

		// Your own head, first. Third person only - in first person there is
		// no head on screen, and the corner readout owns that case. When a
		// sweep has already tagged you with bars, the render pass skips this
		// one rather than stacking two.
		if (wantSelf) {
			auto* camera = RE::PlayerCamera::GetSingleton();
			if ((!camera || !camera->IsInFirstPerson()) && player->Is3DLoaded()) {
				Labels::Entry entry{};
				entry.world = TagAnchor(player, true);
				entry.bornAt = now - 1.0f;
				entry.diesAt = now + 3600.0f;
				entry.scale = settings->selfScale * settings->labelActorScale;
				entry.owner = player->CreateRefHandle();
				entry.vitalsSelf = true;
				ReadVitals(player, entry.vitals, entry.vitalsCap, entry.vitalsPeak);
				if (settings->senseLevel) {
					entry.level = static_cast<std::int16_t>(player->GetLevel());
				}
				if (settings->weaponIcons) {
					entry.weapon = static_cast<std::uint8_t>(ClassifyWeapon(player));
				}
				if (settings->raceIcons) {
					const auto [kind, mod] = ClassifyRace(player);
					entry.race = static_cast<std::uint8_t>(kind);
					entry.raceMark = static_cast<std::uint8_t>(mod);
				}

				// The rules are applied here, per bar, so the render side stays
				// a dumb draw loop. Change tracking is PollSelf's, which runs
				// just before this whenever the overhead stack is on. On-change
				// bars get a real deadline so they fade out instead of popping.
				const float selfLeft =
					std::max(0.0f, _selfChangedAt + settings->selfHudLinger - real);
				bool any = false;
				for (int i = 0; i < 3; ++i) {
					if (settings->selfBarsOverheadWhen == ShowWhen::kNotFull &&
						entry.vitals[i] >= 0.999f) {
						entry.vitals[i] = -1.0f;
					} else if (settings->selfBarsOverheadWhen == ShowWhen::kOnChange &&
							   selfLeft <= 0.0f) {
						entry.vitals[i] = -1.0f;
					}
					any = any || entry.vitals[i] >= 0.0f;
				}
				if (settings->selfBarsOverheadWhen == ShowWhen::kOnChange) {
					entry.diesAt = now + selfLeft;
				}
				if (any) {
					_combatBuffer.push_back(std::move(entry));
				}
			}
		}

		// Each tracked person carries their own clock: alive and fighting YOU
		// refreshes it, anything else - death, calming down, turning on
		// somebody else - freezes it, and the entry fades linger seconds
		// later. Deliberately no global "is the fight on": every global gate
		// tried so far had a corner that either pinned bars up forever or
		// cleared them mid-fight.
		const auto playerHandle = player->CreateRefHandle();

		// Seed the tracked set from things other than a hit. Entries land in the
		// same map the hit event writes to, so the lifetime, the linger and the
		// cap below all apply unchanged - the only difference is how somebody
		// got in. lastHitAt is stamped because the engagement test below treats
		// it as a staleness cap, and these arrived without ever being hit.
		if (settings->combatBarsWhen != CombatBarsWhen::kStruck) {
			constexpr std::size_t kMaxTracked = 24;
			const auto note = [&](RE::Actor* a_actor) {
				if (!a_actor || a_actor->IsPlayerRef() || !a_actor->Is3DLoaded()) {
					return;
				}
				const auto life = a_actor->AsActorState()->GetLifeState();
				if (life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead) {
					return;
				}
				const auto id = a_actor->GetFormID();
				auto       it = _combatHits.find(id);
				if (it == _combatHits.end()) {
					if (_combatHits.size() >= kMaxTracked) {
						return;
					}
					_combatHits[id] = { a_actor->CreateRefHandle(), real, real };
				} else {
					// Hold it up for as long as the reason to show it lasts.
					it->second.lastEngagedAt = real;
					it->second.lastHitAt = real;
				}
			};

			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				lists->ForEachHighActor([&](RE::Actor* a_actor) {
					if (a_actor && a_actor->IsInCombat() &&
						a_actor->GetActorRuntimeData().currentCombatTarget == playerHandle) {
						note(a_actor);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}

			// Whoever the crosshair rests on, fight or no fight. This one is not
			// held by the engagement test below - nothing about looking at
			// somebody makes them a combatant - so it leans on the linger to
			// fade out once you look away.
			if (settings->combatBarsWhen == CombatBarsWhen::kAimed) {
				if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
					if (auto ref = pick->target.get(); ref) {
						note(ref->As<RE::Actor>());
					}
				}
			}
		}
		for (auto it = _combatHits.begin(); it != _combatHits.end();) {
			auto  ref = it->second.handle.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor || !actor->Is3DLoaded()) {
				it = _combatHits.erase(it);
				continue;
			}

			const auto life = actor->AsActorState()->GetLifeState();
			const bool dead =
				life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead;

			// Fighting the player, specifically: a bandit who has turned on
			// somebody else does not hold our bars up. The hard cap keeps a
			// stuck combat flag from pinning an entry forever.
			const bool engaged = !dead && actor->IsInCombat() &&
			                     actor->GetActorRuntimeData().currentCombatTarget == playerHandle &&
			                     real - it->second.lastHitAt < 120.0f;
			if (engaged) {
				it->second.lastEngagedAt = real;
			}

			if (!engaged && real - it->second.lastEngagedAt > settings->combatLinger) {
				it = _combatHits.erase(it);
				continue;
			}

			// A real deadline rather than "far future": the render side fades
			// the last stretch of the linger (the shared Fading over time), so
			// the bar leaves the way it arrived instead of popping off.
			const float left =
				std::max(0.0f, it->second.lastEngagedAt + settings->combatLinger - real);

			Labels::Entry entry{};
			entry.world = TagAnchor(actor, true);
			entry.bornAt = now - 1.0f;
			entry.diesAt = engaged ? now + 3600.0f : now + left;
			entry.scale = settings->labelActorScale;
			entry.owner = actor->CreateRefHandle();
			ReadVitals(actor, entry.vitals, entry.vitalsCap, entry.vitalsPeak);
			if (!settings->combatBarsAll) {
				entry.vitals[1] = -1.0f;
				entry.vitals[2] = -1.0f;
			}
			if (settings->levelOthers) {
				entry.level = static_cast<std::int16_t>(actor->GetLevel());
			}
			if (settings->weaponIcons) {
				entry.weapon = static_cast<std::uint8_t>(ClassifyWeapon(actor));
			}
			if (settings->raceIcons) {
				const auto [kind, mod] = ClassifyRace(actor);
				entry.race = static_cast<std::uint8_t>(kind);
				entry.raceMark = static_cast<std::uint8_t>(mod);
			}
			entry.vitalsAt = now;
			_combatBuffer.push_back(std::move(entry));
			++it;
		}

		Labels::GetSingleton()->SetCombatBars(_combatBuffer);
		// Transitions only - this runs sixty times a second.
		if (!_combatShown && !_combatBuffer.empty()) {
			logger::info("overhead bars: up over {} ({} from combat)",
				_combatBuffer.size(), _combatHits.size());
		} else if (_combatShown && _combatBuffer.empty()) {
			logger::info("overhead bars: down, nothing left to show");
		}
		_combatShown = !_combatBuffer.empty();
	}

	namespace
	{
		// The actor nearest the centre of the screen within range: hunting
		// picks a deer across a valley, far beyond the crosshair's activate
		// reach, so the mark aims by view rather than by touch.
		[[nodiscard]] RE::Actor* PickByView(float a_maxRange)
		{
			auto* camera = RE::Main::WorldRootCamera();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!camera || !player || !lists) {
				return nullptr;
			}

			const auto origin = player->GetPosition();
			RE::Actor* best = nullptr;
			float      bestOff = 0.05f;  // normalised screen distance from centre

			lists->ForEachHighActor([&](RE::Actor* a_actor) {
				if (!a_actor || a_actor->IsPlayerRef() || !a_actor->Is3DLoaded()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto life = a_actor->AsActorState()->GetLifeState();
				if (life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto at = a_actor->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 64.0f };
				if (origin.GetDistance(at) > a_maxRange) {
					return RE::BSContainer::ForEachResult::kContinue;
				}

				float x{}, y{}, z{};
				RE::NiCamera::WorldPtToScreenPt3(camera->GetRuntimeData().worldToCam,
					camera->GetRuntimeData2().port, at, x, y, z, 1e-5f);
				if (z <= 0.0f) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const float dx = x - 0.5f;
				const float dy = y - 0.5f;
				const float off = std::sqrt(dx * dx + dy * dy);
				if (off < bestOff) {
					bestOff = off;
					best = a_actor;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});

			return best;
		}
	}

	// The mark palette: gold first - the classic quarry - then colours far
	// enough apart to tell at a glance. Trail, label and sweep glow all agree.
	static constexpr std::uint32_t kMarkPalette[]{
		0xFFA600,  // gold
		0xFF7A30,  // ember
		0x53C9C4,  // teal
		0xB07AFF,  // violet
		0x7ED957,  // spring
		0xFF6F91,  // rose
	};

	std::uint32_t Sense::MarkColour(RE::FormID a_id) const
	{
		const auto it = _marked.find(a_id);
		return it == _marked.end()
		           ? 0
		           : kMarkPalette[it->second.slot % std::size(kMarkPalette)];
	}

	// The trail whose point sits nearest the screen centre within reach: the
	// footprint half of the trail key's aim. Only points laid in this place
	// count - the rest are not on screen to be aimed at.
	RE::FormID Sense::PickTrailByView(float a_maxRange) const
	{
		auto* camera = RE::Main::WorldRootCamera();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!camera || !player || _trails.empty()) {
			return 0;
		}

		const auto origin = player->GetPosition();
		RE::FormID bestId = 0;
		float      bestOff = 0.05f;  // same view window the actor pick uses
		for (const auto& [id, trail] : _trails) {
			for (const auto& point : trail.points) {
				if (point.place != _placeId || origin.GetDistance(point.pos) > a_maxRange) {
					continue;
				}
				float x{}, y{}, z{};
				RE::NiCamera::WorldPtToScreenPt3(camera->GetRuntimeData().worldToCam,
					camera->GetRuntimeData2().port, point.pos, x, y, z, 1e-5f);
				if (z <= 0.0f) {
					continue;
				}
				const float dx = x - 0.5f;
				const float dy = y - 0.5f;
				const float off = std::sqrt(dx * dx + dy * dy);
				if (off < bestOff) {
					bestOff = off;
					bestId = id;
				}
			}
		}
		return bestId;
	}

	// Keeps the marking sign pointed at whatever the trail key would take:
	// brackets (or ring, or chevron) on a person, running dots on a trail.
	void Sense::UpdateAimPreview()
	{
		const auto* settings = Settings::GetSingleton();
		auto*       labels = Labels::GetSingleton();

		// The sign only shows when a press would actually take: same rule as
		// the key itself, so a standing-opened sweep offers nothing.
		if (!TrailsRevealed()) {
			ClearAimPreview();
			return;
		}

		RE::Actor* aim = nullptr;
		if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
			if (auto ref = pick->target.get(); ref) {
				aim = ref->As<RE::Actor>();
			}
		}
		if (!aim) {
			aim = PickByView(settings->trackRange);
		}
		if (aim && !aim->IsPlayerRef()) {
			const auto tint = MarkColour(aim->GetFormID());
			labels->SetAim(true, aim->GetPosition(), TagAnchor(aim, true),
				tint ? tint : settings->aimColour);
			labels->SetAimTrail(0, settings->aimColour);
			_aimTrailId = 0;
			return;
		}

		labels->SetAim(false, {}, {}, settings->aimColour);
		const bool trailsHidden =
			_trailsHidden.load() && settings->trailMode == TrailKeyMode::kMulti;
		const auto trailId = trailsHidden ? RE::FormID{ 0 }
		                                  : PickTrailByView(settings->trackRange);
		if (trailId != 0) {
			const auto tint = MarkColour(trailId);
			labels->SetAimTrail(trailId, tint ? tint : settings->aimColour);
		} else {
			labels->SetAimTrail(0, settings->aimColour);
		}
		_aimTrailId = trailId;
	}

	void Sense::ClearAimPreview()
	{
		auto* labels = Labels::GetSingleton();
		labels->SetAim(false, {}, {}, Settings::GetSingleton()->aimColour);
		labels->SetAimTrail(0, 0);
		_aimTrailId = 0;
	}

	// One rule for when the ground is readable - which is also when the
	// trail key is willing to work.
	bool Sense::TrailsRevealed() const
	{
		const auto* settings = Settings::GetSingleton();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		switch (settings->trailReveal) {
		case TrailReveal::kAlways:
			return true;
		case TrailReveal::kSense:
			return _active.load();
		case TrailReveal::kCrouch:
			return player && player->IsSneaking();
		case TrailReveal::kCrouchSense:
		default:
			// The stance at the sweep's opening holds for its whole life:
			// kneel to read the ground, then stand and mark freely until
			// the wave ends.
			return _active.load() && _sweepCrouched;
		}
	}

	// The gate every tracking action passes; explains itself when it says no.
	bool Sense::TrailGateOpen()
	{
		if (TrailsRevealed()) {
			return true;
		}
		const auto* settings = Settings::GetSingleton();
		if (settings->trailToasts) {
			const bool wantsCrouch = settings->trailReveal == TrailReveal::kCrouch ||
			                         (settings->trailReveal == TrailReveal::kCrouchSense && _active);
			RE::SendHUDMessage::ShowHUDMessage(Locale::T(
				wantsCrouch ? "Crouch to read the ground"
							: "The sense is closed - sweep first to mark"));
		}
		logger::info("trails: key refused, ground not readable");
		return false;
	}

	// The clean slate: every mark, trail and open recording window at once.
	void Sense::OnTrailLongPress()
	{
		if (!TrailGateOpen()) {
			return;
		}
		_marked.clear();
		_trails.clear();
		_quarry.clear();
		Labels::GetSingleton()->SetTrails({}, false);
		if (Settings::GetSingleton()->trailToasts) {
			RE::SendHUDMessage::ShowHUDMessage(Locale::T("All trails wiped"));
		}
		logger::info("trails: wipe, everything gone");
	}

	void Sense::ToggleTrailsShown()
	{
		const bool hidden = !_trailsHidden.load();
		_trailsHidden.store(hidden);
		if (Settings::GetSingleton()->trailToasts) {
			RE::SendHUDMessage::ShowHUDMessage(Locale::T(hidden ? "Trails hidden" : "Trails shown"));
		}
		logger::info("trails: {}", hidden ? "hidden" : "shown");
	}

	// Marks or releases whatever is under the aim - a person first, then
	// their footprints. True if it acted, false if nothing was there.
	bool Sense::MarkUnderAim()
	{
		const auto* hotkeySettings = Settings::GetSingleton();

		// Marking and releasing share one shape whether the press landed on a
		// person or on their footprints.
		const auto toggleMark = [&](RE::FormID a_id, RE::ObjectRefHandle a_handle,
									const std::string& a_who) {
			std::string note;
			if (_marked.erase(a_id) > 0) {
				note = Locale::T("No longer tracking {}");
				// The owner may be unloaded, in which case no poll will re-tint
				// their trail; hand it back to neutral here.
				if (const auto trail = _trails.find(a_id); trail != _trails.end()) {
					trail->second.colour = hotkeySettings->neutralColour;
				}
			} else {
				if (!hotkeySettings->multiMark) {
					// One quarry at a time: the new mark replaces the old.
					_marked.clear();
				}
				// The lowest palette slot not in use, so colours stay stable
				// as marks come and go.
				std::uint8_t slot = 0;
				for (; slot < std::size(kMarkPalette); ++slot) {
					const bool taken = std::ranges::any_of(_marked,
						[&](const auto& a_pair) { return a_pair.second.slot == slot; });
					if (!taken) {
						break;
					}
				}
				slot = static_cast<std::uint8_t>(slot % std::size(kMarkPalette));
				_marked[a_id] = { a_handle, slot };
				if (const auto trail = _trails.find(a_id); trail != _trails.end()) {
					trail->second.colour = kMarkPalette[slot];
				}
				note = Locale::T("Tracking {}");
			}
			if (const auto at = note.find("{}"); at != std::string::npos) {
				note.replace(at, 2, a_who);
			}
			if (hotkeySettings->trailToasts) {
				RE::SendHUDMessage::ShowHUDMessage(note.c_str());
			}
			logger::info("trails: {} ({} marked)", note, _marked.size());
		};

		auto* target = [&]() -> RE::Actor* {
			if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
				if (auto ref = pick->target.get(); ref) {
					if (auto* actor = ref->As<RE::Actor>(); actor) {
						return actor;
					}
				}
			}
			return PickByView(hotkeySettings->trackRange);
		}();

		if (target && !target->IsPlayerRef()) {
			const auto* name = target->GetDisplayFullName();
			toggleMark(target->GetFormID(), target->CreateRefHandle(),
				name && name[0] ? ToUtf8(name) : std::string{ "?" });
			return true;
		}

		// Nobody under the aim - but maybe their footprints are. Seeing
		// Hulda's trail on the ground and marking the trail itself is how a
		// tracker hunts someone they have not caught up with yet.
		const auto bestId = PickTrailByView(hotkeySettings->trackRange);
		if (bestId != 0) {
			// The owner might be a cell away; the quarry's handle still
			// reaches them, and failing that the form itself does, so the
			// recording resumes the moment they are loaded again.
			RE::ObjectRefHandle handle;
			if (const auto quarry = _quarry.find(bestId); quarry != _quarry.end()) {
				handle = quarry->second.handle;
			}
			if (!handle.get()) {
				if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(bestId)) {
					handle = actor->CreateRefHandle();
				}
			}
			toggleMark(bestId, handle, _trails[bestId].name);
			return true;
		}

		return false;
	}

	// The all-in-one key's lone press, queued until the double-tap window
	// has ruled out a sweep. Refusals are silent here: on a shared key a
	// stray tap outside the hunt is noise, not a question.
	void Sense::QueueDeferredMark(float a_delay)
	{
		_deferredMarkAt = SS::RealNow() + a_delay;
	}

	void Sense::CancelDeferredMark()
	{
		_deferredMarkAt = -1.0f;
	}

	// The single-key mode: a press marks or releases whatever the aim finds
	// - a person, or their footprints. Showing and hiding is the reveal
	// rule's job now, so an empty press just says so; the wipe rides its
	// own gesture on the same key.
	void Sense::OnTrailHotkey()
	{
		if (!TrailGateOpen()) {
			return;
		}
		if (!MarkUnderAim()) {
			if (Settings::GetSingleton()->trailToasts) {
				RE::SendHUDMessage::ShowHUDMessage(Locale::T("Nothing under the aim to mark"));
			}
			logger::info("trails: press, nothing under the aim");
		}
	}

	// The multi-key mode's separated halves: marking alone, and the
	// hide/show toggle alone.
	void Sense::OnTrailMark()
	{
		if (!TrailGateOpen()) {
			return;
		}
		if (!MarkUnderAim()) {
			logger::info("trails: mark key, nothing under the aim");
		}
	}

	void Sense::OnTrailToggle()
	{
		if (!TrailGateOpen()) {
			return;
		}
		ToggleTrailsShown();
	}

	void Sense::PollTrails()
	{
		const auto* settings = Settings::GetSingleton();
		if (!settings->trailsEnabled) {
			if (!_trails.empty() || !_quarry.empty()) {
				_trails.clear();
				_quarry.clear();
				Labels::GetSingleton()->SetTrails({}, false);
			}
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		const float real = SS::RealNow();
		const float lifetime = settings->trailLifetime;

		// The all-in-one key's lone press comes due: no second tap arrived,
		// so it was a mark after all. Quietly ignored when the ground is not
		// readable - on a shared key a stray tap is noise, not a question.
		if (_deferredMarkAt >= 0.0f && real >= _deferredMarkAt) {
			_deferredMarkAt = -1.0f;
			if (!TrailsRevealed()) {
				logger::info("trails: lone press outside the hunt, ignored");
			} else if (!MarkUnderAim()) {
				if (settings->trailToasts) {
					RE::SendHUDMessage::ShowHUDMessage(Locale::T("Nothing under the aim to mark"));
				}
				logger::info("trails: lone press, nothing under the aim");
			}
		}

		// Marks from another place are nonsense in this one: an interior's
		// coordinates mean nothing outside it. Wipe on transitions between
		// interior and worldspace, or between worldspaces.
		//
		// The worldspace comes from the TES singleton's plain field, NEVER
		// TESObjectREFR::GetWorldspace(): that call routes through an engine
		// function LumaUtil hooks as its cell-change thunk, which reads our
		// garbage second-argument register as a cell pointer and crashes the
		// game on the very first poll. Found the hard way, resolved by
		// symbolising the crash stack against the linker map.
		const auto* cell = player->GetParentCell();
		const bool  interior = cell && cell->IsInteriorCell();
		const auto* tes = RE::TES::GetSingleton();
		const auto* currentWs = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
		const auto  worldspace = currentWs ? currentWs->GetFormID() : RE::FormID{ 0 };
		const bool placeChanged =
			_placeKnown && (interior != _wasInterior || (!interior && worldspace != _lastWorldspace));
		_placeId = interior ? (cell ? cell->GetFormID() : RE::FormID{ 0 }) : worldspace;
		if (placeChanged && settings->trailsClearOnTransition) {
			if (settings->trailsForgetMarked) {
				// The hunt ends at the door: marks released, everything wiped.
				_trails.clear();
				_quarry.clear();
				_marked.clear();
				logger::info("trails: place changed, wiped marks and all");
			} else {
				// A marked quarry survives the wipe. Their points are stamped
				// with the place they were laid in and drawn only there, so
				// stale coordinates never bleed into the wrong world - and
				// recording carries on wherever the owner next turns up.
				std::erase_if(_trails,
					[&](const auto& a_pair) { return !_marked.contains(a_pair.first); });
				std::erase_if(_quarry,
					[&](const auto& a_pair) { return !_marked.contains(a_pair.first); });
				logger::info("trails: place changed, wiped ({} marked kept)", _trails.size());
			}
		}

		// Auto-capture watches the world, not the door: everyone loaded gets
		// a recording window - those already here when you arrived, and those
		// the world streams in as you travel. Once a second is plenty; the
		// windows refresh while their owners stay loaded.
		if (settings->trailAutoCapture &&
			(placeChanged || !_placeKnown || real - _lastCaptureAt > 1.0f)) {
			_lastCaptureAt = real;
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				lists->ForEachHighActor([&](RE::Actor* a_actor) {
					if (a_actor && !a_actor->IsPlayerRef() && a_actor->Is3DLoaded()) {
						const auto life = a_actor->AsActorState()->GetLifeState();
						if (life != RE::ACTOR_LIFE_STATE::kDying &&
							life != RE::ACTOR_LIFE_STATE::kDead) {
							_quarry[a_actor->GetFormID()] = { a_actor->CreateRefHandle(),
								real + lifetime };
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		}

		_placeKnown = true;
		_wasInterior = interior;
		_lastWorldspace = worldspace;

		// The optional mercy of the death release: a quarry seen dead slips
		// the mark after a while, and the trail is left to age out on its own.
		if (settings->markDeathRelease) {
			for (auto it = _marked.begin(); it != _marked.end();) {
				auto& mark = it->second;
				auto  ref = mark.handle.get();
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (actor && actor->Is3DLoaded()) {
					const auto life = actor->AsActorState()->GetLifeState();
					const bool dead = life == RE::ACTOR_LIFE_STATE::kDying ||
					                  life == RE::ACTOR_LIFE_STATE::kDead;
					if (dead && mark.deadAt <= 0.0f) {
						mark.deadAt = real;
					} else if (!dead) {
						mark.deadAt = 0.0f;  // resurrection happens, in Skyrim
					}
				}
				if (mark.deadAt > 0.0f && real - mark.deadAt >= settings->markDeathDelay) {
					if (const auto trail = _trails.find(it->first); trail != _trails.end()) {
						trail->second.colour = settings->neutralColour;
						logger::info("trails: {} is dead, mark released", trail->second.name);
					}
					it = _marked.erase(it);
					continue;
				}
				++it;
			}
		}

		// Fighting somebody keeps their recording window open; a mark holds
		// it open for as long as they are anywhere near.
		for (const auto& [id, track] : _combatHits) {
			auto& quarry = _quarry[id];
			quarry.handle = track.handle;
			quarry.untilAt = real + lifetime;
		}
		for (const auto& [id, mark] : _marked) {
			auto& quarry = _quarry[id];
			quarry.handle = mark.handle;
			quarry.untilAt = real + lifetime;
		}

		// Sample everyone still in their window: a point roughly every stride,
		// no closer than 24 units, no more often than 0.4s.
		for (auto it = _quarry.begin(); it != _quarry.end();) {
			if (real > it->second.untilAt) {
				it = _quarry.erase(it);
				continue;
			}
			auto  ref = it->second.handle.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (actor && actor->Is3DLoaded()) {
				const auto life = actor->AsActorState()->GetLifeState();
				const bool dead =
					life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead;
				// A hard cap on how many trails exist at once; a sweep through
				// a city could otherwise open sixty of them.
				if (!dead && (_trails.size() < 64 || _trails.contains(it->first))) {
					auto& trail = _trails[it->first];
					if (trail.name.empty()) {
						const auto* name = actor->GetDisplayFullName();
						trail.name = name && name[0] ? ToUtf8(name) : std::string{ "?" };
					}
					const auto markTint = MarkColour(it->first);
					trail.colour = markTint            ? markTint
					               : actor->IsHostileToActor(player) ? settings->hostileColour
					                                                 : settings->neutralColour;
					if (real - trail.lastSampleAt > 0.4f) {
						const auto at = actor->GetPosition();
						// The stride check only means anything against a point
						// in the same place; against another cell's coordinates
						// it is noise that happens to pass.
						if (trail.points.empty() || trail.points.back().place != _placeId ||
							at.GetDistance(trail.points.back().pos) > 24.0f) {
							trail.lastSampleAt = real;
							trail.points.push_back({ at, real, _placeId });
						}
					}
				}
			}
			++it;
		}

		// Hidden hides, it does not forget: recording carried on above, so
		// showing them again brings the whole picture back.
		// The hide/show toggle is a multi-key affordance; in single-key mode
		// the reveal rule alone decides, so a stale hidden flag cannot trap
		// the trails out of sight.
		if (_trailsHidden.load() && settings->trailMode == TrailKeyMode::kMulti) {
			Labels::GetSingleton()->SetTrails({}, false);
			if (_sneakPreviewWas && !_active) {
				ClearAimPreview();
				_sneakPreviewWas = false;
			}
			return;
		}
		// The reveal rule: outside it only marked quarry stay visible -
		// hiding them too would make marking pointless.
		const bool revealed = TrailsRevealed();
		const bool senseGate = !revealed;

		// Age out, then hand Labels a drawable copy with the fades already
		// worked out - the render thread never reconciles clocks.
		std::vector<Labels::Trail> out;
		for (auto it = _trails.begin(); it != _trails.end();) {
			auto& trail = it->second;
			std::erase_if(trail.points,
				[&](const auto& a_point) { return real - a_point.bornAt > lifetime; });
			if (trail.points.empty()) {
				it = _trails.erase(it);
				continue;
			}

			const bool isMarked = _marked.contains(it->first);
			if (senseGate && !isMarked) {
				++it;
				continue;
			}

			// Only the points laid in this place are drawable; the rest wait
			// for the player to go back to where they were laid.
			const bool anyHere = std::ranges::any_of(trail.points,
				[&](const auto& a_point) { return a_point.place == _placeId; });
			if (!anyHere) {
				++it;
				continue;
			}

			Labels::Trail drawable;
			drawable.colour = trail.colour;
			drawable.bright = isMarked;
			drawable.id = it->first;
			if ((settings->trailNames || isMarked || _aimTrailId == it->first) &&
				!trail.name.empty()) {
				// The translation carries a {} for the name; filled by hand so
				// a bad translation cannot throw.
				std::string form{ Locale::T("{}'s footprints") };
				if (const auto slot = form.find("{}"); slot != std::string::npos) {
					form.replace(slot, 2, trail.name);
				} else {
					form = trail.name;
				}
				drawable.label = std::move(form);
			}
			drawable.marks.reserve(trail.points.size());
			for (const auto& point : trail.points) {
				if (point.place != _placeId) {
					continue;
				}
				drawable.marks.push_back({ point.pos, 1.0f - (real - point.bornAt) / lifetime });
			}
			out.push_back(std::move(drawable));
			++it;
		}
		// A running sweep lights the trails up along with everything else,
		// and a crouch-reveal carries its own light too. "Always" stays dim
		// between sweeps - permanent scent-lines would own the screen.
		Labels::GetSingleton()->SetTrails(std::move(out),
			_active.load() || (revealed && settings->trailReveal != TrailReveal::kAlways));

		// Outside a sweep, a crouch-reveal drives the marking sign; Tick
		// owns it while a wave is live, so only one of them ever does.
		const bool crouchPreview =
			!_active && revealed && settings->trailReveal == TrailReveal::kCrouch;
		if (crouchPreview) {
			UpdateAimPreview();
		} else if (_sneakPreviewWas && !_active) {
			ClearAimPreview();
		}
		_sneakPreviewWas = crouchPreview;
	}

	void Sense::Tick()
	{
		if (!_active) {
			return;
		}

		auto*      ui = RE::UI::GetSingleton();
		const bool suspended = (Settings::GetSingleton()->menuAware && GameMenus::GetSingleton()->Blocking()) ||
		                       (ui && ui->GameIsPaused());

		// Watchdog. A wave is a few seconds of work; if it is still going a long
		// while later in REAL time, something is holding it - a menu we misjudged
		// as blocking, a pause that never lifted - and it has to be broken out of
		// rather than left to pin the mod down forever. 1.13 shipped without this
		// and a single stuck menu made every feature silently stop working.
		const auto realElapsed = SS::RealNow() - _startedRealAt;
		const auto budget = (_waveEnd - _waveStart) + 60.0f;
		if (realElapsed > budget) {
			logger::warn(
				"watchdog: the wave has been running {:.0f}s of real time (budget {:.0f}s), "
				"suspended={} menus={} - cancelling it",
				realElapsed, budget, suspended, GameMenus::GetSingleton()->Describe());
			Cancel();
			return;
		}

		// Keep every tag over the thing it names.
		//
		// This runs on the main thread, which is the only place a reference
		// handle may be resolved, and it is the reason the render thread never
		// sees a game object: it reads a position that was written here. About a
		// hundred anchors at sixty hertz is nothing next to what the sweep
		// already does once per object.
		if (!suspended && Settings::GetSingleton()->labelsFollow) {
			auto* topics = RE::MenuTopicManager::GetSingleton();

			_anchorBuffer.clear();
			_speakingBuffer.clear();
			_vitalsBuffer.clear();
			_anchorBuffer.reserve(_labelBuffer.size());
			_speakingBuffer.reserve(_labelBuffer.size());
			_vitalsBuffer.reserve(_labelBuffer.size());

			for (auto& entry : _labelBuffer) {
				auto ref = entry.owner.get();
				if (!ref) {
					// Gone - unloaded, or a merged tag that never had an owner.
					// Leave it where it was rather than dropping it mid-fade.
					_anchorBuffer.push_back(entry.world);
					_speakingBuffer.push_back(false);
					_vitalsBuffer.push_back({ entry.vitals[0], entry.vitals[1], entry.vitals[2],
						entry.vitalsAt, entry.vitalsCap[0], entry.vitalsCap[1], entry.vitalsCap[2],
						entry.vitalsPeak[0], entry.vitalsPeak[1], entry.vitalsPeak[2] });
					continue;
				}

				auto* actor = ref->As<RE::Actor>();
				entry.world = TagAnchor(ref.get(), actor != nullptr);

				bool speaking = false;
				if (actor) {
					// Two ways of being mid-sentence. The state flag covers a
					// conversation with you; the topic manager's speaker covers
					// the moment after the menu closes while the line plays out,
					// which is exactly when a floating subtitle is on screen.
					if (auto* state = actor->AsActorState(); state) {
						speaking = state->actorState2.talkingToPlayer != 0;
					}
					if (!speaking && topics) {
						speaking = topics->speaker == entry.owner ||
						           topics->lastSpeaker == entry.owner;
					}
				}

				// Yours are re-read rather than carried: a bar that froze at the
				// value you had three seconds ago is worse than no bar, and this
				// is meant to stand in for the HUD.
				if (actor && entry.vitals[0] >= 0.0f) {
					const auto* live = Settings::GetSingleton();
					const bool  wanted = entry.vitalsSelf ? live->selfBars : live->vitalsActors;
					if (wanted) {
						float before[3]{ entry.vitals[0], entry.vitals[1], entry.vitals[2] };
						ReadVitals(actor, entry.vitals, entry.vitalsCap, entry.vitalsPeak);
						if (!entry.vitalsSelf && !live->vitalsActorsAll) {
							entry.vitals[1] = -1.0f;
							entry.vitals[2] = -1.0f;
						}
						for (int i = 0; i < 3; ++i) {
							if (entry.vitals[i] >= 0.0f && before[i] >= 0.0f &&
								std::abs(entry.vitals[i] - before[i]) > 0.0005f) {
								entry.vitalsAt = SS::Now();
								break;
							}
						}
					}
				}

				_anchorBuffer.push_back(entry.world);
				_speakingBuffer.push_back(speaking);
				_vitalsBuffer.push_back({ entry.vitals[0], entry.vitals[1], entry.vitals[2],
						entry.vitalsAt, entry.vitalsCap[0], entry.vitalsCap[1], entry.vitalsCap[2],
						entry.vitalsPeak[0], entry.vitalsPeak[1], entry.vitalsPeak[2] });
			}

			Labels::GetSingleton()->MoveTo(_anchorBuffer, _speakingBuffer, _vitalsBuffer);
		}

		// While the sense is open, the marking sign follows whatever the
		// trail key would take - a person, or a trail on the ground.
		if (!suspended) {
			UpdateAimPreview();
		} else {
			ClearAimPreview();
		}

		// Stop the clock rather than shifting each deadline. Shifting only ever
		// moved the wave: the tags, the ring and the vignette are timed by
		// Labels and kept running, so a long pause used to expire them while the
		// wave sat still. One clock, one place to stop it.
		SetClockFrozen(suspended);
		if (suspended) {
			// Hand the image space back while a menu is up rather than holding
			// it at neutral values. Anything the menu wants to do to the screen
			// is then unobstructed, and we take it again on the way out.
			Vision::GetSingleton()->End();
			return;
		}

		if (!UsingPostFX()) {
			// Re-take it if a menu made us let go.
			Vision::GetSingleton()->Begin(_waveStart, _waveEnd);

			// Ease the image space every tick, outside our own lock: the override
			// is read by the renderer and has nothing to do with the pending list.
			Vision::GetSingleton()->Update();
		}

		std::scoped_lock guard{ _lock };

		const auto now = SS::Now();
		const auto elapsed = now - _waveStart;

		while (_next < _pending.size() && _pending[_next].triggerAt <= elapsed) {
			const auto& entry = _pending[_next];
			if (auto ref = entry.handle.get(); ref && ref->Is3DLoaded()) {
				ApplyTo(ref.get(), entry.category);
			}
			++_next;
		}

		if (_next >= _pending.size() && now >= _waveEnd) {
			logger::info("wave finished, {} shaders were applied", _appliedThisWave);
			ClearAimPreview();
			Labels::GetSingleton()->Clear();
			_active = false;
			_pending.clear();
			_next = 0;
			EndTint();
		}
	}

	void Sense::ApplyTo(RE::TESObjectREFR* a_ref, Category a_category)
	{
		if (_pool.empty()) {
			return;
		}

		const auto* settings = Settings::GetSingleton();
		auto*       shader = _pool[_cursor % _pool.size()];
		++_cursor;

		auto& data = shader->data;

		const auto& category = settings->categories[static_cast<std::size_t>(a_category)];

		const bool isActor = a_category == Category::kActor;
		const auto reading = isActor ? Judge(a_ref, *settings) : Reading{};

		// A sweep opens the recording window: everyone it lights leaves
		// breadcrumbs for the trail lifetime, dead ones excepted - corpses
		// do not walk.
		if (isActor && settings->trailsEnabled) {
			if (auto* quarry = a_ref->As<RE::Actor>(); quarry) {
				const auto life = quarry->AsActorState()->GetLifeState();
				if (life != RE::ACTOR_LIFE_STATE::kDying && life != RE::ACTOR_LIFE_STATE::kDead) {
					_quarry[quarry->GetFormID()] = { quarry->CreateRefHandle(),
						SS::RealNow() + settings->trailLifetime };
				}
			}
		}
		// User rules only apply to people. Everything they can match on -
		// factions, relationship lists, names - is a property of a person.
		const auto mark = isActor ? Marks::GetSingleton()->Match(a_ref->As<RE::Actor>()) : -1;
		auto       markCount = mark >= 0 ? Marks::GetSingleton()->Count(mark, a_ref->As<RE::Actor>()) : -1;

		// Marker appearance is a setting, not a rule - so the menu can restyle a
		// marker without rewriting the hand-annotated rule file.
		float         markFill = -1.0f;
		std::uint32_t markColour = 0xFFFFFF;
		int           markCap = 101;
		int           effectiveMark = -1;
		bool          markAtFull = false;
		if (mark >= 0) {
			const auto& rule = Marks::GetSingleton()->Rules()[static_cast<std::size_t>(mark)];
			auto&       style = Settings::GetSingleton()->StyleFor(rule.name, rule.colour);

			if (style.enabled) {
				effectiveMark = mark;
				markColour = style.overrideColour ? style.colour : rule.colour;
				markCap = rule.countCap;

				// "Full" is the same threshold the filling icon uses, whether or
				// not the icon is drawn that way - so switching the tally to a
				// number does not quietly change what full means.
				const auto full = static_cast<int>(std::max<std::uint32_t>(style.countFull, 1));
				const bool reachedFull = markCount >= full;

				if (reachedFull) {
					markAtFull = settings->ostimMaraAtFull;
					Promote(a_ref->As<RE::Actor>(), *settings);
				}

				switch (style.countStyle) {
				case CountStyle::kFill:
					markFill = markCount >= 0
								   ? std::clamp(static_cast<float>(markCount) /
													static_cast<float>(std::max<std::uint32_t>(style.countFull, 1)),
										 0.0f, 1.0f)
								   : 0.0f;
					break;
				case CountStyle::kHidden:
					markCount = -1;
					break;
				default:
					break;
				}
			}
		}

		// What this person is to Skyrim, rather than to you. Only people have
		// one, and only ever one - two words over one head is worse than the
		// wrong word.
		std::string   title;
		std::uint32_t titleColour = settings->titleColour;
		if (isActor && settings->titlesShow) {
			auto* who = a_ref->As<RE::Actor>();
			if (const auto found = Titles::GetSingleton()->For(who); !found.empty()) {
				title = ToUtf8(found.c_str());
				titleColour = Titles::GetSingleton()->ColourFor(who);
			}
		}

		const bool favourite =
			!isActor && settings->favouriteHighlight && !_favourites.empty() &&
			a_ref->GetBaseObject() && _favourites.contains(a_ref->GetBaseObject()->GetFormID());
		const auto disposition = reading.icon;
		auto colour = (isActor && settings->actorByDisposition && disposition != Disposition::kNone)
						  ? reading.colour
						  : category.colour;

		// A marked quarry outshines every other rule: the sweep lights them in
		// their own mark colour, the same one their trail wears, so a glance
		// says who is being tracked.
		const auto markGlow = isActor ? MarkColour(a_ref->GetFormID()) : 0;
		if (markGlow) {
			colour = markGlow;
		}

		// The membrane redraws the target mesh. With kEqual it only survives where
		// the mesh is the frontmost surface, i.e. walls hide it. kAlways draws it
		// regardless of what is in front - that is the see-through-walls look.
		data.membraneShaderZTestFunction =
			settings->throughWalls ? RE::D3DCMPFUNC::kAlways : RE::D3DCMPFUNC::kEqual;

		// Written here as well as in the esp so a stale esp copy still gets it:
		// without it the membrane inherits the target mesh's own alpha test,
		// and on alpha-carded foliage (mountain flowers and friends) the whole
		// membrane fails the cutoff - name tag, no glow.
		data.flags.set(RE::EffectShaderData::Flags::kIgnoreBaseGeomTexAlpha);

		const auto edge = ToColor(colour, settings->glowStrength);
		const auto fill = ToColor(colour, settings->glowStrength * 0.5f);

		data.edgeEffectColor = edge;
		data.edgeColor = edge;
		// The membrane is flagged greyscale-to-colour, so the fill texture's
		// luminance indexes these three keys. Setting all three the same gives a
		// flat category colour instead of a gradient.
		data.fillTextureEffectColorKey1 = fill;
		data.fillTextureEffectColorKey2 = fill;
		data.fillTextureEffectColorKey3 = fill;
		data.edgeEffectFallOff = settings->edgeFalloff;

		data.edgeEffectAlphaFadeInTime = settings->fadeIn;
		data.edgeEffectFullAlphaTime = std::max(0.0f, settings->duration - settings->fadeIn - settings->fadeOut);
		data.edgeEffectAlphaFadeOutTime = settings->fadeOut;
		data.edgeEffectAlphaPulseAmplitude = settings->pulseAmplitude;
		data.edgeEffectAlphaPulseFrequency = settings->pulseFrequency;

		data.fillTextureEffectAlphaFadeInTime = settings->fadeIn;
		data.fillTextureEffectFullAlphaTime = data.edgeEffectFullAlphaTime;
		data.fillTextureEffectAlphaFadeOutTime = settings->fadeOut;
		data.fillTextureEffectAlphaPulseAmplitude = settings->pulseAmplitude;
		data.fillTextureEffectAlphaPulseFrequency = settings->pulseFrequency;

		// Outline only. The membrane draws two things: a rim whose width comes
		// from the falloff, and a fill across the whole surface. Zeroing the
		// fill's two alpha ratios leaves the rim untouched and the interior
		// empty, which is the silhouette look.
		//
		// These have to be written both ways round, not just when the flag is
		// on: the pool is 128 shaders used round-robin, so the one we picked may
		// have been an outline last sweep and would otherwise stay one.
		data.fillTextureEffectFullAlphaRatio = category.outlineOnly ? 0.0f : 1.0f;
		data.fillTextureEffectPersistentAlphaRatio = category.outlineOnly ? 0.0f : 0.55f;
		// A tighter rim so the silhouette is a line rather than a haze.
		data.edgeEffectFallOff = category.outlineOnly
									 ? std::max(settings->edgeFalloff, 2.6f)
									 : settings->edgeFalloff;

		auto* effect = a_ref->ApplyEffectShader(shader, settings->duration);
		++_appliedThisWave;

		if (favourite) {
			colour = settings->favouriteColour;
		}

		// Snapshot everything the name tag needs now, on the main thread. The
		// HUD callback runs on the render thread and must not touch game objects.
		if (settings->labelsEnabled) {
			const auto* name = a_ref->GetDisplayFullName();
			if (name && name[0] != '\0') {
				const bool inRange =
					settings->labelMaxDistance <= 0.0f ||
					RE::PlayerCharacter::GetSingleton()->GetPosition().GetDistance(a_ref->GetPosition()) <=
						settings->labelMaxDistance;

				if (inRange) {
					const auto now = SS::Now();
					auto       text = ToUtf8(name);
					// Their level rides in the name itself - "(12)" is the same
					// in every language - so the tag layout knows nothing new.
					if (isActor && settings->levelOthers) {
						text += std::format(" ({})", a_ref->As<RE::Actor>()->GetLevel());
					}
					const auto anchor = TagAnchor(a_ref, isActor);

					// Fold this into a tag we already have if it is the same
					// thing standing next to itself. People are never folded -
					// two guards are two guards, and the whole point of naming
					// them is that they are individuals.
					Labels::Entry* merged = nullptr;
					if (!isActor && settings->labelMergeDistance > 0.0f) {
						const auto reach = settings->labelMergeDistance * settings->labelMergeDistance;
						for (auto& existing : _labelBuffer) {
							if (existing.icon != Disposition::kNone || existing.text != text) {
								continue;
							}
							const auto delta = existing.world - anchor;
							if (delta.SqrLength() <= reach) {
								merged = std::addressof(existing);
								break;
							}
						}
					}

					if (merged) {
						// Creep the tag toward the middle of the group and keep
						// it alive as long as the newest member of it.
						const auto weight = 1.0f / static_cast<float>(merged->count + 1);
						merged->world = merged->world * (1.0f - weight) + anchor * weight;
						merged->diesAt = now + settings->duration;
						++merged->count;
					} else {
						_labelBuffer.push_back(Labels::Entry{
							std::move(text),
							anchor,
							colour,
							now,
							now + settings->duration,
							isActor ? settings->labelActorScale
									: (favourite ? settings->favouriteScale : 1.0f),
							disposition,
							1,
							effectiveMark,
							markColour,
							markCount,
							markCap,
							markFill,
							favourite,
							markAtFull,
							isActor ? Arousal::GetSingleton()->Value(a_ref->As<RE::Actor>()) : -1,
							title,
							titleColour,
							a_ref->CreateRefHandle() });

						if (isActor && settings->weaponIcons) {
							_labelBuffer.back().weapon = static_cast<std::uint8_t>(
								ClassifyWeapon(a_ref->As<RE::Actor>()));
						}
						if (isActor && settings->raceIcons) {
							const auto [kind, mod] = ClassifyRace(a_ref->As<RE::Actor>());
							_labelBuffer.back().race = static_cast<std::uint8_t>(kind);
							_labelBuffer.back().raceMark = static_cast<std::uint8_t>(mod);
						}

						// Vitals over other people last exactly as long as the
						// sweep does. Persistent bars over everyone are what a
						// combat HUD mod is for; what this adds is the whole
						// room at once, through walls.
						if (isActor && settings->vitalsActors &&
							(!settings->vitalsActorsHostileOnly ||
								disposition == Disposition::kHostile)) {
							auto& fresh = _labelBuffer.back();
							ReadVitals(a_ref->As<RE::Actor>(), fresh.vitals, fresh.vitalsCap, fresh.vitalsPeak);
							if (!settings->vitalsActorsAll) {
								fresh.vitals[1] = -1.0f;
								fresh.vitals[2] = -1.0f;
							}
							fresh.vitalsAt = now;

							// Sweep bars win the head too, for their few seconds.
							if (g_trueHud && settings->pushTrueHUDAside) {
								g_trueHud->RemoveActorInfoBar(a_ref->As<RE::Actor>()->GetHandle(),
									TRUEHUD_API::WidgetRemovalMode::Immediate);
							}
						}
					}

					Labels::GetSingleton()->Replace(_labelBuffer);
				}
			}
		}

		// Checkpoints across the wave rather than 96 lines: this is what tells
		// us whether the stagger is real or whether everything lit on one frame.
		const auto total = _pending.size();
		const bool checkpoint =
			_appliedThisWave == 1 ||
			(total > 3 && (_appliedThisWave == total / 4 || _appliedThisWave == total / 2 ||
							  _appliedThisWave == total * 3 / 4 || _appliedThisWave == total));

		if (settings->debug || checkpoint) {
			// Ghost actors (mannequins are the everyday case) are suspected of
			// swallowing membranes; say so next to the apply result so a report
			// of "tag but no glow" comes with the evidence attached.
			const auto* asActor = a_ref->As<RE::Actor>();
			logger::info(
				"apply {}/{} at t+{:.2f}s: ref {:08X} ({}) {:.0f} units -> {}{}",
				_appliedThisWave, total, SS::Now() - _waveStart,
				a_ref->GetFormID(), CategoryName(a_category),
				RE::PlayerCharacter::GetSingleton()->GetPosition().GetDistance(a_ref->GetPosition()),
				effect ? "ok" : "ENGINE RETURNED NULL",
				asActor && asActor->IsGhost() ? " [ghost]" : "");
		}
	}

	void Sense::ClearOurEffects()
	{
		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return;
		}

		processLists->ForEachShaderEffect([this](RE::ShaderReferenceEffect* a_effect) {
			if (a_effect && a_effect->effectData && _poolSet.contains(a_effect->effectData)) {
				a_effect->finished = true;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	void Sense::SoftenOurEffects(float a_fade)
	{
		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return;
		}

		// The EFSH fade-out window sits at the end of the lifetime, so pulling
		// the lifetime in to age + fade makes the engine start that fade now.
		processLists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
			if (a_effect && a_effect->effectData && _poolSet.contains(a_effect->effectData) &&
				a_effect->lifetime > a_effect->age + a_fade) {
				a_effect->lifetime = a_effect->age + a_fade;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	void Sense::FadeOut()
	{
		const auto* settings = Settings::GetSingleton();
		const float fade = std::max(settings->fadeOut, 0.15f);

		{
			std::scoped_lock guard{ _lock };
			if (!_active) {
				return;
			}
			// Nothing new lights; everything lit leaves on its own fade. The
			// normal finish in Tick() does the bookkeeping when the fade ends.
			_pending.clear();
			_next = 0;
			SoftenOurEffects(fade);
			_waveEnd = SS::Now() + fade;
		}

		Labels::GetSingleton()->Expire(fade);

		// The image space route would otherwise hold the tint at full strength
		// to the last frame and then pop: give it a short envelope that eases
		// from the current grade back to neutral over the fade.
		if (settings->tintUseImod && _imod) {
			const auto ease = [&](RE::NiFloatInterpolator* a_interp, float a_from) {
				if (!a_interp) {
					return;
				}
				auto* data = a_interp->floatData.get();
				if (!data || !data->keys || data->numKeys != 4 || data->keySize != 8) {
					return;
				}
				auto* keys = reinterpret_cast<float*>(data->keys);
				keys[0] = 0.0f;
				keys[1] = a_from;
				keys[2] = 0.05f * fade;
				keys[3] = a_from;
				keys[4] = 0.9f * fade;
				keys[5] = 1.0f;
				keys[6] = fade;
				keys[7] = 1.0f;
			};
			_imod->data.duration = fade;
			ease(_imod->cinematic.saturation.mult.get(), settings->tintSaturation);
			ease(_imod->cinematic.brightness.mult.get(), settings->tintBrightness);
			ease(_imod->cinematic.contrast.mult.get(), settings->tintContrast);
			RE::ImageSpaceModifierInstanceForm::Trigger(_imod, settings->tintStrength, nullptr);
		}

		logger::info("wave fading out over {:.2f}s", fade);
	}

	void Sense::BeginTint(float a_duration)
	{
		const auto* settings = Settings::GetSingleton();

		// Only one of the two should grade the frame. When the shader pass is
		// available it wins, because it is the one that survives Community
		// Shaders; if it later fails, the image space route picks up again on
		// the next sweep.
		if (!UsingPostFX()) {
			Vision::GetSingleton()->Begin(_waveStart, _waveEnd);
		} else {
			Vision::GetSingleton()->End();
		}

		if (!settings->tintUseImod || !_imod) {
			return;
		}

		const auto duration = std::max(a_duration, 0.5f);

		_imod->data.duration = duration;
		RewriteEnvelope(_imod->cinematic.saturation.mult.get(), duration, settings->tintSaturation);
		RewriteEnvelope(_imod->cinematic.brightness.mult.get(), duration, settings->tintBrightness);
		RewriteEnvelope(_imod->cinematic.contrast.mult.get(), duration, settings->tintContrast);

		const auto* instance = RE::ImageSpaceModifierInstanceForm::Trigger(_imod, settings->tintStrength, nullptr);
		logger::info("tint triggered for {:.2f}s -> {}", duration, instance ? "ok" : "engine returned null");
	}

	void Sense::EndTint()
	{
		Labels::GetSingleton()->StopWash();
		Labels::GetSingleton()->StopRing();
		Vision::GetSingleton()->End();
		// Nothing is timed against it any more, and leaving it stopped would
		// make the next sweep start in the past.
		SetClockFrozen(false);

		if (_imod) {
			RE::ImageSpaceModifierInstanceForm::Stop(_imod);
		}
	}

	void Sense::Cancel()
	{
		{
			std::scoped_lock guard{ _lock };
			ClearOurEffects();
			_labelBuffer.clear();
			_pending.clear();
			_next = 0;
			_active = false;
		}
		ClearAimPreview();
		Labels::GetSingleton()->Clear();
		EndTint();
	}
}
