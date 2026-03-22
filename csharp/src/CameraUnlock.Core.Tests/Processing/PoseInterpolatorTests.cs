using System.Diagnostics;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    public class PoseInterpolatorTests
    {
        private const float DeltaTime = 1f / 120f; // 120Hz frame rate

        private static TrackingPose MakePose(float yaw, float pitch, float roll, long timestamp)
        {
            return new TrackingPose(yaw, pitch, roll, timestamp);
        }

        [Fact]
        public void FirstSample_ReturnsRawPose()
        {
            var interp = new PoseInterpolator();
            var pose = MakePose(10f, 20f, 5f, 1000);

            var result = interp.Update(pose, DeltaTime);

            Assert.Equal(10f, result.Yaw);
            Assert.Equal(20f, result.Pitch);
            Assert.Equal(5f, result.Roll);
        }

        [Fact]
        public void InvalidPose_ReturnsUnmodified()
        {
            var interp = new PoseInterpolator();
            var invalid = new TrackingPose(10f, 20f, 5f, 0);

            var result = interp.Update(invalid, DeltaTime);

            Assert.Equal(10f, result.Yaw);
            Assert.Equal(20f, result.Pitch);
            Assert.Equal(5f, result.Roll);
            Assert.Equal(0, result.TimestampTicks);
        }

        [Fact]
        public void SameTimestamp_OutputsSamePose()
        {
            var interp = new PoseInterpolator();
            var pose = MakePose(10f, 20f, 5f, 1000);

            // First call establishes the sample
            interp.Update(pose, DeltaTime);

            // Second call with same timestamp — from=to, so output stays the same
            var result = interp.Update(pose, DeltaTime);

            Assert.Equal(10f, result.Yaw, precision: 4);
            Assert.Equal(20f, result.Pitch, precision: 4);
            Assert.Equal(5f, result.Roll, precision: 4);
        }

        [Fact]
        public void AfterTwoSamples_InterpolatesBetweenThem()
        {
            var interp = new PoseInterpolator();

            // First sample: yaw=0
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);

            // Simulate 3 frames passing at 120Hz
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime);
            }

            // Second sample: yaw moved to 10
            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Next frame — should be interpolating between pose1 and pose2
            var result = interp.Update(pose2, DeltaTime);

            Assert.True(result.Yaw > 0f && result.Yaw < 10f,
                $"Expected interpolated yaw between 0 and 10, got {result.Yaw}");
        }

        [Fact]
        public void InterpolationHoldsAtTarget_WhenNoNewSample()
        {
            var interp = new PoseInterpolator();

            // Establish two samples
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime);
            }

            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Run many frames without new sample — should converge to pose2 and hold
            float lastYaw = 0f;
            for (int i = 0; i < 100; i++)
            {
                var r = interp.Update(pose2, DeltaTime);
                lastYaw = r.Yaw;
            }

            // Should be exactly at pose2, not beyond it
            Assert.Equal(10f, lastYaw, precision: 3);
        }

        [Fact]
        public void Reset_ClearsState()
        {
            var interp = new PoseInterpolator();

            // Establish some state
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime);
            }
            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Reset
            interp.Reset();

            // After reset, first sample should return raw
            var pose3 = MakePose(5f, 5f, 5f, 3000);
            var result = interp.Update(pose3, DeltaTime);

            Assert.Equal(5f, result.Yaw);
            Assert.Equal(5f, result.Pitch);
            Assert.Equal(5f, result.Roll);

            // Subsequent same-timestamp frame should stay at same pose (from=to)
            var result2 = interp.Update(pose3, DeltaTime);
            Assert.Equal(5f, result2.Yaw, precision: 4);
        }

        [Fact]
        public void HighFrequencyInput_PassesThrough()
        {
            var interp = new PoseInterpolator();

            // Simulate new sample every frame (like a local tracker at 120Hz)
            float yaw = 0f;
            TrackingPose result = default;
            for (int i = 0; i < 20; i++)
            {
                yaw += 0.5f; // steady movement
                var pose = MakePose(yaw, 0f, 0f, 1000 + i); // unique timestamp each frame
                result = interp.Update(pose, DeltaTime);
            }

            // With a new sample every frame, output should match the raw pose
            Assert.Equal(yaw, result.Yaw, precision: 5);
        }

        [Fact]
        public void SteadyMotion_ProducesLinearOutput()
        {
            var interp = new PoseInterpolator();

            // First sample at yaw=0
            interp.Update(MakePose(0f, 0f, 0f, 1000), DeltaTime);
            // 3 stale frames
            for (int i = 0; i < 3; i++)
                interp.Update(MakePose(0f, 0f, 0f, 1000), DeltaTime);

            // Second sample at yaw=10 — establishes interval
            interp.Update(MakePose(10f, 0f, 0f, 2000), DeltaTime);
            // 3 stale frames (interpolating toward 10)
            for (int i = 0; i < 3; i++)
                interp.Update(MakePose(10f, 0f, 0f, 2000), DeltaTime);

            // Third sample at yaw=20 — now we can check linearity
            interp.Update(MakePose(20f, 0f, 0f, 3000), DeltaTime);

            // Collect interpolated values for the next segment
            float prev = 0f;
            float firstDelta = 0f;
            bool isLinear = true;
            for (int i = 0; i < 3; i++)
            {
                var r = interp.Update(MakePose(20f, 0f, 0f, 3000), DeltaTime);
                if (i == 0)
                {
                    firstDelta = r.Yaw - 10f; // should be positive
                }
                else
                {
                    float delta = r.Yaw - prev;
                    // Each step should be approximately equal (linear)
                    if (System.Math.Abs(delta - firstDelta) > 0.5f)
                        isLinear = false;
                }
                prev = r.Yaw;
            }

            Assert.True(isLinear, "Interpolation should produce approximately linear output between samples");
        }
    }
}
