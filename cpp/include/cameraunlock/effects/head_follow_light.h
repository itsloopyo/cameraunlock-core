#pragma once

namespace cameraunlock::effects {

// A carried light that follows the head instead of the aim.
//
// When the player carries a directional light - a flashlight, a torch, a
// headlamp, a flare - it is UNLOCKED from the aim and turned with the head. The
// rest of the doctrine says the game's own aim, projectile and raycast code
// keeps reading the clean camera rotation and we move what is DRAWN to match;
// the beam is drawn, so the beam moves.
//
// **It leads the view by kDefaultLightMultiplier rather than matching it.** A
// player who turns their head keeps their eyes on what they turned towards, so
// their gaze sits past the centre of the screen. A light matched to the view
// alone lands short of what they are actually looking at, and the miss is
// largest exactly when the head is turned furthest, which is when a light is
// most likely to be the reason they turned.
//
// **The scaling is applied to the head pose, never to the tracker.** There is no
// second pipeline here: the pose that reaches the light is the pose that reached
// the camera, multiplied. Composing the beam from its own angles is how a beam
// and a view end up disagreeing about which way the head turned.
//
// **Roll is scaled with the rest.** A cone beam is rotationally symmetric about
// its own axis, so scaled roll changes nothing that can be seen; excluding it
// would be a special case with no observable effect and one more rule to
// remember. Contrast ads_blend.h, where roll IS excluded, and for a reason that
// is visible on screen.
//
// **The write is undone once the frame is drawn.** Everything except an engine
// that asks the mod where to aim the light (see below) applies the turn just
// before the light is rendered and puts the game's own rotation back after, so
// nothing in the game's own update ever observes a turned light. That matters:
// a game whose light publishes an aim point to other players, or whose
// interaction ray comes off the light, keeps reading its own values.
//
// ---------------------------------------------------------------------------
// What this header does NOT do, and will not
//
// Finding the light, and making the engine respect a rotation written to it, is
// the whole length of every implementation in the fleet and none of it
// generalises. Five shipped shapes, for calibration:
//
//   prey                    hooks the ArkLight setter to catch a live component,
//                           then identifies the beam by geometry (on the eye,
//                           facing the clean view) and writes through
//                           IEntity::SetPosRotScale, because a field poked
//                           behind the engine's back is read back perfectly and
//                           rendered from stale state.
//   still-wakes-the-deep    hooks USpringArmComponent::GetTargetRotation gated
//                           on ONE caller, so only the torch's arm sees the
//                           head pose. Nothing is written and nothing is
//                           restored - the engine asks, and the answer is the
//                           turned aim.
//   repo                    reflection onto FlashlightController.Instance, then
//                           its `spotlight` field, then Transform.rotation
//                           across the render pass.
//   resident-evil-requiem   reads the pooled beam off
//                           FlashLightController._CurrentLightObject and writes
//                           the transform's world matrix.
//   outer-wilds             a Harmony prefix/postfix on Flashlight.FixedUpdate,
//                           because the beam direction is taken off the camera
//                           transform inside that method.
//
// The only thing those five share is "here is a rotation, put it somewhere", and
// an abstraction over that would fit none of them. What they share for real is
// below: the number, its bounds, and the two shapes of scaling.
// ---------------------------------------------------------------------------

/// How far the light turns relative to the head. See the note above for why it
/// is not 1.0. Changing it is a breaking change; add a new field instead.
constexpr float kDefaultLightMultiplier = 1.5f;

/// Anything past this is a mistyped number rather than a setting: at 5x a
/// twenty-degree head turn puts the beam a full quadrant off the view, which is
/// already past useful. A value outside [0, this] is REJECTED and the previous
/// one stands, with a line in the log naming the key - not clamped, because
/// silently running at 5 when the file says 8 is a setting that does not do what
/// it says. Zero is inside the range and is a real request to pin the beam to
/// the aim, which is what the game does unmodded.
constexpr float kMaxLightMultiplier = 5.0f;

/// The config surface, in one place so the two keys mean the same thing in every
/// mod. Wired to the shared vocabulary as LightFollowsHead and LightMultiplier
/// (data/config-schema.json), which alias the spellings actually shipped:
/// CompensateFlashlight and FlashlightScale (prey), FlashlightFollowsHead and
/// FlashlightMultiplier (repo). The bare `Enabled` and `Multiplier` that
/// still-wakes-the-deep and resident-evil-requiem ship under their own section
/// are deliberately NOT aliased - section-less, `Enabled` is the master switch -
/// so those two read their own keys.
struct HeadFollowLightSettings {
    bool follows_head = true;
    float multiplier = kDefaultLightMultiplier;
};

/// A head pose in engine degrees, in whatever sign convention the mod's camera
/// composition already uses. This type carries no convention of its own: it is
/// the three numbers on their way to the same composition the view got.
struct HeadEuler {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

/// Scale a per-axis head pose. For a mod that HAS the pose as angles and feeds
/// them to the same composition function the camera uses.
///
/// This and ScaleHeadAngle below are two DIFFERENT operations and they do not
/// agree: scaling three Euler angles by k is not scaling one axis-angle by k
/// except when exactly one of the three is non-zero. On a combined pose they
/// part company by a couple of degrees at 1.5x, and by tens of degrees at 3x.
///
/// So the choice is not free, and it is not about which is more correct. Use the
/// one that matches how the mod already composes the head pose onto the CAMERA.
/// Then the beam and the view cannot disagree with each other, which is the
/// property a player can actually see. Two mods on different engines leading
/// their beams by slightly different numbers is not.
inline HeadEuler ScaleHeadEuler(const HeadEuler& head, float multiplier) {
    HeadEuler out;
    out.yaw = head.yaw * multiplier;
    out.pitch = head.pitch * multiplier;
    out.roll = head.roll * multiplier;
    return out;
}

/// Scale an axis-angle head rotation, for a mod that only has the delta between
/// the clean and the drawn basis. Scaling the ANGLE about the same axis is the
/// axis-angle spelling of an unclamped slerp from identity. See ScaleHeadEuler
/// above for why this is not interchangeable with it.
///
/// PRECONDITION: `angle` is the shortest arc, in [0, pi]. A caller deriving it
/// from two bases gets that for free - acos of the trace cannot exceed pi - but
/// a caller unpacking a quaternion must fold the negative hemisphere first, or
/// a 2 degree head turn scales to 177. The C# twin,
/// HeadFollowLightSettings.ScaleRotation, takes a quaternion and does that fold
/// itself.
///
/// The axis is unchanged and is not returned: a caller holding an axis-angle
/// already has it, and handing back a copy invites the axis and the angle being
/// taken from different frames.
inline float ScaleHeadAngle(float angle, float multiplier) { return angle * multiplier; }

}  // namespace cameraunlock::effects
