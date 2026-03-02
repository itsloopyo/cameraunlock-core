using System;
using Xunit;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Math
{
    public class QuaternionUtilsTests
    {
        private const float Epsilon = 0.0001f;

        [Fact]
        public void FromYawPitchRoll_ZeroAngles_ReturnsIdentity()
        {
            Quat4 result = QuaternionUtils.FromYawPitchRoll(0f, 0f, 0f);
            AssertQuaternionEqual(Quat4.Identity, result);
        }

        [Fact]
        public void FromYawPitchRoll_90YawOnly_CreatesCorrectRotation()
        {
            Quat4 result = QuaternionUtils.FromYawPitchRoll(90f, 0f, 0f);
            Assert.True(System.Math.Abs(result.Y) > 0.5f);
            Assert.Equal(0f, result.X, precision: 4);
            Assert.Equal(0f, result.Z, precision: 4);
        }

        [Fact]
        public void FromYawPitchRoll_90PitchOnly_CreatesCorrectRotation()
        {
            Quat4 result = QuaternionUtils.FromYawPitchRoll(0f, 90f, 0f);
            Assert.True(System.Math.Abs(result.X) > 0.5f);
            Assert.Equal(0f, result.Y, precision: 4);
            Assert.Equal(0f, result.Z, precision: 4);
        }

        [Fact]
        public void FromYawPitchRoll_90RollOnly_CreatesCorrectRotation()
        {
            Quat4 result = QuaternionUtils.FromYawPitchRoll(0f, 0f, 90f);
            Assert.True(System.Math.Abs(result.Z) > 0.5f);
            Assert.Equal(0f, result.X, precision: 4);
            Assert.Equal(0f, result.Y, precision: 4);
        }

        [Fact]
        public void Multiply_IdentityByIdentity_ReturnsIdentity()
        {
            Quat4 result = QuaternionUtils.Multiply(Quat4.Identity, Quat4.Identity);
            AssertQuaternionEqual(Quat4.Identity, result);
        }

        [Fact]
        public void Multiply_QuaternionByIdentity_ReturnsSame()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(45f, 30f, 15f);
            Quat4 result = QuaternionUtils.Multiply(q, Quat4.Identity);
            AssertQuaternionEqual(q, result);
        }

        [Fact]
        public void Multiply_IdentityByQuaternion_ReturnsSame()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(45f, 30f, 15f);
            Quat4 result = QuaternionUtils.Multiply(Quat4.Identity, q);
            AssertQuaternionEqual(q, result);
        }

        [Fact]
        public void Inverse_Identity_ReturnsIdentity()
        {
            Quat4 result = QuaternionUtils.Inverse(Quat4.Identity);
            AssertQuaternionEqual(Quat4.Identity, result);
        }

        [Fact]
        public void Inverse_MultiplyByInverse_ReturnsIdentity()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(45f, 30f, 15f);
            Quat4 inv = QuaternionUtils.Inverse(q);
            Quat4 result = QuaternionUtils.Multiply(q, inv);
            AssertQuaternionEqual(Quat4.Identity, result);
        }

        [Fact]
        public void Slerp_T0_ReturnsFirst()
        {
            Quat4 a = QuaternionUtils.FromYawPitchRoll(0f, 0f, 0f);
            Quat4 b = QuaternionUtils.FromYawPitchRoll(90f, 0f, 0f);
            Quat4 result = QuaternionUtils.Slerp(a, b, 0f);
            AssertQuaternionEqual(a, result);
        }

        [Fact]
        public void Slerp_T1_ReturnsSecond()
        {
            Quat4 a = QuaternionUtils.FromYawPitchRoll(0f, 0f, 0f);
            Quat4 b = QuaternionUtils.FromYawPitchRoll(90f, 0f, 0f);
            Quat4 result = QuaternionUtils.Slerp(a, b, 1f);
            AssertQuaternionEqual(b, result);
        }

        [Fact]
        public void Slerp_T05_ReturnsMidpoint()
        {
            Quat4 a = Quat4.Identity;
            Quat4 b = QuaternionUtils.FromYawPitchRoll(90f, 0f, 0f);
            Quat4 result = QuaternionUtils.Slerp(a, b, 0.5f);
            Quat4 expected = QuaternionUtils.FromYawPitchRoll(45f, 0f, 0f);
            AssertQuaternionEqual(expected, result);
        }

        [Fact]
        public void Slerp_IdenticalQuaternions_ReturnsSame()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(45f, 30f, 15f);
            Quat4 result = QuaternionUtils.Slerp(q, q, 0.5f);
            AssertQuaternionEqual(q, result);
        }

        [Fact]
        public void Normalize_UnitQuaternion_ReturnsSame()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(45f, 30f, 15f);
            Quat4 result = QuaternionUtils.Normalize(q);
            AssertQuaternionEqual(q, result);
        }

        [Fact]
        public void Normalize_NonUnitQuaternion_ReturnsUnitLength()
        {
            var q = new Quat4(2f, 0f, 0f, 2f);
            Quat4 result = QuaternionUtils.Normalize(q);
            float length = (float)System.Math.Sqrt(result.X * result.X + result.Y * result.Y + result.Z * result.Z + result.W * result.W);
            Assert.Equal(1f, length, precision: 5);
        }

        [Fact]
        public void Normalize_ZeroQuaternion_ReturnsIdentity()
        {
            var q = new Quat4(0f, 0f, 0f, 0f);
            Quat4 result = QuaternionUtils.Normalize(q);
            AssertQuaternionEqual(Quat4.Identity, result);
        }

        [Fact]
        public void Identity_IsCorrectValue()
        {
            Assert.Equal(Quat4.Identity.X, QuaternionUtils.Identity.X);
            Assert.Equal(Quat4.Identity.Y, QuaternionUtils.Identity.Y);
            Assert.Equal(Quat4.Identity.Z, QuaternionUtils.Identity.Z);
            Assert.Equal(Quat4.Identity.W, QuaternionUtils.Identity.W);
        }

        // --- ToEulerYXZ tests ---

        [Fact]
        public void ToEulerYXZ_Identity_ReturnsZeros()
        {
            QuaternionUtils.ToEulerYXZ(Quat4.Identity, out float yaw, out float pitch, out float roll);
            Assert.Equal(0f, yaw, precision: 4);
            Assert.Equal(0f, pitch, precision: 4);
            Assert.Equal(0f, roll, precision: 4);
        }

        [Fact]
        public void ToEulerYXZ_90Yaw_ReturnsCorrect()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(90f, 0f, 0f);
            QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
            Assert.Equal(90f, yaw, precision: 3);
            Assert.Equal(0f, pitch, precision: 3);
            Assert.Equal(0f, roll, precision: 3);
        }

        [Fact]
        public void ToEulerYXZ_45Pitch_ReturnsCorrect()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(0f, 45f, 0f);
            QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
            Assert.Equal(0f, yaw, precision: 3);
            Assert.Equal(45f, pitch, precision: 3);
            Assert.Equal(0f, roll, precision: 3);
        }

        [Fact]
        public void ToEulerYXZ_30Roll_ReturnsCorrect()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(0f, 0f, 30f);
            QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
            Assert.Equal(0f, yaw, precision: 3);
            Assert.Equal(0f, pitch, precision: 3);
            Assert.Equal(30f, roll, precision: 3);
        }

        [Theory]
        [InlineData(45f, 30f, 15f)]
        [InlineData(-60f, 20f, -10f)]
        [InlineData(120f, -40f, 25f)]
        [InlineData(0f, 0f, 0f)]
        public void ToEulerYXZ_RoundTrip_ReturnsOriginal(float inYaw, float inPitch, float inRoll)
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(inYaw, inPitch, inRoll);
            QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
            Assert.Equal(inYaw, yaw, precision: 3);
            Assert.Equal(inPitch, pitch, precision: 3);
            Assert.Equal(inRoll, roll, precision: 3);
        }

        [Fact]
        public void ToEulerYXZ_NearGimbalLock_PitchIsCorrect()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(0f, 89f, 0f);
            QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
            Assert.Equal(89f, pitch, precision: 2);
        }

        [Fact]
        public void ToEulerYXZ_AtGimbalLock_PitchIs90_YawPlusRollPreserved()
        {
            Quat4 q = QuaternionUtils.FromYawPitchRoll(0f, 90f, 0f);
            QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
            // Float precision means sinPitch ≈ 0.99999 rather than exactly 1.0,
            // so asin returns ~89.98 instead of exactly 90
            Assert.Equal(90f, pitch, precision: 1);
            Assert.Equal(0f, roll, precision: 1);
        }

        private void AssertQuaternionEqual(Quat4 expected, Quat4 actual)
        {
            float dot = expected.X * actual.X + expected.Y * actual.Y +
                        expected.Z * actual.Z + expected.W * actual.W;
            Assert.True(System.Math.Abs(System.Math.Abs(dot) - 1f) < Epsilon,
                $"Quaternions not equal. Expected: ({expected.X}, {expected.Y}, {expected.Z}, {expected.W}), " +
                $"Actual: ({actual.X}, {actual.Y}, {actual.Z}, {actual.W}), Dot: {dot}");
        }
    }
}
