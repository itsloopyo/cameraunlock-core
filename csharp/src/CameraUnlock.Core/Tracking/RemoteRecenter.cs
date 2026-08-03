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
        /// Consumes a pending recenter request and centers the pipeline on the
        /// current pose. Returns true when a request was consumed, so the
        /// caller can notify the user and cancel any pending
        /// connection-stabilization recenter of its own.
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
        /// <param name="receiver">Receiver to consume the request from.</param>
        /// <param name="processor">Rotation processor to center.</param>
        /// <param name="poseInterpolator">Optional pose interpolator to reset.</param>
        /// <param name="positionProcessor">Optional position processor to center.</param>
        /// <param name="positionInterpolator">Optional position interpolator to reset.</param>
#if NULLABLE_ENABLED
        public static bool TryConsume(
            OpenTrackReceiver receiver,
            TrackingProcessor processor,
            PoseInterpolator? poseInterpolator = null,
            PositionProcessor? positionProcessor = null,
            PositionInterpolator? positionInterpolator = null)
#else
        public static bool TryConsume(
            OpenTrackReceiver receiver,
            TrackingProcessor processor,
            PoseInterpolator poseInterpolator = null,
            PositionProcessor positionProcessor = null,
            PositionInterpolator positionInterpolator = null)
#endif
        {
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
