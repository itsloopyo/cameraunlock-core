using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using CameraUnlock.Core.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="ValueCodecFixtures"/> on this test host's runtime, plus the parts of the
    /// codec API the fixtures do not reach. The CameraUnlock.Core.FrameworkTests console runs the
    /// same rows and sweep on .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class ValueCodecTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Rows()
        {
            return ValueCodecFixtures.Rows(Root()).Select(r => new object[] { r });
        }

        [Theory]
        [MemberData(nameof(Rows))]
        public void Fixture(string row)
        {
            ValueCodecFixtures.RunRow(row);
        }

        [Fact]
        public void FloatsReadBackFromTheirRender()
        {
            Assert.True(ValueCodecFixtures.SweepFloats(200000) > 200000);
        }

        [Fact]
        public void DoublesReadBackFromTheirRender()
        {
            Assert.True(ValueCodecFixtures.SweepDoubles(200000) > 200000);
        }

        private static byte[] B(string text)
        {
            return Encoding.UTF8.GetBytes(text);
        }

        private static string Error<T>(IValueCodec<T> codec, string text)
        {
            Assert.False(codec.TryParse(B(text), out _, out string? error));
            return error!;
        }

        [Fact]
        public void ErrorsNameTheExpectation()
        {
            Assert.Equal("expected true or false", Error(new BoolCodec(), "maybe"));
            Assert.Equal("expected a whole number from 1 to 65535", Error(new IntCodec(1, 65535), "0"));
            Assert.Equal("expected a whole number from -2147483648 to 2147483647", Error(new IntCodec(), "x"));
            Assert.Equal("expected 0x and 1 to 8 hex digits, such as 0x404", Error(new Hex32Codec(), "404"));
            Assert.Equal("expected 0x and 1 to 16 hex digits, such as 0x404", Error(new Hex64Codec(), "404"));
            Assert.Equal("expected a number such as 1.0, 0.15 or 1e-05", Error(new FloatCodec(), ".5"));
            Assert.Equal("expected a number from 0.0 to 1.0", Error(new FloatCodec(0f, 1f), ".5"));
            Assert.Equal("expected a number from 0.0 to 1.0", Error(new FloatCodec(0f, 1f), "1.5"));
            Assert.Equal("expected a number from -3.4028235e+38 to 3.4028235e+38", Error(new FloatCodec(), "1e39"));
            Assert.Equal("expected a number from -1.7976931348623157e+308 to 1.7976931348623157e+308",
                Error(new DoubleCodec(), "-1e309"));
            Assert.Equal("expected 0.0 or a number no closer to zero than 1e-45", Error(new FloatCodec(), "1e-46"));
            Assert.Equal("expected 0.0 or a number no closer to zero than 5e-324", Error(new DoubleCodec(), "-1e-400"));
            Assert.Equal("expected a number from 0.5 to 1.0", Error(new FloatCodec(0.5f, 1f), "1e-46"));
            Assert.Equal("holds the control byte 0x01: expected text with no control character but tab",
                Error(new StringCodec(), "a\u0001z"));
            Assert.Equal("expected Never, MenusOnly, AllDialogue or AllOverlays",
                Error(ValueCodecFixtures.FixtureEnum, "Sometimes"));
            Assert.Equal("expected four numbers from 0.0 to 1.0 separated by commas, such as 1.0, 0.5, 0.0, 1.0",
                Error(new ColorCodec(), "1,1,1"));
            Assert.Equal("item 2 '2': expected a number from 0.0 to 1.0", Error(new ColorCodec(), "1, 2, 1, 1"));
            Assert.Equal("item 2 is empty: expected a value between commas", Error(new Hex32ListCodec(), "0x1, ,0x2"));
            Assert.Equal("item 2 'zz': expected 0x and 1 to 8 hex digits, such as 0x404", Error(new Hex32ListCodec(), "0x1, zz"));
        }

        [Fact]
        public void BytesThatAreNotUtf8AreInvalid()
        {
            Assert.False(new StringCodec().TryParse(new byte[] { 0xE9, (byte)'t', 0xE9 }, out string value, out string? error));
            Assert.Equal("is not UTF-8: expected text saved as UTF-8", error);
            Assert.Equal(string.Empty, value);
            Assert.False(new StringListCodec().TryParse(new byte[] { (byte)'a', (byte)',', 0xE9 }, out string[] items, out error));
            Assert.Equal("item 2 '�': is not UTF-8: expected text saved as UTF-8", error);
            Assert.Empty(items);
        }

        [Fact]
        public void RenderThrowsForWhatWouldNotReadBack()
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => new IntCodec(1, 65535).Render(0));
            Assert.Throws<ArgumentOutOfRangeException>(() => new FloatCodec().Render(float.NaN));
            Assert.Throws<ArgumentOutOfRangeException>(() => new DoubleCodec().Render(double.PositiveInfinity));
            Assert.Throws<ArgumentOutOfRangeException>(() => new FloatCodec(0f, 1f).Render(1.5f));
            Assert.Throws<ArgumentException>(() => new StringCodec().Render(" a"));
            Assert.Throws<ArgumentException>(() => new StringCodec().Render("a\t"));
            Assert.Throws<ArgumentException>(() => new StringCodec().Render("a\nb"));
            Assert.Throws<EncoderFallbackException>(() => new StringCodec().Render("a\uD800"));
            Assert.Throws<ArgumentException>(() => new StringListCodec().Render(new[] { "a,b" }));
            Assert.Throws<ArgumentException>(() => new StringListCodec().Render(new[] { "" }));
            Assert.Throws<ArgumentException>(() => new ColorCodec().Render(new[] { 1f, 1f, 1f }));
            Assert.Throws<ArgumentOutOfRangeException>(() => new ColorCodec().Render(new[] { 1f, 1.5f, 0f, 1f }));
            Assert.Throws<ArgumentException>(() => ValueCodecFixtures.FixtureEnum.Render((ValueCodecFixtures.FixtureMode)7));
        }

        [Fact]
        public void NullsThrow()
        {
            Assert.Throws<ArgumentNullException>(() => new BoolCodec().TryParse(null!, out _, out _));
            Assert.Throws<ArgumentNullException>(() => new Hex64ListCodec().TryParse(null!, out _, out _));
            Assert.Throws<ArgumentNullException>(() => new StringCodec().Render(null!));
            Assert.Throws<ArgumentNullException>(() => new Hex32ListCodec().Render(null!));
            Assert.Throws<ArgumentNullException>(() => new ColorCodec().Equal(null!, new float[4]));
        }

        [Fact]
        public void RangesAreChecked()
        {
            Assert.Throws<ArgumentException>(() => new IntCodec(5, 1));
            Assert.Throws<ArgumentException>(() => new FloatCodec(float.NaN, 1f));
            Assert.Throws<ArgumentException>(() => new DoubleCodec(0, double.PositiveInfinity));
            Assert.Throws<ArgumentException>(() => new FloatCodec(1f, 0f));
            var port = new IntCodec(1, 65535);
            Assert.Equal(1, port.Min);
            Assert.Equal(65535, port.Max);
            Assert.Equal(float.MinValue, new FloatCodec().Min);
            Assert.Equal(double.MaxValue, new DoubleCodec().Max);
        }

        [Fact]
        public void EnumTokensAreChecked()
        {
            Assert.Throws<ArgumentException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>());
            Assert.Throws<ArgumentException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>(
                new EnumToken<ValueCodecFixtures.FixtureMode>("menusOnly", ValueCodecFixtures.FixtureMode.MenusOnly)));
            Assert.Throws<ArgumentException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>(
                new EnumToken<ValueCodecFixtures.FixtureMode>("Menus_Only", ValueCodecFixtures.FixtureMode.MenusOnly)));
            Assert.Throws<ArgumentException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>(
                new EnumToken<ValueCodecFixtures.FixtureMode>("", ValueCodecFixtures.FixtureMode.MenusOnly)));
            Assert.Throws<ArgumentNullException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>(
                default(EnumToken<ValueCodecFixtures.FixtureMode>)));
            Assert.Throws<ArgumentException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>(
                new EnumToken<ValueCodecFixtures.FixtureMode>("Never", ValueCodecFixtures.FixtureMode.Never),
                new EnumToken<ValueCodecFixtures.FixtureMode>("NEVER", ValueCodecFixtures.FixtureMode.MenusOnly)));
            Assert.Throws<ArgumentException>(() => new EnumCodec<ValueCodecFixtures.FixtureMode>(
                new EnumToken<ValueCodecFixtures.FixtureMode>("Never", ValueCodecFixtures.FixtureMode.Never),
                new EnumToken<ValueCodecFixtures.FixtureMode>("Off", ValueCodecFixtures.FixtureMode.Never)));

            var tokens = new[]
            {
                new EnumToken<ValueCodecFixtures.FixtureMode>("Never", ValueCodecFixtures.FixtureMode.Never),
                new EnumToken<ValueCodecFixtures.FixtureMode>("MenusOnly", ValueCodecFixtures.FixtureMode.MenusOnly),
            };
            var codec = new EnumCodec<ValueCodecFixtures.FixtureMode>(tokens);
            tokens[1] = new EnumToken<ValueCodecFixtures.FixtureMode>("AllDialogue", ValueCodecFixtures.FixtureMode.AllDialogue);
            Assert.Equal(new[] { "Never", "MenusOnly" }, codec.Tokens.Select(t => t.Token));
            Assert.Equal("expected Never or MenusOnly", Error(codec, "x"));
        }

        [Fact]
        public void EqualityIsBitwiseAndOrdered()
        {
            Assert.False(new FloatCodec().Equal(0f, -0f));
            Assert.False(new DoubleCodec().Equal(0.0, -0.0));
            Assert.True(new FloatCodec().Equal(float.NaN, float.NaN));
            Assert.False(new ColorCodec().Equal(new[] { 0f, 0f, 0f, 1f }, new[] { -0f, 0f, 0f, 1f }));
            Assert.False(new Hex32ListCodec().Equal(new uint[] { 1, 2 }, new uint[] { 2, 1 }));
            Assert.True(new StringListCodec().Equal(new[] { "a", "b" }, new[] { "a", "b" }));
            Assert.False(new StringCodec().Equal("a", "A"));
        }

        // Kept out of the shared fixtures because .NET Framework disagrees with C++ on them (it
        // writes 1234.5678 and 3451485.3, and net35 reads 3e-324 as 0); this host runs .NET 8,
        // which agrees.
        [Fact]
        public void ModernDotNetMatchesCppWhereNetFrameworkDoesNot()
        {
            Assert.Equal("1234.5677", Encoding.ASCII.GetString(new FloatCodec().Render(1234.5677490234375f)));
            Assert.Equal("3451485.2", Encoding.ASCII.GetString(new FloatCodec().Render(3451485.25f)));
            Assert.True(new DoubleCodec().TryParse(B("3e-324"), out double denormal, out _));
            Assert.Equal(1L, BitConverter.DoubleToInt64Bits(denormal));
        }

        [Fact]
        public void AFloatAndTheDoubleHoldingItAreWrittenForTheirOwnType()
        {
            Assert.Equal("0.1", Encoding.ASCII.GetString(new FloatCodec().Render(0.1f)));
            Assert.Equal("0.10000000149011612", Encoding.ASCII.GetString(new DoubleCodec().Render(0.1f)));
            Assert.Equal("-0.0", Encoding.ASCII.GetString(new FloatCodec(0f, 1f).Render(-0f)));
        }
    }
}
