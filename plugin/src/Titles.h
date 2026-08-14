#pragma once

namespace SS
{
	// A small word above somebody's name saying what they are to Skyrim -
	// Jarl, Housecarl, Blacksmith, Thane of Whiterun - rather than what they are
	// to you.
	//
	// Driven by ScavengerSense_titles.ini, the same shape as the marker rules:
	// each section is a title and the lines under it are ways of matching. The
	// highest priority match wins, because two titles over one head is worse
	// than the wrong one.
	//
	// The game does not keep editor IDs for factions at runtime, so factions
	// have to be named by form ID. Quests do keep theirs, which is what makes
	// thanehood workable - it is not a faction at all, it is nine per-hold
	// favour quests, and those can be found by name.
	class Titles
	{
	public:
		struct Rule
		{
			std::string   name;   // section name, and the default text
			std::string   text;   // what is actually drawn
			std::uint32_t colour{ 0xC9C9C9 };
			int           priority{ 0 };

			std::vector<RE::TESFaction*>  factions;
			std::int8_t                   minRank{ 0 };
			std::vector<RE::BGSKeyword*>  keywords;
			std::vector<RE::BGSListForm*> lists;
			std::vector<RE::TESForm*>     bases;
			std::vector<std::string>      nameContains;
			std::vector<RE::TESQuest*>    questsDone;

			// Any faction the actor belongs to that the game itself flags as a
			// vendor. Far better than a list of shop factions: the flag is on
			// the record, so every modded merchant carries it too.
			bool vendor{ false };

			// Every NPC has a class, and most classes have a name the game
			// already shows the player elsewhere. Matching on it is what turns
			// a handful of curated factions into something that covers almost
			// everybody: a rule with fromClass takes the class's own name as
			// its text, so one rule names farmers, bandits, citizens, soldiers
			// and every class a mod adds without anybody listing them.
			std::vector<RE::TESClass*> classes;
			bool                       fromClass{ false };

			// Who the rule may apply to. Most titles are about other people;
			// thanehood is about you.
			bool forPlayer{ true };
			bool forOthers{ true };

			bool enabled{ true };
		};

		[[nodiscard]] static Titles* GetSingleton();

		void Load();

		// The title for this actor, or an empty view. Main thread.
		// A string rather than a view: a class-derived title is built from the
		// actor, so there is nothing stable to point at.
		[[nodiscard]] std::string   For(RE::Actor* a_actor) const;
		[[nodiscard]] std::uint32_t    ColourFor(RE::Actor* a_actor) const;

		// A title you typed in yourself, which beats every rule. Stored by form
		// ID and written back to the rule file, so it survives a restart.
		void Assign(RE::Actor* a_actor, std::string a_title);
		void Unassign(RE::FormID a_form);
		bool SaveAssignments() const;

		struct Assignment
		{
			RE::FormID  form{ 0 };
			std::string title;
			std::string who;  // the name at the time, so the list is readable
		};

		[[nodiscard]] const std::vector<Assignment>& Assignments() const { return _assigned; }
		[[nodiscard]] std::vector<Rule>&             Rules() { return _rules; }
		[[nodiscard]] const std::string&             Status() const { return _status; }

	private:
		Titles() = default;

		[[nodiscard]] int Match(RE::Actor* a_actor) const;

		std::vector<Rule>       _rules;
		std::vector<Assignment> _assigned;
		std::string             _status{ "not loaded" };
	};
}
