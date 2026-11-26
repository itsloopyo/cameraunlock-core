using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The effective legacy value of one pose-shaping setting the frozen reader read: a
    /// sensitivity, unit scale, deadzone, response curve or axis inversion, which the canonical
    /// format has no row for. A differential test reads these to check the conversion: where
    /// <see cref="Folded"/> is true, the mod's own axis code now does what <see cref="Shipped"/>
    /// did. Built by <see cref="LegacyPoseShaping"/>. The C++ twin is
    /// <c>cameraunlock::config::PoseShapingValue</c>.
    /// </summary>
    public sealed class PoseShapingValue
    {
        /// <exception cref="ArgumentNullException">A text is null.</exception>
        public PoseShapingValue(string section, string key, string value, string shipped, bool folded)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (value == null) throw new ArgumentNullException("value");
            if (shipped == null) throw new ArgumentNullException("shipped");
            Section = section;
            Key = key;
            Value = value;
            Shipped = shipped;
            Folded = folded;
        }

        public string Section { get; }

        public string Key { get; }

        /// <summary>
        /// The value the import read, written as the canonical codecs write one ("true", "1.0",
        /// "0.5"), or "nan", "inf" or "-inf".
        /// </summary>
        public string Value { get; }

        /// <summary>The value the game shipped, written the same way.</summary>
        public string Shipped { get; }

        /// <summary>
        /// True when the value equals the shipped one, compared as numbers, so the conversion folds
        /// it into the mod's axis code; false when the player changed it, and it is also dropped.
        /// </summary>
        public bool Folded { get; }
    }
}
