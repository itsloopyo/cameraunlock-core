// The legacy import support: ImportResult's factories, the dropped-value lines, N2
// (LegacyFiniteOrDefault), and a LegacyImport over a Config.

#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/input/key_bindings.h>

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
using cameraunlock::input::FormatVirtualKey;

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
              static_cast<int>(DropRule::Reticle) == 3 && static_cast<int>(DropRule::FollowsDefault) == 4,
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
    Check(Thrown([] { DescribeDroppedValue({static_cast<DropRule>(9), "A", "B", "C"}); }) ==
              "drop rule 9 is not a DropRule",
          "a rule outside DropRule throws");
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
        legacy.toggle_key = 0x24;
        legacy.remote_smoothing = std::numeric_limits<float>::quiet_NaN();
        std::vector<DroppedValue> dropped;
        out.toggle_key = FormatVirtualKey(static_cast<int>(legacy.toggle_key));
        out.remote_smoothing =
            LegacyFiniteOrDefault(legacy.remote_smoothing, RuntimeConfig{}.remote_smoothing, "Smoothing", "RemoteSmoothing", dropped);
        return ImportResult::Imported(std::move(dropped));
    };
    RuntimeConfig config;
    const ImportResult result = import.run(LegacyInput{L"C:\\Games\\HeadTracking.ini", "C:\\Games\\HeadTracking.ini", false}, config);
    Check(result.status == ImportStatus::Imported && config.toggle_key == "Home" && config.remote_smoothing == 0.15f &&
              result.dropped.size() == 1 &&
              SameDrop(result.dropped[0], DropRule::NonFiniteNumber, "Smoothing", "RemoteSmoothing", "nan"),
          "an import maps the frozen Config, through N2, and returns what it dropped");
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
        TestN2();
        TestLegacyImport();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
