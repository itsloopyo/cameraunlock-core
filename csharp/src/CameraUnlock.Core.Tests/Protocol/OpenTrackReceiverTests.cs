using System;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tests.Protocol
{
    public class OpenTrackReceiverTests : IDisposable
    {
        // One sender for every packet, as a tracker app sends: the receiver follows one source.
        private static readonly UdpClient Sender = new UdpClient();

        private OpenTrackReceiver? _receiver;

        public void Dispose()
        {
            _receiver?.Dispose();
        }

        // The C++ suite asserts this exact address set against cameraunlock::IsRemoteAddress
        // in TestLoopbackClassification. The two languages must agree: the same sender
        // getting LocalSmoothing in a C# mod and RemoteSmoothing in a C++ mod is invisible
        // to the user and to every existing test.
        [Theory]
        [InlineData("127.0.0.1", false)]
        [InlineData("127.0.0.2", false)]
        [InlineData("127.1.2.3", false)]
        [InlineData("127.255.255.254", false)]
        [InlineData("192.168.1.50", true)]
        [InlineData("10.0.0.7", true)]
        [InlineData("8.8.8.8", true)]
        [InlineData("128.0.0.1", true)]
        [InlineData("126.255.255.255", true)]
        public void IsRemoteAddress_TreatsWholeLoopbackBlockAsLocal(string address, bool expectedRemote)
        {
            Assert.Equal(expectedRemote, OpenTrackReceiver.IsRemoteAddress(IPAddress.Parse(address)));
        }

        [Fact]
        public void Constructor_WithNoTransformer_HasTransformerIsFalse()
        {
            _receiver = new OpenTrackReceiver();

            Assert.False(_receiver.HasTransformer);
        }

        [Fact]
        public void Constructor_WithTransformer_HasTransformerIsTrue()
        {
            var transformer = new CoordinateTransformer();
            _receiver = new OpenTrackReceiver(transformer);

            Assert.True(_receiver.HasTransformer);
        }

        [Fact]
        public void Start_BindsToPort_ReturnsTrue()
        {
            _receiver = new OpenTrackReceiver();

            bool result = _receiver.Start(0);

            Assert.True(result);
        }

        [Fact]
        public void Start_WhenPortInUse_ReturnsFalse()
        {
            // First receiver binds successfully
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            // Second receiver should fail
            using (var receiver2 = new OpenTrackReceiver())
            {
                bool result = receiver2.Start(_receiver.BoundPort);
                Assert.False(result);
                Assert.True(receiver2.IsFailed);
            }
        }

        [Fact]
        public void Start_WhenAlreadyRunning_ReturnsTrue()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            bool result = _receiver.Start(0);

            Assert.True(result);
        }

        [Fact]
        public void IsReceiving_BeforeAnyData_ReturnsFalse()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            Assert.False(_receiver.IsReceiving);
        }

        [Fact]
        public void GetLatestPose_WhenNoData_ReturnsZeroPose()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            TrackingPose pose = _receiver.GetLatestPose();

            Assert.Equal(0f, pose.Yaw);
            Assert.Equal(0f, pose.Pitch);
            Assert.Equal(0f, pose.Roll);
        }

        [Fact]
        public void GetRawRotation_WhenNoData_ReturnsZeros()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            _receiver.GetRawRotation(out float yaw, out float pitch, out float roll);

            Assert.Equal(0f, yaw);
            Assert.Equal(0f, pitch);
            Assert.Equal(0f, roll);
        }

        [Fact]
        public void IsDataFresh_WhenNoData_ReturnsFalse()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            Assert.False(_receiver.IsDataFresh());
        }

        [Fact]
        public void Recenter_DoesNotThrow_WhenNoData()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            // Should not throw
            _receiver.Recenter();
        }

        [Fact]
        public void ResetOffset_DoesNotThrow_WhenNoData()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            // Should not throw
            _receiver.ResetOffset();
        }

        [Fact]
        public void IsFailed_BeforeStart_IsFalse()
        {
            _receiver = new OpenTrackReceiver();

            Assert.False(_receiver.IsFailed);
        }

        [Fact]
        public void IsRemoteConnection_WhenNoData_ReturnsFalse()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            Assert.False(_receiver.IsRemoteConnection);
        }

        // Integration test - sends actual UDP data
        [Fact]
        public void ReceivesUdpPacket_UpdatesRotation()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacketAndWait(10.0, 20.0, 30.0);

            _receiver.GetRawRotation(out float yaw, out float pitch, out float roll);

            Assert.Equal(10.0f, yaw, precision: 1);
            Assert.Equal(20.0f, pitch, precision: 1);
            Assert.Equal(30.0f, roll, precision: 1);
        }

        [Fact]
        public void ReceivesUdpPacket_SetsIsReceiving()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacket(10.0, 20.0, 30.0);

            Eventually.True(() => _receiver.IsReceiving, "IsReceiving did not go true after a packet was sent");
        }

        // Short datagrams never time the receive out, so they once held IsReceiving true on
        // the last pose for as long as they kept coming.
        [Fact]
        public void IsReceiving_GoesFalse_WhenOnlyJunkArrivesAfterTheTracker()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacket(10.0, 20.0, 30.0);
            Eventually.True(() => _receiver.IsReceiving, "the tracker's packet never arrived");

            var junk = new byte[8];
            var stopwatch = Stopwatch.StartNew();
            while (_receiver.IsReceiving && stopwatch.ElapsedMilliseconds < 10000)
            {
                Sender.Send(junk, junk.Length, new IPEndPoint(IPAddress.Loopback, _receiver.BoundPort));
                Thread.Sleep(20);
            }

            Assert.False(_receiver.IsReceiving);
            Assert.InRange(stopwatch.ElapsedMilliseconds, 4000, 9000);
        }

        // A receive that waits on the socket's own timeout loses the datagram that arrives
        // as the timeout runs out, so each packet here is sent to land around that moment.
        // Against that receiver this failed 5 runs in 12.
        [Fact]
        public void APacketArrivingAsTheReceiveWaitRunsOut_IsNotLost()
        {
            _receiver = new OpenTrackReceiver();
            Assert.True(_receiver.Start(0));
            SendTestPacketAndWait(1.0, 0.0, 0.0);

            // Thread.Sleep(1) can last a whole 15.6 ms timer tick, so the last 20 ms spin.
            long spinTicks = Stopwatch.Frequency / 50;
            for (int tenths = 990; tenths <= 1020; tenths++)
            {
                double gapMs = tenths / 10.0;
                long last = _receiver.GetRawPose().TimestampTicks;
                long sendAt = last + (long)(gapMs * Stopwatch.Frequency / 1000.0);
                while (Stopwatch.GetTimestamp() < sendAt - spinTicks) Thread.Sleep(1);
                while (Stopwatch.GetTimestamp() < sendAt) { }

                SendTestPacket(1.0, 0.0, 0.0);

                Eventually.True(() => _receiver.GetRawPose().TimestampTicks != last,
                    $"the packet sent {gapMs:F1} ms after the one before it never arrived");
            }
        }

        [Fact]
        public void ReceivesUdpPacket_DataIsFresh()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacket(10.0, 20.0, 30.0);

            Eventually.True(() => _receiver.IsDataFresh(), "IsDataFresh did not go true after a packet was sent");
        }

        [Fact]
        public void Recenter_SetsOffsetToCurrentRotation()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacketAndWait(45.0, 30.0, 15.0);

            _receiver.Recenter();

            TrackingPose pose = _receiver.GetLatestPose();

            // After recenter, pose should be near zero
            Assert.Equal(0f, pose.Yaw, precision: 1);
            Assert.Equal(0f, pose.Pitch, precision: 1);
            Assert.Equal(0f, pose.Roll, precision: 1);
        }

        [Fact]
        public void ResetOffset_ClearsRecenterOffset()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacketAndWait(45.0, 30.0, 15.0);

            _receiver.Recenter();
            _receiver.ResetOffset();

            TrackingPose pose = _receiver.GetLatestPose();

            // After reset, pose should be back to raw values
            Assert.Equal(45f, pose.Yaw, precision: 1);
            Assert.Equal(30f, pose.Pitch, precision: 1);
            Assert.Equal(15f, pose.Roll, precision: 1);
        }

        [Fact]
        public void GetLatestPoseTransformed_WithTransformer_AppliesTransformation()
        {
            var transformer = CoordinateTransformer.CreateFromInversions(true, false, false);
            _receiver = new OpenTrackReceiver(transformer);
            _receiver.Start(0);

            SendTestPacketAndWait(45.0, 30.0, 15.0);

#pragma warning disable CS0618 // Compatibility coverage for the deprecated public entry point.
            TrackingPose transformed = _receiver.GetLatestPoseTransformed();
#pragma warning restore CS0618

            Assert.Equal(-45f, transformed.Yaw, precision: 1);
            Assert.Equal(30f, transformed.Pitch, precision: 1);
            Assert.Equal(15f, transformed.Roll, precision: 1);
        }

        [Fact]
        public void GetLatestPoseTransformed_WithoutTransformer_ReturnsNormalPose()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(0);

            SendTestPacketAndWait(45.0, 30.0, 15.0);

            TrackingPose pose = _receiver.GetLatestPose();
#pragma warning disable CS0618 // Compatibility coverage for the deprecated public entry point.
            TrackingPose transformed = _receiver.GetLatestPoseTransformed();
#pragma warning restore CS0618

            Assert.Equal(pose.Yaw, transformed.Yaw, precision: 1);
            Assert.Equal(pose.Pitch, transformed.Pitch, precision: 1);
            Assert.Equal(pose.Roll, transformed.Roll, precision: 1);
        }

        [Fact]
        public void ASecondSource_IsIgnoredUntilTheFirstGoesSilent()
        {
            _receiver = new OpenTrackReceiver();
            var secondSourceIgnored = new ManualResetEventSlim();
            _receiver.Log = message =>
            {
                if (message.StartsWith("a SECOND tracker source", StringComparison.Ordinal)) secondSourceIgnored.Set();
            };
            Assert.True(_receiver.Start(0));
            SendTestPacketAndWait(10, 0, 0);
            long firstSourceTicks = _receiver.GetRawPose().TimestampTicks;
            Assert.Equal(10f, _receiver.GetLatestPose().Yaw, precision: 3);

            using (var other = new UdpClient())
            {
                byte[] packet = new byte[48];
                Array.Copy(BitConverter.GetBytes(12.0), 0, packet, 24, 8);
                other.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, _receiver.BoundPort));
                Eventually.True(() => secondSourceIgnored.IsSet, "the receiver never logged that it ignored the second source");
                Assert.Equal(10f, _receiver.GetLatestPose().Yaw, precision: 3);

                // Sleep can return a timer tick early, so the silence is measured from
                // the first source's packet and given a margin past the handover.
                long handoverAt = firstSourceTicks + (OpenTrackReceiver.SourceHandoverMs + 100) * Stopwatch.Frequency / 1000;
                while (Stopwatch.GetTimestamp() < handoverAt) Thread.Sleep(10);
                other.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, _receiver.BoundPort));
                Eventually.True(() => _receiver.GetLatestPose().Yaw == 12f,
                    () => "the second source's pose was not taken after the first went silent, yaw stayed " + _receiver.GetLatestPose().Yaw);
            }
        }

        private void SendTestPacketAndWait(double yaw, double pitch, double roll)
        {
            long before = _receiver!.GetRawPose().TimestampTicks;
            SendTestPacket(yaw, pitch, roll);
            Eventually.True(() => _receiver.GetRawPose().TimestampTicks != before,
                $"the packet ({yaw}, {pitch}, {roll}) was never published");
        }

        /// <summary>
        /// Sends a test OpenTrack packet to the receiver.
        /// OpenTrack packet format: 48 bytes (6 doubles)
        /// [0-7]: X position, [8-15]: Y position, [16-23]: Z position
        /// [24-31]: Yaw, [32-39]: Pitch, [40-47]: Roll
        /// </summary>
        private void SendTestPacket(double yaw, double pitch, double roll)
        {
            byte[] packet = new byte[48];

            // Position (not used, set to 0)
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 0, 8);  // X
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 8, 8);  // Y
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 16, 8); // Z

            // Rotation
            Array.Copy(BitConverter.GetBytes(yaw), 0, packet, 24, 8);
            Array.Copy(BitConverter.GetBytes(pitch), 0, packet, 32, 8);
            Array.Copy(BitConverter.GetBytes(roll), 0, packet, 40, 8);

            lock (Sender) Sender.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, _receiver!.BoundPort));
        }
    }
}
