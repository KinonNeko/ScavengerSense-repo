#pragma once

#include "Config.h"

namespace SS
{
	// User-assignable markers on name tags.
	//
	// The built-in shapes answer "is this person going to attack me". This
	// answers anything else you care to ask, because the question is yours: a
	// rule file matches actors on keywords, factions, form lists or names, and
	// hands back a colour and a picture. Drop a PNG in the icons folder, name it
	// in a rule, and it appears over whoever matches.
	//
	// The obvious use is devotion - a Dibella sigil over the people you have
	// been with, a Mara one over your spouse - but nothing here knows about any
	// of that. It knows how to match an actor and how to draw a picture.
	class Marks
	{
	public:
		struct Rule
		{
			std::string   name;
			std::string   icon;      // file in the icons folder, empty = use the shape
			std::string   fullIcon;  // shown instead once the tally is full
			std::uint32_t colour{ 0xFFFFFF };
			int           priority{ 0 };  // higher wins when several match

			// Any one of these matching is enough.
			std::vector<RE::BGSKeyword*>  keywords;
			std::vector<RE::TESFaction*>  factions;
			std::vector<RE::BGSListForm*> lists;
			std::vector<RE::TESForm*>     bases;
			std::vector<std::string>      nameContains;

			// An optional counter shown beside the icon. OStim and friends keep
			// per-NPC tallies as faction ranks, so this is a faction whose rank
			// on the actor is the number.
			RE::TESFaction* countFaction{ nullptr };

			// Or count how many groups of form lists the actor appears in. Each
			// `countLists =` line is one group worth one point, so several form
			// lists that mean the same thing - the two halves of a symmetric
			// act, say - count once between them rather than twice.
			//
			// This exists because the obvious counter did not work. OStim keeps
			// per-NPC tallies as faction ranks, but its GameFaction::getRank and
			// setRank read TESNPC::factions - the static array from the record -
			// while add() writes ExtraFactionChanges on the reference. So the
			// first event adds rank 1, every later one reads -2 from an array
			// that will never contain the faction, and the rank is pinned at 1
			// forever. Every one of its 638 stat factions is a boolean wearing a
			// number's clothes. Its form lists, by contrast, check and insert the
			// same thing, so membership is sound - and counting how many of them
			// someone is in gives a real, monotonic figure.
			// One group per `countLists =` line. The label is that line's
			// trailing comment, which is what makes a count explainable: when
			// someone asks why the number is 4, we can name the four.
			struct CountGroup
			{
				std::string                   label;
				std::vector<RE::BGSListForm*> lists;
			};

			std::vector<CountGroup> countGroups;

			int             countMin{ 1 };    // hide the number below this
			int             countCap{ 101 };  // shown as "101+" at the ceiling

			// Some lists hold base forms shared by every member of a generic
			// type - flag one bandit and you have flagged all of them. On by
			// default for that reason.
			bool uniqueOnly{ true };

			// Rules that exist to read another mod's data belong to that mod,
			// not to us. Naming the integration groups them under one switch in
			// the menu, and naming the file it needs lets us say "OStim is not
			// installed" instead of silently matching nobody.
			std::string integration;   // integration = OStim
			std::string requiresFile;  // requires = OStim.esp
			bool        available{ true };

			// Resolved lazily on the render thread, where the device lives.
			// -1 not tried, -2 tried and failed.
			int texture{ -1 };
			int fullTexture{ -1 };
		};

		[[nodiscard]] static Marks* GetSingleton();

		// Parse the rule file and resolve every form it names. Main thread,
		// after data load. Safe to call again to reload.
		void Load();

		// Which rule applies to this actor, or -1. Main thread.
		[[nodiscard]] int Match(RE::Actor* a_actor);

		[[nodiscard]] std::uint32_t Colour(int a_rule) const;

		// The counter for this rule on this actor, or -1 if the rule has no
		// counter or the actor is not counted. Main thread.
		[[nodiscard]] int Count(int a_rule, RE::Actor* a_actor) const;

		// A human-readable account of how Count reached its answer for this
		// actor: every group, whether it matched, and what it was called. The
		// counters read another mod's bookkeeping, so when a number looks wrong
		// the useful question is which of the two is not recording - and that
		// cannot be answered without seeing the parts.
		[[nodiscard]] std::string Explain(int a_rule, RE::Actor* a_actor) const;

		// Names of every integration mentioned by the rule file, in load order.
		[[nodiscard]] const std::vector<std::string>& Integrations() const { return _integrations; }

		// Whether the plugin an integration needs is actually in the load order.
		[[nodiscard]] bool IntegrationAvailable(std::string_view a_name) const;

		[[nodiscard]] const std::vector<Rule>& Rules() const { return _rules; }

		// The loaded texture for a rule, or nullptr if it has none or the file
		// would not load. Render thread only - it creates D3D resources.
		[[nodiscard]] void* Texture(int a_rule, bool a_full = false);

		// Register a bridge that lives in code rather than in the rule file, so
		// it appears on the Add-ons page beside the ones the rules declare.
		// a_available: 1 present, 0 missing, -1 nothing to detect (an add-on
		// that adjusts our own behaviour rather than reading someone's data).
		void DeclareIntegration(std::string a_name, int a_available);

		// -1 when there is nothing to look for, so the menu can leave the
		// installed/not-installed badge off rather than guessing.
		[[nodiscard]] int IntegrationPresence(std::string_view a_name) const;

		[[nodiscard]] std::size_t RuleCount() const { return _rules.size(); }
		[[nodiscard]] const std::string& Status() const { return _status; }

	private:
		Marks() = default;

		// Whether a rule should act at all: its mod is present and its
		// integration switch is on.
		[[nodiscard]] static bool Live(const Rule& a_rule);

		std::vector<Rule>        _rules;
		std::vector<std::string> _integrations;
		std::vector<void*>       _fullTextures;

		// Bridges declared from code. Kept apart from the rule-file ones because
		// reloading the rules must not forget them.
		std::vector<std::pair<std::string, int>> _declared;

		void MergeDeclared();
		std::vector<void*> _textures;  // ID3D11ShaderResourceView*, parallel to _rules
		std::string        _status{ "not loaded" };
	};
}
