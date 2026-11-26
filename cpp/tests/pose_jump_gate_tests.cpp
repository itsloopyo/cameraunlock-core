// PoseJumpGate against data/fixtures/pose-jump-gate/cases.tsv, which the C# gate runs too
// (csharp/src/CameraUnlock.Core.Tests/Protocol/PoseJumpGateTests.cs).

#include <cameraunlock/protocol/pose_jump_gate.h>
#include <cameraunlock/protocol/udp_receiver.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) fields.push_back(field);
    return fields;
}

}  // namespace

int RunPoseJumpGateTests() {
    std::cout << "\n[pose_jump_gate]\n";
    g_failures = 0;

    Check(cameraunlock::UdpReceiver::kConfirmJumpDegrees == cameraunlock::PoseJumpGate::kConfirmJumpDegrees,
          "the receiver's constant is the gate's");

    std::ifstream file(CAMERAUNLOCK_POSE_JUMP_GATE_FIXTURE);
    if (!file) {
        Check(false, std::string("opens ") + CAMERAUNLOCK_POSE_JUMP_GATE_FIXTURE);
        return g_failures;
    }

    cameraunlock::PoseJumpGate gate;
    std::string name;
    std::string answers;
    std::string expected;
    int cases = 0;
    const auto finish = [&] {
        if (name.empty()) return;
        ++cases;
        Check(answers == expected, name + " (published " + answers + ", expected " + expected + ")");
    };

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = SplitTabs(line);
        if (fields[0] == "case") {
            finish();
            name = fields.at(1);
            answers.clear();
            expected.clear();
            gate.Reset();
            continue;
        }
        const float yaw = std::stof(fields.at(1));
        const float pitch = std::stof(fields.at(2));
        const float roll = std::stof(fields.at(3));
        bool published = true;
        if (fields[0] == "announce") {
            gate.Announce(yaw, pitch, roll);
        } else if (fields[0] == "pose") {
            published = gate.Accept(yaw, pitch, roll);
        } else {
            Check(false, "unknown row '" + fields[0] + "' in the fixture");
            return g_failures;
        }
        answers.push_back(published ? '1' : '0');
        expected.append(fields.at(4));
    }
    finish();
    Check(cases == 15, "every case of the fixture ran (" + std::to_string(cases) + ")");

    return g_failures;
}
