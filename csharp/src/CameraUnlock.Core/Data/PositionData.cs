using System;
using System.Diagnostics;

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Immutable 3DOF position data with timestamp.
    /// </summary>
    public struct PositionData : IEquatable<PositionData>
    {
        /// <summary>X position in meters (lateral).</summary>
        public float X { get; }

        /// <summary>Y position in meters (vertical).</summary>
        public float Y { get; }

        /// <summary>Z position in meters (depth).</summary>
        public float Z { get; }

        /// <summary>Timestamp in Stopwatch ticks when this position was captured.</summary>
        public long TimestampTicks { get; }

        /// <summary>True if this position contains valid data (not default/zero timestamp).</summary>
        public bool IsValid => TimestampTicks != 0;

        /// <summary>Zero position.</summary>
        public static PositionData Zero => new PositionData(0f, 0f, 0f, Stopwatch.GetTimestamp());

        public PositionData(float x, float y, float z, long timestampTicks)
        {
            X = x;
            Y = y;
            Z = z;
            TimestampTicks = timestampTicks;
        }

        public PositionData(float x, float y, float z)
            : this(x, y, z, Stopwatch.GetTimestamp())
        {
        }

        /// <summary>
        /// Converts to a Vec3 (drops timestamp).
        /// </summary>
        public Vec3 ToVec3()
        {
            return new Vec3(X, Y, Z);
        }

        /// <summary>
        /// Subtracts an offset from this position (for recentering).
        /// </summary>
        public PositionData SubtractOffset(PositionData offset)
        {
            return new PositionData(
                X - offset.X,
                Y - offset.Y,
                Z - offset.Z,
                TimestampTicks
            );
        }

        public bool Equals(PositionData other)
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }

        public override bool Equals(object obj)
        {
            if (obj is PositionData)
            {
                return Equals((PositionData)obj);
            }
            return false;
        }

        public override int GetHashCode()
        {
            unchecked
            {
                int hash = 17;
                hash = hash * 31 + X.GetHashCode();
                hash = hash * 31 + Y.GetHashCode();
                hash = hash * 31 + Z.GetHashCode();
                return hash;
            }
        }

        public override string ToString()
        {
            return string.Format("PositionData(X:{0:F4}, Y:{1:F4}, Z:{2:F4})", X, Y, Z);
        }

        public static bool operator ==(PositionData left, PositionData right) => left.Equals(right);
        public static bool operator !=(PositionData left, PositionData right) => !left.Equals(right);
    }
}
