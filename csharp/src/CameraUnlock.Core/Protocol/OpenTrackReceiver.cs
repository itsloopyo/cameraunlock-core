using System;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Protocol
{
    /// <summary>
    /// Receives OpenTrack UDP packets on port 4242.
    /// Thread-safe lock-free implementation using volatile reads.
    /// Optionally applies coordinate transformation at receive time.
    /// </summary>
    public sealed class OpenTrackReceiver : ITrackingDataSource, IDisposable
    {
        public const int DefaultPort = 4242;
        private const int ReceiveTimeoutMs = 100;
        private const int DisconnectThreshold = 50;

        /// <summary>
        /// Default max age in milliseconds for data to be considered fresh.
        /// </summary>
        public const int DefaultMaxDataAgeMs = 500;

#if NULLABLE_ENABLED
        private UdpClient? _udpClient;
        private Thread? _receiveThread;
#else
        private UdpClient _udpClient;
        private Thread _receiveThread;
#endif
        private volatile bool _isRunning;
        private volatile bool _isConnected;
        private int _consecutiveTimeouts;
        private bool _disposed;

        private volatile float _rotationPitch;
        private volatile float _rotationYaw;
        private volatile float _rotationRoll;
        private long _timestampTicks;
        private volatile bool _isRemoteConnection;

        // Recenter offset
        private float _offsetYaw;
        private float _offsetPitch;
        private float _offsetRoll;
        private readonly object _offsetLock = new object();

        // Optional coordinate transformer
#if NULLABLE_ENABLED
        private readonly CoordinateTransformer? _transformer;
#else
        private readonly CoordinateTransformer _transformer;
#endif

        /// <summary>
        /// Creates a receiver with no coordinate transformation.
        /// </summary>
        public OpenTrackReceiver() : this(null)
        {
        }

        /// <summary>
        /// Creates a receiver with optional coordinate transformation.
        /// </summary>
        /// <param name="transformer">Optional transformer to apply to received data. Pass null for no transformation.</param>
#if NULLABLE_ENABLED
        public OpenTrackReceiver(CoordinateTransformer? transformer)
#else
        public OpenTrackReceiver(CoordinateTransformer transformer)
#endif
        {
            _transformer = transformer;
        }

        /// <summary>
        /// Whether the receiver has a coordinate transformer configured.
        /// </summary>
        public bool HasTransformer => _transformer != null;

        public bool IsReceiving => _isConnected;
        public bool IsFailed { get; private set; }
        public bool IsRemoteConnection => _isRemoteConnection;

        public bool Start(int port = DefaultPort)
        {
            if (_disposed) return false;
            if (_isRunning) return true;
            IsFailed = false;

            try
            {
                _udpClient = new UdpClient(new IPEndPoint(IPAddress.Any, port));
                _udpClient.Client.ReceiveTimeout = ReceiveTimeoutMs;
            }
            catch (SocketException)
            {
                IsFailed = true;
                return false;
            }

            _isRunning = true;
            _receiveThread = new Thread(ReceiveLoop)
            {
                Name = "CameraUnlock-OpenTrackReceiver",
                IsBackground = true
            };
            _receiveThread.Start();
            return true;
        }

        public void Stop()
        {
            _isRunning = false;

            var thread = _receiveThread;
            _receiveThread = null;
            if (thread != null)
                thread.Join(1000);

            var client = _udpClient;
            _udpClient = null;
            if (client != null)
            {
                try { client.Close(); }
                catch (SocketException) { }
            }

            _isConnected = false;
        }

        /// <summary>
        /// Gets raw pose with timestamp, without any offset applied.
        /// Used by PoseInterpolator to detect new vs stale samples.
        /// </summary>
        public TrackingPose GetRawPose()
        {
            float yaw = _rotationYaw;
            float pitch = _rotationPitch;
            float roll = _rotationRoll;
            long timestamp = Interlocked.Read(ref _timestampTicks);
            return new TrackingPose(yaw, pitch, roll, timestamp);
        }

        /// <summary>
        /// Gets raw rotation values without offset or transformation applied.
        /// </summary>
        public void GetRawRotation(out float yaw, out float pitch, out float roll)
        {
            pitch = _rotationPitch;
            yaw = _rotationYaw;
            roll = _rotationRoll;
        }

        /// <summary>
        /// Gets the latest pose with recenter offset applied (but no transformation).
        /// Use GetLatestPoseTransformed() if you want coordinate transformation applied.
        /// </summary>
        public TrackingPose GetLatestPose()
        {
            float yaw = _rotationYaw;
            float pitch = _rotationPitch;
            float roll = _rotationRoll;
            long timestamp = Interlocked.Read(ref _timestampTicks);

            // Use explicit Monitor calls for old Mono compatibility
            Monitor.Enter(_offsetLock);
            try
            {
                yaw -= _offsetYaw;
                pitch -= _offsetPitch;
                roll -= _offsetRoll;
            }
            finally
            {
                Monitor.Exit(_offsetLock);
            }

            return new TrackingPose(yaw, pitch, roll, timestamp);
        }

        /// <summary>
        /// Gets the latest pose with both recenter offset and coordinate transformation applied.
        /// If no transformer is configured, behaves identically to GetLatestPose().
        /// </summary>
        public TrackingPose GetLatestPoseTransformed()
        {
            TrackingPose pose = GetLatestPose();

            if (_transformer != null)
            {
                pose = _transformer.Transform(pose);
            }

            return pose;
        }

        /// <summary>
        /// Checks if the latest data is fresh (received within maxAgeMs milliseconds).
        /// More reliable than IsReceiving for detecting stale data.
        /// </summary>
        /// <param name="maxAgeMs">Maximum age in milliseconds (default 500).</param>
        /// <returns>True if data was received recently.</returns>
        public bool IsDataFresh(int maxAgeMs = DefaultMaxDataAgeMs)
        {
            long timestamp = Interlocked.Read(ref _timestampTicks);
            if (timestamp == 0) return false;

            long elapsed = Stopwatch.GetTimestamp() - timestamp;
            double elapsedMs = elapsed * 1000.0 / Stopwatch.Frequency;
            return elapsedMs < maxAgeMs;
        }

        public void Recenter()
        {
            // Use explicit Monitor calls for old Mono compatibility
            Monitor.Enter(_offsetLock);
            try
            {
                _offsetYaw = _rotationYaw;
                _offsetPitch = _rotationPitch;
                _offsetRoll = _rotationRoll;
            }
            finally
            {
                Monitor.Exit(_offsetLock);
            }
        }

        /// <summary>
        /// Resets the recenter offset to zero.
        /// </summary>
        public void ResetOffset()
        {
            // Use explicit Monitor calls for old Mono compatibility
            Monitor.Enter(_offsetLock);
            try
            {
                _offsetYaw = 0;
                _offsetPitch = 0;
                _offsetRoll = 0;
            }
            finally
            {
                Monitor.Exit(_offsetLock);
            }
        }

        private void ReceiveLoop()
        {
            var remoteEndpoint = new IPEndPoint(IPAddress.Any, 0);

            while (_isRunning)
            {
                try
                {
                    if (_udpClient == null) break;

                    byte[] data = _udpClient.Receive(ref remoteEndpoint);

                    if (data.Length >= OpenTrackPacket.MinPacketSize)
                    {
                        if (OpenTrackPacket.TryParse(data, out TrackingPose parsed))
                        {
                            _rotationYaw = parsed.Yaw;
                            _rotationPitch = parsed.Pitch;
                            _rotationRoll = parsed.Roll;
                            Interlocked.Exchange(ref _timestampTicks, Stopwatch.GetTimestamp());
                        }

                        _isRemoteConnection = !IPAddress.IsLoopback(remoteEndpoint.Address);
                        _isConnected = true;
                        _consecutiveTimeouts = 0;
                    }
                }
                catch (SocketException ex)
                {
                    if (ex.SocketErrorCode == SocketError.TimedOut)
                    {
                        _consecutiveTimeouts++;
                        if (_consecutiveTimeouts >= DisconnectThreshold)
                        {
                            _isConnected = false;
                        }
                    }
                    else if (ex.SocketErrorCode == SocketError.Interrupted)
                    {
                        break;
                    }
                }
                catch (ObjectDisposedException)
                {
                    break;
                }
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            Stop();
        }
    }
}
