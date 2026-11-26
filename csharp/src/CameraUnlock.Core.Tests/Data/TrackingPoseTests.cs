using System.Diagnostics;
using Xunit;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Data
{
    public class TrackingPoseTests
    {
        [Fact]
        public void Constructor_WithTimestamp_SetsAllComponents()
        {
            var pose = new TrackingPose(10f, 20f, 30f, 12345);
            Assert.Equal(10f, pose.Yaw);
            Assert.Equal(20f, pose.Pitch);
            Assert.Equal(30f, pose.Roll);
            Assert.Equal(12345, pose.TimestampTicks);
        }

        [Fact]
        public void Constructor_WithoutTimestamp_SetsCurrentTime()
        {
            long before = Stopwatch.GetTimestamp();
            var pose = new TrackingPose(10f, 20f, 30f);
            long after = Stopwatch.GetTimestamp();

            Assert.True(pose.TimestampTicks >= before);
            Assert.True(pose.TimestampTicks <= after);
        }

        [Fact]
        public void IsValid_WithTimestamp_ReturnsTrue()
        {
            var pose = new TrackingPose(10f, 20f, 30f, 12345);
            Assert.True(pose.IsValid);
        }

        [Fact]
        public void IsValid_ZeroTimestamp_ReturnsFalse()
        {
            var pose = new TrackingPose(10f, 20f, 30f, 0);
            Assert.False(pose.IsValid);
        }

        [Fact]
        public void Zero_ReturnsZeroPoseWithValidTimestamp()
        {
            TrackingPose zero = TrackingPose.Zero;
            Assert.Equal(0f, zero.Yaw);
            Assert.Equal(0f, zero.Pitch);
            Assert.Equal(0f, zero.Roll);
            Assert.True(zero.IsValid);
        }

        [Fact]
        public void SubtractOffset_SubtractsFromAllAxes()
        {
            var pose = new TrackingPose(30f, 20f, 10f, 12345);
            var offset = new TrackingPose(10f, 5f, 2f, 0);
            TrackingPose result = pose.SubtractOffset(offset);

            Assert.Equal(20f, result.Yaw);
            Assert.Equal(15f, result.Pitch);
            Assert.Equal(8f, result.Roll);
            Assert.Equal(12345, result.TimestampTicks);
        }

        [Fact]
        public void ApplySensitivity_MultipliesAxes()
        {
            var pose = new TrackingPose(10f, 20f, 30f, 12345);
            var sensitivity = new SensitivitySettings(2f, 0.5f, 1f, false, false, false);
            TrackingPose result = pose.ApplySensitivity(sensitivity);

            Assert.Equal(20f, result.Yaw);
            Assert.Equal(10f, result.Pitch);
            Assert.Equal(30f, result.Roll);
        }

        [Fact]
        public void ApplySensitivity_InvertsAxes()
        {
            var pose = new TrackingPose(10f, 20f, 30f, 12345);
            var sensitivity = new SensitivitySettings(1f, 1f, 1f, true, true, true);
            TrackingPose result = pose.ApplySensitivity(sensitivity);

            Assert.Equal(-10f, result.Yaw);
            Assert.Equal(-20f, result.Pitch);
            Assert.Equal(-30f, result.Roll);
        }

        [Fact]
        public void Equals_SamePose_ReturnsTrue()
        {
            var a = new TrackingPose(10f, 20f, 30f, 12345);
            var b = new TrackingPose(10f, 20f, 30f, 67890);
            Assert.True(a.Equals(b));
        }

        [Fact]
        public void Equals_DifferentPose_ReturnsFalse()
        {
            var a = new TrackingPose(10f, 20f, 30f, 12345);
            var b = new TrackingPose(10f, 20f, 31f, 12345);
            Assert.False(a.Equals(b));
        }

        [Fact]
        public void EqualityOperator_Works()
        {
            var a = new TrackingPose(10f, 20f, 30f, 12345);
            var b = new TrackingPose(10f, 20f, 30f, 67890);
            Assert.True(a == b);
        }

        [Fact]
        public void InequalityOperator_Works()
        {
            var a = new TrackingPose(10f, 20f, 30f, 12345);
            var b = new TrackingPose(10f, 20f, 31f, 12345);
            Assert.True(a != b);
        }

        [Fact]
        public void GetHashCode_SamePose_ReturnsSameHash()
        {
            var a = new TrackingPose(10f, 20f, 30f, 12345);
            var b = new TrackingPose(10f, 20f, 30f, 67890);
            Assert.Equal(a.GetHashCode(), b.GetHashCode());
        }

        [Fact]
        public void ToString_ContainsValues()
        {
            var pose = new TrackingPose(10.5f, 20.5f, 30.5f, 12345);
            string s = pose.ToString();
            Assert.Contains("10.5", s);
            Assert.Contains("20.5", s);
            Assert.Contains("30.5", s);
        }
    }
}
