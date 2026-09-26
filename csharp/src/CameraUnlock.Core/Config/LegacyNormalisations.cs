using System;
using System.Collections.Generic;
using CameraUnlock.Core.Input;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The owner-approved normalisations a legacy import's map applies where the canonical
    /// codecs cannot hold a legacy value.
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

        /// <summary>
        /// N3: a legacy hotkey held as a UnityEngine.KeyCode value, as a hotkey value. 0,
        /// KeyCode.None, gives "", unbound, and records nothing. A Ctrl, Shift or Alt key
        /// (LeftShift to RightAlt) gives "" too, and the drop is added to
        /// <paramref name="dropped"/> under its key name: no hotkey value binds one, because it
        /// goes down before the key of any chord it starts, so a binding on it fires at the start
        /// of every Ctrl+Shift chord. Any other code gives its key name. A map folding the
        /// action's Ctrl+Shift chord into the same list appends the chord to what this gives, so
        /// the player keeps the chord when the key is unbound.
        /// </summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="unityKeyCode"/> is not 0 and has no
        /// name in data/keys.json, so no hotkey value can hold it.</exception>
        public static string KeyCodeToBindings(int unityKeyCode, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (dropped == null) throw new ArgumentNullException("dropped");
            if (unityKeyCode == 0) return string.Empty;
            if (KeyBindings.IsModifierKey(unityKeyCode))
            {
                dropped.Add(new DroppedValue(DropRule.ModifierKey, section, key, KeyBindings.NameOf(unityKeyCode)));
                return string.Empty;
            }
            return KeyBindings.Format(new[] { new KeyBinding(KeyModifiers.None, unityKeyCode) });
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
