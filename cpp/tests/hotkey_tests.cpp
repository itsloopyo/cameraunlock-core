// Tests for the virtual-key table every mod binds its defaults against.
//
// Home / End / PageUp / PageDown are the shared nav-cluster bindings the fleet
// ships. PageUp (0x21) and PageDown (0x22) were absent from input::VK and from
// VirtualKeyToString while IsValidHotkeyCode already accepted them, so the
// convention was half-expressible: a mod wanting the standard binding had to
// hardcode the number, and a config dump printed "Unknown" for the key the user
// had actually pressed.

#include <cameraunlock/input/hotkey_poller.h>

#include <cstring>
#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

void TestNavClusterConstants() {
    namespace VK = cameraunlock::input::VK;
    std::cout << "input::VK nav cluster:\n";

    Check(VK::PageUp == 0x21, "PageUp is VK_PRIOR (0x21)");
    Check(VK::PageDown == 0x22, "PageDown is VK_NEXT (0x22)");
    Check(VK::End == 0x23, "End is VK_END (0x23)");
    Check(VK::Home == 0x24, "Home is VK_HOME (0x24)");
}

void TestNavClusterNames() {
    using cameraunlock::input::VirtualKeyToString;
    namespace VK = cameraunlock::input::VK;
    std::cout << "VirtualKeyToString nav cluster:\n";

    Check(std::strcmp(VirtualKeyToString(VK::PageUp), "PageUp") == 0,
          "PageUp names itself rather than Unknown");
    Check(std::strcmp(VirtualKeyToString(VK::PageDown), "PageDown") == 0,
          "PageDown names itself rather than Unknown");
    Check(std::strcmp(VirtualKeyToString(VK::Home), "Home") == 0, "Home still names itself");
    Check(std::strcmp(VirtualKeyToString(VK::End), "End") == 0, "End still names itself");
}

void TestNavClusterAccepted() {
    using cameraunlock::input::IsValidHotkeyCode;
    namespace VK = cameraunlock::input::VK;
    std::cout << "IsValidHotkeyCode nav cluster:\n";

    Check(IsValidHotkeyCode(VK::PageUp), "PageUp is bindable");
    Check(IsValidHotkeyCode(VK::PageDown), "PageDown is bindable");
    Check(IsValidHotkeyCode(VK::Home) && IsValidHotkeyCode(VK::End),
          "Home and End are bindable");
}

}  // namespace

int RunHotkeyTests() {
    std::cout << "\n=== Hotkey Tests ===\n";
    TestNavClusterConstants();
    TestNavClusterNames();
    TestNavClusterAccepted();
    return g_failures;
}
