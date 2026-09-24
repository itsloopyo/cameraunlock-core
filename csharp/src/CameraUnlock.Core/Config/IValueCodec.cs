namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A canonical value codec: how a value is written in a canonical config file and how it
    /// is read back. The C++ codecs in cameraunlock/config/value_codecs.h write the same
    /// text, and data/fixtures/canonical-ini/codecs holds both languages to it.
    /// </summary>
    /// <typeparam name="T">The value type.</typeparam>
    public interface IValueCodec<T>
    {
        /// <summary>
        /// Reads a value as the canonical reader delivers it (<see cref="CanonicalValue.Value"/>,
        /// already trimmed of spaces and tabs). Nothing in the bytes makes it throw.
        /// </summary>
        /// <param name="text">The value's bytes.</param>
        /// <param name="value">The value read. On failure the type's default, or empty for a
        /// string or an array.</param>
        /// <param name="error">What was expected, for the diagnostic that also names the line
        /// and the value; null when the value was read.</param>
        /// <exception cref="System.ArgumentNullException"><paramref name="text"/> is null.</exception>
#if NULLABLE_ENABLED
        bool TryParse(byte[] text, out T value, out string? error);
#else
        bool TryParse(byte[] text, out T value, out string error);
#endif

        /// <summary>
        /// The canonical text of a value, which <see cref="TryParse"/> reads back as an
        /// <see cref="Equal"/> value.
        /// </summary>
        /// <exception cref="System.ArgumentException">The value would not read back.</exception>
        byte[] Render(T value);

        /// <summary>What verification compares with. Floats compare bitwise.</summary>
        bool Equal(T a, T b);
    }
}
