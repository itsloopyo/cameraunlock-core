// Test files intentionally pass null to test exception handling
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type

using System;
using Xunit;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Tests.Processing.AxisTransform
{
    public class MappingConfigTests
    {
        [Fact]
        public void Constructor_SetsDefaultConfigs()
        {
            var config = new MappingConfig();

            Assert.NotNull(config.YawConfig);
            Assert.NotNull(config.PitchConfig);
            Assert.NotNull(config.RollConfig);
        }

        [Fact]
        public void Constructor_YawMapsToYaw()
        {
            var config = new MappingConfig();

            Assert.Equal(AxisSource.Yaw, config.YawConfig.Source);
        }

        [Fact]
        public void Constructor_PitchMapsToPitch()
        {
            var config = new MappingConfig();

            Assert.Equal(AxisSource.Pitch, config.PitchConfig.Source);
        }

        [Fact]
        public void Constructor_RollMapsToRoll()
        {
            var config = new MappingConfig();

            Assert.Equal(AxisSource.Roll, config.RollConfig.Source);
        }

        [Fact]
        public void ApplyMapping_DirectValues_DefaultPassthrough()
        {
            var config = new MappingConfig();

            config.ApplyMapping(10f, 20f, 30f, out float yaw, out float pitch, out float roll);

            Assert.Equal(10f, yaw, precision: 3);
            Assert.Equal(20f, pitch, precision: 3);
            Assert.Equal(30f, roll, precision: 3);
        }

        [Fact]
        public void ApplyMapping_Array_DefaultPassthrough()
        {
            var config = new MappingConfig();
            float[] rawData = { 10f, 20f, 30f, 0f, 0f, 0f };

            config.ApplyMapping(rawData, out float yaw, out float pitch, out float roll);

            Assert.Equal(10f, yaw, precision: 3);
            Assert.Equal(20f, pitch, precision: 3);
            Assert.Equal(30f, roll, precision: 3);
        }

        [Fact]
        public void ApplyMapping_Array_ThrowsOnNull()
        {
            var config = new MappingConfig();

            Assert.Throws<ArgumentException>(() =>
                config.ApplyMapping(null, out _, out _, out _));
        }

        [Fact]
        public void ApplyMapping_Array_ThrowsOnInsufficientLength()
        {
            var config = new MappingConfig();
            float[] rawData = { 10f, 20f, 30f }; // Only 3 elements

            Assert.Throws<ArgumentException>(() =>
                config.ApplyMapping(rawData, out _, out _, out _));
        }

        [Fact]
        public void ApplyMapping_WithSensitivity_ScalesOutput()
        {
            var config = new MappingConfig();
            config.YawConfig.Sensitivity = 2.0f;
            config.PitchConfig.Sensitivity = 0.5f;

            config.ApplyMapping(10f, 20f, 30f, out float yaw, out float pitch, out float roll);

            Assert.Equal(20f, yaw, precision: 3);
            Assert.Equal(10f, pitch, precision: 3);
            Assert.Equal(30f, roll, precision: 3);
        }

        [Fact]
        public void ApplyMapping_WithInversion_InvertsOutput()
        {
            var config = new MappingConfig();
            config.YawConfig.Inverted = true;
            config.PitchConfig.Inverted = true;

            config.ApplyMapping(10f, 20f, 30f, out float yaw, out float pitch, out float roll);

            Assert.Equal(-10f, yaw, precision: 3);
            Assert.Equal(-20f, pitch, precision: 3);
            Assert.Equal(30f, roll, precision: 3); // Not inverted
        }

        [Fact]
        public void ApplyMapping_WithDisabledAxis_ReturnsZero()
        {
            var config = new MappingConfig();
            config.RollConfig.Source = AxisSource.None;

            config.ApplyMapping(10f, 20f, 30f, out float yaw, out float pitch, out float roll);

            Assert.Equal(10f, yaw, precision: 3);
            Assert.Equal(20f, pitch, precision: 3);
            Assert.Equal(0f, roll, precision: 3);
        }

        [Fact]
        public void ApplyMapping_WithAxisRemap_SwapsAxes()
        {
            var config = new MappingConfig();
            config.YawConfig.Source = AxisSource.Pitch;  // Yaw reads from Pitch
            config.PitchConfig.Source = AxisSource.Yaw;  // Pitch reads from Yaw

            config.ApplyMapping(10f, 20f, 30f, out float yaw, out float pitch, out float roll);

            Assert.Equal(20f, yaw, precision: 3);  // Was pitch
            Assert.Equal(10f, pitch, precision: 3); // Was yaw
        }

        [Fact]
        public void ApplyMapping_WithLimits_ClampsOutput()
        {
            var config = new MappingConfig();
            config.YawConfig.EnableLimits = true;
            config.YawConfig.MinLimit = -30f;
            config.YawConfig.MaxLimit = 30f;

            config.ApplyMapping(100f, 20f, 30f, out float yaw, out float pitch, out float roll);

            Assert.Equal(30f, yaw, precision: 3); // Clamped
        }

        [Fact]
        public void ResetToDefault_RestoresDefaultValues()
        {
            var config = new MappingConfig();
            config.YawConfig.Sensitivity = 5.0f;
            config.YawConfig.Inverted = true;
            config.PitchConfig.Source = AxisSource.None;

            config.ResetToDefault();

            Assert.Equal(1.0f, config.YawConfig.Sensitivity, precision: 3);
            Assert.False(config.YawConfig.Inverted);
            Assert.Equal(AxisSource.Pitch, config.PitchConfig.Source);
        }

        [Fact]
        public void LoadPreset_AppliesPreset()
        {
            var config = new MappingConfig();

            config.LoadPreset(MappingPreset.HighSensitivity);

            Assert.Equal(1.5f, config.YawConfig.Sensitivity, precision: 3);
            Assert.Equal(1.5f, config.PitchConfig.Sensitivity, precision: 3);
            Assert.Equal(1.2f, config.RollConfig.Sensitivity, precision: 3);
        }

        [Fact]
        public void Clone_CreatesIndependentCopy()
        {
            var original = new MappingConfig();
            original.YawConfig.Sensitivity = 2.0f;

            var clone = original.Clone();
            clone.YawConfig.Sensitivity = 3.0f;

            Assert.Equal(2.0f, original.YawConfig.Sensitivity, precision: 3);
            Assert.Equal(3.0f, clone.YawConfig.Sensitivity, precision: 3);
        }

        [Fact]
        public void Clone_CopiesAllSettings()
        {
            var original = new MappingConfig();
            original.YawConfig.Sensitivity = 2.0f;
            original.YawConfig.Inverted = true;
            original.PitchConfig.DeadzoneMin = 5f;
            original.RollConfig.Source = AxisSource.None;

            var clone = original.Clone();

            Assert.Equal(2.0f, clone.YawConfig.Sensitivity, precision: 3);
            Assert.True(clone.YawConfig.Inverted);
            Assert.Equal(5f, clone.PitchConfig.DeadzoneMin, precision: 3);
            Assert.Equal(AxisSource.None, clone.RollConfig.Source);
        }
    }
}
