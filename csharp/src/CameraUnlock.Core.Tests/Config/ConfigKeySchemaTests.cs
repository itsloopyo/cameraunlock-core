using System.Collections.Generic;
using Xunit;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// The schema exists so the C# and C++ halves accept the same key spellings, and so the
    /// fleet's existing files keep parsing after the canonical names landed. The cases here
    /// are real spellings taken from shipped configs, not invented ones.
    /// </summary>
    public class ConfigKeySchemaTests
    {
        private static HeadTrackingConfigData Apply(Dictionary<string, string> values)
        {
            var config = new HeadTrackingConfigData();
            config.ApplyValues(values);
            return config;
        }

        [Theory]
        [InlineData("UdpPort", "udpport")]
        [InlineData("UDPPort", "udpport")]
        [InlineData("udp_port", "udpport")]
        [InlineData("Udp-Port", "udpport")]
        [InlineData("Port", "udpport")]
        [InlineData("yaw_sens", "yawsensitivity")]
        [InlineData("SensitivityYaw", "yawsensitivity")]
        [InlineData("limit_z_back", "positionlimitzback")]
        [InlineData("InvertTrackerZ", "invertpositionz")]
        [InlineData("PivotUp", "trackerpivotup")]
        [InlineData("HorizonLockedYaw", "worldspaceyaw")]
        [InlineData("WorldLockedYaw", "worldspaceyaw")]
        public void Resolve_MapsEverySpellingOntoOneCanonicalName(string spelling, string canonical)
        {
            Assert.Equal(canonical, ConfigKeySchema.Resolve(spelling));
        }

        [Fact]
        public void Resolve_ReturnsNullForAKeyOutsideTheSchema()
        {
            Assert.Null(ConfigKeySchema.Resolve("NotAKeyAnyoneUses"));
        }

        // Regression: Normalize used ToLowerInvariant, whose full Unicode case folding maps
        // U+212A KELVIN SIGN onto 'k', so a key spelling ToggleKey with one bound here and
        // was ignored by the C++ table, which folds A-Z and nothing else. Every spelling in
        // the schema is ASCII, so the narrow fold is the one both halves can implement.
        [Fact]
        public void Normalize_FoldsAsciiOnly()
        {
            Assert.Equal("toggle\u212Aey", ConfigKeySchema.Normalize("Toggle\u212Aey"));
            Assert.Null(ConfigKeySchema.Resolve("Toggle\u212Aey"));
            Assert.Equal("togglekey", ConfigKeySchema.Resolve("ToggleKey"));
        }

        [Fact]
        public void Retired_IsMarkedOnTheRetiredConceptOnly()
        {
            Assert.True(ConfigKeySchema.IsRetired(ConfigKeySchema.Keys.Smoothing));
            Assert.False(ConfigKeySchema.IsRetired(ConfigKeySchema.Keys.LocalSmoothing));
        }

        [Fact]
        public void PositionKeys_LandOnPositionSettings()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "PositionEnabled", "true" },
                { "sensitivity_x", "1.2" },
                { "SensY", "0.8" },
                { "PositionSensitivityZ", "1.1" },
                { "limit_x", "0.25" },
                { "LimitY", "0.30" },
                { "limit_y_down", "0.05" },
                { "LimitZ", "0.45" },
                { "limit_z_back", "0.12" },
                { "InvertZ", "true" },
                { "PivotForward", "0.08" },
                { "PivotUp", "0.03" },
            });

            Assert.True(config.PositionEnabled);
            Assert.Equal(1.2f, config.Position.SensitivityX);
            Assert.Equal(0.8f, config.Position.SensitivityY);
            Assert.Equal(1.1f, config.Position.SensitivityZ);
            Assert.Equal(0.25f, config.Position.LimitX);
            Assert.Equal(0.30f, config.Position.LimitY);
            Assert.Equal(0.05f, config.Position.LimitYDown);
            Assert.Equal(0.45f, config.Position.LimitZ);
            Assert.Equal(0.12f, config.Position.LimitZBack);
            Assert.True(config.Position.InvertZ);
            Assert.Equal(0.08f, config.TrackerPivotForward);
            Assert.Equal(0.03f, config.TrackerPivotUp);
        }

        // A file that names one vertical limit means one vertical limit. Leaving the down
        // side at its default caps a raised LimitY at 0.20m downward, which is the bug ~47
        // mod repos carry.
        [Fact]
        public void PositionLimitY_MirrorsIntoLimitYDownWhenThatKeyIsAbsent()
        {
            var config = Apply(new Dictionary<string, string> { { "LimitY", "0.40" } });

            Assert.Equal(0.40f, config.Position.LimitY);
            Assert.Equal(0.40f, config.Position.LimitYDown);
        }

        // An explicit downward limit wins, whatever order the two keys arrive in.
        [Fact]
        public void PositionLimitYDown_SurvivesAnExplicitLimitY()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "LimitY", "0.40" },
                { "LimitYDown", "0.05" },
            });

            Assert.Equal(0.40f, config.Position.LimitY);
            Assert.Equal(0.05f, config.Position.LimitYDown);
        }

        [Fact]
        public void HotkeyKeys_LandOnTheirNames()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "ToggleKey", "F10" },
                { "TogglePositionKey", "F11" },
                { "ReticleKey", "F12" },
                { "CycleTrackingMode", "F9" },
            });

            Assert.Equal("F10", config.ToggleKeyName);
            Assert.Equal("F11", config.PositionToggleKeyName);
            Assert.Equal("F12", config.ReticleToggleKeyName);
            Assert.Equal("F9", config.CycleTrackingModeKeyName);
        }

        // Position and rotation share one smoothing pair, so a config cannot end up
        // smoothing the two halves of the same pose differently.
        [Fact]
        public void Position_InheritsTheConfiguredSmoothingPair()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "LocalSmoothing", "0.2" },
                { "RemoteSmoothing", "0.4" },
            });

            Assert.Equal(0.2f, config.Position.LocalSmoothing);
            Assert.Equal(0.4f, config.Position.RemoteSmoothing);
        }

        [Fact]
        public void UnknownKeys_AreIgnoredRatherThanGuessedAt()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "SomeGameSpecificKey", "1.5" },
                { "YawSensitivity", "1.5" },
            });

            Assert.Equal(1.5f, config.Sensitivity.Yaw);
            Assert.Equal(1f, config.Sensitivity.Pitch);
        }
    }
}
