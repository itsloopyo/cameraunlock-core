using System;
using CameraUnlock.Core.Math;
using SysMath = System.Math;

namespace CameraUnlock.Core.Processing.AxisTransform
{
    /// <summary>
    /// Sensitivity curve types for non-linear input response.
    /// These curves affect how input values map to output values,
    /// allowing fine-grained control in the center and faster response at extremes (or vice versa).
    /// </summary>
    public enum SensitivityCurve
    {
        /// <summary>
        /// Linear response (1:1 mapping). Default behavior.
        /// </summary>
        Linear,

        /// <summary>
        /// Quadratic response (x²). More precision near center, faster at extremes.
        /// Good for precise aiming.
        /// </summary>
        Quadratic,

        /// <summary>
        /// Cubic response (x³). Even more precision near center than quadratic.
        /// </summary>
        Cubic,

        /// <summary>
        /// Exponential response ((e^(2x) - 1)/(e^2 - 1)). Aggressive acceleration.
        /// </summary>
        Exponential,

        /// <summary>
        /// Logarithmic response (log10(x*9 + 1)). Faster at center, slower at extremes.
        /// Good for quick initial response.
        /// </summary>
        Logarithmic,

        /// <summary>
        /// S-curve (smoothstep). Gentle acceleration and deceleration.
        /// Natural feeling response for simulation.
        /// </summary>
        SCurve,

        /// <summary>
        /// Custom curve provided by user-defined function.
        /// </summary>
        Custom
    }

    /// <summary>
    /// Utility methods for applying sensitivity curves to input values.
    /// </summary>
    public static class SensitivityCurveUtils
    {
        // Pre-calculated constant: Math.Exp(2.0) - 1.0 ≈ 6.389
        private const double Exp2Minus1 = 6.38905609893065;
        /// <summary>
        /// Applies the specified sensitivity curve to a normalized input value.
        /// </summary>
        /// <param name="curve">The curve type to apply.</param>
        /// <param name="normalizedInput">Input value in range [0, 1].</param>
        /// <param name="strength">Curve strength (0 = linear, 1 = full curve effect).</param>
        /// <param name="customCurveFunc">Optional custom curve function (required if curve is Custom).</param>
        /// <returns>Output value after curve application.</returns>
        /// <exception cref="ArgumentException">Thrown if curve is Custom but customCurveFunc is null.</exception>
#if NULLABLE_ENABLED
        public static float ApplyCurve(
            SensitivityCurve curve,
            float normalizedInput,
            float strength,
            Func<float, float>? customCurveFunc = null)
#else
        public static float ApplyCurve(
            SensitivityCurve curve,
            float normalizedInput,
            float strength,
            Func<float, float> customCurveFunc = null)
#endif
        {
            // Clamp input to valid range
            normalizedInput = SysMath.Max(0f, SysMath.Min(1f, normalizedInput));

            float curveValue;

            switch (curve)
            {
                case SensitivityCurve.Linear:
                    curveValue = 1.0f;
                    break;

                case SensitivityCurve.Quadratic:
                    curveValue = normalizedInput * normalizedInput;
                    break;

                case SensitivityCurve.Cubic:
                    curveValue = normalizedInput * normalizedInput * normalizedInput;
                    break;

                case SensitivityCurve.Exponential:
                    // (e^(x*2) - 1) / (e^2 - 1) - normalized exponential
                    curveValue = (float)((SysMath.Exp(normalizedInput * 2.0) - 1.0) / Exp2Minus1);
                    break;

                case SensitivityCurve.Logarithmic:
                    // log10(x*9 + 1) - normalized logarithmic
                    curveValue = (float)SysMath.Log10(normalizedInput * 9.0 + 1.0);
                    break;

                case SensitivityCurve.SCurve:
                    // Smoothstep function: 3x² - 2x³
                    curveValue = normalizedInput * normalizedInput * (3f - 2f * normalizedInput);
                    break;

                case SensitivityCurve.Custom:
                    if (customCurveFunc == null)
                    {
                        throw new ArgumentException(
                            "Custom curve function must be provided when using SensitivityCurve.Custom",
                            nameof(customCurveFunc));
                    }
                    curveValue = customCurveFunc(normalizedInput);
                    break;

                default:
                    curveValue = 1.0f;
                    break;
            }

            // Blend between linear (1.0) and curve value based on strength
            return MathUtils.Lerp(1.0f, curveValue, strength);
        }
    }
}
