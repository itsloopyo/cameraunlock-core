#pragma once

namespace cameraunlock::ads {

// The transition a mod rides to ease a positional lean out while the sights are
// up, and back in when they come down.
//
// Head tracking carries straight on through the aim: rotation is never faded,
// made relative or suspended, because turning the camera about the eye leaves
// the weapon's sight line through the eye, sights lined up. A lean is different.
// It translates the eye off that line, so a mod that cannot draw the weapon from
// the clean eye scales its lean by this fade instead (see the
// shooter-ads-handling skill).
//
// This class owns the SHAPE of the transition and nothing else. It returns a
// scale, 1 at the hip and 0 with the sights up.
//
// Pure: no clock of its own, no logging, no game. nowMs comes from the caller,
// which is what lets the whole transition be driven frame by frame in a test.
class AdsFade {
public:
    // How long the transition takes when the sights start coming up. Short
    // enough to be done before there is a sight picture to look through - a
    // weapon's own raise animation is around a fifth of a second - and long
    // enough that the eye eases onto the sights rather than snapping to them.
    static constexpr unsigned long long kLowerMs = 150;
    // And back when they drop. Longer, because nothing is waiting on it and a
    // slower return is the more comfortable half.
    static constexpr unsigned long long kRaiseMs = 250;

    // Called once per rendered frame, before the lean is applied. `aiming`
    // is the ADS state for this frame, polled rather than latched. Returns the
    // scale to blend at: 1 at the hip, 0 with the sights up.
    float Update(bool aiming, unsigned long long nowMs) {
        const bool turnDown = aiming && (m_state == State::Hip || m_state == State::Raising);
        const bool turnUp = !aiming && (m_state == State::Lowering || m_state == State::Aiming);

        if (turnDown || turnUp) {
            // A reversal starts from WHERE THE TRANSITION IS, not from the end
            // the interrupted leg would have reached. Starting each leg at its
            // own endpoint steps the lean by however far the previous one had
            // travelled, and the worst case is the most common input there is:
            // a tap of the aim button releases a frame after it was pressed, so
            // the lean is 99.99% applied and the next frame removes all of it.
            // That is the jolt this class exists to remove, delivered by the
            // class itself.
            const float from = Current(nowMs);
            const float target = turnDown ? 0.0f : 1.0f;
            const float distance = target > from ? target - from : from - target;
            if (distance < kSettled) {
                m_state = turnDown ? State::Aiming : State::Hip;
                return target;
            }
            m_state = turnDown ? State::Lowering : State::Raising;
            m_from = from;
            m_target = target;
            // Scaled by the distance left to travel, so an interrupted leg
            // moves at the same RATE as a whole one rather than taking the full
            // time to cover a fraction of the distance.
            const float full = static_cast<float>(turnDown ? kLowerMs : kRaiseMs);
            m_durationMs = static_cast<unsigned long long>(full * distance);
            if (m_durationMs == 0) m_durationMs = 1;
            m_startMs = nowMs;
        }

        const float scale = Current(nowMs);
        if ((m_state == State::Lowering || m_state == State::Raising)
                && Elapsed(nowMs) >= m_durationMs) {
            m_state = (m_target == 0.0f) ? State::Aiming : State::Hip;
        }
        return scale;
    }

    // Drops back to hip state. Call wherever tracking is suppressed - menu,
    // loading, cinematic, master toggle, tracker dropout - so the next aim
    // starts clean.
    void Reset() {
        m_state = State::Hip;
        m_from = 1.0f;
        m_target = 1.0f;
    }

private:
    enum class State { Hip, Lowering, Aiming, Raising };

    // Below this the two ends of a leg are the same place and there is nothing
    // to travel.
    static constexpr float kSettled = 1e-6f;

    // Clamped at zero rather than allowed to wrap. nowMs is the caller's clock
    // and an unsigned subtraction across a clock that stepped backwards yields
    // an enormous elapsed, which settles the transition instantly - a snap, in
    // the one place this class exists to prevent one.
    unsigned long long Elapsed(unsigned long long nowMs) const {
        return nowMs > m_startMs ? nowMs - m_startMs : 0;
    }

    // Where the transition is right now, without advancing it.
    float Current(unsigned long long nowMs) const {
        if (m_state == State::Hip) return 1.0f;
        if (m_state == State::Aiming) return 0.0f;
        const unsigned long long elapsed = Elapsed(nowMs);
        if (elapsed >= m_durationMs) return m_target;
        return m_from + (m_target - m_from) * Ease(elapsed, m_durationMs);
    }

    // Smoothstep, so the transition leaves and arrives at rest instead of
    // starting and stopping with a visible corner.
    static float Ease(unsigned long long elapsed, unsigned long long duration) {
        const float t = static_cast<float>(elapsed) / static_cast<float>(duration);
        return t * t * (3.0f - 2.0f * t);
    }

    State m_state = State::Hip;
    unsigned long long m_startMs = 0;
    unsigned long long m_durationMs = kLowerMs;
    // The scale the current leg started from and is heading to. Held rather
    // than assumed, because a leg can start anywhere: see Update().
    float m_from = 1.0f;
    float m_target = 1.0f;
};

}  // namespace cameraunlock::ads
