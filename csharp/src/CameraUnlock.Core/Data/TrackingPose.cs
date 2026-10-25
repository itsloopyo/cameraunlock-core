using System;
using System.Diagnostics;

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Immutable 3DOF tracking pose data with timestamp.
    /// </summary>
    public struct TrackingPose : IEquatable<TrackingPose>
    {
        /// <summary>Yaw rotation in degrees (horizontal head turn).</summary>
        public float Yaw { get; }

        /// <summary>Pitch rotation in degrees (vertical head tilt).</summary>
        public float Pitch { get; }

        /// <summary>Roll rotation in degrees (head tilt side to side).</summary>
        public float Roll { get; }

        /// <summary>Timestamp in Stopwatch ticks when this pose was captured.</summary>
        public long TimestampTicks { get; }

        /// <summary>True if this pose contains valid data (not default/zero timestamp).</summary>
        public bool IsValid => TimestampTicks != 0;

        /// <summary>Default max age in milliseconds for data to be considered fresh.</summary>
        public const int DefaultFreshnessMs = 500;

        /// <summary>
        /// True if this pose is fresh (received within last 500ms).
        /// More reliable than IsValid for detecting stale tracking data.
        /// </summary>
        public bool IsDataFresh => IsRecent(DefaultFreshnessMs);

        /// <summary>Zero pose with no rotation.</summary>
        public static TrackingPose Zero => new TrackingPose(0f, 0f, 0f, Stopwatch.GetTimestamp());

        public TrackingPose(float yaw, float pitch, float roll, long timestampTicks)
        {
            Yaw = yaw;
            Pitch = pitch;
            Roll = roll;
            TimestampTicks = timestampTicks;
        }

        public TrackingPose(float yaw, float pitch, float roll)
            : this(yaw, pitch, roll, Stopwatch.GetTimestamp())
        {
        }

        /// <summary>
        /// Checks if this pose is recent (within maxAgeMs milliseconds).
        /// </summary>
        public bool IsRecent(int maxAgeMs)
        {
            if (TimestampTicks == 0) return false;
            long elapsed = Stopwatch.GetTimestamp() - TimestampTicks;
            double elapsedMs = elapsed * 1000.0 / Stopwatch.Frequency;
            return elapsedMs < maxAgeMs;
        }

        /// <summary>
        /// Subtracts an offset from this pose (for recentering).
        /// </summary>
        public TrackingPose SubtractOffset(TrackingPose offset)
        {
            return new TrackingPose(
                Yaw - offset.Yaw,
                Pitch - offset.Pitch,
                Roll - offset.Roll,
                TimestampTicks
            );
        }

        /// <summary>
        /// Applies sensitivity multipliers to this pose.
        /// </summary>
        public TrackingPose ApplySensitivity(SensitivitySettings sensitivity)
        {
            float yaw = Yaw * sensitivity.Yaw;
            float pitch = Pitch * sensitivity.Pitch;
            float roll = Roll * sensitivity.Roll;

            if (sensitivity.InvertYaw) yaw = -yaw;
            if (sensitivity.InvertPitch) pitch = -pitch;
            if (sensitivity.InvertRoll) roll = -roll;

            return new TrackingPose(yaw, pitch, roll, TimestampTicks);
        }

        public bool Equals(TrackingPose other)
        {
            return Yaw == other.Yaw && Pitch == other.Pitch && Roll == other.Roll;
        }

        public override bool Equals(object obj)
        {
            if (obj is TrackingPose)
            {
                return Equals((TrackingPose)obj);
            }
            return false;
        }

        public override int GetHashCode()
        {
            unchecked
            {
                int hash = 17;
                hash = hash * 31 + Yaw.GetHashCode();
                hash = hash * 31 + Pitch.GetHashCode();
                hash = hash * 31 + Roll.GetHashCode();
                return hash;
            }
        }

        public override string ToString()
        {
            return $"TrackingPose(Y:{Yaw:F2}, P:{Pitch:F2}, R:{Roll:F2})";
        }

        public static bool operator ==(TrackingPose left, TrackingPose right) => left.Equals(right);
        public static bool operator !=(TrackingPose left, TrackingPose right) => !left.Equals(right);
    }
}
