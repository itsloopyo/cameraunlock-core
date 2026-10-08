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

    int last = 0;
    bool wasForeground = true;

    // The key and the foreground as given from `ms` on, polled every 16 ms up to then as
    // they were, the way the poller's thread does.
    void At(int ms, bool down, bool foreground = true) {
        for (int t = last + 16; t < ms; t += 16) Poll(t, wasForeground);
        Jump(ms, down, foreground);
    }

    // The same with no poll since the last one: a polling thread that was held up.
    void Jump(int ms, bool down, bool foreground = true) {
        g_down[cameraunlock::input::VK::Delete] = down;
        Poll(ms, foreground);
        last = ms;
        wasForeground = foreground;
    }

    void Disarm() { poller.DisarmHoldPressesWith(&FakeDown); }

private:
    void Poll(int ms, bool foreground) {
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
        rig.Jump(500, false);
        Check(rig.taps == 0 && rig.holds == 0,
              "a release first seen past the hold time, with no poll that saw the key held, runs nothing");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.Jump(2000, true);
        Check(rig.holds == 0,
              "a key seen down again after two seconds with no poll may be a second press, and is not a hold yet");
        rig.At(2399, true);
        Check(rig.holds == 0, "its time down counts from the poll that found it");
        rig.At(2400, true);
        Check(rig.holds == 1 && rig.taps == 0, "and it is a hold once that is the hold time");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.Jump(2000, true);
        rig.At(2080, false);
        Check(rig.taps == 1 && rig.holds == 0, "let go soon after such a gap it is a tap");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.At(300, true);
        rig.Jump(300 + cameraunlock::input::HotkeyPoller::kMaxHoldPollGapMs, true);
        Check(rig.holds == 1, "a poll late by no more than kMaxHoldPollGapMs is only late: the key was down all along");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.Jump(2000, true, false);
        rig.At(2080, false, true);
        Check(rig.taps == 0, "a press that may have begun again in the background, behind a gap, runs nothing");
    }
    {
        HoldRig rig;
        rig.At(0, true);
        rig.Disarm();
        rig.At(80, false);
        Check(rig.taps == 0, "a press down when the presses are disarmed is not a tap when it is let go");
        rig.At(100, true);
        rig.At(180, false);
        Check(rig.taps == 1, "and the press after it is");
        rig.At(200, true);
        rig.Disarm();
        rig.At(700, true);
        rig.At(800, false);
        Check(rig.holds == 0 && rig.taps == 1, "nor is a disarmed press a hold, at the hold time or at its release");
        rig.At(816, true);
        rig.At(1300, true);
        Check(rig.holds == 1, "and the press after that holds");
    }
    {
        HoldRig rig;
        rig.At(0, false);
        g_down[cameraunlock::input::VK::Delete] = true;
        rig.Disarm();
        rig.At(16, true);
        rig.At(80, false);
        Check(rig.downs == 0 && rig.taps == 0, "a key that went down since the last poll is disarmed with the rest");
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

// A poller with one tap hotkey on Delete, held at 400 ms and doubled within 300, and a count
// of what it ran.
struct TapRig {
    cameraunlock::input::HotkeyPoller poller;
    int taps = 0;
    int doubles = 0;
    int holds = 0;
    int id = 0;
    bool accept = true;
    int last = 0;
    bool wasForeground = true;

    TapRig() {
        g_down[cameraunlock::input::VK::Delete] = false;
        id = poller.AddTapHotkey(
            cameraunlock::input::VK::Delete, 400, 300, [this] { ++taps; }, [this] { ++doubles; }, [this] { ++holds; },
            [this] { return accept; });
    }

    // As HoldRig::At and HoldRig::Jump.
    void At(int ms, bool down, bool foreground = true) {
        for (int t = last + 16; t < ms; t += 16) Poll(t, wasForeground);
        Jump(ms, down, foreground);
    }

    void Jump(int ms, bool down, bool foreground = true) {
        g_down[cameraunlock::input::VK::Delete] = down;
        Poll(ms, foreground);
        last = ms;
        wasForeground = foreground;
    }

    void Disarm() { poller.DisarmHoldPressesWith(&FakeDown); }

    bool Ran(int t, int d, int h) const { return taps == t && doubles == d && holds == h; }

private:
    void Poll(int ms, bool foreground) {
        poller.PollAt(std::chrono::steady_clock::time_point() + std::chrono::milliseconds(ms), foreground, &FakeDown);
    }
};

void TestTapDoubleTapAndHold() {
    std::cout << "HotkeyPoller::AddTapHotkey:\n";
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(80, false);
        Check(rig.Ran(0, 0, 0), "a tap runs nothing as it is let go");
        rig.At(379, false);
        Check(rig.Ran(0, 0, 0), "nor 299 ms later, while a second press could still come");
        rig.At(380, false);
        Check(rig.Ran(1, 0, 0), "at 300 ms with no second press it is a tap");
        rig.At(1000, false);
        Check(rig.Ran(1, 0, 0), "once");
    }
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(80, false);
        rig.At(200, true);
        Check(rig.Ran(0, 1, 0), "a second press 120 ms after the first was let go is a double tap as it goes down");
        rig.At(1000, true);
        Check(rig.Ran(0, 1, 0), "held, the second press is no hold");
        rig.At(1100, false);
        rig.At(1500, false);
        Check(rig.Ran(0, 1, 0), "and let go it is no tap, and the first tap never runs");
        rig.At(1516, true);
        rig.At(1600, false);
        rig.At(1900, false);
        Check(rig.Ran(1, 1, 0), "the press after a double tap starts afresh");
    }
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(80, false);
        rig.At(380, true);
        Check(rig.Ran(1, 0, 0), "a second press at 300 ms is too late: the tap runs");
        rig.At(460, false);
        rig.At(760, false);
        Check(rig.Ran(2, 0, 0), "and that press is a tap of its own");
    }
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(399, true);
        Check(rig.Ran(0, 0, 0), "a first press down 399 ms of 400 is not held yet");
        rig.At(400, true);
        Check(rig.Ran(0, 0, 1), "at 400 ms it is, while still down");
        rig.At(900, true);
        rig.At(1000, false);
        rig.At(1050, true);
        Check(rig.Ran(0, 0, 1), "the hold runs once, nothing runs when it is let go, and a press right after is no double tap");
        rig.At(1130, false);
        rig.At(1430, false);
        Check(rig.Ran(1, 0, 1), "it is a first press, and its tap runs in its time");
    }
    {
        TapRig rig;
        rig.At(0, true, false);
        rig.At(16, true, true);
        rig.At(96, false, true);
        rig.At(500, false, true);
        Check(rig.Ran(0, 0, 0), "a press begun while the process was in the background is no tap");
        rig.At(516, true, false);
        rig.At(532, true, true);
        rig.At(1100, true, true);
        rig.At(1200, false, true);
        Check(rig.Ran(0, 0, 0), "and no hold");
        rig.At(1300, true, true);
        rig.At(1380, false, true);
        rig.At(1450, true, false);
        Check(rig.Ran(0, 0, 0), "nor is one a second press");
        rig.At(1530, false, false);
        rig.At(1546, false, true);
        rig.At(1680, false, true);
        Check(rig.Ran(1, 0, 0), "and the tap before it runs in its time, as if that press had not been made");
    }
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(80, false);
        rig.At(200, false, false);
        rig.At(500, false, false);
        rig.At(516, false, true);
        rig.At(1000, false, true);
        Check(rig.Ran(0, 0, 0), "a tap whose time runs out while the process is in the background is dropped, not run late");
        rig.At(1016, true);
        rig.At(1096, false, false);
        rig.At(1112, false, true);
        rig.At(1500, false, true);
        Check(rig.Ran(0, 0, 0), "and a tap let go there is no tap");
        rig.At(1516, true);
        rig.At(1900, true, false);
        rig.At(1932, true, true);
        rig.At(2000, false, true);
        rig.At(2400, false, true);
        Check(rig.Ran(0, 0, 0), "nor a hold that comes of age there a hold, or a tap after");
    }
    {
        TapRig rig;
        rig.accept = false;
        rig.At(0, true);
        rig.At(80, false);
        rig.At(100, true);
        rig.At(600, true);
        rig.At(700, false);
        rig.At(1100, false);
        Check(rig.Ran(0, 0, 0), "a press its guard refuses is no tap and no hold");
        rig.At(1116, true);
        rig.At(1196, false);
        rig.accept = true;
        rig.At(1250, true);
        Check(rig.Ran(0, 0, 0), "and no first press: the accepted press after it is not a double tap");
        rig.At(1330, false);
        rig.At(1630, false);
        Check(rig.Ran(1, 0, 0), "it is a tap");
        rig.At(1700, true);
        rig.At(1780, false);
        rig.accept = false;
        rig.At(1850, true);
        rig.At(1930, false);
        Check(rig.Ran(1, 0, 0), "a second press the guard refuses is not a double tap");
        rig.At(2080, false);
        Check(rig.Ran(2, 0, 0), "and the tap before it still runs in its time");
    }
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(80, false);
        rig.Disarm();
        rig.At(500, false);
        Check(rig.Ran(0, 0, 0), "a tap waiting for its time is forgotten when the presses are disarmed");
        rig.At(516, true);
        rig.Disarm();
        rig.At(1000, true);
        rig.At(1100, false);
        rig.At(1500, false);
        Check(rig.Ran(0, 0, 0), "and a press down then is no hold and no tap");
        rig.At(1516, true);
        rig.At(1596, false);
        rig.At(1650, true);
        Check(rig.Ran(0, 1, 0), "the presses after it are presses like any other");
    }
    {
        TapRig rig;
        rig.At(0, true);
        rig.At(80, false);
        rig.Jump(2000, false);
        rig.At(2400, false);
        Check(rig.Ran(0, 0, 0), "a tap waiting across two seconds with no poll is dropped: it may not have been the last press");
        rig.At(2416, true);
        rig.Jump(4000, true);
        Check(rig.Ran(0, 0, 0), "a key seen down again after such a gap is not a hold yet");
        rig.At(4399, true);
        rig.At(4400, true);
        Check(rig.Ran(0, 0, 1), "it is one once it has been down the hold time from the poll that found it");
        rig.At(4500, false);
        rig.At(4600, true);
        rig.At(4680, false);
        rig.At(4750, true);
        Check(rig.Ran(0, 1, 1), "a double tap after that");
        rig.Jump(6000, true);
        rig.At(6080, false);
        rig.At(6380, false);
        Check(rig.Ran(1, 1, 1), "and its second press, still down after a gap, is a first press begun at that poll");
    }
    {
        TapRig rig;
        int plain = 0;
        rig.poller.AddHotkey(cameraunlock::input::VK::Delete, [&plain] { ++plain; });
        rig.poller.RemoveHotkey(rig.id);
        rig.At(0, true);
        rig.At(80, false);
        rig.At(500, false);
        Check(rig.Ran(0, 0, 0) && plain == 1, "a tap hotkey is removed by its id");
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
    TestTapDoubleTapAndHold();
    return g_failures;
}
