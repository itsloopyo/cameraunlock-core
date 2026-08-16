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
        /// Update smoothing with a caller-selected smoothing value, which is expected to be
        /// the already-selected effective value from
        /// <see cref="SmoothingUtils.GetEffectiveSmoothing(float, float, bool)"/>.
        ///
        /// There is deliberately no snap branch at low smoothing. It used to be
        /// unreachable, because the old GetEffectiveSmoothing could never return below the
        /// 0.15 baseline floor. With that floor gone and LocalSmoothing defaulting to 0.0,
        /// a snap would become the DEFAULT path for every local user, which is the opposite
        /// of what the same migration did to SmoothedRotationState, the C++
        /// CalculateSmoothingFactor and the mods. The speed clamp inside
        /// <see cref="SmoothingUtils.CalculateSmoothingFactor"/> keeps the per-frame factor
        /// strictly below 1 at smoothing 0, so interpolation stays active and the output is
        /// never stepped.
        /// </summary>
        /// <param name="yaw">Target yaw in degrees.</param>
        /// <param name="pitch">Target pitch in degrees.</param>
        /// <param name="roll">Target roll in degrees.</param>
        /// <param name="smoothing">Effective smoothing factor 0-1. 0 = frame interpolation only.</param>
        /// <param name="deltaTime">Frame delta time in seconds.</param>
        /// <param name="smoothedYaw">Output smoothed yaw.</param>
        /// <param name="smoothedPitch">Output smoothed pitch.</param>
        /// <param name="smoothedRoll">Output smoothed roll.</param>
        public void Update(float yaw, float pitch, float roll,
            float smoothing, float deltaTime,
            out float smoothedYaw, out float smoothedPitch, out float smoothedRoll)
        {
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
