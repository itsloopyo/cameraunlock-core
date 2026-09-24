using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Applies a batch of <see cref="IniEdit"/>s to the bytes of a canonical INI document,
    /// touching nothing else. Pure: no file I/O. The C++ twin is <c>cameraunlock::EditIni</c>,
    /// and both are pinned to the same byte fixtures under data/fixtures/canonical-ini/editor.
    /// </summary>
    public static class IniEditor
    {
        private enum LineKind
        {
            Other,
            Blank,
            Comment,
            Section,
            Key,
        }

        private sealed class Line
        {
            public int Begin;
            public int ContentEnd;
            public int End;
            public LineKind Kind = LineKind.Other;
            // A section's name, or a key line's key.
            public int NameBegin;
            public int NameEnd;
            // A header with no ']' is still a section boundary, but names no section.
            public bool Named;
            public int ValueBegin;
        }

        private sealed class EncodedEdit
        {
            public readonly IniEdit Edit;
            public readonly byte[] Section;
            public readonly byte[] Key;
            public readonly byte[] Value;

            public EncodedEdit(IniEdit edit, byte[] section, byte[] key, byte[] value)
            {
                Edit = edit;
                Section = section;
                Key = key;
                Value = value;
            }
        }

        private sealed class NewSection
        {
            public readonly byte[] Name;
            public readonly List<byte[]> Lines = new List<byte[]>();

            public NewSection(byte[] name)
            {
                Name = name;
            }
        }

        /// <summary>
        /// Applies <paramref name="edits"/> to <paramref name="original"/>. An absent file is
        /// an empty array.
        /// <para>
        /// The editor never decodes. It reads lines as <see cref="CanonicalIni.Parse"/> does:
        /// CRLF, LF and a lone CR each end a line; a UTF-8 byte order mark at offset 0 is
        /// skipped, and kept in the output; each line is trimmed of spaces and tabs only; a
        /// line starting ';' or '#' is a comment; one starting '[' is a section header named
        /// by the text up to its first ']', and one with no ']' ends the section above it and
        /// names none; any other line with text before its first '=' is a key line. Keys above
        /// the first header belong to no section and are never matched. Headers that repeat a
        /// section's name make one section. An edit's key is ASCII, so it never matches a key
        /// holding any other byte. Every byte the edits do not change is copied through,
        /// whatever its encoding.
        /// </para>
        /// <para>
        /// The document is refused whole, with no bytes produced, when it starts with a UTF-16
        /// byte order mark or holds a NUL, which the canonical reader cannot read either, or
        /// when an edit's key is absent and not to be inserted.
        /// </para>
        /// <para>
        /// A replacement rewrites everything after the '=' and the spaces and tabs after it, up
        /// to the line terminator: there are no inline comments. The key's spelling, the white
        /// space around '=' and the terminator are kept.
        /// </para>
        /// <para>
        /// An insertion goes after the last key line of the section's last block, or of its
        /// first block when the edit says the first occurrence wins, and straight after the
        /// header when that block has no key line. A missing section is appended at the end of
        /// the file after a blank line. New lines end with the file's most common line ending,
        /// CRLF on a tie and then LF, except that CRLF is written where a lone CR or LF would
        /// pair with a CR before it or an LF after it into one CRLF, which would lose a line.
        /// A file whose last line has no terminator still ends
        /// without one: the new text is joined on with a line ending in front instead of
        /// behind.
        /// </para>
        /// </summary>
        /// <exception cref="ArgumentException">
        /// An edit cannot be written so that it reads back as given: a section, key or value
        /// holding a character outside printable ASCII (U+0020 to U+007E), an empty section or
        /// key, a section, key or value with a leading or trailing space, a section holding
        /// ']', a key holding '=' or starting '[', ';' or '#', or two edits of the same key. A
        /// value may be empty.
        /// </exception>
        public static IniEditResult Edit(byte[] original, IList<IniEdit> edits)
        {
            if (original == null) throw new ArgumentNullException(nameof(original));
            if (edits == null) throw new ArgumentNullException(nameof(edits));

            List<EncodedEdit> encoded = ValidateEdits(edits);
            byte[] s = original;

            if (s.Length >= 2 && ((s[0] == 0xFF && s[1] == 0xFE) || (s[0] == 0xFE && s[1] == 0xFF)))
            {
                return Refuse(IniEditRefusal.Utf16, null, new int[0]);
            }
            int nul = Array.IndexOf(s, (byte)0);
            if (nul >= 0)
            {
                return Refuse(IniEditRefusal.NulByte, null, new[] { LineOfOffset(s, nul) });
            }
            int body = s.Length >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF ? 3 : 0;

            var lines = new List<Line>();
            int crlfCount = 0;
            int lfCount = 0;
            int crCount = 0;
            for (int begin = body; begin < s.Length;)
            {
                var line = new Line { Begin = begin };
                int end = begin;
                while (end < s.Length && !IsLineEnd(s[end])) end++;
                line.ContentEnd = end;
                if (end == s.Length)
                {
                    line.End = end;
                }
                else if (s[end] == '\r' && end + 1 < s.Length && s[end + 1] == '\n')
                {
                    line.End = end + 2;
                    crlfCount++;
                }
                else
                {
                    line.End = end + 1;
                    if (s[end] == '\n')
                    {
                        lfCount++;
                    }
                    else
                    {
                        crCount++;
                    }
                }
                ClassifyLine(s, line);
                lines.Add(line);
                begin = line.End;
            }
            byte[] eol = crlfCount >= lfCount && crlfCount >= crCount
                ? new[] { (byte)'\r', (byte)'\n' }
                : new[] { lfCount >= crCount ? (byte)'\n' : (byte)'\r' };

            // The section each line belongs to, as the index of its header line.
            var headers = new List<int>();
            var owner = new int[lines.Count];
            for (int i = 0; i < lines.Count; i++)
            {
                if (lines[i].Kind == LineKind.Section) headers.Add(i);
                owner[i] = headers.Count == 0 ? -1 : headers[headers.Count - 1];
            }

            var replacements = new byte[lines.Count][];
            var insertions = new List<byte[]>[lines.Count];
            var newSections = new List<NewSection>();

            foreach (EncodedEdit edit in encoded)
            {
                var namesSection = new bool[lines.Count];
                int firstHeader = -1;
                int lastHeader = -1;
                foreach (int header in headers)
                {
                    Line h = lines[header];
                    if (!h.Named || !EqualsAsciiIgnoreCase(s, h.NameBegin, h.NameEnd, edit.Section)) continue;
                    namesSection[header] = true;
                    if (firstHeader < 0) firstHeader = header;
                    lastHeader = header;
                }
                if (firstHeader < 0)
                {
                    if (!edit.Edit.InsertIfAbsent)
                    {
                        return Refuse(IniEditRefusal.KeyNotFound, edit.Edit, new int[0]);
                    }
                    int target = -1;
                    for (int i = 0; i < newSections.Count; i++)
                    {
                        if (EqualsAsciiIgnoreCase(newSections[i].Name, 0, newSections[i].Name.Length, edit.Section)) target = i;
                    }
                    if (target < 0)
                    {
                        newSections.Add(new NewSection(edit.Section));
                        target = newSections.Count - 1;
                    }
                    newSections[target].Lines.Add(KeyLine(edit));
                    continue;
                }

                int block = edit.Edit.FirstOccurrenceWins ? firstHeader : lastHeader;
                int anchor = block;
                int firstKey = -1;
                int lastKey = -1;
                for (int i = firstHeader + 1; i < lines.Count; i++)
                {
                    Line line = lines[i];
                    if (line.Kind != LineKind.Key || !namesSection[owner[i]]) continue;
                    if (owner[i] == block) anchor = i;
                    if (EqualsAsciiIgnoreCase(s, line.NameBegin, line.NameEnd, edit.Key))
                    {
                        if (firstKey < 0) firstKey = i;
                        lastKey = i;
                    }
                }
                if (firstKey >= 0)
                {
                    replacements[edit.Edit.FirstOccurrenceWins ? firstKey : lastKey] = edit.Value;
                    continue;
                }
                if (!edit.Edit.InsertIfAbsent)
                {
                    return Refuse(IniEditRefusal.KeyNotFound, edit.Edit, new int[0]);
                }
                if (insertions[anchor] == null) insertions[anchor] = new List<byte[]>();
                insertions[anchor].Add(KeyLine(edit));
            }

            var output = new MemoryStream(s.Length + 64 * encoded.Count);
            output.Write(s, 0, body);
            for (int i = 0; i < lines.Count; i++)
            {
                Line line = lines[i];
                if (replacements[i] != null)
                {
                    output.Write(s, line.Begin, line.ValueBegin - line.Begin);
                    output.Write(replacements[i], 0, replacements[i].Length);
                    output.Write(s, line.ContentEnd, line.End - line.ContentEnd);
                }
                else
                {
                    output.Write(s, line.Begin, line.End - line.Begin);
                }
                if (insertions[i] == null) continue;
                bool terminated = line.End != line.ContentEnd;
                bool lfNext = line.End < s.Length && s[line.End] == '\n';
                for (int k = 0; k < insertions[i].Count; k++)
                {
                    WriteLine(output, insertions[i][k], eol, terminated, k + 1 == insertions[i].Count && lfNext);
                }
            }

            if (newSections.Count > 0)
            {
                var appended = new List<byte[]>();
                bool empty = output.Length == body;
                if (!empty && !LastLineIsBlank(output.GetBuffer(), body, (int)output.Length)) appended.Add(new byte[0]);
                for (int i = 0; i < newSections.Count; i++)
                {
                    if (i > 0) appended.Add(new byte[0]);
                    appended.Add(Concat(new[] { (byte)'[' }, newSections[i].Name, new[] { (byte)']' }));
                    appended.AddRange(newSections[i].Lines);
                }
                bool terminated = empty || IsLineEnd(output.GetBuffer()[output.Length - 1]);
                for (int k = 0; k < appended.Count; k++)
                {
                    bool lfFollows = k + 1 < appended.Count && appended[k + 1].Length == 0 && eol.Length == 1 && eol[0] == '\n';
                    WriteLine(output, appended[k], eol, terminated, lfFollows);
                }
            }

            return new IniEditResult(IniEditRefusal.None, output.ToArray(), null, null, new int[0]);
        }

        private static List<EncodedEdit> ValidateEdits(IList<IniEdit> edits)
        {
            var encoded = new List<EncodedEdit>(edits.Count);
            for (int i = 0; i < edits.Count; i++)
            {
                IniEdit edit = edits[i];
                if (edit == null) throw new ArgumentException("edits[" + i + "] is null", nameof(edits));

                byte[] section = RequireName(edit.Section, "section");
                byte[] key = RequireName(edit.Key, "key");
                byte[] value = RequirePrintableAscii(edit.Value, "value");
                RequireNoSurroundingSpace(value, edit.Value, "value");
                if (Array.IndexOf(section, (byte)']') >= 0)
                {
                    throw new ArgumentException("IniEdit section '" + edit.Section + "' holds ']', which ends a section header");
                }
                byte first = key[0];
                if (first == '[' || first == ';' || first == '#' || Array.IndexOf(key, (byte)'=') >= 0)
                {
                    throw new ArgumentException("IniEdit key '" + edit.Key +
                        "' would not read back as a key: it holds '=' or starts '[', ';' or '#'");
                }
                foreach (EncodedEdit earlier in encoded)
                {
                    if (EqualsAsciiIgnoreCase(earlier.Section, 0, earlier.Section.Length, section) &&
                        EqualsAsciiIgnoreCase(earlier.Key, 0, earlier.Key.Length, key))
                    {
                        throw new ArgumentException("IniEdit batch edits [" + edit.Section + "] " + edit.Key + " twice");
                    }
                }
                encoded.Add(new EncodedEdit(edit, section, key, value));
            }
            return encoded;
        }

        private static byte[] RequirePrintableAscii(string text, string what)
        {
            var bytes = new byte[text.Length];
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (c < 0x20 || c > 0x7E)
                {
                    throw new ArgumentException("IniEdit " + what + " holds U+" +
                        ((int)c).ToString("X4", CultureInfo.InvariantCulture) +
                        ", and only printable ASCII (U+0020 to U+007E) can be written");
                }
                bytes[i] = (byte)c;
            }
            return bytes;
        }

        private static byte[] RequireName(string text, string what)
        {
            byte[] bytes = RequirePrintableAscii(text, what);
            if (bytes.Length == 0) throw new ArgumentException("IniEdit " + what + " is empty");
            RequireNoSurroundingSpace(bytes, text, what);
            return bytes;
        }

        private static void RequireNoSurroundingSpace(byte[] bytes, string text, string what)
        {
            if (bytes.Length > 0 && (bytes[0] == ' ' || bytes[bytes.Length - 1] == ' '))
            {
                throw new ArgumentException("IniEdit " + what + " '" + text + "' starts or ends with a space, which the reader trims away");
            }
        }

        private static void ClassifyLine(byte[] s, Line line)
        {
            int first = line.Begin;
            while (first < line.ContentEnd && IsSpaceOrTab(s[first])) first++;
            int last = line.ContentEnd;
            while (last > first && IsSpaceOrTab(s[last - 1])) last--;

            if (first == last)
            {
                line.Kind = LineKind.Blank;
                return;
            }
            byte lead = s[first];
            if (lead == ';' || lead == '#')
            {
                line.Kind = LineKind.Comment;
                return;
            }
            if (lead == '[')
            {
                line.Kind = LineKind.Section;
                int close = Array.IndexOf(s, (byte)']', first + 1, last - (first + 1));
                if (close < 0) return;
                int nameBegin = first + 1;
                int nameEnd = close;
                while (nameBegin < nameEnd && IsSpaceOrTab(s[nameBegin])) nameBegin++;
                while (nameEnd > nameBegin && IsSpaceOrTab(s[nameEnd - 1])) nameEnd--;
                line.Named = true;
                line.NameBegin = nameBegin;
                line.NameEnd = nameEnd;
                return;
            }

            int eq = Array.IndexOf(s, (byte)'=', first, last - first);
            if (eq < 0) return;
            int keyEnd = eq;
            while (keyEnd > first && IsSpaceOrTab(s[keyEnd - 1])) keyEnd--;
            if (keyEnd == first) return;

            int valueBegin = eq + 1;
            while (valueBegin < line.ContentEnd && IsSpaceOrTab(s[valueBegin])) valueBegin++;

            line.Kind = LineKind.Key;
            line.NameBegin = first;
            line.NameEnd = keyEnd;
            line.ValueBegin = valueBegin;
        }

        private static bool IsSpaceOrTab(byte b)
        {
            return b == ' ' || b == '\t';
        }

        private static bool IsLineEnd(byte b)
        {
            return b == '\r' || b == '\n';
        }

        private static byte FoldAscii(byte b)
        {
            return b >= 'A' && b <= 'Z' ? (byte)(b - 'A' + 'a') : b;
        }

        private static bool EqualsAsciiIgnoreCase(byte[] text, int begin, int end, byte[] other)
        {
            if (end - begin != other.Length) return false;
            for (int i = 0; i < other.Length; i++)
            {
                if (FoldAscii(text[begin + i]) != FoldAscii(other[i])) return false;
            }
            return true;
        }

        // The canonical reader's line count up to offset: CRLF, LF and a lone CR each end a line.
        private static int LineOfOffset(byte[] s, int offset)
        {
            int line = 1;
            for (int i = 0; i < offset; i++)
            {
                if (s[i] == '\n' || (s[i] == '\r' && (i + 1 >= s.Length || s[i + 1] != '\n'))) line++;
            }
            return line;
        }

#if NULLABLE_ENABLED
        private static IniEditResult Refuse(IniEditRefusal refusal, IniEdit? edit, int[] lines)
#else
        private static IniEditResult Refuse(IniEditRefusal refusal, IniEdit edit, int[] lines)
#endif
        {
            return new IniEditResult(refusal, new byte[0], edit == null ? null : edit.Section, edit == null ? null : edit.Key, lines);
        }

        private static byte[] KeyLine(EncodedEdit edit)
        {
            return Concat(edit.Key, new[] { (byte)'=' }, edit.Value);
        }

        private static byte[] Concat(byte[] a, byte[] b, byte[] c)
        {
            var result = new byte[a.Length + b.Length + c.Length];
            Buffer.BlockCopy(a, 0, result, 0, a.Length);
            Buffer.BlockCopy(b, 0, result, a.Length, b.Length);
            Buffer.BlockCopy(c, 0, result, a.Length + b.Length, c.Length);
            return result;
        }

        private static void WriteLine(MemoryStream output, byte[] line, byte[] eol, bool afterTerminatedLine, bool lfFollows)
        {
            if (afterTerminatedLine)
            {
                output.Write(line, 0, line.Length);
                WriteEol(output, eol, lfFollows);
            }
            else
            {
                WriteEol(output, eol, line.Length == 0 && lfFollows);
                output.Write(line, 0, line.Length);
            }
        }

        // An ending written straight after a CR, or straight before an LF, would pair with it
        // into one CRLF and the reader would lose a line; CRLF pairs with neither, so it is
        // written there.
        private static void WriteEol(MemoryStream output, byte[] eol, bool lfFollows)
        {
            bool afterCr = output.Length > 0 && output.GetBuffer()[output.Length - 1] == '\r';
            bool pairs = eol.Length == 1 && ((eol[0] == '\n' && afterCr) || (eol[0] == '\r' && lfFollows));
            if (pairs)
            {
                output.WriteByte((byte)'\r');
                output.WriteByte((byte)'\n');
            }
            else
            {
                output.Write(eol, 0, eol.Length);
            }
        }

        private static bool LastLineIsBlank(byte[] s, int bodyBegin, int length)
        {
            int end = length;
            if (end >= bodyBegin + 2 && s[end - 2] == '\r' && s[end - 1] == '\n')
            {
                end -= 2;
            }
            else if (end > bodyBegin && IsLineEnd(s[end - 1]))
            {
                end--;
            }
            int begin = end;
            while (begin > bodyBegin && !IsLineEnd(s[begin - 1])) begin--;
            for (int i = begin; i < end; i++)
            {
                if (!IsSpaceOrTab(s[i])) return false;
            }
            return true;
        }
    }
}
