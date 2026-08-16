using System.Collections.Generic;
using Xunit;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Math;

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
    }
}
