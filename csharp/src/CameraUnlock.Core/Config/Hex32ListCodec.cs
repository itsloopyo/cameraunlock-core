namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A list of <see cref="Hex32Codec"/> items. Read: an empty value is an empty list;
    /// otherwise it is split at ',' and each item, trimmed of spaces and tabs, must be
    /// non-empty and a hex32. Written: the items joined by ", ".
    /// </summary>
    public sealed class Hex32ListCodec : IValueCodec<uint[]>
    {
        private static readonly Hex32Codec Item = new Hex32Codec();

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out uint[] value, out string? error)
#else
        public bool TryParse(byte[] text, out uint[] value, out string error)
#endif
        {
            return CodecText.TryParseList(text, Item, out value, out error);
        }

        /// <inheritdoc/>
        /// <exception cref="System.ArgumentNullException"><paramref name="value"/> is null.</exception>
        public byte[] Render(uint[] value)
        {
            return CodecText.RenderList(value, Item);
        }

        /// <inheritdoc/>
        /// <exception cref="System.ArgumentNullException"><paramref name="a"/> or <paramref name="b"/> is null.</exception>
        public bool Equal(uint[] a, uint[] b)
        {
            return CodecText.ListEqual(a, b, Item);
        }
    }
}
