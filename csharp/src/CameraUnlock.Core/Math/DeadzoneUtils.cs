using System;
using CameraUnlock.Core.Data;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Math
{
    /// <summary>
    /// Deadzone application utilities.
    /// Applies scaled deadzone to prevent jump at threshold.
    /// Performance-critical methods are aggressively inlined on supported frameworks.
    /// </summary>
    public static class DeadzoneUtils
    {
        /// <summary>
        /// Applies deadzone with scaling to prevent jump at threshold.
        /// Values within deadzone return 0, values outside are scaled from the edge.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float Apply(float value, float deadzone)
        {
            if (deadzone <= 0f)
            {
                return value;
            }

            float absValue = value >= 0f ? value : -value;
            if (absValue <= deadzone)
            {
                return 0f;
            }

            // Scale the value so it starts from 0 at the deadzone edge
            // Optimized: avoid branch by using sign computation
            float sign = value >= 0f ? 1f : -1f;
            return sign * (absValue - deadzone);
        }

        /// <summary>
        /// Applies deadzone with scaling (double precision).
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static double Apply(double value, double deadzone)
        {
            if (deadzone <= 0.0)
            {
                return value;
            }

            double absValue = value >= 0.0 ? value : -value;
            if (absValue <= deadzone)
            {
                return 0.0;
            }

            double sign = value >= 0.0 ? 1.0 : -1.0;
            return sign * (absValue - deadzone);
        }

        /// <summary>
        /// Applies deadzone settings to a tracking pose.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static TrackingPose Apply(TrackingPose pose, DeadzoneSettings deadzone)
        {
            return new TrackingPose(
                Apply(pose.Yaw, deadzone.Yaw),
                Apply(pose.Pitch, deadzone.Pitch),
                Apply(pose.Roll, deadzone.Roll),
                pose.TimestampTicks
            );
        }
    }
}
