// Tests for the shared config-key schema and its C++ consumer.
//
// The point of the schema is that the two languages accept the SAME spellings, so the
// cases here are the fleet's real drift: Network.UDPPort vs UdpPort vs udp_port, six
// spellings of yaw sensitivity, position limits under bare LimitX/LimitY names. Every one
// of them must land on the same field, and a key that belongs to no concept must be
// ignored rather than guessed at.

#include <cameraunlock/config/head_tracking_config.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

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

bool NearEq(float a, float b) {
    const float d = a - b;
    return (d < 0 ? -d : d) < 1e-5f;
}

cameraunlock::HeadTrackingConfig Apply(
    std::initializer_list<std::pair<std::string, std::string>> values) {
    cameraunlock::HeadTrackingConfig config;
    config.ApplyValues(std::vector<std::pair<std::string, std::string>>(values));
    return config;
}

void TestNormalization() {
    using cameraunlock::ResolveConfigKey;

    Check(std::string(ResolveConfigKey("UdpPort")) == "udpport", "UdpPort resolves");
    Check(std::string(ResolveConfigKey("UDPPort")) == "udpport", "UDPPort resolves (case)");
    Check(std::string(ResolveConfigKey("udp_port")) == "udpport", "udp_port resolves (underscore)");
    Check(std::string(ResolveConfigKey("Udp-Port")) == "udpport", "Udp-Port resolves (hyphen)");
    Check(std::string(ResolveConfigKey("Port")) == "udpport", "Port resolves (alias)");
    Check(ResolveConfigKey("NotAKeyAnyoneUses") == nullptr, "unknown key resolves to nullptr");

    Check(std::string(ResolveConfigKey("yaw_sens")) == "yawsensitivity", "yaw_sens alias");
    Check(std::string(ResolveConfigKey("SensitivityYaw")) == "yawsensitivity", "SensitivityYaw alias");
    Check(std::string(ResolveConfigKey("limit_z_back")) == "positionlimitzback", "limit_z_back alias");
    Check(std::string(ResolveConfigKey("InvertTrackerZ")) == "invertpositionz", "InvertTrackerZ alias");
    Check(std::string(ResolveConfigKey("PivotUp")) == "trackerpivotup", "PivotUp alias");

    Check(cameraunlock::IsRetiredConfigKey(cameraunlock::config_keys::kSmoothing),
          "Smoothing is marked retired");
    Check(!cameraunlock::IsRetiredConfigKey(cameraunlock::config_keys::kLocalSmoothing),
          "LocalSmoothing is not retired");
}

void TestRotationValues() {
    auto config = Apply({{"Port", "5555"},
                         {"Enabled", "false"},
                         {"yaw_sens", "1.5"},
                         {"PitchSensitivity", "0.5"},
                         {"SensitivityRoll", "2"},
                         {"InvertPitch", "yes"},
                         {"WorldLockedYaw", "false"}});

    Check(config.udp_port == 5555, "Port sets udp_port");
    Check(!config.enable_on_startup, "Enabled=false clears enable_on_startup");
    Check(NearEq(config.yaw_sensitivity, 1.5f), "yaw_sens sets yaw sensitivity");
    Check(NearEq(config.pitch_sensitivity, 0.5f), "PitchSensitivity sets pitch sensitivity");
    Check(NearEq(config.roll_sensitivity, 2.0f), "SensitivityRoll sets roll sensitivity");
    Check(config.invert_pitch, "InvertPitch=yes sets inversion");
    Check(!config.world_space_yaw, "WorldLockedYaw is the same concept as WorldSpaceYaw");
}

void TestPositionValues() {
    auto config = Apply({{"PositionEnabled", "true"},
                         {"sensitivity_x", "1.2"},
                         {"SensY", "0.8"},
                         {"PositionSensitivityZ", "1.1"},
                         {"limit_x", "0.25"},
                         {"LimitY", "0.30"},
                         {"limit_y_down", "0.05"},
                         {"LimitZ", "0.45"},
                         {"limit_z_back", "0.12"},
                         {"InvertZ", "true"},
                         {"PivotForward", "0.08"},
                         {"PivotUp", "0.03"}});

    Check(config.position_enabled, "PositionEnabled");
    Check(NearEq(config.position.sensitivity_x, 1.2f), "sensitivity_x");
    Check(NearEq(config.position.sensitivity_y, 0.8f), "SensY");
    Check(NearEq(config.position.sensitivity_z, 1.1f), "PositionSensitivityZ");
    Check(NearEq(config.position.limit_x, 0.25f), "limit_x");
    Check(NearEq(config.position.limit_y, 0.30f), "LimitY");
    Check(NearEq(config.position.limit_y_down, 0.05f), "limit_y_down");
    Check(NearEq(config.position.limit_z, 0.45f), "LimitZ");
    Check(NearEq(config.position.limit_z_back, 0.12f), "limit_z_back");
    Check(config.position.invert_z, "InvertZ");
    Check(NearEq(config.tracker_pivot_forward, 0.08f), "PivotForward");
    Check(NearEq(config.tracker_pivot_up, 0.03f), "PivotUp");
}

void TestVerticalLimitMirroring() {
    auto only_up = Apply({{"LimitY", "0.40"}});
    Check(NearEq(only_up.position.limit_y, 0.40f), "LimitY alone sets the up limit");
    Check(NearEq(only_up.position.limit_y_down, 0.40f), "LimitY alone mirrors into the down limit");

    auto both = Apply({{"LimitY", "0.40"}, {"LimitYDown", "0.05"}});
    Check(NearEq(both.position.limit_y, 0.40f), "explicit LimitY");
    Check(NearEq(both.position.limit_y_down, 0.05f), "explicit LimitYDown wins over the mirror");

    auto reversed = Apply({{"LimitYDown", "0.05"}, {"LimitY", "0.40"}});
    Check(NearEq(reversed.position.limit_y_down, 0.05f), "order does not change the outcome");
}

void TestSmoothingComposition() {
    auto config = Apply({{"LocalSmoothing", "0.2"}, {"RemoteSmoothing", "0.4"}});
    Check(NearEq(config.local_smoothing, 0.2f), "LocalSmoothing");
    Check(NearEq(config.remote_smoothing, 0.4f), "RemoteSmoothing");
    // Position and rotation share one smoothing pair, so a config cannot end up smoothing
    // the two halves of the same pose differently.
    Check(NearEq(config.position.local_smoothing, 0.2f), "position inherits local smoothing");
    Check(NearEq(config.position.remote_smoothing, 0.4f), "position inherits remote smoothing");
}

void TestRejectedValues() {
    auto config = Apply({{"UdpPort", "70000"},
                         {"YawSensitivity", "nan"},
                         {"PitchSensitivity", "0,5"},
                         {"RollSensitivity", "1.5m"},
                         {"InvertYaw", "maybe"}});

    Check(config.udp_port == 4242, "out-of-range port leaves the default");
    Check(NearEq(config.yaw_sensitivity, 1.0f), "NaN sensitivity leaves the default");
    Check(NearEq(config.pitch_sensitivity, 1.0f), "European decimal comma is rejected whole");
    Check(NearEq(config.roll_sensitivity, 1.0f), "trailing unit text is rejected whole");
    Check(!config.invert_yaw, "unparseable bool leaves the default");
}

void TestHotkeys() {
    auto config = Apply({{"ToggleKey", "F10"},
                         {"TogglePositionKey", "F11"},
                         {"ReticleKey", "F12"},
                         {"CycleTrackingMode", "F9"}});

    Check(config.toggle_key_name == "F10", "ToggleKey");
    Check(config.position_toggle_key_name == "F11", "TogglePositionKey alias");
    Check(config.reticle_toggle_key_name == "F12", "ReticleKey alias");
    Check(config.cycle_tracking_mode_key_name == "F9", "CycleTrackingMode alias");
}

void TestIniParsing() {
    const std::string path = "config_schema_tests.ini";
    {
        std::ofstream file(path);
        file << "; a leading comment\n";
        file << "[Network]\n";
        file << "UDPPort = 4949   ; the port opentrack sends to\n";
        file << "\n";
        file << "[Position]\n";
        file << "Limit_Z = 0.35\n";
        file << "# hash comments too\n";
        file << "[Hotkeys]\n";
        file << "ToggleKey = \"End\"\n";
    }

    auto config = cameraunlock::HeadTrackingConfig::LoadFromFile(path);
    std::remove(path.c_str());

    Check(config.udp_port == 4949, "section headers are ignored, key still found");
    Check(NearEq(config.position.limit_z, 0.35f), "underscore spelling under [Position]");
    Check(config.toggle_key_name == "End", "surrounding quotes stripped");
}

}  // namespace

int RunConfigSchemaTests() {
    std::cout << "\nConfig Schema Tests\n";
    TestNormalization();
    TestRotationValues();
    TestPositionValues();
    TestVerticalLimitMirroring();
    TestSmoothingComposition();
    TestRejectedValues();
    TestHotkeys();
    TestIniParsing();
    return g_failures;
}
