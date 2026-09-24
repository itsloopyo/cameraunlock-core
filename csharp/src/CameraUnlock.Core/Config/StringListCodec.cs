namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A list of <see cref="StringCodec"/> items. Read: an empty value is an empty list;
    /// otherwise it is split at ',' and each item, trimmed of spaces and tabs, must be
    /// non-empty and read as a string. Written: the items joined by ", ".
    /// </summary>
    public sealed class StringListCodec : IValueCodec<string[]>
    {
        private static readonly StringCodec Item = new StringCodec();

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out string[] value, out string? error)
#else
        public bool TryParse(byte[] text, out string[] value, out string error)
#endif
        {
            return CodecText.TryParseList(text, Item, out value, out error);
        }

        /// <inheritdoc/>
        /// <exception cref="System.ArgumentNullException"><paramref name="value"/> is null.</exception>
        /// <exception cref="System.ArgumentException">An item cannot be written as a string, or
        /// is empty or holds ',', which would not read back as one item.</exception>
        public byte[] Render(string[] value)
        {
            return CodecText.RenderList(value, Item);
        }

        /// <inheritdoc/>
        /// <exception cref="System.ArgumentNullException"><paramref name="a"/> or <paramref name="b"/> is null.</exception>
        public bool Equal(string[] a, string[] b)
        {
            return CodecText.ListEqual(a, b, Item);
        }
    }
}
