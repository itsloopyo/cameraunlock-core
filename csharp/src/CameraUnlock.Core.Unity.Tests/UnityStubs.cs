// Minimal managed stand-ins for the UnityEngine types CameraUnlock.Core.Unity touches.
//
// The real UnityEngine assemblies cannot be used here: almost every member on
// Quaternion, Matrix4x4, Time and Camera is an extern call into the native player,
// so referencing the shipped DLLs from a test host compiles and then throws at the
// first call. That is why the Unity half of this library had no executable coverage
// at all, and why a silent connection-flag propagation bug was able to hide in
// ViewMatrixTrackingController.
//
// These stubs implement the maths for real (the quaternion and matrix operations are
// the standard formulations, matching Unity's left-handed Y-up conventions) so the
// tests exercise genuine behaviour rather than asserting against a null object. What
// is under test is the CONTROL FLOW in the classes above them - is the connection flag
// pushed to both processors, is smoothing state retained, is the effective value the
// one that gets used - not Unity's own arithmetic.
//
// This file is compiled ONLY into the test assembly. It never ships.

using System;

// The stubs below are process-wide mutable state (Time, Screen, Camera.main,
// Camera.onPreCull, and CameraCallbackLifecycle's own statics on top of them). Running test
// classes in parallel would have them stomping each other.
[assembly: Xunit.CollectionBehavior(DisableTestParallelization = true)]

namespace UnityEngine
{
    /// Base for the Unity object model, and the reason it exists here: Unity overloads == so a
    /// DESTROYED object compares equal to null while the managed reference is still alive.
    /// Modelling that faithfully is what makes the destroyed-object defect class - a UI target
    /// destroyed by a scene change, a render callback whose owning MonoBehaviour went away -
    /// reachable from a test at all. With plain ReferenceEquals it is structurally invisible.
    public class Object
    {
        private bool _destroyed;

        /// Stands in for Unity destroying the native half of the object.
        public void MarkDestroyed()
        {
            _destroyed = true;
        }

        public static bool operator ==(Object a, Object b)
        {
            bool aNull = ReferenceEquals(a, null) || a._destroyed;
            bool bNull = ReferenceEquals(b, null) || b._destroyed;
            if (aNull || bNull) return aNull && bNull;
            return ReferenceEquals(a, b);
        }

        public static bool operator !=(Object a, Object b) => !(a == b);

        public override bool Equals(object obj) => ReferenceEquals(this, obj);

        public override int GetHashCode() =>
            System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);
    }

    public struct Vector2
    {
        public float x;
        public float y;

        public Vector2(float x, float y)
        {
            this.x = x;
            this.y = y;
        }

        public static Vector2 zero => new Vector2(0f, 0f);

        public static Vector2 Lerp(Vector2 a, Vector2 b, float t)
        {
            if (t < 0f) t = 0f;
            if (t > 1f) t = 1f;
            return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        }
    }

    public struct Vector3
    {
        public float x;
        public float y;
        public float z;

        public Vector3(float x, float y, float z)
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public static Vector3 zero => new Vector3(0f, 0f, 0f);
        public static Vector3 one => new Vector3(1f, 1f, 1f);
        public static Vector3 up => new Vector3(0f, 1f, 0f);
        public static Vector3 forward => new Vector3(0f, 0f, 1f);
        public static Vector3 back => new Vector3(0f, 0f, -1f);

        public float magnitude => (float)System.Math.Sqrt(x * x + y * y + z * z);

        public Vector3 normalized
        {
            get
            {
                float m = magnitude;
                return m > 1e-9f ? new Vector3(x / m, y / m, z / m) : zero;
            }
        }

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.x, -a.y, -a.z);
        public static Vector3 operator *(Vector3 a, float s) => new Vector3(a.x * s, a.y * s, a.z * s);
        public static Vector3 operator *(float s, Vector3 a) => a * s;

        public static Vector3 operator /(Vector3 a, float s) => new Vector3(a.x / s, a.y / s, a.z / s);

        public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;

        public static Vector3 Cross(Vector3 a, Vector3 b)
        {
            return new Vector3(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
        }

        public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
        {
            if (t < 0f) t = 0f;
            if (t > 1f) t = 1f;
            return new Vector3(
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t);
        }

        public override string ToString() => $"({x}, {y}, {z})";
    }

    public struct Quaternion
    {
        public float x;
        public float y;
        public float z;
        public float w;

        public Quaternion(float x, float y, float z, float w)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.w = w;
        }

        public static Quaternion identity => new Quaternion(0f, 0f, 0f, 1f);

        public static Quaternion operator *(Quaternion a, Quaternion b)
        {
            return new Quaternion(
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
        }

        public static Vector3 operator *(Quaternion q, Vector3 v)
        {
            // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
            var u = new Vector3(q.x, q.y, q.z);
            Vector3 t = Vector3.Cross(u, v) + v * q.w;
            return v + Vector3.Cross(u, t) * 2f;
        }

        public static Quaternion Inverse(Quaternion q)
        {
            float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            if (n < 1e-12f) return identity;
            return new Quaternion(-q.x / n, -q.y / n, -q.z / n, q.w / n);
        }

        /// Unity's Euler order: Z, then X, then Y (applied as Y * X * Z).
        public static Quaternion Euler(float xDeg, float yDeg, float zDeg)
        {
            float hx = xDeg * Mathf.Deg2Rad * 0.5f;
            float hy = yDeg * Mathf.Deg2Rad * 0.5f;
            float hz = zDeg * Mathf.Deg2Rad * 0.5f;

            var qx = new Quaternion((float)System.Math.Sin(hx), 0f, 0f, (float)System.Math.Cos(hx));
            var qy = new Quaternion(0f, (float)System.Math.Sin(hy), 0f, (float)System.Math.Cos(hy));
            var qz = new Quaternion(0f, 0f, (float)System.Math.Sin(hz), (float)System.Math.Cos(hz));

            return qy * qx * qz;
        }

        public static Quaternion Euler(Vector3 euler) => Euler(euler.x, euler.y, euler.z);

        public static Quaternion AngleAxis(float angleDeg, Vector3 axis)
        {
            Vector3 n = axis.normalized;
            float half = angleDeg * Mathf.Deg2Rad * 0.5f;
            float s = (float)System.Math.Sin(half);
            return new Quaternion(n.x * s, n.y * s, n.z * s, (float)System.Math.Cos(half));
        }

        public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.up);

        public static Quaternion LookRotation(Vector3 forward, Vector3 up)
        {
            Vector3 f = forward.normalized;
            Vector3 r = Vector3.Cross(up, f).normalized;
            Vector3 u = Vector3.Cross(f, r);

            float trace = r.x + u.y + f.z;
            if (trace > 0f)
            {
                float s = (float)System.Math.Sqrt(trace + 1f) * 2f;
                return new Quaternion((u.z - f.y) / s, (f.x - r.z) / s, (r.y - u.x) / s, 0.25f * s);
            }
            if (r.x > u.y && r.x > f.z)
            {
                float s = (float)System.Math.Sqrt(1f + r.x - u.y - f.z) * 2f;
                return new Quaternion(0.25f * s, (u.x + r.y) / s, (f.x + r.z) / s, (u.z - f.y) / s);
            }
            if (u.y > f.z)
            {
                float s = (float)System.Math.Sqrt(1f + u.y - r.x - f.z) * 2f;
                return new Quaternion((u.x + r.y) / s, 0.25f * s, (f.y + u.z) / s, (f.x - r.z) / s);
            }
            {
                float s = (float)System.Math.Sqrt(1f + f.z - r.x - u.y) * 2f;
                return new Quaternion((f.x + r.z) / s, (f.y + u.z) / s, 0.25f * s, (r.y - u.x) / s);
            }
        }

        public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
        {
            if (t < 0f) t = 0f;
            if (t > 1f) t = 1f;

            float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (dot < 0f)
            {
                b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
                dot = -dot;
            }

            if (dot > 0.9995f)
            {
                var lerped = new Quaternion(
                    a.x + (b.x - a.x) * t,
                    a.y + (b.y - a.y) * t,
                    a.z + (b.z - a.z) * t,
                    a.w + (b.w - a.w) * t);
                return Normalize(lerped);
            }

            float theta0 = (float)System.Math.Acos(dot);
            float theta = theta0 * t;
            float sinTheta = (float)System.Math.Sin(theta);
            float sinTheta0 = (float)System.Math.Sin(theta0);

            float s0 = (float)System.Math.Cos(theta) - dot * sinTheta / sinTheta0;
            float s1 = sinTheta / sinTheta0;

            return new Quaternion(
                a.x * s0 + b.x * s1,
                a.y * s0 + b.y * s1,
                a.z * s0 + b.z * s1,
                a.w * s0 + b.w * s1);
        }

        private static Quaternion Normalize(Quaternion q)
        {
            float m = (float)System.Math.Sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            if (m < 1e-9f) return identity;
            return new Quaternion(q.x / m, q.y / m, q.z / m, q.w / m);
        }

        /// Angle in degrees between two rotations. Test-side convenience, mirrors
        /// UnityEngine.Quaternion.Angle.
        ///
        /// Accumulated in double because acos is brutally ill-conditioned near 1: with
        /// float components the dot product of a unit quaternion with itself lands around
        /// 1 +/- 1e-7, and acos turns that into ~0.03 degrees of apparent rotation. Tests
        /// comparing two rotations for equality therefore use a 0.1 degree epsilon, not a
        /// tighter one - that is the float noise floor, not slop.
        public static float Angle(Quaternion a, Quaternion b)
        {
            double dot = (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z + (double)a.w * b.w;
            dot = System.Math.Abs(dot);
            if (dot > 1.0) dot = 1.0;
            return (float)(System.Math.Acos(dot) * 2.0 * (180.0 / System.Math.PI));
        }

        public override string ToString() => $"({x}, {y}, {z}, {w})";
    }

    public struct Matrix4x4
    {
        public float m00, m01, m02, m03;
        public float m10, m11, m12, m13;
        public float m20, m21, m22, m23;
        public float m30, m31, m32, m33;

        public static Matrix4x4 identity
        {
            get
            {
                var m = new Matrix4x4();
                m.m00 = m.m11 = m.m22 = m.m33 = 1f;
                return m;
            }
        }

        public static Matrix4x4 Translate(Vector3 t)
        {
            Matrix4x4 m = identity;
            m.m03 = t.x;
            m.m13 = t.y;
            m.m23 = t.z;
            return m;
        }

        public static Matrix4x4 Rotate(Quaternion q)
        {
            Matrix4x4 m = identity;
            float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
            float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
            float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

            m.m00 = 1f - 2f * (yy + zz);
            m.m01 = 2f * (xy - wz);
            m.m02 = 2f * (xz + wy);

            m.m10 = 2f * (xy + wz);
            m.m11 = 1f - 2f * (xx + zz);
            m.m12 = 2f * (yz - wx);

            m.m20 = 2f * (xz - wy);
            m.m21 = 2f * (yz + wx);
            m.m22 = 1f - 2f * (xx + yy);
            return m;
        }

        public static Matrix4x4 Scale(Vector3 s)
        {
            Matrix4x4 m = identity;
            m.m00 = s.x;
            m.m11 = s.y;
            m.m22 = s.z;
            return m;
        }

        public static Matrix4x4 TRS(Vector3 pos, Quaternion q, Vector3 s)
        {
            return Translate(pos) * Rotate(q) * Scale(s);
        }

        public Quaternion rotation
        {
            get
            {
                var forward = new Vector3(m02, m12, m22);
                var up = new Vector3(m01, m11, m21);
                if (forward.magnitude < 1e-9f) return Quaternion.identity;
                return Quaternion.LookRotation(forward, up);
            }
        }

        public Matrix4x4 inverse
        {
            get
            {
                float[] m = {
                    m00, m10, m20, m30,
                    m01, m11, m21, m31,
                    m02, m12, m22, m32,
                    m03, m13, m23, m33,
                };
                var inv = new float[16];

                inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                         m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
                inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                         m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
                inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                         m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
                inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                          m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
                inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
                         m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
                inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
                         m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
                inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
                         m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
                inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
                          m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
                inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
                         m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
                inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
                         m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
                inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                          m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
                inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
                          m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
                inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
                         m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
                inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
                         m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
                inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                          m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
                inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                          m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

                float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
                if (System.Math.Abs(det) < 1e-12f) return identity;
                float invDet = 1f / det;

                var r = new Matrix4x4();
                r.m00 = inv[0] * invDet;  r.m10 = inv[1] * invDet;  r.m20 = inv[2] * invDet;  r.m30 = inv[3] * invDet;
                r.m01 = inv[4] * invDet;  r.m11 = inv[5] * invDet;  r.m21 = inv[6] * invDet;  r.m31 = inv[7] * invDet;
                r.m02 = inv[8] * invDet;  r.m12 = inv[9] * invDet;  r.m22 = inv[10] * invDet; r.m32 = inv[11] * invDet;
                r.m03 = inv[12] * invDet; r.m13 = inv[13] * invDet; r.m23 = inv[14] * invDet; r.m33 = inv[15] * invDet;
                return r;
            }
        }

        public static Matrix4x4 operator *(Matrix4x4 a, Matrix4x4 b)
        {
            var r = new Matrix4x4();
            r.m00 = a.m00 * b.m00 + a.m01 * b.m10 + a.m02 * b.m20 + a.m03 * b.m30;
            r.m01 = a.m00 * b.m01 + a.m01 * b.m11 + a.m02 * b.m21 + a.m03 * b.m31;
            r.m02 = a.m00 * b.m02 + a.m01 * b.m12 + a.m02 * b.m22 + a.m03 * b.m32;
            r.m03 = a.m00 * b.m03 + a.m01 * b.m13 + a.m02 * b.m23 + a.m03 * b.m33;

            r.m10 = a.m10 * b.m00 + a.m11 * b.m10 + a.m12 * b.m20 + a.m13 * b.m30;
            r.m11 = a.m10 * b.m01 + a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31;
            r.m12 = a.m10 * b.m02 + a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32;
            r.m13 = a.m10 * b.m03 + a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33;

            r.m20 = a.m20 * b.m00 + a.m21 * b.m10 + a.m22 * b.m20 + a.m23 * b.m30;
            r.m21 = a.m20 * b.m01 + a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31;
            r.m22 = a.m20 * b.m02 + a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32;
            r.m23 = a.m20 * b.m03 + a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33;

            r.m30 = a.m30 * b.m00 + a.m31 * b.m10 + a.m32 * b.m20 + a.m33 * b.m30;
            r.m31 = a.m30 * b.m01 + a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31;
            r.m32 = a.m30 * b.m02 + a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32;
            r.m33 = a.m30 * b.m03 + a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33;
            return r;
        }
    }

    public static class Mathf
    {
        public const float Deg2Rad = 0.0174532924f;
        public const float Rad2Deg = 57.29578f;

        public static float Lerp(float a, float b, float t)
        {
            if (t < 0f) t = 0f;
            if (t > 1f) t = 1f;
            return a + (b - a) * t;
        }

        public static float Tan(float radians) => (float)System.Math.Tan(radians);
        public static float Abs(float v) => System.Math.Abs(v);
    }

    /// Test-controlled clock. Unity drives these from the player loop; here the test
    /// sets them directly so a frame is an explicit, deterministic step.
    public static class Time
    {
        public static float deltaTime = 1f / 60f;

        // Separate from deltaTime so a test can model a paused game (timeScale 0), where
        // Unity zeroes deltaTime but keeps unscaledDeltaTime advancing. Anything that
        // must keep running while paused - the view-matrix transitions, hotkey cooldowns -
        // is only testable against that split.
        public static float unscaledDeltaTime = 1f / 60f;
        public static int frameCount = 1;

        public static void AdvanceFrame()
        {
            frameCount++;
        }

        public static void Reset()
        {
            deltaTime = 1f / 60f;
            unscaledDeltaTime = 1f / 60f;
            frameCount = 1;
        }
    }

    public static class Screen
    {
        public static int width = 1920;
        public static int height = 1080;
    }

    public class Transform
    {
        public Quaternion rotation = Quaternion.identity;
        public Vector3 position = Vector3.zero;
        public Quaternion localRotation = Quaternion.identity;
        public Vector3 localPosition = Vector3.zero;
        public string name = string.Empty;
        public Transform parent;

        public Matrix4x4 localToWorldMatrix => Matrix4x4.TRS(position, rotation, Vector3.one);
        public Matrix4x4 worldToLocalMatrix => localToWorldMatrix.inverse;
    }

    public class Canvas : Object
    {
        public static Action willRenderCanvases;
    }

    public enum CameraType
    {
        Game = 1,
        SceneView = 2,
        Preview = 4,
        VR = 8,
        Reflection = 16,
    }

    public class Camera : Object
    {
        public delegate void CameraCallback(Camera cam);

        public static CameraCallback onPreCull;
        public static CameraCallback onPreRender;
        public static CameraCallback onPostRender;

        public static Camera main;

        /// Assigned directly by tests; Unity populates it from the live scene.
        public static Camera[] allCameras = new Camera[0];

        public Transform transform = new Transform();
        public string name = string.Empty;
        public float fieldOfView = 60f;
        public float aspect = 16f / 9f;
        public float depth;
        public bool enabled = true;
        public CameraType cameraType = CameraType.Game;
        public object targetTexture;

        public Matrix4x4 worldToCameraMatrix = Matrix4x4.identity;

        /// Counts calls so a test can assert the sticky-override reset actually fired.
        public int ResetWorldToCameraMatrixCalls { get; private set; }

        /// Rebuilds the view matrix from the transform exactly as Unity does: the inverse
        /// of the camera's world transform with the z row negated, because Unity's camera
        /// space is right-handed and looks down -z. A plain identity here would make view
        /// space equal world space and quietly hide every handedness bug the offset paths
        /// can have.
        public void ResetWorldToCameraMatrix()
        {
            ResetWorldToCameraMatrixCalls++;

            Matrix4x4 view = Matrix4x4.Rotate(Quaternion.Inverse(transform.rotation))
                           * Matrix4x4.Translate(new Vector3(
                               -transform.position.x, -transform.position.y, -transform.position.z));
            view.m20 = -view.m20;
            view.m21 = -view.m21;
            view.m22 = -view.m22;
            view.m23 = -view.m23;
            worldToCameraMatrix = view;
        }
    }
}
