#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Math
{
    /// <summary>
    /// Angle normalization and manipulation utilities.
    /// Performance-critical methods are aggressively inlined on supported frameworks.
    /// </summary>
    public static class AngleUtils
    {
        /// <summary>
        /// Normalizes an angle to the range -180 to +180 degrees.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float NormalizeAngle(float angle)
        {
            // Fast path for typical head tracking angles
            if (angle >= -180f && angle <= 180f)
            {
                return angle;
            }

            // Wrap to -180..180 range
            angle = angle % 360f;
            if (angle > 180f)
            {
                angle -= 360f;
            }
            else if (angle < -180f)
            {
                angle += 360f;
            }
            return angle;
        }

        /// <summary>
        /// Normalizes an angle to the range -180 to +180 degrees (double precision).
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static double NormalizeAngle(double angle)
        {
            if (angle >= -180.0 && angle <= 180.0)
            {
                return angle;
            }

            angle = angle % 360.0;
            if (angle > 180.0)
            {
                angle -= 360.0;
            }
            else if (angle < -180.0)
            {
                angle += 360.0;
            }
            return angle;
        }

        /// <summary>
        /// Calculates the shortest angular distance from one angle to another.
        /// Returns a value in the range -180 to +180 degrees.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float ShortestAngleDelta(float from, float to)
        {
            float delta = to - from;
            return NormalizeAngle(delta);
        }

        /// <summary>
        /// Converts degrees to radians.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float ToRadians(float degrees)
        {
            return degrees * MathConstants.DegToRad;
        }

        /// <summary>
        /// Converts radians to degrees.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float ToDegrees(float radians)
        {
            return radians * MathConstants.RadToDeg;
        }
    }
}
