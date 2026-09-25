// The legacy import support: ImportResult's factories, the dropped-value lines, N1
// (LegacyVirtualKeyToBindings), N2 (LegacyFiniteOrDefault), pose shaping (LegacyPoseShaping),
// and a LegacyImport over a Config.

#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/input/key_bindings.h>

#include <climits>
#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace cameraunlock::config;
using cameraunlock::input::FormatKeyBindings;
using cameraunlock::input::FormatVirtualKey;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;
using cameraunlock::input::ParseKeyBindings;

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
std::string Thrown(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "(nothing thrown)";
}

bool SameDrop(const DroppedValue& d, DropRule rule, const char* section, const char* key, const char* value) {
    return d.rule == rule && d.section == section && d.key == key && d.value == value;
}

template <class F>
bool SameBits(F a, F b) {
    return std::memcmp(&a, &b, sizeof(F)) == 0;
}

void TestImportResult() {
    const ImportResult imported = ImportResult::Imported({{DropRule::Reticle, "Reticle", "ShowReticle", "false"}});
    Check(imported.status == ImportStatus::Imported && imported.reason.empty() && imported.dropped.size() == 1,
          "Imported keeps its dropped values and has no reason");
    const ImportResult absent = ImportResult::Absent({});
    Check(absent.status == ImportStatus::Absent && absent.reason.empty() && absent.dropped.empty(), "Absent");
    const ImportResult refused = ImportResult::Refused("FieldOfView=30 is outside 60 to 120");
    Check(refused.status == ImportStatus::Refused && refused.reason == "FieldOfView=30 is outside 60 to 120" &&
              refused.dropped.empty(),
          "Refused keeps its reason");
    const ImportResult undecodable = ImportResult::Undecodable("the file is not UTF-8");
    Check(undecodable.status == ImportStatus::Undecodable && undecodable.reason == "the file is not UTF-8",
          "Undecodable keeps its reason");
    Check(Thrown([] { ImportResult::Refused(""); }) == "a refused or undecodable import needs a reason",
          "Refused with no reason throws");
    Check(Thrown([] { ImportResult::Undecodable(""); }) == "a refused or undecodable import needs a reason",
          "Undecodable with no reason throws");
    Check(static_cast<int>(ImportStatus::Imported) == 0 && static_cast<int>(ImportStatus::Refused) == 1 &&
              static_cast<int>(ImportStatus::Undecodable) == 2 && static_cast<int>(ImportStatus::Absent) == 3,
          "ImportStatus numbers match the C# enum");
    Check(static_cast<int>(DropRule::NonFiniteNumber) == 1 && static_cast<int>(DropRule::PoseShaping) == 2 &&
              static_cast<int>(DropRule::Reticle) == 3 && static_cast<int>(DropRule::FollowsDefault) == 4 &&
              static_cast<int>(DropRule::KeyCodeOutOfRange) == 5,
          "DropRule numbers match the C# enum");
}

void TestDescribe() {
    Check(DescribeDroppedValue({DropRule::NonFiniteNumber, "Smoothing", "RemoteSmoothing", "nan"}) ==
              "not carried: [Smoothing] RemoteSmoothing=nan, it is not a finite number, so the default is used",
          "N2 line");
    Check(DescribeDroppedValue({DropRule::PoseShaping, "Sensitivity", "YawSensitivity", "1.5"}) ==
              "not carried: [Sensitivity] YawSensitivity=1.5, sensitivity, deadzones, response curves and axis "
              "inversion are set in the tracker now, not in this mod",
          "pose-shaping line");
    Check(DescribeDroppedValue({DropRule::Reticle, "Reticle", "ShowReticle", "false"}) ==
              "not carried: [Reticle] ShowReticle=false, this mod no longer draws or toggles a reticle",
          "reticle line");
    Check(DescribeDroppedValue({DropRule::FollowsDefault, "Position", "CollisionEnabled", "false"}) ==
              "not carried: [Position] CollisionEnabled=false, this setting now follows the mod's default",
          "follows-default line");
    Check(DescribeDroppedValue({DropRule::KeyCodeOutOfRange, "Hotkeys", "ToggleKey", "0x230"}) ==
              "not carried: [Hotkeys] ToggleKey=0x230, it is not a key code from 0x01 to 0xFE, so the action is unbound",
          "N1 line");
    Check(Thrown([] { DescribeDroppedValue({static_cast<DropRule>(9), "A", "B", "C"}); }) ==
              "drop rule 9 is not a DropRule",
          "a rule outside DropRule throws");
}

void TestN1() {
    Check(LegacyVirtualKeyToBindings(0x23) == "End", "0x23 is End");
    Check(LegacyVirtualKeyToBindings(0x87) == "F24", "0x87 is F24");
    Check(LegacyVirtualKeyToBindings(0xBA) == "0xBA", "0xBA has no name, so hex");
    Check(LegacyVirtualKeyToBindings(0x11) == "0x11", "a bare modifier code has no name, so hex");
    bool every = true;
    for (long long code = 0x01; code <= 0xFE; ++code) {
        const std::string text = LegacyVirtualKeyToBindings(code);
        const auto read = ParseKeyBindings(text);
        every = every && text == FormatVirtualKey(static_cast<int>(code)) && read.ok() && read.bindings.size() == 1 &&
                read.bindings[0].vk == code && read.bindings[0].modifiers == KeyModifiers::kNone;
    }
    Check(every, "every code from 0x01 to 0xFE reads back as that one key");
    bool none = true;
    for (const long long code : {0LL, 0xFFLL, 0x100LL, 0x187LL, 0x230LL, -1LL, -0x79LL, 0x1000000FFLL,
                                 static_cast<long long>(LLONG_MAX), static_cast<long long>(LLONG_MIN)}) {
        none = none && LegacyVirtualKeyToBindings(code).empty();
    }
    Check(none, "codes outside 0x01-0xFE are unbound, 0xFF, 0x230, -1 and the long long limits among them");

    std::vector<DroppedValue> dropped;
    Check(LegacyVirtualKeyToBindings(0x23, "Hotkeys", "ToggleKey", dropped) == "End" && dropped.empty(),
          "an in-range code records nothing");
    Check(LegacyVirtualKeyToBindings(0xFE, "Hotkeys", "ToggleKey", dropped) == "0xFE" && dropped.empty(),
          "0xFE, the top of the range, records nothing");
    Check(LegacyVirtualKeyToBindings(0, "Hotkeys", "YawModeKey", dropped).empty() && dropped.empty(),
          "code 0, a legacy file's way of saying unbound, records nothing");
    Check(LegacyVirtualKeyToBindings(0xFF, "Hotkeys", "PositionToggleKey", dropped).empty(),
          "0xFF, which GetAsyncKeyState can report, is unbound");
    LegacyVirtualKeyToBindings(0x230, "Hotkeys", "ToggleKey", dropped);
    LegacyVirtualKeyToBindings(-1, "General", "CycleKey", dropped);
    LegacyVirtualKeyToBindings(LLONG_MIN, "General", "Min", dropped);
    Check(dropped.size() == 4 && SameDrop(dropped[0], DropRule::KeyCodeOutOfRange, "Hotkeys", "PositionToggleKey", "0xFF") &&
              SameDrop(dropped[1], DropRule::KeyCodeOutOfRange, "Hotkeys", "ToggleKey", "0x230") &&
              SameDrop(dropped[2], DropRule::KeyCodeOutOfRange, "General", "CycleKey", "-1") &&
              SameDrop(dropped[3], DropRule::KeyCodeOutOfRange, "General", "Min", "-9223372036854775808"),
          "out-of-range codes record the drop in hex, or in decimal when negative");

    Check(FormatKeyBindings({KeyBinding{KeyModifiers::kNone, 0x23}, KeyBinding{KeyModifiers::kCtrl | KeyModifiers::kShift, 'Y'}}) ==
              "End, Ctrl+Shift+Y",
          "a chord switch folds into the list through FormatKeyBindings");
}

void TestN2() {
    std::vector<DroppedValue> dropped;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Check(LegacyFiniteOrDefault(0.3f, 0.15f, "Smoothing", "RemoteSmoothing", dropped) == 0.3f && dropped.empty(),
          "a finite float is kept and records nothing");
    Check(SameBits(LegacyFiniteOrDefault(-0.0f, 0.15f, "Smoothing", "LocalSmoothing", dropped), -0.0f) && dropped.empty(),
          "-0.0 is finite and kept bit for bit");
    Check(LegacyFiniteOrDefault(std::numeric_limits<float>::max(), 0.15f, "A", "B", dropped) ==
                  std::numeric_limits<float>::max() &&
              dropped.empty(),
          "the largest float is finite");
    Check(LegacyFiniteOrDefault(nan, 0.15f, "Smoothing", "RemoteSmoothing", dropped) == 0.15f, "nan gives the default");
    Check(LegacyFiniteOrDefault(inf, 0.2f, "Position", "PositionLimitY", dropped) == 0.2f, "inf gives the default");
    Check(LegacyFiniteOrDefault(-inf, 1.0f, "Sensitivity", "YawSensitivity", dropped) == 1.0f, "-inf gives the default");
    Check(LegacyFiniteOrDefault(std::numeric_limits<double>::infinity(), 0.5, "Camera", "Scale", dropped) == 0.5,
          "a double works the same");
    Check(dropped.size() == 4 && SameDrop(dropped[0], DropRule::NonFiniteNumber, "Smoothing", "RemoteSmoothing", "nan") &&
              SameDrop(dropped[1], DropRule::NonFiniteNumber, "Position", "PositionLimitY", "inf") &&
              SameDrop(dropped[2], DropRule::NonFiniteNumber, "Sensitivity", "YawSensitivity", "-inf") &&
              SameDrop(dropped[3], DropRule::NonFiniteNumber, "Camera", "Scale", "inf"),
          "each non-finite value records the drop");
    Check(Thrown([&] { LegacyFiniteOrDefault(1.0f, nan, "Smoothing", "RemoteSmoothing", dropped); }) ==
              "[Smoothing] RemoteSmoothing: the row default is not finite",
          "a non-finite default throws");
}

std::string PoseLine(const PoseShapingValue& p) {
    return "[" + p.section + "] " + p.key + "=" + p.value + " shipped " + p.shipped + (p.folded ? " folded" : "");
}

void TestPoseShaping() {
    std::vector<PoseShapingValue> pose;
    std::vector<DroppedValue> dropped;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    LegacyPoseShaping(true, true, "Tracking", "InvertRoll", pose, dropped);
    LegacyPoseShaping(0.5f, 0.5f, "Tracking", "RollSensitivity", pose, dropped);
    LegacyPoseShaping(-0.0f, 0.0f, "Tracking", "Deadzone", pose, dropped);
    Check(pose.size() == 3 && dropped.empty() && PoseLine(pose[0]) == "[Tracking] InvertRoll=true shipped true folded" &&
              PoseLine(pose[1]) == "[Tracking] RollSensitivity=0.5 shipped 0.5 folded" &&
              PoseLine(pose[2]) == "[Tracking] Deadzone=-0.0 shipped 0.0 folded",
          "a value equal to the shipped one, compared as a number, is folded and nothing is dropped");

    pose.clear();
    LegacyPoseShaping(false, true, "Tracking", "InvertRoll", pose, dropped);
    LegacyPoseShaping(2.0f, 1.0f, "Position", "SensitivityX", pose, dropped);
    LegacyPoseShaping(0.15, 0.0, "Rotation", "YawDeadzone", pose, dropped);
    LegacyPoseShaping(nan, 1.0f, "Sensitivity", "YawSensitivity", pose, dropped);
    LegacyPoseShaping(-std::numeric_limits<double>::infinity(), 1.0, "Sensitivity", "PitchSensitivity", pose, dropped);
    Check(pose.size() == 5 && PoseLine(pose[0]) == "[Tracking] InvertRoll=false shipped true" &&
              PoseLine(pose[1]) == "[Position] SensitivityX=2.0 shipped 1.0" &&
              PoseLine(pose[2]) == "[Rotation] YawDeadzone=0.15 shipped 0.0" &&
              PoseLine(pose[3]) == "[Sensitivity] YawSensitivity=nan shipped 1.0" &&
              PoseLine(pose[4]) == "[Sensitivity] PitchSensitivity=-inf shipped 1.0",
          "a value the player changed is listed with its shipped value, not folded");
    Check(dropped.size() == 5 && SameDrop(dropped[0], DropRule::PoseShaping, "Tracking", "InvertRoll", "false") &&
              SameDrop(dropped[1], DropRule::PoseShaping, "Position", "SensitivityX", "2.0") &&
              SameDrop(dropped[2], DropRule::PoseShaping, "Rotation", "YawDeadzone", "0.15") &&
              SameDrop(dropped[3], DropRule::PoseShaping, "Sensitivity", "YawSensitivity", "nan") &&
              SameDrop(dropped[4], DropRule::PoseShaping, "Sensitivity", "PitchSensitivity", "-inf"),
          "and dropped as pose shaping, in the canonical codecs' spelling");
    Check(Thrown([&] { LegacyPoseShaping(1.0f, nan, "Tracking", "RollSensitivity", pose, dropped); }) ==
              "[Tracking] RollSensitivity: the shipped value is not finite",
          "a shipped value that is not finite throws");
    Check(pose.size() == 5 && dropped.size() == 5, "and records nothing");

    const ImportResult imported = ImportResult::Imported(dropped, pose);
    const ImportResult absent = ImportResult::Absent({}, {pose[0]});
    Check(imported.pose_shaping.size() == 5 && absent.pose_shaping.size() == 1 &&
              ImportResult::Imported({}).pose_shaping.empty() && ImportResult::Refused("r").pose_shaping.empty() &&
              ImportResult::Undecodable("u").pose_shaping.empty(),
          "Imported and Absent carry the pose-shaping values; the other statuses carry none");
}

struct FrozenConfig {
    long long toggle_key = 0x23;
    float remote_smoothing = 0.15f;
};

struct RuntimeConfig {
    std::string toggle_key = "End";
    float remote_smoothing = 0.15f;
};

void TestLegacyImport() {
    LegacyImport<RuntimeConfig> import;
    Check(!import.run, "a default LegacyImport has no import");
    import.keys = {{"General", "ToggleKey"}, {"Smoothing", "RemoteSmoothing"}, {"", "Verbose"}};
    import.run = [](const LegacyInput& input, RuntimeConfig& out) {
        if (input.ansi_lossy) return ImportResult::Absent({});
        FrozenConfig legacy;
        legacy.toggle_key = 0xFF;
        legacy.remote_smoothing = std::numeric_limits<float>::quiet_NaN();
        std::vector<DroppedValue> dropped;
        out.toggle_key = LegacyVirtualKeyToBindings(legacy.toggle_key, "General", "ToggleKey", dropped);
        out.remote_smoothing =
            LegacyFiniteOrDefault(legacy.remote_smoothing, RuntimeConfig{}.remote_smoothing, "Smoothing", "RemoteSmoothing", dropped);
        return ImportResult::Imported(std::move(dropped));
    };
    RuntimeConfig config;
    const ImportResult result = import.run(LegacyInput{L"C:\\Games\\HeadTracking.ini", "C:\\Games\\HeadTracking.ini", false}, config);
    Check(result.status == ImportStatus::Imported && config.toggle_key.empty() && config.remote_smoothing == 0.15f &&
              result.dropped.size() == 2 &&
              SameDrop(result.dropped[0], DropRule::KeyCodeOutOfRange, "General", "ToggleKey", "0xFF") &&
              SameDrop(result.dropped[1], DropRule::NonFiniteNumber, "Smoothing", "RemoteSmoothing", "nan"),
          "an import maps the frozen Config through N1 and N2 and returns what it dropped");
    Check(import.keys.size() == 3 && import.keys[2].section.empty(), "keys hold a section-less entry");
    RuntimeConfig untouched;
    Check(import.run(LegacyInput{L"C:\\Spiele\\\x00DC\\HeadTracking.ini", "C:\\Spiele\\?\\HeadTracking.ini", true}, untouched)
                  .status == ImportStatus::Absent,
          "the import sees the driver's path views");
}

}  // namespace

int RunLegacyImportTests() {
    g_failures = 0;
    std::cout << "\nLegacy import tests\n";
    try {
        TestImportResult();
        TestDescribe();
        TestN1();
        TestN2();
        TestPoseShaping();
        TestLegacyImport();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
