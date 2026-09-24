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
    /// Runs <see cref="IniEditorFixtures"/> on this test host's runtime, plus the API around
    /// <see cref="IniEditor"/>. The CameraUnlock.Core.FrameworkTests console runs the same
    /// fixtures on .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class IniEditorTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return IniEditorFixtures.Cases(Root()).Select(n => new object[] { n });
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            IniEditorFixtures.RunCase(Root(), name);
        }

        private static IniEditResult EditSample(params IniEdit[] edits)
        {
            return IniEditor.Edit(Encoding.ASCII.GetBytes("[General]\nA=1\n"), edits);
        }

        [Fact]
        public void ExactlyPrintableAsciiIsWritable()
        {
            for (int c = 0; c <= 0xFFFF; c++)
            {
                if (char.IsSurrogate((char)c)) continue;
                string ch = ((char)c).ToString();
                bool printable = c >= 0x20 && c <= 0x7E;
                Assert.True(Throws(new IniEdit("General", "A", "x" + ch + "x", true)) == !printable, "value U+" + c.ToString("X4"));
                Assert.True(Throws(new IniEdit("General", "K" + ch + "K", "1", true)) == (!printable || c == '='), "key U+" + c.ToString("X4"));
                Assert.True(Throws(new IniEdit("S" + ch + "S", "A", "1", true)) == (!printable || c == ']'), "section U+" + c.ToString("X4"));
            }
            Assert.True(Throws(new IniEdit("General", "A", "\ud800", true)));
            Assert.True(Throws(new IniEdit("General", "A\udc00", "1", true)));
        }

        private static bool Throws(IniEdit edit)
        {
            try
            {
                EditSample(edit);
            }
            catch (ArgumentException)
            {
                return true;
            }
            return false;
        }

        [Fact]
        public void TwoEditsOfOneKeyThrowWhateverTheirCase()
        {
            Assert.Throws<ArgumentException>(() => EditSample(
                new IniEdit("General", "A", "1", false),
                new IniEdit("general", "a", "2", false)));
            Assert.True(EditSample(new IniEdit("General", "A", "1", false), new IniEdit("Other", "A", "2", true)).Succeeded);
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
        public void NoEditsKeepsTheBytes()
        {
            byte[] original = new byte[] { 0xEF, 0xBB, 0xBF }
                .Concat(Encoding.ASCII.GetBytes("[General]\r\nA=1\nB = 2 ; c\r"))
                .Concat(new byte[] { 0x1A, 0x80 })
                .ToArray();
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
            Assert.Equal(new[] { 0, 1, 2, 3 }, ((IniEditRefusal[])Enum.GetValues(typeof(IniEditRefusal))).Select(v => (int)v));
            Assert.Equal(0, (int)IniEditRefusal.None);
            Assert.Equal(1, (int)IniEditRefusal.Utf16);
            Assert.Equal(2, (int)IniEditRefusal.NulByte);
            Assert.Equal(3, (int)IniEditRefusal.KeyNotFound);
        }
    }
}
