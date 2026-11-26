#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// <see cref="IniEditor"/> against data/fixtures/canonical-ini/editor, which
    /// cpp/tests/ini_editor_tests.cpp runs through the C++ <c>EditIni</c> as well. Each edited
    /// document is read back through <see cref="CanonicalIni"/>, before and after, as
    /// data/fixtures/canonical-ini/README.md describes. The same source runs under xunit on
    /// net8.0 (IniEditorTests) and in the CameraUnlock.Core.FrameworkTests console on .NET
    /// Framework 3.5 and 4.7.2. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class IniEditorFixtures
    {
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        public static string[] Cases(string root)
        {
            string[] names = Directory.GetDirectories(Path.Combine(root, "editor"))
                .Select(d => Path.GetFileName(d))
                .OrderBy(n => n, StringComparer.Ordinal)
                .ToArray();
            if (names.Length == 0) throw new InvalidOperationException("no editor fixtures under " + root);
            return names;
        }

        private sealed class FixtureCase
        {
            public string Name;
            public byte[] Input = new byte[0];
            public readonly List<IniEdit> Edits = new List<IniEdit>();
            public readonly List<IniEdit> Rejected = new List<IniEdit>();
            public bool FirstMode;
            public string[] Refused;
            public byte[] Expected;
        }

        /// <summary>Throws when the case does not behave as its case.tsv and expected.ini say.</summary>
        public static void RunCase(string root, string name)
        {
            FixtureCase c = Load(Path.Combine(Path.Combine(root, "editor"), name), name);
            foreach (IniEdit rejected in c.Rejected)
            {
                bool threw = false;
                try
                {
                    IniEditor.Edit(c.Input, new[] { rejected });
                }
                catch (ArgumentException)
                {
                    threw = true;
                }
                Require(threw, name + ": [" + rejected.Section + "] " + rejected.Key + "=" + rejected.Value + " was not rejected");
            }

            IniEditResult result = IniEditor.Edit(c.Input, c.Edits);
            if (c.Refused != null)
            {
                Require(!result.Succeeded, name + ": was not refused");
                Require(result.Refusal.ToString() == c.Refused[1], name + ": refusal is " + result.Refusal + ", expected " + c.Refused[1]);
                Require((result.Section ?? "") == Text(Unescape(c.Refused[2])) && (result.Key ?? "") == Text(Unescape(c.Refused[3])),
                    name + ": refusal names [" + result.Section + "] " + result.Key);
                int[] lines = c.Refused[4] == "-" ? new int[0] : c.Refused[4].Split(',').Select(n => int.Parse(n, CultureInfo.InvariantCulture)).ToArray();
                Require(result.Lines.SequenceEqual(lines), name + ": refusal names lines " + string.Join(",", result.Lines.Select(n => n.ToString(CultureInfo.InvariantCulture)).ToArray()));
                bool threw = false;
                try
                {
                    byte[] unused = result.Bytes;
                }
                catch (InvalidOperationException)
                {
                    threw = true;
                }
                Require(threw, name + ": a refused result handed out bytes");
                return;
            }

            Require(result.Succeeded, name + ": was refused (" + result.Refusal + ")");
            Require(result.Bytes.SequenceEqual(c.Expected), name + ": output does not match expected.ini byte for byte");
            ReadBack(c, result.Bytes);
        }

        private static FixtureCase Load(string dir, string name)
        {
            var c = new FixtureCase { Name = name };
            string input = Path.Combine(dir, "input.ini");
            if (File.Exists(input)) c.Input = File.ReadAllBytes(input);

            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(Path.Combine(dir, "case.tsv"))).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string directive = line.Split('\t')[0];
                switch (directive)
                {
                    case "set":
                    case "set_or_insert":
                    case "set_first":
                    case "set_or_insert_first":
                    case "rejects":
                        string[] f = line.Split(new[] { '\t' }, 4);
                        Require(f.Length == 4, name + ": malformed edit: " + line);
                        bool first = directive == "set_first" || directive == "set_or_insert_first";
                        var edit = new IniEdit(Text(Unescape(f[1])), Text(Unescape(f[2])), Text(Unescape(f[3])),
                            directive != "set" && directive != "set_first", first);
                        if (directive == "rejects")
                        {
                            c.Rejected.Add(edit);
                        }
                        else
                        {
                            c.Edits.Add(edit);
                            c.FirstMode = c.FirstMode || first;
                        }
                        break;
                    case "refused":
                        c.Refused = line.Split(new[] { '\t' }, 5);
                        Require(c.Refused.Length == 5, name + ": malformed refusal: " + line);
                        break;
                    default:
                        throw new InvalidOperationException(name + ": unknown directive '" + directive + "'");
                }
            }
            if (c.Refused == null) c.Expected = File.ReadAllBytes(Path.Combine(dir, "expected.ini"));
            return c;
        }

        private static void ReadBack(FixtureCase c, byte[] output)
        {
            string name = c.Name;
            CanonicalIni before = CanonicalIni.Parse(c.Input);
            CanonicalIni after = CanonicalIni.Parse(output);
            Require(before.IsReadable && after.IsReadable, name + ": a document is unreadable");

            var replaced = new HashSet<int>();
            foreach (IniEdit edit in c.Edits)
            {
                CanonicalValue v = before.Find(edit.Section, edit.Key);
                if (v == null) continue;
                replaced.Add(edit.FirstOccurrenceWins && v.EarlierLines.Count > 0 ? v.EarlierLines[0] : v.Line);
            }

            // Walk both line lists: an unchanged line is the same bytes, a replaced one pairs
            // with its replacement, and anything else in `after` was inserted.
            List<byte[]> b = Lines(c.Input);
            List<byte[]> a = Lines(output);
            var moved = new Dictionary<int, int>();
            var changed = new HashSet<int>();
            int j = 0;
            for (int i = 0; i < a.Count; i++)
            {
                if (j < b.Count && replaced.Contains(j + 1))
                {
                    moved[j + 1] = i + 1;
                    changed.Add(i + 1);
                    j++;
                }
                else if (j < b.Count && Kept(a[i], b[j]))
                {
                    moved[j + 1] = i + 1;
                    j++;
                }
                else
                {
                    changed.Add(i + 1);
                }
            }
            Require(j == b.Count, name + ": a line the batch did not replace was lost or moved");

            var held = new Dictionary<IniEdit, int>();
            foreach (int number in changed)
            {
                byte[] content = Content(a[number - 1]);
                string value;
                IniEdit edit = EditOnLine(c, content, out value);
                if (edit != null)
                {
                    Require(value == edit.Value, name + ": line " + number + " holds " + edit.Key + "=" + value);
                    int count;
                    held.TryGetValue(edit, out count);
                    held[edit] = count + 1;
                    continue;
                }
                bool header = c.Edits.Any(e => Text(content) == "[" + e.Section + "]");
                Require(content.Length == 0 || header, name + ": line " + number + " changed and holds no edit");
            }
            foreach (IniEdit edit in c.Edits)
            {
                int count;
                held.TryGetValue(edit, out count);
                Require(count == 1, name + ": " + edit.Key + " is on " + count + " changed lines, not one");
            }

            foreach (CanonicalSection section in before.Sections)
            {
                foreach (CanonicalValue v in section.Values)
                {
                    if (Edited(c, section.Name, v.Key)) continue;
                    CanonicalValue now = Find(after, section.Name, v.Key);
                    Require(now != null && now.Key.SequenceEqual(v.Key) && now.Value.SequenceEqual(v.Value),
                        name + ": " + Hex(v.Key) + " no longer reads as before");
                }
            }
            foreach (CanonicalSection section in after.Sections)
            {
                foreach (CanonicalValue v in section.Values)
                {
                    Require(Edited(c, section.Name, v.Key) || Find(before, section.Name, v.Key) != null,
                        name + ": " + Hex(v.Key) + " appeared from nowhere");
                }
            }

            if (c.FirstMode) return;

            foreach (IniEdit edit in c.Edits)
            {
                CanonicalValue v = after.Find(edit.Section, edit.Key);
                Require(v != null && Text(v.Value) == edit.Value && changed.Contains(v.Line),
                    name + ": " + edit.Key + " does not read its new value from the line the batch changed");
            }

            var keptBefore = new List<string>();
            foreach (CanonicalDiagnostic d in before.Diagnostics)
            {
                if (d.Lines.Any(replaced.Contains)) continue;
                keptBefore.Add(Describe(d, d.Lines.Select(n => moved[n])));
            }
            var keptAfter = new List<string>();
            foreach (CanonicalDiagnostic d in after.Diagnostics)
            {
                if (!d.Lines.Any(changed.Contains)) keptAfter.Add(Describe(d, d.Lines));
            }
            Require(keptBefore.SequenceEqual(keptAfter, StringComparer.Ordinal),
                name + ": the diagnostics differ on lines the batch did not change:\n  before: "
                + string.Join("; ", keptBefore.ToArray()) + "\n  after: " + string.Join("; ", keptAfter.ToArray()));
        }

        private static string Describe(CanonicalDiagnostic d, IEnumerable<int> lines)
        {
            return d.Kind + " " + string.Join(",", lines.Select(n => n.ToString(CultureInfo.InvariantCulture)).ToArray())
                + " [" + Hex(d.Section) + "] " + Hex(d.Key) + "=" + Hex(d.Value);
        }

        private static string Hex(byte[] bytes)
        {
            return BitConverter.ToString(bytes);
        }

        // Each line with its terminator, as the canonical reader splits them: CRLF, LF or a
        // lone CR, after a UTF-8 byte order mark at offset 0.
        private static List<byte[]> Lines(byte[] bytes)
        {
            var lines = new List<byte[]>();
            int pos = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
            while (pos < bytes.Length)
            {
                int end = pos;
                while (end < bytes.Length && bytes[end] != '\r' && bytes[end] != '\n') end++;
                if (end < bytes.Length)
                {
                    end += bytes[end] == '\r' && end + 1 < bytes.Length && bytes[end + 1] == '\n' ? 2 : 1;
                }
                lines.Add(Slice(bytes, pos, end));
                pos = end;
            }
            return lines;
        }

        private static byte[] WithoutEnding(byte[] line)
        {
            int end = line.Length;
            while (end > 0 && (line[end - 1] == '\r' || line[end - 1] == '\n')) end--;
            return Slice(line, 0, end);
        }

        // An unterminated last line gains the line ending that joins new text onto it.
        private static bool Kept(byte[] after, byte[] before)
        {
            if (after.SequenceEqual(before)) return true;
            return WithoutEnding(before).Length == before.Length && WithoutEnding(after).SequenceEqual(before);
        }

        private static byte[] Content(byte[] line)
        {
            byte[] bytes = WithoutEnding(line);
            int begin = 0;
            int end = bytes.Length;
            while (begin < end && (bytes[begin] == ' ' || bytes[begin] == '\t')) begin++;
            while (end > begin && (bytes[end - 1] == ' ' || bytes[end - 1] == '\t')) end--;
            return Slice(bytes, begin, end);
        }

        private static IniEdit EditOnLine(FixtureCase c, byte[] content, out string value)
        {
            value = null;
            int equals = Array.IndexOf(content, (byte)'=');
            if (content.Length == 0 || content[0] == '[' || content[0] == ';' || content[0] == '#' || equals < 0) return null;
            byte[] key = Content(Slice(content, 0, equals));
            foreach (IniEdit edit in c.Edits)
            {
                if (!EqualsAsciiIgnoreCase(key, Encoding.ASCII.GetBytes(edit.Key))) continue;
                value = Text(Content(Slice(content, equals + 1, content.Length)));
                return edit;
            }
            return null;
        }

        private static bool Edited(FixtureCase c, byte[] section, byte[] key)
        {
            return c.Edits.Any(e => EqualsAsciiIgnoreCase(section, Encoding.ASCII.GetBytes(e.Section))
                && EqualsAsciiIgnoreCase(key, Encoding.ASCII.GetBytes(e.Key)));
        }

        private static CanonicalValue Find(CanonicalIni doc, byte[] section, byte[] key)
        {
            foreach (CanonicalSection s in doc.Sections)
            {
                if (!EqualsAsciiIgnoreCase(s.Name, section)) continue;
                foreach (CanonicalValue v in s.Values)
                {
                    if (EqualsAsciiIgnoreCase(v.Key, key)) return v;
                }
            }
            return null;
        }

        private static bool EqualsAsciiIgnoreCase(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (Fold(a[i]) != Fold(b[i])) return false;
            }
            return true;
        }

        private static byte Fold(byte b)
        {
            return b >= 'A' && b <= 'Z' ? (byte)(b - 'A' + 'a') : b;
        }

        private static byte[] Slice(byte[] bytes, int begin, int end)
        {
            var slice = new byte[end - begin];
            Array.Copy(bytes, begin, slice, 0, slice.Length);
            return slice;
        }

        // The byte escape data/fixtures/canonical-ini/README.md defines.
        private static byte[] Unescape(string field)
        {
            var bytes = new List<byte>();
            for (int i = 0; i < field.Length; i++)
            {
                if (field[i] != '\\')
                {
                    bytes.Add((byte)field[i]);
                    continue;
                }
                if (i + 1 < field.Length && field[i + 1] == '\\')
                {
                    bytes.Add((byte)'\\');
                    i++;
                    continue;
                }
                if (i + 3 < field.Length && field[i + 1] == 'x')
                {
                    bytes.Add(byte.Parse(field.Substring(i + 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                    i += 3;
                    continue;
                }
                throw new InvalidOperationException("bad escape in fixture field '" + field + "'");
            }
            return bytes.ToArray();
        }

        private static string Text(byte[] bytes)
        {
            return StrictUtf8.GetString(bytes);
        }

        private static void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
        }
    }
}
