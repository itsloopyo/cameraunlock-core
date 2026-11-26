#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Math
{
    /// <summary>
    /// Common math utility functions used throughout the library.
    /// Performance-critical methods are aggressively inlined on supported frameworks.
    /// </summary>
    public static class MathUtils
    {
        /// <summary>
        /// Clamps a value between min and max.
        /// </summary>
        /// <param name="value">The value to clamp.</param>
        /// <param name="min">Minimum bound.</param>
        /// <param name="max">Maximum bound.</param>
        /// <returns>The clamped value.</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float Clamp(float value, float min, float max)
        {
            if (value < min) return min;
            if (value > max) return max;
            return value;
        }

        /// <summary>
        /// Clamps a value between 0 and 1.
        /// </summary>
        /// <param name="value">The value to clamp.</param>
        /// <returns>The clamped value in [0, 1].</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float Clamp01(float value)
        {
            if (value < 0f) return 0f;
            if (value > 1f) return 1f;
            return value;
        }

        /// <summary>
        /// Linear interpolation between two values.
        /// </summary>
        /// <param name="a">Start value.</param>
        /// <param name="b">End value.</param>
        /// <param name="t">Interpolation factor (0 = a, 1 = b).</param>
        /// <returns>Interpolated value.</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }
    }
}
