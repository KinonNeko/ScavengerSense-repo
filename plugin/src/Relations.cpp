#include "Relations.h"

namespace SS
{
	namespace
	{
		constexpr RE::FormID kPotentialMarriageFaction = 0x00019809;
	}

	int Relations::Rank(RE::Actor* a_actor)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_actor || !player) {
			return kNoRecord;
		}

		auto* them = a_actor->GetActorBase();
		auto* you = player->GetActorBase();
		if (!them || !you) {
			return kNoRecord;
		}

		const auto* bond = RE::BGSRelationship::GetRelationship(them, you);
		if (!bond) {
			return kNoRecord;
		}

		// Stored 0 is lover and stored 8 is archnemesis, while the rank the game
		// conditions on runs +4 down to -4. One is the other subtracted from 4.
		return 4 - static_cast<int>(bond->level.underlying());
	}

	bool Relations::Templated(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		return base && base->baseTemplateForm != nullptr;
	}

	bool Relations::Raise(RE::Actor* a_actor, int a_rank)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_actor || !player || a_actor == player) {
			return false;
		}

		const auto* name = a_actor->GetDisplayFullName();
		const auto  who = name && name[0] ? name : "(unnamed)";

		if (Templated(a_actor)) {
			logger::info("relations: {} is built from a template, so a rank set on them "
						 "would be discarded at the end of the session - declining",
				who);
			return false;
		}

		const auto current = Rank(a_actor);

		// Only ever upward. The rank is one shared byte per pair and every
		// romance, follower and quest mod in the load order writes it; lowering
		// somebody else's work because our own tally happens to be lower would
		// be rude and impossible to trace.
		if (current != kNoRecord && current >= a_rank) {
			return false;
		}

		auto* vm = RE::SkyrimVM::GetSingleton();
		auto* impl = vm ? vm->impl.get() : nullptr;
		if (!impl) {
			logger::warn("relations: no script VM, cannot set a rank");
			return false;
		}

		logger::info("relations: raising {} from {} to {} - write this down, it is saved "
					 "in your game and uninstalling this mod will not undo it",
			who, current == kNoRecord ? std::string{ "no relationship record" } : std::to_string(current),
			a_rank);

		// Through the VM's own handle policy rather than the concrete one on
		// SkyrimVM: the concrete override is declared in the headers but not
		// compiled into the library, so it links only through this interface.
		auto* policy = impl->GetObjectHandlePolicy();
		if (!policy) {
			logger::warn("relations: no object handle policy");
			return false;
		}

		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		const auto handle = policy->GetHandleForObject(a_actor->GetFormType(), a_actor);
		if (handle == 0) {
			logger::warn("relations: no script handle for {}", who);
			return false;
		}

		auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), std::int32_t{ a_rank });
		impl->DispatchMethodCall2(handle, "Actor", "SetRelationshipRank", args, callback);
		return true;
	}

	bool Relations::MakeMarriageable(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}

		auto* handler = RE::TESDataHandler::GetSingleton();
		auto* faction = handler ? handler->LookupForm<RE::TESFaction>(
									  kPotentialMarriageFaction & 0xFFFFFF, "Skyrim.esm")
								: nullptr;
		if (!faction) {
			return false;
		}

		if (a_actor->IsInFaction(faction)) {
			return false;
		}

		a_actor->AddToFaction(faction, 1);
		const auto* name = a_actor->GetDisplayFullName();
		logger::info("relations: {} added to the marriage candidate faction",
			name && name[0] ? name : "(unnamed)");
		return true;
	}
}
