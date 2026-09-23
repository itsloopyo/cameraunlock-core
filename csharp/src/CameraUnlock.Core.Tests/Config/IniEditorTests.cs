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
    /// <see cref="IniEditor"/> against the byte fixtures in data/fixtures/ini-editor, which
    /// cpp/tests/ini_editor_tests.cpp runs through the C++ <c>EditIni</c> as well, so the
    /// two implementations are held to the same bytes. Each edited document is also read
    /// back through <see cref="ConfigParsingUtils.ParseIniFile"/>: the edited keys carry
    /// their new values and every other key reads exactly as before.
    /// </summary>
    public class IniEditorTests
    {
        private static string FixtureRoot([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo? dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile)!);
            while (dir != null && !Directory.Exists(Path.Combine(dir.FullName, "data", "fixtures", "ini-editor")))
            {
                dir = dir.Parent;
            }
            if (dir == null)
            {
                throw new InvalidOperationException("no data/fixtures/ini-editor above " + sourceFile);
            }
            return Path.Combine(dir.FullName, "data", "fixtures", "ini-editor");
        }

        public static IEnumerable<object[]> Cases()
        {
            string[] names = Directory.GetDirectories(FixtureRoot())
                .Select(d => Path.GetFileName(d))
                .OrderBy(n => n, StringComparer.Ordinal)
                .ToArray();
            if (names.Length == 0) throw new InvalidOperationException("no fixtures under " + FixtureRoot());
            return names.Select(n => new object[] { n });
        }

        private sealed class FixtureCase
        {
            public byte[] Input = new byte[0];
            public readonly List<IniEdit> Edits = new List<IniEdit>();
            public readonly Dictionary<string, string> Reads = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            public readonly HashSet<string> FlatDuplicates = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            public string[]? Refused;
            public byte[]? Expected;
        }

        private static FixtureCase Load(string name)
        {
            string dir = Path.Combine(FixtureRoot(), name);
            var c = new FixtureCase();
            string input = Path.Combine(dir, "input.ini");
            if (File.Exists(input)) c.Input = File.ReadAllBytes(input);

            string tsv = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(Path.Combine(dir, "case.tsv")));
            foreach (string line in tsv.Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string directive = line.Split('\t')[0];
                switch (directive)
                {
                    case "set":
                    case "set_or_insert":
                        string[] e = line.Split(new[] { '\t' }, 4);
                        Assert.Equal(4, e.Length);
                        c.Edits.Add(new IniEdit(e[1], e[2], e[3], directive == "set_or_insert"));
                        break;
                    case "reads":
                        string[] r = line.Split(new[] { '\t' }, 3);
                        c.Reads[r[1]] = r[2];
                        break;
                    case "flat_duplicate":
                        c.FlatDuplicates.Add(line.Split('\t')[1]);
                        break;
                    case "refused":
                        c.Refused = line.Split(new[] { '\t' }, 5);
                        Assert.Equal(5, c.Refused.Length);
                        break;
                    default:
                        throw new InvalidOperationException(name + ": unknown directive '" + directive + "'");
                }
            }
            if (c.Refused == null) c.Expected = File.ReadAllBytes(Path.Combine(dir, "expected.ini"));
            return c;
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            FixtureCase c = Load(name);
            IniEditResult result = IniEditor.Edit(c.Input, c.Edits);

            if (c.Refused != null)
            {
                Assert.False(result.Succeeded);
                Assert.Equal(c.Refused[1], result.Refusal.ToString());
                Assert.Equal(c.Refused[2], result.Section ?? "");
                Assert.Equal(c.Refused[3], result.Key ?? "");
                int[] lines = c.Refused[4].Length == 0
                    ? new int[0]
                    : c.Refused[4].Split(',').Select(int.Parse).ToArray();
                Assert.Equal(lines, result.Lines.ToArray());
                Assert.Throws<InvalidOperationException>(() => result.Bytes);
                return;
            }

            Assert.True(result.Succeeded, name + " was refused: " + result.Refusal);
            Assert.Equal(c.Expected, result.Bytes);
            AssertReadsBack(c, result.Bytes);
        }

        private static void AssertReadsBack(FixtureCase c, byte[] output)
        {
            string inputPath = Path.GetTempFileName();
            string outputPath = Path.GetTempFileName();
            try
            {
                File.WriteAllBytes(inputPath, c.Input);
                File.WriteAllBytes(outputPath, output);

                if (c.FlatDuplicates.Count > 0)
                {
                    // The flat reader refuses a key it sees twice, in any sections, so it
                    // cannot read this document back at all. That is the reader's answer, and
                    // the C++ half checks the values.
                    FormatException ex = Assert.Throws<FormatException>(() => ConfigParsingUtils.ParseIniFile(outputPath));
                    Assert.Contains(c.FlatDuplicates, k => ex.Message.IndexOf("'" + k + "'", StringComparison.OrdinalIgnoreCase) >= 0);
                    return;
                }

                Dictionary<string, string> before = ConfigParsingUtils.ParseIniFile(inputPath);
                Dictionary<string, string> after = ConfigParsingUtils.ParseIniFile(outputPath);

                var edited = new HashSet<string>(c.Edits.Select(e => e.Key), StringComparer.OrdinalIgnoreCase);
                foreach (KeyValuePair<string, string> pair in before)
                {
                    if (edited.Contains(pair.Key)) continue;
                    Assert.True(after.ContainsKey(pair.Key), pair.Key + " is missing after the edit");
                    Assert.Equal(pair.Value, after[pair.Key]);
                }
                foreach (string key in after.Keys)
                {
                    Assert.True(before.ContainsKey(key) || edited.Contains(key), key + " appeared from nowhere");
                }
                foreach (IniEdit edit in c.Edits)
                {
                    string expected = c.Reads.ContainsKey(edit.Key) ? c.Reads[edit.Key] : edit.Value;
                    Assert.True(after.ContainsKey(edit.Key), edit.Key + " does not read back");
                    Assert.Equal(expected, after[edit.Key]);
                }
            }
            finally
            {
                File.Delete(inputPath);
                File.Delete(outputPath);
            }
        }

        [Fact]
        public void FixturesAreFound()
        {
            Assert.NotEmpty(Cases());
        }

        private static IniEditResult EditSample(params IniEdit[] edits)
        {
            return IniEditor.Edit(Encoding.ASCII.GetBytes("[General]\nA=1\n"), edits);
        }

        [Theory]
        [InlineData("", "A", "1")]
        [InlineData("General", "", "1")]
        [InlineData(" General", "A", "1")]
        [InlineData("General", "A\t", "1")]
        [InlineData("Gen]eral", "A", "1")]
        [InlineData("General", "A=B", "1")]
        [InlineData("General", ";A", "1")]
        [InlineData("General", "#A", "1")]
        [InlineData("General", "[A", "1")]
        [InlineData("General", "A", "1\n2")]
        [InlineData("General", "A", "1\r")]
        [InlineData("General", "A", "1\02")]
        public void UnwritableEditThrows(string section, string key, string value)
        {
            Assert.Throws<ArgumentException>(() => EditSample(new IniEdit(section, key, value, true)));
        }

        [Fact]
        public void UnpairedSurrogateThrows()
        {
            Assert.Throws<ArgumentException>(() => EditSample(new IniEdit("General", "A", "\ud800", true)));
            Assert.Throws<ArgumentException>(() => EditSample(new IniEdit("Gen\udc00", "A", "1", true)));
        }

        [Fact]
        public void TwoEditsOfOneKeyThrowWhateverTheirCase()
        {
            Assert.Throws<ArgumentException>(() => EditSample(
                new IniEdit("General", "A", "1", false),
                new IniEdit("general", "a", "2", false)));
        }

        [Fact]
        public void NullArgumentsThrow()
        {
            Assert.Throws<ArgumentNullException>(() => IniEditor.Edit(null!, new IniEdit[0]));
            Assert.Throws<ArgumentNullException>(() => IniEditor.Edit(new byte[0], null!));
            Assert.Throws<ArgumentException>(() => IniEditor.Edit(new byte[0], new IniEdit[] { null! }));
            Assert.Throws<ArgumentNullException>(() => new IniEdit(null!, "A", "1", false));
            Assert.Throws<ArgumentNullException>(() => new IniEdit("General", null!, "1", false));
            Assert.Throws<ArgumentNullException>(() => new IniEdit("General", "A", null!, false));
        }

        [Fact]
        public void EmptyValueIsWritable()
        {
            IniEditResult result = EditSample(new IniEdit("General", "A", "", false));
            Assert.Equal(Encoding.ASCII.GetBytes("[General]\nA=\n"), result.Bytes);
        }

        [Fact]
        public void NoEditsKeepsTheBytes()
        {
            byte[] original = new byte[] { 0xEF, 0xBB, 0xBF }.Concat(Encoding.ASCII.GetBytes("[General]\r\nA=1\nB = 2 ; c")).ToArray();
            IniEditResult result = IniEditor.Edit(original, new IniEdit[0]);
            Assert.True(result.Succeeded);
            Assert.Equal(original, result.Bytes);
            Assert.Null(result.Section);
            Assert.Null(result.Key);
            Assert.Empty(result.Lines);
            Assert.Equal(IniEditRefusal.Utf16, IniEditor.Edit(new byte[] { 0xFF, 0xFE }, new IniEdit[0]).Refusal);
        }

        [Fact]
        public void TheInputIsNotModified()
        {
            byte[] original = Encoding.ASCII.GetBytes("[General]\nA=1\n");
            byte[] copy = (byte[])original.Clone();
            IniEditor.Edit(original, new[] { new IniEdit("General", "A", "2", false), new IniEdit("General", "B", "3", true) });
            Assert.Equal(copy, original);
        }

        // The C++ enum carries the same numbers. Consumers compile these constants in.
        [Fact]
        public void RefusalValuesArePinned()
        {
            Assert.Equal(0, (int)IniEditRefusal.None);
            Assert.Equal(1, (int)IniEditRefusal.Utf16);
            Assert.Equal(2, (int)IniEditRefusal.InvalidUtf8);
            Assert.Equal(3, (int)IniEditRefusal.NulByte);
            Assert.Equal(4, (int)IniEditRefusal.LoneCarriageReturn);
            Assert.Equal(5, (int)IniEditRefusal.DuplicateSection);
            Assert.Equal(6, (int)IniEditRefusal.DuplicateKey);
            Assert.Equal(7, (int)IniEditRefusal.KeyNotFound);
        }
    }
}
