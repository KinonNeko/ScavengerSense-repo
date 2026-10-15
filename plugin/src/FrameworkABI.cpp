#include "FrameworkABI.h"
#include "ImGuiCompat.h"

namespace SS
{
	bool CheckFrameworkABI()
	{
		const char* version = igGetVersion();
		logger::info("framework ABI: host ImGui {}, headers {}; IO={}, style={}, vertex={}, index={}",
			version ? version : "unknown", IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle),
			sizeof(ImDrawVert), sizeof(ImDrawIdx));
		if (!version || std::strcmp(version, IMGUI_VERSION) != 0) {
			logger::error("framework ABI: ImGui version mismatch; plugin will not register callbacks");
			return false;
		}
		if (!igDebugCheckVersionAndDataLayout(IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle),
				sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx))) {
			logger::error("framework ABI: data layout mismatch; plugin will not register callbacks");
			return false;
		}
		logger::info("framework ABI: version and data layout match");
		return true;
	}
}
