using System;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tests.Protocol
{
    /// <summary>
    /// A double that is finite but too large for float narrows to infinity, which the
    /// NaN/infinity checks on the double alone let straight through. These cover the
    /// narrowed value being validated instead, and the pipeline damage a single such
    /// packet used to do.
    /// </summary>
    public class OpenTrackPacketFloatRangeTests
    {
        private const double AboveFloatMax = 1e300;
        private const double BelowFloatMin = -1e300;

        private static byte[] MakePacket(
            double x = 0, double y = 0, double z = 0,
            double yaw = 0, double pitch = 0, double roll = 0)
        {
            byte[] data = new byte[OpenTrackPacket.MinPacketSize];
            Buffer.BlockCopy(BitConverter.GetBytes(x), 0, data, OpenTrackPacket.XOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(y), 0, data, OpenTrackPacket.YOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(z), 0, data, OpenTrackPacket.ZOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(yaw), 0, data, OpenTrackPacket.YawOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(pitch), 0, data, OpenTrackPacket.PitchOffset, 8);
            Buffer.BlockCopy(BitConverter.GetBytes(roll), 0, data, OpenTrackPacket.RollOffset, 8);
            return data;
        }

        [Fact]
        public void TryParse_YawAboveFloatRange_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParse(MakePacket(yaw: AboveFloatMax), out _));
        }

        [Fact]
        public void TryParse_PitchBelowFloatRange_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParse(MakePacket(pitch: BelowFloatMin), out _));
        }

        [Fact]
        public void TryParse_RollAboveFloatRange_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParse(MakePacket(roll: AboveFloatMax), out _));
        }

        [Fact]
        public void TryParsePosition_XAboveFloatRange_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParsePosition(MakePacket(x: AboveFloatMax), out _));
        }

        [Fact]
        public void TryParsePosition_YBelowFloatRange_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParsePosition(MakePacket(y: BelowFloatMin), out _));
        }

        [Fact]
        public void TryParsePosition_ZAboveFloatRange_ReturnsFalse()
        {
            Assert.False(OpenTrackPacket.TryParsePosition(MakePacket(z: AboveFloatMax), out _));
        }

        [Fact]
        public void TryParse_AtFloatMax_StillParses()
        {
            Assert.True(OpenTrackPacket.TryParse(MakePacket(yaw: float.MaxValue), out TrackingPose pose));
            Assert.Equal(float.MaxValue, pose.Yaw);
        }

        [Fact]
        public void TryParsePosition_AtFloatMax_StillParses()
        {
            Assert.True(OpenTrackPacket.TryParsePosition(MakePacket(x: float.MaxValue), out PositionData pos));
            Assert.False(float.IsInfinity(pos.X));
        }

        [Fact]
        public void RejectedPacket_LeavesPositionSmoothingUsable()
        {
            // What the missing check cost: an infinite sample became the smoothed
            // state, the next finite sample lerped Infinity towards a finite target
            // and produced NaN, and NaN survived every lerp after it.
            var processor = new PositionProcessor { TrackerPivotForward = 0f };
            var identity = Quat4.Identity;

            Assert.False(OpenTrackPacket.TryParsePosition(MakePacket(x: AboveFloatMax), out _));

            Assert.True(OpenTrackPacket.TryParsePosition(MakePacket(x: 5.0), out PositionData good));
            processor.Process(good, identity, 0.016f);
            Vec3 result = processor.Process(good, identity, 0.016f);

            Assert.False(float.IsNaN(result.X));
            Assert.False(float.IsNaN(result.Y));
            Assert.False(float.IsNaN(result.Z));
        }

        [Fact]
        public void InfiniteSampleWouldPoisonSmoothing_DemonstratesWhyTheGuardExists()
        {
            // Fed directly (bypassing the parser) an infinite sample still ruins the
            // processor - which is why the packet boundary, not the processor, is the
            // place that rejects it.
            var processor = new PositionProcessor { TrackerPivotForward = 0f };
            var identity = Quat4.Identity;

            processor.Process(new PositionData(float.PositiveInfinity, 0f, 0f), identity, 0.016f);
            Vec3 result = processor.Process(new PositionData(0.05f, 0f, 0f), identity, 0.016f);

            Assert.True(float.IsNaN(result.X));
        }
    }
}
