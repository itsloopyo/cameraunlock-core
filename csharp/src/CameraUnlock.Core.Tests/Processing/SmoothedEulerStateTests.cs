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
        public void Update_ZeroSmoothing_FirstFrameReturnsTargetExactly()
        {
            var state = new SmoothedEulerState();

            state.Update(10f, -5f, 3f, 0f, DeltaTime,
                out float yaw, out float pitch, out float roll);

            Assert.Equal(10f, yaw, precision: 5);
            Assert.Equal(-5f, pitch, precision: 5);
            Assert.Equal(3f, roll, precision: 5);
        }

        // Replaces Update_ZeroSmoothing_DoesNotRetainState, which pinned the snap-and-clear
        // branch. That branch was unreachable while GetEffectiveSmoothing floored at 0.15;
        // with the floor gone and LocalSmoothing defaulting to 0.0 it would have become the
        // default path for every local user, so it was removed to match the behaviour of
        // SmoothedRotationState and the C++ CalculateSmoothingFactor.
        [Fact]
        public void Update_ZeroSmoothing_RetainsStateAndInterpolates()
        {
            var state = new SmoothedEulerState();

            state.Update(0f, 0f, 0f, 0f, DeltaTime,
                out _, out _, out _);

            state.Update(30f, 0f, 0f, 0f, DeltaTime,
                out float yaw, out _, out _);

            Assert.True(yaw > 0f, "Zero smoothing must still move toward the target");
            Assert.True(yaw < 30f,
                $"Zero smoothing must interpolate rather than snap, got {yaw}");
        }

        [Fact]
        public void Update_ZeroSmoothing_ThenSmoothed_ContinuesFromRetainedState()
        {
            var state = new SmoothedEulerState();

            state.Update(0f, 0f, 0f, 0f, DeltaTime,
                out _, out _, out _);
            state.Update(10f, 0f, 0f, 0f, DeltaTime,
                out float afterZero, out _, out _);

            // Switching to a smoothed value must blend on from where zero smoothing left
            // off, not re-initialize. Under the old snap-and-clear branch this returned
            // the target exactly.
            state.Update(20f, 0f, 0f, 0.5f, DeltaTime,
                out float yaw, out _, out _);

            Assert.NotEqual(20f, yaw, precision: 3);
            Assert.True(yaw > afterZero, "Must keep moving toward the new target");
            Assert.True(yaw < 20f, "Must not snap to the new target");
        }

        // Zero smoothing must converge to the target, just not in one frame. This is the
        // guarantee that removing the snap does not add permanent lag.
        [Fact]
        public void Update_ZeroSmoothing_ConvergesToTarget()
        {
            var state = new SmoothedEulerState();

            state.Update(0f, 0f, 0f, 0f, DeltaTime, out _, out _, out _);

            float yaw = 0f;
            for (int i = 0; i < 60; i++)
            {
                state.Update(30f, 0f, 0f, 0f, DeltaTime, out yaw, out _, out _);
            }

            Assert.InRange(yaw, 29.9f, 30.1f);
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

            // Single step toward 30° yaw - should be between 0 and 30
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
        public void Update_RemoteDefaultSmoothing_Interpolates()
        {
            // The 0.15 that used to be the hidden floor is now the default of
            // RemoteSmoothing, and at that value the state must still interpolate.
            var state = new SmoothedEulerState();

            // Initialize at 0
            state.Update(0f, 0f, 0f, SmoothingUtils.DefaultRemoteSmoothing, DeltaTime,
                out _, out _, out _);

            state.Update(30f, 0f, 0f, SmoothingUtils.DefaultRemoteSmoothing, DeltaTime,
                out float yaw, out _, out _);

            Assert.True(yaw > 0f, "Should have moved toward target");
            Assert.True(yaw < 30f, "Remote default smoothing should prevent instant snap");
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
