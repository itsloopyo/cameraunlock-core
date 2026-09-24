using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One section of a <see cref="CanonicalIni"/>, every header of its name merged.
    /// </summary>
    public sealed class CanonicalSection
    {
        internal CanonicalSection(byte[] name, CanonicalValue[] values)
        {
            Name = name;
            Values = new ReadOnlyCollection<CanonicalValue>(values);
        }

        /// <summary>The name's bytes, spelled as its first header spells it.</summary>
        public byte[] Name { get; }

        /// <summary>In the order each key first occurs.</summary>
        public ReadOnlyCollection<CanonicalValue> Values { get; }

        /// <summary>
        /// The key compared ASCII case-insensitively with <paramref name="key"/>'s UTF-8
        /// bytes, or null.
        /// </summary>
        /// <exception cref="System.ArgumentException"><paramref name="key"/> is not valid UTF-16.</exception>
#if NULLABLE_ENABLED
        public CanonicalValue? Find(string key)
#else
        public CanonicalValue Find(string key)
#endif
        {
            byte[] wanted = CanonicalIni.NameBytes(key, nameof(key));
            foreach (CanonicalValue value in Values)
            {
                if (CanonicalIni.EqualsAsciiIgnoreCase(value.Key, wanted)) return value;
            }
            return null;
        }
    }
}
