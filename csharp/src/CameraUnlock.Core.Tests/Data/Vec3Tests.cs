using System;
using Xunit;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Data
{
    public class Vec3Tests
    {
        private const float Epsilon = 0.0001f;

        [Fact]
        public void Constructor_SetsComponents()
        {
            var v = new Vec3(1f, 2f, 3f);
            Assert.Equal(1f, v.X);
            Assert.Equal(2f, v.Y);
            Assert.Equal(3f, v.Z);
        }

        [Fact]
        public void Zero_ReturnsZeroVector()
        {
            Vec3 z = Vec3.Zero;
            Assert.Equal(0f, z.X);
            Assert.Equal(0f, z.Y);
            Assert.Equal(0f, z.Z);
        }

        [Fact]
        public void Forward_ReturnsCorrectDirection()
        {
            Vec3 f = Vec3.Forward;
            Assert.Equal(0f, f.X);
            Assert.Equal(0f, f.Y);
            Assert.Equal(1f, f.Z);
        }

        [Fact]
        public void Up_ReturnsCorrectDirection()
        {
            Vec3 u = Vec3.Up;
            Assert.Equal(0f, u.X);
            Assert.Equal(1f, u.Y);
            Assert.Equal(0f, u.Z);
        }

        [Fact]
        public void Right_ReturnsCorrectDirection()
        {
            Vec3 r = Vec3.Right;
            Assert.Equal(1f, r.X);
            Assert.Equal(0f, r.Y);
            Assert.Equal(0f, r.Z);
        }

        [Fact]
        public void SqrMagnitude_ReturnsCorrectValue()
        {
            var v = new Vec3(3f, 4f, 0f);
            Assert.Equal(25f, v.SqrMagnitude);
        }

        [Fact]
        public void Magnitude_ReturnsCorrectValue()
        {
            var v = new Vec3(3f, 4f, 0f);
            Assert.Equal(5f, v.Magnitude, precision: 5);
        }

        [Fact]
        public void Normalized_ReturnsUnitVector()
        {
            var v = new Vec3(3f, 4f, 0f);
            Vec3 n = v.Normalized;
            Assert.Equal(1f, n.Magnitude, precision: 5);
            Assert.Equal(0.6f, n.X, precision: 5);
            Assert.Equal(0.8f, n.Y, precision: 5);
        }

        [Fact]
        public void Normalized_ZeroVector_ReturnsZero()
        {
            Vec3 n = Vec3.Zero.Normalized;
            Assert.Equal(0f, n.X);
            Assert.Equal(0f, n.Y);
            Assert.Equal(0f, n.Z);
        }

        [Fact]
        public void Addition_AddsComponents()
        {
            var a = new Vec3(1f, 2f, 3f);
            var b = new Vec3(4f, 5f, 6f);
            Vec3 r = a + b;
            Assert.Equal(5f, r.X);
            Assert.Equal(7f, r.Y);
            Assert.Equal(9f, r.Z);
        }

        [Fact]
        public void Subtraction_SubtractsComponents()
        {
            var a = new Vec3(4f, 5f, 6f);
            var b = new Vec3(1f, 2f, 3f);
            Vec3 r = a - b;
            Assert.Equal(3f, r.X);
            Assert.Equal(3f, r.Y);
            Assert.Equal(3f, r.Z);
        }

        [Fact]
        public void ScalarMultiplication_VectorTimesScalar()
        {
            var v = new Vec3(1f, 2f, 3f);
            Vec3 r = v * 2f;
            Assert.Equal(2f, r.X);
            Assert.Equal(4f, r.Y);
            Assert.Equal(6f, r.Z);
        }

        [Fact]
        public void ScalarMultiplication_ScalarTimesVector()
        {
            var v = new Vec3(1f, 2f, 3f);
            Vec3 r = 2f * v;
            Assert.Equal(2f, r.X);
            Assert.Equal(4f, r.Y);
            Assert.Equal(6f, r.Z);
        }

        [Fact]
        public void Negation_NegatesComponents()
        {
            var v = new Vec3(1f, -2f, 3f);
            Vec3 r = -v;
            Assert.Equal(-1f, r.X);
            Assert.Equal(2f, r.Y);
            Assert.Equal(-3f, r.Z);
        }

        [Fact]
        public void Dot_ReturnsCorrectValue()
        {
            var a = new Vec3(1f, 2f, 3f);
            var b = new Vec3(4f, 5f, 6f);
            float dot = Vec3.Dot(a, b);
            Assert.Equal(32f, dot);
        }

        [Fact]
        public void Dot_PerpendicularVectors_ReturnsZero()
        {
            float dot = Vec3.Dot(Vec3.Right, Vec3.Up);
            Assert.Equal(0f, dot);
        }

        [Fact]
        public void Cross_RightAndUp_ReturnsForward()
        {
            // Right-hand rule: Right × Up = Forward
            Vec3 c = Vec3.Cross(Vec3.Right, Vec3.Up);
            Assert.Equal(0f, c.X, precision: 5);
            Assert.Equal(0f, c.Y, precision: 5);
            Assert.Equal(1f, c.Z, precision: 5);
        }

        [Fact]
        public void Cross_UpAndForward_ReturnsRight()
        {
            // Right-hand rule: Up × Forward = Right
            Vec3 c = Vec3.Cross(Vec3.Up, Vec3.Forward);
            Assert.Equal(1f, c.X, precision: 5);
            Assert.Equal(0f, c.Y, precision: 5);
            Assert.Equal(0f, c.Z, precision: 5);
        }

        [Fact]
        public void Lerp_T0_ReturnsA()
        {
            var a = new Vec3(1f, 2f, 3f);
            var b = new Vec3(4f, 5f, 6f);
            Vec3 r = Vec3.Lerp(a, b, 0f);
            Assert.Equal(1f, r.X);
            Assert.Equal(2f, r.Y);
            Assert.Equal(3f, r.Z);
        }

        [Fact]
        public void Lerp_T1_ReturnsB()
        {
            var a = new Vec3(1f, 2f, 3f);
            var b = new Vec3(4f, 5f, 6f);
            Vec3 r = Vec3.Lerp(a, b, 1f);
            Assert.Equal(4f, r.X);
            Assert.Equal(5f, r.Y);
            Assert.Equal(6f, r.Z);
        }

        [Fact]
        public void Lerp_Midpoint_ReturnsAverage()
        {
            var a = new Vec3(0f, 0f, 0f);
            var b = new Vec3(10f, 20f, 30f);
            Vec3 r = Vec3.Lerp(a, b, 0.5f);
            Assert.Equal(5f, r.X, precision: 5);
            Assert.Equal(10f, r.Y, precision: 5);
            Assert.Equal(15f, r.Z, precision: 5);
        }

        [Fact]
        public void ToString_FormatsCorrectly()
        {
            var v = new Vec3(1.234f, 2.345f, 3.456f);
            string s = v.ToString();
            Assert.Contains("1.234", s);
            Assert.Contains("2.345", s);
            Assert.Contains("3.456", s);
        }
    }
}
