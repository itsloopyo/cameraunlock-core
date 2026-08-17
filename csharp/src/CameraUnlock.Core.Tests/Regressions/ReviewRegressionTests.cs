using System;
using System.Globalization;
using System.IO;
using System.Threading;
using Xunit;
using CameraUnlock.Core.Aim;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Config.Profiles;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Diagnostics;
using CameraUnlock.Core.Input;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Tests.Regressions
{
    /// <summary>
    /// Pins the defects found in the adversarial review. Each test fails against the
    /// pre-fix code, so a future change that reintroduces one of these is caught here
    /// rather than on a user's machine.
    /// </summary>
    public class ReviewRegressionTests
    {
        private const float DeltaTime60Fps = 1f / 60f;

        // ---- ScreenOffsetCalculator: behind-camera aim must not mirror ----

        [Theory]
        [InlineData(100f)]
        [InlineData(135f)]
        [InlineData(170f)]
        [InlineData(-100f)]
        [InlineData(-170f)]
        public void Calculate_YawBehindCamera_ReturnsCenteredOffset(float yaw)
        {
            ScreenOffsetCalculator.Calculate(
                yaw, 0f, 0f, 90f, 50.625f, 1920f, 1080f, 1f,
                out float x, out float y);

            // The old |az| guard let a negative az through and flipped the divide, so a
            // +100 yaw reported a POSITIVE x - the mirrored side of the screen.
            Assert.Equal(0f, x);
            Assert.Equal(0f, y);
        }

        [Fact]
        public void Calculate_YawEitherSideOfNinety_HasNoSignDiscontinuity()
        {
            ScreenOffsetCalculator.Calculate(89.9f, 0f, 0f, 90f, 50.625f, 1920f, 1080f, 1f,
                out float justUnder, out _);
            ScreenOffsetCalculator.Calculate(90.1f, 0f, 0f, 90f, 50.625f, 1920f, 1080f, 1f,
                out float justOver, out _);

            Assert.True(justUnder < 0f, "yaw right stays negative up to the singularity");
            Assert.Equal(0f, justOver);
        }

        [Fact]
        public void CalculatePrecomputed_YawBehindCamera_ReturnsCenteredOffset()
        {
            ScreenOffsetCalculator.CalculatePrecomputed(
                120f, 0f, 0f, 1f, 1f, 960f, 540f, 1f,
                out float x, out float y);

            Assert.Equal(0f, x);
            Assert.Equal(0f, y);
        }

        [Fact]
        public void Calculate_NormalYaw_StillProducesOffset()
        {
            // Guard against the fix over-rejecting the ordinary case.
            ScreenOffsetCalculator.Calculate(15f, 0f, 0f, 90f, 50.625f, 1920f, 1080f, 1f,
                out float x, out _);
            Assert.True(x < 0f);
        }

        // ---- SmoothAngle / interpolation across the +/-180 seam ----

        [Fact]
        public void SmoothAngle_AcrossSeam_TakesShortestArc()
        {
            float result = SmoothingUtils.SmoothAngle(179.5f, -179.5f, 0f, DeltaTime60Fps);

            // A 1 degree movement. The plain scalar lerp travelled -359 degrees and
            // landed around -23; the shortest arc stays hard against the seam.
            Assert.True(System.Math.Abs(AngleUtils.ShortestAngleDelta(179.5f, result)) <= 1.0f,
                "smoothed value moved more than the 1 degree that separates the inputs, got " +
                result.ToString(CultureInfo.InvariantCulture));
        }

        [Fact]
        public void SmoothAngle_NoWrap_MatchesPlainSmooth()
        {
            // Away from the seam the two forms must be identical, or this is a
            // behaviour change for every existing mod rather than a bug fix.
            for (float target = -80f; target <= 80f; target += 10f)
            {
                float plain = SmoothingUtils.Smooth(10f, target, 0.3f, DeltaTime60Fps);
                float wrapped = SmoothingUtils.SmoothAngle(10f, target, 0.3f, DeltaTime60Fps);
                Assert.Equal(plain, wrapped, precision: 4);
            }
        }

        [Fact]
        public void PoseInterpolator_AcrossSeam_DoesNotSweepTheLongWay()
        {
            var interpolator = new PoseInterpolator();
            long t0 = 1000;

            interpolator.Update(new TrackingPose(179f, 0f, 0f, t0), DeltaTime60Fps);
            interpolator.Update(new TrackingPose(179f, 0f, 0f, t0), DeltaTime60Fps);
            var result = interpolator.Update(new TrackingPose(-179f, 0f, 0f, t0 + 1000), DeltaTime60Fps);

            // Every output must stay within the 2 degree arc between the samples.
            float fromFirst = System.Math.Abs(AngleUtils.ShortestAngleDelta(179f, result.Yaw));
            Assert.True(fromFirst <= 3f,
                "interpolated yaw left the short arc: " + result.Yaw.ToString(CultureInfo.InvariantCulture));
        }

        // ---- HotkeyHandler: a clock that restarts must not kill the hotkey ----

        [Fact]
        public void HotkeyHandler_ClockGoesBackwards_StillFires()
        {
            int toggles = 0;
            var handler = new HotkeyHandler(_ => true);
            handler.SetToggleKey(1);
            handler.OnToggled += _ => toggles++;

            handler.Update(360f);
            Assert.Equal(1, toggles);

            // Caller switched to a level-relative clock that just restarted.
            handler.Update(0f);
            Assert.Equal(2, toggles);
        }

        [Fact]
        public void HotkeyHandler_WithinCooldown_StillSuppressed()
        {
            int toggles = 0;
            var handler = new HotkeyHandler(_ => true);
            handler.SetToggleKey(1);
            handler.OnToggled += _ => toggles++;

            handler.Update(10f);
            handler.Update(10.05f);
            Assert.Equal(1, toggles);
        }

        // ---- PerformanceMonitor: a negative sample must not desync the count ----

        [Fact]
        public void PerformanceMonitor_NegativeSample_DoesNotStallTheAverage()
        {
            var monitor = new PerformanceMonitor(1);

            monitor.RecordTicks(-4000);
            monitor.RecordTicks(5000);

            Assert.True(monitor.AverageMicroseconds > 0.0,
                "a valid sample after a negative one must still be averaged");
        }

        // ---- Config boundary: port range and invariant int parsing ----

        [Theory]
        [InlineData("70000")]
        [InlineData("0")]
        [InlineData("80")]
        [InlineData("-1")]
        public void ApplyValues_OutOfRangePort_KeepsDefault(string port)
        {
            var config = new HeadTrackingConfigData();
            int original = config.UdpPort;

            config.ApplyValues(new System.Collections.Generic.Dictionary<string, string>
            {
                { "port", port }
            });

            Assert.Equal(original, config.UdpPort);
        }

        [Fact]
        public void ApplyValues_InRangePort_IsApplied()
        {
            var config = new HeadTrackingConfigData();
            config.ApplyValues(new System.Collections.Generic.Dictionary<string, string>
            {
                { "port", "5555" }
            });

            Assert.Equal(5555, config.UdpPort);
        }

        // ---- ProfileSerializer ----

        [Fact]
        public void Serialize_NewlineInDescription_DoesNotTruncateOrInject()
        {
            var profile = new ConfigProfile("Test", "first line", "General");
            profile.Description = "Aggressive yaw.\nUse with 90 FOV.";
            profile.SetSetting("Note", "x\nIsReadOnly=True");

            var restored = ProfileSerializer.Deserialize(ProfileSerializer.Serialize(profile));

            Assert.False(restored.IsReadOnly, "a setting value must not be able to inject metadata");
            Assert.Contains("90 FOV", restored.Description);
        }

        [Fact]
        public void Deserialize_OutOfRangeEnum_IsRejected()
        {
            var profile = new ConfigProfile("Test", "d", "General");
            profile.AxisMapping.YawConfig.Source = AxisSource.Yaw;

            string text = ProfileSerializer.Serialize(profile);
            text = text.Replace("AxisMapping.Yaw.Source=Yaw", "AxisMapping.Yaw.Source=99");

            var restored = ProfileSerializer.Deserialize(text);

            // Enum.Parse happily returns (AxisSource)99, which silently kills the axis.
            Assert.True(Enum.IsDefined(typeof(AxisSource), restored.AxisMapping.YawConfig.Source));
        }

        [Fact]
        public void RoundTrip_PreservesMaxInputRange()
        {
            var profile = new ConfigProfile("Test", "d", "General");
            profile.AxisMapping.YawConfig.MaxInputRange = 45f;

            var restored = ProfileSerializer.Deserialize(ProfileSerializer.Serialize(profile));

            Assert.Equal(45f, restored.AxisMapping.YawConfig.MaxInputRange, precision: 3);
        }

        [Fact]
        public void RoundTrip_IntSettingKeepsItsType()
        {
            var profile = new ConfigProfile("Test", "d", "General");
            profile.SetSetting("YawTrimDegrees", -5);

            var restored = ProfileSerializer.Deserialize(ProfileSerializer.Serialize(profile));

            Assert.Equal(-5, restored.GetSetting<int>("YawTrimDegrees", 0));
        }

        // ---- ProfileManager name validation ----

        [Fact]
        public void CreateProfile_TraversingName_IsRejectedBeforeMutatingState()
        {
            string dir = NewTempDir();
            try
            {
                var manager = new ProfileManager(dir);

                Assert.Throws<ArgumentException>(() =>
                    manager.CreateProfile("../../evil", "d"));

                Assert.False(manager.ProfileExists("../../evil"),
                    "a rejected name must not be left in the in-memory list");
            }
            finally { Cleanup(dir); }
        }

        [Fact]
        public void CreateProfile_NameWithInvalidChar_IsRejected()
        {
            string dir = NewTempDir();
            try
            {
                var manager = new ProfileManager(dir);
                Assert.Throws<ArgumentException>(() =>
                    manager.CreateProfile("Sniper: Long Range", "d"));
            }
            finally { Cleanup(dir); }
        }

        [Fact]
        public void CreateProfile_OrdinaryNameWithSpaces_IsAccepted()
        {
            // The validation must not reject names that already work in shipped mods.
            string dir = NewTempDir();
            try
            {
                var manager = new ProfileManager(dir);
                var profile = manager.CreateProfile("My Sniper Setup", "d");
                Assert.Equal("My Sniper Setup", profile.Name);
                Assert.True(manager.ProfileExists("My Sniper Setup"));
            }
            finally { Cleanup(dir); }
        }

        [Fact]
        public void LoadAllProfiles_RenamedFile_DoesNotForkIntoTwoEntries()
        {
            string dir = NewTempDir();
            try
            {
                var manager = new ProfileManager(dir);
                manager.CreateProfile("Original", "d");

                File.Move(Path.Combine(dir, "Original.profile"), Path.Combine(dir, "Renamed.profile"));

                var reloaded = new ProfileManager(dir);
                reloaded.LoadAllProfiles();
                int before = reloaded.GetProfileNames().Count;
                reloaded.LoadProfile("Renamed");
                reloaded.SaveProfile(reloaded.ActiveProfile);

                // Before the fix, Name stayed "Original" while the entry was keyed
                // "Renamed", so saving forked it into a second list entry backed by the
                // old filename and every later save went to the wrong file.
                Assert.Equal(before, reloaded.GetProfileNames().Count);
                Assert.False(File.Exists(Path.Combine(dir, "Original.profile")));
                Assert.True(File.Exists(Path.Combine(dir, "Renamed.profile")));
            }
            finally { Cleanup(dir); }
        }

        [Fact]
        public void SaveProfile_LeavesNoStrayTempFile()
        {
            string dir = NewTempDir();
            try
            {
                var manager = new ProfileManager(dir);
                manager.CreateProfile("Atomic", "d");

                // The write-then-rename must not leave the intermediate behind, and the
                // *.profile glob must not pick one up if it ever did.
                Assert.Empty(Directory.GetFiles(dir, "*.tmp"));
                Assert.Contains(Directory.GetFiles(dir, "*.profile"),
                    f => Path.GetFileName(f) == "Atomic.profile");
            }
            finally { Cleanup(dir); }
        }

        private static string NewTempDir()
        {
            string dir = Path.Combine(Path.GetTempPath(),
                "cultests-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(dir);
            return dir;
        }

        private static void Cleanup(string dir)
        {
            try { Directory.Delete(dir, true); }
            catch (IOException) { }
        }
    }
}
