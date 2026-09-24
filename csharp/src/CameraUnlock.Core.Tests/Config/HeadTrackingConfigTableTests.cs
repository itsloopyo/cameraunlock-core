using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Effects;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="HeadTrackingConfigTableFixtures"/> on this test host's runtime, plus the
    /// argument checks, the hotkey defaults and a table over a class derived from
    /// <see cref="HeadTrackingConfigData"/>. The CameraUnlock.Core.FrameworkTests console runs the
    /// same fixtures on .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class HeadTrackingConfigTableTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return HeadTrackingConfigTableFixtures.Cases.Select(c => new object[] { c });
        }

        [Fact]
        public void EveryCanonicalConceptHasABinding()
        {
            Exception? error = Record.Exception(() => HeadTrackingConfigTable.Create(ConfigConcepts.All));
            Assert.True(error == null, "HeadTrackingConfigTable binds every canonical concept: " + error?.Message);
        }

        [Fact]
        public void TheDefaultsWithEveryConceptRenderAsAllConceptsIni()
        {
            HeadTrackingConfigTableFixtures.RunRender(Root());
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            HeadTrackingConfigTableFixtures.RunCase(Root(), name);
        }

        [Fact]
        public void TheCasesMoveEveryFieldOffItsDefault()
        {
            HeadTrackingConfigTableFixtures.RunCoverage(Root());
        }

        [Fact]
        public void TheHotkeyDefaultsAreTheCanonicalDefaults()
        {
            HeadTrackingConfigData defaults = HeadTrackingConfigTableFixtures.Defaults(
                HeadTrackingConfigTable.Create(ConfigConcepts.ToggleKey, ConfigConcepts.CycleTrackingModeKey, ConfigConcepts.YawModeKey,
                    ConfigConcepts.TrueFreeLookKey));

            Assert.Equal("End, Ctrl+Shift+Y", defaults.ToggleKeyName);
            Assert.Equal(ConfigConcepts.ToggleKey.CanonicalDefault, defaults.ToggleKeyName);
            Assert.Equal("PageUp, Ctrl+Shift+G", defaults.CycleTrackingModeKeyName);
            Assert.Equal(ConfigConcepts.CycleTrackingModeKey.CanonicalDefault, defaults.CycleTrackingModeKeyName);
            Assert.Equal("PageDown, Ctrl+Shift+H", defaults.YawModeKeyName);
            Assert.Equal(ConfigConcepts.YawModeKey.CanonicalDefault, defaults.YawModeKeyName);
            Assert.Equal("Insert, Ctrl+Shift+U", defaults.TrueFreeLookKeyName);
            Assert.Equal(ConfigConcepts.TrueFreeLookKey.CanonicalDefault, defaults.TrueFreeLookKeyName);
        }

        [Fact]
        public void TheFlatReadersFieldDefaultsAreSingleKeys()
        {
            var flat = new HeadTrackingConfigData();
            Assert.Equal("End", flat.ToggleKeyName);
            Assert.Equal(string.Empty, flat.CycleTrackingModeKeyName);
            Assert.Equal("PageDown", flat.YawModeKeyName);
            Assert.Equal("Insert", flat.TrueFreeLookKeyName);
        }

        [Fact]
        public void ACanonicalFilesTrueFreeLookSnakeCaseIsMisplacedAndNotRead()
        {
            var config = new HeadTrackingConfigData();
            ApplyReport report = HeadTrackingConfigTable.Create(ConfigConcepts.TrueFreeLook)
                .Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Position]\r\ntrue_free_look=true\r\n")), config);
            Assert.False(config.TrueFreeLook);
            Assert.Equal(CanonicalDiagnosticKind.MisplacedKey, Assert.Single(report.Diagnostics).Kind);
        }

        [Fact]
        public void TheFlatReaderReadsNeitherTrueFreeLookConcept()
        {
            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string>
            {
                { "TrueFreeLook", "true" },
                { "true_free_look", "true" },
                { "TrueFreeLookKey", "F8" },
            }, log.Add);
            Assert.False(flat.TrueFreeLook);
            Assert.Equal("Insert", flat.TrueFreeLookKeyName);
            Assert.Empty(log);
        }

        [Fact]
        public void AnEmptyListThrows()
        {
            var e = Assert.Throws<ArgumentException>(() => HeadTrackingConfigTable.Create());
            Assert.Contains("needs the concepts", e.Message);
        }

        [Fact]
        public void AConceptNamedTwiceThrows()
        {
            var e = Assert.Throws<ArgumentException>(() =>
                HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.ToggleKey, ConfigConcepts.UdpPort));
            Assert.Contains("names UdpPort twice", e.Message);
        }

        [Fact]
        public void ANullConceptThrows()
        {
            Assert.Throws<ArgumentNullException>(() => HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, null!));
            Assert.Throws<ArgumentNullException>(() => HeadTrackingConfigTable.Create((ConceptDescriptor[])null!));
        }

        [Fact]
        public void ATableOfTwoConceptsWritesOnlyTheirSections()
        {
            var table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.EnableOnStartup);
            string rendered = Encoding.ASCII.GetString(table.Render(new HeadTrackingConfigData(), HeadTrackingConfigTableFixtures.Header));
            Assert.Contains("\r\n[Network]\r\n", rendered);
            Assert.Contains("\r\n[General]\r\n", rendered);
            Assert.DoesNotContain("[Hotkeys]", rendered);
            Assert.DoesNotContain("[Light]", rendered);
            Assert.DoesNotContain("Hotkeys are key names", rendered);

            var config = new HeadTrackingConfigData();
            ApplyReport report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Light]\r\nLightMultiplier=2.0\r\n")), config);
            Assert.Equal(HeadFollowLightSettings.DefaultMultiplier, config.Light.Multiplier);
            Assert.Equal(CanonicalDiagnosticKind.UnknownSection, Assert.Single(report.Diagnostics).Kind);
        }

        [Fact]
        public void EachLightRowReplacesTheLightInsteadOfChangingAShared()
        {
            var shared = new HeadFollowLightSettings();
            var config = new HeadTrackingConfigData { Light = shared };
            HeadTrackingConfigTable.Create(ConfigConcepts.LightFollowsHead)
                .Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Light]\r\nLightFollowsHead=false\r\n")), config);
            Assert.False(config.Light.FollowsHead);
            Assert.True(shared.FollowsHead);

            config = new HeadTrackingConfigData { Light = shared };
            HeadTrackingConfigTable.Create(ConfigConcepts.LightMultiplier)
                .Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Light]\r\nLightMultiplier=2.0\r\n")), config);
            Assert.Equal(2.0f, config.Light.Multiplier);
            Assert.Equal(HeadFollowLightSettings.DefaultMultiplier, shared.Multiplier);
        }

        [Fact]
        public void EachSmoothingRowReachesThePositionCopyOnItsOwn()
        {
            var config = new HeadTrackingConfigData();
            HeadTrackingConfigTable.Create(ConfigConcepts.LocalSmoothing)
                .Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Smoothing]\r\nLocalSmoothing=0.25\r\n")), config);
            Assert.Equal(0.25f, config.LocalSmoothing);
            Assert.Equal(0.25f, config.Position.LocalSmoothing);

            config = new HeadTrackingConfigData();
            HeadTrackingConfigTable.Create(ConfigConcepts.RemoteSmoothing)
                .Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Smoothing]\r\nRemoteSmoothing=0.5\r\n")), config);
            Assert.Equal(0.5f, config.RemoteSmoothing);
            Assert.Equal(0.5f, config.Position.RemoteSmoothing);
        }

        private sealed class ModConfig : HeadTrackingConfigData
        {
            public bool DebugCamera;
        }

        [Fact]
        public void ADerivedConfigTakesCoreRowsAndTheGamesLocalRows()
        {
            ConfigTable<ModConfig> table = HeadTrackingConfigTable.Create<ModConfig>(ConfigConcepts.UdpPort, ConfigConcepts.ToggleKey)
                .Local("Debug", "DebugCamera", c => c.DebugCamera, (c, v) => c.DebugCamera = v, new BoolCodec(),
                    "true: log the camera once a second.");

            var config = new ModConfig();
            ApplyReport report = table.Apply(
                CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Network]\r\nUdpPort=5000\r\n[Debug]\r\nDebugCamera=true\r\n")), config);

            Assert.Empty(report.Diagnostics);
            Assert.Equal(5000, config.UdpPort);
            Assert.True(config.DebugCamera);
            Assert.Equal("End, Ctrl+Shift+Y", config.ToggleKeyName);
            Assert.Contains("\r\n[Debug]\r\n; true: log the camera once a second.\r\nDebugCamera=true\r\n",
                Encoding.ASCII.GetString(table.Render(config, HeadTrackingConfigTableFixtures.Header)));
        }
    }
}
