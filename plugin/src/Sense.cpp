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

namespace SS
{
	namespace
	{
		constexpr auto  kPluginFile = "ScavengerSense.esp"sv;
		constexpr auto  kShaderBase = static_cast<RE::FormID>(0x000800);
		constexpr auto  kShaderCount = std::size_t{ 128 };
		constexpr auto  kImodFormID = static_cast<RE::FormID>(0x000880);
		constexpr float kTickSeconds = 1.0f / 60.0f;

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

		// Health, magicka and stamina as fractions of their maximum. Permanent
		// plus the temporary modifier is the ceiling as the game itself reckons
		// it, so a fortify effect widens the bar instead of overflowing it.
		void ReadVitals(RE::Actor* a_actor, float (&a_out)[3])
		{
			auto* values = a_actor ? a_actor->AsActorValueOwner() : nullptr;
			if (!values) {
				return;
			}
			constexpr RE::ActorValue kWanted[3] = { RE::ActorValue::kHealth,
				RE::ActorValue::kMagicka, RE::ActorValue::kStamina };
			for (int i = 0; i < 3; ++i) {
				const float now = values->GetActorValue(kWanted[i]);
				const float top = values->GetPermanentActorValue(kWanted[i]) +
								  a_actor->GetActorValueModifier(
									  RE::ACTOR_VALUE_MODIFIER::kTemporary, kWanted[i]);
				a_out[i] = top > 0.0f ? std::clamp(now / top, 0.0f, 1.0f) : -1.0f;
			}
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
					if (a_player->IsInFaction(wolf)) {
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
					                      GameMenus::GetSingleton()->HasHidden();
					if (!_active && !idleWork) {
						continue;
					}
					if (auto* task = SKSE::GetTaskInterface()) {
						task->AddTask([this]() {
							PollSelf();
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
		case RE::FormType::Furniture:
			return Category::kActivator;
		case RE::FormType::NPC:
			return Category::kActor;
		default:
			return Category::kCount;
		}
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
		}

		if (settings->ignoreUnnamed) {
			const auto* name = a_ref->GetDisplayFullName();
			if (!name || name[0] == '\0') {
				++a_stats.unnamed;
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
			logger::info("a wave was already running - cancelling it instead");
			Cancel();
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
			// Toggle is off, so the press is not a cancel - but a wave that is
			// somehow still flagged active has to be cleared before a new one,
			// or the two share a pending list.
			logger::warn("a wave was still marked active without toggle on - clearing it first");
			Cancel();
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
			"disabled={} no3D={} harvested={} unnamed={} | actors seen={} castFailed={} dead={}",
			settings->radius, stats.visited, stats.accepted, stats.highActors, stats.wrongType, stats.categoryOff,
			stats.disabled, stats.noThreeD, stats.harvested, stats.unnamed,
			stats.actorsSeen, stats.actorCastFailed, stats.deadActor);

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

			ClearOurEffects();

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
					ReadVitals(player, _labelBuffer.back().vitals);
					_labelBuffer.back().vitalsSelf = true;
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

		if (settings->soundFormID != 0) {
			if (auto* audio = RE::BSAudioManager::GetSingleton()) {
				audio->Play(settings->soundFormID);
			}
		}

		logger::info("wave started: lighting {} of {} hits, ends at t+{:.2f}s | menus open: {}",
			keepAlso, hits.size(), _waveEnd - _waveStart, GameMenus::GetSingleton()->Describe());
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
		if (settings->selfHudCorner == Corner::kOff) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		float now[3]{ -1.0f, -1.0f, -1.0f };
		ReadVitals(player, now);

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

		Labels::GetSingleton()->SetSelfHud(now, _selfChangedAt);
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
					_vitalsBuffer.push_back({ entry.vitals[0], entry.vitals[1], entry.vitals[2], entry.vitalsAt });
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
						ReadVitals(actor, entry.vitals);
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
				_vitalsBuffer.push_back({ entry.vitals[0], entry.vitals[1], entry.vitals[2], entry.vitalsAt });
			}

			Labels::GetSingleton()->MoveTo(_anchorBuffer, _speakingBuffer, _vitalsBuffer);
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

		// The membrane redraws the target mesh. With kEqual it only survives where
		// the mesh is the frontmost surface, i.e. walls hide it. kAlways draws it
		// regardless of what is in front - that is the see-through-walls look.
		data.membraneShaderZTestFunction =
			settings->throughWalls ? RE::D3DCMPFUNC::kAlways : RE::D3DCMPFUNC::kEqual;

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

						// Vitals over other people last exactly as long as the
						// sweep does. Persistent bars over everyone are what a
						// combat HUD mod is for; what this adds is the whole
						// room at once, through walls.
						if (isActor && settings->vitalsActors &&
							(!settings->vitalsActorsHostileOnly ||
								disposition == Disposition::kHostile)) {
							auto& fresh = _labelBuffer.back();
							ReadVitals(a_ref->As<RE::Actor>(), fresh.vitals);
							if (!settings->vitalsActorsAll) {
								fresh.vitals[1] = -1.0f;
								fresh.vitals[2] = -1.0f;
							}
							fresh.vitalsAt = now;
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
			logger::info(
				"apply {}/{} at t+{:.2f}s: ref {:08X} ({}) {:.0f} units -> {}",
				_appliedThisWave, total, SS::Now() - _waveStart,
				a_ref->GetFormID(), CategoryName(a_category),
				RE::PlayerCharacter::GetSingleton()->GetPosition().GetDistance(a_ref->GetPosition()),
				effect ? "ok" : "ENGINE RETURNED NULL");
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
		Labels::GetSingleton()->Clear();
		EndTint();
	}
}
