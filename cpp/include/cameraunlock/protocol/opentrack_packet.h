#pragma once

#include <cstdint>
#include <cstddef>
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/data/position_data.h"

namespace cameraunlock {

/// OpenTrack packet constants and parsing utilities.
/// Packet layout: 6 doubles (48 bytes) = X, Y, Z, Yaw, Pitch, Roll.
/// The wire carries position in CENTIMETRES and rotation in degrees. TryParsePosition
/// multiplies by kCmToMeters, so PositionData comes out in metres; every limit and
/// sensitivity downstream is metric. A port that reads the wire values as metres is
/// out by 100x and saturates the limits on a few millimetres of lean.
struct OpenTrackPacket {
    /// Minimum packet size (6 doubles = 48 bytes).
    static constexpr size_t kMinPacketSize = 48;

    /// The largest angle, either way, a datagram may carry on any axis, in degrees. An angle
    /// has no reading outside one full turn, so a sender that reports 0 to 360 passes and
    /// nothing past it is a pose. A datagram over it is refused whole.
    ///
    /// The bound is not tidiness. Any finite float used to pass, and two packets with pitch
    /// at opposite ends of a float's range made the interpolator's step between them
    /// infinite: the view went to NaN and stayed there, since smoothing a NaN gives a NaN.
    /// The port is open to the network, so two datagrams from any host did it.
    static constexpr float kMaxRotationDegrees = 360.0f;

    /// The largest position, either way, a datagram may carry on any axis, in the wire's
    /// centimetres: 100 m, ten times the largest position limit a config can hold.
    static constexpr float kMaxPositionCm = 10000.0f;

    /// Byte offsets for position (doubles at offsets 0, 8, 16).
    static constexpr size_t kPosXOffset = 0;
    static constexpr size_t kPosYOffset = 8;
    static constexpr size_t kPosZOffset = 16;

    /// Byte offsets for rotation (doubles at offsets 24, 32, 40).
    static constexpr size_t kYawOffset = 24;
    static constexpr size_t kPitchOffset = 32;
    static constexpr size_t kRollOffset = 40;

    /// Optional Headcam trailer after the pose: magic "HCAM", version byte,
    /// recenter counter byte.
    static constexpr size_t kTrailerOffset = 48;
    static constexpr size_t kPacketSizeWithTrailer = 54;
    static constexpr uint8_t kTrailerVersion = 1;
    static constexpr size_t kRecenterCounterOffset = 53;

    /// Attempts to parse rotation from an OpenTrack packet.
    /// @param data Raw packet data.
    /// @param length Length of the data in bytes.
    /// @param pose Output tracking pose if successful.
    /// @return True if parsing succeeded.
    static bool TryParse(const void* data, size_t length, TrackingPose& pose);

    /// Attempts to parse position from an OpenTrack packet.
    /// @param data Raw packet data.
    /// @param length Length of the data in bytes.
    /// @param position Output position data if successful.
    /// @return True if parsing succeeded.
    static bool TryParsePosition(const void* data, size_t length, PositionData& position);

    /// Attempts to parse both rotation and position from an OpenTrack packet.
    /// @param data Raw packet data.
    /// @param length Length of the data in bytes.
    /// @param pose Output tracking pose if successful.
    /// @param position Output position data if successful.
    /// @return True if parsing succeeded.
    static bool TryParseAll(const void* data, size_t length, TrackingPose& pose, PositionData& position);

    /// Attempts to parse the recenter counter from a Headcam trailer.
    /// Packets from plain OpenTrack (48 bytes, or 56 with a frame counter)
    /// fail the magic check and return false.
    /// @param data Raw packet data.
    /// @param length Length of the data in bytes.
    /// @param recenterCounter Output counter if a version-1 (or later,
    ///        backward-compatible) trailer is present.
    /// @return True if a trailer is present.
    static bool TryParseRecenterCounter(const void* data, size_t length, uint8_t& recenterCounter);
};

}  // namespace cameraunlock
