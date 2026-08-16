using System;
using CameraUnlock.Core.Aim;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tracking
{
    /// <summary>
    /// Framework-agnostic static head tracking core.
    /// Manages receiver, processor, and provides processed tracking data.
    ///
    /// This is a static class that survives Unity lifecycle events.
    /// Game-specific code should:
    /// 1. Call Initialize() with config
    /// 2. Call Update() each frame with deltaTime
    /// 3. Apply GetProcessedPose() to camera rotation
    /// 4. Use GetAimScreenOffset() for decoupled aim UI
    /// </summary>
    public static class StaticHeadTrackingCore
    {
        // Core components
#if NULLABLE_ENABLED
        private static OpenTrackReceiver? _receiver;
        private static TrackingProcessor? _processor;
        private static IHeadTrackingConfig? _config;
#else
        private static OpenTrackReceiver _receiver;
        private static TrackingProcessor _processor;
        private static IHeadTrackingConfig _config;
#endif

        // State
        private static bool _initialized;
        private static bool _enabled = true;
        private static bool _hasAutoRecentered;

        // Smoothing parameters, mirrored here so the getters keep working before
        // Initialize() and after Shutdown(), when there is no processor to read from.
        //
        // Initialize() OVERWRITES both from the config it is handed. Setting them before
        // Initialize() therefore has no lasting effect - config is the source of truth at
        // startup. Set them AFTER Initialize() to override what the config supplied.
        private static float _localSmoothing = SmoothingUtils.DefaultLocalSmoothing;
        private static float _remoteSmoothing = SmoothingUtils.DefaultRemoteSmoothing;

        // Logging
#if NULLABLE_ENABLED
        private static Action<string>? _log;
#else
        private static Action<string> _log;
#endif

        /// <summary>
        /// Whether tracking is currently enabled.
        /// </summary>
        public static bool IsEnabled
        {
            get { return _enabled; }
            set
            {
                if (_enabled != value)
                {
                    _enabled = value;
                    _log?.Invoke(_enabled ? "Tracking enabled" : "Tracking disabled");
                    if (!_enabled)
                    {
                        Reset();
                    }
                }
            }
        }

        /// <summary>
        /// Whether tracking data is being received.
        /// </summary>
        public static bool IsReceiving
        {
            get { return _receiver != null && _receiver.IsReceiving; }
        }

        /// <summary>
        /// Whether the connection is from a remote source (non-localhost).
        /// </summary>
        public static bool IsRemoteConnection
        {
            get { return _receiver != null && _receiver.IsRemoteConnection; }
        }

        /// <summary>
        /// User smoothing applied when the tracker runs on this machine (loopback).
        /// 0 = frame interpolation only, 1 = heavy smoothing. No floor is applied.
        ///
        /// <see cref="Initialize"/> overwrites this from the supplied config, so set it
        /// after Initialize() rather than before.
        /// </summary>
        public static float LocalSmoothing
        {
            get { return _localSmoothing; }
            set
            {
                _localSmoothing = value;
                if (_processor != null)
                {
                    _processor.LocalSmoothing = value;
                }
            }
        }

        /// <summary>
        /// User smoothing applied when the tracker is a remote device on the network.
        /// 0 = frame interpolation only, 1 = heavy smoothing. No floor is applied.
        ///
        /// <see cref="Initialize"/> overwrites this from the supplied config, so set it
        /// after Initialize() rather than before.
        /// </summary>
        public static float RemoteSmoothing
        {
            get { return _remoteSmoothing; }
            set
            {
                _remoteSmoothing = value;
                if (_processor != null)
                {
                    _processor.RemoteSmoothing = value;
                }
            }
        }

        /// <summary>
        /// Whether the core has been initialized.
        /// </summary>
        public static bool IsInitialized
        {
            get { return _initialized; }
        }

        /// <summary>
        /// Initializes the head tracking core.
        /// </summary>
        /// <param name="config">Configuration to use.</param>
        /// <param name="log">Optional logging action.</param>
#if NULLABLE_ENABLED
        public static void Initialize(IHeadTrackingConfig config, Action<string>? log = null)
#else
        public static void Initialize(IHeadTrackingConfig config, Action<string> log = null)
#endif
        {
            if (_initialized)
            {
                log?.Invoke("StaticHeadTrackingCore already initialized");
                return;
            }

            _config = config;
            _log = log;
            _initialized = true;

            _log?.Invoke("StaticHeadTrackingCore initializing...");

            // Create tracking processor with config settings
            _localSmoothing = config.LocalSmoothing;
            _remoteSmoothing = config.RemoteSmoothing;

            _processor = new TrackingProcessor
            {
                Sensitivity = config.Sensitivity,
                LocalSmoothing = _localSmoothing,
                RemoteSmoothing = _remoteSmoothing
            };

            // Start OpenTrack receiver
            _receiver = new OpenTrackReceiver();
            _receiver.Log = _log;
            if (_receiver.Start(config.UdpPort))
            {
                _log?.Invoke(string.Format("Listening on UDP port {0}", config.UdpPort));
            }

            _enabled = config.EnableOnStartup;
        }

        /// <summary>
        /// Updates tracking state. Call this every frame.
        /// </summary>
        /// <param name="deltaTime">Time since last frame in seconds.</param>
        /// <returns>True if tracking is active and data is available.</returns>
        public static bool Update(float deltaTime)
        {
            if (!_initialized || !_enabled || _receiver == null || _processor == null)
            {
                return false;
            }

            if (!_receiver.IsReceiving)
            {
                return false;
            }

            RefreshConnectionFlag();

            // Auto-recenter on first connection
            if (!_hasAutoRecentered)
            {
                _hasAutoRecentered = true;
                _receiver.Recenter();
                _log?.Invoke("Auto-recentered on first connection");
            }

            if (_receiver.TryConsumeRecenterRequest())
            {
                Recenter();
            }

            return true;
        }

        /// <summary>
        /// Gets the processed tracking pose.
        /// </summary>
        /// <param name="deltaTime">Time since last frame for smoothing.</param>
        /// <returns>Processed tracking pose with sensitivity, smoothing, and limits applied.</returns>
        public static TrackingPose GetProcessedPose(float deltaTime)
        {
            if (!_initialized || _receiver == null || _processor == null)
            {
                return new TrackingPose(0, 0, 0, 0);
            }

            // Also refreshed here, not just in Update(): this method runs the processor
            // and is reachable on its own. Update() returns early when the core is
            // disabled or the receiver is not yet receiving, so a mod that calls
            // GetProcessedPose directly - or on a frame Update bailed out of - would
            // otherwise smooth with a stale connection flag and silently get the wrong
            // parameter. Both call sites read the same receiver, so they cannot disagree.
            RefreshConnectionFlag();

            TrackingPose rawPose = _receiver.GetLatestPose();
            return _processor.Process(rawPose, deltaTime);
        }

        // The smoothing parameter is selected per connection, so this must be re-read
        // rather than sampled once: switching from a local tracker to a phone on WiFi
        // has to take effect without a restart.
        private static void RefreshConnectionFlag()
        {
            if (_receiver == null || _processor == null)
            {
                return;
            }
            _processor.IsRemoteConnection = _receiver.IsRemoteConnection;
        }

        /// <summary>
        /// Gets the processed rotation values.
        /// </summary>
        /// <param name="deltaTime">Time since last frame for smoothing.</param>
        /// <param name="yaw">Output: processed yaw in degrees.</param>
        /// <param name="pitch">Output: processed pitch in degrees.</param>
        /// <param name="roll">Output: processed roll in degrees.</param>
        public static void GetProcessedRotation(float deltaTime, out float yaw, out float pitch, out float roll)
        {
            TrackingPose pose = GetProcessedPose(deltaTime);
            yaw = pose.Yaw;
            pitch = pose.Pitch;
            roll = pose.Roll;
        }

        /// <summary>
        /// Calculates the aim screen offset for decoupled aim.
        /// </summary>
        /// <param name="yaw">Head tracking yaw.</param>
        /// <param name="pitch">Head tracking pitch.</param>
        /// <param name="roll">Head tracking roll.</param>
        /// <param name="horizontalFov">Camera horizontal FOV in degrees.</param>
        /// <param name="verticalFov">Camera vertical FOV in degrees.</param>
        /// <param name="screenWidth">Screen width in pixels.</param>
        /// <param name="screenHeight">Screen height in pixels.</param>
        /// <param name="offsetX">Output: screen offset X from center.</param>
        /// <param name="offsetY">Output: screen offset Y from center.</param>
        public static void GetAimScreenOffset(
            float yaw, float pitch, float roll,
            float horizontalFov, float verticalFov,
            float screenWidth, float screenHeight,
            out float offsetX, out float offsetY)
        {
            ScreenOffsetCalculator.Calculate(
                yaw, pitch, roll,
                horizontalFov, verticalFov,
                screenWidth, screenHeight,
                1.0f,
                out offsetX, out offsetY);
        }

        /// <summary>
        /// Gets the aim screen position (center + offset).
        /// </summary>
        /// <param name="yaw">Head tracking yaw.</param>
        /// <param name="pitch">Head tracking pitch.</param>
        /// <param name="roll">Head tracking roll.</param>
        /// <param name="horizontalFov">Camera horizontal FOV in degrees.</param>
        /// <param name="verticalFov">Camera vertical FOV in degrees.</param>
        /// <param name="screenWidth">Screen width in pixels.</param>
        /// <param name="screenHeight">Screen height in pixels.</param>
        /// <param name="posX">Output: screen position X.</param>
        /// <param name="posY">Output: screen position Y.</param>
        public static void GetAimScreenPosition(
            float yaw, float pitch, float roll,
            float horizontalFov, float verticalFov,
            float screenWidth, float screenHeight,
            out float posX, out float posY)
        {
            GetAimScreenOffset(yaw, pitch, roll, horizontalFov, verticalFov, screenWidth, screenHeight, out float offsetX, out float offsetY);
            posX = screenWidth * 0.5f + offsetX;
            posY = screenHeight * 0.5f + offsetY;
        }

        /// <summary>
        /// Recenters the tracking (sets current position as center).
        /// </summary>
        public static void Recenter()
        {
            _receiver?.Recenter();
            _processor?.Reset();
            _log?.Invoke("Recentered");
        }

        /// <summary>
        /// Toggles tracking enabled state.
        /// </summary>
        /// <returns>New enabled state.</returns>
        public static bool Toggle()
        {
            IsEnabled = !IsEnabled;
            return IsEnabled;
        }

        /// <summary>
        /// Resets the processor state (clears smoothing history).
        /// </summary>
        public static void Reset()
        {
            _processor?.Reset();
            _hasAutoRecentered = false;
        }

        /// <summary>
        /// Shuts down the head tracking core.
        /// </summary>
        public static void Shutdown()
        {
            if (_receiver != null)
            {
                _receiver.Stop();
                _receiver = null;
            }
            _processor = null;
            _config = null;
            _initialized = false;
            _hasAutoRecentered = false;
            // Belongs to the session that is ending. Left populated, the static getters
            // keep reporting the previous session's config after shutdown, and a caller
            // that reads them before the next Initialize() gets stale values that look
            // perfectly plausible.
            _localSmoothing = SmoothingUtils.DefaultLocalSmoothing;
            _remoteSmoothing = SmoothingUtils.DefaultRemoteSmoothing;
            _log?.Invoke("StaticHeadTrackingCore shut down");
            _log = null;
        }

        /// <summary>
        /// Gets the current configuration.
        /// </summary>
#if NULLABLE_ENABLED
        public static IHeadTrackingConfig? Config
#else
        public static IHeadTrackingConfig Config
#endif
        {
            get { return _config; }
        }

        /// <summary>
        /// Gets the tracking processor (for advanced use).
        /// </summary>
#if NULLABLE_ENABLED
        public static TrackingProcessor? Processor
#else
        public static TrackingProcessor Processor
#endif
        {
            get { return _processor; }
        }

        /// <summary>
        /// Gets the OpenTrack receiver (for advanced use).
        /// </summary>
#if NULLABLE_ENABLED
        public static OpenTrackReceiver? Receiver
#else
        public static OpenTrackReceiver Receiver
#endif
        {
            get { return _receiver; }
        }
    }
}
