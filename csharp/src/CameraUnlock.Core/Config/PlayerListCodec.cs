using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A list of player numbers from 1 to 8, held as its canonical text: <c>2, 3</c>, or empty
    /// for none. TryParse reads an empty value, or one of only spaces and tabs, as the empty list.
    /// Otherwise it splits the value at ',' and trims each item of spaces and tabs; an item is one
    /// or more ASCII digits for a number from 1 to 8, and a number given twice is refused. The list
    /// keeps the order written, and its canonical text is the numbers joined by ", ", so
    /// <c>3,02</c> reads as <c>3, 2</c>. The C++ twin is cameraunlock::config::PlayerListCodec.
    /// </summary>
    public sealed class PlayerListCodec : IValueCodec<string>
    {
        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out string value, out string? error)
#else
        public bool TryParse(byte[] text, out string value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = string.Empty;
            error = null;
            var players = new List<int>();
            bool blank = true;
            foreach (byte b in text)
            {
                if (b != (byte)' ' && b != (byte)'\t') blank = false;
            }
            if (blank) return true;

            int start = 0;
            while (true)
            {
                int comma = Array.IndexOf(text, (byte)',', start);
                int end = comma < 0 ? text.Length : comma;
                int first = start;
                int last = end;
                while (first < last && (text[first] == (byte)' ' || text[first] == (byte)'\t')) first++;
                while (last > first && (text[last - 1] == (byte)' ' || text[last - 1] == (byte)'\t')) last--;

                int number = 0;
                bool digits = last > first;
                for (int i = first; i < last; i++)
                {
                    byte c = text[i];
                    if (c < (byte)'0' || c > (byte)'9')
                    {
                        digits = false;
                        break;
                    }
                    // Held at 9 once past 8, so a long run of digits cannot overflow.
                    number = number * 10 + (c - (byte)'0');
                    if (number > 8) number = 9;
                }
                if (!digits || number < 1 || number > 8)
                {
                    byte[] item = new byte[last - first];
                    Array.Copy(text, first, item, 0, item.Length);
                    error = "'" + CodecText.Utf8Text(item) + "' is not a player number from 1 to 8";
                    return false;
                }
                if (players.Contains(number))
                {
                    error = "player " + number.ToString(CultureInfo.InvariantCulture) + " is listed twice";
                    return false;
                }
                players.Add(number);
                if (comma < 0) break;
                start = comma + 1;
            }

            var canonical = new StringBuilder();
            for (int i = 0; i < players.Count; i++)
            {
                if (i != 0) canonical.Append(", ");
                canonical.Append(players[i].ToString(CultureInfo.InvariantCulture));
            }
            value = canonical.ToString();
            return true;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentNullException"><paramref name="value"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> is not the canonical text
        /// of a player list, which would not read back as itself.</exception>
        public byte[] Render(string value)
        {
            if (value == null) throw new ArgumentNullException("value");

            string canonical;
#if NULLABLE_ENABLED
            string? error;
#else
            string error;
#endif
            if (!TryParse(Encoding.UTF8.GetBytes(value), out canonical, out error))
            {
                throw new ArgumentException("'" + value + "' is not a player list: " + error, "value");
            }
            if (canonical != value)
            {
                throw new ArgumentException("'" + value + "' is not written as a player list is written: expected '"
                    + canonical + "'", "value");
            }
            return Encoding.ASCII.GetBytes(canonical);
        }

        /// <inheritdoc/>
        /// <remarks>The canonical texts of two lists are equal exactly when the lists are.</remarks>
        public bool Equal(string a, string b)
        {
            return string.Equals(a, b, StringComparison.Ordinal);
        }
    }
}
