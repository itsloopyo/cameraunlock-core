using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Encapsulates the FromYawPitchRoll → Slerp → ToEulerYXZ smoothing pattern.
    /// Framework-agnostic (Quat4-based). Each instance holds its own interpolation state,
    /// so multi-player setups allocate one per player.
    /// </summary>
    public sealed class SmoothedEulerState
    {
        private Quat4 _smoothed;
        private bool _initialized;

        /// <summary>
        /// Update smoothing with caller-managed smoothing value.
        /// When <paramref name="smoothing"/> &lt; 0.001 the target is returned immediately
        /// and internal state is cleared (no lag on next transition to smoothed mode).
        /// </summary>
        /// <param name="yaw">Target yaw in degrees.</param>
        /// <param name="pitch">Target pitch in degrees.</param>
        /// <param name="roll">Target roll in degrees.</param>
        /// <param name="smoothing">Smoothing factor 0-1. 0 = instant.</param>
        /// <param name="deltaTime">Frame delta time in seconds.</param>
        /// <param name="smoothedYaw">Output smoothed yaw.</param>
        /// <param name="smoothedPitch">Output smoothed pitch.</param>
        /// <param name="smoothedRoll">Output smoothed roll.</param>
        public void Update(float yaw, float pitch, float roll,
            float smoothing, float deltaTime,
            out float smoothedYaw, out float smoothedPitch, out float smoothedRoll)
        {
            if (smoothing < 0.001f)
            {
                // No smoothing: snap to target, clear state so next
                // transition to smoothed mode starts fresh.
                _initialized = false;
                smoothedYaw = yaw;
                smoothedPitch = pitch;
                smoothedRoll = roll;
                return;
            }

            Quat4 target = QuaternionUtils.FromYawPitchRoll(yaw, pitch, roll);

            if (!_initialized)
            {
                _smoothed = target;
                _initialized = true;
                smoothedYaw = yaw;
                smoothedPitch = pitch;
                smoothedRoll = roll;
                return;
            }

            float t = SmoothingUtils.CalculateSmoothingFactor(smoothing, deltaTime);
            _smoothed = QuaternionUtils.Slerp(_smoothed, target, t);
            QuaternionUtils.ToEulerYXZ(_smoothed, out smoothedYaw, out smoothedPitch, out smoothedRoll);
        }

        /// <summary>
        /// Clears interpolation state. Next <see cref="Update"/> will initialize from scratch.
        /// </summary>
        public void Reset()
        {
            _smoothed = Quat4.Identity;
            _initialized = false;
        }
    }
}
