using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Fills in frames between low-rate position samples using velocity extrapolation.
    /// Mirrors PoseInterpolator pattern, operating on PositionData (X/Y/Z) instead of TrackingPose (Yaw/Pitch/Roll).
    /// </summary>
    public sealed class PositionInterpolator
    {
        /// <summary>
        /// Maximum time (seconds) to extrapolate beyond the last sample.
        /// </summary>
        public float MaxExtrapolationTime { get; set; } = 0.1f;

        private const float VelocityBlend = 0.5f;

        // Previous sample state
        private long _prevTimestampTicks;
        private float _prevX;
        private float _prevY;
        private float _prevZ;

        // Last accepted sample (extrapolation base)
        private long _lastTimestampTicks;
        private float _lastX;
        private float _lastY;
        private float _lastZ;

        // Smoothed velocity (meters per second)
        private float _velocityX;
        private float _velocityY;
        private float _velocityZ;
        private bool _hasVelocity;

        // Accumulated time since last new sample
        private float _timeSinceLastSample;

        private bool _hasAnySample;

        /// <summary>
        /// Update with the latest raw position and frame delta time.
        /// Returns an interpolated (extrapolated) position.
        /// </summary>
        public PositionData Update(PositionData rawPosition, float deltaTime)
        {
            if (!rawPosition.IsValid)
            {
                return rawPosition;
            }

            bool isNewSample = rawPosition.TimestampTicks != _lastTimestampTicks;

            if (isNewSample)
            {
                if (_hasAnySample)
                {
                    float sampleDt = _timeSinceLastSample;
                    if (sampleDt > 0f)
                    {
                        float instVelX = (rawPosition.X - _lastX) / sampleDt;
                        float instVelY = (rawPosition.Y - _lastY) / sampleDt;
                        float instVelZ = (rawPosition.Z - _lastZ) / sampleDt;

                        if (_hasVelocity)
                        {
                            _velocityX = _velocityX + (instVelX - _velocityX) * VelocityBlend;
                            _velocityY = _velocityY + (instVelY - _velocityY) * VelocityBlend;
                            _velocityZ = _velocityZ + (instVelZ - _velocityZ) * VelocityBlend;
                        }
                        else
                        {
                            _velocityX = instVelX;
                            _velocityY = instVelY;
                            _velocityZ = instVelZ;
                            _hasVelocity = true;
                        }
                    }
                }

                _prevTimestampTicks = _lastTimestampTicks;
                _prevX = _lastX;
                _prevY = _lastY;
                _prevZ = _lastZ;

                _lastTimestampTicks = rawPosition.TimestampTicks;
                _lastX = rawPosition.X;
                _lastY = rawPosition.Y;
                _lastZ = rawPosition.Z;

                _timeSinceLastSample = 0f;
                _hasAnySample = true;

                return rawPosition;
            }

            // No new sample this frame — extrapolate if we have velocity
            _timeSinceLastSample += deltaTime;

            if (!_hasVelocity)
            {
                return rawPosition;
            }

            float extrapTime = _timeSinceLastSample;
            if (extrapTime > MaxExtrapolationTime)
            {
                extrapTime = MaxExtrapolationTime;
            }

            // Decay factor: velocity influence fades as we get further from the last sample
            float decay = 1f;
            if (MaxExtrapolationTime > 0f)
            {
                float ratio = extrapTime / MaxExtrapolationTime;
                float denom = 1f + ratio;
                decay = 1f / (denom * denom);
            }

            float predX = _lastX + _velocityX * extrapTime * decay;
            float predY = _lastY + _velocityY * extrapTime * decay;
            float predZ = _lastZ + _velocityZ * extrapTime * decay;

            return new PositionData(predX, predY, predZ, rawPosition.TimestampTicks);
        }

        /// <summary>
        /// Resets all interpolation state.
        /// </summary>
        public void Reset()
        {
            _prevTimestampTicks = 0;
            _prevX = 0f;
            _prevY = 0f;
            _prevZ = 0f;

            _lastTimestampTicks = 0;
            _lastX = 0f;
            _lastY = 0f;
            _lastZ = 0f;

            _velocityX = 0f;
            _velocityY = 0f;
            _velocityZ = 0f;
            _hasVelocity = false;

            _timeSinceLastSample = 0f;
            _hasAnySample = false;
        }
    }
}
