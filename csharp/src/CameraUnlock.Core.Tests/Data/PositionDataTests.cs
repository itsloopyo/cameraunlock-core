using System;
using System.Diagnostics;
using Xunit;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Data
{
    public class PositionDataTests
    {
        [Fact]
        public void Constructor_SetsComponents()
        {
            var pos = new PositionData(0.1f, 0.2f, 0.3f, 1000);
            Assert.Equal(0.1f, pos.X);
            Assert.Equal(0.2f, pos.Y);
            Assert.Equal(0.3f, pos.Z);
            Assert.Equal(1000, pos.TimestampTicks);
        }

        [Fact]
        public void Constructor_WithoutTimestamp_SetsCurrentTimestamp()
        {
            var before = Stopwatch.GetTimestamp();
            var pos = new PositionData(1f, 2f, 3f);
            var after = Stopwatch.GetTimestamp();

            Assert.True(pos.TimestampTicks >= before);
            Assert.True(pos.TimestampTicks <= after);
        }

        [Fact]
        public void IsValid_NonZeroTimestamp_ReturnsTrue()
        {
            var pos = new PositionData(0f, 0f, 0f, 1);
            Assert.True(pos.IsValid);
        }

        [Fact]
        public void IsValid_ZeroTimestamp_ReturnsFalse()
        {
            var pos = new PositionData(0f, 0f, 0f, 0);
            Assert.False(pos.IsValid);
        }

        [Fact]
        public void IsValid_Default_ReturnsFalse()
        {
            PositionData pos = default;
            Assert.False(pos.IsValid);
        }

        [Fact]
        public void Zero_ReturnsValidZeroPosition()
        {
            var pos = PositionData.Zero;
            Assert.Equal(0f, pos.X);
            Assert.Equal(0f, pos.Y);
            Assert.Equal(0f, pos.Z);
            Assert.True(pos.IsValid);
        }

        [Fact]
        public void ToVec3_ReturnsCorrectVector()
        {
            var pos = new PositionData(0.1f, 0.2f, 0.3f, 1000);
            Vec3 v = pos.ToVec3();
            Assert.Equal(0.1f, v.X);
            Assert.Equal(0.2f, v.Y);
            Assert.Equal(0.3f, v.Z);
        }

        [Fact]
        public void SubtractOffset_SubtractsComponents()
        {
            var pos = new PositionData(0.5f, 0.6f, 0.7f, 2000);
            var offset = new PositionData(0.1f, 0.2f, 0.3f, 1000);

            var result = pos.SubtractOffset(offset);

            Assert.Equal(0.4f, result.X, precision: 5);
            Assert.Equal(0.4f, result.Y, precision: 5);
            Assert.Equal(0.4f, result.Z, precision: 5);
            Assert.Equal(2000, result.TimestampTicks);
        }

        [Fact]
        public void Equals_SameValues_ReturnsTrue()
        {
            var a = new PositionData(1f, 2f, 3f, 100);
            var b = new PositionData(1f, 2f, 3f, 200);
            Assert.True(a.Equals(b));
            Assert.True(a == b);
        }

        [Fact]
        public void Equals_DifferentValues_ReturnsFalse()
        {
            var a = new PositionData(1f, 2f, 3f, 100);
            var b = new PositionData(1f, 2f, 4f, 100);
            Assert.False(a.Equals(b));
            Assert.True(a != b);
        }

        [Fact]
        public void ToString_ContainsComponents()
        {
            var pos = new PositionData(0.1234f, 0.5678f, 0.9012f, 1000);
            string s = pos.ToString();
            Assert.Contains("0.1234", s);
            Assert.Contains("0.5678", s);
            Assert.Contains("0.9012", s);
        }
    }
}
