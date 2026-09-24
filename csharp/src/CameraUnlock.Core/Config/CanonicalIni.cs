using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A canonical INI document at the level of sections and keys, read from its bytes. The
    /// C++ twin is <c>cameraunlock::config::ParseCanonicalIni</c>, and both are pinned to the
    /// same fixtures under data/fixtures/canonical-ini/reader. Byte-level: values stay raw
    /// bytes, which codecs give a meaning, and names compare as ASCII bytes.
    /// </summary>
    public sealed class CanonicalIni
    {
        /// <summary>The canonical INI dialect this build reads and writes, as [CameraUnlock] ConfigFormat.</summary>
        public const int ConfigFormat = 1;

        private static readonly byte[] StampSection = Encoding.ASCII.GetBytes("CameraUnlock");
        internal const string FormatKeyText = "ConfigFormat";
        internal static readonly byte[] FormatKey = Encoding.ASCII.GetBytes(FormatKeyText);
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);
        private static readonly byte[] Empty = new byte[0];

        private CanonicalIni(CanonicalReadStatus status, int unreadableLine, int formatVersion,
            CanonicalSection[] sections, CanonicalDiagnostic[] diagnostics)
        {
            Status = status;
            UnreadableLine = unreadableLine;
            FormatVersion = formatVersion;
            Sections = new ReadOnlyCollection<CanonicalSection>(sections);
            Diagnostics = new ReadOnlyCollection<CanonicalDiagnostic>(diagnostics);
        }

        public CanonicalReadStatus Status { get; }

        public bool IsReadable
        {
            get { return Status == CanonicalReadStatus.Readable; }
        }

        /// <summary>
        /// For <see cref="CanonicalReadStatus.NulByte"/>, the 1-based line holding the first NUL.
        /// 0 otherwise.
        /// </summary>
        public int UnreadableLine { get; }

        /// <summary>
        /// [CameraUnlock] ConfigFormat as read, <see cref="ConfigFormat"/> when the section,
        /// the key or a valid number is missing, and 0 when the document is unreadable.
        /// </summary>
        public int FormatVersion { get; }

        /// <summary>
        /// Repeated headers of one name are one section. In the order each first occurs.
        /// Empty when the document is unreadable.
        /// </summary>
        public ReadOnlyCollection<CanonicalSection> Sections { get; }

        /// <summary>Ordered by first line, then kind. Empty when the document is unreadable.</summary>
        public ReadOnlyCollection<CanonicalDiagnostic> Diagnostics { get; }

        /// <summary>
        /// The section compared ASCII case-insensitively with <paramref name="name"/>'s UTF-8
        /// bytes, or null.
        /// </summary>
        /// <exception cref="ArgumentException"><paramref name="name"/> holds an unpaired surrogate.</exception>
#if NULLABLE_ENABLED
        public CanonicalSection? FindSection(string name)
#else
        public CanonicalSection FindSection(string name)
#endif
        {
            byte[] wanted = NameBytes(name, nameof(name));
            foreach (CanonicalSection section in Sections)
            {
                if (EqualsAsciiIgnoreCase(section.Name, wanted)) return section;
            }
            return null;
        }

        /// <summary>
        /// The key in the section, both compared as <see cref="FindSection"/> compares, or null.
        /// </summary>
        /// <exception cref="ArgumentException">A name holds an unpaired surrogate.</exception>
#if NULLABLE_ENABLED
        public CanonicalValue? Find(string section, string key)
#else
        public CanonicalValue Find(string section, string key)
#endif
        {
            NameBytes(key, nameof(key));
            var found = FindSection(section);
            return found == null ? null : found.Find(key);
        }

        /// <summary>
        /// Reads a canonical INI document from its bytes. Pure: no file I/O, and nothing in
        /// the bytes makes it throw.
        /// <para>
        /// A document starting with a UTF-16 byte order mark or holding a NUL is unreadable
        /// and nothing in it is interpreted. Otherwise: CRLF, LF and a lone CR each end a
        /// line; a UTF-8 byte order mark at offset 0 is skipped; each line is trimmed of
        /// spaces and tabs only. Blank lines and lines starting ';' or '#' are skipped. A line
        /// starting '[' is a section header named by the text up to its first ']', trimmed.
        /// Any other line is a key line split at its first '=', key and value trimmed. There
        /// are no inline comments, quotes or escapes: <c>B=true ; c</c> gives the value
        /// <c>true ; c</c>. Every other byte, 0x1A and bytes above 0x7F included, is an
        /// ordinary byte. Names compare ASCII case-insensitively and nothing else is folded.
        /// </para>
        /// <para>
        /// [CameraUnlock] belongs to core. Its ConfigFormat sets <see cref="FormatVersion"/>;
        /// its other keys are reserved for core.
        /// </para>
        /// </summary>
        public static CanonicalIni Parse(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));

            if (StartsWithUtf16Mark(bytes))
            {
                return new CanonicalIni(CanonicalReadStatus.Utf16, 0, 0, new CanonicalSection[0], new CanonicalDiagnostic[0]);
            }
            int nul = Array.IndexOf(bytes, (byte)0);
            if (nul >= 0)
            {
                return new CanonicalIni(CanonicalReadStatus.NulByte, LineOfOffset(bytes, nul), 0,
                    new CanonicalSection[0], new CanonicalDiagnostic[0]);
            }

            var builder = new Builder(bytes);
            int number = 0;
            foreach (Span line in Lines(bytes))
            {
                builder.Read(line, ++number);
            }
            return builder.Finish();
        }

        /// <summary>
        /// True when some line opens a section named CameraUnlock under
        /// <see cref="Parse"/>'s header rule, so <c>[CameraUnlock] ; note</c> counts and
        /// <c>; [CameraUnlock]</c> does not. When the bytes start with a UTF-16 byte order
        /// mark they are searched in their UTF-16 decoding by that mark, so a canonical file
        /// re-saved as UTF-16 is still recognised. A NUL does not hide the stamp: a stamped
        /// file can be unreadable.
        /// </summary>
        public static bool HasStamp(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));

            byte[] searched = StartsWithUtf16Mark(bytes) ? NarrowUtf16(bytes) : bytes;
            foreach (Span raw in Lines(searched))
            {
                Span line = Trim(searched, raw);
                if (line.IsEmpty || searched[line.Begin] != (byte)'[') continue;
                Header header = ParseHeader(searched, line);
                if (header.Closed && EqualsAsciiIgnoreCase(searched, header.Name, StampSection)) return true;
            }
            return false;
        }

        internal sealed class KeyLine
        {
#if NULLABLE_ENABLED
            public KeyLine(byte[]? section, byte[] key, byte[] value, int line)
#else
            public KeyLine(byte[] section, byte[] key, byte[] value, int line)
#endif
            {
                Section = section;
                Key = key;
                Value = value;
                Line = line;
            }

            /// <summary>Null for a key above the first header or below one that names no section.</summary>
#if NULLABLE_ENABLED
            public byte[]? Section { get; }
#else
            public byte[] Section { get; }
#endif

            public byte[] Key { get; }

            public byte[] Value { get; }

            public int Line { get; }
        }

        /// <summary>
        /// Every key line in document order under <see cref="Parse"/>'s line, header and key rules,
        /// repeats and keys outside a section included. Bytes are not checked, so a NUL is an
        /// ordinary byte here; a UTF-16 document gives nothing useful and is the caller's to skip.
        /// </summary>
        internal static List<KeyLine> KeyLines(byte[] bytes)
        {
            var found = new List<KeyLine>();
#if NULLABLE_ENABLED
            byte[]? section = null;
#else
            byte[] section = null;
#endif
            int number = 0;
            foreach (Span raw in Lines(bytes))
            {
                number++;
                Span line = Trim(bytes, raw);
                if (line.IsEmpty || bytes[line.Begin] == (byte)';' || bytes[line.Begin] == (byte)'#') continue;
                if (bytes[line.Begin] == (byte)'[')
                {
                    Header header = ParseHeader(bytes, line);
                    section = header.Closed && !header.Name.IsEmpty ? Copy(bytes, header.Name) : null;
                    continue;
                }
                int equals = Array.IndexOf(bytes, (byte)'=', line.Begin, line.Length);
                if (equals < 0) continue;
                Span key = Trim(bytes, new Span(line.Begin, equals));
                if (key.IsEmpty) continue;
                found.Add(new KeyLine(section, Copy(bytes, key), Copy(bytes, Trim(bytes, new Span(equals + 1, line.End))), number));
            }
            return found;
        }

        private static byte[] Copy(byte[] bytes, Span span)
        {
            var copy = new byte[span.Length];
            Array.Copy(bytes, span.Begin, copy, 0, span.Length);
            return copy;
        }

        internal static byte[] NameBytes(string name, string parameter)
        {
            if (name == null) throw new ArgumentNullException(parameter);
            try
            {
                return StrictUtf8.GetBytes(name);
            }
            catch (EncoderFallbackException e)
            {
                throw new ArgumentException("The name holds an unpaired surrogate.", parameter, e);
            }
        }

        internal static bool EqualsAsciiIgnoreCase(byte[] a, byte[] b)
        {
            return EqualsAsciiIgnoreCase(a, new Span(0, a.Length), b);
        }

        private struct Span
        {
            public readonly int Begin;
            public readonly int End;

            public Span(int begin, int end)
            {
                Begin = begin;
                End = end;
            }

            public int Length
            {
                get { return End - Begin; }
            }

            public bool IsEmpty
            {
                get { return End == Begin; }
            }
        }

        private struct Header
        {
            public bool Closed;
            public Span Name;
            public Span Trailing;
        }

        private sealed class Entry
        {
            public byte[] Key = Empty;
            public byte[] Value = Empty;
            public int Line;
            public readonly List<int> EarlierLines = new List<int>();
        }

        private sealed class SectionEntry
        {
            public byte[] Name = Empty;
            public int Line;
            public readonly List<Entry> Values = new List<Entry>();
        }

        private sealed class Builder
        {
            private readonly byte[] _bytes;
            private readonly List<SectionEntry> _sections = new List<SectionEntry>();
            private readonly List<CanonicalDiagnostic> _diagnostics = new List<CanonicalDiagnostic>();
            // Index into _sections, or -1 above the first header and below one naming none.
            private int _current = -1;
            private int _stampHeaderLine;

            public Builder(byte[] bytes)
            {
                _bytes = bytes;
            }

            public void Read(Span raw, int number)
            {
                Span line = Trim(_bytes, raw);
                if (line.IsEmpty) return;
                byte first = _bytes[line.Begin];
                if (first == (byte)';' || first == (byte)'#') return;

                if (first == (byte)'[')
                {
                    ReadHeader(line, number);
                    return;
                }

                int equals = Array.IndexOf(_bytes, (byte)'=', line.Begin, line.Length);
                if (equals < 0)
                {
                    Add(CanonicalDiagnosticKind.MissingEquals, number, Empty, Empty, Slice(line));
                    return;
                }
                Span key = Trim(_bytes, new Span(line.Begin, equals));
                Span value = Trim(_bytes, new Span(equals + 1, line.End));
                if (key.IsEmpty)
                {
                    Add(CanonicalDiagnosticKind.EmptyKey, number, Empty, Empty, Slice(line));
                    return;
                }
                if (_current < 0)
                {
                    Add(CanonicalDiagnosticKind.KeyOutsideSection, number, Empty, Slice(key), Slice(value));
                    return;
                }
                SectionEntry section = _sections[_current];
                foreach (Entry existing in section.Values)
                {
                    if (!EqualsAsciiIgnoreCase(_bytes, key, existing.Key)) continue;
                    existing.EarlierLines.Add(existing.Line);
                    existing.Key = Slice(key);
                    existing.Value = Slice(value);
                    existing.Line = number;
                    return;
                }
                section.Values.Add(new Entry { Key = Slice(key), Value = Slice(value), Line = number });
            }

            public CanonicalIni Finish()
            {
                var sections = new CanonicalSection[_sections.Count];
                for (int s = 0; s < _sections.Count; s++)
                {
                    SectionEntry entry = _sections[s];
                    var values = new CanonicalValue[entry.Values.Count];
                    for (int v = 0; v < entry.Values.Count; v++)
                    {
                        Entry e = entry.Values[v];
                        values[v] = new CanonicalValue(e.Key, e.Value, e.Line, e.EarlierLines.ToArray());
                        if (e.EarlierLines.Count == 0) continue;
                        var lines = new List<int>(e.EarlierLines) { e.Line };
                        _diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.DuplicateKey, lines.ToArray(),
                            entry.Name, e.Key, e.Value));
                    }
                    sections[s] = new CanonicalSection(entry.Name, values, entry.Line);
                }

                int format = ReadFormat(sections);

                // List<T>.Sort is not stable, so the index each diagnostic was added at breaks ties.
                var order = new List<KeyValuePair<int, CanonicalDiagnostic>>();
                for (int i = 0; i < _diagnostics.Count; i++)
                {
                    order.Add(new KeyValuePair<int, CanonicalDiagnostic>(i, _diagnostics[i]));
                }
                order.Sort((a, b) =>
                {
                    int byLine = a.Value.Lines[0].CompareTo(b.Value.Lines[0]);
                    if (byLine != 0) return byLine;
                    int byKind = ((int)a.Value.Kind).CompareTo((int)b.Value.Kind);
                    return byKind != 0 ? byKind : a.Key.CompareTo(b.Key);
                });
                var diagnostics = new CanonicalDiagnostic[order.Count];
                for (int i = 0; i < order.Count; i++) diagnostics[i] = order[i].Value;

                return new CanonicalIni(CanonicalReadStatus.Readable, 0, format, sections, diagnostics);
            }

            private void ReadHeader(Span line, int number)
            {
                Header header = ParseHeader(_bytes, line);
                _current = -1;
                if (!header.Closed)
                {
                    Add(CanonicalDiagnosticKind.UnclosedSectionHeader, number, Empty, Empty, Slice(line));
                    return;
                }
                if (!header.Trailing.IsEmpty)
                {
                    Add(CanonicalDiagnosticKind.TextAfterSectionHeader, number, Slice(header.Name), Empty,
                        Slice(header.Trailing));
                }
                if (header.Name.IsEmpty)
                {
                    Add(CanonicalDiagnosticKind.EmptySectionName, number, Empty, Empty, Slice(line));
                    return;
                }
                for (int i = 0; i < _sections.Count; i++)
                {
                    if (EqualsAsciiIgnoreCase(_bytes, header.Name, _sections[i].Name)) _current = i;
                }
                if (_current >= 0) return;
                _current = _sections.Count;
                _sections.Add(new SectionEntry { Name = Slice(header.Name), Line = number });
                if (_stampHeaderLine == 0 && EqualsAsciiIgnoreCase(_bytes, header.Name, StampSection))
                {
                    _stampHeaderLine = number;
                }
            }

            private int ReadFormat(CanonicalSection[] sections)
            {
#if NULLABLE_ENABLED
                CanonicalSection? stamp = null;
#else
                CanonicalSection stamp = null;
#endif
                foreach (CanonicalSection section in sections)
                {
                    if (EqualsAsciiIgnoreCase(section.Name, StampSection)) stamp = section;
                }
                if (stamp == null) return ConfigFormat;

#if NULLABLE_ENABLED
                CanonicalValue? format = null;
#else
                CanonicalValue format = null;
#endif
                foreach (CanonicalValue value in stamp.Values)
                {
                    if (EqualsAsciiIgnoreCase(value.Key, FormatKey)) format = value;
                }
                if (format == null)
                {
                    Add(CanonicalDiagnosticKind.ConfigFormatMissing, _stampHeaderLine, stamp.Name, Empty, Empty);
                    return ConfigFormat;
                }

                int number = ParseFormatNumber(format.Value);
                if (number < 0)
                {
                    Add(CanonicalDiagnosticKind.ConfigFormatInvalid, format.Line, stamp.Name, format.Key, format.Value);
                    return ConfigFormat;
                }
                if (number > ConfigFormat)
                {
                    Add(CanonicalDiagnosticKind.ConfigFormatNewer, format.Line, stamp.Name, format.Key, format.Value);
                }
                return number;
            }

            private byte[] Slice(Span span)
            {
                var slice = new byte[span.Length];
                Array.Copy(_bytes, span.Begin, slice, 0, span.Length);
                return slice;
            }

            private void Add(CanonicalDiagnosticKind kind, int line, byte[] section, byte[] key, byte[] value)
            {
                _diagnostics.Add(new CanonicalDiagnostic(kind, new[] { line }, section, key, value));
            }
        }

        // ConfigFormat's number, or -1 when the value is not one: digits only, 1 or more.
        private static int ParseFormatNumber(byte[] value)
        {
            if (value.Length == 0) return -1;
            long number = 0;
            foreach (byte b in value)
            {
                if (b < (byte)'0' || b > (byte)'9') return -1;
                if (number < int.MaxValue) number = System.Math.Min(number * 10 + (b - (byte)'0'), int.MaxValue);
            }
            return number < 1 ? -1 : (int)number;
        }

        internal static bool StartsWithUtf16Mark(byte[] bytes)
        {
            return bytes.Length >= 2 && ((bytes[0] == 0xFF && bytes[1] == 0xFE) || (bytes[0] == 0xFE && bytes[1] == 0xFF));
        }

        // Every code unit above 0x7F becomes 0x80, which is not a byte the header rule or the
        // name "CameraUnlock" treats specially, so the header rule reads the result exactly as
        // it would read the units themselves. An odd last byte is not a unit and is dropped.
        private static byte[] NarrowUtf16(byte[] bytes)
        {
            bool littleEndian = bytes[0] == 0xFF;
            var narrow = new byte[(bytes.Length - 2) / 2];
            for (int i = 0; i < narrow.Length; i++)
            {
                int at = 2 + 2 * i;
                int unit = littleEndian ? bytes[at] | (bytes[at + 1] << 8) : (bytes[at] << 8) | bytes[at + 1];
                narrow[i] = unit < 0x80 ? (byte)unit : (byte)0x80;
            }
            return narrow;
        }

        // Each line untrimmed and without its terminator.
        private static IEnumerable<Span> Lines(byte[] bytes)
        {
            int pos = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
            while (pos < bytes.Length)
            {
                int end = pos;
                while (end < bytes.Length && bytes[end] != (byte)'\r' && bytes[end] != (byte)'\n') end++;
                yield return new Span(pos, end);
                bool crlf = end + 1 < bytes.Length && bytes[end] == (byte)'\r' && bytes[end + 1] == (byte)'\n';
                pos = crlf ? end + 2 : end + 1;
            }
        }

        private static int LineOfOffset(byte[] bytes, int offset)
        {
            int line = 1;
            for (int i = 0; i < offset; i++)
            {
                bool loneCr = bytes[i] == (byte)'\r' && (i + 1 >= bytes.Length || bytes[i + 1] != (byte)'\n');
                if (bytes[i] == (byte)'\n' || loneCr) line++;
            }
            return line;
        }

        private static bool IsSpaceOrTab(byte b)
        {
            return b == (byte)' ' || b == (byte)'\t';
        }

        private static Span Trim(byte[] bytes, Span span)
        {
            int begin = span.Begin;
            int end = span.End;
            while (begin < end && IsSpaceOrTab(bytes[begin])) begin++;
            while (end > begin && IsSpaceOrTab(bytes[end - 1])) end--;
            return new Span(begin, end);
        }

        // line is trimmed and starts with '['.
        private static Header ParseHeader(byte[] bytes, Span line)
        {
            var header = new Header();
            int close = Array.IndexOf(bytes, (byte)']', line.Begin + 1, line.Length - 1);
            if (close < 0) return header;
            header.Closed = true;
            header.Name = Trim(bytes, new Span(line.Begin + 1, close));
            header.Trailing = Trim(bytes, new Span(close + 1, line.End));
            return header;
        }

        private static byte FoldAscii(byte b)
        {
            return b >= (byte)'A' && b <= (byte)'Z' ? (byte)(b - 'A' + 'a') : b;
        }

        private static bool EqualsAsciiIgnoreCase(byte[] bytes, Span span, byte[] other)
        {
            if (span.Length != other.Length) return false;
            for (int i = 0; i < other.Length; i++)
            {
                if (FoldAscii(bytes[span.Begin + i]) != FoldAscii(other[i])) return false;
            }
            return true;
        }
    }
}
