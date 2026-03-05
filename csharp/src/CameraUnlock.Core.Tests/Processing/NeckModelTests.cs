using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    public class NeckModelTests
    {
        private static readonly NeckModelSettings TestSettings = new NeckModelSettings(true, 0.10f, 0.08f);

        [Fact]
        public void IdentityRotation_ReturnsZeroOffset()
        {
            Vec3 offset = NeckModel.ComputeOffset(Quat4.Identity, TestSettings);

            Assert.Equal(0f, offset.X, precision: 5);
            Assert.Equal(0f, offset.Y, precision: 5);
            Assert.Equal(0f, offset.Z, precision: 5);
        }

        [Fact]
        public void Disabled_ReturnsZero()
        {
            // Even with non-identity rotation, disabled should return zero
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Vec3 offset = NeckModel.ComputeOffset(rotation, NeckModelSettings.Disabled);

            Assert.Equal(0f, offset.X);
            Assert.Equal(0f, offset.Y);
            Assert.Equal(0f, offset.Z);
        }

        [Fact]
        public void PureYaw_ProducesLateralAndDepthOffset()
        {
            // Turning head right (positive yaw) rotates the neck-to-eyes vector.
            // The forward component (0.08) rotates around Y axis producing lateral offset.
            // Sign depends on rotation convention — FromYawPitchRoll(30,0,0) produces positive X.
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Vec3 offset = NeckModel.ComputeOffset(rotation, TestSettings);

            // With 30° yaw, the forward component (0.08) rotates around Y axis
            // X offset from forward: 0.08 * sin(30°) = 0.08 * 0.5 = 0.04
            // Z offset from forward: 0.08 * (cos(30°) - 1) = 0.08 * (0.866 - 1) = -0.0107
            Assert.True(System.Math.Abs(offset.X) > 0.01f, $"Expected significant X offset, got {offset.X}");
            Assert.True(offset.Z < 0, $"Expected negative Z offset, got {offset.Z}");
            Assert.Equal(0.04f, System.Math.Abs(offset.X), precision: 3);
        }

        [Fact]
        public void PureRoll15_ProducesLateralAndVerticalOffset()
        {
            // Rolling head right (positive roll, clockwise from behind)
            // The neck-to-eyes vector (0, 0.10, 0.08) rotates around Z axis
            // This should produce lateral offset (eyes arc to the left)
            // and slight vertical offset (eyes drop)
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(0f, 0f, 15f);
            Vec3 offset = NeckModel.ComputeOffset(rotation, TestSettings);

            // Rolling right: the Y component of neckToEyes (0.10) rotates to have a -X component
            // sin(15°) ≈ 0.2588, cos(15°) ≈ 0.9659
            // New Y from height: 0.10 * cos(15°) = 0.0966
            // New X from height: -0.10 * sin(15°) = -0.0259
            // Offset Y: 0.0966 - 0.10 = -0.0034
            // Offset X: -0.0259 - 0 = -0.0259
            Assert.True(offset.X < 0, $"Expected negative X offset for right roll, got {offset.X}");
            Assert.True(offset.Y < 0, $"Expected negative Y offset (eyes drop), got {offset.Y}");
        }

        [Fact]
        public void PurePitch_ProducesVerticalAndDepthOffset()
        {
            // Pitching head down (positive pitch in our convention)
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(0f, 20f, 0f);
            Vec3 offset = NeckModel.ComputeOffset(rotation, TestSettings);

            // Pitch rotates around X axis. The neckToEyes (0, 0.10, 0.08) rotates.
            // The Y and Z components change, X stays ~0
            Assert.Equal(0f, offset.X, precision: 4);
            // Looking down should move eyes forward and down relative to neutral
            Assert.True(System.Math.Abs(offset.Y) > 0.001f || System.Math.Abs(offset.Z) > 0.001f,
                "Expected non-zero Y or Z offset for pitch");
        }

        [Fact]
        public void NumericalCorrectness_Roll15()
        {
            // Manually verify the roll computation
            var settings = new NeckModelSettings(true, 0.10f, 0.08f);
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(0f, 0f, 15f);

            Vec3 neckToEyes = settings.NeckToEyes;
            Vec3 rotated = rotation.Rotate(neckToEyes);
            Vec3 expected = rotated - neckToEyes;

            Vec3 actual = NeckModel.ComputeOffset(rotation, settings);

            Assert.Equal(expected.X, actual.X, precision: 5);
            Assert.Equal(expected.Y, actual.Y, precision: 5);
            Assert.Equal(expected.Z, actual.Z, precision: 5);
        }

        [Fact]
        public void ZeroNeckDimensions_ReturnsZero()
        {
            var settings = new NeckModelSettings(true, 0f, 0f);
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 20f, 15f);

            Vec3 offset = NeckModel.ComputeOffset(rotation, settings);

            Assert.Equal(0f, offset.X, precision: 5);
            Assert.Equal(0f, offset.Y, precision: 5);
            Assert.Equal(0f, offset.Z, precision: 5);
        }
    }
}
