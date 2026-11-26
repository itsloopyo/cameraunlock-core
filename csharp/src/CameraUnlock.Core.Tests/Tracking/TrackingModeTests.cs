using Xunit;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    public class TrackingModeTests
    {
        [Theory]
        [InlineData(TrackingMode.RotationAndPosition, "6DOF (rotation + position)")]
        [InlineData(TrackingMode.RotationOnly, "rotation only")]
        [InlineData(TrackingMode.PositionOnly, "position only")]
        public void Description_ReturnsHumanReadableName(TrackingMode mode, string expected)
        {
            Assert.Equal(expected, mode.Description());
        }

        [Fact]
        public void Description_UnknownValue_FallsBackToEnumName()
        {
            Assert.Equal("99", ((TrackingMode)99).Description());
        }
    }
}
