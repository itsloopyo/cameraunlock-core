using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One change to an INI document: give <see cref="Key"/> in
    /// <c>[<see cref="Section"/>]</c> the value <see cref="Value"/>.
    /// <para>
    /// Section and key match ASCII case-insensitively, and a replaced line keeps the file's
    /// own spelling. When the key is absent and <see cref="InsertIfAbsent"/> is set, the
    /// line is added as <c>Key=Value</c>, and a missing section as <c>[Section]</c>, spelled
    /// exactly as given here. The value is written verbatim, so it must be one the flat
    /// readers read back unchanged (see <see cref="IniEditor.Edit"/>).
    /// </para>
    /// <para>
    /// Set <see cref="FirstOccurrenceWins"/> for a reader that takes the first occurrence
    /// of a key, GetPrivateProfileStringA for one. A key that appears more than once in its
    /// section, counting every header of that name, then has its first occurrence in the
    /// document replaced and the others left as they are, and an absent key goes under the
    /// first of several headers of its section. Unset, those edits are refused as
    /// <see cref="IniEditRefusal.DuplicateKey"/> and
    /// <see cref="IniEditRefusal.DuplicateSection"/>.
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
