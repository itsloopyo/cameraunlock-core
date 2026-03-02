using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Fills in frames between low-rate tracking samples using velocity extrapolation.
    /// Sits between the receiver and processor in the pipeline:
    /// Raw Pose (30Hz) → PoseInterpolator → TrackingProcessor → Camera
    /// </summary>
    public sealed class PoseInterpolator
    {
        /// <summary>
        /// Maximum time (seconds) to extrapolate beyond the last sample.
        /// Prevents runaway drift when tracking is lost or stalled.
        /// </summary>
        public float MaxExtrapolationTime { get; set; } = 0.1f;

        // Velocity EMA blend factor. 0.5 balances responsiveness with jitter filtering.
        private const float VelocityBlend = 0.5f;

        // Previous sample state
        private long _prevTimestampTicks;
        private float _prevYaw;
        private float _prevPitch;
        private float _prevRoll;

        // Last accepted sample (used as extrapolation base)
        private long _lastTimestampTicks;
        private float _lastYaw;
        private float _lastPitch;
        private float _lastRoll;

        // Smoothed velocity (degrees per second)
        private float _velocityYaw;
        private float _velocityPitch;
        private float _velocityRoll;
        private bool _hasVelocity;

        // Accumulated time since last new sample
        private float _timeSinceLastSample;

        private bool _hasAnySample;

        /// <summary>
        /// Update with the latest raw pose and frame delta time.
        /// Returns an interpolated (extrapolated) pose suitable for feeding into TrackingProcessor.
        /// </summary>
        public TrackingPose Update(TrackingPose rawPose, float deltaTime)
        {
            if (!rawPose.IsValid)
            {
                return rawPose;
            }

            bool isNewSample = rawPose.TimestampTicks != _lastTimestampTicks;

            if (isNewSample)
            {
                if (_hasAnySample)
                {
                    // Compute time delta between this sample and the previous one
                    float sampleDt = _timeSinceLastSample;
                    if (sampleDt > 0f)
                    {
                        // Instantaneous velocity from sample delta
                        float instVelYaw = (rawPose.Yaw - _lastYaw) / sampleDt;
                        float instVelPitch = (rawPose.Pitch - _lastPitch) / sampleDt;
                        float instVelRoll = (rawPose.Roll - _lastRoll) / sampleDt;

                        if (_hasVelocity)
                        {
                            // EMA smooth the velocity estimate
                            _velocityYaw = _velocityYaw + (instVelYaw - _velocityYaw) * VelocityBlend;
                            _velocityPitch = _velocityPitch + (instVelPitch - _velocityPitch) * VelocityBlend;
                            _velocityRoll = _velocityRoll + (instVelRoll - _velocityRoll) * VelocityBlend;
                        }
                        else
                        {
                            _velocityYaw = instVelYaw;
                            _velocityPitch = instVelPitch;
                            _velocityRoll = instVelRoll;
                            _hasVelocity = true;
                        }
                    }
                }

                // Store as the new base sample
                _prevTimestampTicks = _lastTimestampTicks;
                _prevYaw = _lastYaw;
                _prevPitch = _lastPitch;
                _prevRoll = _lastRoll;

                _lastTimestampTicks = rawPose.TimestampTicks;
                _lastYaw = rawPose.Yaw;
                _lastPitch = rawPose.Pitch;
                _lastRoll = rawPose.Roll;

                _timeSinceLastSample = 0f;
                _hasAnySample = true;

                // New sample just arrived — return it directly (no extrapolation needed)
                return rawPose;
            }

            // No new sample this frame — extrapolate if we have velocity
            _timeSinceLastSample += deltaTime;

            if (!_hasVelocity)
            {
                return rawPose;
            }

            // Cap extrapolation time to prevent drift
            float extrapTime = _timeSinceLastSample;
            if (extrapTime > MaxExtrapolationTime)
            {
                extrapTime = MaxExtrapolationTime;
            }

            // Decay factor: velocity influence fades as we get further from the last sample.
            // At t=0 decay=1 (full velocity), at t=MaxExtrapolationTime decay≈0.37.
            // This prevents overshoot when the head stops moving.
            float decay = 1f;
            if (MaxExtrapolationTime > 0f)
            {
                // Approximation of exp(-t/maxT) using 1/(1+t/maxT)^2 — cheap, no System.Math needed
                float ratio = extrapTime / MaxExtrapolationTime;
                float denom = 1f + ratio;
                decay = 1f / (denom * denom);
            }

            float predYaw = _lastYaw + _velocityYaw * extrapTime * decay;
            float predPitch = _lastPitch + _velocityPitch * extrapTime * decay;
            float predRoll = _lastRoll + _velocityRoll * extrapTime * decay;

            return new TrackingPose(predYaw, predPitch, predRoll, rawPose.TimestampTicks);
        }

        /// <summary>
        /// Resets all interpolation state. Call on recenter, scene transitions, or tracking re-enable.
        /// </summary>
        public void Reset()
        {
            _prevTimestampTicks = 0;
            _prevYaw = 0f;
            _prevPitch = 0f;
            _prevRoll = 0f;

            _lastTimestampTicks = 0;
            _lastYaw = 0f;
            _lastPitch = 0f;
            _lastRoll = 0f;

            _velocityYaw = 0f;
            _velocityPitch = 0f;
            _velocityRoll = 0f;
            _hasVelocity = false;

            _timeSinceLastSample = 0f;
            _hasAnySample = false;
        }
    }
}
