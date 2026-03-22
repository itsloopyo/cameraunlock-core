using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Fills in frames between low-rate tracking samples using linear interpolation.
    /// Buffers one sample and lerps between the previous and current known positions,
    /// trading one sample period of latency (~33ms at 30Hz) for guaranteed smooth output.
    /// Sits between the receiver and processor in the pipeline:
    /// Raw Pose (30Hz) → PoseInterpolator → TrackingProcessor → Camera
    /// </summary>
    public sealed class PoseInterpolator
    {
        /// <summary>
        /// Kept for API compatibility. No longer controls behavior.
        /// </summary>
        public float MaxExtrapolationTime { get; set; } = 0.1f;

        // EMA blend factor for sample interval estimation
        private const float IntervalBlend = 0.3f;

        // Assumed interval until we observe real samples (30Hz)
        private const float DefaultSampleInterval = 1f / 30f;

        // Bounds for sample interval estimate
        private const float MinSampleInterval = 0.001f;
        private const float MaxSampleInterval = 0.2f;

        // Interpolation start point (where we're coming from)
        private float _fromYaw, _fromPitch, _fromRoll;

        // Interpolation target (latest known sample)
        private float _toYaw, _toPitch, _toRoll;

        // Last seen timestamp (for new-sample detection)
        private long _lastTimestampTicks;

        // Interpolation progress within current segment
        private float _progress;

        // EMA-smoothed estimate of time between tracker samples
        private float _sampleInterval = DefaultSampleInterval;

        // Accumulated wall time since last new sample arrived
        private float _timeSinceLastNewSample;

        private bool _hasFirstSample;
        private bool _hasSecondSample;

        /// <summary>
        /// Update with the latest raw pose and frame delta time.
        /// Returns a smoothly interpolated pose suitable for feeding into TrackingProcessor.
        /// </summary>
        public TrackingPose Update(TrackingPose rawPose, float deltaTime)
        {
            if (!rawPose.IsValid)
            {
                return rawPose;
            }

            _timeSinceLastNewSample += deltaTime;

            bool isNewSample = rawPose.TimestampTicks != _lastTimestampTicks;

            if (isNewSample)
            {
                if (!_hasFirstSample)
                {
                    // Very first sample — park at this position
                    _fromYaw = rawPose.Yaw;
                    _fromPitch = rawPose.Pitch;
                    _fromRoll = rawPose.Roll;
                    _toYaw = rawPose.Yaw;
                    _toPitch = rawPose.Pitch;
                    _toRoll = rawPose.Roll;
                    _lastTimestampTicks = rawPose.TimestampTicks;
                    _progress = 1f;
                    _timeSinceLastNewSample = 0f;
                    _hasFirstSample = true;
                    return rawPose;
                }

                // Update sample interval estimate (EMA)
                if (_timeSinceLastNewSample > MinSampleInterval)
                {
                    if (!_hasSecondSample)
                    {
                        _sampleInterval = _timeSinceLastNewSample;
                        _hasSecondSample = true;
                    }
                    else
                    {
                        _sampleInterval += (_timeSinceLastNewSample - _sampleInterval) * IntervalBlend;
                    }

                    if (_sampleInterval < MinSampleInterval) _sampleInterval = MinSampleInterval;
                    if (_sampleInterval > MaxSampleInterval) _sampleInterval = MaxSampleInterval;
                }

                // Capture current interpolated position as new start point
                float t = _progress > 1f ? 1f : _progress;
                _fromYaw = _fromYaw + (_toYaw - _fromYaw) * t;
                _fromPitch = _fromPitch + (_toPitch - _fromPitch) * t;
                _fromRoll = _fromRoll + (_toRoll - _fromRoll) * t;

                // New sample becomes the target
                _toYaw = rawPose.Yaw;
                _toPitch = rawPose.Pitch;
                _toRoll = rawPose.Roll;
                _lastTimestampTicks = rawPose.TimestampTicks;

                _progress = 0f;
                _timeSinceLastNewSample = 0f;
            }

            // Advance interpolation
            _progress += deltaTime / _sampleInterval;

            // Clamp for output — hold at target when waiting for next sample
            float pt = _progress > 1f ? 1f : (_progress < 0f ? 0f : _progress);

            float outYaw = _fromYaw + (_toYaw - _fromYaw) * pt;
            float outPitch = _fromPitch + (_toPitch - _fromPitch) * pt;
            float outRoll = _fromRoll + (_toRoll - _fromRoll) * pt;

            return new TrackingPose(outYaw, outPitch, outRoll, rawPose.TimestampTicks);
        }

        /// <summary>
        /// Resets all interpolation state. Call on recenter, scene transitions, or tracking re-enable.
        /// </summary>
        public void Reset()
        {
            _fromYaw = 0f;
            _fromPitch = 0f;
            _fromRoll = 0f;

            _toYaw = 0f;
            _toPitch = 0f;
            _toRoll = 0f;

            _lastTimestampTicks = 0;
            _progress = 0f;
            _sampleInterval = DefaultSampleInterval;
            _timeSinceLastNewSample = 0f;
            _hasFirstSample = false;
            _hasSecondSample = false;
        }
    }
}
