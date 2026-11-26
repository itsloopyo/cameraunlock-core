using Xunit;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Math
{
    public class DeadzoneUtilsTests
    {
        [Theory]
        [InlineData(0f, 5f, 0f)]
        [InlineData(3f, 5f, 0f)]
        [InlineData(-3f, 5f, 0f)]
        [InlineData(5f, 5f, 0f)]
        [InlineData(-5f, 5f, 0f)]
        [InlineData(10f, 5f, 5f)]
        [InlineData(-10f, 5f, -5f)]
        [InlineData(7f, 5f, 2f)]
        [InlineData(-7f, 5f, -2f)]
        public void Apply_Float_AppliesDeadzoneCorrectly(float value, float deadzone, float expected)
        {
            float result = DeadzoneUtils.Apply(value, deadzone);
            Assert.Equal(expected, result, precision: 5);
        }

        [Theory]
        [InlineData(10f, 0f, 10f)]
        [InlineData(-10f, 0f, -10f)]
        [InlineData(10f, -1f, 10f)]
        public void Apply_Float_ZeroOrNegativeDeadzone_ReturnsOriginal(float value, float deadzone, float expected)
        {
            float result = DeadzoneUtils.Apply(value, deadzone);
            Assert.Equal(expected, result, precision: 5);
        }

        [Theory]
        [InlineData(0.0, 5.0, 0.0)]
        [InlineData(10.0, 5.0, 5.0)]
        [InlineData(-10.0, 5.0, -5.0)]
        public void Apply_Double_AppliesDeadzoneCorrectly(double value, double deadzone, double expected)
        {
            double result = DeadzoneUtils.Apply(value, deadzone);
            Assert.Equal(expected, result, precision: 10);
        }

        [Fact]
        public void Apply_TrackingPose_AppliesDeadzoneToAllAxes()
        {
            var pose = new TrackingPose(10f, 5f, 3f, 12345);
            var deadzone = new DeadzoneSettings(5f, 3f, 2f);

            TrackingPose result = DeadzoneUtils.Apply(pose, deadzone);

            Assert.Equal(5f, result.Yaw, precision: 5);
            Assert.Equal(2f, result.Pitch, precision: 5);
            Assert.Equal(1f, result.Roll, precision: 5);
            Assert.Equal(12345, result.TimestampTicks);
        }

        [Fact]
        public void Apply_TrackingPose_NoDeadzone_ReturnsOriginal()
        {
            var pose = new TrackingPose(10f, 5f, 3f, 12345);
            var deadzone = DeadzoneSettings.None;

            TrackingPose result = DeadzoneUtils.Apply(pose, deadzone);

            Assert.Equal(10f, result.Yaw, precision: 5);
            Assert.Equal(5f, result.Pitch, precision: 5);
            Assert.Equal(3f, result.Roll, precision: 5);
        }
    }
}
