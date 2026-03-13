using Xunit;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Tests.Processing
{
    public class SmoothedEulerStateTests
    {
        private const float DeltaTime = 1f / 60f;
        private const float Epsilon = 0.01f;

        [Fact]
        public void Update_ZeroSmoothing_ReturnsTargetExactly()
        {
            var state = new SmoothedEulerState();

            state.Update(10f, -5f, 3f, 0f, DeltaTime,
                out float yaw, out float pitch, out float roll);

            Assert.Equal(10f, yaw, precision: 5);
            Assert.Equal(-5f, pitch, precision: 5);
            Assert.Equal(3f, roll, precision: 5);
        }

        [Fact]
        public void Update_ZeroSmoothing_DoesNotRetainState()
        {
            var state = new SmoothedEulerState();

            // First call with zero smoothing
            state.Update(10f, 0f, 0f, 0f, DeltaTime,
                out _, out _, out _);

            // Second call with smoothing — should NOT slerp from the zero-smoothing value
            // because zero-smoothing clears _initialized. This is the "first frame" path.
            state.Update(20f, 0f, 0f, 0.5f, DeltaTime,
                out float yaw, out _, out _);

            // First frame with smoothing returns target directly (initialization)
            Assert.Equal(20f, yaw, precision: 5);
        }

        [Fact]
        public void Update_IdentityPose_ReturnsZeros()
        {
            var state = new SmoothedEulerState();

            state.Update(0f, 0f, 0f, 0.5f, DeltaTime,
                out float yaw, out float pitch, out float roll);

            Assert.Equal(0f, yaw, precision: 5);
            Assert.Equal(0f, pitch, precision: 5);
            Assert.Equal(0f, roll, precision: 5);
        }

        [Fact]
        public void Update_FirstFrame_WithSmoothing_ReturnsTarget()
        {
            var state = new SmoothedEulerState();

            state.Update(15f, -10f, 5f, 0.5f, DeltaTime,
                out float yaw, out float pitch, out float roll);

            Assert.Equal(15f, yaw, precision: 5);
            Assert.Equal(-10f, pitch, precision: 5);
            Assert.Equal(5f, roll, precision: 5);
        }

        [Fact]
        public void Update_SubsequentFrames_SlerpConvergesToTarget()
        {
            var state = new SmoothedEulerState();

            // Initialize at 0
            state.Update(0f, 0f, 0f, 0.3f, DeltaTime,
                out _, out _, out _);

            // Step toward 30° yaw over many frames
            float lastYaw = 0f;
            for (int i = 0; i < 120; i++)
            {
                state.Update(30f, 0f, 0f, 0.3f, DeltaTime,
                    out lastYaw, out _, out _);
            }

            // After 2 seconds at 60fps with moderate smoothing, should be very close
            Assert.InRange(lastYaw, 29f, 31f);
        }

        [Fact]
        public void Update_SubsequentFrames_SlerpInterpolatesBetween()
        {
            var state = new SmoothedEulerState();

            // Initialize at 0
            state.Update(0f, 0f, 0f, 0.5f, DeltaTime,
                out _, out _, out _);

            // Single step toward 30° yaw — should be between 0 and 30
            state.Update(30f, 0f, 0f, 0.5f, DeltaTime,
                out float yaw, out _, out _);

            Assert.True(yaw > 0f, "Should have moved toward target");
            Assert.True(yaw < 30f, "Should not have reached target in one frame");
        }

        [Fact]
        public void Reset_ClearsState()
        {
            var state = new SmoothedEulerState();

            // Build up some smoothed state
            state.Update(0f, 0f, 0f, 0.5f, DeltaTime,
                out _, out _, out _);
            state.Update(30f, 0f, 0f, 0.5f, DeltaTime,
                out _, out _, out _);

            // Reset
            state.Reset();

            // After reset, next call should be treated as first frame
            state.Update(45f, 0f, 0f, 0.5f, DeltaTime,
                out float yaw, out _, out _);

            Assert.Equal(45f, yaw, precision: 5);
        }

        [Fact]
        public void Update_BaselineSmoothing_AlwaysApplied()
        {
            // Baseline smoothing (0.15) means smoothing=0 still interpolates
            var state = new SmoothedEulerState();

            // Initialize at 0
            state.Update(0f, 0f, 0f, SmoothingUtils.BaselineSmoothing, DeltaTime,
                out _, out _, out _);

            // Second frame: baseline floor should prevent instant snap
            state.Update(30f, 0f, 0f, SmoothingUtils.BaselineSmoothing, DeltaTime,
                out float yaw, out _, out _);

            Assert.True(yaw > 0f, "Should have moved toward target");
            Assert.True(yaw < 30f, "Baseline smoothing should prevent instant snap");
        }

        [Fact]
        public void Update_NegativeAngles_PreservedThroughSmoothing()
        {
            var state = new SmoothedEulerState();

            state.Update(-15f, -20f, -5f, 0f, DeltaTime,
                out float yaw, out float pitch, out float roll);

            Assert.Equal(-15f, yaw, precision: 5);
            Assert.Equal(-20f, pitch, precision: 5);
            Assert.Equal(-5f, roll, precision: 5);
        }
    }
}
