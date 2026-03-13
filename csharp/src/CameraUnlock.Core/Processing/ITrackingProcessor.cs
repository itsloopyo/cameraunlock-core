using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Interface for tracking data processors.
    /// </summary>
    public interface ITrackingProcessor
    {
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
