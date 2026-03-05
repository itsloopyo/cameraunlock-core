using System;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// An immutable 3D vector struct for framework-agnostic position/direction representation.
    /// Marked readonly for better compiler optimizations on supported frameworks.
    /// </summary>
#if !NET35 && !NET40
    public readonly struct Vec3
#else
    public struct Vec3
#endif
    {
        public readonly float X;
        public readonly float Y;
        public readonly float Z;

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public Vec3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public static Vec3 Zero => new Vec3(0f, 0f, 0f);
        public static Vec3 Forward => new Vec3(0f, 0f, 1f);
        public static Vec3 Up => new Vec3(0f, 1f, 0f);
        public static Vec3 Right => new Vec3(1f, 0f, 0f);

        public float SqrMagnitude
        {
#if !NET35 && !NET40
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
            get => X * X + Y * Y + Z * Z;
        }

        public float Magnitude
        {
#if !NET35 && !NET40
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
            get => (float)System.Math.Sqrt(X * X + Y * Y + Z * Z);
        }

        public Vec3 Normalized
        {
#if !NET35 && !NET40
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
            get
            {
                float sqrMag = X * X + Y * Y + Z * Z;
                if (sqrMag < 0.00000001f) return Zero;
                float inv = 1f / (float)System.Math.Sqrt(sqrMag);
                return new Vec3(X * inv, Y * inv, Z * inv);
            }
        }

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 operator +(Vec3 a, Vec3 b) => new Vec3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 operator -(Vec3 a, Vec3 b) => new Vec3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 operator *(Vec3 v, float s) => new Vec3(v.X * s, v.Y * s, v.Z * s);

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 operator *(float s, Vec3 v) => new Vec3(v.X * s, v.Y * s, v.Z * s);

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 operator -(Vec3 v) => new Vec3(-v.X, -v.Y, -v.Z);

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float Dot(Vec3 a, Vec3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 Lerp(Vec3 a, Vec3 b, float t) => new Vec3(
            a.X + (b.X - a.X) * t,
            a.Y + (b.Y - a.Y) * t,
            a.Z + (b.Z - a.Z) * t
        );

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Vec3 Cross(Vec3 a, Vec3 b) => new Vec3(
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X
        );

        public override string ToString() => string.Format("({0:F3}, {1:F3}, {2:F3})", X, Y, Z);
    }
}
