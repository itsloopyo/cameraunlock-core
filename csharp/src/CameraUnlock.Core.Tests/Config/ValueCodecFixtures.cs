#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// The value codecs against data/fixtures/canonical-ini/codecs/cases.tsv, which
    /// cpp/tests/value_codecs_tests.cpp runs through the C++ codecs, plus the float and double
    /// round-trip sweep. The same source runs under xunit on net8.0 (ValueCodecTests) and in the
    /// CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5 and 4.7.2. C# 7.3 and no
    /// test framework, so the net35 build can compile it.
    /// </summary>
    internal static class ValueCodecFixtures
    {
        public enum FixtureMode
        {
            Never = 0,
            MenusOnly = 1,
            AllDialogue = 2,
            AllOverlays = 3,
        }

        public static readonly EnumCodec<FixtureMode> FixtureEnum = new EnumCodec<FixtureMode>(
            new EnumToken<FixtureMode>("Never", FixtureMode.Never),
            new EnumToken<FixtureMode>("MenusOnly", FixtureMode.MenusOnly),
            new EnumToken<FixtureMode>("AllDialogue", FixtureMode.AllDialogue),
            new EnumToken<FixtureMode>("AllOverlays", FixtureMode.AllOverlays));

        /// <summary>Every row of codecs/cases.tsv under the fixture root, as written.</summary>
        public static string[] Rows(string root)
        {
            byte[] tsv = File.ReadAllBytes(Path.Combine(Path.Combine(root, "codecs"), "cases.tsv"));
            var rows = new List<string>();
            var codecs = new Dictionary<string, bool>();
            foreach (string line in Encoding.ASCII.GetString(tsv).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                rows.Add(line);
                codecs[line.Split('\t')[0]] = true;
            }
            if (codecs.Count != 14) throw new InvalidOperationException(codecs.Count + " codec names under " + root + ", expected 14");
            return rows.ToArray();
        }

        /// <summary>Throws when the row does not read as it says.</summary>
        public static void RunRow(string row)
        {
            string[] fields = row.Split('\t');
            if (fields.Length < 3) throw new InvalidOperationException("malformed row " + row);
            switch (fields[0])
            {
                case "bool": Run(new BoolCodec(), false, fields, row, null); break;
                case "int": Run(new IntCodec(), 0, fields, row, null); break;
                case "int[1,65535]": Run(new IntCodec(1, 65535), 0, fields, row, null); break;
                case "hex32": Run(new Hex32Codec(), 0u, fields, row, null); break;
                case "hex64": Run(new Hex64Codec(), 0ul, fields, row, null); break;
                case "float": Run(new FloatCodec(), 0f, fields, row, FloatBits); break;
                case "float[0,1]": Run(new FloatCodec(0f, 1f), 0f, fields, row, FloatBits); break;
                case "double": Run(new DoubleCodec(), 0.0, fields, row, DoubleBits); break;
                case "string": Run(new StringCodec(), string.Empty, fields, row, null); break;
                case "enum": Run(FixtureEnum, default(FixtureMode), fields, row, null); break;
                case "color": Run(new ColorCodec(), new float[0], fields, row, null); break;
                case "list<hex32>": Run(new Hex32ListCodec(), new uint[0], fields, row, null); break;
                case "list<hex64>": Run(new Hex64ListCodec(), new ulong[0], fields, row, null); break;
                case "list<string>": Run(new StringListCodec(), new string[0], fields, row, null); break;
                default: throw new InvalidOperationException("unknown codec in row " + row);
            }
        }

        private static void Run<T>(IValueCodec<T> codec, T empty, string[] fields, string row, Func<T, string> bits)
        {
            T parsed;
            string error;
            bool read = codec.TryParse(Unescape(fields[1]), out parsed, out error);

            if (fields[2] == "invalid")
            {
                if (fields.Length != 3) throw new InvalidOperationException("malformed row " + row);
                if (read || string.IsNullOrEmpty(error) || !codec.Equal(parsed, empty))
                {
                    throw new InvalidOperationException(row + ": read, expected invalid");
                }
                return;
            }
            if (fields[2] != "canonical" || fields.Length != (bits == null ? 4 : 5))
            {
                throw new InvalidOperationException("malformed row " + row);
            }
            if (!read || error != null) throw new InvalidOperationException(row + ": " + error);

            byte[] expected = Unescape(fields[3]);
            byte[] rendered = codec.Render(parsed);
            if (!Same(rendered, expected))
            {
                throw new InvalidOperationException(row + ": written as '" + Encoding.UTF8.GetString(rendered) + "'");
            }
            if (bits != null && bits(parsed) != fields[4])
            {
                throw new InvalidOperationException(row + ": read as " + bits(parsed));
            }

            T again;
            if (!codec.TryParse(rendered, out again, out error) || !codec.Equal(again, parsed)
                || !Same(codec.Render(again), rendered))
            {
                throw new InvalidOperationException(row + ": its canonical text does not read back as itself");
            }
        }

        public static string FloatBits(float value)
        {
            return "0x" + BitConverter.ToUInt32(BitConverter.GetBytes(value), 0).ToString("X8", CultureInfo.InvariantCulture);
        }

        public static string DoubleBits(double value)
        {
            return "0x" + BitConverter.DoubleToInt64Bits(value).ToString("X16", CultureInfo.InvariantCulture);
        }

        /// <summary>
        /// Renders and reads back every value swept: zeros, the smallest denormal, the smallest
        /// normal, the largest finite value, 64 neighbours of each, every power of ten a float
        /// holds and its two neighbours, 1e-05, 0.15 and <paramref name="randomCount"/> finite
        /// random bit patterns. Returns how many values were swept; throws naming the first
        /// value that did not read back bit for bit.
        /// </summary>
        public static int SweepFloats(int randomCount)
        {
            var codec = new FloatCodec();
            var bits = new List<uint> { 0u, 0x80000000u, 0x3727C5ACu, 0x3E19999Au, 0x3DCCCCCDu, 0x3F800000u };
            foreach (uint edge in new[] { 0x00000001u, 0x00800000u, 0x7F7FFFFFu })
            {
                for (uint k = 0; k <= 64; k++)
                {
                    bits.Add(edge + k);
                    bits.Add((edge + k) | 0x80000000u);
                    if (k > edge) continue;
                    bits.Add(edge - k);
                    bits.Add((edge - k) | 0x80000000u);
                }
            }
            for (int exponent = -45; exponent <= 38; exponent++)
            {
                uint power = FloatToBits((float)System.Math.Pow(10, exponent));
                if (power == 0 || power >= 0x7F800000u) continue;
                bits.Add(power);
                bits.Add(power + 1);
                bits.Add(power - 1);
            }
            ulong state = 0xC0DEC5EEDul;
            while (randomCount > 0)
            {
                uint pattern = (uint)(SplitMix64(ref state) >> 32);
                if ((pattern & 0x7F800000u) == 0x7F800000u) continue;
                bits.Add(pattern);
                randomCount--;
            }

            foreach (uint pattern in bits)
            {
                if ((pattern & 0x7F800000u) == 0x7F800000u) continue;
                float value = BitConverter.ToSingle(BitConverter.GetBytes(pattern), 0);
                byte[] text = codec.Render(value);
                float read;
                string error;
                if (!codec.TryParse(text, out read, out error) || FloatToBits(read) != pattern)
                {
                    throw new InvalidOperationException(FloatBits(value) + " was written '" + Encoding.ASCII.GetString(text)
                        + "' and read back as " + (error ?? FloatBits(read)));
                }
            }
            return bits.Count;
        }

        /// <summary>The double twin of <see cref="SweepFloats"/>.</summary>
        public static int SweepDoubles(int randomCount)
        {
            var codec = new DoubleCodec();
            var bits = new List<ulong> { 0ul, 0x8000000000000000ul, 0x3EE4F8B588E368F1ul, 0x3FC3333333333333ul, 0x3FB999999999999Aul };
            foreach (ulong edge in new[] { 0x0000000000000001ul, 0x0010000000000000ul, 0x7FEFFFFFFFFFFFFFul })
            {
                for (ulong k = 0; k <= 64; k++)
                {
                    bits.Add(edge + k);
                    bits.Add((edge + k) | 0x8000000000000000ul);
                    if (k > edge) continue;
                    bits.Add(edge - k);
                    bits.Add((edge - k) | 0x8000000000000000ul);
                }
            }
            for (int exponent = -323; exponent <= 308; exponent++)
            {
                ulong power = (ulong)BitConverter.DoubleToInt64Bits(System.Math.Pow(10, exponent));
                if (power == 0 || power >= 0x7FF0000000000000ul) continue;
                bits.Add(power);
                bits.Add(power + 1);
                bits.Add(power - 1);
            }
            ulong state = 0xC0DEC5EEDul;
            while (randomCount > 0)
            {
                ulong pattern = SplitMix64(ref state);
                if ((pattern & 0x7FF0000000000000ul) == 0x7FF0000000000000ul) continue;
                bits.Add(pattern);
                randomCount--;
            }

            foreach (ulong pattern in bits)
            {
                if ((pattern & 0x7FF0000000000000ul) == 0x7FF0000000000000ul) continue;
                double value = BitConverter.Int64BitsToDouble((long)pattern);
                byte[] text = codec.Render(value);
                double read;
                string error;
                if (!codec.TryParse(text, out read, out error) || (ulong)BitConverter.DoubleToInt64Bits(read) != pattern)
                {
                    throw new InvalidOperationException(DoubleBits(value) + " was written '" + Encoding.ASCII.GetString(text)
                        + "' and read back as " + (error ?? DoubleBits(read)));
                }
            }
            return bits.Count;
        }

        private static uint FloatToBits(float value)
        {
            return BitConverter.ToUInt32(BitConverter.GetBytes(value), 0);
        }

        private static ulong SplitMix64(ref ulong state)
        {
            ulong z = state += 0x9E3779B97F4A7C15ul;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ul;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBul;
            return z ^ (z >> 31);
        }

        private static bool Same(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (a[i] != b[i]) return false;
            }
            return true;
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
                }
                else if (i + 1 < field.Length && field[i + 1] == '\\')
                {
                    bytes.Add((byte)'\\');
                    i++;
                }
                else if (i + 3 < field.Length && field[i + 1] == 'x')
                {
                    bytes.Add(byte.Parse(field.Substring(i + 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                    i += 3;
                }
                else
                {
                    throw new InvalidOperationException("bad escape in fixture field " + field);
                }
            }
            return bytes.ToArray();
        }
    }
}
