using UnityEngine;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Unity.Extensions;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Manages smoothed rotation state for camera tracking.
    /// The caller selects the smoothing value with
    /// <see cref="SmoothingUtils.GetEffectiveSmoothing(float, float, bool)"/> and passes the
    /// result in; this class never floors or overrides it.
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
        ///
        /// Renamed from Update(Quaternion, float). The signature did not change but the
        /// MEANING of the float did: it used to be the raw user smoothing value, which
        /// this class floored to the 0.15 baseline internally and snapped below 0.001, and
        /// it is now the already-selected effective value, used verbatim. Every existing
        /// call site compiled unchanged and behaved differently, and a caller still passing
        /// its raw single smoothing value would get it applied verbatim and never consult
        /// the connection flag, making RemoteSmoothing dead config. The rename is
        /// deliberate: it forces every call site to be looked at instead of silently
        /// changing under them.
        /// </summary>
        /// <param name="target">Target rotation to smooth towards.</param>
        /// <param name="effectiveSmoothing">
        /// The already-selected smoothing value for the current connection, from
        /// <see cref="SmoothingUtils.GetEffectiveSmoothing(float, float, bool)"/>.
        /// Never a raw per-mod smoothing setting.
        /// </param>
        /// <returns>The new smoothed rotation.</returns>
        public Quaternion UpdateWithEffectiveSmoothing(Quaternion target, float effectiveSmoothing)
        {
            // First update - initialize directly
            if (!_initialized)
            {
                _smoothedRotation = target;
                _initialized = true;
                return _smoothedRotation;
            }

            // No snap branch at low smoothing: frame interpolation is gated on
            // receiving data, never on the smoothing value. With LocalSmoothing
            // defaulting to 0 a snap here would leave every local user with raw
            // stepped output on a high-refresh display.
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
