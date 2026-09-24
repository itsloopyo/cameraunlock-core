using System;
using System.Collections.Generic;
using System.Text;

// The differential corpus: every mutation of a legacy config file that design 6.2 lists, for a
// game's differential test to run through its legacy import and its migration. Test code only:
// no shipped assembly compiles this file. A test project links it with
// <Compile Include="..\cameraunlock-core\csharp\testing\IniMutations.cs" Link="IniMutations.cs" />.
// C# 7.3, and free of nullable warnings where a project enables them. The C++ twin is
// cpp/include/cameraunlock/config/testing/ini_mutations.h, and data/fixtures/canonical-ini/mutations
// holds both to the same bytes.

namespace CameraUnlock.Core.Config.Testing
{
    /// <summary>A legacy switch or letter row that folds a Ctrl+Shift binding into a hotkey's list.</summary>
    internal sealed class ChordSwitch
    {
        /// <param name="section">The section, or "" for a reader that ignores sections, as in
        /// <see cref="MutationKey"/>.</param>
        /// <param name="on">The value that turns it on, as the legacy reader reads it.</param>
        /// <param name="off">The value that turns it off.</param>
        public ChordSwitch(string section, string key, string on, string off)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (on == null) throw new ArgumentNullException("on");
            if (off == null) throw new ArgumentNullException("off");
            Section = section;
            Key = key;
            On = on;
            Off = off;
        }

        public string Section { get; }
        public string Key { get; }
        public string On { get; }
        public string Off { get; }
    }

    /// <summary>One key the frozen legacy reader reads.</summary>
    internal sealed class MutationKey
    {
        /// <param name="section">The section, or "" for a reader that ignores sections, as an empty
        /// <see cref="LegacyKey"/> section means: the key's line is then its first key line anywhere
        /// in the file.</param>
        /// <param name="alternate">A valid value other than the shipped one.</param>
        /// <param name="outOfRange">One value outside each range the legacy reader refuses or clamps.</param>
        /// <param name="isHotkey">Whether the key holds a hotkey.</param>
        /// <param name="chords">For a hotkey, the chord rows that fold into its list.</param>
        public MutationKey(string section, string key, string alternate, IEnumerable<string> outOfRange, bool isHotkey,
            IEnumerable<ChordSwitch> chords)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (alternate == null) throw new ArgumentNullException("alternate");
            if (outOfRange == null) throw new ArgumentNullException("outOfRange");
            if (chords == null) throw new ArgumentNullException("chords");
            Section = section;
            Key = key;
            Alternate = alternate;
            OutOfRange = new List<string>(outOfRange).ToArray();
            IsHotkey = isHotkey;
            Chords = new List<ChordSwitch>(chords).ToArray();
            foreach (string value in OutOfRange)
            {
                if (value == null) throw new ArgumentNullException("outOfRange", "an out-of-range value is null");
            }
            foreach (ChordSwitch chord in Chords)
            {
                if (chord == null) throw new ArgumentNullException("chords", "a chord is null");
            }
        }

        public string Section { get; }
        public string Key { get; }
        public string Alternate { get; }
        public string[] OutOfRange { get; }
        public bool IsHotkey { get; }
        public ChordSwitch[] Chords { get; }
    }

    /// <summary>One corpus input.</summary>
    internal sealed class IniMutation
    {
        public IniMutation(string name, byte[] bytes)
        {
            Name = name;
            Bytes = bytes;
        }

        public string Name { get; }
        public byte[] Bytes { get; }
    }

    internal static class IniMutations
    {
        private static readonly byte[] Mark = { 0xEF, 0xBB, 0xBF };
        private static readonly byte[] Crlf = { 0x0D, 0x0A };
        private static readonly byte[] Lf = { 0x0A };
        private static readonly byte[] Cr = { 0x0D };
        private static readonly byte[] NoEnding = new byte[0];
        private const string Invalid = "abc";

        private static readonly string[][] Values =
        {
            new[] { "empty", "" }, new[] { "space", " " }, new[] { "\"\"", "\"\"" }, new[] { "abc", "abc" },
            new[] { "nan", "nan" }, new[] { "inf", "inf" }, new[] { "-inf", "-inf" }, new[] { "1e400", "1e400" },
            new[] { "0,15", "0,15" }, new[] { "0x10", "0x10" }, new[] { "010", "010" }, new[] { "-1", "-1" },
            new[] { "+1", "+1" }, new[] { "space then 1", " 1" }, new[] { "1 then space", "1 " }, new[] { "1abc", "1abc" },
            new[] { "True", "True" }, new[] { "TRUE", "TRUE" }, new[] { "tRue", "tRue" }, new[] { "yes", "yes" },
            new[] { "on", "on" }, new[] { "2", "2" }, new[] { "1100 characters", new string('1', 1100) },
        };

        private static readonly string[] HotkeyValues = { "0x230", "0", "End" };

        // Code page 1252 at 0x80-0x9F, as Windows decodes it: the five codes it leaves undefined
        // stay the same code point.
        private static readonly int[] Cp1252High =
        {
            0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
            0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
            0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
        };

        /// <summary>
        /// Every corpus input design 6.2 lists, built from <paramref name="baseBytes"/> (a legacy
        /// file's bytes) and the keys the frozen reader reads, as (name, bytes) in a fixed order with
        /// names that never repeat. data/fixtures/canonical-ini/README.md defines each mutation byte
        /// for byte.
        /// </summary>
        /// <param name="baseBytes">The legacy file.</param>
        /// <param name="reads">The import's <see cref="LegacyImport{TConfig}.Keys"/>.</param>
        /// <param name="keys">A descriptor for each key in <paramref name="reads"/>, in the order the
        /// outputs follow.</param>
        /// <remarks>
        /// Lines end at CRLF, LF or a lone CR, and a UTF-8 mark at the start is set aside and put
        /// back. A key missing from the base is added first, set to its alternate value, at the end
        /// of its section or in a new section at the end (a section-less key after the last line
        /// above the first header), and every mutation starts from that file.
        /// </remarks>
        /// <exception cref="ArgumentNullException">An argument, a read or a key is null.</exception>
        /// <exception cref="ArgumentException">No keys, <paramref name="reads"/> and
        /// <paramref name="keys"/> naming different keys, a key read or listed twice, a section-less
        /// key beside another key of its name, a name or value outside printable ASCII, a section or
        /// key with a space at either end, a section holding ']', a key holding '=' or starting with
        /// ';', '#' or '[', or a first key whose line cannot be padded to 199 characters.</exception>
        public static List<IniMutation> Generate(byte[] baseBytes, IList<LegacyKey> reads, IList<MutationKey> keys)
        {
            if (baseBytes == null) throw new ArgumentNullException("baseBytes");
            if (reads == null) throw new ArgumentNullException("reads");
            if (keys == null) throw new ArgumentNullException("keys");
            CheckKeys(reads, keys);

            Doc full = new Doc(baseBytes);
            foreach (MutationKey k in keys)
            {
                if (full.Find(k.Section, k.Key) < 0) full.AppendToSection(Ascii(k.Section), Ascii(k.Key + "=" + k.Alternate));
            }

            var output = new List<IniMutation>();
            Action<string, byte[]> emit = (name, bytes) => output.Add(new IniMutation(name, bytes));

            foreach (MutationKey k in keys)
            {
                string n = Label(k.Section, k.Key) + ": ";
                int i = full.Require(k.Section, k.Key);
                byte[] value = full.Value(i);
                byte[] prefix = Doc.Prefix(full.Lines[i].Content);
                byte[] alternate = Ascii(k.Alternate);

                Doc d = full.Clone();
                d.Lines.RemoveAt(i);
                emit(n + "removed", d.Bytes(true));
                d = full.Clone();
                d.Insert(i, Concat(prefix, alternate));
                emit(n + "duplicate before, another value", d.Bytes(true));
                d = full.Clone();
                d.Insert(i + 1, Concat(prefix, alternate));
                emit(n + "duplicate after, another value", d.Bytes(true));
                d = full.Clone();
                d.Insert(i + 1, Concat(prefix, Ascii(Invalid)));
                emit(n + "valid, then an invalid duplicate", d.Bytes(true));
                d = full.Clone();
                d.Insert(i, Concat(prefix, Ascii(Invalid)));
                emit(n + "invalid, then a valid duplicate", d.Bytes(true));

                d = full.Clone();
                {
                    byte[] content = d.Lines[i].Content;
                    int eq = IndexOf(content, (byte)'=', 0);
                    d.Lines[i].Content = Concat(SwapCase(Slice(content, 0, eq)), Slice(content, eq, content.Length - eq));
                }
                emit(n + "key case swapped", d.Bytes(true));

                if (k.Section.Length != 0)
                {
                    d = full.Clone();
                    int header = i;
                    while (!IsHeader(d.Lines[header].Content)) header--;
                    byte[] content = d.Lines[header].Content;
                    int open = IndexOf(content, (byte)'[', 0);
                    int close = IndexOf(content, (byte)']', open);
                    d.Lines[header].Content = Concat(Slice(content, 0, open + 1),
                        SwapCase(Slice(content, open + 1, close - open - 1)), Slice(content, close, content.Length - close));
                    emit(n + "section case swapped", d.Bytes(true));
                }

                d = full.Clone();
                if (k.Section.Length == 0)
                {
                    byte[] moved = d.Lines[i].Content;
                    d.Lines.RemoveAt(i);
                    d.Insert(d.Lines.Count, Ascii("[Elsewhere]"));
                    d.Insert(d.Lines.Count, moved);
                }
                else
                {
                    byte[] moved = d.Lines[i].Content;
                    d.Lines.RemoveAt(i);
                    string target = "";
                    foreach (Line line in d.Lines)
                    {
                        if (!IsHeader(line.Content)) continue;
                        string name = HeaderName(line.Content);
                        if (name.Length != 0 && !SameName(name, k.Section))
                        {
                            target = name;
                            break;
                        }
                    }
                    if (target.Length != 0)
                    {
                        d.Insert(d.InsertPoint(target), moved);
                    }
                    else
                    {
                        string other = SameName(k.Section, "Elsewhere") ? "Other" : "Elsewhere";
                        d.Insert(d.Lines.Count, Ascii("[" + other + "]"));
                        d.Insert(d.Lines.Count, moved);
                    }
                }
                emit(n + "moved to another section", d.Bytes(true));

                int firstHeader = full.FirstAnyHeader();
                if (firstHeader >= 0 && firstHeader < i)
                {
                    d = full.Clone();
                    byte[] moved = d.Lines[i].Content;
                    d.Lines.RemoveAt(i);
                    d.Insert(firstHeader, moved);
                    emit(n + "before the first header", d.Bytes(true));
                }

                foreach (string tail in new[] { "; x", ";x", " # x" })
                {
                    d = full.Clone();
                    d.Lines[i].Content = Concat(d.Lines[i].Content, Ascii(tail));
                    emit(n + "'" + tail + "' appended", d.Bytes(true));
                }
                d = full.Clone();
                d.Set(i, Concat(Ascii("\""), value, Ascii("\"")));
                emit(n + "in double quotes", d.Bytes(true));
                d = full.Clone();
                d.Set(i, Concat(Ascii("'"), value, Ascii("'")));
                emit(n + "in single quotes", d.Bytes(true));

                foreach (string[] entry in Values)
                {
                    d = full.Clone();
                    d.Set(i, Ascii(entry[1]));
                    emit(n + "value " + entry[0], d.Bytes(true));
                }
                foreach (string text in k.OutOfRange)
                {
                    d = full.Clone();
                    d.Set(i, Ascii(text));
                    emit(n + "out of range " + text, d.Bytes(true));
                }
                if (k.IsHotkey)
                {
                    foreach (string text in HotkeyValues)
                    {
                        d = full.Clone();
                        d.Set(i, Ascii(text));
                        emit(n + "hotkey " + text, d.Bytes(true));
                    }
                    foreach (ChordSwitch chord in k.Chords)
                    {
                        d = full.Clone();
                        d.SetKey(chord.Section, chord.Key, chord.On);
                        emit(n + "chord " + Label(chord.Section, chord.Key) + " on", d.Bytes(true));
                        d = full.Clone();
                        d.SetKey(chord.Section, chord.Key, chord.Off);
                        emit(n + "chord " + Label(chord.Section, chord.Key) + " off", d.Bytes(true));
                    }
                }
            }

            foreach (MutationKey a in keys)
            {
                foreach (MutationKey b in keys)
                {
                    if (ReferenceEquals(a, b)) continue;
                    string n = Label(a.Section, a.Key) + " alternate, " + Label(b.Section, b.Key);
                    Doc d = full.Clone();
                    d.Set(d.Require(a.Section, a.Key), Ascii(a.Alternate));
                    d.Lines.RemoveAt(d.Require(b.Section, b.Key));
                    emit(n + " removed", d.Bytes(true));
                    d = full.Clone();
                    d.Set(d.Require(a.Section, a.Key), Ascii(a.Alternate));
                    d.Set(d.Require(b.Section, b.Key), Ascii(Invalid));
                    emit(n + " invalid", d.Bytes(true));
                }
            }

            var sections = new List<string>();
            foreach (Line line in full.Lines)
            {
                if (!IsHeader(line.Content)) continue;
                string name = HeaderName(line.Content);
                if (name.Length == 0) continue;
                bool seen = false;
                foreach (string s in sections) seen |= SameName(s, name);
                if (!seen) sections.Add(name);
            }
            foreach (string s in sections)
            {
                string n = "[" + s + "]: ";
                int header = full.FirstHeader(s);
                string[][] headers =
                {
                    new[] { "header with spaces", "[ " + s + " ]" },
                    new[] { "header with a comment", "[" + s + "] ; c" },
                    new[] { "header not closed", "[" + s },
                };
                foreach (string[] entry in headers)
                {
                    Doc h = full.Clone();
                    h.Lines[header].Content = Ascii(entry[1]);
                    emit(n + entry[0], h.Bytes(true));
                }
                Doc d = full.Clone();
                int end = d.BlockEnd(header);
                var copy = new List<Line>();
                for (int j = header; j < end; j++) copy.Add(d.Lines[j].Clone());
                if (d.Lines[end - 1].Ending.Length == 0) d.Lines[end - 1].Ending = d.Dominant;
                foreach (Line line in copy)
                {
                    string found = KeyOf(line.Content);
                    if (found.Length == 0) continue;
                    foreach (MutationKey k in keys)
                    {
                        if ((k.Section.Length == 0 || SameName(k.Section, s)) && SameName(k.Key, found))
                        {
                            line.Content = Concat(Doc.Prefix(line.Content), Ascii(k.Alternate));
                            break;
                        }
                    }
                }
                d.Lines.InsertRange(end, copy);
                emit(n + "section repeated", d.Bytes(true));
            }

            const string f = "file: ";
            MutationKey first = keys[0];
            int fileHeader = full.FirstAnyHeader();
            if (fileHeader >= 0)
            {
                Doc d = full.Clone();
                d.Lines.RemoveRange(0, fileHeader);
                emit(f + "UTF-8 mark before a header", Concat(Mark, d.Bytes(false)));
            }
            emit(f + "UTF-8 mark before a comment", Concat(Mark, Ascii("; comment"), full.Dominant, full.Bytes(false)));
            string[] endingNames = { "CRLF", "LF", "lone CR" };
            byte[][] endings = { Crlf, Lf, Cr };
            for (int e = 0; e < endings.Length; e++)
            {
                Doc d = full.Clone();
                foreach (Line line in d.Lines)
                {
                    if (line.Ending.Length != 0) line.Ending = endings[e];
                }
                emit(f + endingNames[e], d.Bytes(true));
            }
            {
                Doc d = full.Clone();
                int t = 0;
                foreach (Line line in d.Lines)
                {
                    if (line.Ending.Length != 0) line.Ending = endings[t++ % 3];
                }
                emit(f + "mixed line endings", d.Bytes(true));
            }
            {
                Doc d = full.Clone();
                while (d.Lines.Count > 0 && d.Lines[d.Lines.Count - 1].Content.Length == 0) d.Lines.RemoveAt(d.Lines.Count - 1);
                if (d.Lines.Count > 0) d.Lines[d.Lines.Count - 1].Ending = NoEnding;
                emit(f + "no final newline", d.Bytes(true));
            }
            foreach (int width in new[] { 199, 200, 254, 255 })
            {
                Doc d = full.Clone();
                foreach (Line line in d.Lines)
                {
                    if (line.Ending.Length != 0) line.Ending = Crlf;
                }
                int i = d.Require(first.Section, first.Key);
                byte[] key = Ascii(KeyOf(d.Lines[i].Content));
                byte[] value = d.Value(i);
                if (key.Length + 1 + value.Length > width)
                {
                    throw new ArgumentException(Label(first.Section, first.Key) + "=" + Encoding.ASCII.GetString(value)
                        + " is longer than " + width + " characters", "keys");
                }
                byte[] pad = Ascii(new string(' ', width - key.Length - 1 - value.Length));
                d.Lines[i].Content = Concat(key, Ascii("="), pad, value);
                emit(f + "CRLF, a " + width + "-character line", d.Bytes(true));
            }
            string[] byteNames = { "0x1A byte", "NUL byte" };
            byte[][] controlBytes = { new byte[] { 0x1A }, new byte[] { 0x00 } };
            for (int b = 0; b < controlBytes.Length; b++)
            {
                Doc d = full.Clone();
                d.Insert(d.Require(first.Section, first.Key) + 1, controlBytes[b]);
                emit(f + byteNames[b], d.Bytes(true));
            }
            emit(f + "trailing NUL padding", Concat(full.Bytes(true), new byte[64]));
            {
                Doc d = full.Clone();
                foreach (Line line in d.Lines)
                {
                    if (KeyOf(line.Content).Length == 0) continue;
                    byte[] content = line.Content;
                    int eq = IndexOf(content, (byte)'=', 0);
                    int before = eq;
                    while (before > 0 && IsBlank(content[before - 1])) before--;
                    int after = eq + 1;
                    while (after < content.Length && IsBlank(content[after])) after++;
                    line.Content = Concat(Slice(content, 0, before), Ascii("\t=\t"), Slice(content, after, content.Length - after));
                }
                emit(f + "tab separators", d.Bytes(true));
            }
            foreach (string comment in new[] { "#", ";" })
            {
                Doc d = full.Clone();
                foreach (MutationKey k in keys) d.Insert(d.Require(k.Section, k.Key) + 1, Ascii(comment + k.Key + "=" + k.Alternate));
                emit(f + comment + "Key= lines", d.Bytes(true));
            }
            {
                Doc d = full.Clone();
                foreach (MutationKey k in keys) d.Insert(d.Require(k.Section, k.Key) + 1, Ascii("    " + k.Alternate));
                emit(f + "indented continuation lines", d.Bytes(true));
            }
            emit(f + "UTF-16 LE with a mark", Utf16(full.Bytes(false)));
            {
                Doc d = full.Clone();
                d.Insert(d.Require(first.Section, first.Key), new byte[] { (byte)';', (byte)' ', (byte)'c', (byte)'a', (byte)'f', 0xE9 });
                emit(f + "cp1252 byte in a comment", d.Bytes(true));
            }
            {
                Doc d = full.Clone();
                foreach (MutationKey k in keys)
                {
                    Line line = d.Lines[d.Require(k.Section, k.Key)];
                    line.Content = Concat(line.Content, new byte[] { 0xE9 });
                }
                emit(f + "cp1252 byte in a value", d.Bytes(true));
            }
            return output;
        }

        private sealed class Line
        {
            public Line(byte[] content, byte[] ending)
            {
                Content = content;
                Ending = ending;
            }

            public byte[] Content { get; set; }
            public byte[] Ending { get; set; }

            public Line Clone()
            {
                return new Line(Content, Ending);
            }
        }

        private sealed class Doc
        {
            private readonly bool _mark;

            public Doc(byte[] data)
            {
                int offset = 0;
                if (data.Length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
                {
                    _mark = true;
                    offset = 3;
                }
                Lines = new List<Line>();
                int start = offset;
                int i = offset;
                while (i < data.Length)
                {
                    if (data[i] == 0x0D || data[i] == 0x0A)
                    {
                        int width = data[i] == 0x0D && i + 1 < data.Length && data[i + 1] == 0x0A ? 2 : 1;
                        Lines.Add(new Line(Slice(data, start, i - start), Slice(data, i, width)));
                        i += width;
                        start = i;
                    }
                    else
                    {
                        i++;
                    }
                }
                if (start < data.Length) Lines.Add(new Line(Slice(data, start, data.Length - start), NoEnding));
                int crlf = 0, lf = 0, cr = 0;
                foreach (Line line in Lines)
                {
                    if (line.Ending.Length == 2) crlf++;
                    else if (line.Ending.Length == 1 && line.Ending[0] == 0x0A) lf++;
                    else if (line.Ending.Length == 1) cr++;
                }
                Dominant = Crlf;
                int best = crlf;
                if (lf > best)
                {
                    Dominant = Lf;
                    best = lf;
                }
                if (cr > best) Dominant = Cr;
            }

            private Doc(bool mark, List<Line> lines, byte[] dominant)
            {
                _mark = mark;
                Lines = lines;
                Dominant = dominant;
            }

            public List<Line> Lines { get; }

            public byte[] Dominant { get; }

            public Doc Clone()
            {
                var lines = new List<Line>(Lines.Count);
                foreach (Line line in Lines) lines.Add(line.Clone());
                return new Doc(_mark, lines, Dominant);
            }

            public byte[] Bytes(bool withMark)
            {
                var output = new List<byte>();
                if (_mark && withMark) output.AddRange(Mark);
                foreach (Line line in Lines)
                {
                    output.AddRange(line.Content);
                    output.AddRange(line.Ending);
                }
                return output.ToArray();
            }

            public int Find(string section, string key)
            {
                string current = "";
                for (int i = 0; i < Lines.Count; i++)
                {
                    if (IsHeader(Lines[i].Content))
                    {
                        current = HeaderName(Lines[i].Content);
                        continue;
                    }
                    string found = KeyOf(Lines[i].Content);
                    bool inSection = section.Length == 0 || (current.Length != 0 && SameName(current, section));
                    if (found.Length != 0 && inSection && SameName(found, key)) return i;
                }
                return -1;
            }

            public int Require(string section, string key)
            {
                int i = Find(section, key);
                if (i < 0) throw new InvalidOperationException(Label(section, key) + " has no line");
                return i;
            }

            public int FirstHeader(string section)
            {
                for (int i = 0; i < Lines.Count; i++)
                {
                    if (IsHeader(Lines[i].Content) && SameName(HeaderName(Lines[i].Content), section)) return i;
                }
                return -1;
            }

            public int FirstAnyHeader()
            {
                for (int i = 0; i < Lines.Count; i++)
                {
                    if (IsHeader(Lines[i].Content)) return i;
                }
                return -1;
            }

            public int BlockEnd(int header)
            {
                int j = header + 1;
                while (j < Lines.Count && !IsHeader(Lines[j].Content)) j++;
                return j;
            }

            public int InsertPoint(string section)
            {
                int header = FirstHeader(section);
                int last = header;
                for (int j = header; j < BlockEnd(header); j++)
                {
                    if (Trim(Lines[j].Content).Length != 0) last = j;
                }
                return last + 1;
            }

            // Just after the last non-blank line above the first header, or in the file when it
            // has no header; the start of the file when there is no such line.
            public int TopInsertPoint()
            {
                int header = FirstAnyHeader();
                int end = header < 0 ? Lines.Count : header;
                int point = 0;
                for (int j = 0; j < end; j++)
                {
                    if (Trim(Lines[j].Content).Length != 0) point = j + 1;
                }
                return point;
            }

            public void Insert(int index, byte[] content)
            {
                if (index == Lines.Count && Lines.Count > 0 && Lines[Lines.Count - 1].Ending.Length == 0)
                {
                    Lines[Lines.Count - 1].Ending = Dominant;
                    Lines.Add(new Line(content, NoEnding));
                }
                else
                {
                    Lines.Insert(index, new Line(content, Dominant));
                }
            }

            public void AppendToSection(byte[] section, byte[] content)
            {
                string name = Encoding.ASCII.GetString(section);
                if (name.Length == 0)
                {
                    Insert(TopInsertPoint(), content);
                }
                else if (FirstHeader(name) < 0)
                {
                    Insert(Lines.Count, Concat(Ascii("["), section, Ascii("]")));
                    Insert(Lines.Count, content);
                }
                else
                {
                    Insert(InsertPoint(name), content);
                }
            }

            // The line up to and including its '=' and the spaces and tabs after it.
            public static byte[] Prefix(byte[] content)
            {
                int j = IndexOf(content, (byte)'=', 0) + 1;
                while (j < content.Length && IsBlank(content[j])) j++;
                return Slice(content, 0, j);
            }

            public byte[] Value(int i)
            {
                byte[] content = Lines[i].Content;
                int eq = IndexOf(content, (byte)'=', 0);
                return Trim(Slice(content, eq + 1, content.Length - eq - 1));
            }

            public void Set(int i, byte[] value)
            {
                Lines[i].Content = Concat(Prefix(Lines[i].Content), value);
            }

            public void SetKey(string section, string key, string value)
            {
                int i = Find(section, key);
                if (i < 0) AppendToSection(Ascii(section), Ascii(key + "=" + value));
                else Set(i, Ascii(value));
            }
        }

        private static void CheckText(string text, string what)
        {
            foreach (char c in text)
            {
                if (c < 0x20 || c > 0x7E) throw new ArgumentException(what + " holds a character outside printable ASCII", "keys");
            }
        }

        private static void CheckName(string section, string key, string what)
        {
            CheckText(section, what + " section");
            CheckText(key, what + " key");
            if (key.Length == 0) throw new ArgumentException(what + " needs a key", "keys");
            if (section.Trim(' ') != section || key.Trim(' ') != key)
            {
                throw new ArgumentException(what + " " + Label(section, key) + " starts or ends with a space", "keys");
            }
            if (section.IndexOf(']') >= 0) throw new ArgumentException(what + " section " + section + " holds ']'", "keys");
            if (key.IndexOf('=') >= 0 || key[0] == ';' || key[0] == '#' || key[0] == '[')
            {
                throw new ArgumentException(what + " key " + key + " holds '=' or starts with ';', '#' or '['", "keys");
            }
        }

        // A section-less key's line is the first with its name in any section, so it clashes with
        // every key of that name.
        private static bool Clash(MutationKey a, MutationKey b)
        {
            return SameName(a.Key, b.Key) && (a.Section.Length == 0 || b.Section.Length == 0 || SameName(a.Section, b.Section));
        }

        private static bool Names(LegacyKey read, MutationKey k)
        {
            return SameName(read.Section, k.Section) && SameName(read.Key, k.Key);
        }

        private static void CheckKeys(IList<LegacyKey> reads, IList<MutationKey> keys)
        {
            if (keys.Count == 0) throw new ArgumentException("the corpus needs at least one key", "keys");
            foreach (LegacyKey read in reads)
            {
                if (read == null) throw new ArgumentNullException("reads", "a read is null");
            }
            for (int n = 0; n < keys.Count; n++)
            {
                MutationKey k = keys[n];
                if (k == null) throw new ArgumentNullException("keys", "a key is null");
                string label = Label(k.Section, k.Key);
                CheckName(k.Section, k.Key, "a key's");
                CheckText(k.Alternate, label + "'s alternate value");
                foreach (string value in k.OutOfRange) CheckText(value, label + "'s out-of-range value");
                foreach (ChordSwitch chord in k.Chords)
                {
                    CheckName(chord.Section, chord.Key, "a chord's");
                    CheckText(chord.On, Label(chord.Section, chord.Key) + "'s on value");
                    CheckText(chord.Off, Label(chord.Section, chord.Key) + "'s off value");
                }
                for (int m = 0; m < n; m++)
                {
                    if (Clash(keys[m], k))
                    {
                        throw new ArgumentException(label + " is listed twice, or once in a section and once without one", "keys");
                    }
                }
                bool read = false;
                foreach (LegacyKey r in reads) read |= Names(r, k);
                if (!read) throw new ArgumentException(label + " is not among the keys the import reads", "keys");
            }
            for (int n = 0; n < reads.Count; n++)
            {
                LegacyKey r = reads[n];
                string label = Label(r.Section, r.Key);
                for (int m = 0; m < n; m++)
                {
                    if (SameName(reads[m].Section, r.Section) && SameName(reads[m].Key, r.Key))
                    {
                        throw new ArgumentException(label + " is read twice", "reads");
                    }
                }
                bool described = false;
                foreach (MutationKey k in keys) described |= Names(r, k);
                if (!described) throw new ArgumentException(label + " is read by the import and has no key descriptor", "reads");
            }
        }

        private static string Label(string section, string key)
        {
            return section.Length == 0 ? key : "[" + section + "] " + key;
        }

        private static bool IsBlank(byte b)
        {
            return b == (byte)' ' || b == (byte)'\t';
        }

        private static byte[] Trim(byte[] text)
        {
            int start = 0;
            int end = text.Length;
            while (start < end && IsBlank(text[start])) start++;
            while (end > start && IsBlank(text[end - 1])) end--;
            return Slice(text, start, end - start);
        }

        private static bool IsHeader(byte[] content)
        {
            byte[] t = Trim(content);
            return t.Length > 0 && t[0] == (byte)'[';
        }

        // The header's name, or "" for an unclosed header or an empty name, which open no section.
        private static string HeaderName(byte[] content)
        {
            byte[] t = Trim(content);
            int close = IndexOf(t, (byte)']', 0);
            if (close < 0) return "";
            return Latin1(Trim(Slice(t, 1, close - 1)));
        }

        // The key of a key line, or "" for any other line.
        private static string KeyOf(byte[] content)
        {
            byte[] t = Trim(content);
            if (t.Length == 0 || t[0] == (byte)';' || t[0] == (byte)'#' || t[0] == (byte)'[') return "";
            int eq = IndexOf(t, (byte)'=', 0);
            if (eq < 0) return "";
            return Latin1(Trim(Slice(t, 0, eq)));
        }

        // One char per byte, so a name read from the file compares by its bytes.
        private static string Latin1(byte[] bytes)
        {
            var chars = new char[bytes.Length];
            for (int i = 0; i < bytes.Length; i++) chars[i] = (char)bytes[i];
            return new string(chars);
        }

        private static bool SameName(string a, string b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (Lower(a[i]) != Lower(b[i])) return false;
            }
            return true;
        }

        private static char Lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
        }

        private static byte[] SwapCase(byte[] text)
        {
            var output = new byte[text.Length];
            for (int i = 0; i < text.Length; i++)
            {
                byte c = text[i];
                bool letter = (c >= (byte)'A' && c <= (byte)'Z') || (c >= (byte)'a' && c <= (byte)'z');
                output[i] = letter ? (byte)(c ^ 0x20) : c;
            }
            return output;
        }

        private static byte[] Ascii(string text)
        {
            var output = new byte[text.Length];
            for (int i = 0; i < text.Length; i++) output[i] = (byte)text[i];
            return output;
        }

        private static int IndexOf(byte[] data, byte value, int start)
        {
            for (int i = start; i < data.Length; i++)
            {
                if (data[i] == value) return i;
            }
            return -1;
        }

        private static byte[] Slice(byte[] data, int start, int count)
        {
            var output = new byte[count];
            Array.Copy(data, start, output, 0, count);
            return output;
        }

        private static byte[] Concat(params byte[][] parts)
        {
            var output = new List<byte>();
            foreach (byte[] part in parts) output.AddRange(part);
            return output.ToArray();
        }

        // Strict UTF-8 into code points. False for anything that is not.
        private static bool DecodeUtf8(byte[] data, List<int> output)
        {
            int i = 0;
            while (i < data.Length)
            {
                int b0 = data[i];
                int width;
                int cp;
                int min;
                if (b0 < 0x80)
                {
                    output.Add(b0);
                    i++;
                    continue;
                }
                if ((b0 & 0xE0) == 0xC0)
                {
                    width = 2;
                    cp = b0 & 0x1F;
                    min = 0x80;
                }
                else if ((b0 & 0xF0) == 0xE0)
                {
                    width = 3;
                    cp = b0 & 0x0F;
                    min = 0x800;
                }
                else if ((b0 & 0xF8) == 0xF0)
                {
                    width = 4;
                    cp = b0 & 0x07;
                    min = 0x10000;
                }
                else
                {
                    return false;
                }
                if (i + width > data.Length) return false;
                for (int j = 1; j < width; j++)
                {
                    int b = data[i + j];
                    if ((b & 0xC0) != 0x80) return false;
                    cp = (cp << 6) | (b & 0x3F);
                }
                if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
                output.Add(cp);
                i += width;
            }
            return true;
        }

        // Decoded as UTF-8 when they are strict UTF-8, else as code page 1252, then written as
        // UTF-16 LE after the mark FF FE.
        private static byte[] Utf16(byte[] data)
        {
            var cps = new List<int>();
            if (!DecodeUtf8(data, cps))
            {
                cps.Clear();
                foreach (byte b in data) cps.Add(b >= 0x80 && b <= 0x9F ? Cp1252High[b - 0x80] : b);
            }
            var output = new List<byte> { 0xFF, 0xFE };
            foreach (int cp in cps)
            {
                if (cp >= 0x10000)
                {
                    Unit(output, 0xD800 + ((cp - 0x10000) >> 10));
                    Unit(output, 0xDC00 + ((cp - 0x10000) & 0x3FF));
                }
                else
                {
                    Unit(output, cp);
                }
            }
            return output.ToArray();
        }

        private static void Unit(List<byte> output, int unit)
        {
            output.Add((byte)(unit & 0xFF));
            output.Add((byte)(unit >> 8));
        }
    }
}
