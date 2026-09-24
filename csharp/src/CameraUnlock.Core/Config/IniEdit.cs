using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One change to a canonical INI document: give <see cref="Key"/> in
    /// <c>[<see cref="Section"/>]</c> the value <see cref="Value"/>.
    /// <para>
    /// Section and key match ASCII case-insensitively, and a replaced line keeps the file's
    /// own spelling. When the key is absent and <see cref="InsertIfAbsent"/> is set, the
    /// line is added as <c>Key=Value</c>, and a missing section as <c>[Section]</c>, spelled
    /// exactly as given here. Section, key and value are printable ASCII (see
    /// <see cref="IniEditor.Edit"/>).
    /// </para>
    /// <para>
    /// By default an edit follows the canonical reader, which keeps the last occurrence of a
    /// repeated key: that occurrence is replaced, and an absent key goes into the last block
    /// of a section whose header repeats. Set <see cref="FirstOccurrenceWins"/> for a reader
    /// that takes the first occurrence instead, GetPrivateProfileStringA for one: the first
    /// occurrence in the document is replaced, and an absent key goes into the first block.
    /// </para>
    /// </summary>
    public sealed class IniEdit
    {
        public IniEdit(string section, string key, string value, bool insertIfAbsent)
            : this(section, key, value, insertIfAbsent, false)
        {
        }

        public IniEdit(string section, string key, string value, bool insertIfAbsent, bool firstOccurrenceWins)
        {
            if (section == null) throw new ArgumentNullException(nameof(section));
            if (key == null) throw new ArgumentNullException(nameof(key));
            if (value == null) throw new ArgumentNullException(nameof(value));
            Section = section;
            Key = key;
            Value = value;
            InsertIfAbsent = insertIfAbsent;
            FirstOccurrenceWins = firstOccurrenceWins;
        }

        public string Section { get; }
        public string Key { get; }
        public string Value { get; }
        public bool InsertIfAbsent { get; }
        public bool FirstOccurrenceWins { get; }
    }
}
