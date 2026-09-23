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

        /// <summary>
        /// The edit asks for an absent key to be inserted, and its section header appears
        /// more than once, so there is no one place to put it.
        /// </summary>
        DuplicateSection = 5,

        /// <summary>
        /// The edit's key appears more than once in its section, counting every header of
        /// that name. Which occurrence counts depends on the reader, so the caller decides
        /// with its own.
        /// </summary>
        DuplicateKey = 6,

        /// <summary>The key is absent and the edit did not ask for it to be inserted.</summary>
        KeyNotFound = 7,

        /// <summary>
        /// A line starts with white space other than a space or tab (a form feed, vertical
        /// tab, no-break space and the like), or a key ends in it. The C# flat reader trims
        /// it and the C++ one does not, so the line has no single reading to edit against.
        /// </summary>
        AmbiguousWhitespace = 8,

        /// <summary>
        /// A SUB byte (0x1A, Ctrl-Z). The C++ flat reader reads through a text-mode stream,
        /// which the Microsoft C runtime ends at that byte, and this one reads past it, so
        /// nothing after it has a single reading.
        /// </summary>
        SubByte = 9,
    }
}
