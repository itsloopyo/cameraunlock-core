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
#include "cameraunlock/tracking/head_tracking_session.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
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

/// The tracking source the shipped HeadTrackingSession is driven by here: the
/// datagram bytes a vector supplies, decoded by the shipped packet parser and
/// offset the way UdpReceiver offsets its own output. Nothing in the session
/// pipeline is reimplemented, so a change to HeadTrackingSession fails here.
class VectorReceiver {
public:
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

        m_yaw = pose.yaw;
        m_pitch = pose.pitch;
        m_roll = pose.roll;
        m_x = position.x;
        m_y = position.y;
        m_z = position.z;
        m_hasData = true;
        ++m_timestamp;
    }

    void SetRemoteConnection(bool remote) { m_remote = remote; }

    bool GetRotation(float& yaw, float& pitch, float& roll) const {
        if (!m_hasData) return false;
        yaw = m_yaw - m_offsetYaw;
        pitch = m_pitch - m_offsetPitch;
        roll = m_roll - m_offsetRoll;
        return true;
    }

    bool GetPosition(float& x, float& y, float& z) const {
        if (!m_hasData) return false;
        x = m_x - m_offsetX;
        y = m_y - m_offsetY;
        z = m_z - m_offsetZ;
        return true;
    }

    int64_t GetLastReceiveTimestamp() const { return m_timestamp; }

    void Recenter() {
        m_offsetYaw = m_yaw;
        m_offsetPitch = m_pitch;
        m_offsetRoll = m_roll;
        m_offsetX = m_x;
        m_offsetY = m_y;
        m_offsetZ = m_z;
    }

    /// Always false, matching UdpReceiver. Present so the session instantiates the
    /// same way it does in a mod - detection failing would compile the remote
    /// branch away and hide a change to it.
    bool TryConsumeRecenterRequest() { return false; }

    bool IsRemoteConnection() const { return m_remote; }

private:
    float m_yaw = 0.0f, m_pitch = 0.0f, m_roll = 0.0f;
    float m_x = 0.0f, m_y = 0.0f, m_z = 0.0f;
    float m_offsetYaw = 0.0f, m_offsetPitch = 0.0f, m_offsetRoll = 0.0f;
    float m_offsetX = 0.0f, m_offsetY = 0.0f, m_offsetZ = 0.0f;
    bool m_hasData = false;
    bool m_remote = false;
    int64_t m_timestamp = 0;
};

using VectorSession = HeadTrackingSession<VectorReceiver>;
static_assert(VectorSession::kHasRemoteRecenter, "the session must see the receiver's trailer accessor");
static_assert(VectorSession::kHasRemoteConnection, "the session must select smoothing from the receiver");

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
    VectorReceiver m_receiver;
    std::unique_ptr<VectorSession> m_session;
    int64_t m_ts = 0;

    void Reset() {
        m_config.clear();
        m_poseInterp = PoseInterpolator();
        m_posInterp = PositionInterpolator();
        m_processor = TrackingProcessor();
        m_posProcessor = PositionProcessor();
        m_receiver = VectorReceiver();
        m_session = std::make_unique<VectorSession>(m_receiver);
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
    void ApplyRotationKeys(TrackingProcessor& p) {
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

    void ApplyPositionKeys(PositionSettings& s) {
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
    }

    void ConfigureTrackingProcessor(TrackingProcessor& p) {
        p.SetLocalSmoothing(Cfg("local_smoothing", p.GetLocalSmoothing()));
        p.SetRemoteSmoothing(Cfg("remote_smoothing", p.GetRemoteSmoothing()));
        p.SetIsRemoteConnection(CfgFlag("is_remote"));
        ApplyRotationKeys(p);
    }

    void ConfigurePositionProcessor(PositionProcessor& p) {
        p.SetTrackerPivotForward(Cfg("pivot", p.GetTrackerPivotForward()));
        p.SetTrackerPivotUp(Cfg("pivot_up", p.GetTrackerPivotUp()));
        PositionSettings s = p.GetSettings();
        ApplyPositionKeys(s);
        s.local_smoothing = Cfg("local_smoothing", s.local_smoothing);
        s.remote_smoothing = Cfg("remote_smoothing", s.remote_smoothing);
        p.SetSettings(s);
        p.SetIsRemoteConnection(CfgFlag("is_remote"));
    }

    /// The session owns the two smoothing values and re-asserts them on every
    /// Update, and it reads the connection kind off the receiver, so those three
    /// keys go to the session rather than straight to a processor.
    void ConfigureSession() {
        m_session->SetLocalSmoothing(Cfg("local_smoothing", m_session->GetLocalSmoothing()));
        m_session->SetRemoteSmoothing(Cfg("remote_smoothing", m_session->GetRemoteSmoothing()));
        m_receiver.SetRemoteConnection(CfgFlag("is_remote"));
        m_session->SetMaxExtrapolationFraction(
            Cfg("max_extrapolation_fraction", m_session->GetMaxExtrapolationFraction()));

        ApplyRotationKeys(m_session->GetProcessor());

        PositionProcessor& pp = m_session->GetPositionProcessor();
        pp.SetTrackerPivotForward(Cfg("pivot", pp.GetTrackerPivotForward()));
        pp.SetTrackerPivotUp(Cfg("pivot_up", pp.GetTrackerPivotUp()));
        PositionSettings ps = pp.GetSettings();
        ApplyPositionKeys(ps);
        m_session->SetPositionSettings(ps);
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
        } else if (m_unit == "session_rot" || m_unit == "session_pos") {
            ConfigureSession();
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

        // All three entry points, not just the combined one. TryParse, TryParsePosition
        // and TryParseAll each carry their own length and finiteness guards, so a harness
        // that exercises one of them leaves two sets of guards untested.
        TrackingPose pose;
        PositionData position;
        TrackingPose posePart;
        PositionData positionPart;
        const bool okAll = OpenTrackPacket::TryParseAll(bytes.data(), bytes.size(), pose, position);
        const bool okPose = OpenTrackPacket::TryParse(bytes.data(), bytes.size(), posePart);
        const bool okPosition =
            OpenTrackPacket::TryParsePosition(bytes.data(), bytes.size(), positionPart);
        const bool ok = okAll;

        uint8_t counter = 0;
        const bool trailer =
            OpenTrackPacket::TryParseRecenterCounter(bytes.data(), bytes.size(), counter);

        Emit({ok ? 1.0 : 0.0, ok ? pose.yaw : 0.0f, ok ? pose.pitch : 0.0f, ok ? pose.roll : 0.0f,
              ok ? position.x : 0.0f, ok ? position.y : 0.0f, ok ? position.z : 0.0f,
              trailer ? 1.0 : 0.0, static_cast<double>(counter),
              okPose ? 1.0 : 0.0, okPosition ? 1.0 : 0.0});
    }

    void SessionFrame(bool hasPacket, std::istringstream& in) {
        float dt;
        if (hasPacket) {
            std::string hex;
            in >> hex >> dt;
            m_receiver.Feed(FromHex(hex));
        } else {
            in >> dt;
        }

        m_session->Update(dt);
        if (m_unit == "session_rot") {
            float yaw, pitch, roll;
            m_session->GetRotation(yaw, pitch, roll);
            Emit({yaw, pitch, roll});
        } else {
            float x, y, z;
            m_session->GetPositionOffset(x, y, z);
            Emit({x, y, z});
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
