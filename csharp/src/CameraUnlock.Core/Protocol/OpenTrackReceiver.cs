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
        // Polled fast rather than lazily: the common case is a second game still
        // holding the port, and the user expects tracking to come alive as soon as
        // they close it. A failed bind is cheap, so the wait is sub-second.
        private const int RetryIntervalMs = 500;
        private const int RetryLogIntervalMs = 30000;
        private const int RetrySleepSliceMs = 100;

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

        // Port retry state
        private volatile bool _retrying;
#if NULLABLE_ENABLED
        private Thread? _retryThread;
#else
        private Thread _retryThread;
#endif
        private int _port;

        private volatile float _rotationPitch;
        private volatile float _rotationYaw;
        private volatile float _rotationRoll;
        private long _timestampTicks;
        private volatile bool _isRemoteConnection;

        // Position data (meters)
        private volatile float _positionX;
        private volatile float _positionY;
        private volatile float _positionZ;

        // Remote recenter (Headcam trailer). The trailer only rides a short
        // burst of packets right after a CENTER press (steady-state packets
        // must stay 48 bytes for plain OpenTrack), so the first sighting is
        // itself a press and must trigger -- latching it silently would
        // swallow the first press after every receiver start.
        private bool _hasRecenterCounter;
        private byte _lastRecenterCounter;
        private int _recenterRequested;

        // Recenter offset
        private float _offsetYaw;
        private float _offsetPitch;
        private float _offsetRoll;
        private float _offsetX;
        private float _offsetY;
        private float _offsetZ;
        private readonly object _offsetLock = new object();

        // Serialises Stop() against RetryLoop publishing a newly-bound socket, so a retry
        // that succeeds concurrently with a shutdown cannot leave _isRunning set.
        private readonly object _lifecycleLock = new object();

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

        /// <summary>
        /// Classifies a packet source as remote or same-machine. Loopback is the whole
        /// 127.0.0.0/8 block, not just 127.0.0.1: pointing OpenTrack at 127.0.0.2 to keep
        /// several local streams apart is a real pattern.
        ///
        /// Must stay in agreement with cameraunlock::IsRemoteAddress on the C++ side. The
        /// two languages disagreeing means the same sender gets LocalSmoothing in one mod
        /// and RemoteSmoothing in another, with nothing to catch it.
        /// </summary>
        public static bool IsRemoteAddress(IPAddress address)
        {
            return !IPAddress.IsLoopback(address);
        }

        /// <summary>
        /// Optional logging callback for bind failures and retry messages.
        /// </summary>
#if NULLABLE_ENABLED
        public Action<string>? Log { get; set; }
#else
        public Action<string> Log { get; set; }
#endif

        public bool Start(int port = DefaultPort)
        {
            if (_disposed) return false;
            // Already-running is only success if it is running on the port asked for.
            // Reporting true for a different port left a mod that rebinds its UDP port
            // from config listening on the old one with IsFailed clear and every status
            // surface claiming healthy.
            if (_isRunning) return port == _port;
            if (_retrying) return false;
            IsFailed = false;
            _port = port;
            _hasRecenterCounter = false;
            _isRemoteConnection = false;
            Interlocked.Exchange(ref _recenterRequested, 0);
            Interlocked.Exchange(ref _timestampTicks, 0L);

            UdpClient client;
            try
            {
                // IPv4 only. Binding dual-stack would deliver a same-machine IPv4 sender
                // as ::ffff:127.0.0.1, which IPAddress.IsLoopback does not recognise, so
                // every local user would be silently reclassified as remote and handed
                // RemoteSmoothing. Widen the classifier to unmap first if this ever changes.
                client = new UdpClient(new IPEndPoint(IPAddress.Any, port));
                client.Client.ReceiveTimeout = ReceiveTimeoutMs;
            }
            catch (SocketException)
            {
                IsFailed = true;
                Log?.Invoke(string.Format("UDP port {0} is in use by another process (another game still running?) -- polling every {1}ms and will start listening as soon as it frees up", port, RetryIntervalMs));
                StartRetryLoop();
                return false;
            }

            // Published under the same lock RetryLoop uses, for the same reason: a Stop()
            // landing between the socket assignment and the _isRunning write would close
            // the socket and then see no thread to join, leaving _isRunning true with no
            // receiver - and every later Start() short-circuits on "already running" while
            // tracking is dead. The bind stays outside the lock so a blocking failure
            // path cannot hold it.
            lock (_lifecycleLock)
            {
                _udpClient = client;
                _isRunning = true;
                _receiveThread = new Thread(ReceiveLoop)
                {
                    Name = "CameraUnlock-OpenTrackReceiver",
                    IsBackground = true
                };
                _receiveThread.Start();
            }
            return true;
        }

        private void StartRetryLoop()
        {
            _retrying = true;
            _retryThread = new Thread(RetryLoop)
            {
                Name = "CameraUnlock-PortRetry",
                IsBackground = true
            };
            _retryThread.Start();
        }

        private void RetryLoop()
        {
            int attemptsPerLog = RetryLogIntervalMs / RetryIntervalMs;
            int attempts = 0;

            while (_retrying && !_disposed)
            {
                // Sleep in short increments so Stop()/Dispose() can interrupt quickly
                for (int i = 0; i < RetryIntervalMs / RetrySleepSliceMs; i++)
                {
                    if (!_retrying || _disposed) return;
                    Thread.Sleep(RetrySleepSliceMs);
                }
                if (!_retrying || _disposed) return;

                attempts++;

                try
                {
                    var client = new UdpClient(new IPEndPoint(IPAddress.Any, _port));
                    client.Client.ReceiveTimeout = ReceiveTimeoutMs;

                    if (!_retrying || _disposed)
                    {
                        try { client.Close(); }
                        catch (SocketException) { }
                        return;
                    }

                    // Stop() can land between the check above and publication here. It
                    // clears _isRunning and joins whatever _receiveThread it can see, so a
                    // receiver published afterwards survives with _isRunning true: the next
                    // Start() then short-circuits on "already running" and tracking is dead
                    // for the process lifetime with IsFailed clear. Publish under the lock
                    // Stop() takes, and unwind if it already ran.
                    lock (_lifecycleLock)
                    {
                        if (!_retrying || _disposed)
                        {
                            try { client.Close(); }
                            catch (SocketException) { }
                            return;
                        }

                        _udpClient = client;
                        IsFailed = false;
                        _retrying = false;

                        _isRunning = true;
                        _receiveThread = new Thread(ReceiveLoop)
                        {
                            Name = "CameraUnlock-OpenTrackReceiver",
                            IsBackground = true
                        };
                        _receiveThread.Start();
                    }

                    Log?.Invoke(string.Format("UDP port {0} freed up - now listening (waited {1}s)", _port, attempts * RetryIntervalMs / 1000));
                    return;
                }
                catch (SocketException)
                {
                    if (attempts % attemptsPerLog == 0)
                    {
                        Log?.Invoke(string.Format("Still waiting for UDP port {0} ({1}s elapsed)", _port, attempts * RetryIntervalMs / 1000));
                    }
                }
            }
        }

        public void Stop()
        {
            // Only the flag writes are serialised against RetryLoop's publication. The
            // joins below must stay outside the lock: RetryLoop takes it, and joining a
            // thread that is blocked on a lock this one holds is a deadlock.
            lock (_lifecycleLock)
            {
                _isRunning = false;
                _retrying = false;
            }

            var retryThread = _retryThread;
            _retryThread = null;
            if (retryThread != null)
                retryThread.Join(1000);

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
            // Locality belongs to the connection that is ending. Left set, the next
            // session reports the previous sender's locality until its first packet.
            _isRemoteConnection = false;
            // Freshness is derived purely from this timestamp, so leaving the dead
            // session's value makes IsDataFresh() report true on a closed socket. A
            // stop/start toggle inside the freshness window then runs the whole
            // per-frame pipeline - including the auto-recenter - against no stream.
            Interlocked.Exchange(ref _timestampTicks, 0L);
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
        /// Gets the latest position with recenter offset applied.
        /// </summary>
        public PositionData GetLatestPosition()
        {
            float x = _positionX;
            float y = _positionY;
            float z = _positionZ;
            long timestamp = Interlocked.Read(ref _timestampTicks);

            Monitor.Enter(_offsetLock);
            try
            {
                x -= _offsetX;
                y -= _offsetY;
                z -= _offsetZ;
            }
            finally
            {
                Monitor.Exit(_offsetLock);
            }

            return new PositionData(x, y, z, timestamp);
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
                _offsetX = _positionX;
                _offsetY = _positionY;
                _offsetZ = _positionZ;
            }
            finally
            {
                Monitor.Exit(_offsetLock);
            }
        }

        /// <summary>
        /// Returns true once per recenter request signaled by the tracker app
        /// through the packet trailer (e.g. the user pressing CENTER in Headcam).
        /// The caller routes it into its own recenter path - the receiver never
        /// applies it, because session-based pipelines center in the processor
        /// and centering at both levels double-subtracts.
        /// </summary>
        public bool TryConsumeRecenterRequest()
        {
            return Interlocked.Exchange(ref _recenterRequested, 0) == 1;
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
                _offsetX = 0;
                _offsetY = 0;
                _offsetZ = 0;
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
                    // Snapshot the field: Stop() nulls it, and re-reading it for the
                    // Receive call would dereference null on the receive thread - an
                    // unhandled background-thread exception, which kills the process on
                    // .NET Framework rather than just ending tracking.
                    var client = _udpClient;
                    if (client == null) break;

                    byte[] data = client.Receive(ref remoteEndpoint);

                    if (data.Length >= OpenTrackPacket.MinPacketSize)
                    {
                        bool poseValid = OpenTrackPacket.TryParse(data, out TrackingPose parsed);

                        if (poseValid)
                        {
                            _rotationYaw = parsed.Yaw;
                            _rotationPitch = parsed.Pitch;
                            _rotationRoll = parsed.Roll;

                            // Classify only packets that survived validation. A malformed
                            // datagram is discarded, so letting it set the flag would let
                            // any LAN host flip a local user onto RemoteSmoothing by
                            // sending 48 bytes of garbage at the port.
                            //
                            _isRemoteConnection = IsRemoteAddress(remoteEndpoint.Address);

                            if (OpenTrackPacket.TryParsePosition(data, out PositionData positionParsed))
                            {
                                _positionX = positionParsed.X;
                                _positionY = positionParsed.Y;
                                _positionZ = positionParsed.Z;
                            }

                            // Published last of the pose data, and with a full fence, so a
                            // reader that observes this timestamp is guaranteed to see the
                            // rotation AND position that came with it. Publishing it right
                            // after the rotation let GetLatestPosition pair a new timestamp
                            // with the previous packet's position, which the interpolator
                            // reads as a new sample and latches one sample behind.
                            Interlocked.Exchange(ref _timestampTicks, Stopwatch.GetTimestamp());

                            // Same-connection liveness, for validated traffic only. Set for
                            // any 48-byte datagram, a LAN host streaming garbage would hold
                            // IsReceiving true and pin _consecutiveTimeouts at 0 forever,
                            // so the tracker-restart re-arm below could never fire.
                            _isConnected = true;
                            _consecutiveTimeouts = 0;
                        }

                        // The trailer only means anything alongside the zeroed pose it rides
                        // with. Honouring it on a packet whose pose failed validation centres
                        // on the PREVIOUS pose - the pre-press drift the tracker just cleared -
                        // which is the double-subtract failure reached through another door.
                        if (poseValid && OpenTrackPacket.TryParseRecenterCounter(data, out byte recenterCounter))
                        {
                            if (!_hasRecenterCounter || recenterCounter != _lastRecenterCounter)
                            {
                                // Exchanged after the pose and timestamp so a consumer that
                                // sees the request is guaranteed to read the pose that carried it.
                                Interlocked.Exchange(ref _recenterRequested, 1);
                            }
                            _lastRecenterCounter = recenterCounter;
                            _hasRecenterCounter = true;
                        }
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
                            // Re-arm first-sighting: the tracker app restarting
                            // resets its counter to zero, so a value latched from
                            // the old session would swallow the first CENTER
                            // press of the new one.
                            _hasRecenterCounter = false;
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
