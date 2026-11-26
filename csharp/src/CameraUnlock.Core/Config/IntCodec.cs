using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A whole number in decimal, '-' only when negative, no leading zeros. Reads -?[0-9]+
    /// (leading zeros and -0 included) within the codec's inclusive range. No '+', no hex, no
    /// white space.
    /// </summary>
    public sealed class IntCodec : IValueCodec<int>
    {
        /// <summary>Every int.</summary>
        public IntCodec() : this(int.MinValue, int.MaxValue)
        {
        }

        /// <exception cref="ArgumentException"><paramref name="min"/> is above <paramref name="max"/>.</exception>
        public IntCodec(int min, int max)
        {
            if (min > max)
            {
                throw new ArgumentException("range " + CodecText.Number(min) + " to " + CodecText.Number(max)
                    + " has min above max", "min");
            }
            Min = min;
            Max = max;
        }

        /// <summary>The smallest value read.</summary>
        public int Min { get; }

        /// <summary>The largest value read.</summary>
        public int Max { get; }

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out int value, out string? error)
#else
        public bool TryParse(byte[] text, out int value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = 0;
            error = null;
            bool negative = text.Length > 0 && text[0] == '-';
            int first = negative ? 1 : 0;
            if (first == text.Length)
            {
                error = Expectation();
                return false;
            }

            const long limit = 1L << 31;
            long magnitude = 0;
            for (int i = first; i < text.Length; i++)
            {
                byte b = text[i];
                if (b < '0' || b > '9')
                {
                    error = Expectation();
                    return false;
                }
                if (magnitude <= limit) magnitude = magnitude * 10 + (b - '0');
            }

            long signed = negative ? -magnitude : magnitude;
            if (signed < Min || signed > Max)
            {
                error = Expectation();
                return false;
            }
            value = (int)signed;
            return true;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> is outside the range.</exception>
        public byte[] Render(int value)
        {
            if (value < Min || value > Max)
            {
                throw new ArgumentOutOfRangeException("value", CodecText.Number(value) + " is outside "
                    + CodecText.Number(Min) + " to " + CodecText.Number(Max) + " and would not read back");
            }
            return CodecText.Ascii(CodecText.Number(value));
        }

        /// <inheritdoc/>
        public bool Equal(int a, int b)
        {
            return a == b;
        }

        private string Expectation()
        {
            return "expected a whole number from " + CodecText.Number(Min) + " to " + CodecText.Number(Max);
        }
    }
}
