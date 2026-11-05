using System;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// A head pose as the Accela filter takes and gives it: yaw, pitch and roll in degrees, in
    /// the OpenTrack packet's convention, and the position in the centimetres the OpenTrack packet carries, which its deadzone and gains are tuned for.
    /// </summary>
    public struct AccelaPose
    {
        public double Yaw;
        public double Pitch;
        public double Roll;
        public double X;
        public double Y;
        public double Z;

        public AccelaPose(double yaw, double pitch, double roll, double x, double y, double z)
        {
            Yaw = yaw;
            Pitch = pitch;
            Roll = roll;
            X = x;
            Y = y;
            Z = z;
        }
    }

    /// <summary>
    /// The Accela filter, ported from Hypari's webcam tracker (hypari-webcam smoothing.rs),
    /// which ports headcam-android's, itself a port of opentrack's filter-accela by Stanislaw
    /// Halik (ISC licence). Rotation is filtered as one quaternion step toward the input, the
    /// position per axis, each with its gain curve; one amount from 0 (lightest) to 1
    /// (heaviest) sets every parameter. Its output on data/accela-cases.json matches the Rust
    /// filter's and the C++ port's (cameraunlock::AccelaFilter), step for step.
    ///
    /// A large step raises the gain, so it is eased less than a small one. There is no jump
    /// handling: whatever feeds the filter has already judged the step.
    /// </summary>
    public sealed class AccelaFilter
    {
        /// <summary>The amount Hypari and headcam-android ship.</summary>
        public const double DefaultAmount = 0.75;
        /// <summary>A long gap is integrated as at most this much time.</summary>
        public const double MaxDt = 0.066;
        /// <summary>A dropped frame is integrated in steps; ordinary 30 Hz jitter is not split.</summary>
        public const double Step = 0.035;

        private const double MinSmoothing = 0.001;
        private const float Epsilon = 1e-7f;
        // Rust's to_degrees and to_radians multiply by these, rounded once.
        private const double DegPerRad = 180.0 / System.Math.PI;
        private const double RadPerDeg = System.Math.PI / 180.0;

        private static readonly double[,] RotationGain =
        {
            { 0.0, 0.0 }, { 0.2, 0.05 }, { 0.4, 0.15 }, { 0.7, 0.4 }, { 1.0, 0.9 }, { 1.5, 2.0 },
            { 2.2, 4.5 }, { 3.0, 8.0 }, { 4.0, 12.0 }, { 5.0, 16.0 }, { 6.5, 21.0 }, { 8.0, 25.0 },
        };

        private static readonly double[,] PositionGain =
        {
            { 0.0, 0.0 }, { 0.2, 0.05 }, { 0.5, 0.2 }, { 0.8, 0.5 }, { 1.2, 1.2 }, { 1.8, 3.0 },
            { 2.6, 6.0 }, { 3.5, 10.0 }, { 4.5, 14.0 }, { 6.0, 19.0 }, { 7.5, 24.0 }, { 9.5, 30.0 },
        };

        private double _amount;
        private double _rotationDeadzone;
        private double _positionDeadzone;
        private double _rotationSmoothing;
        private double _positionSmoothing;
        private readonly Axis[] _position = new Axis[3];
        private Rotation _rotation = new Rotation();

        public AccelaFilter() : this(DefaultAmount)
        {
        }

        public AccelaFilter(double amount)
        {
            SetAmount(amount);
            Reset();
        }

        /// <summary>The amount, 0 to 1.</summary>
        public double Amount
        {
            get { return _amount; }
        }

        /// <summary>Keeps the filter's state; clamped to 0..1.</summary>
        public void SetAmount(double amount)
        {
            double t = amount < 0.0 ? 0.0 : amount > 1.0 ? 1.0 : amount;
            _amount = t;
            // A higher divisor is what buys heaviness; the deadzone is a jitter gate and
            // barely moves, since a large one freezes the output until the error clears it.
            _rotationDeadzone = Ridge(t, 0.01, 0.03, 0.06, 0.5);
            _positionDeadzone = Ridge(t, 0.01, 0.02, 0.05, 0.5);
            _rotationSmoothing = Ridge(t, 0.3, 1.4, 2.4, 0.5);
            _positionSmoothing = Ridge(t, 0.3, 1.6, 2.6, 0.5);
        }

        /// <summary>The next Filter() passes its input through and starts from it.</summary>
        public void Reset()
        {
            _position[0] = new Axis(0.0);
            _position[1] = new Axis(0.0);
            // The gain floor makes a small depth step settle within a second at 15 to 60 Hz
            // (headcam-android's DepthPositionGainTest).
            _position[2] = new Axis(4.0);
            _rotation = new Rotation();
        }

        /// <summary>
        /// Filters one sample. <paramref name="timeSeconds"/> is the sample's time on any clock
        /// that only goes forward and is above zero. A time not after the last one returns the
        /// last output.
        /// </summary>
        public AccelaPose Filter(AccelaPose input, double timeSeconds)
        {
            Quat q = _rotation.Filter(FromEulerYxz(input.Yaw, input.Pitch, input.Roll), timeSeconds,
                _rotationDeadzone, _rotationSmoothing);
            AccelaPose output = new AccelaPose();
            ToEulerYxz(q, out output.Yaw, out output.Pitch, out output.Roll);
            output.X = _position[0].Filter(input.X, timeSeconds, _positionDeadzone, _positionSmoothing);
            output.Y = _position[1].Filter(input.Y, timeSeconds, _positionDeadzone, _positionSmoothing);
            output.Z = _position[2].Filter(input.Z, timeSeconds, _positionDeadzone, _positionSmoothing);
            return output;
        }

        // Single-precision trigonometry computed in double and rounded once, as the Rust filter
        // (hypari-webcam fmath.rs) does.
        private static float Sin(float x) { return (float)System.Math.Sin(x); }
        private static float Cos(float x) { return (float)System.Math.Cos(x); }
        private static float Acos(float x) { return (float)System.Math.Acos(x); }
        private static float Atan2(float y, float x) { return (float)System.Math.Atan2(y, x); }
        private static float Sqrt(float x) { return (float)System.Math.Sqrt(x); }

        private static double CopySign(double magnitude, double sign)
        {
            double m = System.Math.Abs(magnitude);
            return sign < 0.0 || (sign == 0.0 && double.IsNegativeInfinity(1.0 / sign)) ? -m : m;
        }

        private static double Ridge(double t, double lo, double mid, double hi, double pivot)
        {
            if (t <= pivot)
            {
                return System.Math.Exp(System.Math.Log(lo) + (System.Math.Log(mid) - System.Math.Log(lo)) * (t / pivot));
            }
            return System.Math.Exp(System.Math.Log(mid) + (System.Math.Log(hi) - System.Math.Log(mid)) * ((t - pivot) / (1.0 - pivot)));
        }

        private static double Curve(double[,] points, double input)
        {
            double x = System.Math.Abs(input);
            int last = points.GetLength(0) - 1;
            if (x < points[0, 0]) return points[0, 1];
            if (x > points[last, 0]) return points[last, 1];
            for (int i = 0; i < last; i++)
            {
                if (x >= points[i, 0] && x <= points[i + 1, 0])
                {
                    return points[i, 1] + (x - points[i, 0]) / (points[i + 1, 0] - points[i, 0]) * (points[i + 1, 1] - points[i, 1]);
                }
            }
            return points[last, 1];
        }

        private struct Quat
        {
            public float W, X, Y, Z;

            public Quat(float w, float x, float y, float z)
            {
                W = w;
                X = x;
                Y = y;
                Z = z;
            }

            public static readonly Quat Identity = new Quat(1.0f, 0.0f, 0.0f, 0.0f);

            public float Dot(Quat o) { return W * o.W + X * o.X + Y * o.Y + Z * o.Z; }

            public Quat Neg() { return new Quat(-W, -X, -Y, -Z); }

            public Quat Mul(Quat o)
            {
                return new Quat(
                    W * o.W - X * o.X - Y * o.Y - Z * o.Z,
                    W * o.X + X * o.W + Y * o.Z - Z * o.Y,
                    W * o.Y - X * o.Z + Y * o.W + Z * o.X,
                    W * o.Z + X * o.Y - Y * o.X + Z * o.W);
            }

            public Quat Normalize()
            {
                float len = Sqrt(Dot(this));
                if (len < Epsilon) return Identity;
                return new Quat(W / len, X / len, Y / len, Z / len);
            }
        }

        private static Quat Slerp(Quat q0, Quat q1, float t)
        {
            float dot = q0.Dot(q1);
            Quat target = q1;
            if (dot < 0.0f)
            {
                dot = -dot;
                target = q1.Neg();
            }
            if (dot > 0.9995f)
            {
                return new Quat(
                    q0.W + t * (target.W - q0.W),
                    q0.X + t * (target.X - q0.X),
                    q0.Y + t * (target.Y - q0.Y),
                    q0.Z + t * (target.Z - q0.Z)).Normalize();
            }
            float theta0 = Acos(dot < -1.0f ? -1.0f : dot > 1.0f ? 1.0f : dot);
            float theta = theta0 * t;
            float sinTheta = Sin(theta);
            float sinTheta0 = Sin(theta0);
            float s0 = Cos(theta) - dot * sinTheta / sinTheta0;
            float s1 = sinTheta / sinTheta0;
            return new Quat(
                s0 * q0.W + s1 * target.W,
                s0 * q0.X + s1 * target.X,
                s0 * q0.Y + s1 * target.Y,
                s0 * q0.Z + s1 * target.Z).Normalize();
        }

        private struct Axis
        {
            private readonly double _minimumGain;
            private double _lastOutput;
            private double _lastInput;
            private double _lastTime;

            public Axis(double minimumGain)
            {
                _minimumGain = minimumGain;
                _lastOutput = 0.0;
                _lastInput = 0.0;
                _lastTime = 0.0;
            }

            public double Filter(double value, double time, double deadzone, double smoothing)
            {
                if (_lastTime == 0.0)
                {
                    _lastOutput = value;
                    _lastInput = value;
                    _lastTime = time;
                    return value;
                }
                double rawDt = time - _lastTime;
                if (rawDt <= 0.0) return _lastOutput;
                double dt = System.Math.Min(rawDt, MaxDt);
                uint steps = (uint)System.Math.Ceiling(dt / Step);
                double stepDt = dt / steps;
                double inputDelta = value - _lastInput;
                for (uint step = 1; step <= steps; step++)
                {
                    double sampled = steps == 1 ? value : _lastInput + inputDelta * step / steps;
                    double full = sampled - _lastOutput;
                    if (System.Math.Abs(full) < deadzone) continue;
                    double delta = full - CopySign(deadzone, full);
                    double normalized = delta / System.Math.Max(smoothing, MinSmoothing);
                    double gain = System.Math.Max(_minimumGain, Curve(PositionGain, System.Math.Abs(normalized)));
                    double filtered = CopySign(System.Math.Abs(normalized), delta) * gain;
                    double rawStep = filtered * smoothing * stepDt;
                    // Never step past the input.
                    _lastOutput += CopySign(System.Math.Min(System.Math.Abs(rawStep), System.Math.Abs(full)), full);
                }
                _lastInput = value;
                _lastTime = time;
                return _lastOutput;
            }
        }

        private sealed class Rotation
        {
            private Quat _lastOutput = Quat.Identity;
            private Quat _lastInput = Quat.Identity;
            private double _lastTime;

            public Quat Filter(Quat q, double time, double deadzone, double smoothing)
            {
                if (_lastTime == 0.0)
                {
                    _lastOutput = q;
                    _lastInput = q;
                    _lastTime = time;
                    return q;
                }
                double rawDt = time - _lastTime;
                if (rawDt <= 0.0) return _lastOutput;
                double dt = System.Math.Min(rawDt, MaxDt);
                uint steps = (uint)System.Math.Ceiling(dt / Step);
                double stepDt = dt / steps;
                for (uint step = 1; step <= steps; step++)
                {
                    Quat sampled = steps == 1 ? q : Slerp(_lastInput, q, (float)step / (float)steps);
                    Quat input = sampled.Dot(_lastOutput) < 0.0f ? sampled.Neg() : sampled;
                    Quat inverse = new Quat(_lastOutput.W, -_lastOutput.X, -_lastOutput.Y, -_lastOutput.Z);
                    Quat deltaQ = input.Mul(inverse);
                    // The vector part keeps tiny rotations after the scalar has rounded to one.
                    float sinHalf = Sqrt(deltaQ.X * deltaQ.X + deltaQ.Y * deltaQ.Y + deltaQ.Z * deltaQ.Z);
                    double deltaAngle = (double)(2.0f * Atan2(sinHalf, System.Math.Abs(deltaQ.W))) * DegPerRad;
                    float ax = 0.0f, ay = 1.0f, az = 0.0f;
                    float sign = deltaQ.W < 0.0f ? -1.0f : 1.0f;
                    float qx = deltaQ.X * sign, qy = deltaQ.Y * sign, qz = deltaQ.Z * sign;
                    float s = Sqrt(qx * qx + qy * qy + qz * qz);
                    if (s >= Epsilon)
                    {
                        ax = qx / s;
                        ay = qy / s;
                        az = qz / s;
                    }
                    if (System.Math.Abs(deltaAngle) < deadzone) continue;
                    double dz = deltaAngle - CopySign(deadzone, deltaAngle);
                    double normalized = dz / System.Math.Max(smoothing, MinSmoothing);
                    double gain = Curve(RotationGain, System.Math.Abs(normalized));
                    double filtered = CopySign(System.Math.Abs(normalized), dz) * gain;
                    double rawAngle = filtered * smoothing * stepDt;
                    float angle = (float)(System.Math.Min(System.Math.Abs(rawAngle), System.Math.Abs(deltaAngle)) * RadPerDeg);
                    // deltaQ is in the world frame, so the partial step pre-multiplies.
                    Quat stepQ = Quat.Identity;
                    float half = angle / 2.0f;
                    float sh = Sin(half);
                    float len = Sqrt(ax * ax + ay * ay + az * az);
                    if (len >= Epsilon) stepQ = new Quat(Cos(half), ax / len * sh, ay / len * sh, az / len * sh);
                    _lastOutput = stepQ.Mul(_lastOutput).Normalize();
                }
                _lastInput = q;
                _lastTime = time;
                return _lastOutput;
            }
        }

        // Intrinsic Y-X-Z (yaw, pitch, roll) in degrees, the OpenTrack convention:
        // M = Ry(yaw) * Rx(pitch) * Rz(-roll).
        private static Quat FromEulerYxz(double yaw, double pitch, double roll)
        {
            float hy = (float)(yaw * RadPerDeg / 2.0);
            float hx = (float)(pitch * RadPerDeg / 2.0);
            float hz = (float)(-(roll * RadPerDeg) / 2.0);
            float cy = Cos(hy), sy = Sin(hy);
            float cx = Cos(hx), sx = Sin(hx);
            float cz = Cos(hz), sz = Sin(hz);
            return new Quat(
                cy * cx * cz + sy * sx * sz,
                cy * sx * cz + sy * cx * sz,
                sy * cx * cz - cy * sx * sz,
                cy * cx * sz - sy * sx * cz).Normalize();
        }

        private static void ToEulerYxz(Quat q, out double yaw, out double pitch, out double roll)
        {
            float w = q.W, x = q.X, y = q.Y, z = q.Z;
            float xx = x * x, yy = y * y, zz = z * z;
            float xy = x * y, xz = x * z, yz = y * z;
            float wx = w * x, wy = w * y, wz = w * z;
            // Column-major m[col * 3 + row], as the Rust filter has it.
            float m0 = 1.0f - 2.0f * (yy + zz);
            float m1 = 2.0f * (xy + wz);
            float m2 = 2.0f * (xz - wy);
            float m4 = 1.0f - 2.0f * (xx + zz);
            float m6 = 2.0f * (xz + wy);
            float m7 = 2.0f * (yz - wx);
            float m8 = 1.0f - 2.0f * (xx + yy);
            double sinPitch = -(double)m7;
            double rx, ry, rz;
            if (System.Math.Abs(sinPitch) >= 0.99999)
            {
                // Pitch at +-90 degrees: only yaw +- roll is observable, so roll is set to zero.
                rx = (sinPitch < 0.0 ? -1.0 : 1.0) * (System.Math.PI / 2.0);
                rz = 0.0;
                ry = System.Math.Atan2(-(double)m2, m0);
            }
            else
            {
                rx = System.Math.Asin(sinPitch);
                ry = System.Math.Atan2(m6, m8);
                rz = System.Math.Atan2(m1, m4);
            }
            yaw = ry * DegPerRad;
            pitch = rx * DegPerRad;
            roll = -(rz * DegPerRad);
        }
    }
}
