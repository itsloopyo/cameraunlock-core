using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Protocol
{
    public class RecenterTrailerTests : IDisposable
    {
        private const int TestPort = 14260;

        private OpenTrackReceiver? _receiver;

        public void Dispose()
        {
            _receiver?.Dispose();
        }

        [Fact]
        public void TryParseRecenterCounter_PlainOpenTrackPacket_ReturnsFalse()
        {
            byte[] packet = BuildPacket(10, 20, 30);

            Assert.False(OpenTrackPacket.TryParseRecenterCounter(packet, out _));
        }

        [Fact]
        public void TryParseRecenterCounter_OpenTrackFrameCounterPacket_ReturnsFalse()
        {
            // Some OpenTrack builds append an 8-byte frame number after the pose.
            byte[] packet = new byte[56];
            Array.Copy(BuildPacket(10, 20, 30), packet, 48);
            Array.Copy(BitConverter.GetBytes(12345.0), 0, packet, 48, 8);

            Assert.False(OpenTrackPacket.TryParseRecenterCounter(packet, out _));
        }

        [Fact]
        public void TryParseRecenterCounter_ValidTrailer_ReturnsCounter()
        {
            byte[] packet = BuildPacket(10, 20, 30, recenterCounter: 42);

            Assert.True(OpenTrackPacket.TryParseRecenterCounter(packet, out byte counter));
            Assert.Equal(42, counter);
        }

        [Fact]
        public void TryParseRecenterCounter_VersionZero_ReturnsFalse()
        {
            byte[] packet = BuildPacket(10, 20, 30, recenterCounter: 42);
            packet[OpenTrackPacket.TrailerOffset + 4] = 0;

            Assert.False(OpenTrackPacket.TryParseRecenterCounter(packet, out _));
        }

        [Fact]
        public void TryParseRecenterCounter_NewerVersion_StillParses()
        {
            byte[] packet = BuildPacket(10, 20, 30, recenterCounter: 42);
            packet[OpenTrackPacket.TrailerOffset + 4] = 2;

            Assert.True(OpenTrackPacket.TryParseRecenterCounter(packet, out byte counter));
            Assert.Equal(42, counter);
        }

        [Fact]
        public void TryParse_PacketWithTrailer_StillParsesPose()
        {
            byte[] packet = BuildPacket(10, 20, 30, recenterCounter: 7);

            Assert.True(OpenTrackPacket.TryParse(packet, out TrackingPose pose));
            Assert.Equal(10f, pose.Yaw, precision: 1);
        }

        [Fact]
        public void Receiver_NoTrailer_NoRecenterRequest()
        {
            _receiver = StartReceiver();

            Send(BuildPacket(10, 20, 30));
            Thread.Sleep(100);

            Assert.False(_receiver.TryConsumeRecenterRequest());
        }

        [Fact]
        public void Receiver_FirstTrailerSighting_TriggersRecenterRequest()
        {
            // The trailer only rides the post-press burst, so a receiver that
            // starts mid-session sees its first trailer BECAUSE of a press.
            _receiver = StartReceiver();

            Send(BuildPacket(10, 20, 30, recenterCounter: 5));
            Thread.Sleep(100);

            Assert.True(_receiver.TryConsumeRecenterRequest());
            Assert.False(_receiver.TryConsumeRecenterRequest());
        }

        [Fact]
        public void Receiver_CounterChange_TriggersRecenterRequestOnce()
        {
            _receiver = StartReceiver();

            Send(BuildPacket(10, 20, 30, recenterCounter: 5));
            Thread.Sleep(100);
            Send(BuildPacket(10, 20, 30, recenterCounter: 6));
            Thread.Sleep(100);

            Assert.True(_receiver.TryConsumeRecenterRequest());
            Assert.False(_receiver.TryConsumeRecenterRequest());
        }

        [Fact]
        public void Receiver_RepeatedCounter_DoesNotRetrigger()
        {
            _receiver = StartReceiver();

            Send(BuildPacket(10, 20, 30, recenterCounter: 5));
            Thread.Sleep(100);
            Send(BuildPacket(10, 20, 30, recenterCounter: 6));
            Thread.Sleep(100);

            Assert.True(_receiver.TryConsumeRecenterRequest());

            Send(BuildPacket(11, 21, 31, recenterCounter: 6));
            Thread.Sleep(100);

            Assert.False(_receiver.TryConsumeRecenterRequest());
        }

        [Fact]
        public void Session_CounterChange_RecentersToCurrentPose()
        {
            _receiver = StartReceiver();
            var session = new HeadTrackingSession(_receiver, new TrackingProcessor(), new PositionProcessor())
            {
                StabilizationFrames = int.MaxValue
            };

            Send(BuildPacket(45, 30, 15, recenterCounter: 1));
            Thread.Sleep(100);
            session.Update(1f / 60f);

            Send(BuildPacket(45, 30, 15, recenterCounter: 2));
            Thread.Sleep(100);
            session.Update(1f / 60f);

            Assert.Equal(0f, session.Rotation.Yaw, precision: 0);
            Assert.Equal(0f, session.Rotation.Pitch, precision: 0);
            Assert.Equal(0f, session.Rotation.Roll, precision: 0);
        }

        [Fact]
        public void Session_TrackerZeroesStreamBeforeSignaling_DoesNotDoubleSubtract()
        {
            _receiver = StartReceiver();
            var session = new HeadTrackingSession(_receiver, new TrackingProcessor(), new PositionProcessor())
            {
                StabilizationFrames = int.MaxValue
            };

            // Drifted steady state: plain packets, smoothing settles on the pose.
            Send(BuildPacket(45, 30, 15));
            Thread.Sleep(100);
            for (int i = 0; i < 30; i++)
            {
                session.Update(1f / 60f);
            }

            // CENTER press: the tracker subtracts the pose at its end and the
            // trailer rides the first packets of the zeroed stream. Folding the
            // old smoothed pose into the center as well lands the view at the
            // mirrored drift instead of zero.
            Send(BuildPacket(0, 0, 0, recenterCounter: 1));
            Thread.Sleep(100);
            session.Update(1f / 60f);

            Assert.Equal(0f, session.Rotation.Yaw, precision: 0);
            Assert.Equal(0f, session.Rotation.Pitch, precision: 0);
            Assert.Equal(0f, session.Rotation.Roll, precision: 0);
        }

        [Fact]
        public void Session_RemoteRecenter_SuppressesStabilizationAutoRecenter()
        {
            _receiver = StartReceiver();
            var session = new HeadTrackingSession(_receiver, new TrackingProcessor(), new PositionProcessor())
            {
                StabilizationFrames = 3
            };
            var logs = new System.Collections.Generic.List<string>();
            session.Log = logs.Add;

            Send(BuildPacket(45, 30, 15, recenterCounter: 1));
            Thread.Sleep(100);

            for (int i = 0; i < 10; i++)
            {
                session.Update(1f / 60f);
            }

            Assert.Contains("Recentered by tracker app", logs);
            Assert.DoesNotContain("Auto-recentered on tracker connection", logs);
        }

        private static OpenTrackReceiver StartReceiver()
        {
            var receiver = new OpenTrackReceiver();
            Assert.True(receiver.Start(TestPort));
            return receiver;
        }

        private static void Send(byte[] packet)
        {
            using (var client = new UdpClient())
            {
                client.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, TestPort));
            }
        }

        private static byte[] BuildPacket(double yaw, double pitch, double roll, byte? recenterCounter = null)
        {
            byte[] packet = new byte[recenterCounter.HasValue ? OpenTrackPacket.PacketSizeWithTrailer : OpenTrackPacket.MinPacketSize];

            Array.Copy(BitConverter.GetBytes(yaw), 0, packet, OpenTrackPacket.YawOffset, 8);
            Array.Copy(BitConverter.GetBytes(pitch), 0, packet, OpenTrackPacket.PitchOffset, 8);
            Array.Copy(BitConverter.GetBytes(roll), 0, packet, OpenTrackPacket.RollOffset, 8);

            if (recenterCounter.HasValue)
            {
                packet[OpenTrackPacket.TrailerOffset] = OpenTrackPacket.TrailerMagic0;
                packet[OpenTrackPacket.TrailerOffset + 1] = OpenTrackPacket.TrailerMagic1;
                packet[OpenTrackPacket.TrailerOffset + 2] = OpenTrackPacket.TrailerMagic2;
                packet[OpenTrackPacket.TrailerOffset + 3] = OpenTrackPacket.TrailerMagic3;
                packet[OpenTrackPacket.TrailerOffset + 4] = OpenTrackPacket.TrailerVersion;
                packet[OpenTrackPacket.RecenterCounterOffset] = recenterCounter.Value;
            }

            return packet;
        }
    }
}
