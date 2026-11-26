// Conformance harness for the C interface (docs/c-interface.md).
//
// Speaks the line protocol scripts/pipeline-vectors/run-vectors.mjs drives, and reaches the
// pipeline only through cameraunlock/c/cameraunlock.h, as a Java or Lua host does: a datagram in,
// one cameraunlock_session_frame, the pose read out of CameraUnlockFrame. What the vectors hold
// here is the boundary: the wire's units arriving as metres and degrees, the struct's members in
// their places, and the order of calls.
//
// A unit the interface does not expose is skipped, with the reason, and pixi.toml's vectors-c
// names each skipped vector. The interface has one entry to the pipeline, the frame, so the
// vectors that drive an interpolator or a processor on its own are the C++ harness's to run, on
// the same code this DLL is linked from.

#include "cameraunlock/c/testing/cameraunlock_testing.h"

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> FromHex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

void Emit(std::initializer_list<double> values) {
    std::ostringstream line;
    line << std::setprecision(10);
    bool first = true;
    for (const double value : values) {
        if (!first) line << ' ';
        first = false;
        line << value;
    }
    std::cout << line.str() << '\n';
}

// A call that fails ends the run: the runner reports a harness that exited early.
void Must(std::int32_t status) {
    if (status != CAMERAUNLOCK_ERROR) return;
    char reason[1024] = {};
    cameraunlock_last_error(reason, sizeof(reason));
    std::cerr << reason << '\n';
    std::exit(3);
}

class Harness {
public:
    void Run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::istringstream in(line);
            std::string command;
            in >> command;
            if (command == "unit") {
                in >> m_unit >> m_vector;
                m_config.clear();
                m_skipping = false;
            } else if (command == "cfg") {
                std::string key;
                double value;
                in >> key >> value;
                m_config[key] = value;
            } else if (command == "begin") {
                Begin();
            } else if (command == "q") {
                if (!m_skipping) Packet(in);
            } else if (command == "p" || command == "f") {
                if (!m_skipping) Frame(command == "p", in);
            } else if (command == "s" || command == "e") {
                if (!m_skipping) {
                    std::cerr << "a step for a unit this harness skips\n";
                    std::exit(2);
                }
            } else if (command == "end") {
            } else if (command == "bye") {
                return;
            } else {
                std::cerr << "unknown command: " << command << '\n';
                std::exit(2);
            }
        }
    }

private:
    void Skip(const std::string& reason) {
        std::cout << "skip " << reason << '\n';
        m_skipping = true;
    }

    // The session as a new process has it, with the vector's settings over core's defaults.
    bool Configure() {
        Must(cameraunlock_testing_reset());
        CameraUnlockSettings settings = {};
        settings.struct_size = sizeof(settings);
        Must(cameraunlock_settings_defaults(&settings));
        m_remote = false;
        for (const auto& entry : m_config) {
            const float value = static_cast<float>(entry.second);
            if (entry.first == "local_smoothing") settings.local_smoothing = value;
            else if (entry.first == "remote_smoothing") settings.remote_smoothing = value;
            else if (entry.first == "is_remote") m_remote = entry.second != 0.0;
            else if (entry.first == "limit_x") settings.limit_x = value;
            else if (entry.first == "limit_y") settings.limit_y = value;
            else if (entry.first == "limit_y_down") settings.limit_y_down = value;
            else if (entry.first == "limit_z") settings.limit_z = value;
            else if (entry.first == "limit_z_back") settings.limit_z_back = value;
            else {
                Skip("the C interface has no setting for cfg key " + entry.first);
                return false;
            }
        }
        Must(cameraunlock_session_configure(&settings));
        m_now_ms = 0.0;
        return true;
    }

    void Begin() {
        if (m_unit == "packet") {
            if (m_vector == "wire-position-is-centimetres" || m_vector == "hcam-trailer-parses" ||
                m_vector == "packet-accepts-a-longer-datagram-without-inventing-a-trailer" ||
                m_vector == "hcam-trailer-version-is-forward-compatible") {
                Skip("the C interface does not say whether a datagram carried a trailer, which this vector asserts");
                return;
            }
        } else if (m_vector == "hcam-trailer-does-not-recenter" || m_vector == "no-center-captured-on-connect-rotation") {
            // The stream drops from a held pose to zero and then repeats zero bit for bit, which
            // is the shape UdpReceiver's lost-tracker gate refuses (protocol/pose_jump_gate.h).
            // The C++ harness hands its session the parsed pose, past the receiver.
            Skip("the receiver holds a jump to a pose that then repeats bit for bit, and this vector's stream is one");
            return;
        } else if (m_unit != "session_rot" && m_unit != "session_pos") {
            Skip("the C interface runs the whole pipeline in one call and does not expose unit " + m_unit);
            return;
        }
        if (Configure()) std::cout << "ok\n";
    }

    CameraUnlockFrame OneFrame(float delta) {
        CameraUnlockFrameInput input = {};
        input.struct_size = sizeof(input);
        input.flags = CAMERAUNLOCK_FRAME_ACTIVE;
        m_now_ms += static_cast<double>(delta) * 1000.0;
        input.now_ms = static_cast<std::uint64_t>(m_now_ms);
        input.delta_seconds = delta;
        input.tan_half_fov = 1.0f;
        input.tan_half_fov_base = 1.0f;
        input.forward_stop = std::numeric_limits<float>::infinity();
        CameraUnlockFrame frame = {};
        frame.struct_size = sizeof(frame);
        Must(cameraunlock_session_frame(&input, &frame));
        return frame;
    }

    // Each datagram meets a session that has seen nothing. The interface has one verdict on a
    // datagram, whether it gave a pose, so ok_rotation and ok_position repeat it.
    void Packet(std::istringstream& in) {
        std::string hex;
        in >> hex;
        const std::vector<std::uint8_t> bytes = FromHex(hex);
        Configure();
        Must(cameraunlock_testing_deliver(bytes.data(), static_cast<std::int32_t>(bytes.size()), 0));
        const CameraUnlockFrame frame = OneFrame(0.0f);
        const double ok = (frame.flags & CAMERAUNLOCK_STATE_POSE) != 0 ? 1.0 : 0.0;
        Emit({ok, frame.head_yaw, frame.head_pitch, frame.head_roll, frame.head_x, frame.head_y, frame.head_z, 0.0, 0.0,
              ok, ok});
    }

    void Frame(bool has_packet, std::istringstream& in) {
        float delta = 0.0f;
        if (has_packet) {
            std::string hex;
            in >> hex >> delta;
            const std::vector<std::uint8_t> bytes = FromHex(hex);
            Must(cameraunlock_testing_deliver(bytes.data(), static_cast<std::int32_t>(bytes.size()), m_remote ? 1 : 0));
        } else {
            in >> delta;
        }
        const CameraUnlockFrame frame = OneFrame(delta);
        if (m_unit == "session_rot") {
            Emit({frame.head_yaw, frame.head_pitch, frame.head_roll});
        } else {
            Emit({frame.head_x, frame.head_y, frame.head_z});
        }
    }

    std::string m_unit;
    std::string m_vector;
    std::map<std::string, double> m_config;
    bool m_skipping = false;
    bool m_remote = false;
    double m_now_ms = 0.0;
};

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    Harness harness;
    harness.Run();
    std::cout.flush();
    return 0;
}
