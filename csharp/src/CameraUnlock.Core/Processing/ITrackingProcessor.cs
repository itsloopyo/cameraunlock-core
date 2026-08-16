using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Interface for tracking data processors.
    ///
    /// The smoothing members are part of the contract, not an implementation detail of
    /// <see cref="TrackingProcessor"/>: smoothing is selected per connection, so a
    /// processor that cannot be told the locality of the current connection is a
    /// processor stuck on <see cref="LocalSmoothing"/> forever. Declaring them here makes
    /// that invariant compiler-enforceable, so a mod holding one of these cannot silently
    /// fail to wire the flag.
    /// </summary>
    public interface ITrackingProcessor
    {
        /// <summary>
        /// User smoothing applied when the tracker runs on this machine (loopback).
        /// 0 = frame interpolation only, 1 = heavy smoothing. No floor is applied.
        /// </summary>
        float LocalSmoothing { get; set; }

        /// <summary>
        /// User smoothing applied when the tracker is a remote device on the network.
        /// 0 = frame interpolation only, 1 = heavy smoothing. No floor is applied.
        /// </summary>
        float RemoteSmoothing { get; set; }

        /// <summary>
        /// Whether the current connection comes from a remote device. Must be fed from the
        /// data source every update: a user switching between a local tracker and a phone
        /// on WiFi has to get the other parameter without restarting the game.
        /// </summary>
        bool IsRemoteConnection { get; set; }

        /// <summary>
        /// Processes a raw tracking pose through the full pipeline.
        /// </summary>
        /// <param name="rawPose">Raw pose from the tracking source.</param>
        /// <param name="deltaTime">Time since last frame in seconds.</param>
        /// <returns>Processed tracking pose.</returns>
        TrackingPose Process(TrackingPose rawPose, float deltaTime);

        /// <summary>
        /// Resets the processor state.
        /// </summary>
        void Reset();
    }
}
