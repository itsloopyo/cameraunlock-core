// Conformance harness for the C++ pipeline.
//
// Reads the command stream described in scripts/pipeline-vectors/run-vectors.mjs on
// stdin and prints one line of results per step. It holds no assertion logic:
// the runner owns that, so C++, C# and every language port are checked by the
// same code against the same file.

#include "cameraunlock/data/position_data.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/processing/pose_interpolator.h"
#include "cameraunlock/processing/position_interpolator.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/processing/tracking_processor.h"
#include "cameraunlock/protocol/opentrack_packet.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace cameraunlock;

namespace {

std::vector<uint8_t> FromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

void Emit(std::initializer_list<double> values) {
    std::ostringstream line;
    line << std::setprecision(10);
    bool first = true;
    for (double v : values) {
        if (!first) line << ' ';
        first = false;
        line << v;
    }
    std::cout << line.str() << '\n';
}

/// Per-frame wiring shared by session_rot and session_pos, mirroring
/// HeadTrackingSession: the duplicate-sample filter gates the interpolator on
/// changed VALUES, not on the arrival of a datagram, and nothing anywhere
/// recenters - the trailer is parsed and dropped.
struct Session {
    PoseInterpolator poseInterp;
    TrackingProcessor processor;
    PositionInterpolator posInterp;
    PositionProcessor posProcessor;

    float rawYaw = 0.0f, rawPitch = 0.0f, rawRoll = 0.0f;
    float rawX = 0.0f, rawY = 0.0f, rawZ = 0.0f;
    float lastYaw = 0.0f, lastPitch = 0.0f, lastRoll = 0.0f;
    float lastX = 0.0f, lastY = 0.0f, lastZ = 0.0f;
    bool seeded = false;
    int64_t timestamp = 0;

    bool newPacket = false;

    void Feed(const std::vector<uint8_t>& datagram) {
        TrackingPose pose;
        PositionData position;
        if (!OpenTrackPacket::TryParseAll(datagram.data(), datagram.size(), pose, position)) {
            return;
        }
        // The trailer is read and then not acted on. Parsing it here is the
        // point: a receiver that cannot parse a 54-byte datagram drops the pose
        // it carries, and one that acts on it puts a second centre in series
        // with the tracker's own.
        uint8_t counter = 0;
        (void)OpenTrackPacket::TryParseRecenterCounter(datagram.data(), datagram.size(), counter);

        rawYaw = pose.yaw;
        rawPitch = pose.pitch;
        rawRoll = pose.roll;
        rawX = position.x;
        rawY = position.y;
        rawZ = position.z;
        newPacket = true;
        ++timestamp;
    }

    TrackingPose outRotation;
    math::Vec3 outPosition;

    /// One render frame, both halves, in the order HeadTrackingSession runs
    /// them: the position pivot term needs the rotation processor's smoothed
    /// state from this same frame.
    void Step(float dt) {
        const bool newRotSample = newPacket && (!seeded || rawYaw != lastYaw ||
                                                rawPitch != lastPitch || rawRoll != lastRoll);
        const bool newPosSample =
            newPacket && (!seeded || rawX != lastX || rawY != lastY || rawZ != lastZ);
        if (newPacket) {
            lastYaw = rawYaw;
            lastPitch = rawPitch;
            lastRoll = rawRoll;
            lastX = rawX;
            lastY = rawY;
            lastZ = rawZ;
            seeded = true;
        }
        newPacket = false;

        InterpolatedPose interp = poseInterp.Update(rawYaw, rawPitch, rawRoll, newRotSample, dt);
        outRotation = processor.Process(interp.yaw, interp.pitch, interp.roll, dt);

        PositionData raw(rawX, rawY, rawZ, timestamp == 0 ? 1 : timestamp);
        PositionData interpPos = posInterp.Update(raw, newPosSample, dt);

        float physYaw, physPitch, physRoll;
        processor.GetSmoothedRotation(physYaw, physPitch, physRoll);
        math::Quat4 physQ = math::Quat4::FromYawPitchRoll(physYaw, physPitch, physRoll);

        outPosition = posProcessor.Process(interpPos, physQ, dt);
    }
};

class Harness {
public:
    void Run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::istringstream in(line);
            std::string cmd;
            in >> cmd;

            if (cmd == "unit") {
                in >> m_unit;
                Reset();
            } else if (cmd == "cfg") {
                std::string key;
                double value;
                in >> key >> value;
                m_config[key] = value;
            } else if (cmd == "begin") {
                Configure();
            } else if (cmd == "s") {
                if (!m_skipping) Step(in);
            } else if (cmd == "e") {
                if (!m_skipping) EulerRoundtrip(in);
            } else if (cmd == "q") {
                if (!m_skipping) Packet(in);
            } else if (cmd == "p" || cmd == "f") {
                if (!m_skipping) SessionFrame(cmd == "p", in);
            } else if (cmd == "end") {
                // nothing to do; the next `unit` resets
            } else if (cmd == "bye") {
                return;
            } else {
                std::cerr << "unknown command: " << cmd << '\n';
                std::exit(2);
            }
        }
    }

private:
    std::string m_unit;
    std::map<std::string, double> m_config;
    /// Config keys this unit understood. A key left over after Configure() means
    /// the vector asked for something this implementation does not expose, and
    /// running it anyway would silently test a different thing - so the run is
    /// skipped with the key named.
    std::vector<std::string> m_consumed;
    bool m_skipping = false;

    PoseInterpolator m_poseInterp;
    PositionInterpolator m_posInterp;
    TrackingProcessor m_processor;
    PositionProcessor m_posProcessor;
    Session m_session;
    int64_t m_ts = 0;

    void Reset() {
        m_config.clear();
        m_poseInterp = PoseInterpolator();
        m_posInterp = PositionInterpolator();
        m_processor = TrackingProcessor();
        m_posProcessor = PositionProcessor();
        m_session = Session();
        m_ts = 0;
        m_consumed.clear();
        m_skipping = false;
    }

    float Cfg(const char* key, float fallback) {
        m_consumed.push_back(key);
        auto it = m_config.find(key);
        return it == m_config.end() ? fallback : static_cast<float>(it->second);
    }

    bool CfgFlag(const char* key) {
        m_consumed.push_back(key);
        auto it = m_config.find(key);
        return it != m_config.end() && it->second != 0.0;
    }

    /// Every configuration key is optional and an absent key leaves the
    /// implementation's own default in place. That is deliberate: it is how the
    /// pivot-defaults-off vector reads the default rather than restating it.
    void ConfigureTrackingProcessor(TrackingProcessor& p) {
        p.SetLocalSmoothing(Cfg("local_smoothing", p.GetLocalSmoothing()));
        p.SetRemoteSmoothing(Cfg("remote_smoothing", p.GetRemoteSmoothing()));
        p.SetIsRemoteConnection(CfgFlag("is_remote"));

        SensitivitySettings s = p.GetSensitivity();
        s.yaw = Cfg("sens_yaw", s.yaw);
        s.pitch = Cfg("sens_pitch", s.pitch);
        s.roll = Cfg("sens_roll", s.roll);
        s.invert_yaw = CfgFlag("invert_yaw");
        s.invert_pitch = CfgFlag("invert_pitch");
        s.invert_roll = CfgFlag("invert_roll");
        p.SetSensitivity(s);

        DeadzoneSettings d = p.GetDeadzone();
        d.yaw = Cfg("dz_yaw", d.yaw);
        d.pitch = Cfg("dz_pitch", d.pitch);
        d.roll = Cfg("dz_roll", d.roll);
        p.SetDeadzone(d);
    }

    void ConfigurePositionProcessor(PositionProcessor& p) {
        p.SetTrackerPivotForward(Cfg("pivot", p.GetTrackerPivotForward()));
        PositionSettings s = p.GetSettings();
        s.sensitivity_x = Cfg("sens_x", s.sensitivity_x);
        s.sensitivity_y = Cfg("sens_y", s.sensitivity_y);
        s.sensitivity_z = Cfg("sens_z", s.sensitivity_z);
        s.invert_x = CfgFlag("invert_x");
        s.invert_y = CfgFlag("invert_y");
        s.invert_z = CfgFlag("invert_z");
        s.limit_x = Cfg("limit_x", s.limit_x);
        s.limit_y = Cfg("limit_y", s.limit_y);
        s.limit_y_down = Cfg("limit_y_down", s.limit_y_down);
        s.limit_z = Cfg("limit_z", s.limit_z);
        s.limit_z_back = Cfg("limit_z_back", s.limit_z_back);
        s.local_smoothing = Cfg("local_smoothing", s.local_smoothing);
        s.remote_smoothing = Cfg("remote_smoothing", s.remote_smoothing);
        p.SetSettings(s);
        p.SetIsRemoteConnection(CfgFlag("is_remote"));
    }

    void Configure() {
        if (m_unit == "pose_interpolator") {
            m_poseInterp.max_extrapolation_fraction =
                Cfg("max_extrapolation_fraction", m_poseInterp.max_extrapolation_fraction);
        } else if (m_unit == "position_interpolator") {
            m_posInterp.SetMaxExtrapolationFraction(
                Cfg("max_extrapolation_fraction", m_posInterp.GetMaxExtrapolationFraction()));
        } else if (m_unit == "tracking_processor") {
            ConfigureTrackingProcessor(m_processor);
        } else if (m_unit == "position_processor") {
            ConfigurePositionProcessor(m_posProcessor);
        } else if (m_unit == "session_rot") {
            ConfigureTrackingProcessor(m_session.processor);
        } else if (m_unit == "session_pos") {
            ConfigureTrackingProcessor(m_session.processor);
            ConfigurePositionProcessor(m_session.posProcessor);
        } else if (m_unit != "packet" && m_unit != "euler_roundtrip") {
            std::cout << "skip C++ harness does not implement unit " << m_unit << '\n';
            m_skipping = true;
            return;
        }

        for (const auto& entry : m_config) {
            if (std::find(m_consumed.begin(), m_consumed.end(), entry.first) == m_consumed.end()) {
                std::cout << "skip C++ " << m_unit << " has no setting for cfg key "
                          << entry.first << '\n';
                m_skipping = true;
                return;
            }
        }
        std::cout << "ok\n";
    }

    void Step(std::istringstream& in) {
        if (m_unit == "pose_interpolator") {
            float yaw, pitch, roll, dt;
            int isNew;
            in >> yaw >> pitch >> roll >> isNew >> dt;
            InterpolatedPose out = m_poseInterp.Update(yaw, pitch, roll, isNew != 0, dt);
            Emit({out.yaw, out.pitch, out.roll});
        } else if (m_unit == "position_interpolator") {
            float x, y, z, dt;
            int isNew;
            in >> x >> y >> z >> isNew >> dt;
            PositionData out = m_posInterp.Update(PositionData(x, y, z, ++m_ts), isNew != 0, dt);
            Emit({out.x, out.y, out.z});
        } else if (m_unit == "tracking_processor") {
            float yaw, pitch, roll, dt;
            in >> yaw >> pitch >> roll >> dt;
            TrackingPose out = m_processor.Process(yaw, pitch, roll, dt);
            Emit({out.yaw, out.pitch, out.roll});
        } else if (m_unit == "position_processor") {
            float x, y, z, ryaw, rpitch, rroll, dt;
            in >> x >> y >> z >> ryaw >> rpitch >> rroll >> dt;
            math::Quat4 rot = math::Quat4::FromYawPitchRoll(ryaw, rpitch, rroll);
            math::Vec3 out = m_posProcessor.Process(PositionData(x, y, z, ++m_ts), rot, dt);
            Emit({out.x, out.y, out.z});
        } else {
            std::cerr << "unit " << m_unit << " does not take an `s` step\n";
            std::exit(2);
        }
    }

    void EulerRoundtrip(std::istringstream& in) {
        float yaw, pitch, roll;
        in >> yaw >> pitch >> roll;
        math::Quat4 q = math::Quat4::FromYawPitchRoll(yaw, pitch, roll);
        float oy, op, orr;
        q.ToEulerYXZ(oy, op, orr);
        Emit({oy, op, orr});
    }

    void Packet(std::istringstream& in) {
        std::string hex;
        in >> hex;
        std::vector<uint8_t> bytes = FromHex(hex);

        TrackingPose pose;
        PositionData position;
        const bool ok = OpenTrackPacket::TryParseAll(bytes.data(), bytes.size(), pose, position);

        uint8_t counter = 0;
        const bool trailer =
            OpenTrackPacket::TryParseRecenterCounter(bytes.data(), bytes.size(), counter);

        Emit({ok ? 1.0 : 0.0, ok ? pose.yaw : 0.0f, ok ? pose.pitch : 0.0f, ok ? pose.roll : 0.0f,
              ok ? position.x : 0.0f, ok ? position.y : 0.0f, ok ? position.z : 0.0f,
              trailer ? 1.0 : 0.0, static_cast<double>(counter)});
    }

    void SessionFrame(bool hasPacket, std::istringstream& in) {
        float dt;
        if (hasPacket) {
            std::string hex;
            in >> hex >> dt;
            m_session.Feed(FromHex(hex));
        } else {
            in >> dt;
        }

        m_session.Step(dt);
        if (m_unit == "session_rot") {
            Emit({m_session.outRotation.yaw, m_session.outRotation.pitch, m_session.outRotation.roll});
        } else {
            Emit({m_session.outPosition.x, m_session.outPosition.y, m_session.outPosition.z});
        }
    }
};

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    Harness harness;
    harness.Run();
    std::cout.flush();
    return 0;
}
