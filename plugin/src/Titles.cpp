#include "Titles.h"

#include <fstream>

namespace SS
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/ScavengerSense_titles.ini";

		std::string Trim(std::string_view a_in)
		{
			std::size_t b = 0;
			std::size_t e = a_in.size();
			while (b < e && std::isspace(static_cast<unsigned char>(a_in[b]))) {
				++b;
			}
			while (e > b && std::isspace(static_cast<unsigned char>(a_in[e - 1]))) {
				--e;
			}
			return std::string{ a_in.substr(b, e - b) };
		}

		[[nodiscard]] std::vector<std::string> Split(std::string_view a_in)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (start <= a_in.size()) {
				const auto comma = a_in.find(',', start);
				auto       piece = Trim(a_in.substr(
                    start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
				if (!piece.empty()) {
					out.push_back(std::move(piece));
				}
				if (comma == std::string_view::npos) {
					break;
				}
				start = comma + 1;
			}
			return out;
		}

		[[nodiscard]] RE::TESForm* LookupForm(std::string_view a_spec)
		{
			const auto bar = a_spec.find('|');
			if (bar == std::string_view::npos) {
				return nullptr;
			}
			const auto file = Trim(a_spec.substr(0, bar));
			const auto idText = Trim(a_spec.substr(bar + 1));
			if (file.empty() || idText.empty()) {
				return nullptr;
			}
			const auto local = static_cast<RE::FormID>(std::strtoul(idText.c_str(), nullptr, 0));
			auto*      handler = RE::TESDataHandler::GetSingleton();
			return handler ? handler->LookupForm(local, file) : nullptr;
		}

		// Quests are one of the handful of records that keep their editor ID at
		// runtime, so they can be named in the rule file by the name modders
		// actually use. Worth the linear scan: it happens once, at load.
		[[nodiscard]] RE::TESQuest* QuestByEditorID(std::string_view a_editorID)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return nullptr;
			}
			for (auto* quest : handler->GetFormArray<RE::TESQuest>()) {
				if (quest) {
					if (const auto* id = quest->GetFormEditorID(); id && a_editorID == id) {
						return quest;
					}
				}
			}
			return nullptr;
		}

		// Keywords are the other record that keeps its editor ID at runtime -
		// for a keyword the editor ID *is* the string the game matches on. That
		// means a rule can name one the way SPID and KID name it, with no form
		// ID to look up, which is what makes "distribute a keyword, then title
		// on it" workable for somebody who is not a modder.
		[[nodiscard]] RE::BGSKeyword* KeywordByEditorID(std::string_view a_editorID)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return nullptr;
			}
			for (auto* keyword : handler->GetFormArray<RE::BGSKeyword>()) {
				if (keyword) {
					if (const auto* id = keyword->GetFormEditorID(); id && a_editorID == id) {
						return keyword;
					}
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool Truthy(std::string_view a_value)
		{
			return a_value == "true" || a_value == "1" || a_value == "yes";
		}
	}

	Titles* Titles::GetSingleton()
	{
		static Titles singleton;
		return std::addressof(singleton);
	}

	void Titles::Load()
	{
		_rules.clear();
		_assigned.clear();

		std::ifstream file{ kPath };
		if (!file) {
			_status = "no title file";
			logger::info("titles: no {} - titles off", kPath);
			return;
		}

		Rule        current;
		bool        inRule = false;
		bool        inAssigned = false;
		std::size_t unresolved = 0;

		const auto flush = [&] {
			if (!inRule) {
				return;
			}
			const bool matches = !current.factions.empty() || !current.keywords.empty() ||
			                     !current.lists.empty() || !current.bases.empty() ||
			                     !current.nameContains.empty() || !current.questsDone.empty() ||
			                     !current.classes.empty() || current.vendor || current.fromClass;
			if (matches) {
				if (current.text.empty()) {
					current.text = current.name;
				}
				_rules.push_back(current);
			} else {
				logger::warn("titles: [{}] matches nothing, skipped", current.name);
			}
			current = Rule{};
		};

		std::string line;
		while (std::getline(file, line)) {
			if (const auto cut = line.find_first_of(";#"); cut != std::string::npos) {
				line.erase(cut);
			}
			auto trimmed = Trim(line);
			if (trimmed.empty()) {
				continue;
			}

			if (trimmed.front() == '[') {
				flush();
				const auto close = trimmed.find(']');
				const auto name = close == std::string::npos ? trimmed.substr(1)
															 : trimmed.substr(1, close - 1);
				// One reserved section: the titles you assigned by hand.
				inAssigned = name == "Assigned";
				inRule = !inAssigned;
				current.name = inAssigned ? std::string{} : name;
				continue;
			}

			const auto eq = trimmed.find('=');
			if (eq == std::string::npos) {
				continue;
			}

			auto key = Trim(trimmed.substr(0, eq));
			auto value = Trim(trimmed.substr(eq + 1));

			if (inAssigned) {
				// "Skyrim.esm|0x0001A692 = The best blacksmith in Skyrim"
				if (auto* form = LookupForm(key); form && !value.empty()) {
					const auto* name = form->GetName();
					_assigned.push_back(Assignment{ form->GetFormID(), std::move(value),
						name && name[0] ? name : "" });
				} else {
					++unresolved;
				}
				continue;
			}

			if (!inRule) {
				continue;
			}

			for (auto& c : key) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}

			if (key == "text") {
				current.text = value;
			} else if (key == "color" || key == "colour") {
				current.colour = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
			} else if (key == "priority") {
				current.priority = static_cast<int>(std::strtol(value.c_str(), nullptr, 0));
			} else if (key == "minrank") {
				current.minRank = static_cast<std::int8_t>(std::strtol(value.c_str(), nullptr, 0));
			} else if (key == "fromclass") {
				current.fromClass = Truthy(value);
			} else if (key == "vendor") {
				current.vendor = Truthy(value);
			} else if (key == "player") {
				// player = only   this title is about you and nobody else
				// player = false  never about you
				const auto lowered = Trim(value);
				current.forPlayer = lowered != "false" && lowered != "no";
				current.forOthers = lowered != "only";
			} else if (key == "enabled") {
				current.enabled = Truthy(value);
			} else if (key == "namecontains") {
				for (auto& piece : Split(value)) {
					current.nameContains.push_back(std::move(piece));
				}
			} else if (key == "questdone") {
				for (const auto& piece : Split(value)) {
					if (auto* quest = QuestByEditorID(piece); quest) {
						current.questsDone.push_back(quest);
					} else {
						++unresolved;
					}
				}
			} else if (key == "keyword") {
				// Either File.esm|0xFORMID or the keyword's own name. The name
				// is tried first because that is the useful spelling and it
				// cannot be mistaken for a form ID.
				for (const auto& spec : Split(value)) {
					if (auto* kw = KeywordByEditorID(spec); kw) {
						current.keywords.push_back(kw);
						continue;
					}
					auto* form = LookupForm(spec);
					if (auto* kw = form ? form->As<RE::BGSKeyword>() : nullptr; kw) {
						current.keywords.push_back(kw);
					} else {
						++unresolved;
					}
				}
			} else {
				for (const auto& spec : Split(value)) {
					auto* form = LookupForm(spec);
					if (!form) {
						++unresolved;
						continue;
					}
					if (key == "class") {
						if (auto* npcClass = form->As<RE::TESClass>(); npcClass) {
							current.classes.push_back(npcClass);
						}
					} else if (key == "faction") {
						if (auto* faction = form->As<RE::TESFaction>(); faction) {
							current.factions.push_back(faction);
						}
					} else if (key == "formlist" || key == "list") {
						if (auto* list = form->As<RE::BGSListForm>(); list) {
							current.lists.push_back(list);
						}
					} else if (key == "npc" || key == "form") {
						current.bases.push_back(form);
					}
				}
			}
		}
		flush();

		std::stable_sort(_rules.begin(), _rules.end(), [](const Rule& a_lhs, const Rule& a_rhs) {
			return a_lhs.priority > a_rhs.priority;
		});

		_status = std::format("{} titles, {} assigned by hand, {} references did not resolve",
			_rules.size(), _assigned.size(), unresolved);
		logger::info("titles: {}", _status);
	}

	int Titles::Match(RE::Actor* a_actor) const
	{
		if (!a_actor) {
			return -1;
		}

		const bool isPlayer = a_actor->IsPlayerRef();
		auto*      base = a_actor->GetActorBase();

		for (std::size_t i = 0; i < _rules.size(); ++i) {
			const auto& rule = _rules[i];
			if (!rule.enabled || (isPlayer ? !rule.forPlayer : !rule.forOthers)) {
				continue;
			}

			for (auto* faction : rule.factions) {
				if (faction && a_actor->IsInFaction(faction) &&
					a_actor->GetFactionRank(faction, isPlayer) >= rule.minRank) {
					return static_cast<int>(i);
				}
			}

			for (auto* quest : rule.questsDone) {
				if (quest && quest->IsCompleted()) {
					return static_cast<int>(i);
				}
			}

			// The engine's own vendor flag, read off whatever factions this
			// person happens to be in. Catches every modded shopkeeper, which no
			// list of vanilla faction IDs ever could.
			if (rule.vendor) {
				bool found = false;
				a_actor->VisitFactions([&](RE::TESFaction* a_faction, std::int8_t a_rank) {
					if (a_faction && a_rank >= 0 && a_faction->IsVendor()) {
						found = true;
						return true;  // stop
					}
					return false;
				});
				if (found) {
					return static_cast<int>(i);
				}
			}

			for (auto* keyword : rule.keywords) {
				if (keyword && a_actor->HasKeyword(keyword)) {
					return static_cast<int>(i);
				}
			}

			for (auto* npcClass : rule.classes) {
				if (npcClass && base && base->npcClass == npcClass) {
					return static_cast<int>(i);
				}
			}

			// Only matches when the class actually has a name to show. Plenty
			// do not, and "" over somebody's head is worse than nothing.
			if (rule.fromClass && base && base->npcClass) {
				const auto* named = base->npcClass->GetName();
				if (named && named[0]) {
					return static_cast<int>(i);
				}
			}

			for (auto* list : rule.lists) {
				if (list && ((base && list->HasForm(base)) || list->HasForm(a_actor))) {
					return static_cast<int>(i);
				}
			}

			for (auto* form : rule.bases) {
				if (form && (form == base || form == a_actor->GetBaseObject())) {
					return static_cast<int>(i);
				}
			}

			if (!rule.nameContains.empty()) {
				const auto* name = a_actor->GetDisplayFullName();
				if (name && name[0]) {
					const std::string_view view{ name };
					for (const auto& needle : rule.nameContains) {
						if (view.find(needle) != std::string_view::npos) {
							return static_cast<int>(i);
						}
					}
				}
			}
		}

		return -1;
	}

	std::string Titles::For(RE::Actor* a_actor) const
	{
		if (!a_actor) {
			return {};
		}

		// A title you typed in yourself beats anything worked out from the game.
		// Matched on the base as well as the reference, so a title given to a
		// unique NPC survives them being moved or resurrected.
		const auto form = a_actor->GetFormID();
		const auto base = a_actor->GetActorBase() ? a_actor->GetActorBase()->GetFormID() : 0;
		for (const auto& entry : _assigned) {
			if (entry.form == form || (base != 0 && entry.form == base)) {
				return entry.title;
			}
		}

		const auto index = Match(a_actor);
		if (index < 0) {
			return {};
		}

		const auto& rule = _rules[static_cast<std::size_t>(index)];
		if (rule.fromClass) {
			auto* npcBase = a_actor->GetActorBase();
			auto* npcClass = npcBase ? npcBase->npcClass : nullptr;
			const auto* named = npcClass ? npcClass->GetName() : nullptr;
			if (named && named[0]) {
				return named;
			}
		}

		return rule.text;
	}

	std::uint32_t Titles::ColourFor(RE::Actor* a_actor) const
	{
		const auto index = Match(a_actor);
		return index < 0 ? 0xC9C9C9 : _rules[index].colour;
	}

	void Titles::Assign(RE::Actor* a_actor, std::string a_title)
	{
		if (!a_actor) {
			return;
		}

		// Against the base form, not the reference: that is what survives the
		// game rebuilding a reference, and it is what a unique NPC actually is.
		auto* base = a_actor->GetActorBase();
		const auto form = base ? base->GetFormID() : a_actor->GetFormID();

		const auto* name = a_actor->GetDisplayFullName();
		const std::string who = name && name[0] ? name : "";

		for (auto& entry : _assigned) {
			if (entry.form == form) {
				entry.title = std::move(a_title);
				entry.who = who;
				return;
			}
		}

		_assigned.push_back(Assignment{ form, std::move(a_title), who });
	}

	void Titles::Unassign(RE::FormID a_form)
	{
		std::erase_if(_assigned, [a_form](const Assignment& a_entry) { return a_entry.form == a_form; });
	}

	bool Titles::SaveAssignments() const
	{
		// Rewrites only the [Assigned] block, keeping everything above it
		// exactly as it was - the rules are hand written and commented, and a
		// menu has no business reformatting them.
		std::string kept;
		{
			std::ifstream in{ kPath };
			if (in) {
				std::string line;
				while (std::getline(in, line)) {
					if (Trim(line) == "[Assigned]") {
						break;
					}
					kept += line;
					kept += '\n';
				}
			}
		}

		std::ofstream out{ kPath, std::ios::trunc };
		if (!out) {
			logger::error("titles: could not write {}", kPath);
			return false;
		}

		out << kept;
		out << "[Assigned]\n\n";
		out << "; Titles you typed in yourself. These beat every rule above.\n";
		out << "; Written by the menu; the rules above are never touched.\n\n";
		for (const auto& entry : _assigned) {
			auto* form = RE::TESForm::LookupByID(entry.form);
			auto* file = form ? form->GetFile(0) : nullptr;
			if (!file) {
				continue;
			}
			// Back to File|LocalID, which is what survives a load order change.
			const auto local = entry.form & (file->IsLight() ? 0xFFFu : 0xFFFFFFu);
			out << file->GetFilename() << "|0x" << std::hex << std::uppercase << local
				<< std::dec << std::nouppercase << " = " << entry.title;
			if (!entry.who.empty()) {
				out << "   ; " << entry.who;
			}
			out << "\n";
		}

		logger::info("titles: wrote {} assignments to {}", _assigned.size(), kPath);
		return true;
	}
}
