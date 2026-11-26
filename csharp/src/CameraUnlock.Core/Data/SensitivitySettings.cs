using System;

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Sensitivity multipliers and inversion settings for each rotation axis.
    /// </summary>
    public struct SensitivitySettings : IEquatable<SensitivitySettings>
    {
        /// <summary>Yaw sensitivity multiplier (default 1.0).</summary>
        public float Yaw { get; }

        /// <summary>Pitch sensitivity multiplier (default 1.0).</summary>
        public float Pitch { get; }

        /// <summary>Roll sensitivity multiplier (default 1.0).</summary>
        public float Roll { get; }

        /// <summary>Invert yaw axis.</summary>
        public bool InvertYaw { get; }

        /// <summary>Invert pitch axis.</summary>
        public bool InvertPitch { get; }

        /// <summary>Invert roll axis.</summary>
        public bool InvertRoll { get; }

        /// <summary>Default sensitivity (1.0 for all axes, no inversion).</summary>
        public static SensitivitySettings Default => new SensitivitySettings(1f, 1f, 1f, false, false, false);

        public SensitivitySettings(float yaw, float pitch, float roll, bool invertYaw = false, bool invertPitch = false, bool invertRoll = false)
        {
            Yaw = yaw;
            Pitch = pitch;
            Roll = roll;
            InvertYaw = invertYaw;
            InvertPitch = invertPitch;
            InvertRoll = invertRoll;
        }

        /// <summary>
        /// Creates settings with uniform sensitivity on all axes.
        /// </summary>
        public static SensitivitySettings Uniform(float sensitivity)
        {
            return new SensitivitySettings(sensitivity, sensitivity, sensitivity);
        }

        /// <summary>
        /// Creates a copy with the specified yaw sensitivity.
        /// </summary>
        public SensitivitySettings WithYaw(float yaw)
        {
            return new SensitivitySettings(yaw, Pitch, Roll, InvertYaw, InvertPitch, InvertRoll);
        }

        /// <summary>
        /// Creates a copy with the specified pitch sensitivity.
        /// </summary>
        public SensitivitySettings WithPitch(float pitch)
        {
            return new SensitivitySettings(Yaw, pitch, Roll, InvertYaw, InvertPitch, InvertRoll);
        }

        /// <summary>
        /// Creates a copy with the specified roll sensitivity.
        /// </summary>
        public SensitivitySettings WithRoll(float roll)
        {
            return new SensitivitySettings(Yaw, Pitch, roll, InvertYaw, InvertPitch, InvertRoll);
        }

        public bool Equals(SensitivitySettings other)
        {
            return Yaw == other.Yaw && Pitch == other.Pitch && Roll == other.Roll &&
                   InvertYaw == other.InvertYaw && InvertPitch == other.InvertPitch && InvertRoll == other.InvertRoll;
        }

        public override bool Equals(object obj)
        {
            if (obj is SensitivitySettings)
            {
                return Equals((SensitivitySettings)obj);
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
                hash = hash * 31 + InvertYaw.GetHashCode();
                hash = hash * 31 + InvertPitch.GetHashCode();
                hash = hash * 31 + InvertRoll.GetHashCode();
                return hash;
            }
        }

        public static bool operator ==(SensitivitySettings left, SensitivitySettings right) => left.Equals(right);
        public static bool operator !=(SensitivitySettings left, SensitivitySettings right) => !left.Equals(right);
    }
}
