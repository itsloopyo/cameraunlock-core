using Xunit;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Tests.Math
{
    public class MathUtilsTests
    {
        [Theory]
        [InlineData(50f, 0f, 100f, 50f)]
        [InlineData(-10f, 0f, 100f, 0f)]
        [InlineData(150f, 0f, 100f, 100f)]
        [InlineData(0f, 0f, 100f, 0f)]
        [InlineData(100f, 0f, 100f, 100f)]
        public void Clamp_ClampsValueWithinRange(float value, float min, float max, float expected)
        {
            float result = MathUtils.Clamp(value, min, max);
            Assert.Equal(expected, result);
        }

        [Theory]
        [InlineData(-0.5f, 0f)]
        [InlineData(0f, 0f)]
        [InlineData(0.5f, 0.5f)]
        [InlineData(1f, 1f)]
        [InlineData(1.5f, 1f)]
        public void Clamp01_ClampsValueBetweenZeroAndOne(float value, float expected)
        {
            float result = MathUtils.Clamp01(value);
            Assert.Equal(expected, result);
        }

        [Theory]
        [InlineData(0f, 100f, 0f, 0f)]
        [InlineData(0f, 100f, 1f, 100f)]
        [InlineData(0f, 100f, 0.5f, 50f)]
        [InlineData(-50f, 50f, 0.5f, 0f)]
        [InlineData(10f, 20f, 0.25f, 12.5f)]
        public void Lerp_InterpolatesCorrectly(float a, float b, float t, float expected)
        {
            float result = MathUtils.Lerp(a, b, t);
            Assert.Equal(expected, result, precision: 5);
        }

        [Fact]
        public void Lerp_ExtrapolatesBeyondRange()
        {
            float result = MathUtils.Lerp(0f, 100f, 2f);
            Assert.Equal(200f, result, precision: 5);
        }
    }
}
