#pragma once
// Minimal stub: just enough surface for the overlay headers to compile.
#include <windows.h>

#define IMGUI_IMPL_API
#define IM_COL32(r, g, b, a) ((unsigned)(a) << 24 | (unsigned)(b) << 16 | (unsigned)(g) << 8 | (unsigned)(r))

struct ImVec2 { float x, y; ImVec2() : x(0), y(0) {} ImVec2(float a, float b) : x(a), y(b) {} };
struct ImVec4 { float x, y, z, w; };

typedef int ImGuiConfigFlags;
enum { ImGuiConfigFlags_NoMouseCursorChange = 1 << 4 };

struct ImDrawData { int _unused; };
struct ImDrawList {
    void AddCircleFilled(const ImVec2&, float, unsigned, int = 0) {}
    void AddLine(const ImVec2&, const ImVec2&, unsigned, float = 1.0f) {}
    void AddRectFilled(const ImVec2&, const ImVec2&, unsigned) {}
};
struct ImGuiIO {
    ImGuiConfigFlags ConfigFlags;
    const char* IniFilename;
    ImVec2 DisplaySize;
};

namespace ImGui {
    inline void* CreateContext() { return nullptr; }
    inline void DestroyContext() {}
    inline ImGuiIO& GetIO() { static ImGuiIO io{}; return io; }
    inline void NewFrame() {}
    inline void Render() {}
    inline ImDrawData* GetDrawData() { static ImDrawData d{}; return &d; }
    inline ImDrawList* GetBackgroundDrawList() { static ImDrawList l{}; return &l; }
    inline ImDrawList* GetForegroundDrawList() { static ImDrawList l{}; return &l; }
}
