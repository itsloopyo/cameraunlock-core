using System;
using System.Collections.Generic;
using System.Globalization;
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
        /// value is returned as it is. On a row that follows Defaults.ini the migration writes such
        /// a value <c>default</c> (owner ruling of 2026-09-27):
        /// <see cref="LegacyFollowsDefaultsIni.Setting{T}(ConceptDescriptor, T, T)"/> leaves the row
        /// to Defaults.ini when handed the value as read.
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
        /// N4 (approved 2026-09-27): a finite legacy number outside the inclusive range
        /// <paramref name="min"/> to <paramref name="max"/> imports as the nearest end of it, and
        /// the clamp is added to <paramref name="dropped"/> as <see cref="DropRule.NumberOutOfRange"/>
        /// with the value read. A value in the range is returned as it is. Apply N2 first. Hand
        /// <see cref="LegacyFollowsDefaultsIni"/> the value read, not the clamped one: a player set
        /// it, so the row is not left to Defaults.ini even where the clamped value equals the
        /// shipped default.
        /// </summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> or a bound is not finite, or
        /// <paramref name="min"/> is above <paramref name="max"/>.</exception>
        public static float ClampToRange(float value, float min, float max, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            CheckRange(Finite(min) && Finite(max), min > max, Finite(value), section, key, dropped);
            if (value >= min && value <= max) return value;
            dropped.Add(new DroppedValue(DropRule.NumberOutOfRange, section, key, FloatCodec.RenderFinite(value)));
            return value < min ? min : max;
        }

        /// <summary>N4 for a double, as the float overload.</summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> or a bound is not finite, or
        /// <paramref name="min"/> is above <paramref name="max"/>.</exception>
        public static double ClampToRange(double value, double min, double max, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            CheckRange(Finite(min) && Finite(max), min > max, Finite(value), section, key, dropped);
            if (value >= min && value <= max) return value;
            dropped.Add(new DroppedValue(DropRule.NumberOutOfRange, section, key, DoubleCodec.RenderFinite(value)));
            return value < min ? min : max;
        }

        /// <summary>N4 for an int, as the float overload.</summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="min"/> is above <paramref name="max"/>.</exception>
        public static int ClampToRange(int value, int min, int max, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            CheckRange(true, min > max, true, section, key, dropped);
            if (value >= min && value <= max) return value;
            dropped.Add(new DroppedValue(DropRule.NumberOutOfRange, section, key, value.ToString(CultureInfo.InvariantCulture)));
            return value < min ? min : max;
        }

        /// <summary>
        /// N4 on the range data/config-schema.json gives <paramref name="concept"/>, a float
        /// concept of <see cref="ConfigConcepts"/>.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/>, a text or
        /// <paramref name="dropped"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> is not finite.</exception>
        public static float ClampToRange(ConceptDescriptor<float> concept, float value, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            var codec = (FloatCodec)concept.Codec;
            return ClampToRange(value, codec.Min, codec.Max, section, key, dropped);
        }

        /// <summary>
        /// N4 on the range data/config-schema.json gives <paramref name="concept"/>, an int concept
        /// of <see cref="ConfigConcepts"/>.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/>, a text or
        /// <paramref name="dropped"/> is null.</exception>
        public static int ClampToRange(ConceptDescriptor<int> concept, int value, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            var codec = (IntCodec)concept.Codec;
            return ClampToRange(value, codec.Min, codec.Max, section, key, dropped);
        }

        /// <summary>
        /// N1 and N3: a legacy hotkey held as a UnityEngine.KeyCode value, as a hotkey value. 0,
        /// KeyCode.None, gives "", unbound, and records nothing. A code data/keys.json has no name
        /// for (<see cref="KeyBindings.HasName"/>) gives "" too, and the drop is added to
        /// <paramref name="dropped"/> as <see cref="DropRule.KeyCodeOutOfRange"/> with the code in
        /// decimal (N1, owner ruling of 2026-09-27): no hotkey value can spell it. A Ctrl, Shift or Alt key
        /// (LeftShift, RightShift, LeftControl, RightControl, LeftAlt or RightAlt) gives "" too, and the drop is added to
        /// <paramref name="dropped"/> under its key name: no hotkey value binds one, because it
        /// goes down before the key of any chord made with it, so a binding on it fires on the way
        /// into every such chord. Any other code gives its key name. A map folding the
        /// action's Ctrl+Shift chord into the same list appends the chord to what this gives, so
        /// the player keeps the chord when the key is unbound.
        /// </summary>
        /// <exception cref="ArgumentNullException">A text or <paramref name="dropped"/> is null.</exception>
        public static string KeyCodeToBindings(int unityKeyCode, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (dropped == null) throw new ArgumentNullException("dropped");
            if (unityKeyCode == 0) return string.Empty;
            if (!KeyBindings.HasName(unityKeyCode))
            {
                dropped.Add(new DroppedValue(DropRule.KeyCodeOutOfRange, section, key,
                    unityKeyCode.ToString(CultureInfo.InvariantCulture)));
                return string.Empty;
            }
            if (KeyBindings.IsModifierKey(unityKeyCode))
            {
                dropped.Add(new DroppedValue(DropRule.ModifierKey, section, key, KeyBindings.NameOf(unityKeyCode)));
                return string.Empty;
            }
            return KeyBindings.Format(new[] { new KeyBinding(KeyModifiers.None, unityKeyCode) });
        }

        private static bool Finite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }

        private static bool Finite(double value)
        {
            return !double.IsNaN(value) && !double.IsInfinity(value);
        }

        private static void CheckRange(bool finiteBounds, bool inverted, bool finiteValue, string section, string key,
            ICollection<DroppedValue> dropped)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (dropped == null) throw new ArgumentNullException("dropped");
            if (!finiteBounds) throw new ArgumentException("[" + section + "] " + key + ": a bound of the range is not finite", "min");
            if (!finiteValue) throw new ArgumentException("[" + section + "] " + key + ": the value is not finite, which N2 imports", "value");
            if (inverted) throw new ArgumentException("[" + section + "] " + key + ": the range's low end is above its high end", "min");
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
