#include "cameraunlock/protocol/opentrack_packet.h"
#include <cmath>
#include <cstring>

namespace cameraunlock {

namespace {
// A finite double can exceed the float range (e.g. 1e300), in which case the
// narrowing cast produces +/-inf - which then poisons every downstream
// sin/cos and view-matrix computation with NaN. Checking isnan/isinf on the
// source double is not enough; the float result must also be finite. Packets
// arrive from any host on the network (the socket binds INADDR_ANY), so this
// is an untrusted-input boundary.
// Matches OpenTrackPacket.CmToMeters on the C# side.
constexpr float kCmToMeters = 0.01f;

inline bool FiniteFloat(double v, float& out) {
    out = static_cast<float>(v);
    return std::isfinite(out);
}
}  // namespace

bool OpenTrackPacket::TryParse(const void* data, size_t length, TrackingPose& pose) {
    if (data == nullptr || length < kMinPacketSize) {
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);

    double yaw, pitch, roll;
    std::memcpy(&yaw, bytes + kYawOffset, sizeof(double));
    std::memcpy(&pitch, bytes + kPitchOffset, sizeof(double));
    std::memcpy(&roll, bytes + kRollOffset, sizeof(double));

    float fyaw, fpitch, froll;
    if (!FiniteFloat(yaw, fyaw) || !FiniteFloat(pitch, fpitch) || !FiniteFloat(roll, froll)) {
        return false;
    }

    pose = TrackingPose(fyaw, fpitch, froll);
    return true;
}

bool OpenTrackPacket::TryParsePosition(const void* data, size_t length, PositionData& position) {
    if (data == nullptr || length < kMinPacketSize) {
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);

    double px, py, pz;
    std::memcpy(&px, bytes + kPosXOffset, sizeof(double));
    std::memcpy(&py, bytes + kPosYOffset, sizeof(double));
    std::memcpy(&pz, bytes + kPosZOffset, sizeof(double));

    // OpenTrack position is in centimeters, convert to meters.
    // The finiteness gate is applied to the RAW double's narrowing, then the scale runs
    // in float - matching OpenTrackPacket.cs, which validates IsFiniteAsFloat(x) before
    // computing (float)x * CmToMeters. Scaling first in double instead let the whole band
    // 3.4e38..3.4e40 cm through here while C# rejected it, so the same hostile datagram
    // produced a 1e37 m position in native mods and no position at all in Unity mods.
    float fx, fy, fz;
    if (!FiniteFloat(px, fx) || !FiniteFloat(py, fy) || !FiniteFloat(pz, fz)) {
        return false;
    }

    position = PositionData(fx * kCmToMeters, fy * kCmToMeters, fz * kCmToMeters);
    return true;
}

bool OpenTrackPacket::TryParseAll(const void* data, size_t length, TrackingPose& pose, PositionData& position) {
    if (data == nullptr || length < kMinPacketSize) {
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);

    double px, py, pz, yaw, pitch, roll;
    std::memcpy(&px, bytes + kPosXOffset, sizeof(double));
    std::memcpy(&py, bytes + kPosYOffset, sizeof(double));
    std::memcpy(&pz, bytes + kPosZOffset, sizeof(double));
    std::memcpy(&yaw, bytes + kYawOffset, sizeof(double));
    std::memcpy(&pitch, bytes + kPitchOffset, sizeof(double));
    std::memcpy(&roll, bytes + kRollOffset, sizeof(double));

    float fyaw, fpitch, froll, fx, fy, fz;
    // See TryParsePosition: validate the raw double's narrowing, scale in float.
    if (!FiniteFloat(yaw, fyaw) || !FiniteFloat(pitch, fpitch) || !FiniteFloat(roll, froll) ||
        !FiniteFloat(px, fx) || !FiniteFloat(py, fy) || !FiniteFloat(pz, fz)) {
        return false;
    }

    pose = TrackingPose(fyaw, fpitch, froll);
    position = PositionData(fx * kCmToMeters, fy * kCmToMeters, fz * kCmToMeters);
    return true;
}

bool OpenTrackPacket::TryParseRecenterCounter(const void* data, size_t length, uint8_t& recenterCounter) {
    if (data == nullptr || length < kPacketSizeWithTrailer) {
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    if (bytes[kTrailerOffset] != 'H' || bytes[kTrailerOffset + 1] != 'C' ||
        bytes[kTrailerOffset + 2] != 'A' || bytes[kTrailerOffset + 3] != 'M') {
        return false;
    }
    if (bytes[kTrailerOffset + 4] < kTrailerVersion) {
        return false;
    }

    recenterCounter = bytes[kRecenterCounterOffset];
    return true;
}

}  // namespace cameraunlock
