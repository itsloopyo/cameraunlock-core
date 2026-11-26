using System;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A float, written for people. Of the <c>ToString("G" + N)</c> texts (N = 1 to 9,
    /// invariant culture, 'E' lower-cased) that read back to the same bits, the one with the
    /// smallest N that has no exponent, or, when every one has an exponent, the one with the
    /// smallest N; then ".0" appended when the text has neither '.' nor 'e'. So 1 is
    /// <c>1.0</c>, 10 is <c>10.0</c> rather than <c>1e+01</c>, 0.15 is <c>0.15</c>, 0.00001 is
    /// <c>1e-05</c> and -0 is <c>-0.0</c>, as the C++ codec writes them.
    /// <para>
    /// Reads -?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)? through float.Parse with the invariant
    /// culture, within the codec's inclusive range. No inf, nan, leading '+', leading or
    /// trailing '.', comma or hex float. A number too large for a float is invalid, and so is
    /// one that is not zero but reads as zero, as std::from_chars reports both out of range.
    /// </para>
    /// <para>
    /// A value always reads back to the same bits on the runtime that wrote it. On .NET
    /// Framework the text can differ from the C++ codec's in the last digit, because its "G"
    /// formatting of a float is not always correctly rounded: it writes 1234.5677490234375 as
    /// 1234.5678 and the exact tie 3451485.25 as 3451485.3, where C++ and .NET 8 write
    /// 1234.5677 and 3451485.2. C++ reads such a text as the same float. Its float.Parse is
    /// not correctly rounded either: it reads 1.00000005960464477539062500001 as 1.0 where
    /// C++ reads 1.0000001, so a hand-typed text with more digits than this codec writes can
    /// read one step apart in the two languages.
    /// </para>
    /// </summary>
    public sealed class FloatCodec : IValueCodec<float>
    {
        private const float NegativeZero = -0.0f;

        /// <summary>Every finite float.</summary>
        public FloatCodec() : this(float.MinValue, float.MaxValue)
        {
        }

        /// <exception cref="ArgumentException">A bound is not finite, or <paramref name="min"/> is above <paramref name="max"/>.</exception>
        public FloatCodec(float min, float max)
        {
            if (float.IsNaN(min) || float.IsInfinity(min) || float.IsNaN(max) || float.IsInfinity(max) || min > max)
            {
                throw new ArgumentException("float codec range needs finite bounds with min not above max", "min");
            }
            Min = min;
            Max = max;
        }

        /// <summary>The smallest value read.</summary>
        public float Min { get; }

        /// <summary>The largest value read.</summary>
        public float Max { get; }

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out float value, out string? error)
#else
        public bool TryParse(byte[] text, out float value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = 0f;
            error = null;
            bool fullRange = Bits(Min) == Bits(float.MinValue) && Bits(Max) == Bits(float.MaxValue);
            if (!CodecText.MatchesNumberGrammar(text))
            {
                error = fullRange ? "expected a number such as 1.0, 0.15 or 1e-05" : RangeExpectation();
                return false;
            }

            string s = Encoding.ASCII.GetString(text);
            float read;
            if (!TryRead(s, out read))
            {
                error = fullRange ? "expected a number from " + RenderFinite(float.MinValue) + " to "
                    + RenderFinite(float.MaxValue) : RangeExpectation();
                return false;
            }
            if (read == 0f && CodecText.HasNonZeroDigit(s))
            {
                error = Min > 0f || Max < 0f ? RangeExpectation()
                    : "expected 0.0 or a number no closer to zero than " + RenderFinite(float.Epsilon);
                return false;
            }
            if (read < Min || read > Max)
            {
                error = RangeExpectation();
                return false;
            }
            value = read;
            return true;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentOutOfRangeException"><paramref name="value"/> is not finite or is outside the range.</exception>
        public byte[] Render(float value)
        {
            if (float.IsNaN(value) || float.IsInfinity(value) || value < Min || value > Max)
            {
                throw new ArgumentOutOfRangeException("value", "float value is not finite or is outside "
                    + RenderFinite(Min) + " to " + RenderFinite(Max) + ", and would not read back");
            }
            return CodecText.Ascii(RenderFinite(value));
        }

        /// <summary>Bitwise, so -0.0 and 0.0 differ.</summary>
        public bool Equal(float a, float b)
        {
            return Bits(a) == Bits(b);
        }

        private string RangeExpectation()
        {
            return "expected a number from " + RenderFinite(Min) + " to " + RenderFinite(Max);
        }

        internal static int Bits(float value)
        {
            return BitConverter.ToInt32(BitConverter.GetBytes(value), 0);
        }

        // False for a number too large for a float on .NET Framework, which throws
        // OverflowException; newer runtimes return infinity, which is outside every range. A
        // text starting '-' that reads as zero is -0, which .NET Framework reads as +0.
        private static bool TryRead(string text, out float value)
        {
            try
            {
                value = float.Parse(text, NumberStyles.Float, CultureInfo.InvariantCulture);
            }
            catch (OverflowException)
            {
                value = 0f;
                return false;
            }
            if (value == 0f && text[0] == '-') value = NegativeZero;
            return true;
        }

        internal static string RenderFinite(float value)
        {
            if (Bits(value) == Bits(NegativeZero)) return "-0.0";
            return CodecText.ChooseFloatText(9,
                precision => value.ToString("G" + CodecText.Number(precision), CultureInfo.InvariantCulture),
                text =>
                {
                    float read;
                    return TryRead(text, out read) && Bits(read) == Bits(value);
                });
        }
    }
}
