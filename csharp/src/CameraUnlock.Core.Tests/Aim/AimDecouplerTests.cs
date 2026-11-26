using System;
using Xunit;
using CameraUnlock.Core.Aim;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Tests.Aim
{
    public class AimDecouplerTests
    {
        [Fact]
        public void ComputeAimDirectionLocal_ZeroRotation_ReturnsForward()
        {
            Vec3 result = AimDecoupler.ComputeAimDirectionLocal(0f, 0f, 0f);
            Assert.Equal(0f, result.X, precision: 3);
            Assert.Equal(0f, result.Y, precision: 3);
            Assert.Equal(1f, result.Z, precision: 3);
        }

        [Fact]
        public void ComputeAimDirectionLocal_Quat_ZeroRotation_ReturnsForward()
        {
            Vec3 result = AimDecoupler.ComputeAimDirectionLocal(Quat4.Identity);
            Assert.Equal(0f, result.X, precision: 3);
            Assert.Equal(0f, result.Y, precision: 3);
            Assert.Equal(1f, result.Z, precision: 3);
        }

        [Fact]
        public void ComputeAimDirectionLocal_YawRight_AimPointsLeft()
        {
            Vec3 result = AimDecoupler.ComputeAimDirectionLocal(45f, 0f, 0f);
            Assert.True(result.X < 0, "Yaw right should make aim point left (negative X)");
            Assert.Equal(0f, result.Y, precision: 3);
            Assert.True(result.Z > 0);
        }

        [Fact]
        public void ComputeAimDirectionLocal_PitchUp_AimPointsUp()
        {
            // When head pitches up, inverse rotation pitches down, so aim direction has positive Y
            Vec3 result = AimDecoupler.ComputeAimDirectionLocal(0f, 30f, 0f);
            Assert.Equal(0f, result.X, precision: 3);
            Assert.True(result.Y > 0, "Pitch up tracking -> inverse pitches down -> aim has positive Y");
            Assert.True(result.Z > 0);
        }

        [Fact]
        public void ComputeAimDirection_OutParams_ReturnsComponents()
        {
            AimDecoupler.ComputeAimDirection(0f, 0f, 0f, out float x, out float y, out float z);
            Assert.Equal(0f, x, precision: 3);
            Assert.Equal(0f, y, precision: 3);
            Assert.Equal(1f, z, precision: 3);
        }

        [Fact]
        public void ComputeInverseTracking_ZeroRotation_ReturnsIdentity()
        {
            Quat4 result = AimDecoupler.ComputeInverseTracking(0f, 0f, 0f);
            Assert.Equal(0f, result.X, precision: 4);
            Assert.Equal(0f, result.Y, precision: 4);
            Assert.Equal(0f, result.Z, precision: 4);
            Assert.Equal(1f, result.W, precision: 4);
        }

        [Fact]
        public void ComputeInverseTracking_OutParams_ReturnsComponents()
        {
            AimDecoupler.ComputeInverseTracking(0f, 0f, 0f, out float x, out float y, out float z, out float w);
            Assert.Equal(0f, x, precision: 4);
            Assert.Equal(0f, y, precision: 4);
            Assert.Equal(0f, z, precision: 4);
            Assert.Equal(1f, w, precision: 4);
        }

        [Fact]
        public void ComputeInverseTracking_NonZeroRotation_IsInverse()
        {
            Quat4 original = QuaternionUtils.FromYawPitchRoll(45f, 30f, 15f);
            Quat4 inverse = AimDecoupler.ComputeInverseTracking(45f, 30f, 15f);

            Quat4 multiplied = QuaternionUtils.Multiply(original, inverse);
            Assert.Equal(0f, multiplied.X, precision: 3);
            Assert.Equal(0f, multiplied.Y, precision: 3);
            Assert.Equal(0f, multiplied.Z, precision: 3);
            Assert.True(System.Math.Abs(multiplied.W) > 0.99f);
        }

        [Fact]
        public void ComputeAimDirectionLocal_InverseOfTracking_ReconstructsForward()
        {
            Quat4 tracking = QuaternionUtils.FromYawPitchRoll(30f, 20f, 10f);
            Quat4 inverse = tracking.Inverse;

            Vec3 rotatedForward = tracking.Rotate(Vec3.Forward);
            Vec3 reconstructed = inverse.Rotate(rotatedForward);

            Assert.Equal(Vec3.Forward.X, reconstructed.X, precision: 3);
            Assert.Equal(Vec3.Forward.Y, reconstructed.Y, precision: 3);
            Assert.Equal(Vec3.Forward.Z, reconstructed.Z, precision: 3);
        }
    }
}
