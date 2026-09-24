using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One key of a <see cref="CanonicalSection"/>: its last occurrence.
    /// </summary>
    public sealed class CanonicalValue
    {
        internal CanonicalValue(byte[] key, byte[] value, int line, int[] earlierLines)
        {
            Key = key;
            Value = value;
            Line = line;
            EarlierLines = new ReadOnlyCollection<int>(earlierLines);
        }

        /// <summary>The key's bytes, spelled as on <see cref="Line"/>.</summary>
        public byte[] Key { get; }

        /// <summary>
        /// The raw bytes after '=', trimmed of spaces and tabs. May be empty. Codecs decide
        /// what they mean.
        /// </summary>
        public byte[] Value { get; }

        /// <summary>1-based.</summary>
        public int Line { get; }

        /// <summary>Every earlier occurrence of the key in its section, ascending.</summary>
        public ReadOnlyCollection<int> EarlierLines { get; }
    }
}
