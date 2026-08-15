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

        /// <summary>Byte offset of the optional Headcam trailer.</summary>
        public const int TrailerOffset = 48;

        /// <summary>Trailer magic bytes: "HCAM".</summary>
        public const byte TrailerMagic0 = 0x48;
        public const byte TrailerMagic1 = 0x43;
        public const byte TrailerMagic2 = 0x41;
        public const byte TrailerMagic3 = 0x4D;

        /// <summary>Current trailer version.</summary>
        public const byte TrailerVersion = 1;

        /// <summary>Packet size including the version-1 trailer (magic + version + recenter counter).</summary>
        public const int PacketSizeWithTrailer = 54;

        /// <summary>Byte offset of the recenter counter within the packet.</summary>
        public const int RecenterCounterOffset = 53;

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

            if (!IsFiniteAsFloat(yaw) || !IsFiniteAsFloat(pitch) || !IsFiniteAsFloat(roll))
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

            if (!IsFiniteAsFloat(x) || !IsFiniteAsFloat(y) || !IsFiniteAsFloat(z))
            {
                return false;
            }

            position = new PositionData((float)x * CmToMeters, (float)y * CmToMeters, (float)z * CmToMeters);
            return true;
        }

        /// <summary>
        /// True when the value is still a finite number after the narrowing to float
        /// that every parse here performs.
        ///
        /// Checking the double alone is not enough: a finite double outside float range
        /// (1e300, say) narrows to +/-Infinity, and one such packet is permanent. The
        /// smoothing stages hold their own state and lerp towards each new sample, so
        /// Infinity turns that state to NaN on the very next finite sample and every
        /// lerp after it keeps it NaN - the view stays broken until the mod is toggled
        /// off and on. The socket accepts packets from any host on the network, so this
        /// is the boundary that has to reject them.
        /// </summary>
        private static bool IsFiniteAsFloat(double value)
        {
            float narrowed = (float)value;
            return !float.IsNaN(narrowed) && !float.IsInfinity(narrowed);
        }

        /// <summary>
        /// Attempts to parse the recenter counter from a Headcam trailer.
        /// The trailer is appended after the standard 48 OpenTrack bytes:
        /// magic "HCAM", version byte, recenter counter byte. Packets from
        /// plain OpenTrack (48 bytes, or 56 with a frame counter) fail the
        /// magic check and return false.
        /// </summary>
        /// <param name="data">Raw packet data.</param>
        /// <param name="recenterCounter">Parsed recenter counter if successful.</param>
        /// <returns>True if a version-1 (or later, backward-compatible) trailer is present.</returns>
        public static bool TryParseRecenterCounter(byte[] data, out byte recenterCounter)
        {
            recenterCounter = 0;

            if (data == null || data.Length < PacketSizeWithTrailer)
            {
                return false;
            }

            if (data[TrailerOffset] != TrailerMagic0 ||
                data[TrailerOffset + 1] != TrailerMagic1 ||
                data[TrailerOffset + 2] != TrailerMagic2 ||
                data[TrailerOffset + 3] != TrailerMagic3)
            {
                return false;
            }

            if (data[TrailerOffset + 4] < TrailerVersion)
            {
                return false;
            }

            recenterCounter = data[RecenterCounterOffset];
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

            if (!IsFiniteAsFloat(yaw) || !IsFiniteAsFloat(pitch) || !IsFiniteAsFloat(roll))
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

            if (!IsFiniteAsFloat(x) || !IsFiniteAsFloat(y) || !IsFiniteAsFloat(z))
            {
                return false;
            }

            position = new PositionData((float)x * CmToMeters, (float)y * CmToMeters, (float)z * CmToMeters);
            return true;
        }
#endif
    }
}
