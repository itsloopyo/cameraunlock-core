// Tests for the virtual-key table every mod binds its defaults against.
//
// Home / End / PageUp / PageDown are the shared nav-cluster bindings the fleet
// ships. PageUp (0x21) and PageDown (0x22) were absent from input::VK and from
// VirtualKeyToString while IsValidHotkeyCode already accepted them, so the
// convention was half-expressible: a mod wanting the standard binding had to
// hardcode the number, and a config dump printed "Unknown" for the key the user
// had actually pressed.
//
// And for the poller itself, through PollAt: a key that does one thing tapped and
// another held, driven down and up at chosen times with no keyboard and no waiting.

#include <cameraunlock/input/hotkey_poller.h>

#include <chrono>
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

bool g_down[256] = {};
bool FakeDown(int vkCode) { return g_down[vkCode]; }

// A poller with one hold hotkey on Delete, 400 ms, and a count of what it ran.
struct HoldRig {
    cameraunlock::input::HotkeyPoller poller;
    int downs = 0;
    int taps = 0;
    int holds = 0;
    int id = 0;

    HoldRig() {
        g_down[cameraunlock::input::VK::Delete] = false;
        id = poller.AddHoldHotkey(
            cameraunlock::input::VK::Delete, 400, [this] { ++taps; }, [this] { ++holds; }, [this] { ++downs; });
    }

    void At(int ms, bool down, bool foreground = true) {
        g_down[cameraunlock::input::VK::Delete] = down;
        poller.PollAt(std::chrono::steady_clock::time_point() + std::chrono::milliseconds(ms), foreground, &FakeDown);
    }
};

void TestTapAndHold() {
    std::cout << "HotkeyPoller::AddHoldHotkey:\n";
    {
        HoldRig rig;
        rig.At(0, false);
        rig.At(16, true);
        Check(rig.downs == 1 && rig.taps == 0 && rig.holds == 0, "a key going down runs neither action yet");
        rig.At(48, true);
        rig.At(96, false);
        Check(rig.taps == 1 && rig.holds == 0, "let go 80 ms later it is a tap");
        rig.At(112, false);
        rig.At(600, false);
        Check(rig.taps == 1 && rig.holds == 0 && rig.downs == 1, "once");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.At(399, true);
        Check(rig.taps == 0 && rig.holds == 0, "a key down 399 ms of 400 is not held yet");
        rig.At(400, true);
        Check(rig.holds == 1 && rig.taps == 0, "at 400 ms it is, while still down");
        rig.At(416, true);
        rig.At(800, true);
        Check(rig.holds == 1, "the hold runs once however long the key stays down");
        rig.At(816, false);
        Check(rig.taps == 0 && rig.holds == 1, "and nothing runs when it is let go");
        rig.At(832, true);
        rig.At(900, false);
        Check(rig.taps == 1 && rig.holds == 1 && rig.downs == 2, "the next press is a press of its own");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.At(399, false);
        Check(rig.taps == 1, "let go at 399 ms it is still a tap");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.At(500, false);
        Check(rig.taps == 0 && rig.holds == 0,
              "a release first seen past the hold time, with no poll that saw the key held, runs nothing");
    }
    {
        HoldRig rig;
        rig.At(0, true, false);
        rig.At(16, true, true);
        rig.At(96, false, true);
        Check(rig.downs == 0 && rig.taps == 0, "a tap begun while the process was in the background runs nothing");
        rig.At(100, true, false);
        rig.At(116, true, true);
        rig.At(600, true, true);
        rig.At(700, false, true);
        Check(rig.downs == 0 && rig.taps == 0 && rig.holds == 0, "nor a hold begun there, at 400 ms or at its release");
        rig.At(716, true, true);
        rig.At(780, false, true);
        Check(rig.taps == 1, "the press after it, begun in the foreground, does");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.At(80, false, false);
        Check(rig.taps == 0, "a tap let go while the process is in the background does not run");
        rig.At(100, true);
        rig.At(500, true, false);
        Check(rig.holds == 0, "nor a hold that comes of age there");
        rig.At(516, true, true);
        rig.At(600, false, true);
        Check(rig.holds == 0 && rig.taps == 0, "and it is not run late, as a hold or as a tap, when the process is back");
    }
    {
        HoldRig rig;
        int plain = 0;
        rig.poller.AddHotkey(cameraunlock::input::VK::Delete, [&plain] { ++plain; });
        rig.At(0, true);
        rig.At(16, true);
        rig.At(32, false);
        Check(plain == 1 && rig.taps == 1, "a hotkey of the older kind on the same key still fires as it goes down");
        rig.poller.RemoveHotkey(rig.id);
        rig.At(48, true);
        rig.At(64, false);
        Check(rig.taps == 1 && plain == 2, "and a hold hotkey is removed by its id");
    }
    g_down[cameraunlock::input::VK::Delete] = false;
}

}  // namespace

int RunHotkeyTests() {
    std::cout << "\n=== Hotkey Tests ===\n";
    TestNavClusterConstants();
    TestNavClusterNames();
    TestNavClusterAccepted();
    TestTapAndHold();
    return g_failures;
}
