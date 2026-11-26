using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Protocol
{
    /// <summary>
    /// Everything the per-frame pipeline needs from a tracking data source.
    ///
    /// <see cref="OpenTrackReceiver"/> is the implementation that ships here; the
    /// interface exists so the pipeline owners (HeadTrackingSession,
    /// ViewMatrixTrackingController, RemoteRecenter) can be driven by a test double or by
    /// a mod's own transport. Lifecycle (Start/Stop/Dispose) is deliberately NOT part of
    /// this: whoever constructs the source owns it, and the owners here do not.
    /// </summary>
    public interface ITrackingDataSource
    {
        /// <summary>
        /// True if data has been received within the connection timeout.
        /// </summary>
        bool IsReceiving { get; }

        /// <summary>
        /// True if the packet source is a remote network device rather than this machine
        /// (loopback). Selects between the two smoothing parameters via
        /// <see cref="CameraUnlock.Core.Math.SmoothingUtils.GetEffectiveSmoothing(float, float, bool)"/>.
        /// Re-read every frame, not sampled once: a user switching between a local
        /// OpenTrack instance and a phone on WiFi must get the other parameter without
        /// restarting the game.
        /// </summary>
        bool IsRemoteConnection { get; }

        /// <summary>
        /// True if the data source failed to initialize.
        /// </summary>
        bool IsFailed { get; }

        /// <summary>
        /// True while the most recent sample is younger than <paramref name="maxAgeMs"/>.
        /// </summary>
        bool IsDataFresh(int maxAgeMs = OpenTrackReceiver.DefaultMaxDataAgeMs);

        /// <summary>
        /// Gets the latest tracking pose with sensitivity and offset applied.
        /// </summary>
        TrackingPose GetLatestPose();

        /// <summary>
        /// Gets the latest positional sample with the center offset applied.
        /// </summary>
        PositionData GetLatestPosition();

        /// <summary>
        /// Gets the raw rotation values without sensitivity or offset applied.
        /// </summary>
        /// <param name="yaw">Output: raw yaw value.</param>
        /// <param name="pitch">Output: raw pitch value.</param>
        /// <param name="roll">Output: raw roll value.</param>
        void GetRawRotation(out float yaw, out float pitch, out float roll);

        /// <summary>
        /// Consumes a pending tracker-app recenter request (HCAM packet trailer), returning
        /// true exactly once per press. Exactly one consumer per source: a second consumer
        /// races the first and only one of them ever sees a given press.
        /// </summary>
        bool TryConsumeRecenterRequest();

        /// <summary>
        /// Sets the current position as the new center point.
        /// </summary>
        void Recenter();
    }
}
