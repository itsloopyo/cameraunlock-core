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
    /// </summary>
    public sealed class IniEdit
    {
        public IniEdit(string section, string key, string value, bool insertIfAbsent)
        {
            if (section == null) throw new ArgumentNullException(nameof(section));
            if (key == null) throw new ArgumentNullException(nameof(key));
            if (value == null) throw new ArgumentNullException(nameof(value));
            Section = section;
            Key = key;
            Value = value;
            InsertIfAbsent = insertIfAbsent;
        }

        public string Section { get; }
        public string Key { get; }
        public string Value { get; }
        public bool InsertIfAbsent { get; }
    }
}
