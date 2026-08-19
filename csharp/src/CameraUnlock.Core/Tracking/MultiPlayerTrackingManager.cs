using System;
using System.Text;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tracking
{
    /// <summary>
    /// Manages one <see cref="HeadTrackingSession"/> per local player for split-screen /
    /// shared-screen multiplayer games. Each player gets their own UDP receiver on their
    /// own port, with independent recentering and tracking-loss handling.
    ///
    /// Framework-agnostic: call <see cref="Start"/> once, <see cref="Update"/> every frame,
    /// then read per-player poses via <see cref="GetSession"/> / <see cref="HasPose"/>.
    /// All sessions share a single <see cref="TrackingMode"/>.
    /// </summary>
    public sealed class MultiPlayerTrackingManager : IDisposable
    {
        private readonly int[] _ports;
        private readonly OpenTrackReceiver[] _receivers;
        private readonly TrackingProcessor[] _processors;
        private readonly PositionProcessor[] _positionProcessors;
        private readonly HeadTrackingSession[] _sessions;
        private readonly bool[] _hasPose;

        private TrackingMode _mode = TrackingMode.RotationAndPosition;
        private bool _started;
        private bool _disposed;

        // Owned here for the same reason the sessions own theirs: the values live inside
        // PositionSettings, which is assigned wholesale, so without an owner an
        // ApplyPositionSettings would reset whatever ApplySmoothing had just put there.
        private float _localSmoothing = SmoothingUtils.DefaultLocalSmoothing;
        private float _remoteSmoothing = SmoothingUtils.DefaultRemoteSmoothing;

        /// <summary>
        /// Optional logging callback. Receiver bind/retry messages and session
        /// auto-recenter notifications are prefixed with the player number.
        /// </summary>
#if NULLABLE_ENABLED
        public Action<string>? Log { get; set; }
#else
        public Action<string> Log { get; set; }
#endif

        /// <param name="ports">One UDP port per local player, in player order.</param>
        public MultiPlayerTrackingManager(int[] ports)
        {
            if (ports == null) throw new ArgumentNullException("ports");
            if (ports.Length == 0) throw new ArgumentException("At least one port is required", "ports");

            // Duplicates rejected at the boundary, alongside the null and empty checks.
            // Only the first receiver can bind a shared port; the rest fail and spin a
            // retry thread every 500ms FOREVER, because the port will never be released
            // by the sibling holding it. The log then reads "port in use" immediately
            // followed by "listening on port" for the same player.
            for (int a = 0; a < ports.Length; a++)
            {
                for (int b = a + 1; b < ports.Length; b++)
                {
                    if (ports[a] == ports[b])
                    {
                        throw new ArgumentException(
                            "Duplicate UDP port " + ports[a] + " (players " + (a + 1) + " and " + (b + 1) +
                            "): each player needs its own port.", "ports");
                    }
                }
            }

            _ports = (int[])ports.Clone();
            _receivers = new OpenTrackReceiver[_ports.Length];
            _processors = new TrackingProcessor[_ports.Length];
            _positionProcessors = new PositionProcessor[_ports.Length];
            _sessions = new HeadTrackingSession[_ports.Length];
            _hasPose = new bool[_ports.Length];

            for (int i = 0; i < _ports.Length; i++)
            {
                int playerNumber = i + 1;
                _receivers[i] = new OpenTrackReceiver
                {
                    Log = msg => Log?.Invoke("Player " + playerNumber + ": " + msg)
                };
                _processors[i] = new TrackingProcessor();
                _positionProcessors[i] = new PositionProcessor();
                _sessions[i] = new HeadTrackingSession(_receivers[i], _processors[i], _positionProcessors[i])
                {
                    Log = msg => Log?.Invoke("Player " + playerNumber + ": " + msg)
                };
            }
        }

        /// <summary>Number of player slots.</summary>
        public int PlayerCount => _ports.Length;

        /// <summary>
        /// Shared tracking mode applied to every player's session.
        /// </summary>
        public TrackingMode Mode
        {
            get { return _mode; }
            set
            {
                _mode = value;
                for (int i = 0; i < _sessions.Length; i++)
                {
                    _sessions[i].Mode = value;
                }
            }
        }

        /// <summary>The per-player tracking session.</summary>
        public HeadTrackingSession GetSession(int playerIndex)
        {
            return _sessions[playerIndex];
        }

        /// <summary>Whether the player's receiver is currently bound and receiving data.</summary>
        public bool IsReceiving(int playerIndex)
        {
            return _receivers[playerIndex].IsReceiving;
        }

        /// <summary>
        /// Whether the player's session produced a pose (fresh or held) in the latest
        /// <see cref="Update"/>.
        /// </summary>
        public bool HasPose(int playerIndex)
        {
            return _hasPose[playerIndex];
        }

        /// <summary>True when any player's receiver is receiving data.</summary>
        public bool IsAnyReceiving
        {
            get
            {
                for (int i = 0; i < _receivers.Length; i++)
                {
                    if (_receivers[i].IsReceiving) return true;
                }
                return false;
            }
        }

        /// <summary>
        /// Starts every player's receiver. A failed bind is not fatal: the receiver's
        /// internal retry loop keeps trying, and the slot goes live once it succeeds.
        /// </summary>
        public void Start()
        {
            if (_disposed) throw new ObjectDisposedException("MultiPlayerTrackingManager");
            if (_started) return;
            _started = true;

            for (int i = 0; i < _receivers.Length; i++)
            {
                // Logged on the result, not unconditionally: a failed bind puts the
                // receiver into its retry loop, and claiming it is listening there
                // contradicts the failure message the receiver itself just logged.
                if (_receivers[i].Start(_ports[i]))
                {
                    Log?.Invoke("Player " + (i + 1) + " receiver listening on port " + _ports[i]);
                }
                else
                {
                    Log?.Invoke("Player " + (i + 1) + " receiver could not bind port " + _ports[i] +
                                " yet - retrying in the background");
                }
            }
        }

        /// <summary>
        /// Runs every player's pipeline for this frame. Call once per frame.
        ///
        /// Each session reads the connection flag from ITS OWN receiver and feeds its own
        /// processors, so there is deliberately no shared flag here: split-screen players
        /// bind separate ports and can legitimately differ, one on a local tracker and one
        /// on a phone over WiFi, and a shared flag would hand one of them the wrong
        /// smoothing parameter.
        /// </summary>
        public void Update(float deltaTime)
        {
            for (int i = 0; i < _sessions.Length; i++)
            {
                _hasPose[i] = _sessions[i].Update(deltaTime);
            }
        }

        /// <summary>
        /// Whether the given player's latest <see cref="Update"/> saw a remote connection.
        /// Per player, never shared.
        /// </summary>
        public bool IsRemoteConnection(int playerIndex)
        {
            return _sessions[playerIndex].IsRemoteConnection;
        }

        /// <summary>
        /// Recenters every player that is currently receiving data.
        /// </summary>
        public void Recenter()
        {
            for (int i = 0; i < _sessions.Length; i++)
            {
                if (_receivers[i].IsReceiving)
                {
                    _sessions[i].Recenter();
                    Log?.Invoke("Player " + (i + 1) + " view recentered");
                }
            }
        }

        /// <summary>
        /// Resets transient pipeline state for every player (interpolators, smoothing,
        /// held poses) while preserving center offsets.
        /// </summary>
        public void Reset()
        {
            for (int i = 0; i < _sessions.Length; i++)
            {
                _sessions[i].Reset();
                _hasPose[i] = false;
            }
        }

        /// <summary>
        /// Advances every session to the next tracking mode and returns it.
        /// </summary>
        public TrackingMode CycleMode()
        {
            Mode = (TrackingMode)(((int)_mode + 1) % 3);
            return _mode;
        }

        /// <summary>Applies sensitivity settings to every player's processor.</summary>
        public void ApplySensitivity(SensitivitySettings sensitivity)
        {
            for (int i = 0; i < _processors.Length; i++)
            {
                _processors[i].Sensitivity = sensitivity;
            }
        }

        /// <summary>
        /// The local smoothing value currently applied to every player, as actually held by
        /// the sessions.
        /// </summary>
        public float LocalSmoothing
        {
            get { return _localSmoothing; }
        }

        /// <summary>
        /// The remote smoothing value currently applied to every player, as actually held by
        /// the sessions.
        /// </summary>
        public float RemoteSmoothing
        {
            get { return _remoteSmoothing; }
        }

        /// <summary>
        /// Applies both smoothing parameters to every player's session, covering rotation
        /// and position. Which one is used is decided per connection from the packet source
        /// address, re-evaluated by the session every frame.
        /// </summary>
        public void ApplySmoothing(float localSmoothing, float remoteSmoothing)
        {
            _localSmoothing = localSmoothing;
            _remoteSmoothing = remoteSmoothing;

            for (int i = 0; i < _sessions.Length; i++)
            {
                _sessions[i].LocalSmoothing = localSmoothing;
                _sessions[i].RemoteSmoothing = remoteSmoothing;
            }
        }

        /// <summary>
        /// Applies position settings to every player's session.
        ///
        /// Order does not matter: the smoothing fields carried by <paramref name="settings"/>
        /// are discarded and replaced with the values from <see cref="ApplySmoothing"/>, so
        /// the two calls compose in either direction. Change smoothing through
        /// <see cref="ApplySmoothing"/>, never by building it into
        /// <paramref name="settings"/>. The connection flag is unaffected - it lives on the
        /// processor, not in the settings struct.
        /// </summary>
        public void ApplyPositionSettings(PositionSettings settings)
        {
            for (int i = 0; i < _sessions.Length; i++)
            {
                _sessions[i].PositionSettings = settings;
            }
        }

        /// <summary>
        /// Applies the post-connection stabilization frame count to every session. Only
        /// consulted while <see cref="HeadTrackingSession.AutoRecenterOnConnect"/> is on.
        /// </summary>
        public void ApplyStabilizationFrames(int frames)
        {
            for (int i = 0; i < _sessions.Length; i++)
            {
                _sessions[i].StabilizationFrames = frames;
            }
        }

        /// <summary>
        /// Applies <see cref="HeadTrackingSession.AutoRecenterOnConnect"/> to every session.
        /// Off by default there, and the reasoning applies per player unchanged.
        /// </summary>
        public void ApplyAutoRecenterOnConnect(bool enabled)
        {
            for (int i = 0; i < _sessions.Length; i++)
            {
                _sessions[i].AutoRecenterOnConnect = enabled;
            }
        }

        /// <summary>
        /// Human-readable connection summary, e.g. "Players 1, 3 connected".
        /// </summary>
        public string GetConnectionStatus()
        {
            var sb = new StringBuilder();
            int count = 0;
            for (int i = 0; i < _receivers.Length; i++)
            {
                if (_receivers[i].IsReceiving)
                {
                    if (count > 0) sb.Append(", ");
                    sb.Append(i + 1);
                    count++;
                }
            }
            return count > 0 ? "Players " + sb + " connected" : "No players connected";
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            for (int i = 0; i < _receivers.Length; i++)
            {
                _receivers[i].Dispose();
            }
        }
    }
}
