using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using CameraUnlock.Core.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="DefaultsIniFixtures"/> on this test host's runtime, and holds the key names
    /// Defaults.ini takes to data/keys.json. The CameraUnlock.Core.FrameworkTests console runs the
    /// same fixtures on .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class DefaultsIniTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return DefaultsIniFixtures.Cases(Root()).Select(c => new object[] { c });
        }

        [Fact]
        public void TheGlobalTableRendersAsDefaultsIniAndReadsBackAsItsDefaults()
        {
            DefaultsIniFixtures.RunRender(Root());
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            DefaultsIniFixtures.RunCase(Root(), name);
        }

        [Fact]
        public void TheLineFunctionsRefuseWhatIsNotRefused()
        {
            DefaultsIniFixtures.RunLineArguments();
        }

        [Fact]
        public void TheHeaderListsExactlyTheKeyNamesWithAVirtualKeyCodeButCtrlShiftAndAltKeys()
        {
            List<string> listed = DefaultsIniFixtures.HeaderKeyNames(DefaultsIni.Render());
            Assert.Equal(98, listed.Count);
            Assert.Equal(listed.Count, listed.Distinct().Count());
            Assert.Equal(KeyNames(true).OrderBy(n => n, System.StringComparer.Ordinal),
                listed.OrderBy(n => n, System.StringComparer.Ordinal));
        }

        [Fact]
        public void EveryKeyNameTheHeaderListsIsReadAndNoOtherName()
        {
            foreach (string name in KeyNames(true))
            {
                DefaultsIniValue value = ToggleKey(name);
                Assert.True(value.State == DefaultsIniValueState.Accepted, name + " is " + value.State + ": " + value.Reason);
            }
            foreach (string name in KeyNames(false))
            {
                DefaultsIniValue value = ToggleKey(name);
                Assert.True(value.State == DefaultsIniValueState.Refused, name + " is " + value.State);
                Assert.Equal(name + " is not one of the key names this file takes", value.Reason);
            }
        }

        private static DefaultsIniValue ToggleKey(string keys)
        {
            return DefaultsIni.Read(Encoding.ASCII.GetBytes("[Hotkeys]\r\nToggleKey=Ctrl+" + keys + "\r\n"))
                .Value(ConfigConcepts.ToggleKey);
        }

        // Every key name in data/keys.json the header lists, those with a Windows virtual-key code
        // that are not a Ctrl, Shift or Alt key, or every other name and every alias, since an alias
        // is not a name the header lists.
        private static List<string> KeyNames(bool taken)
        {
            var names = new List<string>();
            string path = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(Root()))!, "keys.json");
            using (JsonDocument keys = JsonDocument.Parse(File.ReadAllText(path)))
            {
                var modifierKeys = new HashSet<string>(keys.RootElement.GetProperty("modifiers").EnumerateArray()
                    .SelectMany(m => m.GetProperty("unity").EnumerateArray().Select(side => side.GetString()!)));
                Assert.Equal(6, modifierKeys.Count);
                foreach (JsonElement key in keys.RootElement.GetProperty("keys").EnumerateArray())
                {
                    string name = key.GetProperty("name").GetString()!;
                    if ((key.TryGetProperty("vk", out _) && !modifierKeys.Contains(name)) == taken) names.Add(name);
                    if (taken || !key.TryGetProperty("aliases", out JsonElement aliases)) continue;
                    names.AddRange(aliases.EnumerateArray().Select(alias => alias.GetString()!));
                }
            }
            Assert.NotEmpty(names);
            if (!taken) Assert.Contains("LeftShift", names);
            return names;
        }
    }
}
