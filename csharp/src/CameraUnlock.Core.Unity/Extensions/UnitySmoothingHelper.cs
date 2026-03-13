using UnityEngine;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Unity-specific smoothing helpers that wrap CameraUnlock.Core.Math.SmoothingUtils.
    /// </summary>
    public static class UnitySmoothingHelper
    {
        /// <summary>
        /// Smooths a rotation using frame-rate independent exponential smoothing.
        /// </summary>
        /// <param name="current">Current smoothed rotation.</param>
        /// <param name="target">Target rotation to smooth towards.</param>
        /// <param name="smoothing">Smoothing factor 0-1. 0=instant, 1=very slow.</param>
        /// <returns>New smoothed rotation.</returns>
        public static Quaternion SmoothRotation(Quaternion current, Quaternion target, float smoothing)
        {
            float t = SmoothingUtils.CalculateSmoothingFactor(smoothing, Time.deltaTime);
            return Quaternion.Slerp(current, target, t);
        }

        /// <summary>
        /// Smooths a Vector3 value (e.g., Euler angles or position).
        /// </summary>
        /// <param name="current">Current smoothed value.</param>
        /// <param name="target">Target value to smooth towards.</param>
        /// <param name="smoothing">Smoothing factor 0-1.</param>
        /// <returns>New smoothed value.</returns>
        public static Vector3 SmoothVector3(Vector3 current, Vector3 target, float smoothing)
        {
            float t = SmoothingUtils.CalculateSmoothingFactor(smoothing, Time.deltaTime);
            return Vector3.Lerp(current, target, t);
        }

        /// <summary>
        /// Smooths a Vector2 value (e.g., screen offset).
        /// </summary>
        /// <param name="current">Current smoothed value.</param>
        /// <param name="target">Target value to smooth towards.</param>
        /// <param name="smoothing">Smoothing factor 0-1.</param>
        /// <returns>New smoothed value.</returns>
        public static Vector2 SmoothVector2(Vector2 current, Vector2 target, float smoothing)
        {
            float t = SmoothingUtils.CalculateSmoothingFactor(smoothing, Time.deltaTime);
            return Vector2.Lerp(current, target, t);
        }

        /// <summary>
        /// Smooths a single float value.
        /// </summary>
        /// <param name="current">Current smoothed value.</param>
        /// <param name="target">Target value to smooth towards.</param>
        /// <param name="smoothing">Smoothing factor 0-1.</param>
        /// <returns>New smoothed value.</returns>
        public static float SmoothFloat(float current, float target, float smoothing)
        {
            return SmoothingUtils.Smooth(current, target, smoothing, Time.deltaTime);
        }

        /// <summary>
        /// Gets the smoothing interpolation factor for this frame.
        /// Use this if you need to apply smoothing manually.
        /// </summary>
        /// <param name="smoothing">Smoothing factor 0-1.</param>
        /// <returns>Interpolation factor (0-1) to use with Lerp/Slerp.</returns>
        public static float GetSmoothingT(float smoothing)
        {
            return SmoothingUtils.CalculateSmoothingFactor(smoothing, Time.deltaTime);
        }

    }
}
