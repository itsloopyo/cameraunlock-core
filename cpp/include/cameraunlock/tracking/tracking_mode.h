#pragma once

#include <optional>

namespace cameraunlock {

/// Active tracking mode for a HeadTrackingSession.
enum class TrackingMode {
    RotationAndPosition = 0,
    RotationOnly = 1,
    PositionOnly = 2,
};

/// How a TrackingMode is stored in a config: [General] RotationEnabled and
/// [Position] PositionEnabled. The mapping is data/pipeline-conformance.json's
/// preference_modes.tracking_mode, and the tests hold these functions to it.
struct TrackingModeChannels {
    bool rotation_enabled;
    bool position_enabled;
};

/// The pair a mode is saved as. Selecting a mode writes both keys together.
constexpr TrackingModeChannels EncodeTrackingMode(TrackingMode mode) {
    return {mode != TrackingMode::PositionOnly, mode != TrackingMode::RotationOnly};
}

/// The mode a stored pair names, or nullopt when no mode writes that pair.
/// false/false is one such pair. It is reported, not mapped onto a mode: what
/// an unrepresentable config means is the caller's decision, and a caller that
/// saved a repaired mode back would overwrite what the user wrote.
constexpr std::optional<TrackingMode> DecodeTrackingMode(bool rotation_enabled, bool position_enabled) {
    if (rotation_enabled && position_enabled) return TrackingMode::RotationAndPosition;
    if (rotation_enabled) return TrackingMode::RotationOnly;
    if (position_enabled) return TrackingMode::PositionOnly;
    return std::nullopt;
}

}  // namespace cameraunlock
