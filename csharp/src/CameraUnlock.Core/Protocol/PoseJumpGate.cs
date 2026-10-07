using System;

namespace CameraUnlock.Core.Protocol
{
    /// <summary>
    /// Decides whether a freshly arrived pose is head tracking or the tracker having
    /// stopped tracking. The C# twin of cameraunlock::PoseJumpGate
    /// (cpp/include/cameraunlock/protocol/pose_jump_gate.h), which carries the reasoning
    /// and the measurement behind it. data/fixtures/pose-jump-gate/cases.tsv holds both
    /// to the same answers.
    /// <para>
    /// A head tracker that loses the head does not say so. It starts repeating one pose,
    /// usually centred, and followed faithfully that swings the view to centre and back
    /// every time tracking blinks. The tell is that the repeat is bit-identical, which
    /// cannot be known until the repeat arrives, so a large jump is held back for one
    /// packet and accepted only once the following packet differs from it.
    /// </para>
    /// <para>
    /// One gate per receiver, fed every parsed pose of the stream in arrival order, on
    /// the thread that receives.
    /// </para>
    /// </summary>
    public sealed class PoseJumpGate
    {
        /// <summary>
        /// The first pose change larger than this, in degrees per packet on any one axis,
        /// is held back one packet. A continuing movement is never held.
        /// </summary>
        public const float ConfirmJumpDegrees = 8.0f;

        private float _lastYaw;
        private float _lastPitch;
        private float _lastRoll;

        private float _acceptedYaw;
        private float _acceptedPitch;
        private float _acceptedRoll;
        private bool _hasAccepted;
        private bool _pending;
        // The last accepted step was itself large, so the head is mid-movement. Holding
        // every large step rejects alternate packets for as long as a fast movement
        // lasts, and the published pose then flickers between current and stale.
        private bool _lastStepWasLarge;

        /// <summary>
        /// True when the pose is to be published, false when it is held back or is a lost
        /// tracker repeating itself.
        /// </summary>
        public bool Accept(float yaw, float pitch, float roll)
        {
            bool repeatsPrevious = Observe(yaw, pitch, roll);

            if (!_hasAccepted)
            {
                Seed(yaw, pitch, roll);
                return true;
            }

            if (repeatsPrevious)
            {
                // A resent duplicate of the published pose changes nothing. A repeat of a
                // jump still held is the lost tracker repeating itself.
                if (_pending)
                {
                    _lastStepWasLarge = false;
                    return false;
                }
                return true;
            }

            float jump = System.Math.Max(System.Math.Abs(yaw - _acceptedYaw),
                System.Math.Max(System.Math.Abs(pitch - _acceptedPitch), System.Math.Abs(roll - _acceptedRoll)));

            if (jump > ConfirmJumpDegrees && !_pending && !_lastStepWasLarge)
            {
                _pending = true;
                return false;
            }

            _pending = false;
            _lastStepWasLarge = jump > ConfirmJumpDegrees;
            _acceptedYaw = yaw;
            _acceptedPitch = pitch;
            _acceptedRoll = roll;
            return true;
        }

        /// <summary>
        /// Takes a pose the tracker announced as a deliberate discontinuity (a CENTER press
        /// carried in the packet's trailer) as the truth the next one is measured against,
        /// discarding any jump still held. The caller publishes it.
        /// </summary>
        public void Announce(float yaw, float pitch, float roll)
        {
            Observe(yaw, pitch, roll);
            Seed(yaw, pitch, roll);
        }

        /// <summary>Forgets the stream, for a receiver that has stopped.</summary>
        public void Reset()
        {
            _lastYaw = 0f;
            _lastPitch = 0f;
            _lastRoll = 0f;
            _acceptedYaw = 0f;
            _acceptedPitch = 0f;
            _acceptedRoll = 0f;
            _hasAccepted = false;
            _pending = false;
            _lastStepWasLarge = false;
        }

        // Whether the pose is bit for bit the one the stream carried last.
        private bool Observe(float yaw, float pitch, float roll)
        {
            bool repeats = yaw == _lastYaw && pitch == _lastPitch && roll == _lastRoll;
            if (!repeats)
            {
                _lastYaw = yaw;
                _lastPitch = pitch;
                _lastRoll = roll;
            }
            return repeats;
        }

        private void Seed(float yaw, float pitch, float roll)
        {
            _hasAccepted = true;
            _pending = false;
            _lastStepWasLarge = false;
            _acceptedYaw = yaw;
            _acceptedPitch = pitch;
            _acceptedRoll = roll;
        }
    }
}
