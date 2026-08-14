#pragma once

namespace SS
{
	// The one place this mod writes to your save.
	//
	// Everything else here reads. Raising a relationship does not: the engine
	// keeps it on the NPC's base form and saves it as a change record, so it
	// survives uninstalling this mod and there is no undo. That is why it is off
	// until asked for, why it only ever raises, and why the previous value is
	// written to the log before anything happens - if it has to be put back, the
	// log says what it was.
	namespace Relations
	{
		// Nobody has a relationship with everybody, and "no record" is not the
		// same as "neutral", so it gets its own value.
		inline constexpr int kNoRecord = -100;

		// The rank the game's own condition functions use: 4 lover, 3 ally,
		// 2 confidant, 1 friend, 0 acquaintance, down to -4 archnemesis.
		//
		// Worth knowing, because it has caught people out: the stored byte runs
		// the other way, 0 for lover up to 8 for archnemesis. Reading it as the
		// rank gives an answer that looks plausible and is wrong by a mirror.
		[[nodiscard]] int Rank(RE::Actor* a_actor);

		// Levelled and generated NPCs are built from a template, and the engine
		// discards relationship changes made to them at the end of the session
		// without saying so. Better to decline than to appear to work.
		[[nodiscard]] bool Templated(RE::Actor* a_actor);

		// Raise this person's relationship with the player to a_rank, if it is
		// not already there or higher. Returns true if anything changed.
		//
		// Goes through the Papyrus method rather than poking the record, so the
		// engine does its own bookkeeping and the story event other mods listen
		// for is raised exactly as it would be normally.
		bool Raise(RE::Actor* a_actor, int a_rank);

		// Add to the vanilla faction that makes somebody a marriage candidate.
		// Held on the reference, so this one is reversible and does not touch
		// any record shipped in a plugin.
		bool MakeMarriageable(RE::Actor* a_actor);
	}
}
