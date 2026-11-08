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

        [Fact]
        public void TheFreshFileWithEveryConceptIsAllConceptsFreshIni()
        {
            HeadTrackingConfigTableFixtures.RunFreshRender(Root());
        }

        [Fact]
        public void EffectiveDefaultsReachThePositionCopyAndMigrationWritesTheEngineRowAsAValue()
        {
            HeadTrackingConfigTableFixtures.RunEffectiveDefaults();
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
                    ConfigConcepts.TrueFreeLookKey, ConfigConcepts.FirstPersonKey));

            Assert.Equal("End, Ctrl+Shift+Y", defaults.ToggleKeyName);
            Assert.Equal(ConfigConcepts.ToggleKey.CanonicalDefault, defaults.ToggleKeyName);
            Assert.Equal("PageUp, Ctrl+Shift+G", defaults.CycleTrackingModeKeyName);
            Assert.Equal(ConfigConcepts.CycleTrackingModeKey.CanonicalDefault, defaults.CycleTrackingModeKeyName);
            Assert.Equal("PageDown, Ctrl+Shift+H", defaults.YawModeKeyName);
            Assert.Equal(ConfigConcepts.YawModeKey.CanonicalDefault, defaults.YawModeKeyName);
            Assert.Equal("Insert, Ctrl+Shift+U", defaults.TrueFreeLookKeyName);
            Assert.Equal(ConfigConcepts.TrueFreeLookKey.CanonicalDefault, defaults.TrueFreeLookKeyName);
            Assert.Equal("ScrollLock, Ctrl+Shift+B", defaults.FirstPersonKeyName);
            Assert.Equal(ConfigConcepts.FirstPersonKey.CanonicalDefault, defaults.FirstPersonKeyName);
        }

        [Fact]
        public void CollisionEnabledStartsAtItsCanonicalDefaultAndTheFlatReaderAtFalse()
        {
            HeadTrackingConfigData defaults = HeadTrackingConfigTableFixtures.Defaults(
                HeadTrackingConfigTable.Create(ConfigConcepts.CollisionEnabled));
            Assert.True(defaults.CollisionEnabled);
            Assert.Equal("true", ConfigConcepts.CollisionEnabled.CanonicalDefault);
            Assert.False(new HeadTrackingConfigData().CollisionEnabled);
        }

        [Fact]
        public void TheGlobalConceptsAreThe29TheSchemaNames()
        {
            string[] global =
            {
                "UdpPort", "EnableOnStartup", "LocalSmoothing", "RemoteSmoothing", "Accela", "AccelaPlayers", "WorldSpaceYaw",
                "RotationEnabled", "DataFreshnessMs", "PositionEnabled", "TrueFreeLook", "FreeLookMarker",
                "StockSights", "PositionLimitX", "PositionLimitY", "PositionLimitYDown", "PositionLimitZ", "PositionLimitZBack",
                "CollisionEnabled", "CollisionReleaseSmoothing", "ToggleKey",
                "CycleTrackingModeKey", "YawModeKey", "TrueFreeLookKey", "FirstPersonKey", "LightMultiplier", "GameFov",
                "FrameRateUnlock", "Fxaa",
            };
            Assert.Equal(global, ConfigConcepts.All.Where(c => c.Global).Select(c => c.Id));
            Assert.Equal(new[] { "CollisionMargin", "CollisionChannel" }, ConfigConcepts.All.Where(c => !c.Global).Select(c => c.Id));
        }

        [Fact]
        public void CollisionMarginAndChannelKeepTheGamesOwnDefaultAndChannelIsAnEngineRow()
        {
            var pinned = new HeadTrackingConfigData { CollisionMargin = 10f, CollisionChannel = 3 };
            ConfigTable<HeadTrackingConfigData> table = HeadTrackingConfigTable.Create(
                ConfigConcepts.CollisionEnabled, ConfigConcepts.CollisionMargin, ConfigConcepts.CollisionChannel);
            string fresh = Encoding.ASCII.GetString(table.RenderFresh(new RenderHeader("G")));
            Assert.Contains("\r\nCollisionEnabled=default\r\n", fresh);
            Assert.Contains("\r\nCollisionMargin=0.1\r\n", fresh);
            Assert.Contains("\r\n; CollisionChannel=0\r\n", fresh);
            Assert.Equal(Encoding.ASCII.GetString(table.Render(pinned, new RenderHeader("G"))),
                fresh.Replace("CollisionEnabled=default", "CollisionEnabled=false")
                    .Replace("CollisionMargin=0.1", "CollisionMargin=10.0")
                    .Replace("; CollisionChannel=0", "CollisionChannel=3"));
        }

        [Fact]
        public void TheFlatReadersFieldDefaultsAreSingleKeys()
        {
            var flat = new HeadTrackingConfigData();
            Assert.Equal("End", flat.ToggleKeyName);
            Assert.Equal(string.Empty, flat.CycleTrackingModeKeyName);
            Assert.Equal("PageDown", flat.YawModeKeyName);
        }

        [Fact]
        public void FirstPersonKeyNamesInitialiserIsItsCanonicalDefault()
        {
            Assert.Equal(ConfigConcepts.FirstPersonKey.CanonicalDefault, new HeadTrackingConfigData().FirstPersonKeyName);
        }

        // A mod with a first-person view names FirstPersonKey and reads the one global key list.
        // The C++ twin is TestFirstPersonKey in head_tracking_config_table_tests.cpp.
        [Fact]
        public void AModThatNamesFirstPersonKeyReadsItAndDefaultsToScrollLockAndCtrlShiftB()
        {
            Assert.True(ConfigConcepts.FirstPersonKey.Global);
            Assert.Equal(ConceptValueFamily.Hotkey, ConfigConcepts.FirstPersonKey.Family);
            ConfigTable<HeadTrackingConfigData> table = HeadTrackingConfigTable.Create(
                ConfigConcepts.ToggleKey, ConfigConcepts.FirstPersonKey);
            Assert.Equal("ScrollLock, Ctrl+Shift+B", HeadTrackingConfigTableFixtures.Defaults(table).FirstPersonKeyName);

            string fresh = Encoding.ASCII.GetString(table.RenderFresh(new RenderHeader("G")));
            int hotkeys = fresh.IndexOf("\r\n[Hotkeys]\r\n", StringComparison.Ordinal);
            int toggle = fresh.IndexOf("\r\nToggleKey=default\r\n", StringComparison.Ordinal);
            int key = fresh.IndexOf(
                "\r\n; Switches the view between first person and the game's own camera, where the game has first person."
                + "\r\nFirstPersonKey=default\r\n", StringComparison.Ordinal);
            Assert.True(hotkeys >= 0 && toggle > hotkeys && key > toggle, fresh);

            var config = new HeadTrackingConfigData();
            ApplyReport report = table.Apply(
                CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Hotkeys]\r\nFirstPersonKey=f7, ctrl+shift+b\r\n")), config);
            Assert.Equal("F7, Ctrl+Shift+B", config.FirstPersonKeyName);
            Assert.Empty(report.Diagnostics);

            config = new HeadTrackingConfigData { FirstPersonKeyName = "F1" };
            report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Hotkeys]\r\nFirstPersonKey=default\r\n")), config);
            Assert.Equal("ScrollLock, Ctrl+Shift+B", config.FirstPersonKeyName);
            Assert.Empty(report.Diagnostics);

            config = new HeadTrackingConfigData();
            report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Hotkeys]\r\nToggleFirstPersonKey=F7\r\n")), config);
            Assert.Equal("ScrollLock, Ctrl+Shift+B", config.FirstPersonKeyName);
            Assert.Equal(CanonicalDiagnosticKind.MisplacedKey, Assert.Single(report.Diagnostics).Kind);

            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string>
            {
                { "FirstPersonKey", "F8" },
                { "ToggleFirstPersonKey", "F9" },
                { "first_person_toggle_key", "F10" },
            }, log.Add);
            Assert.Equal("ScrollLock, Ctrl+Shift+B", flat.FirstPersonKeyName);
            Assert.Empty(log);
        }

        // GameFov reads as vertical degrees, 0 for the game's own, and refuses a value past 140 for its default.
        // The C++ twin is TestGameFov in head_tracking_config_table_tests.cpp.
        [Fact]
        public void GameFovReadsDegreesAndDefaultsToTheGamesOwn()
        {
            Assert.True(ConfigConcepts.GameFov.Global);
            Assert.Equal(ConceptValueFamily.Floating, ConfigConcepts.GameFov.Family);
            ConfigTable<HeadTrackingConfigData> table = HeadTrackingConfigTable.Create(
                ConfigConcepts.LightMultiplier, ConfigConcepts.GameFov);
            Assert.Equal(0f, HeadTrackingConfigTableFixtures.Defaults(table).GameFov);

            string fresh = Encoding.ASCII.GetString(table.RenderFresh(new RenderHeader("G")));
            int light = fresh.IndexOf("\r\n[Light]\r\n", StringComparison.Ordinal);
            int view = fresh.IndexOf("\r\n[View]\r\n", StringComparison.Ordinal);
            int key = fresh.IndexOf(
                "\r\n; The game's field of view in vertical degrees; 60 is about 91 horizontal at 16:9."
                + "\r\n; 0 keeps the game's own. Scopes and zoom keep their share of it.\r\nGameFov=default\r\n",
                StringComparison.Ordinal);
            Assert.True(light >= 0 && view > light && key > view, fresh);

            var config = new HeadTrackingConfigData();
            ApplyReport report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[View]\r\nGameFov=75.5\r\n")), config);
            Assert.Equal(75.5f, config.GameFov);
            Assert.Empty(report.Diagnostics);

            config = new HeadTrackingConfigData { GameFov = 60f };
            report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[View]\r\nGameFov=141\r\n")), config);
            Assert.Equal(0f, config.GameFov);
            Assert.Single(report.Diagnostics);

            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string> { { "GameFov", "90" } }, log.Add);
            Assert.Equal(0f, flat.GameFov);
            Assert.Empty(log);
        }

        // FrameRateUnlock reads as a bool, false by default, and refuses a word that is not one for its default.
        // The C++ twin is TestFrameRateUnlock in head_tracking_config_table_tests.cpp.
        [Fact]
        public void FrameRateUnlockReadsABoolAndDefaultsToTheGamesOwnLimit()
        {
            Assert.True(ConfigConcepts.FrameRateUnlock.Global);
            Assert.Equal(ConceptValueFamily.Bool, ConfigConcepts.FrameRateUnlock.Family);
            ConfigTable<HeadTrackingConfigData> table = HeadTrackingConfigTable.Create(
                ConfigConcepts.GameFov, ConfigConcepts.FrameRateUnlock);
            Assert.False(HeadTrackingConfigTableFixtures.Defaults(table).FrameRateUnlock);

            string fresh = Encoding.ASCII.GetString(table.RenderFresh(new RenderHeader("G")));
            int view = fresh.IndexOf("\r\n[View]\r\n", StringComparison.Ordinal);
            int fov = fresh.IndexOf("\r\nGameFov=default\r\n", StringComparison.Ordinal);
            int key = fresh.IndexOf(
                "\r\n; true: lifts the game's own frame-rate limit where the mod can, at the game's normal speed."
                + "\r\n; Leave it false if another mod already unlocks this game's frame rate.\r\nFrameRateUnlock=default\r\n",
                StringComparison.Ordinal);
            Assert.True(view >= 0 && fov > view && key > fov, fresh);

            var config = new HeadTrackingConfigData();
            ApplyReport report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[View]\r\nFrameRateUnlock=true\r\n")), config);
            Assert.True(config.FrameRateUnlock);
            Assert.Empty(report.Diagnostics);

            config = new HeadTrackingConfigData { FrameRateUnlock = true };
            report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[View]\r\nFrameRateUnlock=maybe\r\n")), config);
            Assert.False(config.FrameRateUnlock);
            Assert.Single(report.Diagnostics);

            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string> { { "FrameRateUnlock", "true" } }, log.Add);
            Assert.False(flat.FrameRateUnlock);
            Assert.Empty(log);
        }

        // Fxaa reads as a bool, false by default, and refuses a word that is not one for its default.
        // The C++ twin is TestFxaa in head_tracking_config_table_tests.cpp.
        [Fact]
        public void FxaaReadsABoolAndDefaultsToThePictureAsDrawn()
        {
            Assert.True(ConfigConcepts.Fxaa.Global);
            Assert.Equal(ConceptValueFamily.Bool, ConfigConcepts.Fxaa.Family);
            ConfigTable<HeadTrackingConfigData> table = HeadTrackingConfigTable.Create(
                ConfigConcepts.FrameRateUnlock, ConfigConcepts.Fxaa);
            Assert.False(HeadTrackingConfigTableFixtures.Defaults(table).Fxaa);

            string fresh = Encoding.ASCII.GetString(table.RenderFresh(new RenderHeader("G")));
            int view = fresh.IndexOf("\r\n[View]\r\n", StringComparison.Ordinal);
            int unlock = fresh.IndexOf("\r\nFrameRateUnlock=default\r\n", StringComparison.Ordinal);
            int key = fresh.IndexOf(
                "\r\n; true: smooths the jagged edges of the game's 3D with FXAA where the mod can."
                + "\r\n; It softens fine detail a little; leave it false if the game already has anti-aliasing on.\r\nFxaa=default\r\n",
                StringComparison.Ordinal);
            Assert.True(view >= 0 && unlock > view && key > unlock, fresh);

            var config = new HeadTrackingConfigData();
            ApplyReport report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[View]\r\nFxaa=true\r\n")), config);
            Assert.True(config.Fxaa);
            Assert.Empty(report.Diagnostics);

            config = new HeadTrackingConfigData { Fxaa = true };
            report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[View]\r\nFxaa=maybe\r\n")), config);
            Assert.False(config.Fxaa);
            Assert.Single(report.Diagnostics);

            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string> { { "Fxaa", "true" } }, log.Add);
            Assert.False(flat.Fxaa);
            Assert.Empty(log);
        }

        [Fact]
        public void TrueFreeLookKeyNamesInitialiserIsItsCanonicalDefault()
        {
            Assert.Equal(ConfigConcepts.TrueFreeLookKey.CanonicalDefault, new HeadTrackingConfigData().TrueFreeLookKeyName);
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
        public void TheFlatReaderReadsNoneOfTheAimModeConcepts()
        {
            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string>
            {
                { "TrueFreeLook", "true" },
                { "true_free_look", "true" },
                { "TrueFreeLookKey", "F8" },
                { "FreeLookMarker", "true" },
                { "StockSights", "true" },
            }, log.Add);
            Assert.False(flat.TrueFreeLook);
            Assert.False(flat.FreeLookMarker);
            Assert.False(flat.StockSights);
            Assert.Equal("Insert, Ctrl+Shift+U", flat.TrueFreeLookKeyName);
            Assert.Empty(log);
        }

        [Fact]
        public void TheFlatReaderDoesNotReadAccela()
        {
            var flat = new HeadTrackingConfigData();
            var log = new List<string>();
            flat.ApplyValues(new Dictionary<string, string>
            {
                { "Accela", "0.2" },
                { "accela", "0.3" },
                { "AccelaPlayers", "2, 3" },
                { "accela_players", "4" },
            }, log.Add);
            Assert.Equal(0.75f, flat.Accela);
            Assert.Equal(string.Empty, flat.AccelaPlayers);
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
        public void TheLightRowReplacesTheLightInsteadOfChangingAShared()
        {
            var shared = new HeadFollowLightSettings();
            var config = new HeadTrackingConfigData { Light = shared };
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
