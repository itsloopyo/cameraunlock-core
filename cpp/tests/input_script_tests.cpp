// Tests for the isolated-input command language (cameraunlock/dev/input_script.h).
// A harness writes these lines blind and a game plays them unattended, so a line
// that half parses, or a chord whose modifier comes up before its key, is a test
// run that did something other than what was asked.

#include <cameraunlock/dev/input_script.h>

#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

using cameraunlock::dev::InputAction;
using cameraunlock::dev::InputStep;
using cameraunlock::dev::MouseButton;
using cameraunlock::dev::ParseInputLine;

std::vector<InputStep> Parse(const char* line, bool& ok) {
    std::vector<InputStep> steps;
    std::string error;
    ok = ParseInputLine(line, steps, error);
    return steps;
}

bool Rejects(const char* line) {
    std::vector<InputStep> steps{InputStep{}};
    std::string error;
    return !ParseInputLine(line, steps, error) && !error.empty() && steps.size() == 1;
}

}  // namespace

int RunInputScriptTests() {
    std::cout << "Input script:\n";
    bool ok = false;

    Check(Parse("", ok).empty() && ok, "a blank line is no steps");
    Check(Parse("  # raise the sights", ok).empty() && ok, "a comment is no steps");

    auto steps = Parse("down E", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kKeyDown && steps[0].vk == 0x45,
          "down holds a key by name");
    steps = Parse("up shift", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kKeyUp && steps[0].vk == 0x10,
          "down and up take a modifier by name, which a hotkey list may not");

    steps = Parse("tap Insert", ok);
    Check(ok && steps.size() == 3 && steps[0].action == InputAction::kKeyDown && steps[0].vk == 0x2D
              && steps[1].action == InputAction::kWait && steps[1].waitMs == 60
              && steps[2].action == InputAction::kKeyUp && steps[2].vk == 0x2D,
          "tap presses, holds 60 ms and releases");
    steps = Parse("tap E 250", ok);
    Check(ok && steps.size() == 3 && steps[1].waitMs == 250, "tap takes its hold time");

    steps = Parse("tap Ctrl+Shift+U", ok);
    Check(ok && steps.size() == 11, "a chord is its modifiers, the key and the waits between");
    Check(ok && steps.size() == 11 && steps[0].vk == 0x11 && steps[0].action == InputAction::kKeyDown
              && steps[2].vk == 0x10 && steps[2].action == InputAction::kKeyDown
              && steps[4].vk == 0x55 && steps[4].action == InputAction::kKeyDown,
          "the modifiers go down before the key");
    Check(ok && steps.size() == 11 && steps[6].vk == 0x55 && steps[6].action == InputAction::kKeyUp
              && steps[8].vk == 0x10 && steps[8].action == InputAction::kKeyUp
              && steps[10].vk == 0x11 && steps[10].action == InputAction::kKeyUp,
          "the key comes up before the modifiers, which come up in reverse");

    steps = Parse("mouse right down", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kMouseDown && steps[0].button == MouseButton::kRight,
          "mouse holds a button");
    steps = Parse("mouse left click", ok);
    Check(ok && steps.size() == 3 && steps[0].action == InputAction::kMouseDown && steps[1].action == InputAction::kWait
              && steps[2].action == InputAction::kMouseUp && steps[2].button == MouseButton::kLeft,
          "a click is down, a wait and up");
    steps = Parse("move -260 10", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kMouseMove && steps[0].dx == -260 && steps[0].dy == 10,
          "move takes signed counts");
    steps = Parse("text help \"grendel\" 4 weap", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kText && steps[0].text == "help \"grendel\" 4 weap",
          "text keeps the rest of the line, spaces and quotes included");
    steps = Parse("wait 1500", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kWait && steps[0].waitMs == 1500, "wait takes milliseconds");

    Check(Rejects("press E"), "an unknown command is refused and adds nothing");
    Check(Rejects("down Ctrl+E"), "down refuses a chord");
    Check(Rejects("down E F"), "down refuses a second key");
    Check(Rejects("tap NotAKey"), "tap refuses a key that has no name");
    Check(Rejects("tap E soon"), "tap refuses a hold time that is not a number");
    Check(Rejects("mouse right press"), "mouse refuses anything but down, up or click, and adds nothing");
    Check(Rejects("mouse fourth down"), "mouse refuses an unknown button");
    steps = Parse("cursor 150 238", ok);
    Check(ok && steps.size() == 1 && steps[0].action == InputAction::kCursor && steps[0].dx == 150 && steps[0].dy == 238,
          "cursor takes a point of the client area");
    Check(Rejects("cursor 150"), "cursor refuses one number");
    Check(Rejects("cursor -5 20"), "cursor refuses a point left of the client area");
    Check(Rejects("move 10"), "move refuses one number");
    Check(Rejects("move 1.5 2"), "move refuses a fraction");
    Check(Rejects("wait"), "wait refuses no time");
    Check(Rejects("text"), "text refuses nothing to type");

    steps = Parse("pad 1 a tap", ok);
    Check(ok && steps.size() == 3 && steps[0].action == InputAction::kPad && steps[0].pad == 1
              && steps[0].padButton == 0x1000 && steps[0].padDown && steps[1].action == InputAction::kWait
              && steps[2].action == InputAction::kPad && !steps[2].padDown,
          "a pad tap is the button down, a wait and up");
    steps = Parse("pad 0 start down", ok);
    Check(ok && steps.size() == 1 && steps[0].pad == 0 && steps[0].padButton == 0x0010 && steps[0].padDown,
          "pad holds a button");
    steps = Parse("pad 2 stick right 0.5 -1", ok);
    Check(ok && steps.size() == 1 && steps[0].padControl == cameraunlock::dev::PadControl::kRightStick
              && steps[0].padX == 0.5f && steps[0].padY == -1.0f,
          "pad stick takes a side and two deflections");
    steps = Parse("pad 3 trigger left 0.25", ok);
    Check(ok && steps.size() == 1 && steps[0].padControl == cameraunlock::dev::PadControl::kLeftTrigger
              && steps[0].padX == 0.25f,
          "pad trigger takes a side and a pull");
    Check(Rejects("pad 4 a tap"), "pad refuses a fifth controller");
    Check(Rejects("pad 0 q tap"), "pad refuses a button that has no name");
    Check(Rejects("pad 0 a press"), "pad refuses anything but down, up or tap");
    Check(Rejects("pad 0 stick left 2 0"), "pad refuses a stick past its end");
    Check(Rejects("pad 0 stick up 0 0"), "pad refuses a stick that is not left or right");
    Check(Rejects("pad 0 trigger right -0.5"), "pad refuses a trigger below zero");
    Check(Rejects("pad 0 stick left 0.5"), "pad refuses a stick with one number");

    cameraunlock::dev::PadState pad;
    Check(!pad.connected, "a pad the script never named is not plugged in");
    cameraunlock::dev::ApplyPadStep(pad, cameraunlock::dev::PadControl::kButton, 0x1000, true, 0, 0);
    Check(pad.connected && pad.buttons == 0x1000 && pad.packet == 1, "a button press plugs the pad in and moves the packet on");
    cameraunlock::dev::ApplyPadStep(pad, cameraunlock::dev::PadControl::kButton, 0x0100, true, 0, 0);
    cameraunlock::dev::ApplyPadStep(pad, cameraunlock::dev::PadControl::kButton, 0x1000, false, 0, 0);
    Check(pad.buttons == 0x0100 && pad.packet == 3, "a release leaves the other held buttons down");
    cameraunlock::dev::ApplyPadStep(pad, cameraunlock::dev::PadControl::kRightStick, 0, false, 1.0f, -1.0f);
    Check(pad.rightX == 32767 && pad.rightY == -32768 && pad.leftX == 0, "a stick at its ends is XInput's range");
    cameraunlock::dev::ApplyPadStep(pad, cameraunlock::dev::PadControl::kLeftTrigger, 0, false, 1.0f, 0);
    Check(pad.leftTrigger == 255 && pad.rightTrigger == 0, "a full trigger pull is 255");

    return g_failures;
}
