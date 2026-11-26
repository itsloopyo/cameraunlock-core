using System;

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Deadzone thresholds for each axis in degrees.
    /// Values below the deadzone are treated as zero.
    /// </summary>
    public struct DeadzoneSettings : IEquatable<DeadzoneSettings>
    {
        /// <summary>Yaw deadzone in degrees.</summary>
        public float Yaw { get; }

        /// <summary>Pitch deadzone in degrees.</summary>
        public float Pitch { get; }

        /// <summary>Roll deadzone in degrees.</summary>
        public float Roll { get; }

        /// <summary>No deadzone.</summary>
        public static DeadzoneSettings None => new DeadzoneSettings(0f, 0f, 0f);

        /// <summary>Default deadzone (0.5 degrees on all axes).</summary>
        public static DeadzoneSettings Default => new DeadzoneSettings(0.5f, 0.5f, 0.5f);

        public DeadzoneSettings(float yaw, float pitch, float roll)
        {
            Yaw = yaw;
            Pitch = pitch;
            Roll = roll;
        }

        /// <summary>
        /// Creates settings with uniform deadzone on all axes.
        /// </summary>
        public static DeadzoneSettings Uniform(float deadzone)
        {
            return new DeadzoneSettings(deadzone, deadzone, deadzone);
        }

        public bool Equals(DeadzoneSettings other)
        {
            return Yaw == other.Yaw && Pitch == other.Pitch && Roll == other.Roll;
        }

        public override bool Equals(object obj)
        {
            if (obj is DeadzoneSettings)
            {
                return Equals((DeadzoneSettings)obj);
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

        public static bool operator ==(DeadzoneSettings left, DeadzoneSettings right) => left.Equals(right);
        public static bool operator !=(DeadzoneSettings left, DeadzoneSettings right) => !left.Equals(right);
    }
}
