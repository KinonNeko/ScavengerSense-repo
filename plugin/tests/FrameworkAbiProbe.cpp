#include "../src/ImGuiCompat.h"
#include <cstdio>
int main()
{
	std::printf("{\"version\":\"%s\",\"io\":%zu,\"style\":%zu,\"vec2\":%zu,\"vec4\":%zu,\"drawvert\":%zu,\"drawidx\":%zu}\n",
		IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx));
	return sizeof(ImGuiIO) == 14616 && sizeof(ImGuiStyle) == 1140 && sizeof(ImDrawVert) == 20 && sizeof(ImDrawIdx) == 2 ? 0 : 1;
}
