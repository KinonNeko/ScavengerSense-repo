#include "Settings.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace SSC
{
	namespace {
		std::string Trim(std::string value) {
			const auto start=value.find_first_not_of(" \t\r\n"), end=value.find_last_not_of(" \t\r\n");
			return start==std::string::npos ? "" : value.substr(start,end-start+1);
		}
		std::string Lower(std::string value) {
			std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return value;
		}
	}
	Ini ReadIni(const std::filesystem::path& path)
	{
		std::ifstream input(path,std::ios::binary); Ini result; std::string line,section;
		bool first=true;
		while (std::getline(input,line)) {
			if (first && line.starts_with("\xEF\xBB\xBF")) { line.erase(0,3); } first=false;
			line=Trim(line);
			if (line.empty() || line[0]==';' || line[0]=='#') { continue; }
			if (line.front()=='[' && line.back()==']') { section=Lower(Trim(line.substr(1,line.size()-2))); continue; }
			const auto eq=line.find('='); if (eq==std::string::npos) { continue; }
			auto key=Trim(line.substr(0,eq));
			if (section!="translation") { key=Lower(key); }
			result[section][key]=Trim(line.substr(eq+1));
		}
		return result;
	}
	std::string LanguageID(std::string value)
	{
		value=Lower(Trim(value));
		if (value.empty() || value.size()>48 || !std::all_of(value.begin(),value.end(),[](unsigned char c){
			return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-'; })) { return "english"; }
		return value;
	}
	void Settings::Load(const std::filesystem::path& path)
	{
		*this=Settings{};
		auto ini=ReadIni(path); const auto& general=ini["general"]; const auto& hotkey=ini["hotkey"];
		const auto boolean=[](const auto& section,const char* key,bool& value) {
			if (const auto it=section.find(key);it!=section.end()) {
				const auto text=Lower(it->second);
				if (text=="true" || text=="1") { value=true; } else if (text=="false" || text=="0") { value=false; }
			}
		};
		boolean(general,"enabled",enabled); boolean(hotkey,"ctrl",ctrl); boolean(hotkey,"shift",shift); boolean(hotkey,"alt",alt);
		if (const auto it=general.find("directory");it!=general.end() && !it->second.empty() && it->second.size()<1024) { directory=it->second; }
		if (const auto it=general.find("language");it!=general.end()) { language=LanguageID(it->second); }
		if (const auto it=hotkey.find("key");it!=hotkey.end()) {
			try { std::size_t used{}; const auto number=std::stoi(it->second,&used,0);
				if (used==it->second.size() && number>=0 && number<=255) { key=number; }
			} catch (...) {}
		}
	}
	void Settings::Save(const std::filesystem::path& path) const
	{
		if (directory.empty() || directory.size()>=1024 || directory.find_first_of("\r\n")!=std::string::npos) { throw std::runtime_error("Invalid screenshot directory"); }
		if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
		// Write separately before replacement, so a failed write preserves the old INI.
		auto temp=path; temp += ".tmp";
		std::ofstream output(temp,std::ios::binary|std::ios::trunc);
		output << "; Optional screenshot add-on. UTF-8. Key uses Windows VK, not SKSE scan codes.\n[General]\nenabled=" << (enabled?"true":"false")
			<< "\nlanguage=" << LanguageID(language) << "\ndirectory=" << directory
			<< "\n[Hotkey]\nkey=" << std::clamp(key,0,255) << "\nctrl=" << (ctrl?"true":"false") << "\nshift=" << (shift?"true":"false") << "\nalt=" << (alt?"true":"false") << '\n';
		output.flush(); if (!output) { throw std::runtime_error("Could not save capture settings"); } output.close();
		if (!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { throw std::runtime_error("Could not replace capture settings"); }
	}
}
