using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace CameraUnlock.Core.Tests
{
    // Sends to a receiver the way a tracker app does: from one socket, every packet queued
    // in order, then the last one again every 5 ms until the next is queued or the stream
    // is paused. The receiver's 500 ms freshness window then stays met however long the
    // test thread is held up between a packet arriving and its Update.
    internal sealed class TrackerStream : IDisposable
    {
        private const int RepeatMs = 5;

        private readonly UdpClient _socket = new UdpClient();
        private readonly IPEndPoint _receiver;
        private readonly Queue<byte[]> _queued = new Queue<byte[]>();
        private readonly Thread _thread;
        private byte[]? _repeat;
        private volatile bool _running = true;
        private volatile Exception? _failure;

        public TrackerStream(int receiverPort)
        {
            _receiver = new IPEndPoint(IPAddress.Loopback, receiverPort);
            _thread = new Thread(Run) { Name = "TrackerStream", IsBackground = true };
            _thread.Start();
        }

        public void Send(byte[] packet)
        {
            ThrowIfFailed();
            lock (_queued) _queued.Enqueue(packet);
        }

        public void Pause()
        {
            ThrowIfFailed();
            lock (_queued)
            {
                _queued.Clear();
                _repeat = null;
            }
        }

        // An exception left on this thread would end the test host with no test named, so
        // it is handed to the test thread, which throws it from the next call.
        private void Run()
        {
            try
            {
                while (_running)
                {
                    lock (_queued)
                    {
                        while (_queued.Count > 0)
                        {
                            _repeat = _queued.Dequeue();
                            _socket.Send(_repeat, _repeat.Length, _receiver);
                        }
                        if (_repeat != null) _socket.Send(_repeat, _repeat.Length, _receiver);
                    }
                    Thread.Sleep(RepeatMs);
                }
            }
            catch (Exception e)
            {
                _failure = e;
            }
        }

        private void ThrowIfFailed()
        {
            if (_failure != null) throw new InvalidOperationException("the tracker stream stopped sending", _failure);
        }

        public void Dispose()
        {
            _running = false;
            _thread.Join();
            _socket.Close();
            ThrowIfFailed();
        }
    }
}
