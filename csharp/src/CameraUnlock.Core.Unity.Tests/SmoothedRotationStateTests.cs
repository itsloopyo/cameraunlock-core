using Xunit;
using UnityEngine;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Unity.Tracking;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// Pins the post-migration contract of <see cref="SmoothedRotationState"/>.
    ///
    /// The method kept its exact signature while its meaning changed: the float used to be
    /// the RAW user smoothing value, which the class floored internally and snapped below
    /// 0.001, and it is now the ALREADY-SELECTED effective value, passed through untouched.
    /// Every mod call site compiled unchanged and behaved differently, which is why the
    /// method has been renamed - and why it now needs coverage that actually executes.
    /// </summary>
    public class SmoothedRotationStateTests
    {
        public SmoothedRotationStateTests()
        {
            Time.Reset();
        }

        private static Quaternion Yaw(float degrees)
        {
            return Quaternion.Euler(0f, degrees, 0f);
        }

        [Fact]
        public void FirstUpdate_InitializesToTargetExactly()
        {
            var state = new SmoothedRotationState();

            Quaternion result = state.UpdateWithEffectiveSmoothing(Yaw(30f), 0.5f);

            Assert.True(state.IsInitialized);
            Assert.True(Quaternion.Angle(result, Yaw(30f)) < 0.1f);
        }

        // The snap branch was deliberately removed. With LocalSmoothing defaulting to 0.0
        // a snap here would leave every local user with raw stepped output on a
        // high-refresh display, which is the exact failure the migration set out to fix.
        [Fact]
        public void ZeroSmoothing_StillInterpolatesRatherThanSnapping()
        {
            var state = new SmoothedRotationState();
            state.UpdateWithEffectiveSmoothing(Yaw(0f), 0f);

            Quaternion result = state.UpdateWithEffectiveSmoothing(Yaw(30f), 0f);

            float toStart = Quaternion.Angle(result, Yaw(0f));
            float toTarget = Quaternion.Angle(result, Yaw(30f));

            Assert.True(toStart > 0.5f, "must have moved off the starting rotation");
            Assert.True(toTarget > 0.5f, $"must not have snapped to the target, gap was {toTarget}");
        }

        [Fact]
        public void ZeroSmoothing_ConvergesToTarget()
        {
            var state = new SmoothedRotationState();
            state.UpdateWithEffectiveSmoothing(Yaw(0f), 0f);

            Quaternion result = Quaternion.identity;
            for (int i = 0; i < 60; i++)
            {
                result = state.UpdateWithEffectiveSmoothing(Yaw(30f), 0f);
            }

            Assert.True(Quaternion.Angle(result, Yaw(30f)) < 0.1f);
        }

        [Fact]
        public void ZeroSmoothing_RetainsStateAcrossACallWithSmoothing()
        {
            var state = new SmoothedRotationState();
            state.UpdateWithEffectiveSmoothing(Yaw(0f), 0f);
            state.UpdateWithEffectiveSmoothing(Yaw(10f), 0f);

            // Under the old snap-and-clear branch this re-initialized and returned the
            // target exactly.
            Quaternion result = state.UpdateWithEffectiveSmoothing(Yaw(40f), 0.5f);

            Assert.True(Quaternion.Angle(result, Yaw(40f)) > 0.5f,
                "must blend on from retained state, not re-initialize");
        }

        // The value is passed through untouched. This is what makes the two-parameter
        // model work: the caller selects with GetEffectiveSmoothing and this class never
        // second-guesses it.
        [Fact]
        public void HeavierSmoothing_MovesLessPerFrame()
        {
            var light = new SmoothedRotationState();
            light.UpdateWithEffectiveSmoothing(Yaw(0f), 0f);
            Quaternion lightResult = light.UpdateWithEffectiveSmoothing(Yaw(30f), 0f);

            var heavy = new SmoothedRotationState();
            heavy.UpdateWithEffectiveSmoothing(Yaw(0f), 0.95f);
            Quaternion heavyResult = heavy.UpdateWithEffectiveSmoothing(Yaw(30f), 0.95f);

            float lightStep = Quaternion.Angle(lightResult, Yaw(0f));
            float heavyStep = Quaternion.Angle(heavyResult, Yaw(0f));

            Assert.True(lightStep > heavyStep,
                $"light={lightStep} must move further than heavy={heavyStep}");
        }

        [Fact]
        public void RemoteDefaultSmoothing_Interpolates()
        {
            var state = new SmoothedRotationState();
            state.UpdateWithEffectiveSmoothing(Yaw(0f), SmoothingUtils.DefaultRemoteSmoothing);

            Quaternion result = state.UpdateWithEffectiveSmoothing(
                Yaw(30f), SmoothingUtils.DefaultRemoteSmoothing);

            Assert.True(Quaternion.Angle(result, Yaw(0f)) > 0.5f, "must move toward the target");
            Assert.True(Quaternion.Angle(result, Yaw(30f)) > 0.5f, "must not snap to the target");
        }

        [Fact]
        public void Reset_ClearsInitialization()
        {
            var state = new SmoothedRotationState();
            state.UpdateWithEffectiveSmoothing(Yaw(30f), 0.5f);

            state.Reset();

            Assert.False(state.IsInitialized);
            Quaternion result = state.UpdateWithEffectiveSmoothing(Yaw(45f), 0.5f);
            Assert.True(Quaternion.Angle(result, Yaw(45f)) < 0.1f,
                "after Reset the next update initializes to the target");
        }

        [Fact]
        public void ResetToRotation_SeedsStateWithoutReinitializing()
        {
            var state = new SmoothedRotationState();

            state.Reset(Yaw(90f));

            Assert.True(state.IsInitialized);
            Assert.True(Quaternion.Angle(state.Current, Yaw(90f)) < 0.1f);

            Quaternion result = state.UpdateWithEffectiveSmoothing(Yaw(0f), 0.5f);
            Assert.True(Quaternion.Angle(result, Yaw(0f)) > 0.5f,
                "must blend from the seeded rotation rather than snapping");
        }
    }
}
