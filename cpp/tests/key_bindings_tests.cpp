// ParseKeyBindings and FormatKeyBindings against the native rows of
// data/fixtures/canonical-ini/keys/cases.tsv, which the C# KeyBindingFixtures runs the unity
// rows of, plus the key table and, on Windows, RegisterKeyBindings.

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/input/key_names.g.h>

#ifdef _WIN32
#include <cameraunlock/input/key_binding_registration.h>
#endif

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cameraunlock::input::FormatKeyBindings;
using cameraunlock::input::FormatVirtualKey;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyBindingsParseResult;
using cameraunlock::input::KeyModifiers;
using cameraunlock::input::ParseKeyBindings;

// CameraUnlock.Core.Input.KeyModifiers carries the same numbers.
static_assert(static_cast<unsigned>(KeyModifiers::kNone) == 0, "KeyModifiers::kNone");
static_assert(static_cast<unsigned>(KeyModifiers::kCtrl) == 1, "KeyModifiers::kCtrl");
static_assert(static_cast<unsigned>(KeyModifiers::kShift) == 2, "KeyModifiers::kShift");
static_assert(static_cast<unsigned>(KeyModifiers::kAlt) == 4, "KeyModifiers::kAlt");

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

template <class F>
bool Throws(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open fixture " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The byte escape data/fixtures/canonical-ini/README.md defines.
std::string Unescape(const std::string& field) {
    std::string bytes;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\') {
            bytes.push_back(field[i]);
        } else if (i + 1 < field.size() && field[i + 1] == '\\') {
            bytes.push_back('\\');
            ++i;
        } else if (i + 3 < field.size() && field[i + 1] == 'x') {
            bytes.push_back(static_cast<char>(std::stoi(field.substr(i + 2, 2), nullptr, 16)));
            i += 3;
        } else {
            throw std::runtime_error("bad escape in fixture field " + field);
        }
    }
    return bytes;
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

void TestFixtures() {
    std::cout << "Key binding fixtures (native rows):\n";
    const std::string tsv = ReadBytes(fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES) / "keys" / "cases.tsv");

    int rows = 0;
    std::size_t start = 0;
    while (start < tsv.size()) {
        std::size_t end = tsv.find('\n', start);
        if (end == std::string::npos) end = tsv.size();
        const std::string line = tsv.substr(start, end - start);
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;

        const std::vector<std::string> fields = SplitTabs(line);
        if (fields[0] != "native") {
            if (fields[0] != "unity") throw std::runtime_error("unknown dialect in row " + line);
            continue;
        }
        ++rows;
        const std::string input = Unescape(fields[1]);
        const KeyBindingsParseResult parsed = ParseKeyBindings(input);

        if (fields.size() == 3 && fields[2] == "invalid") {
            Check(!parsed.ok() && parsed.bindings.empty(), "invalid: " + line);
            continue;
        }
        if (fields.size() != 4 || fields[2] != "canonical") throw std::runtime_error("malformed row " + line);

        const std::string canonical = Unescape(fields[3]);
        if (!parsed.ok()) {
            Check(false, line + " (error: " + parsed.error + ")");
            continue;
        }
        const std::string formatted = FormatKeyBindings(parsed.bindings);
        const KeyBindingsParseResult again = ParseKeyBindings(formatted);
        Check(formatted == canonical && again.ok() && again.bindings == parsed.bindings &&
                  FormatKeyBindings(again.bindings) == formatted,
              "canonical: " + line + (formatted == canonical ? "" : " (got " + formatted + ")"));
    }
    Check(rows > 100, "the fixture file has native rows (" + std::to_string(rows) + ")");
}

void TestErrorsNameTheExpectation() {
    std::cout << "ParseKeyBindings errors:\n";
    const char* inputs[] = {"End,,Home", "Ctrl+Ctrl+End", "Ctrl+Shift", "End+Ctrl", "Page Up",
                            "0x100", "Mouse0", "End, end"};
    for (const char* input : inputs) {
        const KeyBindingsParseResult parsed = ParseKeyBindings(input);
        Check(!parsed.ok() && parsed.error.find("expected") != std::string::npos,
              std::string("'") + input + "' fails naming what was expected: " + parsed.error);
    }
    Check(ParseKeyBindings("Ctrl+Ctrl+End").error.find("Ctrl twice") != std::string::npos,
          "a repeated modifier is named");
    Check(ParseKeyBindings("Mouse0").error.find("Windows key code") != std::string::npos,
          "a Unity-only name says it has no Windows key code");
    Check(ParseKeyBindings("End").ok() && ParseKeyBindings("End").error.empty(), "success has no error");
}

void TestParsedValues() {
    std::cout << "ParseKeyBindings values:\n";
    const KeyBindingsParseResult parsed = ParseKeyBindings("End, Shift+Ctrl+Y, Alt+0xBA");
    const std::vector<KeyBinding> expected = {
        {KeyModifiers::kNone, 0x23},
        {KeyModifiers::kCtrl | KeyModifiers::kShift, 0x59},
        {KeyModifiers::kAlt, 0xBA},
    };
    Check(parsed.ok() && parsed.bindings == expected, "codes and modifier flags as written");
    Check(ParseKeyBindings("  ").ok() && ParseKeyBindings("  ").bindings.empty(), "blank is an empty list");
}

void TestFormat() {
    std::cout << "FormatVirtualKey and FormatKeyBindings:\n";
    Check(FormatVirtualKey(0x23) == "End", "a named code is its name");
    Check(FormatVirtualKey(0x05) == "0x5", "an unnamed code is hex without padding");
    Check(FormatVirtualKey(0xBA) == "0xBA", "hex is upper case");
    Check(FormatVirtualKey(0x01) == "0x1" && FormatVirtualKey(0xFE) == "0xFE", "both ends of the range");
    Check(Throws([] { FormatVirtualKey(0); }), "0 throws");
    Check(Throws([] { FormatVirtualKey(0xFF); }), "0xFF throws");
    Check(Throws([] { FormatVirtualKey(-1); }), "-1 throws");
    Check(Throws([] { FormatVirtualKey(0x123); }), "0x123 throws");

    Check(FormatKeyBindings({}).empty(), "an empty list is empty text");
    Check(FormatKeyBindings({{KeyModifiers::kAlt | KeyModifiers::kCtrl, 0x70}, {KeyModifiers::kNone, 0x23}}) ==
              "Ctrl+Alt+F1, End",
          "modifiers in Ctrl, Shift, Alt order, items joined by comma and space");
    Check(Throws([] { FormatKeyBindings({{KeyModifiers::kNone, 0x23}, {KeyModifiers::kNone, 0x23}}); }),
          "a repeated binding throws");
    Check(Throws([] { FormatKeyBindings({{KeyModifiers::kNone, 0}}); }), "code 0 throws");
    Check(Throws([] { FormatKeyBindings({{static_cast<KeyModifiers>(8), 0x23}}); }),
          "a modifier value outside the flags throws");
}

void TestTable() {
    using namespace cameraunlock::input;
    std::cout << "Key table:\n";

    Check(kKeyModifierCount == 3 && std::strcmp(kKeyModifiers[0].name, "Ctrl") == 0 &&
              std::strcmp(kKeyModifiers[1].name, "Shift") == 0 && std::strcmp(kKeyModifiers[2].name, "Alt") == 0,
          "modifiers are Ctrl, Shift, Alt: the flags 1, 2, 4");

    // Every code VirtualKeyToString names has a name in the table too, and the VK constants
    // are the codes of the table's names.
    int named = 0;
    bool all = true;
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        if (std::strcmp(VirtualKeyToString(vk), "Unknown") == 0) continue;
        ++named;
        if (FormatVirtualKey(vk).rfind("0x", 0) == 0) {
            all = false;
            std::cout << "    VirtualKeyToString names " << vk << " but the table does not\n";
        }
    }
    Check(named > 0 && all, "every code VirtualKeyToString names has a table name");

    struct Pair { int vk; const char* name; };
    const Pair constants[] = {
        {VK::F1, "F1"}, {VK::F12, "F12"}, {VK::Escape, "Escape"}, {VK::Space, "Space"},
        {VK::PageUp, "PageUp"}, {VK::PageDown, "PageDown"}, {VK::Home, "Home"}, {VK::End, "End"},
        {VK::Insert, "Insert"}, {VK::Delete, "Delete"}, {VK::NumPad0, "Keypad0"}, {VK::NumPad9, "Keypad9"},
    };
    for (const Pair& pair : constants) {
        Check(FormatVirtualKey(pair.vk) == pair.name, std::string("input::VK code for ") + pair.name);
    }

#ifdef _WIN32
    // Every named code against the Windows SDK's own constant.
    const Pair sdk[] = {
        {VK_BACK, "Backspace"}, {VK_TAB, "Tab"}, {VK_RETURN, "Return"}, {VK_PAUSE, "Pause"},
        {VK_CAPITAL, "CapsLock"}, {VK_ESCAPE, "Escape"}, {VK_SPACE, "Space"}, {VK_PRIOR, "PageUp"},
        {VK_NEXT, "PageDown"}, {VK_END, "End"}, {VK_HOME, "Home"}, {VK_LEFT, "LeftArrow"},
        {VK_UP, "UpArrow"}, {VK_RIGHT, "RightArrow"}, {VK_DOWN, "DownArrow"}, {VK_SNAPSHOT, "Print"},
        {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_LWIN, "LeftWindows"}, {VK_RWIN, "RightWindows"},
        {VK_APPS, "Menu"}, {VK_NUMPAD0, "Keypad0"}, {VK_NUMPAD1, "Keypad1"}, {VK_NUMPAD2, "Keypad2"},
        {VK_NUMPAD3, "Keypad3"}, {VK_NUMPAD4, "Keypad4"}, {VK_NUMPAD5, "Keypad5"}, {VK_NUMPAD6, "Keypad6"},
        {VK_NUMPAD7, "Keypad7"}, {VK_NUMPAD8, "Keypad8"}, {VK_NUMPAD9, "Keypad9"},
        {VK_MULTIPLY, "KeypadMultiply"}, {VK_ADD, "KeypadPlus"}, {VK_SUBTRACT, "KeypadMinus"},
        {VK_DECIMAL, "KeypadPeriod"}, {VK_DIVIDE, "KeypadDivide"},
        {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"},
        {VK_F7, "F7"}, {VK_F8, "F8"}, {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
        {VK_F13, "F13"}, {VK_F14, "F14"}, {VK_F15, "F15"}, {VK_F16, "F16"}, {VK_F17, "F17"}, {VK_F18, "F18"},
        {VK_F19, "F19"}, {VK_F20, "F20"}, {VK_F21, "F21"}, {VK_F22, "F22"}, {VK_F23, "F23"}, {VK_F24, "F24"},
        {VK_NUMLOCK, "Numlock"}, {VK_SCROLL, "ScrollLock"}, {VK_LSHIFT, "LeftShift"}, {VK_RSHIFT, "RightShift"},
        {VK_LCONTROL, "LeftControl"}, {VK_RCONTROL, "RightControl"}, {VK_LMENU, "LeftAlt"}, {VK_RMENU, "RightAlt"},
    };
    int sdkNamed = static_cast<int>(sizeof(sdk) / sizeof(sdk[0]));
    for (const Pair& pair : sdk) {
        Check(FormatVirtualKey(pair.vk) == pair.name, std::string("the SDK's code for ") + pair.name);
    }
    // Letters and digits have no VK_ constant: the SDK documents them as their ASCII codes.
    for (char c = 'A'; c <= 'Z'; ++c) {
        Check(FormatVirtualKey(c) == std::string(1, c), std::string("letter ") + c);
    }
    for (char c = '0'; c <= '9'; ++c) {
        Check(FormatVirtualKey(c) == std::string("Alpha") + c, std::string("digit ") + c);
    }
    int tableNamed = 0;
    for (std::size_t i = 0; i < kKeyNameCount; ++i) {
        if (kKeyNames[i].vk != 0) ++tableNamed;
    }
    Check(tableNamed == sdkNamed + 26 + 10, "every named code is one checked above");

    Check(kKeyModifiers[0].vk == VK_CONTROL && kKeyModifiers[1].vk == VK_SHIFT && kKeyModifiers[2].vk == VK_MENU,
          "modifier codes are VK_CONTROL, VK_SHIFT, VK_MENU");
#endif
}

#ifdef _WIN32
KeyModifiers g_held = KeyModifiers::kNone;
KeyModifiers FakeHeld() { return g_held; }

void TestRegistration() {
    using cameraunlock::input::HotkeyPoller;
    using cameraunlock::input::RegisterKeyBindings;
    using cameraunlock::input::detail::BindingFires;
    using cameraunlock::input::detail::GuardBinding;
    std::cout << "RegisterKeyBindings:\n";

    const KeyModifiers ctrl = KeyModifiers::kCtrl;
    const KeyModifiers shift = KeyModifiers::kShift;
    const KeyModifiers alt = KeyModifiers::kAlt;
    const KeyModifiers none = KeyModifiers::kNone;

    Check(BindingFires(none, none), "a plain key fires alone");
    Check(BindingFires(none, ctrl) && BindingFires(none, shift) && BindingFires(none, alt),
          "a plain key fires with one modifier held");
    Check(!BindingFires(none, ctrl | shift) && !BindingFires(none, ctrl | shift | alt),
          "a plain key does not fire while Ctrl and Shift are both held (NavGuarded)");
    Check(BindingFires(ctrl | shift, ctrl | shift), "a chord fires with its modifiers held");
    Check(BindingFires(ctrl | shift, ctrl | shift | alt), "a chord fires with an extra modifier held");
    Check(!BindingFires(ctrl | shift, ctrl) && !BindingFires(ctrl | shift, none),
          "a chord does not fire without every modifier it names");
    Check(BindingFires(alt, alt) && !BindingFires(alt, ctrl), "Alt is a modifier like the others");

    int fired = 0;
    const auto guarded = GuardBinding(ctrl | shift, [&fired] { ++fired; }, &FakeHeld);
    g_held = ctrl;
    guarded();
    g_held = ctrl | shift;
    guarded();
    Check(fired == 1, "the guard reads the held modifiers when the key fires");

    HotkeyPoller poller;
    const auto parsed = ParseKeyBindings("End, Ctrl+Shift+Y, Alt+0xBA");
    const std::vector<int> ids = RegisterKeyBindings(poller, parsed.bindings, [] {});
    Check(ids.size() == 3 && ids[0] != ids[1] && ids[1] != ids[2] && ids[0] != ids[2], "one hotkey per binding");
    Check(RegisterKeyBindings(poller, {}, [] {}).empty(), "an empty list registers nothing");

    HotkeyPoller fresh;
    Check(Throws([&] { RegisterKeyBindings(fresh, {{none, 0x23}}, std::function<void()>()); }),
          "an empty action throws");
    Check(Throws([&] { RegisterKeyBindings(fresh, {{none, 0x23}, {none, 0x100}}, [] {}); }),
          "a code outside 0x01-0xFE throws");
    Check(Throws([&] { RegisterKeyBindings(fresh, {{static_cast<KeyModifiers>(8), 0x23}}, [] {}); }),
          "a modifier value outside the flags throws");
    Check(RegisterKeyBindings(fresh, {{none, 0x23}}, [] {}) == std::vector<int>{1},
          "a refused list registered nothing");
}
#endif

}  // namespace

int RunKeyBindingsTests() {
    std::cout << "\n=== Key binding tests ===\n";
    g_failures = 0;
    TestFixtures();
    TestErrorsNameTheExpectation();
    TestParsedValues();
    TestFormat();
    TestTable();
#ifdef _WIN32
    TestRegistration();
#endif
    return g_failures;
}
