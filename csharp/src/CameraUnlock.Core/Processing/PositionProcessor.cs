using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Complete positional tracking data processing pipeline.
    /// Pipeline: raw position → center subtraction → tracker pivot compensation → sensitivity/inversion → smoothing → box clamp
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
        /// Whether the current connection comes from a remote device. Fed from the
        /// receiver every update so a switch between a local and a remote tracker
        /// picks up the other parameter without a restart.
        /// </summary>
        public bool IsRemoteConnection { get; set; }

        /// <summary>
        /// Forward distance (meters) of the tracker's face tracking point from the
        /// neck/rotation pivot. When the head rotates, the tracked face point traces
        /// an arc, injecting phantom translation into the position data. This value
        /// is used to compute and subtract that artifact so only genuine leaning
        /// translation remains.
        /// <para>
        /// Defaults to 0 - compensation OFF - and must be measured per tracker app,
        /// because the correct arm length is not a property of this library. The
        /// Headcam Android app already applies its own eye-anchor offset (3.5cm up,
        /// 2.5cm forward) so that rotation-induced eye travel arrives as position,
        /// while the iOS app applies none; a single shared constant is wrong for at
        /// least one of them. The previous defaults (0.01 in C#, 0.15 in C++) were
        /// both picked while the compensation was inverted and doubling the artifact
        /// it removed, so neither carries over.
        /// </para>
        /// </summary>
        public float TrackerPivotForward { get; set; } = 0.0f;

        /// <summary>
        /// Upward distance (meters) of the tracker's face tracking point from the
        /// neck/rotation pivot, the vertical companion to <see cref="TrackerPivotForward"/>.
        /// A tracker watching the eyes sees a point both ahead of and above the neck, and
        /// pitching the head swings it through both. Defaults to 0, and compensation stays
        /// off until one of the two is positive.
        /// </summary>
        public float TrackerPivotUp { get; set; } = 0.0f;

        /// <summary>
        /// Processes a raw position through the full pipeline.
        /// </summary>
        /// <param name="raw">Raw position data from the tracker.</param>
        /// <param name="physicalRotationQ">
        /// The centered head rotation BEFORE per-axis sensitivity and inversion. The pivot
        /// artifact is a physical property of where the tracker's face point sits relative
        /// to the neck, so scaling the angle first over-corrects by the sensitivity factor
        /// and inverting it drives the correction the wrong way. Use
        /// <see cref="TrackingProcessor.GetSmoothedRotation"/>, which exposes exactly this.
        /// </param>
        /// <param name="deltaTime">Frame delta time in seconds.</param>
        /// <returns>Final position offset in meters, box-clamped.</returns>
        public Vec3 Process(PositionData raw, Quat4 physicalRotationQ, float deltaTime)
        {
            if (!raw.IsValid)
            {
                return Vec3.Zero;
            }

            // Step 1: Center subtraction
            Vec3 pos = raw.ToVec3() - _center;

            // Step 1.5: Subtract tracker pivot rotation artifact
            // The tracker reports a point on the face, which sits TrackerPivotForward
            // metres AHEAD of the neck pivot. When the head rotates, that point traces an
            // arc and the tracker reports the arc as translation; subtracting it isolates
            // genuine leaning.
            //
            // The pivot vector is -z, not +z: negative z is forward throughout this
            // library, and the Headcam trackers pin the same convention with a test
            // ("wire +Z out the back of the head"). Because rotation is linear,
            // R(-v) - (-v) == -(R(v) - v), so a +z pivot computes the exact NEGATION of
            // the real artifact and `pos - artifact` then DOUBLES the phantom translation
            // it was supposed to remove.
            if (TrackerPivotForward > 0f || TrackerPivotUp > 0f)
            {
                Vec3 pivot = new Vec3(0, TrackerPivotUp, -TrackerPivotForward);
                Vec3 artifact = physicalRotationQ.Rotate(pivot) - pivot;
                pos = pos - artifact;
            }

            // Step 2: Per-axis sensitivity and inversion
            float x = pos.X * Settings.SensitivityX;
            float y = pos.Y * Settings.SensitivityY;
            float z = pos.Z * Settings.SensitivityZ;

            if (Settings.InvertX) x = -x;
            if (Settings.InvertY) y = -y;
            if (Settings.InvertZ) z = -z;

            // Clamped BEFORE smoothing as well as after. The smoothing state used to track
            // the unclamped input, so a high sensitivity drove it far outside the limits
            // and it then spent time saturated on the way back - the output sat pinned at
            // the limit for hundreds of milliseconds after the head had already returned.
            // Clamping a bounded input is a no-op, so nothing changes for the common case.
            Vec3 scaled = ClampToLimits(new Vec3(x, y, z));

            // Step 3: Exponential smoothing on tracker position
            float effectiveSmoothing = SmoothingUtils.GetEffectiveSmoothing(
                Settings.LocalSmoothing, Settings.RemoteSmoothing, IsRemoteConnection);

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

            // Step 4: Box clamp position against limits
            return ClampToLimits(_smoothedPosition);
        }

        /// <summary>
        /// Box-clamps against the configured limits. Y and Z are asymmetric: up/forward
        /// lean is generous, down/backward lean is restricted to avoid clipping into the
        /// player body. NEGATIVE z is the forward lean, so LimitZ (forward) is the LOWER
        /// bound and LimitZBack (backward) the upper one. Reading LimitZ as "the +z bound"
        /// transposes the pair and hands forward lean the tight backward budget.
        /// </summary>
        private Vec3 ClampToLimits(Vec3 value)
        {
            return new Vec3(
                MathUtils.Clamp(value.X, -Settings.LimitX, Settings.LimitX),
                MathUtils.Clamp(value.Y, -Settings.LimitYDown, Settings.LimitY),
                MathUtils.Clamp(value.Z, -Settings.LimitZ, Settings.LimitZBack)
            );
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
