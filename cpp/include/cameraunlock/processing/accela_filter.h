#pragma once

#include <array>
#include <utility>
#include <vector>

namespace cameraunlock {

/// A head pose as the Accela filter takes and gives it: yaw, pitch and roll in degrees,
/// in the OpenTrack packet's convention, and the position in the centimetres the OpenTrack packet carries, which its deadzone and gains are tuned for.
struct AccelaPose {
    double yaw = 0.0;
    double pitch = 0.0;
    double roll = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// The Accela filter, ported from Hypari's webcam tracker (hypari-webcam `smoothing.rs`),
/// which ports headcam-android's, itself a port of opentrack's filter-accela by Stanislaw
/// Halik (ISC licence). Rotation is filtered as one quaternion step toward the input, the
/// position per axis, each with its gain curve; one amount from 0 (lightest) to 1
/// (heaviest) sets every parameter. Its output on data/accela-cases.json matches the Rust
/// filter's, and the C# port's (CameraUnlock.Core.Processing.AccelaFilter).
///
/// A large step raises the gain, so it is eased less than a small one. There is no jump
/// handling: whatever feeds the filter has already judged the step.
class AccelaFilter {
public:
    /// The amount Hypari and headcam-android ship.
    static constexpr double kDefaultAmount = 0.75;
    /// A long gap is integrated as at most this much time.
    static constexpr double kMaxDt = 0.066;
    /// A dropped frame is integrated in steps; ordinary 30 Hz jitter is not split.
    static constexpr double kStep = 0.035;

    explicit AccelaFilter(double amount = kDefaultAmount);

    /// Keeps the filter's state; clamped to 0..1.
    void SetAmount(double amount);
    double GetAmount() const { return m_amount; }

    /// The next Filter() passes its input through and starts from it.
    void Reset();

    /// Filters one sample. @p timeSeconds is the sample's time on any clock that only goes
    /// forward and is above zero. A time not after the last one returns the last output.
    AccelaPose Filter(const AccelaPose& input, double timeSeconds);

    /// One parameter of the filter, as the amount sets it (exposed for tests).
    struct Params {
        double rotationDeadzone;
        double positionDeadzone;
        double rotationSmoothing;
        double positionSmoothing;
    };
    static Params ParamsFor(double amount);

private:
    struct Quat {
        float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
    };

    struct Axis {
        double minimumGain = 0.0;
        double lastOutput = 0.0;
        double lastInput = 0.0;
        double lastTime = 0.0;
        double Filter(double value, double time, double deadzone, double smoothing,
                      const std::vector<std::pair<double, double>>& curve);
    };

    struct Rotation {
        Quat lastOutput;
        Quat lastInput;
        double lastTime = 0.0;
        Quat Filter(Quat q, double time, double deadzone, double smoothing,
                    const std::vector<std::pair<double, double>>& curve);
    };

    static Quat FromEulerYxz(double yaw, double pitch, double roll);
    static void ToEulerYxz(Quat q, double& yaw, double& pitch, double& roll);

    double m_amount;
    Params m_params;
    std::array<Axis, 3> m_position;
    Rotation m_rotation;
};

}  // namespace cameraunlock
