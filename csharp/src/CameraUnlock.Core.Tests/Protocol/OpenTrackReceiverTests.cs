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
        private OpenTrackReceiver? _receiver;
        private const int TestPort = 14242; // Use non-default port to avoid conflicts

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

            bool result = _receiver.Start(TestPort);

            Assert.True(result);
        }

        [Fact]
        public void Start_WhenPortInUse_ReturnsFalse()
        {
            // First receiver binds successfully
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            // Second receiver should fail
            using (var receiver2 = new OpenTrackReceiver())
            {
                bool result = receiver2.Start(TestPort);
                Assert.False(result);
                Assert.True(receiver2.IsFailed);
            }
        }

        [Fact]
        public void Start_WhenPortInUse_BindsSoonAfterItIsReleased()
        {
            const int port = 14243;

            var holder = new OpenTrackReceiver();
            Assert.True(holder.Start(port));

            _receiver = new OpenTrackReceiver();
            Assert.False(_receiver.Start(port));
            Assert.True(_receiver.IsFailed);

            holder.Dispose();

            Assert.True(WaitFor(() => !_receiver.IsFailed, 3000), "receiver did not claim the port after it was released");

            // Proves it is really listening, not just flagged as bound.
            SendTestPacket(port, 10.0, 20.0, 30.0);
            Assert.True(WaitFor(() => _receiver.IsReceiving, 1000), "receiver bound the port but received no data");
        }

        private static bool WaitFor(Func<bool> condition, int timeoutMs)
        {
            var elapsed = Stopwatch.StartNew();
            while (elapsed.ElapsedMilliseconds < timeoutMs)
            {
                if (condition()) return true;
                Thread.Sleep(25);
            }
            return condition();
        }

        [Fact]
        public void Start_WhenAlreadyRunning_ReturnsTrue()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            bool result = _receiver.Start(TestPort);

            Assert.True(result);
        }

        [Fact]
        public void Stop_ReleasesPort()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);
            _receiver.Stop();

            // Should be able to bind again after stop
            using (var receiver2 = new OpenTrackReceiver())
            {
                bool result = receiver2.Start(TestPort);
                Assert.True(result);
            }
        }

        [Fact]
        public void IsReceiving_BeforeAnyData_ReturnsFalse()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            Assert.False(_receiver.IsReceiving);
        }

        [Fact]
        public void GetLatestPose_WhenNoData_ReturnsZeroPose()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            TrackingPose pose = _receiver.GetLatestPose();

            Assert.Equal(0f, pose.Yaw);
            Assert.Equal(0f, pose.Pitch);
            Assert.Equal(0f, pose.Roll);
        }

        [Fact]
        public void GetRawRotation_WhenNoData_ReturnsZeros()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            _receiver.GetRawRotation(out float yaw, out float pitch, out float roll);

            Assert.Equal(0f, yaw);
            Assert.Equal(0f, pitch);
            Assert.Equal(0f, roll);
        }

        [Fact]
        public void IsDataFresh_WhenNoData_ReturnsFalse()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            Assert.False(_receiver.IsDataFresh());
        }

        [Fact]
        public void Dispose_StopsReceiver()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            _receiver.Dispose();

            // Should be able to bind again after dispose
            using (var receiver2 = new OpenTrackReceiver())
            {
                bool result = receiver2.Start(TestPort);
                Assert.True(result);
            }

            _receiver = null; // Prevent double dispose in test cleanup
        }

        [Fact]
        public void Recenter_DoesNotThrow_WhenNoData()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            // Should not throw
            _receiver.Recenter();
        }

        [Fact]
        public void ResetOffset_DoesNotThrow_WhenNoData()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

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
            _receiver.Start(TestPort);

            Assert.False(_receiver.IsRemoteConnection);
        }

        // Integration test - sends actual UDP data
        [Fact]
        public void ReceivesUdpPacket_UpdatesRotation()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            // Send a test packet
            SendTestPacket(TestPort, 10.0, 20.0, 30.0);

            // Wait for packet to be received
            Thread.Sleep(100);

            _receiver.GetRawRotation(out float yaw, out float pitch, out float roll);

            Assert.Equal(10.0f, yaw, precision: 1);
            Assert.Equal(20.0f, pitch, precision: 1);
            Assert.Equal(30.0f, roll, precision: 1);
        }

        [Fact]
        public void ReceivesUdpPacket_SetsIsReceiving()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            SendTestPacket(TestPort, 10.0, 20.0, 30.0);
            Thread.Sleep(100);

            Assert.True(_receiver.IsReceiving);
        }

        [Fact]
        public void ReceivesUdpPacket_DataIsFresh()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            SendTestPacket(TestPort, 10.0, 20.0, 30.0);
            Thread.Sleep(100);

            Assert.True(_receiver.IsDataFresh());
        }

        [Fact]
        public void Recenter_SetsOffsetToCurrentRotation()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            SendTestPacket(TestPort, 45.0, 30.0, 15.0);
            Thread.Sleep(100);

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
            _receiver.Start(TestPort);

            SendTestPacket(TestPort, 45.0, 30.0, 15.0);
            Thread.Sleep(100);

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
            _receiver.Start(TestPort);

            SendTestPacket(TestPort, 45.0, 30.0, 15.0);
            Thread.Sleep(100);

            TrackingPose transformed = _receiver.GetLatestPoseTransformed();

            Assert.Equal(-45f, transformed.Yaw, precision: 1);
            Assert.Equal(30f, transformed.Pitch, precision: 1);
            Assert.Equal(15f, transformed.Roll, precision: 1);
        }

        [Fact]
        public void GetLatestPoseTransformed_WithoutTransformer_ReturnsNormalPose()
        {
            _receiver = new OpenTrackReceiver();
            _receiver.Start(TestPort);

            SendTestPacket(TestPort, 45.0, 30.0, 15.0);
            Thread.Sleep(100);

            TrackingPose pose = _receiver.GetLatestPose();
            TrackingPose transformed = _receiver.GetLatestPoseTransformed();

            Assert.Equal(pose.Yaw, transformed.Yaw, precision: 1);
            Assert.Equal(pose.Pitch, transformed.Pitch, precision: 1);
            Assert.Equal(pose.Roll, transformed.Roll, precision: 1);
        }

        /// <summary>
        /// Sends a test OpenTrack packet to the specified port.
        /// OpenTrack packet format: 48 bytes (6 doubles)
        /// [0-7]: X position, [8-15]: Y position, [16-23]: Z position
        /// [24-31]: Yaw, [32-39]: Pitch, [40-47]: Roll
        /// </summary>
        private static void SendTestPacket(int port, double yaw, double pitch, double roll)
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

            using (var client = new UdpClient())
            {
                client.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, port));
            }
        }
    }
}
