using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Complete positional tracking data processing pipeline.
    /// Pipeline: raw position → center subtraction → tracker pivot compensation → sensitivity/inversion → smoothing → add neck model → box clamp
    /// </summary>
    public sealed class PositionProcessor
    {
        private Vec3 _center;
        private Vec3 _smoothedPosition;
        private bool _hasSmoothedValue;

        /// <summary>
        /// Position settings (sensitivity, limits, smoothing, inversion).
        /// </summary>
        public PositionSettings Settings { get; set; } = PositionSettings.Default;

        /// <summary>
        /// Neck model settings.
        /// </summary>
        public NeckModelSettings NeckModelSettings { get; set; } = NeckModelSettings.Default;

        /// <summary>
        /// Forward distance (meters) of the tracker's face tracking point from the
        /// neck/rotation pivot. When the head rotates, the tracked face point traces
        /// an arc, injecting phantom translation into the position data. This value
        /// is used to compute and subtract that artifact so only genuine leaning
        /// translation remains.
        /// Default: 0.01m (small residual after face-tracking calibration).
        /// Set to 0 to disable pivot compensation.
        /// </summary>
        public float TrackerPivotForward { get; set; } = 0.01f;

        /// <summary>
        /// Processes a raw position through the full pipeline.
        /// </summary>
        /// <param name="raw">Raw position data from the tracker.</param>
        /// <param name="processedRotationQ">Already-processed head rotation quaternion (for neck model).</param>
        /// <param name="isRemote">Whether the data source is remote (for smoothing baseline).</param>
        /// <param name="deltaTime">Frame delta time in seconds.</param>
        /// <returns>Final position offset in meters, box-clamped.</returns>
        public Vec3 Process(PositionData raw, Quat4 processedRotationQ, bool isRemote, float deltaTime)
        {
            if (!raw.IsValid)
            {
                return Vec3.Zero;
            }

            // Step 1: Center subtraction
            Vec3 pos = raw.ToVec3() - _center;

            // Step 1.5: Subtract tracker pivot rotation artifact
            // The tracker tracks a point on the face that's TrackerPivotForward meters
            // ahead of the neck pivot. When the head rotates, this point traces an arc,
            // injecting phantom translation into the raw position data. Subtracting
            // the arc displacement isolates genuine head translation (leaning).
            if (TrackerPivotForward > 0f)
            {
                Vec3 pivot = new Vec3(0, 0, TrackerPivotForward);
                Vec3 artifact = processedRotationQ.Rotate(pivot) - pivot;
                pos = pos - artifact;
            }

            // Step 2: Per-axis sensitivity and inversion
            float x = pos.X * Settings.SensitivityX;
            float y = pos.Y * Settings.SensitivityY;
            float z = pos.Z * Settings.SensitivityZ;

            if (Settings.InvertX) x = -x;
            if (Settings.InvertY) y = -y;
            if (Settings.InvertZ) z = -z;

            Vec3 scaled = new Vec3(x, y, z);

            // Step 3: Exponential smoothing on tracker position
            float effectiveSmoothing = SmoothingUtils.GetEffectiveSmoothing(Settings.Smoothing, isRemote);

            if (!_hasSmoothedValue)
            {
                _smoothedPosition = scaled;
                _hasSmoothedValue = true;
            }
            else
            {
                float t = SmoothingUtils.CalculateSmoothingFactor(effectiveSmoothing, deltaTime);
                _smoothedPosition = new Vec3(
                    MathUtils.Lerp(_smoothedPosition.X, scaled.X, t),
                    MathUtils.Lerp(_smoothedPosition.Y, scaled.Y, t),
                    MathUtils.Lerp(_smoothedPosition.Z, scaled.Z, t)
                );
            }

            // Step 4: Add neck model offset
            Vec3 neckOffset = NeckModel.ComputeOffset(processedRotationQ, NeckModelSettings);
            Vec3 total = _smoothedPosition + neckOffset;

            // Step 5: Box clamp total position against limits
            Vec3 clamped = new Vec3(
                MathUtils.Clamp(total.X, -Settings.LimitX, Settings.LimitX),
                MathUtils.Clamp(total.Y, -Settings.LimitY, Settings.LimitY),
                MathUtils.Clamp(total.Z, -Settings.LimitZ, Settings.LimitZ)
            );

            return clamped;
        }

        /// <summary>
        /// Sets the center offset for recentering.
        /// </summary>
        public void SetCenter(PositionData centerPosition)
        {
            _center = centerPosition.ToVec3();
        }

        /// <summary>
        /// Resets only the smoothing state, preserving center offset.
        /// </summary>
        public void ResetSmoothing()
        {
            _smoothedPosition = Vec3.Zero;
            _hasSmoothedValue = false;
        }

        /// <summary>
        /// Resets the processor state.
        /// </summary>
        public void Reset()
        {
            _center = Vec3.Zero;
            _smoothedPosition = Vec3.Zero;
            _hasSmoothedValue = false;
        }
    }
}
