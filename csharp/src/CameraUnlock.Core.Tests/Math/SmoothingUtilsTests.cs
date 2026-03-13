using Xunit;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Tests.Math
{
    public class SmoothingUtilsTests
    {
        private const float DeltaTime60Fps = 1f / 60f;

        [Fact]
        public void CalculateSmoothingFactor_ZeroSmoothing_ReturnsHighButNotOne()
        {
            float result = SmoothingUtils.CalculateSmoothingFactor(0f, DeltaTime60Fps);
            Assert.InRange(result, 0.5f, 0.99f);
        }

        [Fact]
        public void CalculateSmoothingFactor_VerySmallSmoothing_ReturnsHighButNotOne()
        {
            float result = SmoothingUtils.CalculateSmoothingFactor(0.0001f, DeltaTime60Fps);
            Assert.InRange(result, 0.5f, 0.99f);
        }

        [Fact]
        public void CalculateSmoothingFactor_NormalSmoothing_ReturnsBetweenZeroAndOne()
        {
            float result = SmoothingUtils.CalculateSmoothingFactor(0.5f, DeltaTime60Fps);
            Assert.InRange(result, 0f, 1f);
        }

        [Fact]
        public void CalculateSmoothingFactor_HighSmoothing_ReturnsLowerFactor()
        {
            float lowSmoothing = SmoothingUtils.CalculateSmoothingFactor(0.2f, DeltaTime60Fps);
            float highSmoothing = SmoothingUtils.CalculateSmoothingFactor(0.8f, DeltaTime60Fps);
            Assert.True(highSmoothing < lowSmoothing);
        }

        [Fact]
        public void CalculateSmoothingFactor_LargerDeltaTime_ReturnsHigherFactor()
        {
            float smallDelta = SmoothingUtils.CalculateSmoothingFactor(0.5f, 1f / 120f);
            float largeDelta = SmoothingUtils.CalculateSmoothingFactor(0.5f, 1f / 30f);
            Assert.True(largeDelta > smallDelta);
        }

        [Fact]
        public void Smooth_Float_MovesTowardsTarget()
        {
            float current = 0f;
            float target = 100f;
            float result = SmoothingUtils.Smooth(current, target, 0.5f, DeltaTime60Fps);
            Assert.True(result > 0f);
            Assert.True(result < 100f);
        }

        [Fact]
        public void Smooth_Double_MovesTowardsTarget()
        {
            double current = 0.0;
            double target = 100.0;
            double result = SmoothingUtils.Smooth(current, target, 0.5f, DeltaTime60Fps);
            Assert.True(result > 0.0);
            Assert.True(result < 100.0);
        }

        [Fact]
        public void Smooth_ZeroSmoothing_MovesSignificantlyTowardsTarget()
        {
            float current = 0f;
            float target = 100f;
            float result = SmoothingUtils.Smooth(current, target, 0f, DeltaTime60Fps);
            Assert.InRange(result, 50f, 99f);
        }

        [Fact]
        public void CalculateSmoothingFactor_NegativeSmoothing_ClampedToFrameInterpolationSpeed()
        {
            // Negative smoothing should not produce a faster speed than FrameInterpolationSpeed
            float normal = SmoothingUtils.CalculateSmoothingFactor(0f, DeltaTime60Fps);
            float negative = SmoothingUtils.CalculateSmoothingFactor(-1f, DeltaTime60Fps);
            Assert.Equal(normal, negative);
        }

        [Fact]
        public void CalculateSmoothingFactor_AboveOneSmoothing_ClampedToMinSpeed()
        {
            // Smoothing > 1 should not produce a negative or zero speed
            float result = SmoothingUtils.CalculateSmoothingFactor(2f, DeltaTime60Fps);
            Assert.InRange(result, 0.001f, 0.1f);
        }

        [Fact]
        public void CalculateSmoothingFactor_AlwaysInterpolates_NeverReturnsOne()
        {
            // At any reasonable framerate, the factor should never reach 1.0
            // (which would mean snap-to-target, no interpolation)
            float at240Hz = SmoothingUtils.CalculateSmoothingFactor(0f, 1f / 240f);
            float at60Hz = SmoothingUtils.CalculateSmoothingFactor(0f, 1f / 60f);
            Assert.True(at240Hz < 1f, "Frame interpolation must always be active at 240Hz");
            Assert.True(at60Hz < 1f, "Frame interpolation must always be active at 60Hz");
            Assert.True(at240Hz > 0f, "Must produce non-zero blend at 240Hz");
        }

        [Fact]
        public void GetEffectiveSmoothing_BelowBaseline_ReturnsBaseline()
        {
            float result = SmoothingUtils.GetEffectiveSmoothing(0.05f);
            Assert.Equal(SmoothingUtils.BaselineSmoothing, result);
        }

        [Fact]
        public void GetEffectiveSmoothing_AboveBaseline_ReturnsBaseSmoothing()
        {
            float highSmoothing = 0.5f;
            float result = SmoothingUtils.GetEffectiveSmoothing(highSmoothing);
            Assert.Equal(highSmoothing, result);
        }
    }
}
