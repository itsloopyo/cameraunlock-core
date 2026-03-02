using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Complete tracking data processing pipeline.
    /// Pipeline: raw(Euler) -> quat centering -> Euler deadzone -> quat SLERP smoothing -> Euler sensitivity
    /// </summary>
    public sealed class TrackingProcessor : ITrackingProcessor
    {
        private readonly CenterOffsetManager _centerManager = new CenterOffsetManager();

        // Smoothed rotation as quaternion for gimbal-lock-free SLERP
        private Quat4 _smoothedQuat = Quat4.Identity;
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
        /// Smoothing factor (0 = instant, 1 = very slow).
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
            QuaternionUtils.ToEulerYXZ(_smoothedQuat, out yaw, out pitch, out roll);
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Gets the current smoothed rotation values (tuple version for modern runtimes).
        /// </summary>
        public (float Yaw, float Pitch, float Roll) SmoothedRotation
        {
            get
            {
                QuaternionUtils.ToEulerYXZ(_smoothedQuat, out float y, out float p, out float r);
                return (y, p, r);
            }
        }
#endif

        /// <summary>
        /// Processes a raw tracking pose through the full pipeline.
        /// </summary>
        public TrackingPose Process(TrackingPose rawPose, bool isRemoteConnection, float deltaTime)
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

            // Step 3: Convert back to quaternion for SLERP smoothing
            Quat4 targetQ = QuaternionUtils.FromYawPitchRoll(yaw, pitch, roll);

            float effectiveSmoothing = SmoothingUtils.GetEffectiveSmoothing(SmoothingFactor, isRemoteConnection);

            if (!_hasSmoothedValue)
            {
                _smoothedQuat = targetQ;
                _hasSmoothedValue = true;
            }
            else
            {
                float t = SmoothingUtils.CalculateSmoothingFactor(effectiveSmoothing, deltaTime);
                _smoothedQuat = QuaternionUtils.Slerp(_smoothedQuat, targetQ, t);
            }

            // Step 4: Decompose to Euler for per-axis sensitivity
            QuaternionUtils.ToEulerYXZ(_smoothedQuat, out float smoothedYaw, out float smoothedPitch, out float smoothedRoll);

            return new TrackingPose(smoothedYaw, smoothedPitch, smoothedRoll, rawPose.TimestampTicks)
                .ApplySensitivity(Sensitivity);
        }

        /// <summary>
        /// Sets the current smoothed pose as the center.
        /// </summary>
        public void Recenter()
        {
            _centerManager.ComposeAdditionalOffset(_smoothedQuat);
            _smoothedQuat = Quat4.Identity;
        }

        /// <summary>
        /// Sets a specific pose as the center (for recentering without prior data).
        /// </summary>
        public void RecenterTo(TrackingPose pose)
        {
            _centerManager.SetCenter(pose);
            _smoothedQuat = Quat4.Identity;
        }

        /// <summary>
        /// Resets only the smoothing state, preserving center offset.
        /// Use on tracking regain to avoid blending from stale pre-loss values.
        /// </summary>
        public void ResetSmoothing()
        {
            _smoothedQuat = Quat4.Identity;
            _hasSmoothedValue = false;
        }

        /// <summary>
        /// Resets the processor state.
        /// </summary>
        public void Reset()
        {
            _centerManager.Reset();
            _smoothedQuat = Quat4.Identity;
            _hasSmoothedValue = false;
        }
    }
}
