using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One finding about a canonical INI document. Returned, never logged, so a caller can
    /// read the file before its logger exists and report afterwards. Every diagnostic names
    /// at least one line. The byte fields hold the file's own bytes, and every field is empty
    /// where <see cref="CanonicalDiagnosticKind"/> says the kind does not use it.
    /// </summary>
    public sealed class CanonicalDiagnostic
    {
        internal CanonicalDiagnostic(CanonicalDiagnosticKind kind, int[] lines, byte[] section, byte[] key, byte[] value)
            : this(kind, lines, section, key, value, string.Empty)
        {
        }

        internal CanonicalDiagnostic(CanonicalDiagnosticKind kind, int[] lines, byte[] section, byte[] key, byte[] value,
            string detail)
        {
            Kind = kind;
            Lines = new ReadOnlyCollection<int>(lines);
            Section = section;
            Key = key;
            Value = value;
            Detail = detail;
        }

        public CanonicalDiagnosticKind Kind { get; }

        /// <summary>1-based, ascending.</summary>
        public ReadOnlyCollection<int> Lines { get; }

        public byte[] Section { get; }

        public byte[] Key { get; }

        public byte[] Value { get; }

        /// <summary>The kind's explanation, where <see cref="CanonicalDiagnosticKind"/> names one.</summary>
        public string Detail { get; }

        /// <summary>
        /// One sentence for the player, naming the line or lines, e.g.
        /// <c>[General] ToggleKey is set on lines 3 and 9. Line 9 is used.</c> The file's bytes
        /// are shown as UTF-8, with U+FFFD for a byte that is not.
        /// </summary>
        public string Describe()
        {
            string line = (Lines.Count > 1 ? "Lines " : "Line ") + JoinLines() + ": ";
            string format = CanonicalIni.ConfigFormat.ToString(CultureInfo.InvariantCulture);
            string section = Text(Section);
            string key = Text(Key);
            string value = Text(Value);
            switch (Kind)
            {
                case CanonicalDiagnosticKind.TextAfterSectionHeader:
                    return line + "\"" + value + "\" after [" + section + "] is ignored. Comments go on their own line.";
                case CanonicalDiagnosticKind.UnclosedSectionHeader:
                    return line + "\"" + value
                        + "\" has no closing ], so the settings below it are ignored up to the next section header.";
                case CanonicalDiagnosticKind.EmptySectionName:
                    return line + "\"" + value
                        + "\" names no section, so the settings below it are ignored up to the next section header.";
                case CanonicalDiagnosticKind.EmptyKey:
                    return line + "\"" + value + "\" has no setting name before the =, so it is ignored.";
                case CanonicalDiagnosticKind.MissingEquals:
                    return line + "\"" + value + "\" is not a setting (it has no =), so it is ignored.";
                case CanonicalDiagnosticKind.KeyOutsideSection:
                    return line + key + " is not under a section header, so it is ignored.";
                case CanonicalDiagnosticKind.DuplicateKey:
                    return "[" + section + "] " + key + " is set on lines " + JoinLines() + ". Line "
                        + Lines[Lines.Count - 1].ToString(CultureInfo.InvariantCulture) + " is used.";
                case CanonicalDiagnosticKind.ConfigFormatMissing:
                    return line + "[" + section + "] has no ConfigFormat, so the file is read as format " + format + ".";
                case CanonicalDiagnosticKind.ConfigFormatInvalid:
                    return line + key + "=" + value + " is not a format number, so the file is read as format " + format + ".";
                case CanonicalDiagnosticKind.ConfigFormatNewer:
                    return line + key + "=" + value + " was written by a newer version of the mod. This version reads format "
                        + format + ".";
                case CanonicalDiagnosticKind.InvalidValue:
                    return line + "[" + section + "] " + key + "=" + value + " is not valid (" + Detail
                        + "), so the default is used.";
                case CanonicalDiagnosticKind.UnknownSection:
                    return line + "[" + section + "] is not a section this mod reads, so everything in it is ignored.";
                case CanonicalDiagnosticKind.UnknownKey:
                    return line + "[" + section + "] " + key + " is not a setting this mod reads, so it is ignored.";
                case CanonicalDiagnosticKind.RetiredKey:
                    return line + "[" + section + "] " + key + " is a setting this mod no longer uses, so it is ignored.";
                case CanonicalDiagnosticKind.NonCanonicalConcept:
                    return line + "[" + section + "] " + key + " is ignored. " + Detail;
                case CanonicalDiagnosticKind.NoTrackingMode:
                    return line + "RotationEnabled and PositionEnabled are both false, which is not a tracking mode, "
                        + "so both are read as their defaults.";
                case CanonicalDiagnosticKind.MisplacedKey:
                    return line + "[" + section + "] " + key + " is ignored. This mod reads it as " + Detail + ".";
                default:
                    throw new InvalidOperationException("CanonicalDiagnosticKind " + (int)Kind + " has no description");
            }
        }

        private string JoinLines()
        {
            var text = new StringBuilder();
            for (int i = 0; i < Lines.Count; i++)
            {
                if (i > 0) text.Append(i + 1 == Lines.Count ? " and " : ", ");
                text.Append(Lines[i].ToString(CultureInfo.InvariantCulture));
            }
            return text.ToString();
        }

        private static string Text(byte[] bytes)
        {
            return Encoding.UTF8.GetString(bytes);
        }
    }
}
