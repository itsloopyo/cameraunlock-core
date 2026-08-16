using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    /// <summary>
    /// Pins the fact that decided whether the two-parameter smoothing migration should
    /// also change <see cref="PoseInterpolator.MaxExtrapolationFraction"/>'s default.
    ///
    /// The concern was that the old hidden 0.15 baseline floor had been damping the
    /// interpolator's extrapolation overshoot, so removing the floor (LocalSmoothing now
    /// defaults to 0.0) would let the overshoot through and produce a wobble at the
    /// tracker's sample rate.
    ///
    /// It does not, and these tests are why. Extrapolation happens in the interpolator,
    /// UPSTREAM of the processor that applies smoothing, so the smoothing parameter cannot
    /// affect how far the interpolator predicts. And the smoothing stage was never the
    /// damping mechanism in the first place: the speed clamp at
    /// FrameInterpolationSpeed = 50 dominates at every smoothing value, so 0.0 and 0.15
    /// differ by about a tenth in per-frame response, not by a category.
    ///
    /// The default therefore stays at 0.5. If a specific mod shows a real wobble, that is
    /// a per-mod measurement and SetMaxExtrapolationFraction(0) already exists for it.
    /// </summary>
    public class ExtrapolationSmoothingIndependenceTests
    {
        private const float RenderDt = 1f / 165f;
        private const float SampleDt = 1f / 30f;
        private const float TurnRateDegPerSecond = 60f;

        /// Runs a constant-velocity head turn through interpolator + processor and returns
        /// how far past the newest REPORTED sample the interpolator's output ever gets.
        private static float PeakExtrapolationOvershoot(float smoothing, float maxExtrapolation)
        {
            var interpolator = new PoseInterpolator { MaxExtrapolationFraction = maxExtrapolation };
            var processor = new TrackingProcessor
            {
                LocalSmoothing = smoothing,
                RemoteSmoothing = smoothing,
            };

            float time = 0f;
            float nextSampleAt = 0f;
            float reportedYaw = 0f;
            long ticks = 1;
            float peak = 0f;

            for (int i = 0; i < 2000; i++)
            {
                if (time >= nextSampleAt)
                {
                    reportedYaw = TurnRateDegPerSecond * time;
                    nextSampleAt += SampleDt;
                    ticks++;
                }

                var raw = new TrackingPose(reportedYaw, 0f, 0f, ticks);
                TrackingPose interpolated = interpolator.Update(raw, RenderDt);
                processor.Process(interpolated, RenderDt);

                // Skip the warm-up while the sample-interval estimate converges.
                if (i > 400)
                {
                    float overshoot = interpolated.Yaw - reportedYaw;
                    if (overshoot > peak) peak = overshoot;
                }

                time += RenderDt;
            }

            return peak;
        }

        [Fact]
        public void ExtrapolationOvershoot_IsIndependentOfTheSmoothingParameter()
        {
            float atZero = PeakExtrapolationOvershoot(SmoothingUtils.DefaultLocalSmoothing, 0.5f);
            float atOldFloor = PeakExtrapolationOvershoot(SmoothingUtils.DefaultRemoteSmoothing, 0.5f);

            Assert.Equal(atOldFloor, atZero, precision: 5);
        }

        [Fact]
        public void ExtrapolationOvershoot_AtRealisticRates_IsUnderAQuarterDegree()
        {
            // 30Hz tracker on a 165Hz display turning at 60 deg/s. Extrapolation only
            // engages for the fraction of a sample interval by which a sample is late, so
            // the steady-state overshoot is bounded by render-frame quantisation rather
            // than by the full half-interval budget. The 1.5x figure only materialises on
            // an actually dropped packet, which ExtrapolationHoldSeconds bounds separately.
            float overshoot = PeakExtrapolationOvershoot(SmoothingUtils.DefaultLocalSmoothing, 0.5f);

            Assert.True(overshoot < 0.25f,
                $"Steady-state extrapolation overshoot should be a fraction of a degree, was {overshoot}");
        }

        [Fact]
        public void DisablingExtrapolation_RemovesOvershootEntirely()
        {
            float overshoot = PeakExtrapolationOvershoot(SmoothingUtils.DefaultLocalSmoothing, 0f);

            Assert.Equal(0f, overshoot, precision: 5);
        }

        // The speed clamp, not the smoothing value, is what damps the pipeline. This is
        // the number that made "the floor was doing the damping" wrong.
        [Fact]
        public void SmoothingZeroAndTheOldFloor_DifferOnlyMarginallyInPerFrameResponse()
        {
            float atZero = SmoothingUtils.CalculateSmoothingFactor(
                SmoothingUtils.DefaultLocalSmoothing, 1f / 60f);
            float atOldFloor = SmoothingUtils.CalculateSmoothingFactor(
                SmoothingUtils.DefaultRemoteSmoothing, 1f / 60f);

            Assert.True(atZero > atOldFloor, "removing the floor must be at least as responsive");
            Assert.True(atZero - atOldFloor < 0.1f,
                $"the floor was never the damping mechanism: {atOldFloor} vs {atZero}");
        }

        [Fact]
        public void DefaultMaxExtrapolationFraction_IsStillOneHalf()
        {
            // A pinned public default that the mod repos rely on. Changing it is a
            // declared breaking change, not a side effect of the smoothing migration.
            Assert.Equal(0.5f, new PoseInterpolator().MaxExtrapolationFraction);
        }
    }
}
