using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Complete tracking data processing pipeline.
    /// Pipeline: raw(Euler) -> quat centering -> Euler deadzone -> per-axis Euler smoothing -> Euler sensitivity
    /// </summary>
    public sealed class TrackingProcessor : ITrackingProcessor
    {
        private readonly CenterOffsetManager _centerManager = new CenterOffsetManager();

        // Per-axis smoothed Euler angles (no quaternion SLERP — prevents phantom roll)
        private float _smoothedYaw;
        private float _smoothedPitch;
        private float _smoothedRoll;
        private bool _hasSmoothedValue;

        /// <summary>
        /// Sensitivity settings.
        /// </summary>
        public SensitivitySettings Sensitivity { get; set; } = SensitivitySettings.Default;

        /// <summary>
        /// Deadzone settings.
        /// </summary>
        public DeadzoneSettings Deadzone { get; set; } = DeadzoneSettings.None;

        /// <summary>
        /// User smoothing factor (0 = frame interpolation only, 1 = heavy smoothing).
        /// Frame interpolation is always applied regardless of this value.
        /// </summary>
        public float SmoothingFactor { get; set; } = 0f;

        /// <summary>
        /// The center offset manager for recentering.
        /// </summary>
        public CenterOffsetManager CenterManager => _centerManager;

        /// <summary>
        /// Gets the current smoothed rotation values.
        /// </summary>
        /// <param name="yaw">Output: smoothed yaw.</param>
        /// <param name="pitch">Output: smoothed pitch.</param>
        /// <param name="roll">Output: smoothed roll.</param>
        public void GetSmoothedRotation(out float yaw, out float pitch, out float roll)
        {
            yaw = _smoothedYaw;
            pitch = _smoothedPitch;
            roll = _smoothedRoll;
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Gets the current smoothed rotation values (tuple version for modern runtimes).
        /// </summary>
        public (float Yaw, float Pitch, float Roll) SmoothedRotation
        {
            get
            {
                return (_smoothedYaw, _smoothedPitch, _smoothedRoll);
            }
        }
#endif

        /// <summary>
        /// Processes a raw tracking pose through the full pipeline.
        /// </summary>
        public TrackingPose Process(TrackingPose rawPose, float deltaTime)
        {
            if (!rawPose.IsValid)
            {
                return rawPose;
            }

            // Step 1: Convert raw Euler to quaternion and apply center offset
            Quat4 rawQ = QuaternionUtils.FromYawPitchRoll(rawPose.Yaw, rawPose.Pitch, rawPose.Roll);
            Quat4 centeredQ = _centerManager.ApplyOffsetQuat(rawQ);

            // Step 2: Decompose to Euler for per-axis deadzone
            QuaternionUtils.ToEulerYXZ(centeredQ, out float yaw, out float pitch, out float roll);

            yaw = (float)DeadzoneUtils.Apply(yaw, Deadzone.Yaw);
            pitch = (float)DeadzoneUtils.Apply(pitch, Deadzone.Pitch);
            roll = (float)DeadzoneUtils.Apply(roll, Deadzone.Roll);

            // Step 3: Per-axis Euler smoothing (no quaternion SLERP — prevents phantom roll)
            float effectiveSmoothing = SmoothingUtils.GetEffectiveSmoothing(SmoothingFactor);

            if (!_hasSmoothedValue)
            {
                _smoothedYaw = yaw;
                _smoothedPitch = pitch;
                _smoothedRoll = roll;
                _hasSmoothedValue = true;
            }
            else
            {
                _smoothedYaw = SmoothingUtils.Smooth(_smoothedYaw, yaw, effectiveSmoothing, deltaTime);
                _smoothedPitch = SmoothingUtils.Smooth(_smoothedPitch, pitch, effectiveSmoothing, deltaTime);
                _smoothedRoll = SmoothingUtils.Smooth(_smoothedRoll, roll, effectiveSmoothing, deltaTime);
            }

            // Step 4: Apply per-axis sensitivity
            return new TrackingPose(_smoothedYaw, _smoothedPitch, _smoothedRoll, rawPose.TimestampTicks)
                .ApplySensitivity(Sensitivity);
        }

        /// <summary>
        /// Sets the current smoothed pose as the center.
        /// </summary>
        public void Recenter()
        {
            Quat4 smoothedQ = QuaternionUtils.FromYawPitchRoll(_smoothedYaw, _smoothedPitch, _smoothedRoll);
            _centerManager.ComposeAdditionalOffset(smoothedQ);
            _smoothedYaw = 0f;
            _smoothedPitch = 0f;
            _smoothedRoll = 0f;
        }

        /// <summary>
        /// Sets a specific pose as the center (for recentering without prior data).
        /// </summary>
        public void RecenterTo(TrackingPose pose)
        {
            _centerManager.SetCenter(pose);
            _smoothedYaw = 0f;
            _smoothedPitch = 0f;
            _smoothedRoll = 0f;
        }

        /// <summary>
        /// Resets only the smoothing state, preserving center offset.
        /// Use on tracking regain to avoid blending from stale pre-loss values.
        /// </summary>
        public void ResetSmoothing()
        {
            _smoothedYaw = 0f;
            _smoothedPitch = 0f;
            _smoothedRoll = 0f;
            _hasSmoothedValue = false;
        }

        /// <summary>
        /// Resets the processor state.
        /// </summary>
        public void Reset()
        {
            _centerManager.Reset();
            _smoothedYaw = 0f;
            _smoothedPitch = 0f;
            _smoothedRoll = 0f;
            _hasSmoothedValue = false;
        }
    }
}
