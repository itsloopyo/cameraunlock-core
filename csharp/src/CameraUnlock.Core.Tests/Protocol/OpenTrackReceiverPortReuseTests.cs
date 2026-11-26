using System;
using System.Net;
using System.Net.Sockets;
using Xunit;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tests.Protocol
{
    // These bind a port the OS handed out and then freed. Run beside the other UDP tests,
    // a Start(0) there could be handed the same port in between.
    [CollectionDefinition(Name, DisableParallelization = true)]
    public class PortReuseCollection
    {
        public const string Name = "Port reuse";
    }

    [Collection(PortReuseCollection.Name)]
    public class OpenTrackReceiverPortReuseTests : IDisposable
    {
        private OpenTrackReceiver? _receiver;

        public void Dispose()
        {
            _receiver?.Dispose();
        }

        [Fact]
        public void Start_WhenPortInUse_BindsSoonAfterItIsReleased()
        {
            var holder = new OpenTrackReceiver();
            Assert.True(holder.Start(0));
            int port = holder.BoundPort;

            _receiver = new OpenTrackReceiver();
            Assert.False(_receiver.Start(port));
            Assert.True(_receiver.IsFailed);

            holder.Dispose();

            Eventually.True(() => !_receiver.IsFailed, "the receiver did not claim the port after it was released");

            // Proves it is really listening, not just flagged as bound.
            using (var sender = new UdpClient())
            {
                byte[] packet = new byte[OpenTrackPacket.MinPacketSize];
                sender.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, port));
            }
            Eventually.True(() => _receiver.IsReceiving, "the receiver bound the port but the packet sent to it never arrived");
        }

        [Fact]
        public void Stop_ReleasesPort()
        {
            _receiver = new OpenTrackReceiver();
            Assert.True(_receiver.Start(0));
            int port = _receiver.BoundPort;
            _receiver.Stop();

            using (var receiver2 = new OpenTrackReceiver())
            {
                Assert.True(receiver2.Start(port), "port " + port + " could not be bound again after Stop");
            }
        }

        [Fact]
        public void Dispose_StopsReceiver()
        {
            _receiver = new OpenTrackReceiver();
            Assert.True(_receiver.Start(0));
            int port = _receiver.BoundPort;

            _receiver.Dispose();
            _receiver = null;

            using (var receiver2 = new OpenTrackReceiver())
            {
                Assert.True(receiver2.Start(port), "port " + port + " could not be bound again after Dispose");
            }
        }
    }
}
