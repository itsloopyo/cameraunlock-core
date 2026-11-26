using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using CameraUnlock.Core.Input;
using CameraUnlock.Core.Unity.Extensions;
using UnityEngine;
using Xunit;

namespace CameraUnlock.Core.Unity.Tests
{
    /// KeyBindingInput decides from Input.GetKeyDown and Input.GetKey alone, so these drive the
    /// fake Input the way a frame would: which keys went down, which are held.
    public class KeyBindingInputTests : IDisposable
    {
        public KeyBindingInputTests()
        {
            UnityEngine.Input.Reset();
        }

        public void Dispose()
        {
            UnityEngine.Input.Reset();
        }

        private static KeyBinding[] Read(string value)
        {
            Assert.True(KeyBindings.TryParse(value, out KeyBinding[] bindings, out string error), error);
            return bindings;
        }

        private static void Frame(KeyCode down, params KeyCode[] held)
        {
            UnityEngine.Input.Reset();
            UnityEngine.Input.Down.Add(down);
            UnityEngine.Input.Held.Add(down);
            foreach (KeyCode key in held) UnityEngine.Input.Held.Add(key);
        }

        [Fact]
        public void APlainKeyFiresOnItsDownFrame()
        {
            Frame(KeyCode.End);
            Assert.True(KeyBindingInput.IsTriggered(Read("End")));
        }

        [Fact]
        public void AHeldKeyDoesNotFireAgain()
        {
            UnityEngine.Input.Held.Add(KeyCode.End);
            Assert.False(KeyBindingInput.IsTriggered(Read("End")));
        }

        [Fact]
        public void APlainKeyFiresWithOneModifierHeld()
        {
            Frame(KeyCode.End, KeyCode.LeftControl);
            Assert.True(KeyBindingInput.IsTriggered(Read("End")));
            Frame(KeyCode.End, KeyCode.RightShift);
            Assert.True(KeyBindingInput.IsTriggered(Read("End")));
            Frame(KeyCode.End, KeyCode.LeftAlt);
            Assert.True(KeyBindingInput.IsTriggered(Read("End")));
        }

        [Fact]
        public void APlainKeyDoesNotFireWhileCtrlAndShiftAreHeld()
        {
            Frame(KeyCode.End, KeyCode.RightControl, KeyCode.LeftShift);
            Assert.False(KeyBindingInput.IsTriggered(Read("End")));
        }

        [Fact]
        public void AChordFiresWithEitherSideOfEachModifier()
        {
            Frame(KeyCode.Y, KeyCode.LeftControl, KeyCode.LeftShift);
            Assert.True(KeyBindingInput.IsTriggered(Read("Ctrl+Shift+Y")));
            Frame(KeyCode.Y, KeyCode.RightControl, KeyCode.RightShift);
            Assert.True(KeyBindingInput.IsTriggered(Read("Ctrl+Shift+Y")));
            Frame(KeyCode.Y, KeyCode.RightControl, KeyCode.LeftShift, KeyCode.RightAlt);
            Assert.True(KeyBindingInput.IsTriggered(Read("Ctrl+Shift+Y")));
        }

        [Fact]
        public void AChordNeedsEveryModifierItNames()
        {
            Frame(KeyCode.Y, KeyCode.LeftControl);
            Assert.False(KeyBindingInput.IsTriggered(Read("Ctrl+Shift+Y")));
            Frame(KeyCode.Y);
            Assert.False(KeyBindingInput.IsTriggered(Read("Ctrl+Shift+Y")));
            Frame(KeyCode.End, KeyCode.LeftControl);
            Assert.False(KeyBindingInput.IsTriggered(Read("Alt+End")));
            Frame(KeyCode.End, KeyCode.RightAlt);
            Assert.True(KeyBindingInput.IsTriggered(Read("Alt+End")));
        }

        [Fact]
        public void EveryItemOfAListIsABinding()
        {
            KeyBinding[] toggle = Read("End, Ctrl+Shift+Y");
            Frame(KeyCode.End);
            Assert.True(KeyBindingInput.IsTriggered(toggle));
            Frame(KeyCode.Y, KeyCode.LeftControl, KeyCode.LeftShift);
            Assert.True(KeyBindingInput.IsTriggered(toggle));
            Frame(KeyCode.End, KeyCode.LeftControl, KeyCode.LeftShift);
            Assert.False(KeyBindingInput.IsTriggered(toggle));
            Frame(KeyCode.Y);
            Assert.False(KeyBindingInput.IsTriggered(toggle));
        }

        [Fact]
        public void AnEmptyListNeverFires()
        {
            Frame(KeyCode.End, KeyCode.LeftControl);
            Assert.False(KeyBindingInput.IsTriggered(Read("")));
        }

        [Fact]
        public void NullIsRefused()
        {
            Assert.Throws<ArgumentNullException>(() => KeyBindingInput.IsTriggered(null));
        }

        /// The decision reads keys through the delegates only: the key of each binding as
        /// went-down, the six modifier keys as held.
        [Fact]
        public void TheDecisionAsksWentDownForKeysAndHeldForModifiers()
        {
            var askedDown = new List<int>();
            var askedHeld = new List<int>();
            bool fired = KeyBindingInput.IsTriggered(
                Read("Ctrl+Y"),
                code => { askedDown.Add(code); return true; },
                code => { askedHeld.Add(code); return code == (int)KeyCode.RightControl; });

            Assert.True(fired);
            Assert.Equal(new[] { (int)KeyCode.Y }, askedDown);
            Assert.Contains((int)KeyCode.LeftControl, askedHeld);
            Assert.Contains((int)KeyCode.RightControl, askedHeld);
            Assert.Contains((int)KeyCode.LeftAlt, askedHeld);
        }
    }

    /// The fake KeyCode members carry the reference stub's values, so a binding read from the
    /// key table polls the key it names here too.
    public class KeyCodeFakeTests
    {
        private static string ReferenceStub([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile));
            while (dir != null && !File.Exists(Path.Combine(dir.FullName, "csharp", "stubs", "UnityStubs.cs")))
            {
                dir = dir.Parent;
            }
            if (dir == null) throw new InvalidOperationException("no csharp/stubs/UnityStubs.cs above " + sourceFile);
            return File.ReadAllText(Path.Combine(dir.FullName, "csharp", "stubs", "UnityStubs.cs"));
        }

        [Fact]
        public void EveryFakeMemberHasTheReferenceValue()
        {
            string body = Regex.Match(ReferenceStub(), @"\benum\s+KeyCode\s*\{(?<body>[^}]*)\}").Groups["body"].Value;
            foreach (KeyCode member in Enum.GetValues(typeof(KeyCode)))
            {
                Match declared = Regex.Match(body, @"\b" + member + @"\s*=\s*(?<value>\d+)");
                Assert.True(declared.Success, "the reference stub declares no KeyCode." + member);
                Assert.Equal(int.Parse(declared.Groups["value"].Value), (int)member);
            }
        }
    }
}
