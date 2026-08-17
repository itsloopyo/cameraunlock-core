using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

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
        /// Maximum extrapolation past the target, as a fraction of the estimated
        /// sample interval. 0.5 = continue the last velocity for up to half a
        /// sample period beyond the latest known position.  This eliminates the
        /// velocity-drops-to-zero flat spots that make 60 Hz tracking look choppy
        /// on high-refresh displays (e.g. 240 Hz).
        /// </summary>
        public float MaxExtrapolationFraction { get; set; } = 0.5f;

        /// <summary>
        /// Seconds a sample may be late before the extrapolation starts expiring.
        /// Sized to outlast an ordinary Wi-Fi loss burst (50-200 ms): a dropped
        /// packet or two is a live feed and must behave as it always did -
        /// continue the prediction, then hold. Retreating on a dropped packet
        /// would pull the camera backwards against a head that is still turning.
        /// </summary>
        public const float ExtrapolationHoldSeconds = 0.25f;

        /// <summary>
        /// Seconds over which a genuinely stalled feed converges back to the
        /// last reported sample, so the correction is a drift and not a snap.
        /// </summary>
        public const float ExtrapolationDecaySeconds = 0.35f;

        /// <summary>
        /// Segment position to sample at, given interpolation progress and how
        /// long the next sample has been outstanding.
        /// <para>
        /// Progress past 1.0 is extrapolation - a short prediction that keeps
        /// velocity continuous between samples. Clamping it and then HOLDING
        /// parks the output at 1.5x the last reported pose for as long as new
        /// samples fail to arrive: a tracker app streaming its last value while
        /// the face is lost, or a silent socket inside the freshness window. A
        /// 25 degree head turn then renders as 37.5 degrees.
        /// </para>
        /// <para>
        /// So the prediction expires, on a WALL CLOCK rather than on progress -
        /// progress is measured in units of a sample-interval estimate that is
        /// stale by construction in the stall case. Below the hold threshold
        /// this is bit-for-bit the old behaviour; past it the segment position
        /// eases (smoothstep, so no velocity step at either end) to 1.0, the
        /// pose the tracker actually reported.
        /// </para>
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

                // Update sample interval estimate (EMA). A packet-loss gap is not an
                // observation of the tracker's rate, so it is rejected rather than clamped
                // into the estimate - folding a 0.5s stall in at MaxSampleInterval drags the
                // estimate up and leaves the camera lagging for ~12 samples after recovery.
                if (_timeSinceLastNewSample > MinSampleInterval &&
                    _timeSinceLastNewSample <= MaxSampleInterval)
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

                // Capture current interpolated (possibly extrapolated) position as new start point
                // Yaw and roll are (-180, 180] and can step across the seam, so the segment
                // is traversed along the shortest arc; a plain (to - from) turns a 1° move
                // from 179.5 to -179.5 into a -359° sweep the long way round. Pitch is
                // bounded to ±90 by asin and never wraps.
                float t = SegmentPosition(_progress, _timeSinceLastNewSample);
                _fromYaw = AngleUtils.NormalizeAngle(_fromYaw + AngleUtils.ShortestAngleDelta(_fromYaw, _toYaw) * t);
                _fromPitch = _fromPitch + (_toPitch - _fromPitch) * t;
                _fromRoll = AngleUtils.NormalizeAngle(_fromRoll + AngleUtils.ShortestAngleDelta(_fromRoll, _toRoll) * t);

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

            // Allow extrapolation past 1.0 to maintain velocity continuity,
            // bounded to avoid runaway prediction on direction reversals.
            float pt = SegmentPosition(_progress, _timeSinceLastNewSample);

            float outYaw = AngleUtils.NormalizeAngle(_fromYaw + AngleUtils.ShortestAngleDelta(_fromYaw, _toYaw) * pt);
            float outPitch = _fromPitch + (_toPitch - _fromPitch) * pt;
            float outRoll = AngleUtils.NormalizeAngle(_fromRoll + AngleUtils.ShortestAngleDelta(_fromRoll, _toRoll) * pt);

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
