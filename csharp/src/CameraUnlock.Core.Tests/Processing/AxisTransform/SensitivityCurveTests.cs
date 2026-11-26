using System;
using Xunit;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Tests.Processing.AxisTransform
{
    public class SensitivityCurveTests
    {
        [Fact]
        public void Linear_ReturnsOne()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Linear, 0.5f, 1.0f);

            // Linear always returns 1.0 (no modification)
            Assert.Equal(1.0f, result, precision: 4);
        }

        [Fact]
        public void Quadratic_AtZero_ReturnsOne()
        {
            // At input 0, quadratic curve = 0*0 = 0, lerped with strength 1 = 0
            // But strength 0 means full linear (1.0)
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 0f, 1.0f);

            Assert.True(result < 0.01f); // Near zero at input 0
        }

        [Fact]
        public void Quadratic_AtOne_ReturnsOne()
        {
            // At input 1, quadratic curve = 1*1 = 1
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 1.0f, 1.0f);

            Assert.Equal(1.0f, result, precision: 4);
        }

        [Fact]
        public void Quadratic_AtHalf_ReturnsLessThanLinear()
        {
            // At input 0.5, quadratic curve = 0.25
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 0.5f, 1.0f);

            Assert.True(result < 1.0f);
            Assert.True(result > 0.0f);
        }

        [Fact]
        public void Cubic_AtHalf_ReturnsLessThanQuadratic()
        {
            float quadratic = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 0.5f, 1.0f);
            float cubic = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Cubic, 0.5f, 1.0f);

            Assert.True(cubic < quadratic);
        }

        [Fact]
        public void Exponential_AtZero_ReturnsNearZero()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Exponential, 0f, 1.0f);

            Assert.True(result < 0.1f);
        }

        [Fact]
        public void Exponential_AtOne_ReturnsOne()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Exponential, 1.0f, 1.0f);

            Assert.Equal(1.0f, result, precision: 2);
        }

        [Fact]
        public void Logarithmic_AtZero_ReturnsNearZero()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Logarithmic, 0f, 1.0f);

            Assert.True(result < 0.1f);
        }

        [Fact]
        public void Logarithmic_AtOne_ReturnsOne()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Logarithmic, 1.0f, 1.0f);

            Assert.Equal(1.0f, result, precision: 2);
        }

        [Fact]
        public void SCurve_AtZero_ReturnsNearZero()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.SCurve, 0f, 1.0f);

            Assert.True(result < 0.1f);
        }

        [Fact]
        public void SCurve_AtOne_ReturnsOne()
        {
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.SCurve, 1.0f, 1.0f);

            Assert.Equal(1.0f, result, precision: 2);
        }

        [Fact]
        public void SCurve_AtHalf_ReturnsHalf()
        {
            // S-curve should pass through 0.5 at input 0.5
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.SCurve, 0.5f, 1.0f);

            Assert.Equal(0.5f, result, precision: 2);
        }

        [Fact]
        public void Custom_ThrowsOnNullFunction()
        {
            Assert.Throws<ArgumentException>(() =>
                SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Custom, 0.5f, 1.0f, null));
        }

        [Fact]
        public void Custom_UsesProvidedFunction()
        {
            // Custom function that returns double the input
            Func<float, float> customFunc = input => input * 2f;

            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Custom, 0.3f, 1.0f, customFunc);

            Assert.Equal(0.6f, result, precision: 4);
        }

        [Fact]
        public void ZeroStrength_ReturnsLinear()
        {
            // With strength 0, should always return 1.0 (linear)
            float quadResult = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 0.5f, 0f);
            float cubicResult = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Cubic, 0.5f, 0f);

            Assert.Equal(1.0f, quadResult, precision: 4);
            Assert.Equal(1.0f, cubicResult, precision: 4);
        }

        [Fact]
        public void PartialStrength_BlendsCurve()
        {
            float fullCurve = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 0.5f, 1.0f);
            float halfCurve = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 0.5f, 0.5f);

            // Half strength should be between linear (1.0) and full curve
            Assert.True(halfCurve > fullCurve);
            Assert.True(halfCurve < 1.0f);
        }

        [Fact]
        public void InputClamped_BelowZero()
        {
            // Negative input should be clamped to 0
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, -0.5f, 1.0f);

            // At input 0, quadratic returns 0
            Assert.True(result < 0.1f);
        }

        [Fact]
        public void InputClamped_AboveOne()
        {
            // Input above 1 should be clamped to 1
            float result = SensitivityCurveUtils.ApplyCurve(SensitivityCurve.Quadratic, 1.5f, 1.0f);

            Assert.Equal(1.0f, result, precision: 4);
        }
    }
}
