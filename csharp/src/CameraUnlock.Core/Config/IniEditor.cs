using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Applies a batch of <see cref="IniEdit"/>s to the bytes of an INI document, touching
    /// nothing else. Pure: no file I/O. The C++ twin is <c>cameraunlock::EditIni</c>, and
    /// both are pinned to the same byte fixtures under data/fixtures/ini-editor.
    /// </summary>
    public static class IniEditor
    {
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        private enum LineKind
        {
            Blank,
            Comment,
            Section,
            Key,
            Other,
        }

        private sealed class Line
        {
            public int Begin;
            public int ContentEnd;
            public int End;
            // The first byte that is not a space or tab.
            public int First;
            public LineKind Kind = LineKind.Other;
            // A section's name, or a key line's key.
            public int NameBegin;
            public int NameEnd;
            // A header with no ']' is still a section boundary, but names no section.
            public bool Named;
            public int ValueBegin;
            public int ValueEnd;
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
        /// The document is refused whole, with no bytes produced, when it is UTF-16, invalid
        /// UTF-8, holds a NUL, a SUB (0x1A) or a lone CR, or when any edit is ambiguous or missing. A UTF-8
        /// byte order mark is kept.
        /// </para>
        /// <para>
        /// Lines are read the way the flat readers read them. Leading and trailing spaces and
        /// tabs are ignored; a line starting ';' or '#' is a comment; one starting '[' is a
        /// section header named by the text up to the first ']'; otherwise the first '=' with
        /// text before it makes a key line. Keys before the first header belong to no section
        /// and are never matched. Headers that repeat a section's name make one section: a key
        /// under any of them is that section's key.
        /// </para>
        /// <para>
        /// The C# flat reader trims every white-space character off a line and its key, the
        /// C++ one only spaces and tabs. A line that starts with any other white space, or a
        /// key that ends in it, reads differently in the two, so the document is refused as
        /// <see cref="IniEditRefusal.AmbiguousWhitespace"/>.
        /// </para>
        /// <para>
        /// A replacement rewrites only the value: the text after '=' and its whitespace, up to
        /// an inline ';' or '#' outside quotes, less the whitespace before that comment.
        /// Everything else on the line, its terminator included, is kept byte for byte.
        /// </para>
        /// <para>
        /// An insertion goes after the section's last line that is neither blank nor a
        /// comment, so a comment block introducing the next section stays with that section.
        /// A missing section is appended at the end of the file after a blank line. New lines
        /// end with the file's dominant line ending, CRLF when the counts tie. A file whose
        /// last line has no terminator still ends without one: the new text is joined on with
        /// a single line ending in front instead of behind.
        /// </para>
        /// </summary>
        /// <exception cref="ArgumentException">
        /// An edit cannot be written so that it reads back as given: a section or key that is
        /// empty, has surrounding white space or holds CR, LF, NUL, SUB or an unpaired
        /// surrogate, a section holding ']', a key holding '=' or starting '[', ';' or '#', a
        /// value holding CR, LF, NUL, SUB or an unpaired surrogate, a value a flat reader would read as something
        /// else (surrounding white space, a ';' or '#' outside quotes, a quote left open, or
        /// one pair of matching quotes around the whole of it), or two edits of the same key.
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
            int body = s.Length >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF ? 3 : 0;

            int invalid = FirstInvalidUtf8(s, body);
            if (invalid >= 0)
            {
                return Refuse(IniEditRefusal.InvalidUtf8, null, new[] { LineNumberAt(s, body, invalid) });
            }
            for (int i = body; i < s.Length; i++)
            {
                if (s[i] == 0)
                {
                    return Refuse(IniEditRefusal.NulByte, null, new[] { LineNumberAt(s, body, i) });
                }
            }
            for (int i = body; i < s.Length; i++)
            {
                if (s[i] == 0x1A)
                {
                    return Refuse(IniEditRefusal.SubByte, null, new[] { LineNumberAt(s, body, i) });
                }
            }
            for (int i = body; i < s.Length; i++)
            {
                if (s[i] == '\r' && (i + 1 == s.Length || s[i + 1] != '\n'))
                {
                    return Refuse(IniEditRefusal.LoneCarriageReturn, null, new[] { LineNumberAt(s, body, i) });
                }
            }

            var lines = new List<Line>();
            var ambiguous = new List<int>();
            int crlfCount = 0;
            int lfCount = 0;
            for (int begin = body; begin < s.Length;)
            {
                var line = new Line { Begin = begin };
                int newline = Array.IndexOf(s, (byte)'\n', begin);
                if (newline < 0)
                {
                    line.ContentEnd = s.Length;
                    line.End = s.Length;
                }
                else
                {
                    line.End = newline + 1;
                    if (newline > begin && s[newline - 1] == '\r')
                    {
                        line.ContentEnd = newline - 1;
                        crlfCount++;
                    }
                    else
                    {
                        line.ContentEnd = newline;
                        lfCount++;
                    }
                }
                ClassifyLine(s, line);
                if (StartsWithReaderWhitespace(s, line.First, line.ContentEnd) ||
                    (line.Kind == LineKind.Key && EndsWithReaderWhitespace(s, line.NameBegin, line.NameEnd)))
                {
                    ambiguous.Add(lines.Count);
                }
                lines.Add(line);
                begin = line.End;
            }
            if (ambiguous.Count > 0)
            {
                return Refuse(IniEditRefusal.AmbiguousWhitespace, null, LineNumbers(ambiguous));
            }
            byte[] eol = crlfCount >= lfCount ? new[] { (byte)'\r', (byte)'\n' } : new[] { (byte)'\n' };

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
                var matched = new List<int>();
                foreach (int header in headers)
                {
                    Line h = lines[header];
                    if (h.Named && EqualsAsciiIgnoreCase(s, h.NameBegin, h.NameEnd, edit.Section))
                    {
                        matched.Add(header);
                    }
                }
                if (matched.Count == 0)
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

                int anchor = matched[0];
                var keys = new List<int>();
                foreach (int sectionHeader in matched)
                {
                    for (int i = sectionHeader + 1; i < lines.Count && owner[i] == sectionHeader; i++)
                    {
                        Line line = lines[i];
                        if (sectionHeader == matched[0] && line.Kind != LineKind.Blank && line.Kind != LineKind.Comment)
                        {
                            anchor = i;
                        }
                        if (line.Kind == LineKind.Key && EqualsAsciiIgnoreCase(s, line.NameBegin, line.NameEnd, edit.Key))
                        {
                            keys.Add(i);
                        }
                    }
                }
                if (keys.Count > 1 && !edit.Edit.FirstOccurrenceWins)
                {
                    return Refuse(IniEditRefusal.DuplicateKey, edit.Edit, LineNumbers(keys));
                }
                if (keys.Count > 0)
                {
                    replacements[keys[0]] = edit.Value;
                    continue;
                }
                if (!edit.Edit.InsertIfAbsent)
                {
                    return Refuse(IniEditRefusal.KeyNotFound, edit.Edit, new int[0]);
                }
                if (matched.Count > 1 && !edit.Edit.FirstOccurrenceWins)
                {
                    return Refuse(IniEditRefusal.DuplicateSection, edit.Edit, LineNumbers(matched));
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
                    output.Write(s, line.ValueEnd, line.End - line.ValueEnd);
                }
                else
                {
                    output.Write(s, line.Begin, line.End - line.Begin);
                }
                if (insertions[i] == null) continue;
                bool terminated = line.End != line.ContentEnd;
                foreach (byte[] inserted in insertions[i])
                {
                    WriteLine(output, inserted, eol, terminated);
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
                bool terminated = empty || output.GetBuffer()[output.Length - 1] == '\n';
                foreach (byte[] line in appended)
                {
                    WriteLine(output, line, eol, terminated);
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
                byte[] value = RequireValue(edit.Value);
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

        private static byte[] RequireWritable(string text, string what)
        {
            byte[] bytes;
            try
            {
                bytes = StrictUtf8.GetBytes(text);
            }
            catch (EncoderFallbackException ex)
            {
                throw new ArgumentException("IniEdit " + what + " holds an unpaired surrogate and has no UTF-8 form", ex);
            }
            foreach (byte b in bytes)
            {
                if (b == '\r' || b == '\n' || b == 0)
                {
                    throw new ArgumentException("IniEdit " + what + " holds a CR, LF or NUL, which cannot be written on one line");
                }
                if (b == 0x1A)
                {
                    throw new ArgumentException("IniEdit " + what + " holds a SUB (0x1A), where a text-mode reader stops reading");
                }
            }
            return bytes;
        }

        private static byte[] RequireName(string text, string what)
        {
            byte[] bytes = RequireWritable(text, what);
            if (bytes.Length == 0) throw new ArgumentException("IniEdit " + what + " is empty");
            RequireNoSurroundingWhitespace(bytes, text, what);
            return bytes;
        }

        private static void RequireNoSurroundingWhitespace(byte[] bytes, string text, string what)
        {
            if (StartsWithReaderWhitespace(bytes, 0, bytes.Length) || EndsWithReaderWhitespace(bytes, 0, bytes.Length))
            {
                throw new ArgumentException("IniEdit " + what + " '" + text + "' has surrounding white space, which a reader trims away");
            }
        }

        // The value lands on a key=value line, perhaps with an inline comment after it. The
        // flat readers trim, cut the comment off, then take off one pair of surrounding
        // quotes, so a value any of that would change cannot be written.
        private static byte[] RequireValue(string text)
        {
            byte[] bytes = RequireWritable(text, "value");
            RequireNoSurroundingWhitespace(bytes, text, "value");
            bool inQuotes = false;
            byte quote = 0;
            foreach (byte c in bytes)
            {
                if (inQuotes)
                {
                    if (c == quote) inQuotes = false;
                    continue;
                }
                if (c == '"' || c == '\'')
                {
                    inQuotes = true;
                    quote = c;
                    continue;
                }
                if (c == ';' || c == '#')
                {
                    throw new ArgumentException("IniEdit value '" + text +
                        "' holds ';' or '#' outside quotes, which a reader takes as the start of a comment");
                }
            }
            if (inQuotes)
            {
                throw new ArgumentException("IniEdit value '" + text +
                    "' leaves a quote open, so a comment after it on the line would read as part of the value");
            }
            byte first = bytes.Length >= 2 ? bytes[0] : (byte)0;
            if ((first == '"' || first == '\'') && bytes[bytes.Length - 1] == first)
            {
                throw new ArgumentException("IniEdit value '" + text + "' is wrapped in quotes, which a reader strips");
            }
            return bytes;
        }

        private static void ClassifyLine(byte[] s, Line line)
        {
            int first = line.Begin;
            while (first < line.ContentEnd && IsSpaceOrTab(s[first])) first++;
            int last = line.ContentEnd;
            while (last > first && IsSpaceOrTab(s[last - 1])) last--;
            line.First = first;

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
                int close = Array.IndexOf(s, (byte)']', first + 1, line.ContentEnd - (first + 1));
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

            int eq = Array.IndexOf(s, (byte)'=', first, line.ContentEnd - first);
            if (eq < 0 || eq == first)
            {
                line.Kind = LineKind.Other;
                return;
            }
            int keyEnd = eq;
            while (keyEnd > first && IsSpaceOrTab(s[keyEnd - 1])) keyEnd--;

            int valueBegin = eq + 1;
            while (valueBegin < line.ContentEnd && IsSpaceOrTab(s[valueBegin])) valueBegin++;

            // The inline comment rule of ConfigParsingUtils.StripInlineComment: the first ';'
            // or '#' outside a quoted run.
            int comment = line.ContentEnd;
            bool inQuotes = false;
            byte quote = 0;
            for (int i = valueBegin; i < line.ContentEnd; i++)
            {
                byte c = s[i];
                if (inQuotes)
                {
                    if (c == quote) inQuotes = false;
                    continue;
                }
                if (c == '"' || c == '\'')
                {
                    inQuotes = true;
                    quote = c;
                    continue;
                }
                if (c == ';' || c == '#')
                {
                    comment = i;
                    break;
                }
            }
            int valueEnd = comment;
            while (valueEnd > valueBegin && IsSpaceOrTab(s[valueEnd - 1])) valueEnd--;

            line.Kind = LineKind.Key;
            line.NameBegin = first;
            line.NameEnd = keyEnd;
            line.ValueBegin = valueBegin;
            line.ValueEnd = valueEnd;
        }

        // Well-formed UTF-8 as Unicode's table 3-7 defines it: no overlongs, no surrogates,
        // nothing past U+10FFFF. Returns the offset of the first byte that starts a bad
        // sequence, or -1.
        private static int FirstInvalidUtf8(byte[] s, int pos)
        {
            int n = s.Length;
            int i = pos;
            while (i < n)
            {
                byte lead = s[i];
                if (lead < 0x80)
                {
                    i++;
                    continue;
                }
                int length;
                byte low = 0x80;
                byte high = 0xBF;
                if (lead >= 0xC2 && lead <= 0xDF)
                {
                    length = 2;
                }
                else if (lead == 0xE0)
                {
                    length = 3;
                    low = 0xA0;
                }
                else if ((lead >= 0xE1 && lead <= 0xEC) || lead == 0xEE || lead == 0xEF)
                {
                    length = 3;
                }
                else if (lead == 0xED)
                {
                    length = 3;
                    high = 0x9F;
                }
                else if (lead == 0xF0)
                {
                    length = 4;
                    low = 0x90;
                }
                else if (lead >= 0xF1 && lead <= 0xF3)
                {
                    length = 4;
                }
                else if (lead == 0xF4)
                {
                    length = 4;
                    high = 0x8F;
                }
                else
                {
                    return i;
                }
                if (n - i < length) return i;
                byte second = s[i + 1];
                if (second < low || second > high) return i;
                for (int k = 2; k < length; k++)
                {
                    byte next = s[i + k];
                    if (next < 0x80 || next > 0xBF) return i;
                }
                i += length;
            }
            return -1;
        }

        private static bool IsSpaceOrTab(byte b)
        {
            return b == ' ' || b == '\t';
        }

        // Every character some runtime's String.Trim() strips. .NET Framework 3.5 trims a
        // fixed list that includes U+200B and U+FEFF and leaves out U+180E, U+202F and
        // U+205F; .NET Framework 4 and later trim char.IsWhiteSpace. This is the union.
        private static bool IsReaderWhitespace(int c)
        {
            return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
                c == 0x180E || (c >= 0x2000 && c <= 0x200B) || c == 0x2028 || c == 0x2029 ||
                c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
        }

        // The code point starting at s[i], which must begin well-formed UTF-8.
        private static int CodePointAt(byte[] s, int i)
        {
            int b = s[i];
            if (b < 0x80) return b;
            if (b < 0xE0) return ((b & 0x1F) << 6) | (s[i + 1] & 0x3F);
            if (b < 0xF0) return ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            return ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
        }

        private static bool StartsWithReaderWhitespace(byte[] s, int begin, int end)
        {
            return begin < end && IsReaderWhitespace(CodePointAt(s, begin));
        }

        private static bool EndsWithReaderWhitespace(byte[] s, int begin, int end)
        {
            if (begin >= end) return false;
            int i = end - 1;
            while (i > begin && (s[i] & 0xC0) == 0x80) i--;
            return IsReaderWhitespace(CodePointAt(s, i));
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

        private static int LineNumberAt(byte[] s, int bodyBegin, int offset)
        {
            int number = 1;
            for (int i = bodyBegin; i < offset; i++)
            {
                if (s[i] == '\n') number++;
            }
            return number;
        }

        private static int[] LineNumbers(List<int> indices)
        {
            var numbers = new int[indices.Count];
            for (int i = 0; i < indices.Count; i++) numbers[i] = indices[i] + 1;
            return numbers;
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

        private static void WriteLine(MemoryStream output, byte[] line, byte[] eol, bool afterTerminatedLine)
        {
            if (afterTerminatedLine)
            {
                output.Write(line, 0, line.Length);
                output.Write(eol, 0, eol.Length);
            }
            else
            {
                output.Write(eol, 0, eol.Length);
                output.Write(line, 0, line.Length);
            }
        }

        private static bool LastLineIsBlank(byte[] s, int bodyBegin, int length)
        {
            int end = length;
            if (end > bodyBegin && s[end - 1] == '\n')
            {
                end--;
                if (end > bodyBegin && s[end - 1] == '\r') end--;
            }
            int begin = end;
            while (begin > bodyBegin && s[begin - 1] != '\n') begin--;
            for (int i = begin; i < end; i++)
            {
                if (!IsSpaceOrTab(s[i])) return false;
            }
            return true;
        }
    }
}
