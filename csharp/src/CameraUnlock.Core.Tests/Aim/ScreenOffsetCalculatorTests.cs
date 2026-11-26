using System;
using Xunit;
using CameraUnlock.Core.Aim;

namespace CameraUnlock.Core.Tests.Aim
{
    public class ScreenOffsetCalculatorTests
    {
        private const float ScreenWidth = 1920f;
        private const float ScreenHeight = 1080f;
        private const float HorizontalFov = 90f;
        private const float VerticalFov = 50.625f;
        private const float CompensationScale = 1f;

        [Fact]
        public void Calculate_ZeroAngles_ReturnsZeroOffset()
        {
            ScreenOffsetCalculator.Calculate(
                0f, 0f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.Equal(0f, x, precision: 5);
            Assert.Equal(0f, y, precision: 5);
        }

        [Fact]
        public void Calculate_YawRight_OffsetLeft()
        {
            ScreenOffsetCalculator.Calculate(
                10f, 0f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.True(x < 0, "Yaw right should produce negative X offset (aim moves left)");
            Assert.Equal(0f, y, precision: 5);
        }

        [Fact]
        public void Calculate_YawLeft_OffsetRight()
        {
            ScreenOffsetCalculator.Calculate(
                -10f, 0f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.True(x > 0, "Yaw left should produce positive X offset (aim moves right)");
            Assert.Equal(0f, y, precision: 5);
        }

        [Fact]
        public void Calculate_PitchUp_OffsetUp()
        {
            ScreenOffsetCalculator.Calculate(
                0f, 10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.Equal(0f, x, precision: 5);
            Assert.True(y > 0, "Pitch up should produce positive Y offset");
        }

        [Fact]
        public void Calculate_PitchDown_OffsetDown()
        {
            ScreenOffsetCalculator.Calculate(
                0f, -10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.Equal(0f, x, precision: 5);
            Assert.True(y < 0, "Pitch down should produce negative Y offset");
        }

        [Fact]
        public void Calculate_CompensationScale_ScalesOffset()
        {
            ScreenOffsetCalculator.Calculate(
                10f, 10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                1f,
                out float x1, out float y1);

            ScreenOffsetCalculator.Calculate(
                10f, 10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                2f,
                out float x2, out float y2);

            Assert.Equal(x1 * 2f, x2, precision: 3);
            Assert.Equal(y1 * 2f, y2, precision: 3);
        }

        [Fact]
        public void ApplyRollRotation_ZeroRoll_ReturnsUnchanged()
        {
            ScreenOffsetCalculator.ApplyRollRotation(100f, 50f, 0f, out float rx, out float ry);
            Assert.Equal(100f, rx, precision: 5);
            Assert.Equal(50f, ry, precision: 5);
        }

        [Fact]
        public void ApplyRollRotation_VerySmallRoll_ReturnsUnchanged()
        {
            ScreenOffsetCalculator.ApplyRollRotation(100f, 50f, 0.0001f, out float rx, out float ry);
            Assert.Equal(100f, rx, precision: 3);
            Assert.Equal(50f, ry, precision: 3);
        }

        [Fact]
        public void ApplyRollRotation_90Degrees_RotatesCorrectly()
        {
            ScreenOffsetCalculator.ApplyRollRotation(100f, 0f, 90f, out float rx, out float ry);
            Assert.Equal(0f, rx, precision: 2);
            Assert.True(System.Math.Abs(ry) > 90f);
        }

        [Fact]
        public void CalculateHorizontalFov_FromVertical_ReturnsCorrectValue()
        {
            float hFov = ScreenOffsetCalculator.CalculateHorizontalFov(60f, 16f / 9f);
            Assert.True(hFov > 60f, "Horizontal FOV should be larger than vertical for widescreen");
            Assert.True(hFov < 120f, "Horizontal FOV should be reasonable");
        }

        [Fact]
        public void CalculateHorizontalFov_SquareAspect_ReturnsSameAsVertical()
        {
            float hFov = ScreenOffsetCalculator.CalculateHorizontalFov(60f, 1f);
            Assert.Equal(60f, hFov, precision: 3);
        }

        [Fact]
        public void Calculate_CombinedYawPitch_XOffsetScalesByCosP()
        {
            // With pitch, the x offset should be larger by 1/cos(pitch) due to
            // spherical coordinate projection (prevents reticle "orbiting").
            ScreenOffsetCalculator.Calculate(
                10f, 0f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float xYawOnly, out _);

            ScreenOffsetCalculator.Calculate(
                10f, 30f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float xCombined, out _);

            float ratio = xCombined / xYawOnly;
            float expectedRatio = 1f / (float)System.Math.Cos(30f * System.Math.PI / 180f);
            Assert.Equal(expectedRatio, ratio, precision: 3);
        }

        [Fact]
        public void Calculate_CombinedYawPitch_YOffsetUnaffectedByYaw()
        {
            // The y offset should be independent of yaw (cosY cancels in the projection).
            ScreenOffsetCalculator.Calculate(
                0f, 20f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out _, out float yPitchOnly);

            ScreenOffsetCalculator.Calculate(
                25f, 20f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out _, out float yCombined);

            Assert.Equal(yPitchOnly, yCombined, precision: 3);
        }

        [Fact]
        public void Calculate_PitchAt90Degrees_ReturnsFiniteOffset()
        {
            // Regression: when pitch is ±90°, az = cos(pitch) * cos(yaw) ≈ 0 and
            // the perspective divide produced ±Infinity / NaN, propagating into
            // the reticle UI. The guard clamps the result to (0, 0).
            ScreenOffsetCalculator.Calculate(
                10f, 90f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.True(float.IsFinite(x), $"X should be finite, got {x}");
            Assert.True(float.IsFinite(y), $"Y should be finite, got {y}");
        }

        [Fact]
        public void Calculate_YawAt90Degrees_ReturnsFiniteOffset()
        {
            ScreenOffsetCalculator.Calculate(
                90f, 10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.True(float.IsFinite(x));
            Assert.True(float.IsFinite(y));
        }

        [Fact]
        public void Calculate_ZeroFov_ReturnsZeroOffset()
        {
            // Degenerate FOV (e.g. orthographic / unset camera) used to divide by
            // tan(0) = 0 and emit ±Infinity. The guard collapses the projection
            // to (0, 0) instead.
            ScreenOffsetCalculator.Calculate(
                10f, 5f, 0f,
                0f, 0f,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x, out float y);

            Assert.Equal(0f, x);
            Assert.Equal(0f, y);
        }

        [Fact]
        public void CalculatePrecomputed_DegenerateFov_ReturnsZeroOffset()
        {
            ScreenOffsetCalculator.CalculatePrecomputed(
                10f, 5f, 0f,
                0f, 0f,
                ScreenWidth * 0.5f, ScreenHeight * 0.5f,
                CompensationScale,
                out float x, out float y);

            Assert.Equal(0f, x);
            Assert.Equal(0f, y);
        }

        [Fact]
        public void Calculate_SymmetricAngles_ProduceSymmetricOffsets()
        {
            ScreenOffsetCalculator.Calculate(
                10f, 10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x1, out float y1);

            ScreenOffsetCalculator.Calculate(
                -10f, -10f, 0f,
                HorizontalFov, VerticalFov,
                ScreenWidth, ScreenHeight,
                CompensationScale,
                out float x2, out float y2);

            Assert.Equal(-x1, x2, precision: 3);
            Assert.Equal(-y1, y2, precision: 3);
        }
    }
}
