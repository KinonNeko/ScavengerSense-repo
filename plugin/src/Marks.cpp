#include "Marks.h"

#include <d3d11.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <wincodec.h>

namespace SS
{
	namespace
	{
		constexpr auto kRulesPath = "Data/SKSE/Plugins/ScavengerSense_marks.ini";
		constexpr auto kIconDir = "Data/SKSE/Plugins/ScavengerSense/icons/";
		constexpr auto kPluginDir = "Data/SKSE/Plugins";

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

		// "Skyrim.esm|0x0001A696" - the only sane way to name a form in a text
		// file, because a raw runtime FormID depends on load order and an editor
		// ID is not available for most records at runtime.
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

			auto* handler = RE::TESDataHandler::GetSingleton();
			return handler ? handler->LookupForm(local, file) : nullptr;
		}

		// Comma separated values, so one key can name several things.
		[[nodiscard]] std::vector<std::string> Split(std::string_view a_in)
		{
			std::vector<std::string> out;
			std::size_t              start = 0;
			while (start <= a_in.size()) {
				const auto comma = a_in.find(',', start);
				const auto piece = Trim(a_in.substr(start, comma == std::string_view::npos
															   ? std::string_view::npos
															   : comma - start));
				if (!piece.empty()) {
					out.push_back(piece);
				}
				if (comma == std::string_view::npos) {
					break;
				}
				start = comma + 1;
			}
			return out;
		}

		// Decode a PNG into a texture. WIC is part of Windows, so this needs no
		// third party image library and no shipped dependency.
		[[nodiscard]] void* LoadTexture(const std::string& a_path)
		{
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			auto* device = renderer ? reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder) : nullptr;
			if (!device) {
				return nullptr;
			}

			// The render thread is already in a COM apartment; asking again is
			// harmless and returns RPC_E_CHANGED_MODE, which we ignore.
			::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

			IWICImagingFactory* factory = nullptr;
			if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(&factory))) ||
				!factory) {
				return nullptr;
			}

			const std::wstring wide(a_path.begin(), a_path.end());

			IWICBitmapDecoder*     decoder = nullptr;
			IWICBitmapFrameDecode* frame = nullptr;
			IWICFormatConverter*   converter = nullptr;
			void*                  result = nullptr;

			if (SUCCEEDED(factory->CreateDecoderFromFilename(
					wide.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
				SUCCEEDED(decoder->GetFrame(0, &frame)) &&
				SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
				SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
					WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
				UINT width = 0;
				UINT height = 0;
				if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0 &&
					width <= 4096 && height <= 4096) {
					std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
					if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4,
							static_cast<UINT>(pixels.size()), pixels.data()))) {
						D3D11_TEXTURE2D_DESC desc{};
						desc.Width = width;
						desc.Height = height;
						desc.MipLevels = 1;
						desc.ArraySize = 1;
						desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
						desc.SampleDesc.Count = 1;
						desc.Usage = D3D11_USAGE_IMMUTABLE;
						desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

						D3D11_SUBRESOURCE_DATA initial{};
						initial.pSysMem = pixels.data();
						initial.SysMemPitch = width * 4;

						ID3D11Texture2D* texture = nullptr;
						if (SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)) && texture) {
							ID3D11ShaderResourceView* srv = nullptr;
							if (SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &srv))) {
								result = srv;
							}
							texture->Release();  // the view keeps it alive
						}
					}
				}
			}

			if (converter) {
				converter->Release();
			}
			if (frame) {
				frame->Release();
			}
			if (decoder) {
				decoder->Release();
			}
			factory->Release();

			return result;
		}
	}

	Marks* Marks::GetSingleton()
	{
		static Marks singleton;
		return std::addressof(singleton);
	}

	void Marks::Load()
	{
		_rules.clear();
		_textures.clear();

		// The base file, plus any ScavengerSense_marks_*.ini beside it. Splitting
		// them is what lets an add-on be left out of an install: no file, no
		// rules, nothing in the menu, and nothing to switch off.
		std::vector<std::string> files;
		{
			std::error_code ec;
			if (std::filesystem::exists(kRulesPath, ec)) {
				files.emplace_back(kRulesPath);
			}
			for (const auto& entry : std::filesystem::directory_iterator{ kPluginDir, ec }) {
				if (!entry.is_regular_file(ec)) {
					continue;
				}
				const auto name = entry.path().filename().string();
				if (name.starts_with("ScavengerSense_marks_") && entry.path().extension() == ".ini") {
					files.push_back(entry.path().string());
				}
			}
			std::sort(files.begin(), files.end());
		}

		if (files.empty()) {
			_status = "no rule file";
			logger::info("marks: no {} - custom markers off", kRulesPath);
			return;
		}

		Rule        current;
		bool        inSection = false;
		std::size_t unresolved = 0;

		const auto flush = [&] {
			if (!inSection) {
				return;
			}
			const bool matchesSomething = !current.keywords.empty() || !current.factions.empty() ||
			                              !current.lists.empty() || !current.bases.empty() ||
			                              !current.nameContains.empty();
			// A rule that names a plugin nobody has installed resolves none of
			// its forms and so matches nothing - but dropping it here is what
			// made the OStim add-on disappear from the Add-ons page entirely,
			// rather than appear there saying OStim is not installed. Keep it:
			// the availability pass marks it inert and Match() skips it.
			if (matchesSomething || !current.requiresFile.empty()) {
				_rules.push_back(current);
			} else {
				logger::warn("marks: rule [{}] matches nothing and requires nothing, "
					"skipped", current.name);
			}
			current = Rule{};
		};

		for (const auto& path : files) {
		std::ifstream file{ path };
		if (!file) {
			continue;
		}
		logger::info("marks: reading {}", path);

		std::string line;
		while (std::getline(file, line)) {
			std::string comment;
			if (const auto cut = line.find_first_of(";#"); cut != std::string::npos) {
				comment = Trim(line.substr(cut + 1));
				line.erase(cut);
			}
			auto trimmed = Trim(line);
			if (trimmed.empty()) {
				continue;
			}

			if (trimmed.front() == '[') {
				flush();
				const auto close = trimmed.find(']');
				current.name = close == std::string::npos ? trimmed.substr(1) : trimmed.substr(1, close - 1);
				inSection = true;
				continue;
			}

			const auto eq = trimmed.find('=');
			if (eq == std::string::npos || !inSection) {
				continue;
			}

			auto key = Trim(trimmed.substr(0, eq));
			auto value = Trim(trimmed.substr(eq + 1));
			for (auto& c : key) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}

			if (key == "iconfull") {
				current.fullIcon = value;
			} else if (key == "integration") {
				current.integration = value;
			} else if (key == "requires") {
				current.requiresFile = value;
			} else if (key == "icon") {
				current.icon = value;
			} else if (key == "color" || key == "colour") {
				current.colour = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
			} else if (key == "priority") {
				current.priority = static_cast<int>(std::strtol(value.c_str(), nullptr, 0));
			} else if (key == "countmin") {
				current.countMin = static_cast<int>(std::strtol(value.c_str(), nullptr, 0));
			} else if (key == "countcap") {
				current.countCap = static_cast<int>(std::strtol(value.c_str(), nullptr, 0));
			} else if (key == "uniqueonly") {
				current.uniqueOnly = value == "true" || value == "1" || value == "yes";
			} else if (key == "countlists") {
				Rule::CountGroup group;
				group.label = comment.empty()
				                  ? std::format("group {}", current.countGroups.size() + 1)
				                  : comment;
				for (const auto& spec : Split(value)) {
					if (auto* form = LookupForm(spec); form) {
						if (auto* list = form->As<RE::BGSListForm>(); list) {
							group.lists.push_back(list);
						}
					} else {
						++unresolved;
					}
				}
				if (!group.lists.empty()) {
					current.countGroups.push_back(std::move(group));
				}
			} else if (key == "namecontains") {
				for (auto& piece : Split(value)) {
					current.nameContains.push_back(std::move(piece));
				}
			} else {
				// Everything else names forms. A form that will not resolve is
				// almost always a mod that is not installed, which is a normal
				// state for a rule file shared between setups - note it and
				// carry on rather than discarding the rule.
				for (const auto& spec : Split(value)) {
					auto* form = LookupForm(spec);
					if (!form) {
						++unresolved;
						continue;
					}

					if (key == "keyword") {
						if (auto* kw = form->As<RE::BGSKeyword>(); kw) {
							current.keywords.push_back(kw);
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
					} else if (key == "countfaction") {
						if (auto* faction = form->As<RE::TESFaction>(); faction) {
							current.countFaction = faction;
						}
					}
				}
			}
		}
		flush();
		inSection = false;
		}

		// Highest priority first, so Match can return on the first hit.
		std::stable_sort(_rules.begin(), _rules.end(), [](const Rule& a_lhs, const Rule& a_rhs) {
			return a_lhs.priority > a_rhs.priority;
		});

		// Is the mod each integration reads actually here? Answering this once,
		// by name, is what lets the menu say "OStim is not installed" rather
		// than showing a rule that quietly matches nobody forever.
		auto* handler = RE::TESDataHandler::GetSingleton();
		_integrations.clear();
		for (auto& rule : _rules) {
			if (!rule.requiresFile.empty()) {
				rule.available = handler && (handler->LookupModByName(rule.requiresFile) ||
												handler->LookupLoadedLightModByName(rule.requiresFile));
			}
			if (!rule.integration.empty() &&
				std::find(_integrations.begin(), _integrations.end(), rule.integration) == _integrations.end()) {
				_integrations.push_back(rule.integration);
			}
		}

		MergeDeclared();

		_textures.assign(_rules.size(), nullptr);
		_byFile.clear();
		_fullTextures.assign(_rules.size(), nullptr);
		_status = std::format("{} rules, {} form references did not resolve", _rules.size(), unresolved);
		logger::info("marks: {}", _status);

		// Twice now somebody has reported an add-on missing from the menu
		// with nothing in the log to say why. Name every one and whether it
		// counts as present, since that is exactly what the page filters on.
		for (const auto& name : _integrations) {
			logger::info("  add-on '{}': presence {} ({})", name,
				IntegrationPresence(name),
				IntegrationPresence(name) == 0 ? "hidden unless you untick 'hide missing'"
				                               : "should be listed");
		}
		for (const auto& rule : _rules) {
			logger::info("  [{}] priority {} icon '{}' - {} keywords, {} factions, {} lists, {} forms, {} names, counter {}",
				rule.name, rule.priority, rule.icon, rule.keywords.size(), rule.factions.size(),
				rule.lists.size(), rule.bases.size(), rule.nameContains.size(),
				rule.countGroups.empty() ? (rule.countFaction ? "faction rank" : "none")
										 : std::format("{} list groups", rule.countGroups.size()));
		}
	}

	int Marks::Match(RE::Actor* a_actor)
	{
		if (_rules.empty() || !a_actor) {
			return -1;
		}

		auto* base = a_actor->GetActorBase();

		for (std::size_t i = 0; i < _rules.size(); ++i) {
			const auto& rule = _rules[i];

			if (!Live(rule)) {
				continue;
			}

			// Generic NPCs share one base form, so anything recorded against
			// the base is really recorded against the whole species. Refusing
			// to mark them is the difference between "the woman you know" and
			// "every bandit in Skyrim".
			if (rule.uniqueOnly && base && !base->IsUnique()) {
				continue;
			}

			for (auto* keyword : rule.keywords) {
				if (keyword && a_actor->HasKeyword(keyword)) {
					return static_cast<int>(i);
				}
			}

			// Actor rather than the base: the base's faction array misses ranks
			// added to this particular reference at runtime, which is exactly
			// how mods record what someone has done.
			// Read the faction list rather than asking the engine. Actor::IsInFaction
			// is a virtual call into the game, and it is what leaves an actor in a
			// state where More Informative Console dies enumerating that same
			// actor's factions. Titles had the identical call and the identical
			// problem; VisitFactions is plain C++ and answers the same question.
			for (auto* faction : rule.factions) {
				if (!faction) {
					continue;
				}
				bool matched = false;
				a_actor->VisitFactions([&](RE::TESFaction* a_it, std::int8_t a_rank) {
					if (a_it == faction) {
						matched = a_rank >= 0;
						return true;  // stop
					}
					return false;
				});
				if (matched) {
					return static_cast<int>(i);
				}
			}

			for (auto* list : rule.lists) {
				if (!list) {
					continue;
				}
				if ((base && list->HasForm(base)) || list->HasForm(a_actor)) {
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
				if (name && name[0] != '\0') {
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

	bool Marks::Live(const Rule& a_rule)
	{
		if (!a_rule.available) {
			return false;
		}
		if (a_rule.integration.empty()) {
			return true;
		}
		return Settings::GetSingleton()->IntegrationEnabled(a_rule.integration);
	}

	void Marks::MergeDeclared()
	{
		for (const auto& [name, available] : _declared) {
			if (std::find(_integrations.begin(), _integrations.end(), name) == _integrations.end()) {
				_integrations.push_back(name);
			}
		}
	}

	int Marks::IntegrationPresence(std::string_view a_name) const
	{
		for (const auto& [name, available] : _declared) {
			if (name == a_name) {
				return available;
			}
		}
		return IntegrationAvailable(a_name) ? 1 : 0;
	}

	void Marks::DeclareIntegration(std::string a_name, int a_available)
	{
		for (auto& entry : _declared) {
			if (entry.first == a_name) {
				entry.second = a_available;
				MergeDeclared();
				return;
			}
		}
		_declared.emplace_back(std::move(a_name), a_available);
		MergeDeclared();
	}

	bool Marks::IntegrationAvailable(std::string_view a_name) const
	{
		// A bridge declared from code answers for itself; it has no rules.
		// -1 means there is nothing to look for, which is not the same as
		// missing, so it counts as present.
		for (const auto& [name, available] : _declared) {
			if (name == a_name) {
				return available != 0;
			}
		}

		// An integration is available when every rule that belongs to it found
		// the plugin it named. A rule with no `requires` is always available,
		// which is the right answer for one that reads only vanilla forms.
		for (const auto& rule : _rules) {
			if (rule.integration == a_name && !rule.available) {
				return false;
			}
		}
		return true;
	}

	std::string Marks::Explain(int a_rule, RE::Actor* a_actor) const
	{
		if (a_rule < 0 || static_cast<std::size_t>(a_rule) >= _rules.size() || !a_actor) {
			return "no rule, or nobody to explain it for";
		}

		const auto& rule = _rules[static_cast<std::size_t>(a_rule)];
		auto*       base = a_actor->GetActorBase();
		const auto* name = a_actor->GetDisplayFullName();

		std::string out = std::format("[{}] on {}\n", rule.name, name && name[0] ? name : "(unnamed)");

		if (!rule.available) {
			out += std::format("  {} is not in the load order - rule inert\n", rule.requiresFile);
			return out;
		}
		if (!rule.integration.empty() && !Settings::GetSingleton()->IntegrationEnabled(rule.integration)) {
			out += std::format("  the {} integration is switched off\n", rule.integration);
			return out;
		}
		if (base && !base->IsUnique() && rule.uniqueOnly) {
			out += "  generic NPC and uniqueOnly is on - never marked\n";
			return out;
		}
		if (rule.countGroups.empty()) {
			out += rule.countFaction ? "  counts a faction rank, no groups to list\n"
									 : "  this rule has no counter\n";
			return out;
		}

		int hits = 0;
		for (const auto& group : rule.countGroups) {
			bool matched = false;
			for (auto* list : group.lists) {
				if (list && ((base && list->HasForm(base)) || list->HasForm(a_actor))) {
					matched = true;
					break;
				}
			}
			if (matched) {
				++hits;
			}
			out += std::format("  {} {}\n", matched ? "[x]" : "[ ]", group.label);
		}

		out += std::format("  {} of {} - shown as {}\n", hits, rule.countGroups.size(),
			hits >= rule.countMin ? std::to_string(hits) : std::string{ "nothing" });
		return out;
	}

	int Marks::Count(int a_rule, RE::Actor* a_actor) const
	{
		if (a_rule < 0 || static_cast<std::size_t>(a_rule) >= _rules.size() || !a_actor) {
			return -1;
		}

		const auto& rule = _rules[static_cast<std::size_t>(a_rule)];

		if (!Live(rule)) {
			return -1;
		}

		if (!rule.countGroups.empty()) {
			auto* base = a_actor->GetActorBase();

			int score = 0;
			for (const auto& group : rule.countGroups) {
				// One point per group, however many of its lists match.
				for (auto* list : group.lists) {
					if (list && ((base && list->HasForm(base)) || list->HasForm(a_actor))) {
						++score;
						break;
					}
				}
			}

			return score >= rule.countMin ? score : -1;
		}

		if (!rule.countFaction) {
			return -1;
		}

		// A rank of -1 means "not in the faction", which is how these tallies
		// say the thing never happened. Ranks start at 1, not 0.
		const auto rank = a_actor->GetFactionRank(rule.countFaction, a_actor->IsPlayerRef());
		return rank >= rule.countMin ? rank : -1;
	}

	std::uint32_t Marks::Colour(int a_rule) const
	{
		if (a_rule < 0 || static_cast<std::size_t>(a_rule) >= _rules.size()) {
			return 0xFFFFFF;
		}
		return _rules[a_rule].colour;
	}

	std::string Marks::Snapshot() const
	{
		std::string out;
		for (const auto& rule : _rules) {
			if (rule.lists.empty()) {
				continue;
			}
			out += std::format("[{}] live={} available={}", rule.name, Live(rule), rule.available);
			for (auto* list : rule.lists) {
				if (!list) {
					out += " (unresolved list)";
					continue;
				}
				const auto added = list->scriptAddedTempForms ? list->scriptAddedTempForms->size() : 0u;
				out += std::format(" | list {:08X}: {} static, {} added", list->GetFormID(),
					list->forms.size(), added);
				if (list->scriptAddedTempForms) {
					int shown = 0;
					for (const auto id : *list->scriptAddedTempForms) {
						if (shown++ >= 12) {
							out += " ...";
							break;
						}
						auto*       form = RE::TESForm::LookupByID(id);
						const char* name = form ? form->GetName() : nullptr;
						out += std::format(" {}({:08X})", name && name[0] ? name : "?", id);
					}
				}
			}
			out += "; ";
		}
		return out.empty() ? "no list rules" : out;
	}

	std::string Marks::Why(RE::Actor* a_actor) const
	{
		if (!a_actor) {
			return "nobody";
		}
		auto*       base = a_actor->GetActorBase();
		std::string out = std::format("base {:08X} unique={}", base ? base->GetFormID() : 0u,
			base && base->IsUnique());
		for (const auto& rule : _rules) {
			out += std::format(" | [{}] live={}", rule.name, Live(rule));
			for (auto* list : rule.lists) {
				out += std::format(" list={}", !list ? "null" :
					((base && list->HasForm(base)) || list->HasForm(a_actor)) ? "in" : "out");
			}
			for (auto* faction : rule.factions) {
				std::int8_t rank = -3;
				a_actor->VisitFactions([&](RE::TESFaction* a_it, std::int8_t a_rank) {
					if (a_it == faction) {
						rank = a_rank;
						return true;
					}
					return false;
				});
				out += std::format(" faction={}", rank == -3 ? "absent" : std::to_string(rank));
			}
		}
		return out;
	}

	void* Marks::TextureFile(const std::string& a_file, const std::string& a_rule)
	{
		if (a_file.empty()) {
			return nullptr;
		}
		if (const auto it = _byFile.find(a_file); it != _byFile.end()) {
			return it->second;
		}
		// First time this picture is drawn. Load once and remember the answer
		// either way - a missing file must not retry every frame.
		auto* srv = LoadTexture(kIconDir + a_file);
		if (srv) {
			logger::info("marks: loaded icon '{}' for [{}]", a_file, a_rule);
		} else {
			logger::warn("marks: could not load {}{} for [{}] - falling back",
				kIconDir, a_file, a_rule);
		}
		_byFile[a_file] = srv;
		return srv;
	}

	void* Marks::Texture(int a_rule, bool a_full, const std::string& a_icon)
	{
		if (a_rule < 0 || static_cast<std::size_t>(a_rule) >= _rules.size()) {
			return nullptr;
		}
		const auto& rule = _rules[static_cast<std::size_t>(a_rule)];

		// The menu's choice beats the file's. "heart" means no picture at all:
		// the caller draws the built-in shape when this returns nothing.
		std::string chosen = a_icon;
		for (auto& ch : chosen) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		if (chosen == "heart") {
			return nullptr;
		}

		// A rule with no second picture keeps drawing its first one, which is
		// the right answer for "full" when nobody supplied an alternative.
		if (a_full && !rule.fullIcon.empty()) {
			if (auto* full = TextureFile(rule.fullIcon, rule.name)) {
				return full;
			}
		}
		return TextureFile(a_icon.empty() ? rule.icon : a_icon, rule.name);
	}

	void* Marks::IconTexture(const std::string& a_file)
	{
		return TextureFile(a_file, "stats row");
	}

	std::vector<std::string> Marks::IconFiles()
	{
		std::vector<std::string> files;
		std::error_code          ec;
		for (const auto& entry : std::filesystem::directory_iterator{ kIconDir, ec }) {
			if (!entry.is_regular_file(ec)) {
				continue;
			}
			auto ext = entry.path().extension().string();
			for (auto& ch : ext) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			if (ext == ".png") {
				files.push_back(entry.path().filename().string());
			}
		}
		std::sort(files.begin(), files.end());
		return files;
	}
}
