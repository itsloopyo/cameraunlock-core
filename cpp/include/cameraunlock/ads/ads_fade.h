#pragma once

namespace cameraunlock::ads {

// The shape of the transition into and out of aiming down sights.
//
// Head tracking and iron sights want different things from the camera. Tracking
// says the view is wherever you are looking; a sight picture says the view is
// down the barrel, because that is the only place the weapon's own reticle means
// anything. So the moment the sights start coming up the head pose comes off the
// camera and the frame settles onto the aim - which is where the reticle was
// already pointing, so the thing the player was about to shoot ends up in the
// middle of the screen.
//
// This class owns the SHAPE of that transition and nothing else. It returns a
// scale, 1 at the hip and 0 with the sights up, and the caller decides what the
// scale blends between:
//
//   AdsMode::Paused    blend the head pose down to nothing and hold it there.
//   Marker / Tracked   blend the absolute pose into the pose measured from the
//                      entry frame (entry_pose.h), which is identity at that
//                      moment.
//
// So all three modes make the same swing onto the aim, and differ only in what
// happens for the rest of the aim.
//
// It is a SUSPEND, not a reset. The pose keeps flowing through the pipeline with
// its smoothing state intact, so lowering the weapon eases the view back to
// where the head actually is. Resetting instead would swing the view back
// through the whole head angle on the way out, dozens of times a firefight.
// Reset stays right for menus, cinematics and the master toggle, which is what
// Reset() below is for.
//
// The tracker's centre is deliberately not moved by any of this. Head centre
// means "looking down the gun", always. Recentring on the sights coming up - the
// obvious first idea - makes the pose the player happened to hold when they
// pressed aim the new neutral, so they have to HOLD their head turned to keep
// looking where they shot, and every aim press walks the neutral further from
// where the head actually rests.
//
// Pure: no clock of its own, no logging, no game. nowMs comes from the caller,
// which is what lets the whole transition be driven frame by frame in a test.
class AdsFade {
public:
    // How long the transition takes when the sights start coming up. Short
    // enough to be done before there is a sight picture to look through - a
    // weapon's own raise animation is around a fifth of a second - and long
    // enough that the view leans onto the gun rather than snapping to it.
    static constexpr unsigned long long kLowerMs = 150;
    // And back when they drop. Longer, because nothing is waiting on it and a
    // slower return is the more comfortable half.
    static constexpr unsigned long long kRaiseMs = 250;

    // Called once per rendered frame, before the head pose is applied. `aiming`
    // is the ADS state for this frame, polled rather than latched. Returns the
    // scale to blend at: 1 at the hip, 0 with the sights up.
    float Update(bool aiming, unsigned long long nowMs) {
        if (aiming && (m_state == State::Hip || m_state == State::Raising)) {
            m_state = State::Lowering;
            m_startMs = nowMs;
        } else if (!aiming && (m_state == State::Lowering || m_state == State::Aiming)) {
            m_state = State::Raising;
            m_startMs = nowMs;
        }

        // Both transitions run off the same clock, so a player who taps aim gets
        // a partial fade in each direction rather than a state machine that has
        // to be told what to do about being interrupted.
        switch (m_state) {
            case State::Hip:
                return 1.0f;
            case State::Aiming:
                return 0.0f;
            case State::Lowering: {
                const unsigned long long elapsed = nowMs - m_startMs;
                if (elapsed >= kLowerMs) {
                    m_state = State::Aiming;
                    return 0.0f;
                }
                return 1.0f - Ease(elapsed, kLowerMs);
            }
            case State::Raising: {
                const unsigned long long elapsed = nowMs - m_startMs;
                if (elapsed >= kRaiseMs) {
                    m_state = State::Hip;
                    return 1.0f;
                }
                return Ease(elapsed, kRaiseMs);
            }
        }
        return 1.0f;
    }

    // Drops back to hip state. Call wherever tracking is suppressed - menu,
    // loading, cinematic, master toggle, tracker dropout - so the next aim
    // starts clean.
    void Reset() { m_state = State::Hip; }

private:
    enum class State { Hip, Lowering, Aiming, Raising };

    // Smoothstep, so the transition leaves and arrives at rest instead of
    // starting and stopping with a visible corner.
    static float Ease(unsigned long long elapsed, unsigned long long duration) {
        const float t = static_cast<float>(elapsed) / static_cast<float>(duration);
        return t * t * (3.0f - 2.0f * t);
    }

    State m_state = State::Hip;
    unsigned long long m_startMs = 0;
};

}  // namespace cameraunlock::ads
