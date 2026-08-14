#include "Locale.h"

#include <Windows.h>

#include <fstream>
#include <unordered_map>

namespace SS
{
	namespace
	{
		std::unordered_map<std::string, std::string> g_strings;

		// Strict UTF-8 validation. Anything that fails is assumed to be in the
		// system code page - which for a Chinese install means GBK.
		bool IsUtf8(const char* a_text, std::size_t a_length)
		{
			std::size_t i = 0;
			while (i < a_length) {
				const auto c = static_cast<unsigned char>(a_text[i]);
				std::size_t extra = 0;

				if (c < 0x80) {
					extra = 0;
				} else if ((c & 0xE0) == 0xC0) {
					extra = 1;
				} else if ((c & 0xF0) == 0xE0) {
					extra = 2;
				} else if ((c & 0xF8) == 0xF0) {
					extra = 3;
				} else {
					return false;
				}

				if (i + extra >= a_length) {
					return false;
				}
				for (std::size_t k = 1; k <= extra; ++k) {
					if ((static_cast<unsigned char>(a_text[i + k]) & 0xC0) != 0x80) {
						return false;
					}
				}
				i += extra + 1;
			}
			return true;
		}

		// Tooltips are multi-line, and an INI line cannot hold a real newline, so
		// both sides of the pair use a literal backslash-n.
		std::string Unescape(std::string a_in)
		{
			std::string out;
			out.reserve(a_in.size());
			for (std::size_t i = 0; i < a_in.size(); ++i) {
				if (a_in[i] == '\\' && i + 1 < a_in.size()) {
					switch (a_in[i + 1]) {
					case 'n':
						out.push_back('\n');
						++i;
						continue;
					case 't':
						out.push_back('\t');
						++i;
						continue;
					case '=':
						out.push_back('=');
						++i;
						continue;
					case '\\':
						out.push_back('\\');
						++i;
						continue;
					default:
						break;
					}
				}
				out.push_back(a_in[i]);
			}
			return out;
		}

		std::string Trim(std::string_view a_in)
		{
			std::size_t b = 0;
			std::size_t e = a_in.size();
			while (b < e && (a_in[b] == ' ' || a_in[b] == '\t' || a_in[b] == '\r')) {
				++b;
			}
			while (e > b && (a_in[e - 1] == ' ' || a_in[e - 1] == '\t' || a_in[e - 1] == '\r')) {
				--e;
			}
			return std::string{ a_in.substr(b, e - b) };
		}
	}

	std::string ToUtf8(const char* a_text)
	{
		if (!a_text || a_text[0] == '\0') {
			return {};
		}

		const std::size_t length = std::strlen(a_text);
		if (IsUtf8(a_text, length)) {
			return std::string{ a_text, length };
		}

		const int wide = MultiByteToWideChar(CP_ACP, 0, a_text, static_cast<int>(length), nullptr, 0);
		if (wide <= 0) {
			return std::string{ a_text, length };
		}

		std::wstring buffer(static_cast<std::size_t>(wide), L'\0');
		MultiByteToWideChar(CP_ACP, 0, a_text, static_cast<int>(length), buffer.data(), wide);

		const int utf8 = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, nullptr, 0, nullptr, nullptr);
		if (utf8 <= 0) {
			return std::string{ a_text, length };
		}

		std::string out(static_cast<std::size_t>(utf8), '\0');
		WideCharToMultiByte(CP_UTF8, 0, buffer.data(), wide, out.data(), utf8, nullptr, nullptr);
		return out;
	}

	void Locale::Load(std::string_view a_language)
	{
		g_strings.clear();

		if (a_language.empty() || a_language == "english") {
			logger::info("locale: english (no translation file needed)");
			return;
		}

		const std::string path =
			std::string{ "Data/SKSE/Plugins/ScavengerSense_" } + std::string{ a_language } + ".ini";

		std::ifstream file{ path };
		if (!file) {
			logger::warn("locale: {} not found - falling back to english", path);
			return;
		}

		std::string line;
		while (std::getline(file, line)) {
			// Strip a UTF-8 byte order mark on the first line.
			if (line.size() >= 3 &&
				static_cast<unsigned char>(line[0]) == 0xEF &&
				static_cast<unsigned char>(line[1]) == 0xBB &&
				static_cast<unsigned char>(line[2]) == 0xBF) {
				line.erase(0, 3);
			}

			if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') {
				continue;
			}

			// Several English strings contain an equals sign, so split on the
			// first UNescaped one; a key writes its own as \= .
			std::size_t eq = std::string::npos;
			for (std::size_t i = 0; i < line.size(); ++i) {
				if (line[i] == '\\') {
					++i;
					continue;
				}
				if (line[i] == '=') {
					eq = i;
					break;
				}
			}
			if (eq == std::string::npos) {
				continue;
			}

			auto key = Unescape(Trim(line.substr(0, eq)));
			auto value = Unescape(Trim(line.substr(eq + 1)));
			if (!key.empty() && !value.empty()) {
				g_strings.emplace(std::move(key), std::move(value));
			}
		}

		logger::info("locale: loaded {} strings from {}", g_strings.size(), path);
	}

	const char* Locale::T(const char* a_english)
	{
		if (!a_english || g_strings.empty()) {
			return a_english;
		}

		const auto it = g_strings.find(a_english);
		return it != g_strings.end() ? it->second.c_str() : a_english;
	}

	std::size_t Locale::Count()
	{
		return g_strings.size();
	}
}
