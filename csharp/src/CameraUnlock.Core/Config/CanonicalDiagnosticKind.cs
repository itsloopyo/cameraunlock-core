namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What a <see cref="CanonicalDiagnostic"/> reports. The numbers match the C++
    /// <c>cameraunlock::config::CanonicalDiagnosticKind</c>. New kinds are appended; a number
    /// is never reused or changed.
    /// </summary>
    public enum CanonicalDiagnosticKind
    {
        /// <summary>
        /// Text after a section header's ']' is ignored. Section is the header's name, Value
        /// the ignored text.
        /// </summary>
        TextAfterSectionHeader = 1,

        /// <summary>
        /// A '[' line with no ']'. It ends the section above and names none, so every key up
        /// to the next header is reported as <see cref="KeyOutsideSection"/>. Value is the line.
        /// </summary>
        UnclosedSectionHeader = 2,

        /// <summary>
        /// A header whose name is empty. Nothing matches it, so every key up to the next
        /// header is reported as <see cref="KeyOutsideSection"/>. Value is the line.
        /// </summary>
        EmptySectionName = 3,

        /// <summary>A key line with nothing before its '='. Value is the line.</summary>
        EmptyKey = 4,

        /// <summary>
        /// A line that is not blank, a comment or a header, and has no '='. Value is the line.
        /// </summary>
        MissingEquals = 5,

        /// <summary>
        /// A key line above the first header, or below one that names no section. Key and
        /// Value are the line's.
        /// </summary>
        KeyOutsideSection = 6,

        /// <summary>
        /// A key given more than once in one section, counting every header of that name. The
        /// last occurrence is kept. Lines names every occurrence in order; Section, Key and
        /// Value are the kept ones.
        /// </summary>
        DuplicateKey = 7,

        /// <summary>
        /// [CameraUnlock] has no ConfigFormat, so the file is read as
        /// <see cref="CanonicalIni.ConfigFormat"/>. The line is the section's first header.
        /// </summary>
        ConfigFormatMissing = 8,

        /// <summary>
        /// ConfigFormat is not a format number: digits only, 1 or more. The file is read as
        /// <see cref="CanonicalIni.ConfigFormat"/>. Key and Value are the line's.
        /// </summary>
        ConfigFormatInvalid = 9,

        /// <summary>
        /// ConfigFormat is above <see cref="CanonicalIni.ConfigFormat"/>. It is kept as read
        /// (saturated at <see cref="int.MaxValue"/>), and Key and Value are the line's.
        /// </summary>
        ConfigFormatNewer = 10,
    }
}
