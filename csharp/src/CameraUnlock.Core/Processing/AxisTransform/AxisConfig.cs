using System;
using SysMath = System.Math;

namespace CameraUnlock.Core.Processing.AxisTransform
{
    /// <summary>
    /// Available input axis sources from tracking data.
    /// </summary>
    public enum AxisSource
    {
        /// <summary>Yaw rotation (horizontal head turn).</summary>
        Yaw,
        /// <summary>Pitch rotation (vertical head tilt).</summary>
        Pitch,
        /// <summary>Roll rotation (head tilt side to side).</summary>
        Roll,
        /// <summary>X translation (lateral movement).</summary>
        X,
        /// <summary>Y translation (vertical movement).</summary>
        Y,
        /// <summary>Z translation (forward/backward movement).</summary>
        Z,
        /// <summary>No input (disabled axis).</summary>
        None
    }

    /// <summary>
    /// Configuration for transforming a single tracking axis.
    /// Applies deadzone, sensitivity curve, sensitivity multiplier, inversion, and limits.
    ///
    /// Processing order: Input → Deadzone → Curve → Sensitivity → Inversion → Limits → Output
    /// </summary>
    public class AxisConfig
    {
        /// <summary>
        /// Which input axis to read from.
        /// </summary>
        public AxisSource Source { get; set; } = AxisSource.Yaw;


        /// <summary>
        /// Sensitivity multiplier. 1.0 = normal, &gt;1 = more sensitive, &lt;1 = less sensitive.
        /// </summary>
        public float Sensitivity { get; set; } = 1.0f;

        /// <summary>
        /// Whether to invert the axis output.
        /// </summary>
        public bool Inverted { get; set; }

        /// <summary>
        /// Minimum deadzone threshold (in degrees). Input below this is treated as zero.
        /// </summary>
        public float DeadzoneMin { get; set; }

        /// <summary>
        /// Maximum deadzone threshold (in degrees). Values between Min and Max are scaled smoothly.
        /// Set equal to DeadzoneMin to disable smooth transition.
        /// </summary>
        public float DeadzoneMax { get; set; }

        /// <summary>
        /// Minimum output limit (in degrees). Output is clamped to this value.
        /// </summary>
        public float MinLimit { get; set; } = -180f;

        /// <summary>
        /// Maximum output limit (in degrees). Output is clamped to this value.
        /// </summary>
        public float MaxLimit { get; set; } = 180f;

        /// <summary>
        /// Whether to apply output limits.
        /// </summary>
        public bool EnableLimits { get; set; }

        /// <summary>
        /// Sensitivity curve type to apply.
        /// </summary>
        public SensitivityCurve SensitivityCurve { get; set; } = SensitivityCurve.Linear;

        /// <summary>
        /// Strength of the sensitivity curve (0 = linear, 1 = full curve effect).
        /// </summary>
        public float CurveStrength { get; set; } = 1.0f;

        /// <summary>
        /// Custom curve function for SensitivityCurve.Custom.
        /// Input: normalized value [0,1]. Output: curve value [0,1].
        /// </summary>
#if NULLABLE_ENABLED
        public Func<float, float>? CustomCurveFunc { get; set; }
#else
        public Func<float, float> CustomCurveFunc { get; set; }
#endif

        /// <summary>
        /// Input magnitude (degrees) that maps to the top of the sensitivity curve.
        /// <para>
        /// Defaults to 45, not 180. Head-tracking input lives within roughly +/-30
        /// degrees, so normalising against a half-turn pinned every non-linear curve to
        /// its near-zero end: the shipped Competitive preset advertises "fast yaw" with
        /// Sensitivity 1.2 and a quadratic curve, and at a 10 degree input the curve
        /// multiplier collapsed to 0.50, making it 0.60x overall - slower than the
        /// Default preset it claims to beat.
        /// </para>
        /// <para>
        /// Values at or below zero are rejected on assignment: 0 made the normalisation
        /// 0/0 = NaN for an at-rest axis, and NaN survives both Min and Max clamps
        /// (every comparison against it is false), so a single at-rest frame poisoned
        /// the axis permanently.
        /// </para>
        /// </summary>
        public float MaxInputRange
        {
            get { return _maxInputRange; }
            set { _maxInputRange = value > 0f ? value : DefaultMaxInputRange; }
        }

        /// <summary>Default for <see cref="MaxInputRange"/>.</summary>
        public const float DefaultMaxInputRange = 45f;

        private float _maxInputRange = DefaultMaxInputRange;

        /// <summary>
        /// Applies all transformations to the input value.
        /// Processing order: Deadzone → Curve → Sensitivity → Inversion → Limits
        /// </summary>
        /// <param name="input">Raw input value in degrees.</param>
        /// <returns>Transformed output value in degrees.</returns>
        public float TransformValue(float input)
        {
            // Source = None means this axis is disabled
            if (Source == AxisSource.None)
            {
                return 0f;
            }

            // Apply deadzone
            input = ApplyDeadzone(input);

            // Apply sensitivity curve
            float curveMultiplier = ApplySensitivityCurve(input);

            // Apply sensitivity and curve
            float result = input * Sensitivity * curveMultiplier;

            // Apply inversion
            if (Inverted)
            {
                result = -result;
            }

            // Apply limits
            if (EnableLimits)
            {
                result = SysMath.Max(MinLimit, SysMath.Min(MaxLimit, result));
            }

            return result;
        }

        /// <summary>
        /// Applies deadzone processing to the input value.
        /// Values below DeadzoneMin are zeroed.
        /// Values between DeadzoneMin and DeadzoneMax are smoothly scaled.
        /// </summary>
        private float ApplyDeadzone(float input)
        {
            float absInput = SysMath.Abs(input);
            float sign = input >= 0 ? 1f : -1f;

            // Below minimum deadzone = zero
            if (absInput < DeadzoneMin)
            {
                return 0f;
            }

            // If smooth deadzone is configured (max > min), apply smooth scaling
            if (DeadzoneMax > DeadzoneMin && absInput < DeadzoneMax)
            {
                float deadzoneRange = DeadzoneMax - DeadzoneMin;
                float normalizedInput = (absInput - DeadzoneMin) / deadzoneRange;
                return sign * normalizedInput * DeadzoneMax;
            }

            // No smooth band configured (DeadzoneMax <= DeadzoneMin, which is the
            // DEFAULT): subtract the threshold rather than passing the input through
            // untouched. Returning `input` here put a hard step at the boundary - with
            // DeadzoneMin = 5, an input of 4.99 gave 0 and 5.01 gave 5.01, so a 0.02
            // degree head movement popped the camera 5 degrees. This is the same
            // continuous form DeadzoneUtils.Apply uses.
            if (DeadzoneMax <= DeadzoneMin)
            {
                return sign * (absInput - DeadzoneMin);
            }

            return input;
        }

        /// <summary>
        /// Applies sensitivity curve to the input and returns a multiplier.
        /// </summary>
        private float ApplySensitivityCurve(float input)
        {
            // Normalize input to [0, 1] range
            float normalizedInput = SysMath.Abs(input) / MaxInputRange;
            normalizedInput = SysMath.Max(0f, SysMath.Min(1f, normalizedInput));

            return SensitivityCurveUtils.ApplyCurve(
                SensitivityCurve,
                normalizedInput,
                CurveStrength,
                CustomCurveFunc);
        }

        /// <summary>
        /// Creates a copy of this configuration.
        /// </summary>
        /// <returns>A new AxisConfig with the same settings.</returns>
        public AxisConfig Clone()
        {
            return new AxisConfig
            {
                Source = Source,
                Sensitivity = Sensitivity,
                Inverted = Inverted,
                DeadzoneMin = DeadzoneMin,
                DeadzoneMax = DeadzoneMax,
                MinLimit = MinLimit,
                MaxLimit = MaxLimit,
                EnableLimits = EnableLimits,
                SensitivityCurve = SensitivityCurve,
                CurveStrength = CurveStrength,
                CustomCurveFunc = CustomCurveFunc,
                MaxInputRange = MaxInputRange
            };
        }
    }
}
