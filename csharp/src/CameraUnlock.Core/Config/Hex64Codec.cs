using System.Globalization;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// <c>0x</c> and upper-case hex digits without padding: <c>0x404</c>, <c>0x0</c>. Reads
    /// <c>0x</c> or <c>0X</c> and 1 to 16 digits of either case, leading zeros included.
    /// </summary>
    public sealed class Hex64Codec : IValueCodec<ulong>
    {
        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out ulong value, out string? error)
#else
        public bool TryParse(byte[] text, out ulong value, out string error)
#endif
        {
            ulong read;
            bool ok = CodecText.TryParseHex(text, 16, out read, out error);
            value = read;
            return ok;
        }

        /// <inheritdoc/>
        public byte[] Render(ulong value)
        {
            return CodecText.Ascii("0x" + value.ToString("X", CultureInfo.InvariantCulture));
        }

        /// <inheritdoc/>
        public bool Equal(ulong a, ulong b)
        {
            return a == b;
        }
    }
}
