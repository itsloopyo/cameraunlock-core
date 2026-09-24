namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Whether <see cref="CanonicalIni.Parse"/> interpreted the document. The numbers match
    /// the C++ <c>cameraunlock::config::CanonicalReadStatus</c>.
    /// </summary>
    public enum CanonicalReadStatus
    {
        Readable = 0,

        /// <summary>The bytes start with a UTF-16 byte order mark, FF FE or FE FF.</summary>
        Utf16 = 1,

        /// <summary>The bytes hold a 0x00 byte, which also catches UTF-16 without a mark.</summary>
        NulByte = 2,
    }
}
