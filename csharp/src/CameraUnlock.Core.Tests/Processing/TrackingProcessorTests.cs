using System.Diagnostics;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    public class TrackingProcessorTests
    {
        private const float DeltaTime = 1f / 60f;

        [Fact]
        public void DefaultSettings_AreCorrect()
        {
            var processor = new TrackingProcessor();

            Assert.Equal(SensitivitySettings.Default, processor.Sensitivity);
            Assert.Equal(DeadzoneSettings.None, processor.Deadzone);
            Assert.Equal(0f, processor.SmoothingFactor);
        }

        [Fact]
        public void CenterManager_IsAccessible()
        {
            var processor = new TrackingProcessor();
            Assert.NotNull(processor.CenterManager);
        }

        [Fact]
        public void Process_InvalidPose_ReturnsUnmodified()
        {
            var processor = new TrackingProcessor();
            var invalidPose = new TrackingPose(10f, 20f, 30f, 0);

            TrackingPose result = processor.Process(invalidPose, false, DeltaTime);

            Assert.Equal(10f, result.Yaw);
            Assert.Equal(20f, result.Pitch);
            Assert.Equal(30f, result.Roll);
        }

        [Fact]
        public void Process_ValidPose_DefaultSettings_ReturnsClampedValues()
        {
            var processor = new TrackingProcessor();
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 15f, timestamp);

            TrackingPose result = processor.Process(pose, false, DeltaTime);

            Assert.Equal(10f, result.Yaw, precision: 4);
            Assert.Equal(20f, result.Pitch, precision: 4);
            Assert.Equal(15f, result.Roll, precision: 4);
        }

        [Fact]
        public void Process_WithSensitivity_ScalesOutput()
        {
            var processor = new TrackingProcessor
            {
                Sensitivity = new SensitivitySettings(2f, 0.5f, 1f, false, false, false)
            };
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 15f, timestamp);

            TrackingPose result = processor.Process(pose, false, DeltaTime);

            Assert.Equal(20f, result.Yaw, precision: 4);
            Assert.Equal(10f, result.Pitch, precision: 4);
            Assert.Equal(15f, result.Roll, precision: 4);
        }

        [Fact]
        public void Process_WithInversion_InvertsAxes()
        {
            var processor = new TrackingProcessor
            {
                Sensitivity = new SensitivitySettings(1f, 1f, 1f, true, true, true)
            };
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 15f, timestamp);

            TrackingPose result = processor.Process(pose, false, DeltaTime);

            Assert.Equal(-10f, result.Yaw, precision: 4);
            Assert.Equal(-20f, result.Pitch, precision: 4);
            Assert.Equal(-15f, result.Roll, precision: 4);
        }

        [Fact]
        public void Process_WithDeadzone_AppliesDeadzone()
        {
            var processor = new TrackingProcessor
            {
                Deadzone = new DeadzoneSettings(5f, 5f, 5f)
            };
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(3f, 10f, 6f, timestamp);

            TrackingPose result = processor.Process(pose, false, DeltaTime);

            Assert.Equal(0f, result.Yaw, precision: 4);
            Assert.Equal(5f, result.Pitch, precision: 4);
            Assert.Equal(1f, result.Roll, precision: 4);
        }

        [Fact]
        public void Process_WithCenter_SubtractsOffset()
        {
            var processor = new TrackingProcessor();
            // Use single-axis center where quaternion and Euler subtraction agree exactly
            processor.CenterManager.SetCenter(10f, 0f, 0f);

            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(30f, 15f, 5f, timestamp);

            TrackingPose result = processor.Process(pose, false, DeltaTime);

            Assert.Equal(20f, result.Yaw, precision: 3);
            Assert.Equal(15f, result.Pitch, precision: 3);
            Assert.Equal(5f, result.Roll, precision: 3);
        }

        [Fact]
        public void GetSmoothedRotation_ReturnsCurrentSmoothedValues()
        {
            var processor = new TrackingProcessor();
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 30f, timestamp);

            processor.Process(pose, false, DeltaTime);
            processor.GetSmoothedRotation(out float yaw, out float pitch, out float roll);

            Assert.True(yaw >= 0);
        }

        [Fact]
        public void Recenter_SetsCenterFromSmoothedValues()
        {
            var processor = new TrackingProcessor();
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 30f, timestamp);

            processor.Process(pose, false, DeltaTime);
            processor.Recenter();

            Assert.True(processor.CenterManager.HasValidCenter);
        }

        [Fact]
        public void RecenterTo_SetsCenterAndResetsSmoothing()
        {
            var processor = new TrackingProcessor();
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            processor.RecenterTo(pose);

            Assert.True(processor.CenterManager.HasValidCenter);
            Assert.Equal(10f, processor.CenterManager.CenterOffset.Yaw);
        }

        [Fact]
        public void Process_AtExtremeYaw_PitchDoesNotContaminateRoll()
        {
            var processor = new TrackingProcessor();
            // Center at 80° yaw
            processor.CenterManager.SetCenter(80f, 0f, 0f);

            long timestamp = Stopwatch.GetTimestamp();
            // Input at 80° yaw + 15° pitch — relative is ~15° pitch only
            var pose = new TrackingPose(80f, 15f, 0f, timestamp);

            TrackingPose result = processor.Process(pose, false, DeltaTime);

            // The key assertion: roll should be near zero, not contaminated by cross-axis leakage
            Assert.True(System.Math.Abs(result.Roll) < 2f,
                $"Roll contamination detected: expected ~0, got {result.Roll}");
            Assert.True(System.Math.Abs(result.Pitch - 15f) < 2f,
                $"Pitch not preserved: expected ~15, got {result.Pitch}");
        }

        [Fact]
        public void Reset_ClearsState()
        {
            var processor = new TrackingProcessor();
            long timestamp = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 30f, timestamp);

            processor.Process(pose, false, DeltaTime);
            processor.CenterManager.SetCenter(5f, 5f, 5f);
            processor.Reset();

            Assert.False(processor.CenterManager.HasValidCenter);
        }
    }
}
