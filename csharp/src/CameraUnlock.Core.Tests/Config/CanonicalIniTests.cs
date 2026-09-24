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
    /// Runs <see cref="CanonicalIniFixtures"/> on this test host's runtime, plus the API
    /// around <see cref="CanonicalIni"/>. The CameraUnlock.Core.FrameworkTests console runs the
    /// same fixtures on .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class CanonicalIniTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> ReaderCases()
        {
            return CanonicalIniFixtures.ReaderCases(Root()).Select(n => new object[] { n });
        }

        [Theory]
        [MemberData(nameof(ReaderCases))]
        public void ReaderFixture(string name)
        {
            CanonicalIniFixtures.RunReaderCase(Root(), name);
        }

        [Fact]
        public void NumbersMatchTheCppTwin()
        {
            Assert.Equal(1, CanonicalIni.ConfigFormat);
            Assert.Equal(new[] { 0, 1, 2 }, ((CanonicalReadStatus[])Enum.GetValues(typeof(CanonicalReadStatus))).Select(v => (int)v));
            Assert.Equal(0, (int)CanonicalReadStatus.Readable);
            Assert.Equal(1, (int)CanonicalReadStatus.Utf16);
            Assert.Equal(2, (int)CanonicalReadStatus.NulByte);

            Assert.Equal(Enumerable.Range(1, 17), ((CanonicalDiagnosticKind[])Enum.GetValues(typeof(CanonicalDiagnosticKind))).Select(v => (int)v));
            Assert.Equal(1, (int)CanonicalDiagnosticKind.TextAfterSectionHeader);
            Assert.Equal(2, (int)CanonicalDiagnosticKind.UnclosedSectionHeader);
            Assert.Equal(3, (int)CanonicalDiagnosticKind.EmptySectionName);
            Assert.Equal(4, (int)CanonicalDiagnosticKind.EmptyKey);
            Assert.Equal(5, (int)CanonicalDiagnosticKind.MissingEquals);
            Assert.Equal(6, (int)CanonicalDiagnosticKind.KeyOutsideSection);
            Assert.Equal(7, (int)CanonicalDiagnosticKind.DuplicateKey);
            Assert.Equal(8, (int)CanonicalDiagnosticKind.ConfigFormatMissing);
            Assert.Equal(9, (int)CanonicalDiagnosticKind.ConfigFormatInvalid);
            Assert.Equal(10, (int)CanonicalDiagnosticKind.ConfigFormatNewer);
            Assert.Equal(11, (int)CanonicalDiagnosticKind.InvalidValue);
            Assert.Equal(12, (int)CanonicalDiagnosticKind.UnknownSection);
            Assert.Equal(13, (int)CanonicalDiagnosticKind.UnknownKey);
            Assert.Equal(14, (int)CanonicalDiagnosticKind.RetiredKey);
            Assert.Equal(15, (int)CanonicalDiagnosticKind.NonCanonicalConcept);
            Assert.Equal(16, (int)CanonicalDiagnosticKind.NoTrackingMode);
            Assert.Equal(17, (int)CanonicalDiagnosticKind.MisplacedKey);
        }

        [Fact]
        public void LookupFoldsAsciiCaseAndNothingElse()
        {
            CanonicalIni doc = CanonicalIni.Parse(Bytes(
                "[General]\r\nToggleKey=End\r\n[]\r\nA=1\r\n[Café]\r\nété=1\r\nToggleKey=Home\r\ntogglekey=Insert\r\n"));

            CanonicalValue? toggle = doc.Find("gEnErAl", "TOGGLEKEY");
            Assert.NotNull(toggle);
            Assert.Equal("End", Text(toggle!.Value));
            Assert.Equal(2, toggle.Line);
            Assert.Null(doc.Find("General", "Toggle"));
            Assert.Null(doc.Find("General", "ToggleKey "));
            Assert.Null(doc.FindSection(""));
            Assert.NotNull(doc.FindSection("CAFé"));
            Assert.Null(doc.FindSection("CAFÉ"));
            Assert.NotNull(doc.Find("Café", "été"));
            Assert.Null(doc.Find("Café", "Été"));

            CanonicalValue? last = doc.Find("café", "ToggleKey");
            Assert.NotNull(last);
            Assert.Equal("togglekey", Text(last!.Key));
            Assert.Equal("Insert", Text(last.Value));
            Assert.Equal(8, last.Line);
            Assert.Equal(new[] { 7 }, last.EarlierLines);

            CanonicalSection? general = doc.FindSection("general");
            Assert.NotNull(general);
            Assert.Equal("General", Text(general!.Name));
            Assert.Same(toggle, general.Find("togglekey"));
            Assert.Equal(2, doc.Sections.Count);
        }

        [Fact]
        public void LookupRejectsNamesThatHaveNoBytes()
        {
            CanonicalIni doc = CanonicalIni.Parse(Bytes("[General]\r\nA=1\r\n"));
            Assert.Throws<ArgumentNullException>(() => doc.FindSection(null!));
            Assert.Throws<ArgumentNullException>(() => doc.Find("General", null!));
            Assert.Throws<ArgumentException>(() => doc.FindSection("\ud800"));
            Assert.Throws<ArgumentException>(() => doc.Find("Missing", "\udc00"));
            Assert.Throws<ArgumentNullException>(() => CanonicalIni.Parse(null!));
            Assert.Throws<ArgumentNullException>(() => CanonicalIni.HasStamp(null!));
        }

        [Fact]
        public void UnreadableDocumentsInterpretNothing()
        {
            CanonicalIni utf16 = CanonicalIni.Parse(new byte[] { 0xFF, 0xFE, (byte)'[', 0, (byte)'G', 0, (byte)']', 0 });
            Assert.Equal(CanonicalReadStatus.Utf16, utf16.Status);
            Assert.False(utf16.IsReadable);
            Assert.Equal(0, utf16.FormatVersion);
            Assert.Equal(0, utf16.UnreadableLine);
            Assert.Empty(utf16.Sections);
            Assert.Empty(utf16.Diagnostics);

            CanonicalIni nul = CanonicalIni.Parse(Bytes("[General]\nA=1\n=\n\0"));
            Assert.Equal(CanonicalReadStatus.NulByte, nul.Status);
            Assert.Equal(4, nul.UnreadableLine);
            Assert.Equal(0, nul.FormatVersion);
            Assert.Empty(nul.Sections);
            Assert.Empty(nul.Diagnostics);

            CanonicalIni empty = CanonicalIni.Parse(new byte[0]);
            Assert.True(empty.IsReadable);
            Assert.Equal(1, empty.FormatVersion);
        }

        public static IEnumerable<object[]> DiagnosticCases()
        {
            yield return D("[General]  ; note\n", CanonicalDiagnosticKind.TextAfterSectionHeader, new[] { 1 }, "General", "", "; note",
                "Line 1: \"; note\" after [General] is ignored. Comments go on their own line.");
            yield return D("[General]\n [Position \n", CanonicalDiagnosticKind.UnclosedSectionHeader, new[] { 2 }, "", "", "[Position",
                "Line 2: \"[Position\" has no closing ], so the settings below it are ignored up to the next section header.");
            yield return D("[ ]\n", CanonicalDiagnosticKind.EmptySectionName, new[] { 1 }, "", "", "[ ]",
                "Line 1: \"[ ]\" names no section, so the settings below it are ignored up to the next section header.");
            yield return D("[General]\n = 5\n", CanonicalDiagnosticKind.EmptyKey, new[] { 2 }, "", "", "= 5",
                "Line 2: \"= 5\" has no setting name before the =, so it is ignored.");
            yield return D("[General]\n\tJust text\t\n", CanonicalDiagnosticKind.MissingEquals, new[] { 2 }, "", "", "Just text",
                "Line 2: \"Just text\" is not a setting (it has no =), so it is ignored.");
            yield return D("ToggleKey = End\n", CanonicalDiagnosticKind.KeyOutsideSection, new[] { 1 }, "", "ToggleKey", "End",
                "Line 1: ToggleKey is not under a section header, so it is ignored.");
            yield return D("[General]\nA=1\nA=2\n[general]\na=3\n", CanonicalDiagnosticKind.DuplicateKey, new[] { 2, 3, 5 }, "General", "a", "3",
                "[General] a is set on lines 2, 3 and 5. Line 5 is used.");
            yield return D("[General]\nA=1\n[General]\nA=2\n", CanonicalDiagnosticKind.DuplicateKey, new[] { 2, 4 }, "General", "A", "2",
                "[General] A is set on lines 2 and 4. Line 4 is used.");
            yield return D("; x\n[cameraUnlock]\n", CanonicalDiagnosticKind.ConfigFormatMissing, new[] { 2 }, "cameraUnlock", "", "",
                "Line 2: [cameraUnlock] has no ConfigFormat, so the file is read as format 1.");
            yield return D("[CameraUnlock]\nconfigformat = v1\n", CanonicalDiagnosticKind.ConfigFormatInvalid, new[] { 2 }, "CameraUnlock",
                "configformat", "v1", "Line 2: configformat=v1 is not a format number, so the file is read as format 1.");
            yield return D("[CameraUnlock]\nConfigFormat=7\n", CanonicalDiagnosticKind.ConfigFormatNewer, new[] { 2 }, "CameraUnlock",
                "ConfigFormat", "7", "Line 2: ConfigFormat=7 was written by a newer version of the mod. This version reads format 1.");
        }

        [Theory]
        [MemberData(nameof(DiagnosticCases))]
        public void DiagnosticsCarryTheirFieldsAndASentence(string input, CanonicalDiagnosticKind kind, int[] lines, string section,
            string key, string value, string sentence)
        {
            CanonicalDiagnostic d = CanonicalIni.Parse(Bytes(input)).Diagnostics.First(x => x.Kind == kind);
            Assert.Equal(lines, d.Lines);
            Assert.Equal(section, Text(d.Section));
            Assert.Equal(key, Text(d.Key));
            Assert.Equal(value, Text(d.Value));
            Assert.Equal(sentence, d.Describe());
        }

        [Fact]
        public void ANewerFormatIsKeptAsRead()
        {
            Assert.Equal(7, CanonicalIni.Parse(Bytes("[CameraUnlock]\nConfigFormat=7\n")).FormatVersion);
        }

        [Fact]
        public void DescribeShowsBytesThatAreNotUtf8AsReplacementCharacters()
        {
            CanonicalDiagnostic d = CanonicalIni.Parse(new byte[] { (byte)'K', 0xE9, (byte)'=', (byte)'1' }).Diagnostics.Single();
            Assert.Equal("Line 1: K� is not under a section header, so it is ignored.", d.Describe());
        }

        [Fact]
        public void TheStampIsASectionHeader()
        {
            Assert.True(CanonicalIni.HasStamp(Bytes("[General]\n[CameraUnlock]\n")));
            Assert.False(CanonicalIni.HasStamp(Bytes("[CameraUnlocked]\n")));
            Assert.False(CanonicalIni.HasStamp(Bytes("CameraUnlock=1\n")));
            Assert.True(CanonicalIni.HasStamp(Bytes("[CameraUnlock]\n\0\0")));
            byte[] le = new byte[] { 0xFF, 0xFE }.Concat(Encoding.Unicode.GetBytes("[CameraUnlock]")).ToArray();
            Assert.True(CanonicalIni.HasStamp(le));
            byte[] leUnderBeMark = new byte[] { 0xFE, 0xFF }.Concat(Encoding.Unicode.GetBytes("[CameraUnlock]")).ToArray();
            Assert.False(CanonicalIni.HasStamp(leUnderBeMark));
            Assert.False(CanonicalIni.HasStamp(new byte[] { 0xFF, 0xFE }));
        }

        private static object[] D(string input, CanonicalDiagnosticKind kind, int[] lines, string section, string key, string value,
            string sentence)
        {
            return new object[] { input, kind, lines, section, key, value, sentence };
        }

        private static byte[] Bytes(string text)
        {
            return Encoding.UTF8.GetBytes(text);
        }

        private static string Text(byte[] bytes)
        {
            return Encoding.UTF8.GetString(bytes);
        }
    }
}
