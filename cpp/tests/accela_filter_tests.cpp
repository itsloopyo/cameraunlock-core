// AccelaFilter against data/accela-cases.json, the outputs Hypari's Rust filter gives on the
// same inputs, which the C# port matches too.

#include <cameraunlock/processing/accela_filter.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
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

// The C++ port makes the Rust filter's float and double operations in the same order, so
// it matches to the bit.
constexpr double kTolerance = 0.0;

struct Sample {
    double time;
    cameraunlock::AccelaPose input;
    cameraunlock::AccelaPose output;
};

struct Case {
    std::string name;
    double amount;
    std::vector<Sample> samples;
};

cameraunlock::AccelaPose ReadPose(const std::string& fields) {
    static const std::regex field(R"re("(\w+)"\s*:\s*([-+0-9.eE]+))re");
    std::map<std::string, double> values;
    for (std::sregex_iterator it(fields.begin(), fields.end(), field), end; it != end; ++it) {
        values[(*it)[1].str()] = std::stod((*it)[2].str());
    }
    for (const char* key : {"yaw", "pitch", "roll", "x", "y", "z"}) {
        if (values.count(key) == 0) throw std::runtime_error(std::string("a pose in accela-cases.json has no ") + key);
    }
    cameraunlock::AccelaPose pose;
    pose.yaw = values["yaw"];
    pose.pitch = values["pitch"];
    pose.roll = values["roll"];
    pose.x = values["x"];
    pose.y = values["y"];
    pose.z = values["z"];
    return pose;
}

std::vector<Case> ReadCases() {
    std::ifstream in(CAMERAUNLOCK_ACCELA_CASES, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error(std::string("cannot open ") + CAMERAUNLOCK_ACCELA_CASES);
    std::stringstream text;
    text << in.rdbuf();
    const std::string json = text.str();

    // serde_json writes each object's keys in order: amount, name, samples; input, output, time.
    static const std::regex head(R"re("amount"\s*:\s*([-+0-9.eE]+)\s*,\s*"name"\s*:\s*"([^"]*)")re");
    static const std::regex sample(
        R"re("input"\s*:\s*\{([^}]*)\}\s*,\s*"output"\s*:\s*\{([^}]*)\}\s*,\s*"time"\s*:\s*([-+0-9.eE]+))re");
    std::vector<std::pair<size_t, Case>> starts;
    for (std::sregex_iterator it(json.begin(), json.end(), head), end; it != end; ++it) {
        Case c;
        c.amount = std::stod((*it)[1].str());
        c.name = (*it)[2].str();
        starts.emplace_back(static_cast<size_t>(it->position()), c);
    }
    std::vector<Case> cases;
    for (size_t i = 0; i < starts.size(); ++i) {
        const size_t from = starts[i].first;
        const size_t to = i + 1 < starts.size() ? starts[i + 1].first : json.size();
        const std::string body = json.substr(from, to - from);
        Case c = starts[i].second;
        for (std::sregex_iterator it(body.begin(), body.end(), sample), end; it != end; ++it) {
            c.samples.push_back({std::stod((*it)[3].str()), ReadPose((*it)[1].str()), ReadPose((*it)[2].str())});
        }
        if (c.samples.empty()) throw std::runtime_error("case '" + c.name + "' in accela-cases.json has no samples");
        cases.push_back(c);
    }
    return cases;
}

double Worst(const cameraunlock::AccelaPose& a, const cameraunlock::AccelaPose& b) {
    // Yaw near the +-180 seam is compared the short way round.
    double yaw = std::fabs(a.yaw - b.yaw);
    if (yaw > 180.0) yaw = 360.0 - yaw;
    const double rest[] = {std::fabs(a.pitch - b.pitch), std::fabs(a.roll - b.roll), std::fabs(a.x - b.x),
                           std::fabs(a.y - b.y), std::fabs(a.z - b.z)};
    double worst = yaw;
    for (double d : rest) worst = d > worst ? d : worst;
    return worst;
}

void TestFixture() {
    std::cout << "\n[AccelaFilter gives the outputs in data/accela-cases.json]\n";
    const std::vector<Case> cases = ReadCases();
    Check(cases.size() >= 8, "data/accela-cases.json reads as at least eight cases");
    for (const Case& c : cases) {
        cameraunlock::AccelaFilter filter(c.amount);
        double worst = 0.0;
        size_t at = 0;
        for (size_t i = 0; i < c.samples.size(); ++i) {
            const double d = Worst(filter.Filter(c.samples[i].input, c.samples[i].time), c.samples[i].output);
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        std::ostringstream name;
        name << c.name << " (worst difference " << worst << " at sample " << at << ")";
        Check(worst <= kTolerance, name.str());
    }
}

void TestSmallStepEases() {
    std::cout << "\n[a small step eases: the first output lies between the old pose and the new]\n";
    cameraunlock::AccelaFilter filter;
    cameraunlock::AccelaPose pose;
    filter.Filter(pose, 1.0);
    pose.yaw = 2.0;
    pose.x = 1.0;
    const cameraunlock::AccelaPose out = filter.Filter(pose, 1.0 + 1.0 / 60.0);
    Check(out.yaw > 0.0 && out.yaw < 2.0, "yaw moves part of the way");
    Check(out.x > 0.0 && out.x < 1.0, "x moves part of the way");
}

void TestResetStartsFresh() {
    std::cout << "\n[Reset starts from the next input]\n";
    cameraunlock::AccelaFilter filter;
    cameraunlock::AccelaPose pose;
    filter.Filter(pose, 1.0);
    filter.Reset();
    pose.yaw = 25.0;
    pose.z = -3.0;
    const cameraunlock::AccelaPose out = filter.Filter(pose, 1.5);
    Check(std::fabs(out.yaw - 25.0) < 1e-4 && std::fabs(out.z + 3.0) < 1e-12, "the first input after Reset passes through");
}

void TestAmountKeepsState() {
    std::cout << "\n[SetAmount keeps the filter's state and clamps to 0..1]\n";
    cameraunlock::AccelaFilter filter;
    cameraunlock::AccelaPose pose;
    pose.yaw = 10.0;
    filter.Filter(pose, 1.0);
    filter.SetAmount(3.0);
    Check(filter.GetAmount() == 1.0, "an amount above 1 is 1");
    pose.yaw = 10.0;
    const cameraunlock::AccelaPose out = filter.Filter(pose, 1.0 + 1.0 / 60.0);
    Check(std::fabs(out.yaw - 10.0) < 1e-4, "the output stays where the filter was");
}

}  // namespace

int RunAccelaFilterTests() {
    g_failures = 0;
    try {
        TestFixture();
    } catch (const std::exception& e) {
        Check(false, std::string("reading data/accela-cases.json: ") + e.what());
    }
    TestSmallStepEases();
    TestResetStartsFresh();
    TestAmountKeepsState();
    return g_failures;
}
