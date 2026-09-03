#pragma once

#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/math/vec3.h"

// Keeps a 6DOF lean from putting the eye inside the level.
//
// The problem splits in two, and only one half is engine-agnostic. Asking "is
// there anything between the eye and where the lean wants to go" is a physics
// query, different in every engine, and it stays with the mod. Deciding what to
// do with the answer is arithmetic, it is the same everywhere, and it is here.
//
// So the engine query arrives as a callback and this owns the policy: how far
// off a surface to hold the eye, how fast the allowance may open back up, and
// what happens when the query cannot answer.
namespace cameraunlock {
namespace camera {

/// What the engine's world query found along the lean direction.
struct LeanObstruction {
    /// False when the query could not be performed at all. Distinct from a
    /// definite no-hit: see LeanClamp::Apply for why the two cannot share a
    /// value.
    bool queried = false;
    /// True when something blocks the lean. False with queried=true is a
    /// definite clear path.
    bool blocked = false;
    /// From the eye along the lean direction, in the caller's world units.
    /// Only meaningful when blocked.
    float distance = 0.0f;
};

/// The engine-side query. `start` is the CLEAN eye position (before the lean),
/// `direction` is a unit vector toward the lean target, `max_distance` is the
/// magnitude of the lean being asked for. `context` is whatever the mod passed
/// to Apply - typically its player pawn or world pointer.
using LeanQueryFn = LeanObstruction (*)(void* context,
                                        const math::Vec3& start,
                                        const math::Vec3& direction,
                                        float max_distance);

struct LeanClampSettings {
    /// How far off the blocking surface to hold the eye, in the caller's world
    /// units. The units are the caller's throughout: a Unity mod working in
    /// metres passes 0.10f, an Unreal mod working in centimetres passes 10.0f.
    ///
    /// This must EXCEED the camera's near clip distance or the clamp does not
    /// deliver what it promises. Geometry closer to the eye than the near plane
    /// is culled, so a wall held at 2cm with a 10cm near plane is still not
    /// drawn and the player still sees the room beyond it. Stopping the eye
    /// short of the wall and having the wall vanish anyway is the same
    /// complaint with extra steps.
    float skin = 0.10f;

    /// How quickly the allowance reopens once an obstruction clears, on the
    /// same 0-1 scale as every other smoothing value in the fleet. 0.9 is a
    /// 200ms time constant. Tightening is never smoothed - see Apply.
    float release_smoothing = 0.9f;
};

/// Scales a lean offset down to whatever the world leaves room for.
///
/// Stateful because the release is damped, so one instance per camera and
/// Reset() whenever the camera cuts (chapter change, teleport) or the allowance
/// carries a previous room's wall into the new one.
class LeanClamp {
public:
    void SetSettings(const LeanClampSettings& settings) { m_settings = settings; }
    const LeanClampSettings& Settings() const { return m_settings; }

    /// Returns the offset the world leaves room for, along the direction of
    /// `desired_offset`.
    ///
    /// `query` runs at most once per call and only when there is a lean to
    /// test. A null query is the feature switched off and passes the offset
    /// straight through, which is what every mod ships until its clamp has been
    /// tested in game.
    ///
    /// Tightening is instant and releasing is damped, which is the spring-arm
    /// rule and it is not symmetric by accident. Easing INTO a smaller
    /// allowance would let the eye sit inside the wall for the duration of the
    /// ease, which is the whole bug. Easing back OUT stops the view popping
    /// when an obstruction clears. The cost is that swinging from a blocked
    /// direction to a free one lags by the release time constant, because the
    /// allowance is one scalar along a direction that moves.
    math::Vec3 Apply(const math::Vec3& eye,
                     const math::Vec3& desired_offset,
                     float delta_time,
                     LeanQueryFn query,
                     void* context) {
        m_contact = false;
        m_query_failed = false;

        const float desired = desired_offset.Magnitude();
        if (query == nullptr) return desired_offset;
        if (desired <= kMinimumLean) {
            // No lean to test, so nothing was blocking it either. Dropping the
            // allowance here rather than leaving it stale is what stops a wall
            // the player has already backed away from rationing the next lean
            // through its release ease.
            m_has_allowance = false;
            return desired_offset;
        }

        const math::Vec3 direction = desired_offset * (1.0f / desired);
        const LeanObstruction hit = query(context, eye, direction, desired + m_settings.skin);

        if (!hit.queried) {
            // The query is the only thing that knows where the walls are, so
            // without it there is no clamp to apply and the lean passes through
            // unchanged. That is reported rather than absorbed: a mod polls
            // LastQueryFailed() and logs it, because a clamp that has quietly
            // stopped clamping looks exactly like one that never engaged.
            m_query_failed = true;
            m_has_allowance = false;
            return desired_offset;
        }

        // A surface closer than the skin leaves nothing to give, including the
        // case where the eye starts inside geometry and the query answers zero.
        const float room = hit.blocked ? hit.distance - m_settings.skin : desired;
        const float target = room < 0.0f ? 0.0f : (room > desired ? desired : room);

        // With no allowance carried in, the frame is unrestricted and starts at
        // what the tracker asked for. Easing UP from zero instead would fade
        // every lean in over the release time constant from a standing start,
        // including the first lean of the session and the first after any pose
        // that passed through neutral.
        float allowed = m_has_allowance ? m_allowed : desired;
        if (allowed > desired) allowed = desired;

        if (target < allowed) {
            allowed = target;
            m_has_allowance = true;
        } else if (m_has_allowance) {
            allowed += (target - allowed) *
                       math::CalculateSmoothingFactor(m_settings.release_smoothing, delta_time);
            // The ease approaches asymptotically, so without a settle the
            // allowance never quite arrives and the clamp reports contact
            // forever after one touch. The tolerance is a FRACTION of the lean
            // rather than a fixed distance because the caller owns the units:
            // a threshold sized to settle in metres is a hundred times tighter
            // in centimetres, which is exactly how this shipped the first time
            // and why an Unreal mod reported contact continuously.
            if (desired - allowed <= desired * kSettleFraction) {
                allowed = desired;
                m_has_allowance = false;
            }
        } else {
            // Never restricted, so there is nothing to ease away FROM. Easing
            // here anyway makes a lean that is simply growing faster than the
            // release rate look like a lean held off a wall, which is both a
            // lie in the log and a drag on the pose the player asked for.
            allowed = desired;
        }

        m_allowed = allowed;
        m_contact = allowed < desired;
        return direction * allowed;
    }

    /// True when the last Apply held the eye short of where the tracker asked.
    bool InContact() const { return m_contact; }

    /// True when the last Apply could not run its query and passed the lean
    /// through unclamped.
    bool LastQueryFailed() const { return m_query_failed; }

    /// Forget the current allowance. The next Apply takes its query answer
    /// outright instead of easing up to it.
    void Reset() {
        m_allowed = 0.0f;
        m_has_allowance = false;
        m_contact = false;
        m_query_failed = false;
    }

private:
    /// Below this the offset has no reliable direction to query along, and the
    /// lean is too small to reach anything regardless.
    static constexpr float kMinimumLean = 1e-4f;

    /// How close the release has to get, as a fraction of the lean, before the
    /// allowance is called full. Relative so it means the same thing whatever
    /// units the caller works in.
    static constexpr float kSettleFraction = 1e-3f;

    LeanClampSettings m_settings{};
    float m_allowed = 0.0f;
    /// False means nothing is currently restricting the lean, which is not the
    /// same as an allowance of zero.
    bool m_has_allowance = false;
    bool m_contact = false;
    bool m_query_failed = false;
};

}  // namespace camera
}  // namespace cameraunlock
