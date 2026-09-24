namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A list of <see cref="Hex64Codec"/> items. Read: an empty value is an empty list;
    /// otherwise it is split at ',' and each item, trimmed of spaces and tabs, must be
    /// non-empty and a hex64. Written: the items joined by ", ".
    /// </summary>
    public sealed class Hex64ListCodec : IValueCodec<ulong[]>
    {
        private static readonly Hex64Codec Item = new Hex64Codec();

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out ulong[] value, out string? error)
#else
        public bool TryParse(byte[] text, out ulong[] value, out string error)
#endif
        {
            return CodecText.TryParseList(text, Item, out value, out error);
        }

        /// <inheritdoc/>
        /// <exception cref="System.ArgumentNullException"><paramref name="value"/> is null.</exception>
        public byte[] Render(ulong[] value)
        {
            return CodecText.RenderList(value, Item);
        }

        /// <inheritdoc/>
        /// <exception cref="System.ArgumentNullException"><paramref name="a"/> or <paramref name="b"/> is null.</exception>
        public bool Equal(ulong[] a, ulong[] b)
        {
            return CodecText.ListEqual(a, b, Item);
        }
    }
}
