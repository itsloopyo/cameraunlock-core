using System;
using CameraUnlock.Core.Data;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Math
{
    /// <summary>
    /// Framework-agnostic quaternion utilities for rotation composition.
    /// Performance-critical methods are aggressively inlined on supported frameworks.
    /// </summary>
    public static class QuaternionUtils
    {
        // Pre-computed constant for degrees to half-radians conversion
        private const float DegToHalfRad = MathConstants.DegToRad * 0.5f;

        // Threshold for linear interpolation fallback in Slerp
        private const float SlerpLinearThreshold = 0.9995f;

        // Minimum length for quaternion normalization
        private const float NormalizationEpsilon = 0.0001f;

        /// <summary>
        /// Creates a quaternion from yaw, pitch, roll angles using the standard head tracking order.
        /// Order: yaw (around Y/up) -> pitch (around X/right) -> roll (around Z/forward).
        /// This prevents gimbal lock and axis contamination.
        /// </summary>
        /// <param name="yaw">Yaw angle in degrees (horizontal turn).</param>
        /// <param name="pitch">Pitch angle in degrees (vertical tilt).</param>
        /// <param name="roll">Roll angle in degrees (head tilt).</param>
        /// <returns>Quaternion as Quat4.</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Quat4 FromYawPitchRoll(float yaw, float pitch, float roll)
        {
            // Convert to radians and half angles (optimized: single constant multiply)
            float yawRad = yaw * DegToHalfRad;
            float pitchRad = pitch * DegToHalfRad;
            float rollRad = roll * DegToHalfRad;

            // Pre-calculate sin/cos for each axis
            float cy = (float)System.Math.Cos(yawRad);
            float sy = (float)System.Math.Sin(yawRad);
            float cp = (float)System.Math.Cos(pitchRad);
            float sp = (float)System.Math.Sin(pitchRad);
            float cr = (float)System.Math.Cos(rollRad);
            float sr = (float)System.Math.Sin(rollRad);

            // YXZ rotation order (yaw * pitch * roll)
            float w = cy * cp * cr + sy * sp * sr;
            float x = cy * sp * cr + sy * cp * sr;
            float y = sy * cp * cr - cy * sp * sr;
            float z = cy * cp * sr - sy * sp * cr;

            return new Quat4(x, y, z, w);
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Creates a quaternion from yaw, pitch, roll angles (tuple version for modern runtimes).
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (float x, float y, float z, float w) FromYawPitchRollTuple(float yaw, float pitch, float roll)
        {
            var q = FromYawPitchRoll(yaw, pitch, roll);
            return (q.X, q.Y, q.Z, q.W);
        }
#endif

        /// <summary>
        /// Multiplies two quaternions: result = a * b.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Quat4 Multiply(Quat4 a, Quat4 b)
        {
            return new Quat4(
                a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
                a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
                a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
                a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z
            );
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Multiplies two quaternions (tuple version for modern runtimes).
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (float x, float y, float z, float w) MultiplyTuple(
            (float x, float y, float z, float w) a,
            (float x, float y, float z, float w) b)
        {
            var result = Multiply(
                new Quat4(a.x, a.y, a.z, a.w),
                new Quat4(b.x, b.y, b.z, b.w));
            return (result.X, result.Y, result.Z, result.W);
        }
#endif

        /// <summary>
        /// Returns the inverse (conjugate) of a unit quaternion.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Quat4 Inverse(Quat4 q)
        {
            return q.Inverse;
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Returns the inverse (conjugate) of a unit quaternion (tuple version).
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (float x, float y, float z, float w) InverseTuple((float x, float y, float z, float w) q)
        {
            return (-q.x, -q.y, -q.z, q.w);
        }
#endif

        /// <summary>
        /// Spherical linear interpolation between two quaternions.
        /// </summary>
        public static Quat4 Slerp(Quat4 a, Quat4 b, float t)
        {
            // Compute the cosine of the angle between the two vectors
            float dot = a.Dot(b);

            // If the dot product is negative, use a sign multiplier to take the shorter path
            // This avoids allocating a new Quat4 via b.Negated
            float sign = 1f;
            if (dot < 0f)
            {
                sign = -1f;
                dot = -dot;
            }

            // If the quaternions are very close, use linear interpolation
            if (dot > SlerpLinearThreshold)
            {
                return Normalize(new Quat4(
                    a.X + t * (sign * b.X - a.X),
                    a.Y + t * (sign * b.Y - a.Y),
                    a.Z + t * (sign * b.Z - a.Z),
                    a.W + t * (sign * b.W - a.W)
                ));
            }

            // Compute the spherical interpolation
            float theta = (float)System.Math.Acos(dot);
            float sinTheta = (float)System.Math.Sin(theta);
            float invSinTheta = 1f / sinTheta;
            float wa = (float)System.Math.Sin((1f - t) * theta) * invSinTheta;
            float wb = sign * (float)System.Math.Sin(t * theta) * invSinTheta;

            return new Quat4(
                wa * a.X + wb * b.X,
                wa * a.Y + wb * b.Y,
                wa * a.Z + wb * b.Z,
                wa * a.W + wb * b.W
            );
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Spherical linear interpolation (tuple version for modern runtimes).
        /// </summary>
        public static (float x, float y, float z, float w) SlerpTuple(
            (float x, float y, float z, float w) a,
            (float x, float y, float z, float w) b,
            float t)
        {
            var result = Slerp(
                new Quat4(a.x, a.y, a.z, a.w),
                new Quat4(b.x, b.y, b.z, b.w),
                t);
            return (result.X, result.Y, result.Z, result.W);
        }
#endif

        /// <summary>
        /// Normalizes a quaternion to unit length.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Quat4 Normalize(Quat4 q)
        {
            float lengthSq = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
            if (lengthSq < NormalizationEpsilon * NormalizationEpsilon)
            {
                return Quat4.Identity;
            }
            float inv = 1f / (float)System.Math.Sqrt(lengthSq);
            return new Quat4(q.X * inv, q.Y * inv, q.Z * inv, q.W * inv);
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Normalizes a quaternion (tuple version for modern runtimes).
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (float x, float y, float z, float w) NormalizeTuple((float x, float y, float z, float w) q)
        {
            var result = Normalize(new Quat4(q.x, q.y, q.z, q.w));
            return (result.X, result.Y, result.Z, result.W);
        }
#endif

        /// <summary>
        /// Decomposes a quaternion to yaw, pitch, roll angles in degrees using YXZ rotation order.
        /// Inverse of <see cref="FromYawPitchRoll"/>. At gimbal lock (pitch ±90°), roll is set to 0
        /// and the full rotation is attributed to yaw.
        /// </summary>
        /// <param name="q">Unit quaternion to decompose.</param>
        /// <param name="yaw">Output yaw in degrees.</param>
        /// <param name="pitch">Output pitch in degrees.</param>
        /// <param name="roll">Output roll in degrees.</param>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static void ToEulerYXZ(Quat4 q, out float yaw, out float pitch, out float roll)
        {
            // sinPitch = 2(wx - yz) for YXZ order
            float sinPitch = 2f * (q.W * q.X - q.Y * q.Z);

            // Clamp to [-1, 1] to handle numerical drift
            if (sinPitch >= 1f)
            {
                // Gimbal lock: pitch = +90°
                pitch = 90f;
                yaw = (float)(System.Math.Atan2(2f * (q.X * q.Z + q.W * q.Y), 1f - 2f * (q.X * q.X + q.Y * q.Y)) * MathConstants.RadToDeg);
                roll = 0f;
            }
            else if (sinPitch <= -1f)
            {
                // Gimbal lock: pitch = -90°
                pitch = -90f;
                yaw = (float)(System.Math.Atan2(2f * (q.X * q.Z + q.W * q.Y), 1f - 2f * (q.X * q.X + q.Y * q.Y)) * MathConstants.RadToDeg);
                roll = 0f;
            }
            else
            {
                pitch = (float)(System.Math.Asin(sinPitch) * MathConstants.RadToDeg);
                yaw = (float)(System.Math.Atan2(2f * (q.X * q.Z + q.W * q.Y), 1f - 2f * (q.X * q.X + q.Y * q.Y)) * MathConstants.RadToDeg);
                roll = (float)(System.Math.Atan2(2f * (q.X * q.Y + q.W * q.Z), 1f - 2f * (q.X * q.X + q.Z * q.Z)) * MathConstants.RadToDeg);
            }
        }

        /// <summary>
        /// Identity quaternion (no rotation).
        /// </summary>
        public static readonly Quat4 Identity = Quat4.Identity;
    }
}
