using System;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Text. Reads strict UTF-8 with no control byte below 0x20 other than tab; bytes that are
    /// not UTF-8 are invalid. Writes the text as UTF-8. The C++ codec keeps a value's bytes
    /// whatever their encoding, so a value that is not UTF-8 reads in a native mod and not
    /// here.
    /// </summary>
    public sealed class StringCodec : IValueCodec<string>
    {
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out string value, out string? error)
#else
        public bool TryParse(byte[] text, out string value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = string.Empty;
            error = null;
            foreach (byte b in text)
            {
                if (b < 0x20 && b != '\t')
                {
                    error = "holds the control byte 0x" + b.ToString("X2", CultureInfo.InvariantCulture)
                        + ": expected text with no control character but tab";
                    return false;
                }
            }
            try
            {
                value = StrictUtf8.GetString(text);
            }
            catch (DecoderFallbackException)
            {
                error = "is not UTF-8: expected text saved as UTF-8";
                return false;
            }
            return true;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentNullException"><paramref name="value"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> holds a control character
        /// below U+0020 other than tab, starts or ends with a space or tab (the reader trims
        /// them), or holds an unpaired surrogate (no UTF-8 for it).</exception>
        public byte[] Render(string value)
        {
            if (value == null) throw new ArgumentNullException("value");

            foreach (char c in value)
            {
                if (c < 0x20 && c != '\t')
                {
                    throw new ArgumentException("text holding the control character U+"
                        + ((int)c).ToString("X4", CultureInfo.InvariantCulture)
                        + " would not read back", "value");
                }
            }
            if (value.Length > 0 && (CodecText.IsSpaceOrTab(value[0]) || CodecText.IsSpaceOrTab(value[value.Length - 1])))
            {
                throw new ArgumentException(
                    "text starting or ending in a space or tab would not read back, because the reader trims it", "value");
            }
            return StrictUtf8.GetBytes(value);
        }

        /// <inheritdoc/>
        public bool Equal(string a, string b)
        {
            return string.Equals(a, b, StringComparison.Ordinal);
        }
    }
}
