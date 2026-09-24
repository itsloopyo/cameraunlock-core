namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Why <see cref="IniEditor.Edit"/> produced no document. The numbers match the C++
    /// <c>cameraunlock::IniEditRefusal</c>.
    /// </summary>
    public enum IniEditRefusal
    {
        None = 0,

        /// <summary>
        /// The document starts with a UTF-16 byte order mark, either byte order. The canonical
        /// reader cannot read it.
        /// </summary>
        Utf16 = 1,

        /// <summary>The document holds a 0x00 byte. The canonical reader cannot read it.</summary>
        NulByte = 2,

        /// <summary>The key is absent and the edit did not ask for it to be inserted.</summary>
        KeyNotFound = 3,
    }
}
