using System;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tests.Protocol
{
    public class OpenTrackPacketPositionTests
    {
        private static byte[] MakePacket(double x, double y, double z, double yaw = 0, double pitch = 0, double roll = 0)
        {
            byte[] data = new byte[48];
            Buffer.BlockCopy(BitConverter.GetBytes(x), 0, data, OpenTrackPacket.XOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(y), 0, data, OpenTrackPacket.YOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(z), 0, data, OpenTrackPacket.ZOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(yaw), 0, data, OpenTrackPacket.YawOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(pitch), 0, data, OpenTrackPacket.PitchOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(roll), 0, data, OpenTrackPacket.RollOffset, 8);
            return data;
        }

        [Fact]
        public void TryParsePosition_ValidPacket_ParsesCorrectly()
        {
            // OpenTrack sends cm, we convert to meters
            byte[] data = MakePacket(10.0, 20.0, 30.0);

            bool result = OpenTrackPacket.TryParsePosition(data, out PositionData pos);

            Assert.True(result);
            Assert.Equal(0.10f, pos.X, precision: 5);
            Assert.Equal(0.20f, pos.Y, precision: 5);
            Assert.Equal(0.30f, pos.Z, precision: 5);
        }

        [Fact]
        public void TryParsePosition_CmToMetersConversion()
        {
            // 1 cm = 0.01 m
            byte[] data = MakePacket(1.0, 1.0, 1.0);

            bool result = OpenTrackPacket.TryParsePosition(data, out PositionData pos);

            Assert.True(result);
            Assert.Equal(0.01f, pos.X, precision: 5);
            Assert.Equal(0.01f, pos.Y, precision: 5);
            Assert.Equal(0.01f, pos.Z, precision: 5);
        }

        [Fact]
        public void TryParsePosition_NegativeValues()
        {
            byte[] data = MakePacket(-5.0, -10.0, -15.0);

            bool result = OpenTrackPacket.TryParsePosition(data, out PositionData pos);

            Assert.True(result);
            Assert.Equal(-0.05f, pos.X, precision: 5);
            Assert.Equal(-0.10f, pos.Y, precision: 5);
            Assert.Equal(-0.15f, pos.Z, precision: 5);
        }

        [Fact]
        public void TryParsePosition_NaN_ReturnsFalse()
        {
            byte[] data = MakePacket(double.NaN, 0, 0);
            Assert.False(OpenTrackPacket.TryParsePosition(data, out _));
        }

        [Fact]
        public void TryParsePosition_Infinity_ReturnsFalse()
        {
            byte[] data = MakePacket(0, double.PositiveInfinity, 0);
            Assert.False(OpenTrackPacket.TryParsePosition(data, out _));
        }

        [Fact]
        public void TryParsePosition_NegativeInfinity_ReturnsFalse()
        {
            byte[] data = MakePacket(0, 0, double.NegativeInfinity);
            Assert.False(OpenTrackPacket.TryParsePosition(data, out _));
        }

        [Fact]
        public void TryParsePosition_NullData_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParsePosition((byte[])null!, out _));
        }

        [Fact]
        public void TryParsePosition_TooSmallPacket_ReturnsFalse()
        {
            byte[] data = new byte[24]; // Only 3 doubles, need 6
            Assert.False(OpenTrackPacket.TryParsePosition(data, out _));
        }

        [Fact]
        public void TryParsePosition_ZeroValues_Succeeds()
        {
            byte[] data = MakePacket(0, 0, 0);

            bool result = OpenTrackPacket.TryParsePosition(data, out PositionData pos);

            Assert.True(result);
            Assert.Equal(0f, pos.X);
            Assert.Equal(0f, pos.Y);
            Assert.Equal(0f, pos.Z);
        }

        [Fact]
        public void TryParsePosition_DoesNotAffectRotationParsing()
        {
            // Ensure position and rotation parsing are independent
            byte[] data = MakePacket(10.0, 20.0, 30.0, 45.0, -15.0, 5.0);

            bool posResult = OpenTrackPacket.TryParsePosition(data, out PositionData pos);
            bool rotResult = OpenTrackPacket.TryParse(data, out var pose);

            Assert.True(posResult);
            Assert.True(rotResult);
            Assert.Equal(0.10f, pos.X, precision: 5);
            Assert.Equal(45.0f, pose.Yaw, precision: 3);
            Assert.Equal(-15.0f, pose.Pitch, precision: 3);
        }
    }
}
