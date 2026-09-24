using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using CameraUnlock.Core.Input;
using CameraUnlock.Core.Tests.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Input
{
    /// <summary>
    /// Runs <see cref="KeyBindingFixtures"/> on this test host's runtime, plus the API around
    /// <see cref="KeyBindings"/>. The CameraUnlock.Core.FrameworkTests console runs the same
    /// rows on .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class KeyBindingsTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> UnityRows()
        {
            return KeyBindingFixtures.UnityRows(Root()).Select(r => new object[] { r });
        }

        [Theory]
        [MemberData(nameof(UnityRows))]
        public void Fixture(string row)
        {
            KeyBindingFixtures.RunRow(row);
        }

        [Fact]
        public void NumbersMatchTheCppTwin()
        {
            Assert.Equal(0, (int)KeyModifiers.None);
            Assert.Equal(1, (int)KeyModifiers.Ctrl);
            Assert.Equal(2, (int)KeyModifiers.Shift);
            Assert.Equal(4, (int)KeyModifiers.Alt);
            Assert.Equal(new[] { "Ctrl", "Shift", "Alt" }, KeyNames.Modifiers.Select(m => m.Name));
        }

        [Fact]
        public void ReadsCodesAndModifiersAsWritten()
        {
            Assert.True(KeyBindings.TryParse("End, Shift+Ctrl+Y, Alt+Mouse0", out KeyBinding[] bindings, out string? error));
            Assert.Null(error);
            Assert.Equal(
                new[]
                {
                    new KeyBinding(KeyModifiers.None, 279),
                    new KeyBinding(KeyModifiers.Ctrl | KeyModifiers.Shift, 121),
                    new KeyBinding(KeyModifiers.Alt, 323),
                },
                bindings);
        }

        [Theory]
        [InlineData("End,,Home", "is empty")]
        [InlineData("Ctrl+Ctrl+End", "names Ctrl twice")]
        [InlineData("Ctrl+Shift", "has no key")]
        [InlineData("End+Ctrl", "Ctrl, Shift or Alt before each '+'")]
        [InlineData("Page Up", "is not a key name")]
        [InlineData("0x23", "is a key code")]
        [InlineData("End, end", "is listed twice")]
        public void AnErrorNamesTheExpectation(string input, string says)
        {
            Assert.False(KeyBindings.TryParse(input, out KeyBinding[] bindings, out string? error));
            Assert.Empty(bindings);
            Assert.Contains(says, error);
            Assert.Contains("expected", error);
        }

        [Fact]
        public void NullIsRefused()
        {
            Assert.Throws<ArgumentNullException>(() => KeyBindings.TryParse(null!, out _, out _));
            Assert.Throws<ArgumentNullException>(() => KeyBindings.Format(null!));
        }

        [Fact]
        public void FormatRefusesWhatCannotReadBack()
        {
            Assert.Equal("", KeyBindings.Format(new KeyBinding[0]));
            Assert.Equal("Ctrl+Alt+F1, End",
                KeyBindings.Format(new[] { new KeyBinding(KeyModifiers.Alt | KeyModifiers.Ctrl, 282), new KeyBinding(KeyModifiers.None, 279) }));
            Assert.Throws<ArgumentException>(() => KeyBindings.Format(new[] { new KeyBinding(KeyModifiers.None, 999) }));
            Assert.Throws<ArgumentException>(() => KeyBindings.Format(new[] { new KeyBinding(KeyModifiers.None, 0) }));
            Assert.Throws<ArgumentException>(() => KeyBindings.Format(new[] { new KeyBinding(KeyModifiers.None, 279), new KeyBinding(KeyModifiers.None, 279) }));
        }

        [Fact]
        public void ABindingHoldsOnlyModifierFlags()
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => new KeyBinding((KeyModifiers)8, 279));
            Assert.Equal(new KeyBinding(KeyModifiers.Ctrl, 279), new KeyBinding(KeyModifiers.Ctrl, 279));
            Assert.NotEqual(new KeyBinding(KeyModifiers.Ctrl, 279), new KeyBinding(KeyModifiers.Shift, 279));
        }
    }
}
