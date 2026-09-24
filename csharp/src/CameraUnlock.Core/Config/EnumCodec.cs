using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A word from a closed list, never a number. Written in the declared spelling, read ASCII
    /// case-insensitively.
    /// </summary>
    public sealed class EnumCodec<TEnum> : IValueCodec<TEnum> where TEnum : struct, Enum
    {
        private readonly EnumToken<TEnum>[] tokens;

        /// <exception cref="ArgumentNullException"><paramref name="tokens"/> or a token is null.</exception>
        /// <exception cref="ArgumentException">The list is empty, a token is not PascalCase (a
        /// capital ASCII letter, then ASCII letters and digits), two tokens are equal ASCII
        /// case-insensitively, or two tokens name one enumerator.</exception>
        public EnumCodec(params EnumToken<TEnum>[] tokens)
        {
            if (tokens == null) throw new ArgumentNullException("tokens");
            if (tokens.Length == 0) throw new ArgumentException("an enum codec needs at least one token", "tokens");

            this.tokens = (EnumToken<TEnum>[])tokens.Clone();
            var comparer = EqualityComparer<TEnum>.Default;
            for (int i = 0; i < this.tokens.Length; i++)
            {
                string token = this.tokens[i].Token;
                if (token == null) throw new ArgumentNullException("tokens", "token " + CodecText.Number(i + 1) + " is null");
                if (!IsPascalCase(token))
                {
                    throw new ArgumentException("enum token '" + token
                        + "' is not PascalCase: expected a capital letter, then letters and digits", "tokens");
                }
                for (int j = 0; j < i; j++)
                {
                    if (CodecText.EqualsAsciiIgnoreCase(CodecText.Ascii(token), this.tokens[j].Token))
                    {
                        throw new ArgumentException("enum tokens '" + this.tokens[j].Token + "' and '" + token
                            + "' read the same", "tokens");
                    }
                    if (comparer.Equals(this.tokens[i].Value, this.tokens[j].Value))
                    {
                        throw new ArgumentException("enum tokens '" + this.tokens[j].Token + "' and '" + token
                            + "' name one value", "tokens");
                    }
                }
            }
            Tokens = new ReadOnlyCollection<EnumToken<TEnum>>(this.tokens);
        }

        /// <summary>The tokens, in the order given.</summary>
        public ReadOnlyCollection<EnumToken<TEnum>> Tokens { get; }

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out TEnum value, out string? error)
#else
        public bool TryParse(byte[] text, out TEnum value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = default(TEnum);
            error = null;
            foreach (EnumToken<TEnum> entry in tokens)
            {
                if (CodecText.EqualsAsciiIgnoreCase(text, entry.Token))
                {
                    value = entry.Value;
                    return true;
                }
            }
            var words = new List<string>();
            foreach (EnumToken<TEnum> entry in tokens) words.Add(entry.Token);
            error = "expected " + CodecText.JoinAlternatives(words);
            return false;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentException"><paramref name="value"/> has no token.</exception>
        public byte[] Render(TEnum value)
        {
            var comparer = EqualityComparer<TEnum>.Default;
            foreach (EnumToken<TEnum> entry in tokens)
            {
                if (comparer.Equals(entry.Value, value)) return CodecText.Ascii(entry.Token);
            }
            throw new ArgumentException("enum value " + Convert.ToString(value, CultureInfo.InvariantCulture)
                + " has no token", "value");
        }

        /// <inheritdoc/>
        public bool Equal(TEnum a, TEnum b)
        {
            return EqualityComparer<TEnum>.Default.Equals(a, b);
        }

        private static bool IsPascalCase(string token)
        {
            if (token.Length == 0 || token[0] < 'A' || token[0] > 'Z') return false;
            foreach (char c in token)
            {
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
            }
            return true;
        }
    }
}
