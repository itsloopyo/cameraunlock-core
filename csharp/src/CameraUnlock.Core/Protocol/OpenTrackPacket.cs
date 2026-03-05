using System;
using System.Runtime.InteropServices;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Protocol
{
    /// <summary>
    /// OpenTrack UDP packet parsing.
    /// OpenTrack sends 48 bytes of position/rotation data (6 doubles).
    /// Some versions also append 8 bytes for frame number.
    /// </summary>
    public static class OpenTrackPacket
    {
        /// <summary>Minimum packet size (6 doubles = 48 bytes).</summary>
        public const int MinPacketSize = 48;

        /// <summary>Byte offset of X position in the packet.</summary>
        public const int XOffset = 0;

        /// <summary>Byte offset of Y position in the packet.</summary>
        public const int YOffset = 8;

        /// <summary>Byte offset of Z position in the packet.</summary>
        public const int ZOffset = 16;

        /// <summary>Byte offset of yaw in the packet.</summary>
        public const int YawOffset = 24;

        /// <summary>Byte offset of pitch in the packet.</summary>
        public const int PitchOffset = 32;

        /// <summary>Byte offset of roll in the packet.</summary>
        public const int RollOffset = 40;

        /// <summary>Conversion factor from centimeters (OpenTrack default) to meters.</summary>
        public const float CmToMeters = 0.01f;

        /// <summary>
        /// Attempts to parse an OpenTrack packet.
        /// </summary>
        /// <param name="data">Raw packet data.</param>
        /// <param name="pose">Parsed tracking pose if successful.</param>
        /// <returns>True if parsing succeeded.</returns>
        public static bool TryParse(byte[] data, out TrackingPose pose)
        {
            pose = default;

            if (data == null || data.Length < MinPacketSize)
            {
                return false;
            }

            double yaw = BitConverter.ToDouble(data, YawOffset);
            double pitch = BitConverter.ToDouble(data, PitchOffset);
            double roll = BitConverter.ToDouble(data, RollOffset);

            // Validate values are not NaN or Infinity
            if (double.IsNaN(yaw) || double.IsInfinity(yaw) ||
                double.IsNaN(pitch) || double.IsInfinity(pitch) ||
                double.IsNaN(roll) || double.IsInfinity(roll))
            {
                return false;
            }

            pose = new TrackingPose((float)yaw, (float)pitch, (float)roll);
            return true;
        }

        /// <summary>
        /// Attempts to parse position data (X/Y/Z) from an OpenTrack packet.
        /// Converts from centimeters to meters.
        /// </summary>
        /// <param name="data">Raw packet data.</param>
        /// <param name="position">Parsed position data if successful.</param>
        /// <returns>True if parsing succeeded.</returns>
        public static bool TryParsePosition(byte[] data, out PositionData position)
        {
            position = default;

            if (data == null || data.Length < MinPacketSize)
            {
                return false;
            }

            double x = BitConverter.ToDouble(data, XOffset);
            double y = BitConverter.ToDouble(data, YOffset);
            double z = BitConverter.ToDouble(data, ZOffset);

            if (double.IsNaN(x) || double.IsInfinity(x) ||
                double.IsNaN(y) || double.IsInfinity(y) ||
                double.IsNaN(z) || double.IsInfinity(z))
            {
                return false;
            }

            position = new PositionData((float)x * CmToMeters, (float)y * CmToMeters, (float)z * CmToMeters);
            return true;
        }

#if NETSTANDARD2_1_OR_GREATER || NET5_0_OR_GREATER
        /// <summary>
        /// Attempts to parse an OpenTrack packet from a span.
        /// </summary>
        public static bool TryParse(ReadOnlySpan<byte> data, out TrackingPose pose)
        {
            pose = default;

            if (data.Length < MinPacketSize)
            {
                return false;
            }

            double yaw = BitConverter.ToDouble(data.Slice(YawOffset, 8));
            double pitch = BitConverter.ToDouble(data.Slice(PitchOffset, 8));
            double roll = BitConverter.ToDouble(data.Slice(RollOffset, 8));

            if (double.IsNaN(yaw) || double.IsInfinity(yaw) ||
                double.IsNaN(pitch) || double.IsInfinity(pitch) ||
                double.IsNaN(roll) || double.IsInfinity(roll))
            {
                return false;
            }

            pose = new TrackingPose((float)yaw, (float)pitch, (float)roll);
            return true;
        }

        /// <summary>
        /// Attempts to parse position data (X/Y/Z) from an OpenTrack packet span.
        /// Converts from centimeters to meters.
        /// </summary>
        public static bool TryParsePosition(ReadOnlySpan<byte> data, out PositionData position)
        {
            position = default;

            if (data.Length < MinPacketSize)
            {
                return false;
            }

            double x = BitConverter.ToDouble(data.Slice(XOffset, 8));
            double y = BitConverter.ToDouble(data.Slice(YOffset, 8));
            double z = BitConverter.ToDouble(data.Slice(ZOffset, 8));

            if (double.IsNaN(x) || double.IsInfinity(x) ||
                double.IsNaN(y) || double.IsInfinity(y) ||
                double.IsNaN(z) || double.IsInfinity(z))
            {
                return false;
            }

            position = new PositionData((float)x * CmToMeters, (float)y * CmToMeters, (float)z * CmToMeters);
            return true;
        }
#endif
    }
}
