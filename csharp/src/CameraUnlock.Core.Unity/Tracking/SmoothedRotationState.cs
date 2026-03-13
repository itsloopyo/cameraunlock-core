using UnityEngine;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Unity.Extensions;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Manages smoothed rotation state for camera tracking.
    /// Applies baseline smoothing floor automatically.
    /// </summary>
    public class SmoothedRotationState
    {
        private Quaternion _smoothedRotation = Quaternion.identity;
        private bool _initialized;

        /// <summary>
        /// Gets the current smoothed rotation.
        /// </summary>
        public Quaternion Current => _smoothedRotation;

        /// <summary>
        /// Gets whether the smoothing state has been initialized.
        /// </summary>
        public bool IsInitialized => _initialized;

        /// <summary>
        /// Updates the smoothed rotation towards a target.
        /// Applies baseline smoothing floor automatically.
        /// </summary>
        /// <param name="target">Target rotation to smooth towards.</param>
        /// <param name="smoothing">User-configured smoothing (0=instant).</param>
        /// <returns>The new smoothed rotation.</returns>
        public Quaternion Update(Quaternion target, float smoothing)
        {
            float effectiveSmoothing = SmoothingUtils.GetEffectiveSmoothing(smoothing);

            // First update - initialize directly
            if (!_initialized)
            {
                _smoothedRotation = target;
                _initialized = true;
                return _smoothedRotation;
            }

            // Skip smoothing if negligible
            if (effectiveSmoothing < 0.001f)
            {
                _smoothedRotation = target;
                return _smoothedRotation;
            }

            // Apply frame-rate independent smoothing
            _smoothedRotation = UnitySmoothingHelper.SmoothRotation(
                _smoothedRotation,
                target,
                effectiveSmoothing
            );

            return _smoothedRotation;
        }

        /// <summary>
        /// Resets the smoothing state.
        /// Call when tracking is re-enabled or camera changes.
        /// </summary>
        public void Reset()
        {
            _smoothedRotation = Quaternion.identity;
            _initialized = false;
        }

        /// <summary>
        /// Resets the smoothing state to a specific rotation.
        /// </summary>
        /// <param name="rotation">Initial rotation to reset to.</param>
        public void Reset(Quaternion rotation)
        {
            _smoothedRotation = rotation;
            _initialized = true;
        }
    }
}
