#include "../src/ImGuiCompat.h"
#include "../src/FrameworkABI.h"
#include <cstdio>

namespace {
    const char* version = IMGUI_VERSION;
    bool sizesMatch = true;
    int checks = 0;
}
extern "C" const char* igGetVersion() { return version; }
extern "C" bool igDebugCheckVersionAndDataLayout(const char* text, size_t io, size_t style,
    size_t vec2, size_t vec4, size_t vertex, size_t index)
{
    ++checks;
    return sizesMatch && std::strcmp(text, IMGUI_VERSION) == 0 && io == 14616 && style == 1140 &&
        vec2 == 8 && vec4 == 16 && vertex == 20 && index == 2;
}
int main()
{
    if (!SS::CheckFrameworkABI() || checks != 1) { return 1; }
    sizesMatch = false;
    if (SS::CheckFrameworkABI() || checks != 2) { return 2; }
    version = "different-version";
    if (SS::CheckFrameworkABI() || checks != 2) { return 3; }
    std::puts("ABI guard accepts a match and rejects wrong sizes/version before callback registration");
    return 0;
}
