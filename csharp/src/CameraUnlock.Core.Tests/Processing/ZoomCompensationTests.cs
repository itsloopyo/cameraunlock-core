using System;
using CameraUnlock.Core.Processing;
using Xunit;

namespace CameraUnlock.Core.Tests.Processing
{
    /// <summary>
    /// Case for case with cpp/tests/zoom_compensation_tests.cpp. The checks compute the screen
    /// displacement both ways and compare it, rather than restating the formula.
    /// </summary>
    public class ZoomCompensationTests
    {
        private const double DegToRad = System.Math.PI / 180.0;

        // Deus Ex: Human Revolution's three measured fields of view, as tan(vfov/2) on a 16:9
        // frame: 90 degrees horizontal walking around, 75 aiming, 45 scoped.
        private const float Base = 0.5625f;
        private const float Ads = 0.4316f;
        private const float Scope = 0.2330f;

        // Where an angle off the view axis lands, as a fraction of the frame from its centre.
        private static float ScreenFraction(float angleDeg, float tanHalfFov)
        {
            return (float)(System.Math.Tan(angleDeg * DegToRad) / (2.0 * tanHalfFov));
        }

        private static void Near(float expected, float actual, float eps = 1e-5f)
        {
            Assert.InRange(actual, expected - eps, expected + eps);
        }

        [Fact]
        public void UnZoomedIsExactlyOne()
        {
            Near(1.0f, ZoomCompensation.FovZoomFactor(Base, Base));
        }

        [Fact]
        public void TighterZoomGivesSmallerFactor()
        {
            Assert.True(ZoomCompensation.FovZoomFactor(Scope, Base) < ZoomCompensation.FovZoomFactor(Ads, Base));
        }

        [Fact]
        public void ScopeFactorIsTheRatioOfTheTangents()
        {
            Near(0.41422223f, ZoomCompensation.FovZoomFactor(Scope, Base), 1e-6f);
        }

        [Theory]
        [InlineData(2.0f, Ads)]
        [InlineData(10.0f, Ads)]
        [InlineData(25.0f, Ads)]
        [InlineData(45.0f, Ads)]
        [InlineData(2.0f, Scope)]
        [InlineData(10.0f, Scope)]
        [InlineData(25.0f, Scope)]
        [InlineData(45.0f, Scope)]
        public void ScaledAngleLandsWhereTheRawAngleLandedUnZoomed(float angle, float tanFov)
        {
            float scaled = ZoomCompensation.ScaleAngleForZoom(angle, ZoomCompensation.FovZoomFactor(tanFov, Base));
            Near(ScreenFraction(angle, Base), ScreenFraction(scaled, tanFov));
        }

        [Fact]
        public void FactorOfOneReturnsTheAngleUntouched()
        {
            Near(20.0f, ZoomCompensation.ScaleAngleForZoom(20.0f, 1.0f), 1e-4f);
        }

        [Fact]
        public void ZeroStaysZero()
        {
            Near(0.0f, ZoomCompensation.ScaleAngleForZoom(0.0f, ZoomCompensation.FovZoomFactor(Scope, Base)));
        }

        [Fact]
        public void ScalingIsOdd()
        {
            float factor = ZoomCompensation.FovZoomFactor(Scope, Base);
            Near(-ZoomCompensation.ScaleAngleForZoom(25.0f, factor), ZoomCompensation.ScaleAngleForZoom(-25.0f, factor));
        }

        [Fact]
        public void SmallAnglesMatchAPlainMultiplyAndLargeOnesDoNot()
        {
            float factor = ZoomCompensation.FovZoomFactor(Scope, Base);
            Near(5.0f * factor, ZoomCompensation.ScaleAngleForZoom(5.0f, factor), 0.01f);
            Assert.True(ZoomCompensation.ScaleAngleForZoom(45.0f, factor) > 45.0f * factor);
        }
    }
}
