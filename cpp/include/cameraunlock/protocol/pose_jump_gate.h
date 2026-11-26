#pragma once

#include <cmath>

namespace cameraunlock {

/// Decides whether a freshly arrived pose is head tracking or the tracker having
/// stopped tracking.
///
/// A head tracker that loses the head does not say so - it just starts repeating
/// one pose, usually centred. Followed faithfully, that swings the view from
/// wherever the head was to centre and back every time tracking blinks, which is
/// the single most visible failure a mod can have and is indistinguishable
/// from a camera bug. Measured against a simulated eye-tracker dropout, each
/// 200 ms blink moved the view 17 to 25 degrees.
///
/// The tell is that the repeat is BIT-IDENTICAL. Real sensor output always
/// jitters, so a value that arrives twice unchanged is not a measurement. That
/// cannot be known until the repeat arrives, so a large jump is held back for one
/// packet and only accepted once the following packet DIFFERS from it. Small
/// changes - ordinary head movement - are never delayed.
///
/// One gate per receiver, fed every parsed pose of the stream in arrival order,
/// on the thread that receives. CameraUnlock.Core.Protocol.PoseJumpGate is the C#
/// twin, and data/fixtures/pose-jump-gate/cases.tsv holds both to the same answers.
class PoseJumpGate {
public:
    /// The FIRST pose change larger than this is held back one packet and only
    /// accepted if the next packet differs from it.
    ///
    /// Note what this measures: degrees per PACKET, not per second, so what
    /// counts as large depends entirely on the tracker's sample rate. 300 deg/s
    /// is 5 degrees a packet at 60 Hz but 9 at 33 Hz, and eye trackers commonly
    /// run at the low end - so ordinary movement DOES cross this on real
    /// hardware. That is survivable only because a continuing movement is never
    /// held (see m_lastStepWasLarge); holding every large step rejects alternate
    /// packets and publishes a pose that alternates between current and stale.
    /// Ordinary head movement between packets is far smaller; a tracker snapping
    /// to its "lost the head" pose is far larger, and the packet after it
    /// corroborates the snap rather than continuing the movement.
    static constexpr float kConfirmJumpDegrees = 8.0f;

    /// True when the pose is to be published, false when it is held back or is a
    /// lost tracker repeating itself.
    bool Accept(float yaw, float pitch, float roll) {
        const bool repeatsPrevious = Observe(yaw, pitch, roll);

        if (!m_hasAccepted) {
            Seed(yaw, pitch, roll);
            return true;
        }

        if (repeatsPrevious) {
            // A resent duplicate is harmless - it is the value already published, so
            // publishing it again changes nothing. Refusing them would fight every
            // tracker app that resends faster than its sensor updates, which is most
            // of them, and that fight would itself look like jitter.
            //
            // The one case that matters is a repeat of a jump still awaiting
            // confirmation: a source that has stopped tracking repeats its "lost"
            // pose exactly, so the jump toward it was never a head movement. Keep
            // holding the last pose the head was actually in.
            if (m_pending) {
                // The source has stopped moving, so whatever movement preceded this
                // is over: the next real step starts a new movement and gets the
                // confirmation hold again.
                m_lastStepWasLarge = false;
                return false;
            }
            return true;
        }

        const float jump = LargestAxisChange(yaw, pitch, roll);

        // Hold back the FIRST large step of a movement, never a continuing one.
        //
        // The `m_lastStepWasLarge` half is not a refinement, it is the whole
        // correctness of this gate. Without it, sustained fast movement is rejected
        // on every OTHER packet: one is held, the next is accepted, the one after
        // that is a large step again from the newly accepted pose, and so on. The
        // published pose then alternates between current and one packet stale for as
        // long as the head keeps moving, at half the tracker's rate - which is
        // exactly "the view flickers between two poses", and it appears only while
        // the head is moving, so a held test pose never shows it.
        //
        // Rejecting a packet also leaves the accepted pose behind, so the next step
        // measures even larger and the gate is more certain to trip again. It
        // self-sustains.
        //
        // A dropout is still caught at any sample rate, because the thing that
        // identifies one is not the size of the jump but that the pose STOPS moving
        // afterwards - which the repeat branch above tests directly.
        if (jump > kConfirmJumpDegrees && !m_pending && !m_lastStepWasLarge) {
            m_pending = true;
            return false;
        }

        m_pending = false;
        m_lastStepWasLarge = jump > kConfirmJumpDegrees;
        m_acceptedYaw = yaw;
        m_acceptedPitch = pitch;
        m_acceptedRoll = roll;
        return true;
    }

    /// Takes a pose the tracker announced as a deliberate discontinuity (a CENTER
    /// press carried in the packet's trailer) as the truth the next one is measured
    /// against, discarding any jump still held for confirmation. The caller
    /// publishes it.
    void Announce(float yaw, float pitch, float roll) {
        Observe(yaw, pitch, roll);
        Seed(yaw, pitch, roll);
    }

    /// Forgets the stream, for a receiver that has stopped.
    void Reset() { *this = PoseJumpGate(); }

private:
    // Whether the pose is bit for bit the one the stream carried last.
    bool Observe(float yaw, float pitch, float roll) {
        const bool repeats = yaw == m_lastYaw && pitch == m_lastPitch && roll == m_lastRoll;
        if (!repeats) {
            m_lastYaw = yaw;
            m_lastPitch = pitch;
            m_lastRoll = roll;
        }
        return repeats;
    }

    void Seed(float yaw, float pitch, float roll) {
        m_hasAccepted = true;
        m_pending = false;
        m_lastStepWasLarge = false;
        m_acceptedYaw = yaw;
        m_acceptedPitch = pitch;
        m_acceptedRoll = roll;
    }

    float LargestAxisChange(float yaw, float pitch, float roll) const {
        const float dy = std::fabs(yaw - m_acceptedYaw);
        const float dp = std::fabs(pitch - m_acceptedPitch);
        const float dr = std::fabs(roll - m_acceptedRoll);
        const float worst = dy > dp ? dy : dp;
        return worst > dr ? worst : dr;
    }

    float m_lastYaw = 0.0f;
    float m_lastPitch = 0.0f;
    float m_lastRoll = 0.0f;

    /// The last pose published.
    float m_acceptedYaw = 0.0f;
    float m_acceptedPitch = 0.0f;
    float m_acceptedRoll = 0.0f;
    bool m_hasAccepted = false;
    /// A large jump is waiting for a following packet that differs from it.
    bool m_pending = false;
    /// The last accepted step was itself larger than the threshold, so the head is
    /// mid-movement rather than starting one.
    bool m_lastStepWasLarge = false;
};

}  // namespace cameraunlock
