using System;
using System.Net;
using System.Net.Sockets;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Protocol
{
    public class RecenterTrailerTests : IDisposable
    {
        // One sender for every packet, as a tracker app sends: the receiver follows one source.
        private static readonly UdpClient Sender = new UdpClient();

        private OpenTrackReceiver? _receiver;
        private TrackerStream? _tracker;

        public void Dispose()
        {
            _tracker?.Dispose();
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

            SendAndWait(BuildPacket(10, 20, 30));

            Assert.False(_receiver.TryConsumeRecenterRequest());
        }

        [Fact]
        public void Receiver_Trailer_NeverRaisesARecenterRequest()
        {
            // Headcam owns centring: it zeroes its own output on CENTER, and the
            // pipeline's centre is identity, so the zeroed stream is already
            // correct. An older app that still sends the trailer is ignored
            // rather than acted on.
            _receiver = StartReceiver();

            SendAndWait(BuildPacket(10, 20, 30, recenterCounter: 1));
            Assert.False(_receiver.TryConsumeRecenterRequest());

            SendAndWait(BuildPacket(10, 20, 30, recenterCounter: 2));
            Assert.False(_receiver.TryConsumeRecenterRequest());

            SendAndWait(BuildPacket(10, 20, 30, recenterCounter: 2));
            Assert.False(_receiver.TryConsumeRecenterRequest());
        }

        [Fact]
        public void Receiver_Trailer_StillDeliversThePose()
        {
            // Parsing stays: a trailered packet is a normal 54-byte datagram and
            // its pose must still reach the consumer.
            _receiver = StartReceiver();

            SendAndWait(BuildPacket(10, 20, 30, recenterCounter: 1));

            TrackingPose pose = _receiver.GetLatestPose();
            Assert.Equal(10f, pose.Yaw, precision: 3);
            Assert.Equal(20f, pose.Pitch, precision: 3);
            Assert.Equal(30f, pose.Roll, precision: 3);
        }

        [Fact]
        public void Session_Trailer_DoesNotMoveTheView()
        {
            _receiver = StartReceiver();
            var session = new HeadTrackingSession(_receiver, new TrackingProcessor(), new PositionProcessor());

            StreamAndWait(45, 30, 15);
            for (int i = 0; i < 60; i++) session.Update(1f / 60f);
            float before = session.Rotation.Yaw;

            // Moved 1 cm so its arrival can be seen; the rotation is the same.
            StreamAndWait(45, 30, 15, recenterCounter: 1, xCm: 1.0);
            for (int i = 0; i < 60; i++) session.Update(1f / 60f);

            Assert.Equal(before, session.Rotation.Yaw, precision: 2);
        }

        [Fact]
        public void Session_Trailer_LeavesAHotkeyCentreAlone()
        {
            // The trailer used to clear a hotkey-set centre. It no longer does, so
            // a centre the player chose survives whatever the tracker signals.
            _receiver = StartReceiver();
            var session = new HeadTrackingSession(_receiver, new TrackingProcessor(), new PositionProcessor());

            StreamAndWait(40, 0, 0);
            for (int i = 0; i < 60; i++) session.Update(1f / 60f);
            session.Recenter();
            for (int i = 0; i < 60; i++) session.Update(1f / 60f);
            Assert.True(System.Math.Abs(session.Rotation.Yaw) < 1f);

            StreamAndWait(60, 0, 0, recenterCounter: 1);
            for (int i = 0; i < 60; i++) session.Update(1f / 60f);

            Assert.True(System.Math.Abs(session.Rotation.Yaw - 20f) < 1f,
                $"the hotkey centre should still be 40, got a residual of {session.Rotation.Yaw}");
        }

        [Fact]
        public void Session_TrackingLossGap_DoesNotRearmAutoRecenter()
        {
            _receiver = StartReceiver();
            var session = new HeadTrackingSession(_receiver, new TrackingProcessor(), new PositionProcessor())
            {
                StabilizationFrames = 3,
                AutoRecenterOnConnect = true
            };
            var logs = new System.Collections.Generic.List<string>();
            session.Log = logs.Add;

            StreamAndWait(10, 5, 0);
            for (int i = 0; i < 5; i++)
            {
                session.Update(1f / 60f);
            }
            Assert.Single(logs, "Auto-recentered on tracker connection");

            // Face lost: the tracker stops sending and data goes stale.
            _tracker!.Pause();
            Eventually.True(() => !_receiver.IsDataFresh(), "the data never went stale after the tracker stopped sending");
            session.Update(1f / 60f);
            Assert.True(session.IsHolding);

            // Face re-acquired mid-motion: resuming packets must not re-capture
            // the center - that decision belongs to the tracker app (trailer).
            // Two samples: the receiver holds the first of a jump this size (PoseJumpGate).
            _tracker.Send(BuildPacket(39.95, 20, 10));
            StreamAndWait(40, 20, 10);
            for (int i = 0; i < 5; i++)
            {
                session.Update(1f / 60f);
            }
            Assert.Single(logs, "Auto-recentered on tracker connection");
        }

        [Fact]
        public void RemoteRecenter_NoRequest_ReturnsFalse()
        {
            _receiver = StartReceiver();

            SendAndWait(BuildPacket(10, 20, 30));

            Assert.False(RemoteRecenter.TryConsume(_receiver, new TrackingProcessor()));
        }

        [Fact]
        public void RemoteRecenter_Trailer_NeverConsumesARequest()
        {
            // RemoteRecenter still pushes the connection flag onto the processors
            // every frame, which is its other job and the reason hand-wired
            // pipelines call it. It just never reports a press any more.
            _receiver = StartReceiver();
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();

            SendAndWait(BuildPacket(0, 0, 0, recenterCounter: 1));

            Assert.False(RemoteRecenter.TryConsume(
                _receiver, processor, new PoseInterpolator(), positionProcessor, new PositionInterpolator()));
            Assert.False(processor.IsRemoteConnection);
            Assert.False(positionProcessor.IsRemoteConnection);
        }

        private static OpenTrackReceiver StartReceiver()
        {
            var receiver = new OpenTrackReceiver();
            Assert.True(receiver.Start(0));
            return receiver;
        }

        private void Send(byte[] packet)
        {
            lock (Sender) Sender.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, _receiver!.BoundPort));
        }

        private void SendAndWait(byte[] packet)
        {
            long before = _receiver!.GetRawPose().TimestampTicks;
            Send(packet);
            Eventually.True(() => _receiver.GetRawPose().TimestampTicks != before, "the packet sent was never published");
        }

        // For the session tests, whose Update runs need the data kept fresh as a tracker
        // keeps it. Waits on the values, as a repeat of the previous packet also moves
        // the receiver's timestamp.
        private void StreamAndWait(double yaw, double pitch, double roll, byte? recenterCounter = null, double xCm = 0.0)
        {
            _tracker ??= new TrackerStream(_receiver!.BoundPort);
            _tracker.Send(BuildPacket(yaw, pitch, roll, recenterCounter, x: xCm));
            Eventually.True(() =>
            {
                TrackingPose pose = _receiver!.GetRawPose();
                return pose.Yaw == (float)yaw && pose.Pitch == (float)pitch && pose.Roll == (float)roll &&
                    System.Math.Abs(_receiver.GetLatestPosition().X - (float)xCm / 100f) < 0.0001f;
            }, () => $"the receiver never reported ({yaw}, {pitch}, {roll}) at x {xCm} cm, last saw {_receiver!.GetRawPose()}");
        }

        private static byte[] BuildPacket(double yaw, double pitch, double roll, byte? recenterCounter = null,
            double x = 0.0, double y = 0.0, double z = 0.0)
        {
            byte[] packet = new byte[recenterCounter.HasValue ? OpenTrackPacket.PacketSizeWithTrailer : OpenTrackPacket.MinPacketSize];

            Array.Copy(BitConverter.GetBytes(x), 0, packet, 0, 8);
            Array.Copy(BitConverter.GetBytes(y), 0, packet, 8, 8);
            Array.Copy(BitConverter.GetBytes(z), 0, packet, 16, 8);
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
