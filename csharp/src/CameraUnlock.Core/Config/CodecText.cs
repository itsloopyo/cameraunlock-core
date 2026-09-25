using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>Byte and number helpers the value codecs share.</summary>
    internal static class CodecText
    {
        public static byte[] Ascii(string text)
        {
            return Encoding.ASCII.GetBytes(text);
        }

        /// <summary>'text' in single quotes, as <see cref="Utf8Text"/> decodes it.</summary>
        public static string Quote(byte[] text)
        {
            return "'" + Utf8Text(text) + "'";
        }

        /// <summary>
        /// The bytes as UTF-8, each maximal subpart of an ill-formed sequence decoded as one U+FFFD,
        /// which is what C++ core writes for the same bytes. Encoding.UTF8 is not used because .NET
        /// Framework substitutes differently: it reads F0 80 80 as two U+FFFD, not three.
        /// </summary>
        public static string Utf8Text(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException("bytes");

            var text = new StringBuilder(bytes.Length);
            int i = 0;
            while (i < bytes.Length)
            {
                byte lead = bytes[i];
                if (lead < 0x80)
                {
                    text.Append((char)lead);
                    i++;
                    continue;
                }

                int length = 0;
                int codePoint = 0;
                byte low = 0x80;
                byte high = 0xBF;
                if (lead >= 0xC2 && lead <= 0xDF)
                {
                    length = 2;
                    codePoint = lead & 0x1F;
                }
                else if (lead >= 0xE0 && lead <= 0xEF)
                {
                    length = 3;
                    codePoint = lead & 0x0F;
                    if (lead == 0xE0) low = 0xA0;
                    else if (lead == 0xED) high = 0x9F;
                }
                else if (lead >= 0xF0 && lead <= 0xF4)
                {
                    length = 4;
                    codePoint = lead & 0x07;
                    if (lead == 0xF0) low = 0x90;
                    else if (lead == 0xF4) high = 0x8F;
                }

                int next = i + 1;
                while (next < i + length && next < bytes.Length && bytes[next] >= low && bytes[next] <= high)
                {
                    codePoint = (codePoint << 6) | (bytes[next] & 0x3F);
                    next++;
                    low = 0x80;
                    high = 0xBF;
                }
                text.Append(char.ConvertFromUtf32(length != 0 && next == i + length ? codePoint : 0xFFFD));
                i = next;
            }
            return text.ToString();
        }

        public static string Number(int value)
        {
            return value.ToString(CultureInfo.InvariantCulture);
        }

        public static bool EqualsAsciiIgnoreCase(byte[] bytes, string ascii)
        {
            if (bytes.Length != ascii.Length) return false;
            for (int i = 0; i < bytes.Length; i++)
            {
                if (FoldAscii((char)bytes[i]) != FoldAscii(ascii[i])) return false;
            }
            return true;
        }

        public static char FoldAscii(char c)
        {
            return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        }

        public static bool IsSpaceOrTab(byte b)
        {
            return b == ' ' || b == '\t';
        }

        public static bool IsSpaceOrTab(char c)
        {
            return c == ' ' || c == '\t';
        }

        public static byte[] Trim(byte[] bytes, int start, int end)
        {
            while (start < end && IsSpaceOrTab(bytes[start])) start++;
            while (end > start && IsSpaceOrTab(bytes[end - 1])) end--;
            var trimmed = new byte[end - start];
            Array.Copy(bytes, start, trimmed, 0, trimmed.Length);
            return trimmed;
        }

        /// <summary>The parts between commas, each trimmed of spaces and tabs.</summary>
        public static List<byte[]> SplitAtCommasTrimmed(byte[] bytes)
        {
            var parts = new List<byte[]>();
            int start = 0;
            for (int i = 0; i <= bytes.Length; i++)
            {
                if (i == bytes.Length || bytes[i] == ',')
                {
                    parts.Add(Trim(bytes, start, i));
                    start = i + 1;
                }
            }
            return parts;
        }

        /// <summary>"A", "A or B", "A, B or C".</summary>
        public static string JoinAlternatives(IList<string> items)
        {
            var text = new StringBuilder();
            for (int i = 0; i < items.Count; i++)
            {
                if (i > 0) text.Append(i + 1 == items.Count ? " or " : ", ");
                text.Append(items[i]);
            }
            return text.ToString();
        }

        private static bool IsDigit(byte b)
        {
            return b >= '0' && b <= '9';
        }

        /// <summary>0x or 0X and 1 to <paramref name="maxDigits"/> hex digits of either case.</summary>
#if NULLABLE_ENABLED
        public static bool TryParseHex(byte[] text, int maxDigits, out ulong value, out string? error)
#else
        public static bool TryParseHex(byte[] text, int maxDigits, out ulong value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = 0;
            error = "expected 0x and 1 to " + Number(maxDigits) + " hex digits, such as 0x404";
            if (text.Length < 3 || text.Length - 2 > maxDigits || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
            {
                return false;
            }
            ulong read = 0;
            for (int i = 2; i < text.Length; i++)
            {
                byte b = text[i];
                int digit;
                if (b >= '0' && b <= '9') digit = b - '0';
                else if (b >= 'a' && b <= 'f') digit = b - 'a' + 10;
                else if (b >= 'A' && b <= 'F') digit = b - 'A' + 10;
                else return false;
                read = (read << 4) | (uint)digit;
            }
            value = read;
            error = null;
            return true;
        }

        /// <summary>-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?</summary>
        public static bool MatchesNumberGrammar(byte[] text)
        {
            int i = 0;
            int n = text.Length;
            if (i < n && text[i] == '-') i++;
            int integerStart = i;
            while (i < n && IsDigit(text[i])) i++;
            if (i == integerStart) return false;
            if (i < n && text[i] == '.')
            {
                i++;
                int fractionStart = i;
                while (i < n && IsDigit(text[i])) i++;
                if (i == fractionStart) return false;
            }
            if (i < n && (text[i] == 'e' || text[i] == 'E'))
            {
                i++;
                if (i < n && (text[i] == '+' || text[i] == '-')) i++;
                int exponentStart = i;
                while (i < n && IsDigit(text[i])) i++;
                if (i == exponentStart) return false;
            }
            return i == n;
        }

        /// <summary>For a text of the number grammar: whether a digit before the exponent is not 0.</summary>
        public static bool HasNonZeroDigit(string text)
        {
            foreach (char c in text)
            {
                if (c == 'e' || c == 'E') return false;
                if (c >= '1' && c <= '9') return true;
            }
            return false;
        }

        /// <summary>
        /// The float rule's choice among the "G1" to "G{max}" texts (lower-case 'e'): the one
        /// with the smallest precision that has no exponent, or, when every text that reads
        /// back has an exponent, the one with the smallest precision; then ".0" appended when it
        /// has neither '.' nor 'e'. <paramref name="format"/> gives the text at a precision and
        /// <paramref name="readsBack"/> whether it reads back to the same bits.
        /// </summary>
        public static string ChooseFloatText(int maxPrecision, Func<int, string> format, Func<string, bool> readsBack)
        {
            string chosen = string.Empty;
            for (int precision = 1; precision <= maxPrecision; precision++)
            {
                string text = format(precision).Replace('E', 'e');
                if (!readsBack(text)) continue;
                if (text.IndexOf('e') < 0)
                {
                    chosen = text;
                    break;
                }
                if (chosen.Length == 0) chosen = text;
            }
            if (chosen.Length == 0)
            {
                throw new InvalidOperationException(
                    "no G1 to G" + Number(maxPrecision) + " text of a finite value read back to the same bits");
            }
            if (chosen.IndexOf('.') < 0 && chosen.IndexOf('e') < 0) chosen += ".0";
            return chosen;
        }

        /// <summary>Items separated by commas, each trimmed, non-empty and read by <paramref name="item"/>.</summary>
#if NULLABLE_ENABLED
        public static bool TryParseList<T>(byte[] text, IValueCodec<T> item, out T[] items, out string? error)
#else
        public static bool TryParseList<T>(byte[] text, IValueCodec<T> item, out T[] items, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            items = new T[0];
            error = null;
            if (Trim(text, 0, text.Length).Length == 0) return true;

            List<byte[]> parts = SplitAtCommasTrimmed(text);
            var read = new T[parts.Count];
            for (int i = 0; i < parts.Count; i++)
            {
                if (parts[i].Length == 0)
                {
                    error = "item " + Number(i + 1) + " is empty: expected a value between commas";
                    return false;
                }
                if (!item.TryParse(parts[i], out read[i], out error))
                {
                    error = "item " + Number(i + 1) + " " + Quote(parts[i]) + ": " + error;
                    return false;
                }
            }
            items = read;
            return true;
        }

        /// <summary>The items' texts joined by ", ".</summary>
        public static byte[] RenderList<T>(T[] items, IValueCodec<T> item)
        {
            if (items == null) throw new ArgumentNullException("items");

            var text = new List<byte>();
            for (int i = 0; i < items.Length; i++)
            {
                byte[] rendered = item.Render(items[i]);
                if (rendered.Length == 0 || Array.IndexOf(rendered, (byte)',') >= 0)
                {
                    throw new ArgumentException("list item " + Number(i + 1)
                        + " is empty or holds ',' and would not read back as one item", "items");
                }
                if (i > 0)
                {
                    text.Add((byte)',');
                    text.Add((byte)' ');
                }
                text.AddRange(rendered);
            }
            return text.ToArray();
        }

        public static bool ListEqual<T>(T[] a, T[] b, IValueCodec<T> item)
        {
            if (a == null) throw new ArgumentNullException("a");
            if (b == null) throw new ArgumentNullException("b");
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (!item.Equal(a[i], b[i])) return false;
            }
            return true;
        }
    }
}
