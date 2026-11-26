using System;
using Xunit;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Data
{
    public class Quat4Tests
    {
        private const float Epsilon = 0.0001f;

        [Fact]
        public void Constructor_SetsComponents()
        {
            var q = new Quat4(1f, 2f, 3f, 4f);
            Assert.Equal(1f, q.X);
            Assert.Equal(2f, q.Y);
            Assert.Equal(3f, q.Z);
            Assert.Equal(4f, q.W);
        }

        [Fact]
        public void Identity_ReturnsCorrectQuaternion()
        {
            Quat4 id = Quat4.Identity;
            Assert.Equal(0f, id.X);
            Assert.Equal(0f, id.Y);
            Assert.Equal(0f, id.Z);
            Assert.Equal(1f, id.W);
        }

        [Fact]
        public void Negated_NegatesAllComponents()
        {
            var q = new Quat4(1f, 2f, 3f, 4f);
            Quat4 neg = q.Negated;
            Assert.Equal(-1f, neg.X);
            Assert.Equal(-2f, neg.Y);
            Assert.Equal(-3f, neg.Z);
            Assert.Equal(-4f, neg.W);
        }

        [Fact]
        public void Inverse_NegatesXYZ_KeepsW()
        {
            var q = new Quat4(1f, 2f, 3f, 4f);
            Quat4 inv = q.Inverse;
            Assert.Equal(-1f, inv.X);
            Assert.Equal(-2f, inv.Y);
            Assert.Equal(-3f, inv.Z);
            Assert.Equal(4f, inv.W);
        }

        [Fact]
        public void Inverse_Identity_ReturnsIdentity()
        {
            Quat4 inv = Quat4.Identity.Inverse;
            Assert.Equal(0f, inv.X);
            Assert.Equal(0f, inv.Y);
            Assert.Equal(0f, inv.Z);
            Assert.Equal(1f, inv.W);
        }

        [Fact]
        public void Dot_IdentityWithItself_ReturnsOne()
        {
            float dot = Quat4.Identity.Dot(Quat4.Identity);
            Assert.Equal(1f, dot, precision: 5);
        }

        [Fact]
        public void Dot_OppositeQuaternions_ReturnsNegativeOne()
        {
            float dot = Quat4.Identity.Dot(Quat4.Identity.Negated);
            Assert.Equal(-1f, dot, precision: 5);
        }

        [Fact]
        public void Rotate_Forward_ByIdentity_ReturnsForward()
        {
            Vec3 result = Quat4.Identity.Rotate(Vec3.Forward);
            Assert.Equal(0f, result.X, precision: 5);
            Assert.Equal(0f, result.Y, precision: 5);
            Assert.Equal(1f, result.Z, precision: 5);
        }

        [Fact]
        public void Rotate_RotatesVectorCorrectly()
        {
            Quat4 rot = CameraUnlock.Core.Math.QuaternionUtils.FromYawPitchRoll(90f, 0f, 0f);
            Vec3 result = rot.Rotate(Vec3.Forward);
            Assert.True(System.Math.Abs(result.X - (-1f)) < Epsilon || System.Math.Abs(result.X - 1f) < Epsilon);
            Assert.Equal(0f, result.Y, precision: 3);
            Assert.Equal(0f, result.Z, precision: 3);
        }

        [Fact]
        public void Multiply_IdentityByIdentity_ReturnsIdentity()
        {
            Quat4 result = Quat4.Identity.Multiply(Quat4.Identity);
            Assert.Equal(0f, result.X, precision: 5);
            Assert.Equal(0f, result.Y, precision: 5);
            Assert.Equal(0f, result.Z, precision: 5);
            Assert.Equal(1f, result.W, precision: 5);
        }

        [Fact]
        public void Multiply_QuaternionByIdentity_ReturnsSame()
        {
            var q = new Quat4(0.5f, 0.5f, 0.5f, 0.5f);
            Quat4 result = q.Multiply(Quat4.Identity);
            Assert.Equal(q.X, result.X, precision: 5);
            Assert.Equal(q.Y, result.Y, precision: 5);
            Assert.Equal(q.Z, result.Z, precision: 5);
            Assert.Equal(q.W, result.W, precision: 5);
        }

        [Fact]
        public void MultiplyOperator_WorksCorrectly()
        {
            var a = new Quat4(0.5f, 0.5f, 0.5f, 0.5f);
            Quat4 result = a * Quat4.Identity;
            Assert.Equal(a.X, result.X, precision: 5);
            Assert.Equal(a.Y, result.Y, precision: 5);
            Assert.Equal(a.Z, result.Z, precision: 5);
            Assert.Equal(a.W, result.W, precision: 5);
        }
    }
}
