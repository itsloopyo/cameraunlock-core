using System.Collections.Generic;
using Xunit;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Config
{
    public class HeadTrackingConfigDataTests
    {
        private static HeadTrackingConfigData Apply(Dictionary<string, string> values, out List<string> log)
        {
            var captured = new List<string>();
            var config = new HeadTrackingConfigData();
            config.ApplyValues(values, captured.Add);
            log = captured;
            return config;
        }

        private static HeadTrackingConfigData Apply(string key, string value, out List<string> log)
        {
            return Apply(new Dictionary<string, string> { { key, value } }, out log);
        }

        [Fact]
        public void Defaults_AreZeroLocalAndFifteenHundredthsRemote()
        {
            var config = new HeadTrackingConfigData();

            Assert.Equal(SmoothingUtils.DefaultLocalSmoothing, config.LocalSmoothing);
            Assert.Equal(SmoothingUtils.DefaultRemoteSmoothing, config.RemoteSmoothing);
        }

        // A configured 0.0 is a legitimate choice under the two-parameter model - it is
        // the local default - so the sanitiser must be validation, never a floor.
        [Theory]
        [InlineData("0", 0f)]
        [InlineData("0.0", 0f)]
        [InlineData("0.42", 0.42f)]
        [InlineData("1", 1f)]
        public void LocalSmoothing_ValidValuesSurviveUntouched(string raw, float expected)
        {
            var config = Apply("LocalSmoothing", raw, out List<string> log);

            Assert.Equal(expected, config.LocalSmoothing);
            Assert.Empty(log);
        }

        [Theory]
        [InlineData("1.5", 1f)]
        [InlineData("-3", 0f)]
        public void LocalSmoothing_OutOfRangeValuesAreClamped(string raw, float expected)
        {
            var config = Apply("LocalSmoothing", raw, out _);

            Assert.Equal(expected, config.LocalSmoothing);
        }

        // NaN is the dangerous one: the clamps in CalculateSmoothingFactor compare with
        // > and <, every comparison against NaN is false, so nothing fires, exp(NaN) is
        // NaN, and the smoothed pose is NaN for the rest of the session.
        [Theory]
        [InlineData("NaN")]
        [InlineData("Infinity")]
        [InlineData("-Infinity")]
        [InlineData("banana")]
        public void LocalSmoothing_NonFiniteFallsBackToTheDefaultAndWarns(string raw)
        {
            var config = Apply("LocalSmoothing", raw, out List<string> log);

            Assert.Equal(SmoothingUtils.DefaultLocalSmoothing, config.LocalSmoothing);
            Assert.False(float.IsNaN(config.LocalSmoothing));
            Assert.Contains(log, m => m.Contains("LocalSmoothing"));
        }

        [Theory]
        [InlineData("NaN")]
        [InlineData("Infinity")]
        [InlineData("-Infinity")]
        [InlineData("banana")]
        public void RemoteSmoothing_NonFiniteFallsBackToTheDefaultAndWarns(string raw)
        {
            var config = Apply("RemoteSmoothing", raw, out List<string> log);

            Assert.Equal(SmoothingUtils.DefaultRemoteSmoothing, config.RemoteSmoothing);
            Assert.False(float.IsNaN(config.RemoteSmoothing));
            Assert.Contains(log, m => m.Contains("RemoteSmoothing"));
        }

        [Fact]
        public void RemoteSmoothing_ZeroSurvives_NotFlooredToTheDefault()
        {
            var config = Apply("RemoteSmoothing", "0", out _);

            Assert.Equal(0f, config.RemoteSmoothing);
        }

        [Theory]
        [InlineData("YawSensitivity")]
        [InlineData("PitchSensitivity")]
        [InlineData("RollSensitivity")]
        public void Sensitivity_NonFiniteIsRejectedAndTheDefaultSurvives(string key)
        {
            var config = Apply(key, "NaN", out _);

            Assert.False(float.IsNaN(config.Sensitivity.Yaw));
            Assert.False(float.IsNaN(config.Sensitivity.Pitch));
            Assert.False(float.IsNaN(config.Sensitivity.Roll));
        }

        [Fact]
        public void ReticleColor_NonFiniteComponentIsRejected()
        {
            var config = Apply("ReticleColor", "NaN,0.5,0.5", out _);

            foreach (float channel in config.ReticleColorRgba)
            {
                Assert.False(float.IsNaN(channel));
            }
        }

        // The retired-key warning is latched once per process, so both halves of the
        // contract have to be asserted in ONE test. Splitting them would make the second
        // test depend on xUnit's execution order.
        [Fact]
        public void RetiredSmoothingKey_IsIgnored_AndWarnsOnceNamingBothReplacements()
        {
            var config = Apply("Smoothing", "0.8", out List<string> log);

            Assert.Equal(SmoothingUtils.DefaultLocalSmoothing, config.LocalSmoothing);
            Assert.Equal(SmoothingUtils.DefaultRemoteSmoothing, config.RemoteSmoothing);

            string warning = Assert.Single(log);
            Assert.Contains("Smoothing", warning);
            Assert.Contains("LocalSmoothing", warning);
            Assert.Contains("RemoteSmoothing", warning);

            // Second sighting, in the same process, stays quiet.
            Apply("Smoothing", "0.8", out List<string> secondLog);
            Assert.Empty(secondLog);
        }

        // A negative limit is not a small limit. ClampToLimits calls
        // Clamp(y, -LimitYDown, LimitY), so LimitY = -0.5 mirrored into LimitYDown gave
        // Clamp(y, 0.5, -0.5) - min above max - and every input came back pinned half a
        // metre off centre while the load still logged "Config loaded successfully".
        [Theory]
        [InlineData("LimitX")]
        [InlineData("LimitY")]
        [InlineData("LimitYDown")]
        [InlineData("LimitZ")]
        [InlineData("LimitZBack")]
        [InlineData("PositionLimitX")]
        [InlineData("PositionLimitY")]
        public void Limit_NegativeValueIsRejectedAndWarns(string key)
        {
            var defaults = PositionSettings.Default;
            var config = Apply(key, "-0.5", out List<string> log);

            Assert.Equal(defaults.LimitX, config.Position.LimitX);
            Assert.Equal(defaults.LimitY, config.Position.LimitY);
            Assert.Equal(defaults.LimitYDown, config.Position.LimitYDown);
            Assert.Equal(defaults.LimitZ, config.Position.LimitZ);
            Assert.Equal(defaults.LimitZBack, config.Position.LimitZBack);

            string warning = Assert.Single(log);
            Assert.Contains(key, warning);
            Assert.Contains("-0.5", warning);
        }

        [Theory]
        [InlineData("YawSensitivity")]
        [InlineData("PitchSensitivity")]
        [InlineData("RollSensitivity")]
        [InlineData("PositionSensitivityX")]
        [InlineData("PositionSensitivityY")]
        [InlineData("PositionSensitivityZ")]
        public void Sensitivity_NegativeValueIsRejectedAndWarns(string key)
        {
            var config = Apply(key, "-2", out List<string> log);

            Assert.Equal(1f, config.Sensitivity.Yaw);
            Assert.Equal(1f, config.Sensitivity.Pitch);
            Assert.Equal(1f, config.Sensitivity.Roll);
            Assert.Equal(1f, config.Position.SensitivityX);
            Assert.Equal(1f, config.Position.SensitivityY);
            Assert.Equal(1f, config.Position.SensitivityZ);

            string warning = Assert.Single(log);
            Assert.Contains(key, warning);
            Assert.Contains("-2", warning);
        }

        [Theory]
        [InlineData("TrackerPivotForward")]
        [InlineData("TrackerPivotUp")]
        public void TrackerPivot_NegativeValueIsRejectedAndWarns(string key)
        {
            var config = Apply(key, "-0.1", out List<string> log);

            Assert.Equal(0f, config.TrackerPivotForward);
            Assert.Equal(0f, config.TrackerPivotUp);
            Assert.Contains(log, m => m.Contains(key));
        }

        // Zero is a real request, not a mistake: a zero limit locks the axis and a zero
        // sensitivity disables it. Rejecting or flooring either would take a working
        // configuration away from whoever wrote it.
        [Fact]
        public void Zero_IsAcceptedForEveryLimitAndSensitivity()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "LimitX", "0" },
                { "LimitY", "0" },
                { "LimitYDown", "0" },
                { "LimitZ", "0" },
                { "LimitZBack", "0" },
                { "YawSensitivity", "0" },
                { "PitchSensitivity", "0" },
                { "RollSensitivity", "0" },
                { "PositionSensitivityX", "0" },
                { "PositionSensitivityY", "0" },
                { "PositionSensitivityZ", "0" },
            }, out List<string> log);

            Assert.Equal(0f, config.Position.LimitX);
            Assert.Equal(0f, config.Position.LimitY);
            Assert.Equal(0f, config.Position.LimitYDown);
            Assert.Equal(0f, config.Position.LimitZ);
            Assert.Equal(0f, config.Position.LimitZBack);
            Assert.Equal(0f, config.Sensitivity.Yaw);
            Assert.Equal(0f, config.Position.SensitivityX);
            Assert.Empty(log);
        }

        // The defect measured through the real processor, not through the settings struct:
        // before the sign check, every one of these came back at -0.5.
        [Theory]
        [InlineData(0f)]
        [InlineData(0.1f)]
        [InlineData(-0.1f)]
        public void NegativeLimitY_NoLongerPinsThePositionProcessor(float input)
        {
            var config = Apply("LimitY", "-0.5", out _);

            var processor = new PositionProcessor { Settings = config.Position };
            Vec3 result = processor.Process(new PositionData(0f, input, 0f, 1000L), Quat4.Identity, 1f / 60f);

            Assert.Equal(input, result.Y, precision: 4);
        }

        // Two spellings of one concept resolve on Dictionary enumeration order, so the
        // file is ambiguous. Which one wins is left exactly as it was - consumers may be
        // relying on it - but the ambiguity is now reported instead of swallowed.
        [Theory]
        [InlineData("PositionLimitX", "LimitX")]
        [InlineData("LimitX", "PositionLimitX")]
        public void CanonicalAndAlias_AreReportedAsOneAmbiguousSetting(string first, string second)
        {
            var config = Apply(new Dictionary<string, string>
            {
                { first, "0.9" },
                { second, "0.1" },
            }, out List<string> log);

            Assert.True(config.Position.LimitX == 0.9f || config.Position.LimitX == 0.1f);

            string warning = Assert.Single(log);
            Assert.Contains(first, warning);
            Assert.Contains(second, warning);
        }

        [Fact]
        public void DistinctConcepts_DoNotWarn()
        {
            Apply(new Dictionary<string, string>
            {
                { "LimitX", "0.1" },
                { "LimitY", "0.2" },
                { "LimitZ", "0.3" },
            }, out List<string> log);

            Assert.Empty(log);
        }

        [Fact]
        public void SymmetricVerticalLimit_MirrorsALimitYOnlyConfigIntoLimitYDown()
        {
            var config = Apply("LimitY", "0.40", out List<string> log);

            Assert.Equal(0.40f, config.Position.LimitY);
            Assert.Equal(0.40f, config.Position.LimitYDown);
            Assert.Empty(log);
        }

        [Fact]
        public void ExplicitLimitYDown_BeatsTheMirror()
        {
            var config = Apply(new Dictionary<string, string>
            {
                { "LimitY", "0.40" },
                { "LimitYDown", "0.05" },
            }, out _);

            Assert.Equal(0.40f, config.Position.LimitY);
            Assert.Equal(0.05f, config.Position.LimitYDown);
        }

        // A caller that built an asymmetric vertical limit chose it to keep the camera out
        // of the player body. The mirror used to overwrite it with the file's upward limit,
        // so a LimitY of 0.40 quietly widened the downward range from 0.05 to 0.40.
        [Fact]
        public void AsymmetricProgrammaticLimit_SurvivesALimitYOnlyConfig()
        {
            var config = new HeadTrackingConfigData();
            config.Position = new PositionSettings(
                1f, 1f, 1f,
                0.30f, 0.20f, 0.05f, 0.40f, 0.10f,
                0f, 0.15f);

            var log = new List<string>();
            config.ApplyValues(new Dictionary<string, string> { { "LimitY", "0.40" } }, log.Add);

            Assert.Equal(0.40f, config.Position.LimitY);
            Assert.Equal(0.05f, config.Position.LimitYDown);

            string message = Assert.Single(log);
            Assert.Contains("LimitY", message);
            Assert.Contains("LimitYDown", message);
        }
    }
}
