using System;
using System.Collections.Generic;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The owner-approved normalisations a legacy import's map applies where the canonical
    /// codecs cannot hold a legacy value. N1, the hotkey code rule, is native only: C# imports
    /// read Unity key names, not virtual-key codes.
    /// </summary>
    public static class LegacyNormalisations
    {
        /// <summary>
        /// N2: a legacy float that is not finite imports as <paramref name="rowDefault"/>, the
        /// runtime row's default, and the drop is added to <paramref name="dropped"/>. A finite
        /// value is returned as it is.
        /// </summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="rowDefault"/> is not finite.</exception>
        public static float FiniteOrDefault(float value, float rowDefault, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            Check(!float.IsNaN(rowDefault) && !float.IsInfinity(rowDefault), section, key, dropped);
            if (!float.IsNaN(value) && !float.IsInfinity(value)) return value;
            dropped.Add(new DroppedValue(DropRule.NonFiniteNumber, section, key, Text(float.IsNaN(value), value > 0)));
            return rowDefault;
        }

        /// <summary>N2 for a double, as the float overload.</summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="rowDefault"/> is not finite.</exception>
        public static double FiniteOrDefault(double value, double rowDefault, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            Check(!double.IsNaN(rowDefault) && !double.IsInfinity(rowDefault), section, key, dropped);
            if (!double.IsNaN(value) && !double.IsInfinity(value)) return value;
            dropped.Add(new DroppedValue(DropRule.NonFiniteNumber, section, key, Text(double.IsNaN(value), value > 0)));
            return rowDefault;
        }

        private static string Text(bool nan, bool positive)
        {
            return nan ? "nan" : positive ? "inf" : "-inf";
        }

        private static void Check(bool finiteDefault, string section, string key, ICollection<DroppedValue> dropped)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (dropped == null) throw new ArgumentNullException("dropped");
            if (!finiteDefault)
            {
                throw new ArgumentException("[" + section + "] " + key + ": the row default is not finite", "rowDefault");
            }
        }
    }
}
