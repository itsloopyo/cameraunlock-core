namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Why <see cref="IniEditor.Edit"/> produced no document. The numbers match the C++
    /// <c>cameraunlock::IniEditRefusal</c>.
    /// </summary>
    public enum IniEditRefusal
    {
        None = 0,

        /// <summary>The document starts with a UTF-16 byte order mark, either byte order.</summary>
        Utf16 = 1,

        InvalidUtf8 = 2,

        NulByte = 3,

        /// <summary>
        /// A CR not followed by LF. The C# and C++ flat readers split such a line
        /// differently, so no edit of it can be checked against both.
        /// </summary>
        LoneCarriageReturn = 4,

        /// <summary>The edit's section header appears more than once.</summary>
        DuplicateSection = 5,

        /// <summary>
        /// The edit's key appears more than once in its section. Which occurrence counts
        /// depends on the reader, so the caller decides with its own.
        /// </summary>
        DuplicateKey = 6,

        /// <summary>The key is absent and the edit did not ask for it to be inserted.</summary>
        KeyNotFound = 7,
    }
}
