#pragma once
#include "imgui.h"
inline bool ImGui_ImplWin32_Init(void*) { return true; }
inline void ImGui_ImplWin32_Shutdown() {}
inline void ImGui_ImplWin32_NewFrame() {}
