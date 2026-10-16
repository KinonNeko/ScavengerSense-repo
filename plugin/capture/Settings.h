#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace SSC
{
	using Ini = std::map<std::string,std::map<std::string,std::string>>;
	Ini ReadIni(const std::filesystem::path& path);
	std::string LanguageID(std::string value);
	struct Settings
	{
		bool enabled{true}, ctrl{true}, shift{true}, alt{false};
		int key{0x79};  // Windows VK_F10, not an SKSE scan code
		std::string directory{"Screenshots/ScavengerSense"};
		std::string language{"auto"};
		void Load(const std::filesystem::path& path);
		void Save(const std::filesystem::path& path) const;
	};
}
