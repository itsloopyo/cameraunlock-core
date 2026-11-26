using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tracking
{
    /// <summary>
    /// Canonical handling of a tracker-app recenter request (packet trailer)
    /// for mods that wire the pipeline pieces themselves instead of using
    /// <see cref="HeadTrackingSession"/> or ViewMatrixTrackingController -
    /// both of which already consume the request internally, so never combine
    /// them with this helper on the same receiver.
    /// </summary>
    public static class RemoteRecenter
    {
        /// <summary>
        /// Pushes the source's connection flag onto the processors, then consumes a
        /// pending recenter request and centers the pipeline on the current pose. Returns
        /// true when a request was consumed, so the caller can notify the user and cancel
        /// any pending connection-stabilization recenter of its own.
        ///
        /// Call this every frame, not only when a recenter is expected: a hand-wired
        /// pipeline has no other component that owns the connection flag, and the two
        /// smoothing parameters are selected per connection. Without a per-frame push both
        /// processors stay local forever and RemoteSmoothing is dead config with nothing to
        /// catch it, which is exactly the population this helper exists for.
        ///
        /// Centers via <see cref="TrackingProcessor.RecenterTo"/> with the
        /// latest received pose: the tracker app zeroes its own output before
        /// signaling, so the packet carrying the request already holds the new
        /// center. TrackingProcessor.Recenter() would fold the previous
        /// smoothed pose - which the tracker just subtracted at its end - into
        /// the offset a second time, parking the view mirrored from the
        /// pre-press drift. Never also call OpenTrackReceiver.Recenter():
        /// centering at both levels subtracts the offset twice.
        /// </summary>
        /// <param name="receiver">Source to read the connection flag from and consume the request from.</param>
        /// <param name="processor">Rotation processor to feed and center.</param>
        /// <param name="poseInterpolator">Optional pose interpolator to reset.</param>
        /// <param name="positionProcessor">Optional position processor to feed and center.</param>
        /// <param name="positionInterpolator">Optional position interpolator to reset.</param>
#if NULLABLE_ENABLED
        public static bool TryConsume(
            ITrackingDataSource receiver,
            TrackingProcessor processor,
            PoseInterpolator? poseInterpolator = null,
            PositionProcessor? positionProcessor = null,
            PositionInterpolator? positionInterpolator = null)
#else
        public static bool TryConsume(
            ITrackingDataSource receiver,
            TrackingProcessor processor,
            PoseInterpolator poseInterpolator = null,
            PositionProcessor positionProcessor = null,
            PositionInterpolator positionInterpolator = null)
#endif
        {
            bool isRemote = receiver.IsRemoteConnection;
            processor.IsRemoteConnection = isRemote;
            if (positionProcessor != null)
            {
                positionProcessor.IsRemoteConnection = isRemote;
            }

            if (!receiver.TryConsumeRecenterRequest())
            {
                return false;
            }

            processor.RecenterTo(receiver.GetLatestPose());
            if (poseInterpolator != null)
            {
                poseInterpolator.Reset();
            }
            if (positionProcessor != null)
            {
                positionProcessor.SetCenter(receiver.GetLatestPosition());
            }
            if (positionInterpolator != null)
            {
                positionInterpolator.Reset();
            }
            return true;
        }
    }
}
