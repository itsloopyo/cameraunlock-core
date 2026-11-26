#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// An immutable quaternion struct for framework-agnostic rotation representation.
    /// Uses xyzw component order (matching Unity's Quaternion).
    /// Marked readonly for better compiler optimizations on supported frameworks.
    /// </summary>
#if !NET35 && !NET40
    public readonly struct Quat4
#else
    public struct Quat4
#endif
    {
        public readonly float X;
        public readonly float Y;
        public readonly float Z;
        public readonly float W;

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public Quat4(float x, float y, float z, float w)
        {
            X = x;
            Y = y;
            Z = z;
            W = w;
        }

        /// <summary>
        /// Identity quaternion (no rotation).
        /// </summary>
        public static Quat4 Identity => new Quat4(0f, 0f, 0f, 1f);

        /// <summary>
        /// Returns the negated quaternion (represents the same rotation).
        /// </summary>
        public Quat4 Negated
        {
#if !NET35 && !NET40
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
            get => new Quat4(-X, -Y, -Z, -W);
        }

        /// <summary>
        /// Returns the conjugate/inverse of a unit quaternion.
        /// </summary>
        public Quat4 Inverse
        {
#if !NET35 && !NET40
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
            get => new Quat4(-X, -Y, -Z, W);
        }

        /// <summary>
        /// Computes the dot product with another quaternion.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public float Dot(Quat4 other)
        {
            return X * other.X + Y * other.Y + Z * other.Z + W * other.W;
        }

        /// <summary>
        /// Rotates a vector by this quaternion.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public Vec3 Rotate(Vec3 v)
        {
            // q * v * q^-1 optimized
            float x2 = X + X;
            float y2 = Y + Y;
            float z2 = Z + Z;

            float xx2 = X * x2;
            float yy2 = Y * y2;
            float zz2 = Z * z2;
            float xy2 = X * y2;
            float xz2 = X * z2;
            float yz2 = Y * z2;
            float wx2 = W * x2;
            float wy2 = W * y2;
            float wz2 = W * z2;

            return new Vec3(
                (1f - yy2 - zz2) * v.X + (xy2 - wz2) * v.Y + (xz2 + wy2) * v.Z,
                (xy2 + wz2) * v.X + (1f - xx2 - zz2) * v.Y + (yz2 - wx2) * v.Z,
                (xz2 - wy2) * v.X + (yz2 + wx2) * v.Y + (1f - xx2 - yy2) * v.Z
            );
        }

        /// <summary>
        /// Multiplies two quaternions: this * other.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public Quat4 Multiply(Quat4 b)
        {
            return new Quat4(
                W * b.X + X * b.W + Y * b.Z - Z * b.Y,
                W * b.Y - X * b.Z + Y * b.W + Z * b.X,
                W * b.Z + X * b.Y - Y * b.X + Z * b.W,
                W * b.W - X * b.X - Y * b.Y - Z * b.Z
            );
        }

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static Quat4 operator *(Quat4 a, Quat4 b) => a.Multiply(b);
    }
}
