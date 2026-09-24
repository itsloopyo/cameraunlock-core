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

        /// <summary>
        /// <see cref="ConfigTable{TConfig}.Apply"/>: a value its row's codec does not read. The row
        /// keeps its default. Section, Key and Value are the line's; Detail is what the codec expected.
        /// </summary>
        InvalidValue = 11,

        /// <summary>
        /// <see cref="ConfigTable{TConfig}.Apply"/>: a section the table has no row in, so nothing in
        /// it is read. Section is its name; the line is its first header's.
        /// </summary>
        UnknownSection = 12,

        /// <summary>
        /// <see cref="ConfigTable{TConfig}.Apply"/>: a key in a section the table reads that no row
        /// names and that is no retired or non-canonical concept, so it is not read. Section, Key and
        /// Value are the line's.
        /// </summary>
        UnknownKey = 13,

        /// <summary>
        /// <see cref="ConfigTable{TConfig}.Apply"/>: a key naming a retired concept (the schema's
        /// retired list), in any section but [CameraUnlock]. It is not read. Section, Key and Value
        /// are the line's.
        /// </summary>
        RetiredKey = 14,

        /// <summary>
        /// <see cref="ConfigTable{TConfig}.Apply"/>: a key naming a concept the canonical format does
        /// not write, in any section but [CameraUnlock]. It is not read. Section, Key and Value are the
        /// line's; Detail is the schema's canonical_reason, the line saying why.
        /// </summary>
        NonCanonicalConcept = 15,

        /// <summary>
        /// <see cref="ConfigTable{TConfig}.Apply"/>: RotationEnabled and PositionEnabled are both
        /// false, which is no tracking mode, so both take the table's defaults. Lines are the lines
        /// that set them.
        /// </summary>
        NoTrackingMode = 16,
    }
}
