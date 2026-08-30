// Conformance harness for the C# pipeline.
//
// Same command stream and same output contract as the C++ harness in
// ../cpp/main.cpp - see scripts/pipeline-vectors/run-vectors.mjs for the protocol.
// Holds no assertion logic: the runner owns that, so both ports are checked by
// the same code against the same file.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;

internal static class Program
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    private static string _unit = "";
    private static readonly Dictionary<string, double> Config = new Dictionary<string, double>();

    private static PoseInterpolator _poseInterp = new PoseInterpolator();
    private static PositionInterpolator _posInterp = new PositionInterpolator();
    private static TrackingProcessor _processor = new TrackingProcessor();
    private static PositionProcessor _posProcessor = new PositionProcessor();
    private static Session _session = new Session();
    private static long _ts;

    /// Config keys the selected unit understood. A key left over after Configure()
    /// means the vector asked for something this implementation does not expose,
    /// and running it anyway would silently test a different thing - so the run is
    /// skipped with the key named.
    private static readonly HashSet<string> Consumed = new HashSet<string>();
    private static bool _skipping;

    private static int Main()
    {
        var output = new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = false };
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            if (line.Length == 0) continue;
            string[] t = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            switch (t[0])
            {
                case "unit":
                    _unit = t[1];
                    Reset();
                    break;
                case "cfg":
                    Config[t[1]] = double.Parse(t[2], Inv);
                    break;
                case "begin":
                    Configure(output);
                    break;
                case "s":
                    if (!_skipping) Step(t, output);
                    break;
                case "e":
                    if (!_skipping) EulerRoundtrip(t, output);
                    break;
                case "q":
                    if (!_skipping) Packet(t, output);
                    break;
                case "p":
                case "f":
                    if (!_skipping) SessionFrame(t, output);
                    break;
                case "end":
                    break;
                case "bye":
                    output.Flush();
                    return 0;
                default:
                    Console.Error.WriteLine("unknown command: " + t[0]);
                    return 2;
            }
        }

        output.Flush();
        return 0;
    }

    private static void Reset()
    {
        Config.Clear();
        _poseInterp = new PoseInterpolator();
        _posInterp = new PositionInterpolator();
        _processor = new TrackingProcessor();
        _posProcessor = new PositionProcessor();
        _session = new Session();
        _ts = 0;
        Consumed.Clear();
        _skipping = false;
    }

    private static float F(string s) => (float)double.Parse(s, Inv);

    /// Every configuration key is optional and an absent key leaves the
    /// implementation's own default in place. That is deliberate: it is how the
    /// pivot-defaults-off vector reads the default rather than restating it.
    private static float Cfg(string key, float fallback)
    {
        Consumed.Add(key);
        return Config.TryGetValue(key, out double v) ? (float)v : fallback;
    }

    private static bool Flag(string key)
    {
        Consumed.Add(key);
        return Config.TryGetValue(key, out double v) && v != 0.0;
    }

    private static void ConfigureTracking(TrackingProcessor p)
    {
        p.LocalSmoothing = Cfg("local_smoothing", p.LocalSmoothing);
        p.RemoteSmoothing = Cfg("remote_smoothing", p.RemoteSmoothing);
        p.IsRemoteConnection = Flag("is_remote");
        p.Sensitivity = new SensitivitySettings(
            Cfg("sens_yaw", p.Sensitivity.Yaw),
            Cfg("sens_pitch", p.Sensitivity.Pitch),
            Cfg("sens_roll", p.Sensitivity.Roll),
            Flag("invert_yaw"), Flag("invert_pitch"), Flag("invert_roll"));
        p.Deadzone = new DeadzoneSettings(
            Cfg("dz_yaw", p.Deadzone.Yaw),
            Cfg("dz_pitch", p.Deadzone.Pitch),
            Cfg("dz_roll", p.Deadzone.Roll));
    }

    private static void ConfigurePosition(PositionProcessor p)
    {
        p.TrackerPivotForward = Cfg("pivot", p.TrackerPivotForward);
        PositionSettings s = p.Settings;
        p.Settings = new PositionSettings(
            Cfg("sens_x", s.SensitivityX), Cfg("sens_y", s.SensitivityY), Cfg("sens_z", s.SensitivityZ),
            Cfg("limit_x", s.LimitX), Cfg("limit_y", s.LimitY), Cfg("limit_y_down", s.LimitYDown),
            Cfg("limit_z", s.LimitZ), Cfg("limit_z_back", s.LimitZBack),
            Cfg("local_smoothing", s.LocalSmoothing), Cfg("remote_smoothing", s.RemoteSmoothing),
            Flag("invert_x"), Flag("invert_y"), Flag("invert_z"));
        p.IsRemoteConnection = Flag("is_remote");
    }

    private static void Configure(TextWriter w)
    {
        switch (_unit)
        {
            case "pose_interpolator":
                _poseInterp.MaxExtrapolationFraction =
                    Cfg("max_extrapolation_fraction", _poseInterp.MaxExtrapolationFraction);
                break;
            case "position_interpolator":
                _posInterp.MaxExtrapolationFraction =
                    Cfg("max_extrapolation_fraction", _posInterp.MaxExtrapolationFraction);
                break;
            case "tracking_processor":
                ConfigureTracking(_processor);
                break;
            case "position_processor":
                ConfigurePosition(_posProcessor);
                break;
            case "session_rot":
                ConfigureTracking(_session.Processor);
                break;
            case "session_pos":
                ConfigureTracking(_session.Processor);
                ConfigurePosition(_session.PosProcessor);
                break;
            case "packet":
            case "euler_roundtrip":
                break;
            default:
                w.Write("skip C# harness does not implement unit " + _unit + "\n");
                _skipping = true;
                return;
        }

        foreach (string key in Config.Keys)
        {
            if (!Consumed.Contains(key))
            {
                w.Write("skip C# " + _unit + " has no setting for cfg key " + key + "\n");
                _skipping = true;
                return;
            }
        }

        w.Write("ok\n");
    }

    private static void Emit(TextWriter w, params double[] values)
    {
        for (int i = 0; i < values.Length; i++)
        {
            if (i > 0) w.Write(' ');
            w.Write(values[i].ToString("G10", Inv));
        }
        w.Write('\n');
    }

    private static void Step(string[] t, TextWriter w)
    {
        switch (_unit)
        {
            case "pose_interpolator":
            {
                TrackingPose outPose = _poseInterp.Update(
                    new TrackingPose(F(t[1]), F(t[2]), F(t[3])), t[4] != "0", F(t[5]));
                Emit(w, outPose.Yaw, outPose.Pitch, outPose.Roll);
                break;
            }
            case "position_interpolator":
            {
                PositionData outPos = _posInterp.Update(
                    new PositionData(F(t[1]), F(t[2]), F(t[3]), ++_ts), t[4] != "0", F(t[5]));
                Emit(w, outPos.X, outPos.Y, outPos.Z);
                break;
            }
            case "tracking_processor":
            {
                TrackingPose outPose = _processor.Process(
                    new TrackingPose(F(t[1]), F(t[2]), F(t[3])), F(t[4]));
                Emit(w, outPose.Yaw, outPose.Pitch, outPose.Roll);
                break;
            }
            case "position_processor":
            {
                Quat4 rot = QuaternionUtils.FromYawPitchRoll(F(t[4]), F(t[5]), F(t[6]));
                Vec3 outVec = _posProcessor.Process(
                    new PositionData(F(t[1]), F(t[2]), F(t[3]), ++_ts), rot, F(t[7]));
                Emit(w, outVec.X, outVec.Y, outVec.Z);
                break;
            }
            default:
                throw new InvalidOperationException("unit " + _unit + " does not take an `s` step");
        }
    }

    private static void EulerRoundtrip(string[] t, TextWriter w)
    {
        Quat4 q = QuaternionUtils.FromYawPitchRoll(F(t[1]), F(t[2]), F(t[3]));
        QuaternionUtils.ToEulerYXZ(q, out float yaw, out float pitch, out float roll);
        Emit(w, yaw, pitch, roll);
    }

    private static byte[] FromHex(string hex)
    {
        var bytes = new byte[hex.Length / 2];
        for (int i = 0; i < bytes.Length; i++)
        {
            bytes[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
        }
        return bytes;
    }

    private static void Packet(string[] t, TextWriter w)
    {
        byte[] bytes = FromHex(t[1]);
        bool okPose = OpenTrackPacket.TryParse(bytes, out TrackingPose pose);
        bool okPos = OpenTrackPacket.TryParsePosition(bytes, out PositionData position);
        bool ok = okPose && okPos;
        bool trailer = OpenTrackPacket.TryParseRecenterCounter(bytes, out byte counter);
        Emit(w,
            ok ? 1 : 0,
            ok ? pose.Yaw : 0f, ok ? pose.Pitch : 0f, ok ? pose.Roll : 0f,
            ok ? position.X : 0f, ok ? position.Y : 0f, ok ? position.Z : 0f,
            trailer ? 1 : 0, counter);
    }

    private static void SessionFrame(string[] t, TextWriter w)
    {
        float dt;
        if (t[0] == "p")
        {
            _session.Feed(FromHex(t[1]));
            dt = F(t[2]);
        }
        else
        {
            dt = F(t[1]);
        }

        _session.Step(dt);
        if (_unit == "session_rot")
        {
            Emit(w, _session.OutRotation.Yaw, _session.OutRotation.Pitch, _session.OutRotation.Roll);
        }
        else
        {
            Emit(w, _session.OutPosition.X, _session.OutPosition.Y, _session.OutPosition.Z);
        }
    }

    /// Per-frame wiring shared by session_rot and session_pos, mirroring
    /// HeadTrackingSession: the duplicate-sample filter gates the interpolator on
    /// changed VALUES, not on the arrival of a datagram, and nothing anywhere
    /// recenters - the trailer is parsed and dropped.
    private sealed class Session
    {
        public readonly PoseInterpolator PoseInterp = new PoseInterpolator();
        public readonly TrackingProcessor Processor = new TrackingProcessor();
        public readonly PositionInterpolator PosInterp = new PositionInterpolator();
        public readonly PositionProcessor PosProcessor = new PositionProcessor();

        public TrackingPose OutRotation;
        public Vec3 OutPosition;

        private float _rawYaw, _rawPitch, _rawRoll, _rawX, _rawY, _rawZ;
        private float _lastYaw, _lastPitch, _lastRoll, _lastX, _lastY, _lastZ;
        private bool _seeded;
        private bool _newPacket;
        private long _timestamp;

        public void Feed(byte[] datagram)
        {
            if (!OpenTrackPacket.TryParse(datagram, out TrackingPose pose)) return;
            if (!OpenTrackPacket.TryParsePosition(datagram, out PositionData position)) return;

            // The trailer is read and then not acted on. Parsing it here is the
            // point: a receiver that cannot parse a 54-byte datagram drops the
            // pose it carries, and one that acts on it puts a second centre in
            // series with the tracker's own.
            OpenTrackPacket.TryParseRecenterCounter(datagram, out byte _);

            _rawYaw = pose.Yaw;
            _rawPitch = pose.Pitch;
            _rawRoll = pose.Roll;
            _rawX = position.X;
            _rawY = position.Y;
            _rawZ = position.Z;
            _newPacket = true;
            _timestamp++;
        }

        public void Step(float dt)
        {
            bool newRot = _newPacket && (!_seeded || _rawYaw != _lastYaw ||
                                         _rawPitch != _lastPitch || _rawRoll != _lastRoll);
            bool newPos = _newPacket && (!_seeded || _rawX != _lastX || _rawY != _lastY || _rawZ != _lastZ);
            if (_newPacket)
            {
                _lastYaw = _rawYaw;
                _lastPitch = _rawPitch;
                _lastRoll = _rawRoll;
                _lastX = _rawX;
                _lastY = _rawY;
                _lastZ = _rawZ;
                _seeded = true;
            }
            _newPacket = false;

            TrackingPose interp = PoseInterp.Update(
                new TrackingPose(_rawYaw, _rawPitch, _rawRoll), newRot, dt);
            OutRotation = Processor.Process(interp, dt);

            PositionData interpPos = PosInterp.Update(
                new PositionData(_rawX, _rawY, _rawZ, _timestamp == 0 ? 1 : _timestamp), newPos, dt);

            Processor.GetSmoothedRotation(out float py, out float pp, out float pr);
            OutPosition = PosProcessor.Process(interpPos, QuaternionUtils.FromYawPitchRoll(py, pp, pr), dt);
        }
    }
}
