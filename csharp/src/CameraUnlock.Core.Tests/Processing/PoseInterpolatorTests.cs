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
        public void ExtrapolationIsBounded_WhenNoNewSample()
        {
            var interp = new PoseInterpolator();

            // Establish two samples: 0 → 10 yaw
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime);
            }

            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Run many frames without new sample — should extrapolate then cap
            float lastYaw = 0f;
            for (int i = 0; i < 100; i++)
            {
                var r = interp.Update(pose2, DeltaTime);
                lastYaw = r.Yaw;
            }

            // With MaxExtrapolationFraction=0.5, the output should be
            // from + (to - from) * 1.5 = 0 + 10 * 1.5 = 15
            // (extrapolates half a sample period beyond the target, then caps)
            Assert.Equal(15f, lastYaw, precision: 1);
        }

        [Fact]
        public void NoExtrapolation_HoldsAtTarget()
        {
            var interp = new PoseInterpolator { MaxExtrapolationFraction = 0f };

            // Establish two samples
            var pose1 = MakePose(0f, 0f, 0f, 1000);
            interp.Update(pose1, DeltaTime);
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pose1, DeltaTime);
            }

            var pose2 = MakePose(10f, 0f, 0f, 2000);
            interp.Update(pose2, DeltaTime);

            // Run many frames without new sample — should hold at target
            float lastYaw = 0f;
            for (int i = 0; i < 100; i++)
            {
                var r = interp.Update(pose2, DeltaTime);
                lastYaw = r.Yaw;
            }

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
            // Collect 4 frames (isNewSample + 3 stale) and verify constant frame-to-frame deltas
            var values = new float[4];
            values[0] = interp.Update(MakePose(20f, 0f, 0f, 3000), DeltaTime).Yaw;
            for (int i = 1; i < 4; i++)
                values[i] = interp.Update(MakePose(20f, 0f, 0f, 3000), DeltaTime).Yaw;

            // All frame-to-frame deltas should be approximately equal (linear interpolation)
            float refDelta = values[1] - values[0];
            Assert.True(refDelta > 0f, $"Expected positive delta, got {refDelta}");
            for (int i = 2; i < 4; i++)
            {
                float delta = values[i] - values[i - 1];
                Assert.True(System.Math.Abs(delta - refDelta) < 0.5f,
                    $"Frame deltas should be equal: {refDelta} vs {delta} at frame {i}");
            }
        }
    }
}
