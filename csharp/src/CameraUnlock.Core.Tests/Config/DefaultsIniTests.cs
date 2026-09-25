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
        public void TheHeaderListsExactlyTheKeyNamesWithAVirtualKeyCode()
        {
            List<string> listed = DefaultsIniFixtures.HeaderKeyNames(DefaultsIni.Render());
            Assert.Equal(104, listed.Count);
            Assert.Equal(listed.Count, listed.Distinct().Count());
            Assert.Equal(KeyNames(true).OrderBy(n => n, System.StringComparer.Ordinal),
                listed.OrderBy(n => n, System.StringComparer.Ordinal));
        }

        [Fact]
        public void EveryKeyNameWithAVirtualKeyCodeIsReadAndNoOtherName()
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

        // Every key name in data/keys.json with a Windows virtual-key code, or every other name and
        // every alias, since an alias is not a name the header lists.
        private static List<string> KeyNames(bool withVirtualKey)
        {
            var names = new List<string>();
            string path = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(Root()))!, "keys.json");
            using (JsonDocument keys = JsonDocument.Parse(File.ReadAllText(path)))
            {
                foreach (JsonElement key in keys.RootElement.GetProperty("keys").EnumerateArray())
                {
                    if (key.TryGetProperty("vk", out _) == withVirtualKey) names.Add(key.GetProperty("name").GetString()!);
                    if (withVirtualKey || !key.TryGetProperty("aliases", out JsonElement aliases)) continue;
                    names.AddRange(aliases.EnumerateArray().Select(alias => alias.GetString()!));
                }
            }
            Assert.NotEmpty(names);
            return names;
        }
    }
}
