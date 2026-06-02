// Null-argument constructor tests intentionally pass null literals
#pragma warning disable CS8625

using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    public class HeadTrackingSessionTests : IDisposable
    {
        private const int TestPort = 14245;
        private const float FrameTime = 1f / 60f;

        private readonly OpenTrackReceiver _receiver;
        private readonly TrackingProcessor _processor;
        private readonly PositionProcessor _positionProcessor;
        private readonly HeadTrackingSession _session;

        public HeadTrackingSessionTests()
        {
            _receiver = new OpenTrackReceiver();
            _processor = new TrackingProcessor();
            _positionProcessor = new PositionProcessor();
            _session = new HeadTrackingSession(_receiver, _processor, _positionProcessor)
            {
                // Tests drive single packets; don't let the post-connection auto-recenter
                // zero out the pose under us unless a test asks for it.
                StabilizationFrames = int.MaxValue
            };
        }

        public void Dispose()
        {
            _receiver.Dispose();
        }

        [Fact]
        public void Constructor_NullReceiver_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new HeadTrackingSession(null, _processor, _positionProcessor));
        }

        [Fact]
        public void Constructor_NullProcessor_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new HeadTrackingSession(_receiver, null, _positionProcessor));
        }

        [Fact]
        public void Constructor_NullPositionProcessor_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new HeadTrackingSession(_receiver, _processor, null));
        }

        [Fact]
        public void Mode_Default_IsRotationAndPosition()
        {
            Assert.Equal(TrackingMode.RotationAndPosition, _session.Mode);
            Assert.True(_session.RotationActive);
            Assert.True(_session.PositionActive);
        }

        [Fact]
        public void CycleMode_AdvancesThroughAllModesAndWraps()
        {
            Assert.Equal(TrackingMode.RotationOnly, _session.CycleMode());
            Assert.Equal(TrackingMode.PositionOnly, _session.CycleMode());
            Assert.Equal(TrackingMode.RotationAndPosition, _session.CycleMode());
        }

        [Fact]
        public void RotationOnly_DisablesPosition()
        {
            _session.Mode = TrackingMode.RotationOnly;

            Assert.True(_session.RotationActive);
            Assert.False(_session.PositionActive);
        }

        [Fact]
        public void PositionOnly_DisablesRotation()
        {
            _session.Mode = TrackingMode.PositionOnly;

            Assert.False(_session.RotationActive);
            Assert.True(_session.PositionActive);
        }

        [Fact]
        public void Update_NoDataEverReceived_ReturnsFalse()
        {
            bool result = _session.Update(FrameTime);

            Assert.False(result);
            Assert.False(_session.IsHolding);
        }

        [Fact]
        public void Update_WithFreshData_ReturnsTrueAndProducesRotation()
        {
            StartReceiverAndSend(yaw: 30.0, pitch: 10.0, roll: 5.0);

            bool result = _session.Update(FrameTime);

            Assert.True(result);
            Assert.False(_session.IsHolding);
            // Pipeline applies interpolation and smoothing, so just verify the sign and
            // that the pose is non-trivial.
            Assert.True(_session.Rotation.Yaw > 0f);
            Assert.True(_session.Rotation.Pitch > 0f);
        }

        [Fact]
        public void Update_RotationOnlyMode_PositionOffsetIsZero()
        {
            _session.Mode = TrackingMode.RotationOnly;
            StartReceiverAndSend(yaw: 30.0, pitch: 0.0, roll: 0.0);

            _session.Update(FrameTime);

            Assert.Equal(Vec3.Zero, _session.PositionOffset);
        }

        [Fact]
        public void Update_AfterDataLoss_HoldsLastPose()
        {
            StartReceiverAndSend(yaw: 30.0, pitch: 10.0, roll: 0.0);

            // Process fresh data over several frames to settle smoothing.
            for (int i = 0; i < 30; i++)
            {
                _session.Update(FrameTime);
            }
            var heldYaw = _session.Rotation.Yaw;

            // Simulate tracking loss by waiting out the 500ms freshness window.
            Thread.Sleep(600);

            bool result = _session.Update(FrameTime);

            Assert.True(result);
            Assert.True(_session.IsHolding);
            Assert.Equal(heldYaw, _session.Rotation.Yaw);
        }

        [Fact]
        public void Update_AutoRecenters_AfterStabilizationFrames()
        {
            _session.StabilizationFrames = 5;
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 40.0, pitch: 0.0, roll: 0.0);

            // Run past the stabilization window; the recenter fires on frame 5 and the
            // pose should collapse toward zero afterwards.
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw) < 5f,
                $"Expected yaw near 0 after auto-recenter, got {_session.Rotation.Yaw}");
        }

        [Fact]
        public void Reset_ClearsHeldPose()
        {
            StartReceiverAndSend(yaw: 30.0, pitch: 0.0, roll: 0.0);
            _session.Update(FrameTime);

            _session.Reset();
            // Wait out freshness so Update has neither fresh data nor a held pose.
            Thread.Sleep(600);

            bool result = _session.Update(FrameTime);

            Assert.False(result);
        }

        private void StartReceiverAndSend(double yaw, double pitch, double roll)
        {
            Assert.True(_receiver.Start(TestPort), $"Failed to bind test UDP port {TestPort}");
            SendTestPacket(TestPort, yaw, pitch, roll);
            WaitForData();
        }

        private void WaitForData()
        {
            for (int i = 0; i < 50; i++)
            {
                if (_receiver.IsDataFresh()) return;
                Thread.Sleep(10);
            }
            Assert.True(false, "Receiver never reported fresh data");
        }

        /// <summary>
        /// Sends a test OpenTrack packet (48 bytes, 6 doubles: x, y, z, yaw, pitch, roll).
        /// </summary>
        private static void SendTestPacket(int port, double yaw, double pitch, double roll)
        {
            byte[] packet = new byte[48];

            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 0, 8);
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 8, 8);
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 16, 8);
            Array.Copy(BitConverter.GetBytes(yaw), 0, packet, 24, 8);
            Array.Copy(BitConverter.GetBytes(pitch), 0, packet, 32, 8);
            Array.Copy(BitConverter.GetBytes(roll), 0, packet, 40, 8);

            using (var client = new UdpClient())
            {
                client.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, port));
            }
        }
    }
}
