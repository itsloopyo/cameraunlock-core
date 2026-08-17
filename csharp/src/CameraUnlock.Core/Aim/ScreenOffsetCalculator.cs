using System;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Aim
{
    /// <summary>
    /// Framework-agnostic screen offset calculation using FOV-based tangent projection.
    /// Based on the proven pattern from green-hell's AimController.
    /// </summary>
    public static class ScreenOffsetCalculator
    {
        // Magnitude floor below which the perspective divide / FOV scale is
        // treated as singular. Picked to be small enough that legitimate near-edge
        // poses still project, but large enough to keep the result finite when a
        // call site feeds in a degenerate FOV or a head pose pointing 90° off-axis.
        private const float ProjectionEpsilon = 1e-6f;

        /// <summary>
        /// Calculates screen offset using tangent-based FOV projection.
        /// This method works without Unity's camera system.
        /// </summary>
        /// <param name="yawDegrees">Head tracking yaw offset in degrees.</param>
        /// <param name="pitchDegrees">Head tracking pitch offset in degrees.</param>
        /// <param name="rollDegrees">Head tracking roll offset in degrees.</param>
        /// <param name="horizontalFov">Camera horizontal FOV in degrees.</param>
        /// <param name="verticalFov">Camera vertical FOV in degrees.</param>
        /// <param name="screenWidth">Screen width in pixels.</param>
        /// <param name="screenHeight">Screen height in pixels.</param>
        /// <param name="compensationScale">Optional scale factor for sensitivity tuning (default 1.0).</param>
        /// <param name="offsetX">Output: Screen offset X in pixels from center.</param>
        /// <param name="offsetY">Output: Screen offset Y in pixels from center.</param>
        public static void Calculate(
            float yawDegrees,
            float pitchDegrees,
            float rollDegrees,
            float horizontalFov,
            float verticalFov,
            float screenWidth,
            float screenHeight,
            float compensationScale,
            out float offsetX,
            out float offsetY)
        {
            float halfWidth = screenWidth * 0.5f;
            float halfHeight = screenHeight * 0.5f;

            float tanHalfFovX = (float)System.Math.Tan(horizontalFov * MathConstants.DegToRad * 0.5f);
            float tanHalfFovY = (float)System.Math.Tan(verticalFov * MathConstants.DegToRad * 0.5f);

            // Exact spherical coordinate projection matching camera rotation.
            // Original aim direction decomposed into tracked camera basis:
            //   right   = -sin(yaw)
            //   up      = sin(pitch) * cos(yaw)
            //   forward = cos(pitch) * cos(yaw)
            // The key correction vs independent tangent projection is the 1/cos(pitch)
            // factor on the x term, which prevents "orbiting" when both yaw and pitch are non-zero.
            float yawRad = yawDegrees * MathConstants.DegToRad;
            float pitchRad = pitchDegrees * MathConstants.DegToRad;

            float sinY = (float)System.Math.Sin(yawRad);
            float cosY = (float)System.Math.Cos(yawRad);
            float sinP = (float)System.Math.Sin(pitchRad);
            float cosP = (float)System.Math.Cos(pitchRad);

            float ax = -sinY;
            float ay = sinP * cosY;
            float az = cosP * cosY;

            // Apply roll in direction space (before perspective divide) to avoid
            // elliptical distortion from different horizontal/vertical FOV scales.
            ApplyRollRotation(ax, ay, rollDegrees, out ax, out ay);

            // Perspective divide and FOV scaling. az -> 0 when pitch or yaw approach
            // ±90°, and tanHalf* -> 0 when FOV is degenerate (e.g. zoomed-out menu cam).
            // In either case the projection is undefined; emit zero offset so the
            // reticle stays centred instead of jumping to ±Infinity / NaN on screen.
            // az < 0 is the half-space behind the camera plane: dividing by it mirrors
            // the offset, so a 100° yaw would report a reticle just right of centre
            // while the aim is actually behind the player. Rejected, not clamped.
            if (az < ProjectionEpsilon ||
                System.Math.Abs(tanHalfFovX) < ProjectionEpsilon ||
                System.Math.Abs(tanHalfFovY) < ProjectionEpsilon)
            {
                offsetX = 0f;
                offsetY = 0f;
                return;
            }

            offsetX = (ax / az) / tanHalfFovX * halfWidth * compensationScale;
            offsetY = (ay / az) / tanHalfFovY * halfHeight * compensationScale;
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Calculates screen offset (tuple version for modern runtimes).
        /// </summary>
        public static (float x, float y) CalculateTuple(
            float yawDegrees,
            float pitchDegrees,
            float rollDegrees,
            float horizontalFov,
            float verticalFov,
            float screenWidth,
            float screenHeight,
            float compensationScale = 1.0f)
        {
            Calculate(yawDegrees, pitchDegrees, rollDegrees, horizontalFov, verticalFov,
                screenWidth, screenHeight, compensationScale, out float x, out float y);
            return (x, y);
        }
#endif

        /// <summary>
        /// Pre-computes the tangent of half-FOV values for use with the optimized Calculate overload.
        /// Call this once when FOV changes, then use the tangent values every frame.
        /// </summary>
        /// <param name="horizontalFov">Camera horizontal FOV in degrees.</param>
        /// <param name="verticalFov">Camera vertical FOV in degrees.</param>
        /// <param name="tanHalfFovX">Output: Pre-computed tan(horizontalFov/2).</param>
        /// <param name="tanHalfFovY">Output: Pre-computed tan(verticalFov/2).</param>
        public static void PrecomputeFovTangents(float horizontalFov, float verticalFov,
            out float tanHalfFovX, out float tanHalfFovY)
        {
            tanHalfFovX = (float)System.Math.Tan(horizontalFov * MathConstants.DegToRad * 0.5f);
            tanHalfFovY = (float)System.Math.Tan(verticalFov * MathConstants.DegToRad * 0.5f);
        }

        /// <summary>
        /// Calculates screen offset using pre-computed FOV tangent values.
        /// Use <see cref="PrecomputeFovTangents"/> to compute tangent values once when FOV changes.
        /// </summary>
        /// <param name="yawDegrees">Head tracking yaw offset in degrees.</param>
        /// <param name="pitchDegrees">Head tracking pitch offset in degrees.</param>
        /// <param name="rollDegrees">Head tracking roll offset in degrees.</param>
        /// <param name="tanHalfFovX">Pre-computed tan(horizontalFov/2).</param>
        /// <param name="tanHalfFovY">Pre-computed tan(verticalFov/2).</param>
        /// <param name="halfWidth">Half screen width in pixels.</param>
        /// <param name="halfHeight">Half screen height in pixels.</param>
        /// <param name="compensationScale">Scale factor for sensitivity tuning.</param>
        /// <param name="offsetX">Output: Screen offset X in pixels from center.</param>
        /// <param name="offsetY">Output: Screen offset Y in pixels from center.</param>
        public static void CalculatePrecomputed(
            float yawDegrees,
            float pitchDegrees,
            float rollDegrees,
            float tanHalfFovX,
            float tanHalfFovY,
            float halfWidth,
            float halfHeight,
            float compensationScale,
            out float offsetX,
            out float offsetY)
        {
            float yawRad = yawDegrees * MathConstants.DegToRad;
            float pitchRad = pitchDegrees * MathConstants.DegToRad;

            float sinY = (float)System.Math.Sin(yawRad);
            float cosY = (float)System.Math.Cos(yawRad);
            float sinP = (float)System.Math.Sin(pitchRad);
            float cosP = (float)System.Math.Cos(pitchRad);

            float ax = -sinY;
            float ay = sinP * cosY;
            float az = cosP * cosY;

            ApplyRollRotation(ax, ay, rollDegrees, out ax, out ay);

            // See Calculate(): clamp to zero offset when the projection is singular or
            // behind the camera plane, so callers don't propagate ±Infinity / NaN or a
            // mirrored offset into the reticle UI.
            if (az < ProjectionEpsilon ||
                System.Math.Abs(tanHalfFovX) < ProjectionEpsilon ||
                System.Math.Abs(tanHalfFovY) < ProjectionEpsilon)
            {
                offsetX = 0f;
                offsetY = 0f;
                return;
            }

            offsetX = (ax / az) / tanHalfFovX * halfWidth * compensationScale;
            offsetY = (ay / az) / tanHalfFovY * halfHeight * compensationScale;
        }

        /// <summary>
        /// Calculates horizontal FOV from vertical FOV and aspect ratio.
        /// </summary>
        /// <param name="verticalFov">Vertical FOV in degrees.</param>
        /// <param name="aspectRatio">Screen aspect ratio (width/height).</param>
        /// <returns>Horizontal FOV in degrees.</returns>
        public static float CalculateHorizontalFov(float verticalFov, float aspectRatio)
        {
            float tanHalfVFov = (float)System.Math.Tan(verticalFov * MathConstants.DegToRad * 0.5f);
            return 2f * (float)System.Math.Atan(tanHalfVFov * aspectRatio) / MathConstants.DegToRad;
        }

        /// <summary>
        /// Applies 2D roll rotation to screen offset.
        /// </summary>
        /// <param name="x">X offset input.</param>
        /// <param name="y">Y offset input.</param>
        /// <param name="rollDegrees">Roll angle in degrees.</param>
        /// <param name="rotatedX">Output: Rotated X offset.</param>
        /// <param name="rotatedY">Output: Rotated Y offset.</param>
        public static void ApplyRollRotation(float x, float y, float rollDegrees, out float rotatedX, out float rotatedY)
        {
            if (System.Math.Abs(rollDegrees) < 0.001f)
            {
                rotatedX = x;
                rotatedY = y;
                return;
            }

            // Negate roll to match view matrix convention
            float rollRad = -rollDegrees * MathConstants.DegToRad;
            float cos = (float)System.Math.Cos(rollRad);
            float sin = (float)System.Math.Sin(rollRad);

            rotatedX = x * cos - y * sin;
            rotatedY = x * sin + y * cos;
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Applies 2D roll rotation (tuple version for modern runtimes).
        /// </summary>
        public static (float x, float y) ApplyRollRotationTuple(float x, float y, float rollDegrees)
        {
            ApplyRollRotation(x, y, rollDegrees, out float rx, out float ry);
            return (rx, ry);
        }
#endif
    }
}
