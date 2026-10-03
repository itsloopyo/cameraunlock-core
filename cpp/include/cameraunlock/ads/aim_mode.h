#pragma once

namespace cameraunlock::ads {

// The aim mode of a shooter with positional tracking, in the order its key
// cycles (see the shooter-ads-handling skill). It is stored as two config
// bools, [Position] TrueFreeLook and [Position] FreeLookMarker, a pair as
// RotationEnabled and PositionEnabled are the tracking mode.
enum class AimMode {
    // The eye stays on the sight line while aiming.
    SightsLocked,
    // The weapon stays put in the world, the head moves freely around it, and
    // the mod draws an aim marker where the round will land while the sights
    // are up.
    FreeLookMarker,
    // The same with no marker.
    TrueFreeLook,
};

struct AimModePair {
    bool trueFreeLook;
    bool freeLookMarker;
};

// The marker bit means nothing without free look: FreeLookMarker alone is
// sights locked with no marker. TrueFreeLook alone is true free look, which
// is also what a config written before the marker existed holds.
constexpr AimMode DecodeAimMode(bool trueFreeLook, bool freeLookMarker) {
    return !trueFreeLook ? AimMode::SightsLocked : freeLookMarker ? AimMode::FreeLookMarker : AimMode::TrueFreeLook;
}

// The pair a mode is saved as. Never {false, true}.
constexpr AimModePair EncodeAimMode(AimMode mode) {
    return {mode != AimMode::SightsLocked, mode == AimMode::FreeLookMarker};
}

constexpr AimMode NextAimMode(AimMode mode) {
    return mode == AimMode::SightsLocked     ? AimMode::FreeLookMarker
           : mode == AimMode::FreeLookMarker ? AimMode::TrueFreeLook
                                             : AimMode::SightsLocked;
}

// The line a mod shows and logs when the mode changes, the same in every mod.
constexpr const char* AimModeLabel(AimMode mode) {
    return mode == AimMode::SightsLocked     ? "Aim mode: sights locked"
           : mode == AimMode::FreeLookMarker ? "Aim mode: free look with marker"
                                             : "Aim mode: true free look";
}

// How opaque the aim marker is drawn. `sightsUp` is the mod's own fade for the
// sights, 0 at the hip and 1 with them fully up, which is the other way round
// from AdsFade's scale.
constexpr float AimMarkerOpacity(AimMode mode, float sightsUp) {
    return mode == AimMode::FreeLookMarker ? sightsUp : 0.0f;
}

}  // namespace cameraunlock::ads
