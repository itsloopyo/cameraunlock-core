using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// <c>true</c> / <c>false</c>. Reads true false 1 0 yes no on off, ASCII case-insensitive.
    /// </summary>
    public sealed class BoolCodec : IValueCodec<bool>
    {
        private static readonly string[] TrueWords = { "true", "1", "yes", "on" };
        private static readonly string[] FalseWords = { "false", "0", "no", "off" };

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out bool value, out string? error)
#else
        public bool TryParse(byte[] text, out bool value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = false;
            error = null;
            foreach (string word in TrueWords)
            {
                if (CodecText.EqualsAsciiIgnoreCase(text, word))
                {
                    value = true;
                    return true;
                }
            }
            foreach (string word in FalseWords)
            {
                if (CodecText.EqualsAsciiIgnoreCase(text, word)) return true;
            }
            error = "expected true or false";
            return false;
        }

        /// <inheritdoc/>
        public byte[] Render(bool value)
        {
            return CodecText.Ascii(value ? "true" : "false");
        }

        /// <inheritdoc/>
        public bool Equal(bool a, bool b)
        {
            return a == b;
        }
    }
}
