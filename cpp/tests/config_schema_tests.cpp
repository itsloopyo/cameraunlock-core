// Tests for the shared config-key schema and its C++ consumer.
//
// The point of the schema is that the two languages accept the SAME spellings, so the
// cases here are the fleet's real drift: Network.UDPPort vs UdpPort vs udp_port, six
// spellings of yaw sensitivity, position limits under bare LimitX/LimitY names. Every one
// of them must land on the same field, and a key that belongs to no concept must be
// ignored rather than guessed at.

#include <cameraunlock/config/head_tracking_config.h>

#include <cameraunlock/config/value_guards.h>

#include <cmath>
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

    // The bare section-dependent nouns. Matching throws the section away, so
    // aliasing "Enabled" onto EnableOnStartup meant a file carrying [Position]
    // Enabled=false - which 39 repos write - turned the whole mod off while
    // leaving position tracking on, and said nothing. Resolving to nothing is
    // the only answer this table can give without the section.
    Check(ResolveConfigKey("Enabled") == nullptr, "a bare Enabled belongs to no concept");
    Check(ResolveConfigKey("Enable") == nullptr, "nor does a bare Enable");
    Check(std::string(ResolveConfigKey("EnableOnStartup")) == "enableonstartup",
          "the master switch still has its own name");
    Check(std::string(ResolveConfigKey("AutoEnable")) == "enableonstartup",
          "and its unambiguous aliases");
    Check(std::string(ResolveConfigKey("PositionEnabled")) == "positionenabled",
          "and position tracking has its own");

    Check(cameraunlock::IsRetiredConfigKey(cameraunlock::config_keys::kSmoothing),
          "Smoothing is marked retired");
    Check(!cameraunlock::IsRetiredConfigKey(cameraunlock::config_keys::kLocalSmoothing),
          "LocalSmoothing is not retired");
}

void TestRotationValues() {
    auto config = Apply({{"Port", "5555"},
                         {"AutoEnable", "false"},
                         {"yaw_sens", "1.5"},
                         {"PitchSensitivity", "0.5"},
                         {"SensitivityRoll", "2"},
                         {"InvertPitch", "yes"},
                         {"WorldLockedYaw", "false"}});

    Check(config.udp_port == 5555, "Port sets udp_port");
    Check(!config.enable_on_startup, "AutoEnable=false clears enable_on_startup");
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

// The one line that used to kill a whole mod. Every RE Engine plugin's shipped
// HeadTracking.ini carries [Position] Enabled, and section-less matching read it
// as the master switch.
void TestAmbiguousEnabledIsIgnored() {
    auto config = Apply({{"Enabled", "false"}});
    Check(config.enable_on_startup, "a bare Enabled=false does not turn head tracking off");
    Check(config.position_enabled, "and does not silently mean position tracking either");
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

// The range guards on the numeric keys. Both of the values covered here are one
// character away in a hand-edited ini and neither is survivable downstream: a
// negative position limit inverts the bounds of math::Clamp(v, -limit, limit),
// which then returns the same number for every input, and a huge sensitivity
// multiplies out to an infinity that reaches the camera.
void TestOutOfRangeNumericValues() {
    using cameraunlock::config::kMaxPositionLimit;
    using cameraunlock::config::kMaxSensitivity;

    auto negative = Apply({{"PositionLimitX", "-0.30"},
                           {"PositionLimitY", "-1.0"},
                           {"PositionLimitYDown", "-1.0"},
                           {"PositionLimitZ", "-0.40"},
                           {"PositionLimitZBack", "-0.10"}});

    Check(negative.position.limit_x >= 0.0f && negative.position.limit_y >= 0.0f &&
              negative.position.limit_y_down >= 0.0f && negative.position.limit_z >= 0.0f &&
              negative.position.limit_z_back >= 0.0f,
          "a negative position limit never reaches PositionSettings");

    const cameraunlock::HeadTrackingConfig defaults;
    Check(NearEq(negative.position.limit_x, defaults.position.limit_x) &&
              NearEq(negative.position.limit_z, defaults.position.limit_z),
          "a refused position limit keeps the working default rather than collapsing to zero");

    auto huge = Apply({{"PositionLimitZ", "10000"}, {"YawSensitivity", "1e30"},
                       {"PitchSensitivity", "-1e30"}, {"PositionSensitivityZ", "1e30"}});
    Check(NearEq(huge.position.limit_z, defaults.position.limit_z),
          "a mistyped 10000 for 0.10 is refused");
    Check(NearEq(huge.yaw_sensitivity, 1.0f) && NearEq(huge.pitch_sensitivity, 1.0f) &&
              NearEq(huge.position.sensitivity_z, 1.0f),
          "a sensitivity past the magnitude bound is refused");

    // The reason the sensitivity bound is where it is: the processor decomposes
    // to at most 180 degrees, and that product has to stay finite.
    Check(std::isfinite(180.0f * kMaxSensitivity),
          "a full-range angle at the largest accepted sensitivity stays finite");

    // Sign is a tuning choice - inverting an axis without the Invert flags - so
    // only the magnitude is bounded.
    auto negativeSensitivity = Apply({{"YawSensitivity", "-1.0"},
                                      {"PositionSensitivityY", "-2.5"}});
    Check(NearEq(negativeSensitivity.yaw_sensitivity, -1.0f) &&
              NearEq(negativeSensitivity.position.sensitivity_y, -2.5f),
          "a negative sensitivity is accepted");

    // The bounds themselves are inclusive, and zero disables an axis rather than
    // meaning "unset", so it must not be replaced by the default.
    auto edges = Apply({{"PositionLimitZBack", "0"},
                        {"PositionLimitX", std::to_string(kMaxPositionLimit)},
                        {"RollSensitivity", std::to_string(kMaxSensitivity)}});
    Check(NearEq(edges.position.limit_z_back, 0.0f), "zero is a legitimate position limit");
    Check(NearEq(edges.position.limit_x, kMaxPositionLimit) &&
              NearEq(edges.roll_sensitivity, kMaxSensitivity),
          "the documented maximum is accepted, not refused");
}

// A refused LimitY must not be mirrored into limit_y_down: the file did not
// successfully name a vertical limit, so the post-loop mirror has nothing to
// carry across.
void TestRefusedVerticalLimitIsNotMirrored() {
    const cameraunlock::HeadTrackingConfig defaults;
    auto config = Apply({{"PositionLimitY", "-1.0"}});
    Check(NearEq(config.position.limit_y, defaults.position.limit_y) &&
              NearEq(config.position.limit_y_down, defaults.position.limit_y_down),
          "a refused LimitY leaves both vertical limits at their defaults");

    // A duplicated key whose second entry is refused must not unsay the first.
    auto duplicated = Apply({{"PositionLimitY", "0.35"}, {"PositionLimitY", "-1.0"}});
    Check(NearEq(duplicated.position.limit_y, 0.35f) &&
              NearEq(duplicated.position.limit_y_down, 0.35f),
          "a later refused entry does not undo an accepted LimitY or its mirror");
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
    TestAmbiguousEnabledIsIgnored();
    TestVerticalLimitMirroring();
    TestSmoothingComposition();
    TestRejectedValues();
    TestOutOfRangeNumericValues();
    TestRefusedVerticalLimitIsNotMirrored();
    TestHotkeys();
    TestIniParsing();
    return g_failures;
}
