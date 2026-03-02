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
        public void SameTimestamp_ReturnsRawPose_NoExtrapolation()
        {
            var interp = new PoseInterpolator();
            var pose = MakePose(10f, 20f, 5f, 1000);

            // First call establishes the sample
            interp.Update(pose, DeltaTime);

            // Second call with same timestamp — no velocity yet, returns raw
            var result = interp.Update(pose, DeltaTime);

            Assert.Equal(10f, result.Yaw);
            Assert.Equal(20f, result.Pitch);
            Assert.Equal(5f, result.Roll);
        }

        [Fact]
        public void AfterTwoSamples_ExtrapolatesBetweenThem()
        {
            var interp = new PoseInterpolator();

            // First sample: yaw=0
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);

            // Simulate 4 frames passing at 120Hz (33ms total ≈ one 30Hz interval)
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime); // same timestamp, accumulates time
            }

            // Second sample: yaw moved to 10 over ~33ms
            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Now on the next frame (same timestamp as pose2), extrapolation should kick in
            var result = interp.Update(pose2, DeltaTime);

            // Should have extrapolated yaw beyond 10 (velocity is positive)
            Assert.True(result.Yaw > 10f, $"Expected extrapolated yaw > 10, got {result.Yaw}");
        }

        [Fact]
        public void Extrapolation_CapsAtMaxTime()
        {
            var interp = new PoseInterpolator { MaxExtrapolationTime = 0.05f };

            // Establish velocity
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);

            // 4 frames pass
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime);
            }

            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Let a LOT of time pass with same timestamp (well beyond max extrapolation)
            float resultAtMedium = 0f;
            float resultAtLong = 0f;

            // Accumulate to just beyond max extrapolation
            for (int i = 0; i < 10; i++)
            {
                var r = interp.Update(pose2, DeltaTime);
                resultAtMedium = r.Yaw;
            }

            // Accumulate way beyond max extrapolation
            for (int i = 0; i < 100; i++)
            {
                var r = interp.Update(pose2, DeltaTime);
                resultAtLong = r.Yaw;
            }

            // Both should be capped — the long result should equal the medium result
            // (extrapolation time is clamped, so no further movement)
            Assert.Equal(resultAtMedium, resultAtLong, precision: 3);
        }

        [Fact]
        public void Reset_ClearsVelocityState()
        {
            var interp = new PoseInterpolator();

            // Establish velocity
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

            // After reset, first sample should return raw (no velocity)
            var pose3 = MakePose(5f, 5f, 5f, 3000);
            var result = interp.Update(pose3, DeltaTime);

            Assert.Equal(5f, result.Yaw);
            Assert.Equal(5f, result.Pitch);
            Assert.Equal(5f, result.Roll);

            // And subsequent same-timestamp frame should NOT extrapolate (no velocity yet)
            var result2 = interp.Update(pose3, DeltaTime);
            Assert.Equal(5f, result2.Yaw);
        }

        [Fact]
        public void HighFrequencyInput_NearZeroExtrapolation()
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

            // With a new sample every frame, the result should be exactly the raw pose
            // because we never hit the "stale timestamp" branch
            Assert.Equal(yaw, result.Yaw, precision: 5);
        }
    }
}
