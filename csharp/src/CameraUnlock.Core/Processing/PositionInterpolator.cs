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

        /// <summary>
        /// Mirrors PoseInterpolator.ExtrapolationHoldSeconds.
        /// </summary>
        public const float ExtrapolationHoldSeconds = 0.25f;

        /// <summary>
        /// Mirrors PoseInterpolator.ExtrapolationDecaySeconds.
        /// </summary>
        public const float ExtrapolationDecaySeconds = 0.35f;

        /// <summary>
        /// Segment position to sample at, given progress and how long the next
        /// sample has been outstanding. Mirrors PoseInterpolator.SegmentPosition -
        /// see there for why the extrapolation expires on a wall clock rather
        /// than parking on the overshoot, and why a dropped packet must not
        /// trigger it.
        /// </summary>
        public float SegmentPosition(float progress, float timeSinceLastSample)
        {
            if (progress < 0f) return 0f;
            float maxPt = 1f + MaxExtrapolationFraction;
            float pt = progress > maxPt ? maxPt : progress;
            if (timeSinceLastSample <= ExtrapolationHoldSeconds) return pt;

            float late = timeSinceLastSample - ExtrapolationHoldSeconds;
            float u = late / ExtrapolationDecaySeconds;
            if (u > 1f) u = 1f;
            float eased = u * u * (3f - 2f * u);
            return pt + (1f - pt) * eased;
        }

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
            return Update(rawPosition, rawPosition.TimestampTicks != _lastTimestampTicks, deltaTime);
        }

        /// <summary>
        /// Update with an explicit new-sample flag. See
        /// <see cref="PoseInterpolator.Update(Data.TrackingPose, bool, float)"/> for why the
        /// timestamp alone is not enough. Matches PositionInterpolator::Update in C++.
        /// </summary>
        public PositionData Update(PositionData rawPosition, bool isNewSample, float deltaTime)
        {
            if (!rawPosition.IsValid)
            {
                return rawPosition;
            }

            _timeSinceLastNewSample += deltaTime;

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

                // See PoseInterpolator for why this is NOT gated on <= MaxSampleInterval.
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

                float t = SegmentPosition(_progress, _timeSinceLastNewSample);
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

            float pt = SegmentPosition(_progress, _timeSinceLastNewSample);

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
