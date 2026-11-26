#pragma once

namespace cameraunlock::ads {

// The aim mode of a shooter with positional tracking, in the order its key
// cycles (see the shooter-ads-handling skill). It is stored as three config
// bools, [Position] TrueFreeLook, FreeLookMarker and StockSights, a set as
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
    // While the sights are up the head's yaw, pitch and lean ease out and only
    // roll stays, so the sight picture is the game's own. At the hip tracking
    // is whole.
    StockSights,
};

struct AimModeSettings {
    bool trueFreeLook;
    bool freeLookMarker;
    bool stockSights;
};

// StockSights wins over the other two. Without it the marker bit means nothing
// without free look: FreeLookMarker alone is sights locked with no marker.
// TrueFreeLook alone is true free look, which is also what a config written
// before the marker existed holds, and a config written before stock sights
// existed has no StockSights and is one of the other three.
constexpr AimMode DecodeAimMode(bool trueFreeLook, bool freeLookMarker, bool stockSights) {
    return stockSights      ? AimMode::StockSights
           : !trueFreeLook  ? AimMode::SightsLocked
           : freeLookMarker ? AimMode::FreeLookMarker
                            : AimMode::TrueFreeLook;
}

// The three values a mode is saved as. Stock sights is {false, false, true},
// and the marker is never set without free look.
constexpr AimModeSettings EncodeAimMode(AimMode mode) {
    return {mode == AimMode::FreeLookMarker || mode == AimMode::TrueFreeLook, mode == AimMode::FreeLookMarker,
            mode == AimMode::StockSights};
}

constexpr AimMode NextAimMode(AimMode mode) {
    return mode == AimMode::SightsLocked     ? AimMode::FreeLookMarker
           : mode == AimMode::FreeLookMarker ? AimMode::TrueFreeLook
           : mode == AimMode::TrueFreeLook   ? AimMode::StockSights
                                             : AimMode::SightsLocked;
}

// The line a mod shows and logs when the mode changes, the same in every mod.
constexpr const char* AimModeLabel(AimMode mode) {
    return mode == AimMode::SightsLocked     ? "Aim mode: sights locked"
           : mode == AimMode::FreeLookMarker ? "Aim mode: free look with marker"
           : mode == AimMode::TrueFreeLook   ? "Aim mode: true free look"
                                             : "Aim mode: stock sights";
}

// Whether the lean stays honest on the camera while aiming, which is what the
// two free look modes share. Stock sights eases the whole lean out instead, so
// while it is fading it handles the lean as sights locked does.
constexpr bool IsFreeLook(AimMode mode) {
    return mode == AimMode::FreeLookMarker || mode == AimMode::TrueFreeLook;
}

// Whether the head's yaw, pitch and lean are eased out this frame. Feed it to
// an AdsFade of its own (ads/ads_fade.h): the fade's output is the share of
// those five that reaches the view, 1 at the hip and 0 with the sights up, and
// a press of the mode key mid-aim rides the fade like the aim button does.
// Roll is never scaled by it.
constexpr bool StockSightsEngaged(AimMode mode, bool aiming) {
    return aiming && mode == AimMode::StockSights;
}

// How opaque the aim marker is drawn. `sightsUp` is the mod's own fade for the
// sights, 0 at the hip and 1 with them fully up, which is the other way round
// from AdsFade's scale.
constexpr float AimMarkerOpacity(AimMode mode, float sightsUp) {
    return mode == AimMode::FreeLookMarker ? sightsUp : 0.0f;
}

}  // namespace cameraunlock::ads
