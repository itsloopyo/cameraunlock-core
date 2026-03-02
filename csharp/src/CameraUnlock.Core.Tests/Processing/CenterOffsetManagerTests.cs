using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    public class CenterOffsetManagerTests
    {
        [Fact]
        public void HasValidCenter_Initially_IsFalse()
        {
            var manager = new CenterOffsetManager();
            Assert.False(manager.HasValidCenter);
        }

        [Fact]
        public void SetCenter_WithPose_SetsCenter()
        {
            var manager = new CenterOffsetManager();
            var pose = new TrackingPose(10f, 20f, 30f, 12345);
            manager.SetCenter(pose);

            Assert.True(manager.HasValidCenter);
            Assert.Equal(10f, manager.CenterOffset.Yaw);
            Assert.Equal(20f, manager.CenterOffset.Pitch);
            Assert.Equal(30f, manager.CenterOffset.Roll);
        }

        [Fact]
        public void SetCenter_WithComponents_SetsCenter()
        {
            var manager = new CenterOffsetManager();
            manager.SetCenter(10f, 20f, 30f);

            Assert.True(manager.HasValidCenter);
            Assert.Equal(10f, manager.CenterOffset.Yaw);
            Assert.Equal(20f, manager.CenterOffset.Pitch);
            Assert.Equal(30f, manager.CenterOffset.Roll);
        }

        [Fact]
        public void ApplyOffset_NoCenter_ReturnsSamePose()
        {
            var manager = new CenterOffsetManager();
            var pose = new TrackingPose(10f, 20f, 30f, 12345);
            TrackingPose result = manager.ApplyOffset(pose);

            Assert.Equal(pose.Yaw, result.Yaw);
            Assert.Equal(pose.Pitch, result.Pitch);
            Assert.Equal(pose.Roll, result.Roll);
        }

        [Fact]
        public void ApplyOffset_WithCenter_SubtractsOffset()
        {
            var manager = new CenterOffsetManager();
            manager.SetCenter(10f, 5f, 2f);
            var pose = new TrackingPose(30f, 20f, 10f, 12345);
            TrackingPose result = manager.ApplyOffset(pose);

            Assert.Equal(20f, result.Yaw);
            Assert.Equal(15f, result.Pitch);
            Assert.Equal(8f, result.Roll);
        }

        [Fact]
        public void ApplyOffset_OutParams_NoCenter_ReturnsSame()
        {
            var manager = new CenterOffsetManager();
            manager.ApplyOffset(10f, 20f, 30f, out float y, out float p, out float r);

            Assert.Equal(10f, y);
            Assert.Equal(20f, p);
            Assert.Equal(30f, r);
        }

        [Fact]
        public void ApplyOffset_OutParams_WithCenter_SubtractsOffset()
        {
            var manager = new CenterOffsetManager();
            manager.SetCenter(5f, 5f, 5f);
            manager.ApplyOffset(10f, 20f, 30f, out float y, out float p, out float r);

            Assert.Equal(5f, y);
            Assert.Equal(15f, p);
            Assert.Equal(25f, r);
        }

        [Fact]
        public void Reset_ClearsCenter()
        {
            var manager = new CenterOffsetManager();
            manager.SetCenter(10f, 20f, 30f);
            Assert.True(manager.HasValidCenter);

            manager.Reset();
            Assert.False(manager.HasValidCenter);
        }

        [Fact]
        public void Reset_AfterReset_ApplyOffsetReturnsSame()
        {
            var manager = new CenterOffsetManager();
            manager.SetCenter(10f, 20f, 30f);
            manager.Reset();

            var pose = new TrackingPose(50f, 50f, 50f, 12345);
            TrackingPose result = manager.ApplyOffset(pose);

            Assert.Equal(50f, result.Yaw);
            Assert.Equal(50f, result.Pitch);
            Assert.Equal(50f, result.Roll);
        }

        // --- Quaternion offset tests ---

        [Fact]
        public void ApplyOffsetQuat_NoCenter_ReturnsSame()
        {
            var manager = new CenterOffsetManager();
            Quat4 input = QuaternionUtils.FromYawPitchRoll(30f, 20f, 10f);
            Quat4 result = manager.ApplyOffsetQuat(input);

            // Should be identical — no center set
            Assert.Equal(input.X, result.X, precision: 5);
            Assert.Equal(input.Y, result.Y, precision: 5);
            Assert.Equal(input.Z, result.Z, precision: 5);
            Assert.Equal(input.W, result.W, precision: 5);
        }

        [Fact]
        public void ApplyOffsetQuat_WithCenter_RemovesOffset()
        {
            var manager = new CenterOffsetManager();
            manager.SetCenter(45f, 0f, 0f);

            Quat4 input = QuaternionUtils.FromYawPitchRoll(45f, 0f, 0f);
            Quat4 result = manager.ApplyOffsetQuat(input);

            // Should be near identity (the offset cancels the input)
            QuaternionUtils.ToEulerYXZ(result, out float yaw, out float pitch, out float roll);
            Assert.Equal(0f, yaw, precision: 3);
            Assert.Equal(0f, pitch, precision: 3);
            Assert.Equal(0f, roll, precision: 3);
        }

        [Fact]
        public void ComposeAdditionalOffset_ComposesCorrectly()
        {
            var manager = new CenterOffsetManager();
            // Start with center at 20° yaw
            manager.SetCenter(20f, 0f, 0f);

            // Compose an additional 10° yaw offset
            Quat4 additional = QuaternionUtils.FromYawPitchRoll(10f, 0f, 0f);
            manager.ComposeAdditionalOffset(additional);

            // Input at 30° yaw should now give ~0° output (20 + 10 = 30 offset)
            Quat4 input = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Quat4 result = manager.ApplyOffsetQuat(input);

            QuaternionUtils.ToEulerYXZ(result, out float yaw, out float pitch, out float roll);
            Assert.Equal(0f, yaw, precision: 2);
            Assert.Equal(0f, pitch, precision: 2);
            Assert.Equal(0f, roll, precision: 2);
        }
    }
}
