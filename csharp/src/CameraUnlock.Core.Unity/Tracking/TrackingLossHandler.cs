using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Handles graceful degradation when head tracking data is lost.
    /// Provides:
    /// - Delay before starting to fade (hold position briefly)
    /// - Smooth fade toward identity rotation
    /// - Stabilization wait when tracking resumes (for phone apps that need recalibration)
    /// - Optional auto-recenter after extended loss (off by default; see
    ///   RecenterThresholdFrames)
    /// </summary>
    public class TrackingLossHandler
    {
        /// <summary>
        /// Delay in seconds before starting to fade when tracking is lost.
        /// During this time, the last known rotation is held.
        /// </summary>
        public float FadeDelaySeconds { get; set; } = 0.5f;

        /// <summary>
        /// Speed of exponential fade toward identity rotation.
        /// Higher values = faster fade.
        /// </summary>
        public float FadeSpeed { get; set; } = 2.0f;

        /// <summary>
        /// Number of frames to wait after tracking resumes before recentering.
        /// Allows phone tracking apps to stabilize their readings.
        /// </summary>
        public int StabilizationFrames { get; set; } = 10;

        /// <summary>
        /// Number of frames without data before triggering auto-recenter.
        /// 0 (the default) disables it: the tracker app stops sending while the
        /// face is lost, so a mod-side recenter on resumption captures whatever
        /// pose the user holds while sitting back down. Re-acquisition
        /// recentering is the tracker app's decision, signaled through the
        /// packet trailer after its hold-still flow. Set above 0 only for
        /// sources that cannot signal (plain OpenTrack).
        /// </summary>
        public int RecenterThresholdFrames { get; set; } = 0;

        private int _framesWithoutData;
        private float _secondsWithoutData;
        private int _stabilizationFramesRemaining;
        private bool _needsRecenter;
        private TrackingLossState _currentState;

        /// <summary>
        /// Gets the current state of tracking loss handling.
        /// </summary>
        public TrackingLossState CurrentState => _currentState;

        /// <summary>
        /// Gets whether a recenter is needed (tracking was lost long enough to trigger auto-recenter).
        /// Check this and call ClearRecenterFlag() after recentering.
        /// </summary>
        public bool NeedsRecenter => _needsRecenter;

        /// <summary>
        /// Gets the number of stabilization frames remaining.
        /// </summary>
        public int StabilizationFramesRemaining => _stabilizationFramesRemaining;

        /// <summary>
        /// Creates a new TrackingLossHandler with default settings.
        /// </summary>
        public TrackingLossHandler()
        {
            Reset();
        }

        /// <summary>
        /// Updates the tracking loss state based on whether data is available.
        /// Call this every frame.
        /// </summary>
        /// <param name="hasValidData">True if valid tracking data was received this frame.</param>
        /// <param name="deltaTime">Frame delta time (Time.deltaTime).</param>
        /// <returns>The current tracking loss state.</returns>
        public TrackingLossState Update(bool hasValidData, float deltaTime)
        {
            if (!hasValidData)
            {
                _framesWithoutData++;
                _secondsWithoutData += deltaTime;

                // Reset stabilization counter while tracking is lost
                // This ensures we wait for stabilization when tracking resumes
                _stabilizationFramesRemaining = StabilizationFrames;

                // Check for auto-recenter threshold
                if (RecenterThresholdFrames > 0 && _framesWithoutData > RecenterThresholdFrames)
                {
                    _needsRecenter = true;
                }

                // Determine state based on time without data
                if (_secondsWithoutData <= FadeDelaySeconds)
                {
                    _currentState = TrackingLossState.Holding;
                }
                else
                {
                    _currentState = TrackingLossState.Fading;
                }
            }
            else
            {
                _framesWithoutData = 0;
                _secondsWithoutData = 0f;

                // Handle stabilization countdown
                if (_stabilizationFramesRemaining > 0)
                {
                    _stabilizationFramesRemaining--;
                    _currentState = TrackingLossState.Stabilizing;
                }
                else
                {
                    _currentState = TrackingLossState.Active;
                }
            }

            return _currentState;
        }

        /// <summary>
        /// Calculates the fade interpolation factor for the current frame.
        /// Use this with Quaternion.Slerp to fade toward identity.
        /// </summary>
        /// <param name="deltaTime">Frame delta time.</param>
        /// <returns>Interpolation factor (0 = no change, approaching 1 = snap to target).</returns>
        public float GetFadeInterpolation(float deltaTime)
        {
            if (_currentState != TrackingLossState.Fading && _currentState != TrackingLossState.Stabilizing)
            {
                return 0f;
            }

            // Exponential decay: 1 - e^(-speed * dt)
            return 1f - Mathf.Exp(-FadeSpeed * deltaTime);
        }

        /// <summary>
        /// Applies fade to a rotation, moving it toward identity.
        /// Call this when state is Fading or Stabilizing.
        /// </summary>
        /// <param name="currentRotation">The current smoothed rotation.</param>
        /// <param name="deltaTime">Frame delta time.</param>
        /// <returns>The faded rotation (closer to identity).</returns>
        public Quaternion ApplyFade(Quaternion currentRotation, float deltaTime)
        {
            float t = GetFadeInterpolation(deltaTime);
            if (t <= 0f)
            {
                return currentRotation;
            }

            return Quaternion.Slerp(currentRotation, Quaternion.identity, t);
        }

        /// <summary>
        /// Clears the recenter flag after handling a recenter operation.
        /// </summary>
        public void ClearRecenterFlag()
        {
            _needsRecenter = false;
        }

        /// <summary>
        /// Resets all state. Call when tracking is toggled off or camera changes.
        /// </summary>
        public void Reset()
        {
            _framesWithoutData = 0;
            _secondsWithoutData = 0f;
            _stabilizationFramesRemaining = 0;
            _needsRecenter = false;
            _currentState = TrackingLossState.Active;
        }

        /// <summary>
        /// Forces stabilization mode. Call when tracking has just been re-enabled
        /// or after a recenter to allow the tracking source to settle.
        /// </summary>
        public void TriggerStabilization()
        {
            _stabilizationFramesRemaining = StabilizationFrames;
            _currentState = TrackingLossState.Stabilizing;
        }
    }

    /// <summary>
    /// States for tracking loss handling.
    /// </summary>
    public enum TrackingLossState
    {
        /// <summary>
        /// Tracking is active and receiving valid data.
        /// </summary>
        Active,

        /// <summary>
        /// Tracking data lost, but within the hold delay.
        /// Last known rotation should be maintained.
        /// </summary>
        Holding,

        /// <summary>
        /// Tracking data lost beyond hold delay.
        /// Rotation should fade toward identity.
        /// </summary>
        Fading,

        /// <summary>
        /// Tracking just resumed, waiting for source to stabilize.
        /// Continue fading toward identity during this time.
        /// </summary>
        Stabilizing
    }
}
