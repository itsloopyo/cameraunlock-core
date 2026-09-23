// Holds the hand-written TrackingMode encode/decode to data/pipeline-conformance.json's
// preference_modes.tracking_mode, through the header the generator emits from it. The
// C# twin is TrackingModeChannelsTests, which reads the JSON directly.

#include "preference_modes.g.h"

#include <cameraunlock/config/head_tracking_config.h>
#include <cameraunlock/tracking/head_tracking_session.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

using cameraunlock::DecodeTrackingMode;
using cameraunlock::EncodeTrackingMode;
using cameraunlock::TrackingMode;
using preference_modes::kTrackingModeCount;
using preference_modes::kTrackingModes;

std::string Describe(std::optional<TrackingMode> mode) {
    if (!mode) return "unrepresentable";
    switch (*mode) {
        case TrackingMode::RotationAndPosition: return "RotationAndPosition";
        case TrackingMode::RotationOnly: return "RotationOnly";
        case TrackingMode::PositionOnly: return "PositionOnly";
    }
    return std::to_string(static_cast<int>(*mode));
}

void TestEveryModeIsDeclaredOnce() {
    for (TrackingMode mode : {TrackingMode::RotationAndPosition, TrackingMode::RotationOnly,
                              TrackingMode::PositionOnly}) {
        int seen = 0;
        for (size_t i = 0; i < kTrackingModeCount; ++i) {
            if (kTrackingModes[i].mode == mode) ++seen;
        }
        Check(seen == 1, "TrackingMode " + Describe(mode) + " is declared exactly once in preference_modes");
    }
    Check(kTrackingModeCount == 3, "preference_modes declares no mode TrackingMode lacks");
}

void TestEncode() {
    for (size_t i = 0; i < kTrackingModeCount; ++i) {
        const auto& expected = kTrackingModes[i];
        const auto channels = EncodeTrackingMode(expected.mode);
        Check(channels.rotation_enabled == expected.rotation_enabled &&
                  channels.position_enabled == expected.position_enabled,
              std::string("EncodeTrackingMode writes the pair declared for '") + expected.name + "'");
    }
}

void TestDecodeEveryPair() {
    for (bool rotation : {true, false}) {
        for (bool position : {true, false}) {
            std::optional<TrackingMode> declared;
            std::string name = "no mode";
            for (size_t i = 0; i < kTrackingModeCount; ++i) {
                if (kTrackingModes[i].rotation_enabled == rotation &&
                    kTrackingModes[i].position_enabled == position) {
                    declared = kTrackingModes[i].mode;
                    name = std::string("'") + kTrackingModes[i].name + "'";
                }
            }
            const auto decoded = DecodeTrackingMode(rotation, position);
            Check(decoded == declared,
                  std::string("DecodeTrackingMode(") + (rotation ? "true" : "false") + ", " +
                      (position ? "true" : "false") + ") returns " + Describe(decoded) +
                      ", preference_modes declares " + name);
        }
    }
}

void TestBothOffIsUnrepresentable() {
    Check(!DecodeTrackingMode(false, false).has_value(), "false/false decodes as unrepresentable");
}

struct IdleReceiver {
    bool GetRotation(float&, float&, float&) const { return false; }
    bool GetPosition(float&, float&, float&) const { return false; }
    int64_t GetLastReceiveTimestamp() const { return 0; }
    void Recenter() {}
};

// tracking_mode.h has no cycle of its own: HeadTrackingSession::CycleMode already
// defines one, and the file's array order is held to it here.
void TestSessionCycleFollowsTheFile() {
    IdleReceiver rx;
    cameraunlock::HeadTrackingSession<IdleReceiver> session(rx);
    for (size_t i = 0; i < kTrackingModeCount; ++i) {
        const auto& next = kTrackingModes[(i + 1) % kTrackingModeCount];
        session.SetMode(kTrackingModes[i].mode);
        Check(session.CycleMode() == next.mode,
              std::string("CycleMode from '") + kTrackingModes[i].name + "' lands on '" + next.name +
                  "', the next entry in preference_modes");
    }
}

std::optional<TrackingMode> DecodeParsed(std::vector<std::pair<std::string, std::string>> values) {
    cameraunlock::HeadTrackingConfig config;
    config.ApplyValues(values);
    return DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
}

void TestOmittedChannelsTakeTheirDefaults() {
    Check(DecodeParsed({}) == TrackingMode::RotationAndPosition, "a config naming neither channel is 6DOF");
    Check(DecodeParsed({{"PositionEnabled", "false"}}) == TrackingMode::RotationOnly,
          "PositionEnabled=false alone is rotation only");
    Check(DecodeParsed({{"RotationEnabled", "false"}}) == TrackingMode::PositionOnly,
          "RotationEnabled=false alone is position only");
}

void TestParsedBothOffStaysOff() {
    cameraunlock::HeadTrackingConfig config;
    config.ApplyValues({{"RotationEnabled", "false"}, {"PositionEnabled", "false"}});
    Check(!DecodeTrackingMode(config.rotation_enabled, config.position_enabled).has_value() &&
              !config.rotation_enabled && !config.position_enabled,
          "a parsed false/false stays false/false and decodes as unrepresentable");
}

}  // namespace

int RunTrackingModeTests() {
    std::cout << "\nTracking Mode Tests\n";
    TestEveryModeIsDeclaredOnce();
    TestEncode();
    TestDecodeEveryPair();
    TestBothOffIsUnrepresentable();
    TestSessionCycleFollowsTheFile();
    TestOmittedChannelsTakeTheirDefaults();
    TestParsedBothOffStaysOff();
    return g_failures;
}
