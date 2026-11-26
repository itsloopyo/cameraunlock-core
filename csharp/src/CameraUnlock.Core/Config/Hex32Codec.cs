using System.Globalization;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// <c>0x</c> and upper-case hex digits without padding: <c>0x404</c>, <c>0x0</c>. Reads
    /// <c>0x</c> or <c>0X</c> and 1 to 8 digits of either case, leading zeros included.
    /// </summary>
    public sealed class Hex32Codec : IValueCodec<uint>
    {
        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out uint value, out string? error)
#else
        public bool TryParse(byte[] text, out uint value, out string error)
#endif
        {
            ulong read;
            bool ok = CodecText.TryParseHex(text, 8, out read, out error);
            value = (uint)read;
            return ok;
        }

        /// <inheritdoc/>
        public byte[] Render(uint value)
        {
            return CodecText.Ascii("0x" + value.ToString("X", CultureInfo.InvariantCulture));
        }

        /// <inheritdoc/>
        public bool Equal(uint a, uint b)
        {
            return a == b;
        }
    }
}
