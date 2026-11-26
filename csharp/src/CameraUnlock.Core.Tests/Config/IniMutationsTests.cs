using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Config.Testing;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="IniMutationFixtures"/> on this test host's runtime, plus the argument checks.
    /// The CameraUnlock.Core.FrameworkTests console runs the same fixtures on .NET Framework 3.5 and
    /// 4.7.2 (pixi run test-framework).
    /// </summary>
    public class IniMutationsTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return IniMutationFixtures.Cases(Root()).Select(c => new object[] { c });
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void GivesTheFixtureOutputs(string name)
        {
            IniMutationFixtures.RunCase(Root(), name);
        }

        [Fact]
        public void Sha256MatchesKnownDigests()
        {
            Assert.Equal("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", IniMutationFixtures.Sha256(new byte[0]));
            Assert.Equal("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                IniMutationFixtures.Sha256(Encoding.ASCII.GetBytes("abc")));
        }

        private static readonly byte[] Base = Encoding.ASCII.GetBytes("[General]\nToggleKey=0x23\n");

        private static MutationKey Key(string section, string key, string alternate = "1", string[]? outOfRange = null,
            ChordSwitch[]? chords = null)
        {
            return new MutationKey(section, key, alternate, outOfRange ?? new string[0], chords != null, chords ?? new ChordSwitch[0]);
        }

        private static List<IniMutation> Generate(byte[] input, params MutationKey[] keys)
        {
            return IniMutations.Generate(input, IniMutationFixtures.Reads(keys), keys);
        }

        private static string Refusal(params MutationKey[] keys)
        {
            return Assert.Throws<ArgumentException>(() => Generate(Base, keys)).Message;
        }

        private static string Refusal(LegacyKey[] reads, params MutationKey[] keys)
        {
            return Assert.Throws<ArgumentException>(() => IniMutations.Generate(Base, reads, keys)).Message;
        }

        [Fact]
        public void RefusesBadKeys()
        {
            Assert.StartsWith("the corpus needs at least one key", Refusal());
            Assert.StartsWith("[general] togglekey is listed twice, or once in a section and once without one",
                Refusal(Key("General", "ToggleKey"), Key("general", "togglekey")));
            Assert.StartsWith("togglekey is listed twice, or once in a section and once without one",
                Refusal(Key("General", "ToggleKey"), Key("", "togglekey")));
            Refusal(Key("General", ""));
            Refusal(Key(" General", "ToggleKey"));
            Refusal(Key("General", "ToggleKey "));
            Refusal(Key("Gen]eral", "ToggleKey"));
            Refusal(Key("General", "Toggle=Key"));
            Refusal(Key("General", ";ToggleKey"));
            Refusal(Key("General", "#ToggleKey"));
            Refusal(Key("General", "[ToggleKey"));
            Refusal(Key("General", "ToggleKey", "1\t2"));
            Refusal(Key("General", "ToggleKey", "caf" + (char)0xE9));
            Refusal(Key("General", "ToggleKey", "1", new[] { "\u007f" }));
            Refusal(Key("General", "ToggleKey", "1", null, new[] { new ChordSwitch("Hotkeys", "Chord=", "1", "0") }));
            Refusal(Key("General", "ToggleKey", "1", null, new[] { new ChordSwitch("Hotkeys", "Chord", "1", "\n") }));
            Assert.Throws<ArgumentNullException>(() => IniMutations.Generate(Base, new LegacyKey[0], new MutationKey[] { null! }));
            Assert.Throws<ArgumentNullException>(() => Generate(null!, Key("General", "ToggleKey")));
            Assert.Throws<ArgumentNullException>(() => IniMutations.Generate(Base, null!, new[] { Key("General", "ToggleKey") }));
            Assert.Throws<ArgumentNullException>(() =>
                IniMutations.Generate(Base, new LegacyKey[] { null! }, new[] { Key("General", "ToggleKey") }));
        }

        [Fact]
        public void TakesTheImportsKeysAndRefusesAMismatch()
        {
            MutationKey toggle = Key("General", "ToggleKey");
            Assert.StartsWith("[Position] LimitY is not among the keys the import reads",
                Refusal(new[] { new LegacyKey("General", "ToggleKey") }, toggle, Key("Position", "LimitY")));
            Assert.StartsWith("Verbose is read by the import and has no key descriptor",
                Refusal(new[] { new LegacyKey("General", "ToggleKey"), new LegacyKey("", "Verbose") }, toggle));
            Assert.StartsWith("ToggleKey is not among the keys the import reads",
                Refusal(new[] { new LegacyKey("General", "ToggleKey") }, Key("", "ToggleKey")));
            Assert.StartsWith("[general] TOGGLEKEY is read twice",
                Refusal(new[] { new LegacyKey("General", "ToggleKey"), new LegacyKey("general", "TOGGLEKEY") }, toggle));
            Assert.NotEmpty(IniMutations.Generate(Base, new[] { new LegacyKey("GENERAL", "togglekey") }, new[] { toggle }));
            Assert.NotEmpty(Generate(Base, Key("", "ToggleKey")));
        }

        [Fact]
        public void RefusesAFirstKeyTooLongForThe199CharacterLine()
        {
            byte[] input = Encoding.ASCII.GetBytes("[General]\nToggleKey=" + new string('x', 190) + "\n");
            var error = Assert.Throws<ArgumentException>(() => Generate(input, Key("General", "ToggleKey")));
            Assert.StartsWith("[General] ToggleKey=" + new string('x', 190) + " is longer than 199 characters", error.Message);
        }

        private static byte[] Utf16Of(byte[] comment)
        {
            byte[] input = Encoding.ASCII.GetBytes("; ").Concat(comment).Concat(Encoding.ASCII.GetBytes("\n[Main]\nK=1\n")).ToArray();
            return Generate(input, Key("Main", "K", "2")).Single(m => m.Name == "file: UTF-16 LE with a mark").Bytes;
        }

        private static byte[] Utf16Expected(params int[] commentUnits)
        {
            var units = new List<int> { ';', ' ' };
            units.AddRange(commentUnits);
            units.AddRange("\n[Main]\nK=1\n".Select(c => (int)c));
            return new byte[] { 0xFF, 0xFE }.Concat(units.SelectMany(u => new[] { (byte)(u & 0xFF), (byte)(u >> 8) })).ToArray();
        }

        [Fact]
        public void TheUtf16OutputDecodesUtf8OrElseCodePage1252()
        {
            Assert.Equal(Utf16Expected(0x20AC), Utf16Of(new byte[] { 0xE2, 0x82, 0xAC }));
            Assert.Equal(Utf16Expected(0xD83C, 0xDFAE), Utf16Of(new byte[] { 0xF0, 0x9F, 0x8E, 0xAE }));
            Assert.Equal(Utf16Expected(0x00C0, 0x00AF), Utf16Of(new byte[] { 0xC0, 0xAF }));
            Assert.Equal(Utf16Expected(0x00ED, 0x00A0, 0x20AC), Utf16Of(new byte[] { 0xED, 0xA0, 0x80 }));
            Assert.Equal(Utf16Expected(0x00F4, 0x0090, 0x20AC, 0x20AC), Utf16Of(new byte[] { 0xF4, 0x90, 0x80, 0x80 }));
            Assert.Equal(Utf16Expected(0x00E2, 0x201A), Utf16Of(new byte[] { 0xE2, 0x82 }));

            // What .NET Framework's Encoding.GetEncoding(1252), which is Windows' table, gives for 0x80-0x9F.
            int[] windows1252 =
            {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
            };
            byte[] high = Enumerable.Range(0x80, 0x80).Select(b => (byte)b).ToArray();
            int[] units = high.Select(b => b <= 0x9F ? windows1252[b - 0x80] : b).ToArray();
            Assert.Equal(Utf16Expected(units), Utf16Of(high));
        }

        [Fact]
        public void AnEmptyBaseGainsTheKeyInANewSection()
        {
            List<IniMutation> outputs = Generate(new byte[0], Key("General", "Enabled", "false"));
            Assert.Equal(62, outputs.Count);
            Assert.Equal("[General] Enabled: removed", outputs[0].Name);
            Assert.Equal("[General]\r\n", Encoding.ASCII.GetString(outputs[0].Bytes));
            Assert.Equal("file: cp1252 byte in a value", outputs[outputs.Count - 1].Name);
            Assert.Equal(Encoding.ASCII.GetBytes("[General]\r\nEnabled=false").Concat(new byte[] { 0xE9, 0x0D, 0x0A }),
                outputs[outputs.Count - 1].Bytes);
        }

        [Fact]
        public void AnEmptyBaseGainsASectionLessKeyAndNoHeader()
        {
            List<IniMutation> outputs = Generate(new byte[0], Key("", "Enabled", "false"));
            Assert.Equal(55, outputs.Count);
            Assert.Equal("Enabled: removed", outputs[0].Name);
            Assert.Empty(outputs[0].Bytes);
            Assert.Equal(Encoding.ASCII.GetBytes("Enabled=false").Concat(new byte[] { 0xE9, 0x0D, 0x0A }),
                outputs[outputs.Count - 1].Bytes);
        }
    }
}
