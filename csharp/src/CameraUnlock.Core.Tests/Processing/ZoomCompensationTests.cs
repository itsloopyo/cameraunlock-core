using System;
using CameraUnlock.Core.Data;
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

        // The formula every caller had before an angle past 90 degrees was handled.
        private static float TangentRoundTrip(float angleDeg, float factor)
        {
            const double degToRad = System.Math.PI / 180.0;
            return (float)(System.Math.Atan(System.Math.Tan(angleDeg * degToRad) * factor) / degToRad);
        }

        [Fact]
        public void Below90TheAnswerIsBitForBitTheTangentRoundTrips()
        {
            foreach (float factor in new[] { 1.0f, 0.9f, 0.7673f, 0.5f, 0.41422223f, 0.25f, 0.1f, 2.0f })
            {
                for (int tenths = -899; tenths <= 899; tenths++)
                {
                    float angle = tenths / 10.0f;
                    Assert.Equal(
                        BitConverter.SingleToInt32Bits(TangentRoundTrip(angle, factor)),
                        BitConverter.SingleToInt32Bits(ZoomCompensation.ScaleAngleForZoom(angle, factor)));
                }
            }
        }

        // A tracker's response curve reaches angles no neck does.
        [Theory]
        [InlineData(90.0f)]
        [InlineData(120.0f)]
        [InlineData(-150.0f)]
        [InlineData(179.0f)]
        public void FactorOfOneReturnsAnAnglePast90OnItsOwnSide(float angle)
        {
            Near(angle, ZoomCompensation.ScaleAngleForZoom(angle, 1.0f), 1e-3f);
        }

        [Fact]
        public void From90OnTheScalingIsContinuousOddAndRising()
        {
            float factor = ZoomCompensation.FovZoomFactor(Scope, Base);
            Near(90.0f, ZoomCompensation.ScaleAngleForZoom(90.0f, factor), 1e-3f);
            Near(180.0f, System.Math.Abs(ZoomCompensation.ScaleAngleForZoom(180.0f, factor)), 1e-3f);
            Assert.True(System.Math.Abs(ZoomCompensation.ScaleAngleForZoom(90.5f, factor)
                - ZoomCompensation.ScaleAngleForZoom(89.5f, factor)) < 3.0f);
            Near(-ZoomCompensation.ScaleAngleForZoom(120.0f, factor), ZoomCompensation.ScaleAngleForZoom(-120.0f, factor));
            for (float angle = -179.0f; angle < 179.0f; angle += 1.0f)
            {
                Assert.True(ZoomCompensation.ScaleAngleForZoom(angle + 1.0f, factor) > ZoomCompensation.ScaleAngleForZoom(angle, factor),
                    "the scaled angle fell between " + angle + " and " + (angle + 1.0f));
            }
        }

        private static void NearVec(Vec3 expected, Vec3 actual)
        {
            Near(expected.X, actual.X);
            Near(expected.Y, actual.Y);
            Near(expected.Z, actual.Z);
        }

        [Fact]
        public void ALeanAcrossTheViewScalesByTheFactor()
        {
            float factor = ZoomCompensation.FovZoomFactor(Scope, Base);
            NearVec(new Vec3(0.2f * factor, -0.1f * factor, 0f),
                ZoomCompensation.ScaleLeanForZoom(new Vec3(0.2f, -0.1f, 0f), Vec3.Forward, factor));
        }

        [Fact]
        public void ALeanAlongTheViewIsUntouched()
        {
            NearVec(new Vec3(0f, 0f, 0.4f), ZoomCompensation.ScaleLeanForZoom(new Vec3(0f, 0f, 0.4f), Vec3.Forward, 0.25f));
            NearVec(new Vec3(0f, 0f, -0.1f), ZoomCompensation.ScaleLeanForZoom(new Vec3(0f, 0f, -0.1f), Vec3.Forward, 0.25f));
        }

        [Fact]
        public void AFactorOfOneReturnsTheLeanUntouched()
        {
            NearVec(new Vec3(0.2f, -0.1f, 0.4f), ZoomCompensation.ScaleLeanForZoom(new Vec3(0.2f, -0.1f, 0.4f), Vec3.Forward, 1.0f));
        }

        [Fact]
        public void AMixedLeanKeepsItsPartAlongTheViewAndScalesTheRest()
        {
            var lean = new Vec3(0.2f, -0.1f, 0.4f);
            NearVec(new Vec3(0.1f, -0.05f, 0.4f), ZoomCompensation.ScaleLeanForZoom(lean, Vec3.Forward, 0.5f));
            NearVec(new Vec3(0.1f, -0.05f, 0.4f), ZoomCompensation.ScaleLeanForZoom(lean, -Vec3.Forward, 0.5f));
        }

        [Fact]
        public void TheSplitFollowsAViewAxisThatIsNotACoordinateAxis()
        {
            var pitched = new Vec3(0f, 0.6f, 0.8f);
            var across = new Vec3(1f, 0f, 0f);
            NearVec(pitched * 0.3f + across * 0.1f,
                ZoomCompensation.ScaleLeanForZoom(pitched * 0.3f + across * 0.2f, pitched, 0.5f));
        }
    }
}
