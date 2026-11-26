// Test files intentionally pass null to test exception handling
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type

using System;
using Xunit;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Tests.Processing.AxisTransform
{
    public class MappingPresetTests
    {
        [Fact]
        public void CreateFromPreset_Default_HasUnitSensitivity()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Default);

            Assert.Equal(1.0f, config.YawConfig.Sensitivity, precision: 3);
            Assert.Equal(1.0f, config.PitchConfig.Sensitivity, precision: 3);
            Assert.Equal(1.0f, config.RollConfig.Sensitivity, precision: 3);
        }

        [Fact]
        public void CreateFromPreset_Default_NoInversion()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Default);

            Assert.False(config.YawConfig.Inverted);
            Assert.False(config.PitchConfig.Inverted);
            Assert.False(config.RollConfig.Inverted);
        }

        [Fact]
        public void CreateFromPreset_InvertedPitch_OnlyPitchInverted()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.InvertedPitch);

            Assert.False(config.YawConfig.Inverted);
            Assert.True(config.PitchConfig.Inverted);
            Assert.False(config.RollConfig.Inverted);
        }

        [Fact]
        public void CreateFromPreset_NoRoll_RollDisabled()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.NoRoll);

            Assert.Equal(AxisSource.Yaw, config.YawConfig.Source);
            Assert.Equal(AxisSource.Pitch, config.PitchConfig.Source);
            Assert.Equal(AxisSource.None, config.RollConfig.Source);
        }

        [Fact]
        public void CreateFromPreset_HighSensitivity_HasExpectedValues()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.HighSensitivity);

            Assert.Equal(1.5f, config.YawConfig.Sensitivity, precision: 3);
            Assert.Equal(1.5f, config.PitchConfig.Sensitivity, precision: 3);
            Assert.Equal(1.2f, config.RollConfig.Sensitivity, precision: 3);
        }

        [Fact]
        public void CreateFromPreset_LowSensitivity_HasExpectedValues()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.LowSensitivity);

            Assert.Equal(0.7f, config.YawConfig.Sensitivity, precision: 3);
            Assert.Equal(0.7f, config.PitchConfig.Sensitivity, precision: 3);
            Assert.Equal(0.5f, config.RollConfig.Sensitivity, precision: 3);
        }

        [Fact]
        public void CreateFromPreset_Competitive_HasQuadraticYawCurve()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Competitive);

            Assert.Equal(SensitivityCurve.Quadratic, config.YawConfig.SensitivityCurve);
            Assert.Equal(0.5f, config.YawConfig.CurveStrength, precision: 3);
        }

        [Fact]
        public void CreateFromPreset_Competitive_HasDeadzone()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Competitive);

            Assert.Equal(0.5f, config.YawConfig.DeadzoneMin, precision: 3);
            Assert.Equal(0.5f, config.PitchConfig.DeadzoneMin, precision: 3);
        }

        [Fact]
        public void CreateFromPreset_Competitive_NoRoll()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Competitive);

            Assert.Equal(AxisSource.None, config.RollConfig.Source);
        }

        [Fact]
        public void CreateFromPreset_Simulation_HasSCurveAllAxes()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Simulation);

            Assert.Equal(SensitivityCurve.SCurve, config.YawConfig.SensitivityCurve);
            Assert.Equal(SensitivityCurve.SCurve, config.PitchConfig.SensitivityCurve);
            Assert.Equal(SensitivityCurve.SCurve, config.RollConfig.SensitivityCurve);
        }

        [Fact]
        public void CreateFromPreset_Simulation_CurveStrength()
        {
            var config = MappingPresets.CreateFromPreset(MappingPreset.Simulation);

            Assert.Equal(0.7f, config.YawConfig.CurveStrength, precision: 3);
            Assert.Equal(0.7f, config.PitchConfig.CurveStrength, precision: 3);
            Assert.Equal(0.7f, config.RollConfig.CurveStrength, precision: 3);
        }

        [Fact]
        public void ApplyPreset_ThrowsOnNullConfig()
        {
            Assert.Throws<ArgumentNullException>(() =>
                MappingPresets.ApplyPreset(null, MappingPreset.Default));
        }

        [Fact]
        public void ApplyPreset_ResetsBeforeApplying()
        {
            var config = new MappingConfig();
            config.YawConfig.Sensitivity = 10.0f; // Set a custom value

            MappingPresets.ApplyPreset(config, MappingPreset.Default);

            Assert.Equal(1.0f, config.YawConfig.Sensitivity, precision: 3); // Reset to default
        }

        [Fact]
        public void CreateFromPreset_ReturnsNewInstance()
        {
            var config1 = MappingPresets.CreateFromPreset(MappingPreset.Default);
            var config2 = MappingPresets.CreateFromPreset(MappingPreset.Default);

            Assert.NotSame(config1, config2);
        }
    }
}
