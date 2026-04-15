using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Fills in frames between low-rate position samples using linear interpolation.
    /// Mirrors PoseInterpolator pattern, operating on PositionData (X/Y/Z) instead of TrackingPose (Yaw/Pitch/Roll).
    /// </summary>
    public sealed class PositionInterpolator
    {
        /// <summary>
        /// Maximum extrapolation past the target, as a fraction of the estimated
        /// sample interval. Mirrors PoseInterpolator.MaxExtrapolationFraction.
        /// </summary>
        public float MaxExtrapolationFraction { get; set; } = 0.5f;

        private const float IntervalBlend = 0.3f;
        private const float DefaultSampleInterval = 1f / 30f;
        private const float MinSampleInterval = 0.001f;
        private const float MaxSampleInterval = 0.2f;

        private float _fromX, _fromY, _fromZ;
        private float _toX, _toY, _toZ;
        private long _lastTimestampTicks;

        private float _progress;
        private float _sampleInterval = DefaultSampleInterval;
        private float _timeSinceLastNewSample;

        private bool _hasFirstSample;
        private bool _hasSecondSample;

        /// <summary>
        /// Update with the latest raw position and frame delta time.
        /// Returns a smoothly interpolated position.
        /// </summary>
        public PositionData Update(PositionData rawPosition, float deltaTime)
        {
            if (!rawPosition.IsValid)
            {
                return rawPosition;
            }

            _timeSinceLastNewSample += deltaTime;

            bool isNewSample = rawPosition.TimestampTicks != _lastTimestampTicks;

            if (isNewSample)
            {
                if (!_hasFirstSample)
                {
                    _fromX = rawPosition.X;
                    _fromY = rawPosition.Y;
                    _fromZ = rawPosition.Z;
                    _toX = rawPosition.X;
                    _toY = rawPosition.Y;
                    _toZ = rawPosition.Z;
                    _lastTimestampTicks = rawPosition.TimestampTicks;
                    _progress = 1f;
                    _timeSinceLastNewSample = 0f;
                    _hasFirstSample = true;
                    return rawPosition;
                }

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

                float maxP = 1f + MaxExtrapolationFraction;
                float t = _progress < 0f ? 0f : (_progress > maxP ? maxP : _progress);
                _fromX = _fromX + (_toX - _fromX) * t;
                _fromY = _fromY + (_toY - _fromY) * t;
                _fromZ = _fromZ + (_toZ - _fromZ) * t;

                _toX = rawPosition.X;
                _toY = rawPosition.Y;
                _toZ = rawPosition.Z;
                _lastTimestampTicks = rawPosition.TimestampTicks;

                _progress = 0f;
                _timeSinceLastNewSample = 0f;
            }

            _progress += deltaTime / _sampleInterval;

            float maxPt = 1f + MaxExtrapolationFraction;
            float pt = _progress > maxPt ? maxPt : (_progress < 0f ? 0f : _progress);

            float outX = _fromX + (_toX - _fromX) * pt;
            float outY = _fromY + (_toY - _fromY) * pt;
            float outZ = _fromZ + (_toZ - _fromZ) * pt;

            return new PositionData(outX, outY, outZ, rawPosition.TimestampTicks);
        }

        /// <summary>
        /// Resets all interpolation state.
        /// </summary>
        public void Reset()
        {
            _fromX = 0f;
            _fromY = 0f;
            _fromZ = 0f;

            _toX = 0f;
            _toY = 0f;
            _toZ = 0f;

            _lastTimestampTicks = 0;
            _progress = 0f;
            _sampleInterval = DefaultSampleInterval;
            _timeSinceLastNewSample = 0f;
            _hasFirstSample = false;
            _hasSecondSample = false;
        }
    }
}
