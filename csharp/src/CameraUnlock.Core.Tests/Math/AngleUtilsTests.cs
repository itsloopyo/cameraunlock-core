using Xunit;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Tests.Math
{
    public class AngleUtilsTests
    {
        [Theory]
        [InlineData(0f, 0f)]
        [InlineData(90f, 90f)]
        [InlineData(-90f, -90f)]
        [InlineData(180f, 180f)]
        [InlineData(-180f, -180f)]
        [InlineData(270f, -90f)]
        [InlineData(-270f, 90f)]
        [InlineData(450f, 90f)]
        [InlineData(-450f, -90f)]
        [InlineData(360f, 0f)]
        [InlineData(-360f, 0f)]
        public void NormalizeAngle_Float_NormalizesToRange(float input, float expected)
        {
            float result = AngleUtils.NormalizeAngle(input);
            Assert.Equal(expected, result, precision: 5);
        }

        [Theory]
        [InlineData(0.0, 0.0)]
        [InlineData(270.0, -90.0)]
        [InlineData(-270.0, 90.0)]
        public void NormalizeAngle_Double_NormalizesToRange(double input, double expected)
        {
            double result = AngleUtils.NormalizeAngle(input);
            Assert.Equal(expected, result, precision: 10);
        }

        [Theory]
        [InlineData(0f, 90f, 90f)]
        [InlineData(0f, -90f, -90f)]
        [InlineData(170f, -170f, 20f)]
        [InlineData(-170f, 170f, -20f)]
        [InlineData(0f, 180f, 180f)]
        [InlineData(45f, 45f, 0f)]
        public void ShortestAngleDelta_ReturnsShortestPath(float from, float to, float expected)
        {
            float result = AngleUtils.ShortestAngleDelta(from, to);
            Assert.Equal(expected, result, precision: 5);
        }

        [Theory]
        [InlineData(0f, 0f)]
        [InlineData(180f, 3.14159265f)]
        [InlineData(90f, 1.5707963f)]
        [InlineData(-90f, -1.5707963f)]
        public void ToRadians_ConvertsCorrectly(float degrees, float expectedRadians)
        {
            float result = AngleUtils.ToRadians(degrees);
            Assert.Equal(expectedRadians, result, precision: 5);
        }

        [Theory]
        [InlineData(0f, 0f)]
        [InlineData(3.14159265f, 180f)]
        [InlineData(1.5707963f, 90f)]
        [InlineData(-1.5707963f, -90f)]
        public void ToDegrees_ConvertsCorrectly(float radians, float expectedDegrees)
        {
            float result = AngleUtils.ToDegrees(radians);
            Assert.Equal(expectedDegrees, result, precision: 4);
        }
    }
}
