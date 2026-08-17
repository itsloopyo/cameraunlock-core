using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Abstract base class for managing camera lifecycle and tracking hook attachment.
    /// Watches for the main gameplay camera to become available and attaches a tracking hook.
    /// Handles scene transitions and camera recreation gracefully.
    ///
    /// Subclasses implement:
    /// - FindCamera(): Game-specific camera discovery
    /// - OnCameraFound(Camera): Called when camera becomes available
    /// - OnCameraLost(): Called when camera is no longer available
    /// - CreateTrackingHook(Camera): Creates the game-specific tracking component
    /// </summary>
    public abstract class CameraLifecycleManager : MonoBehaviour
    {
        private Camera _trackedCamera;
        private Component _trackingHook;

        // Throttle logging to avoid spam
        private float _lastLogTime;
        private float _logInterval = 5f;

        /// <summary>
        /// The currently tracked camera, if any.
        /// </summary>
        public Camera TrackedCamera => _trackedCamera;

        /// <summary>
        /// The currently attached tracking hook component, if any.
        /// </summary>
        public Component TrackingHook => _trackingHook;

        /// <summary>
        /// Returns true if a camera is currently being tracked.
        /// </summary>
        public bool IsTracking => _trackedCamera != null;

        /// <summary>
        /// Returns true if the tracking hook is currently attached and active.
        /// </summary>
        public bool IsHookAttached => _trackingHook != null && _trackingHook.gameObject != null;

        /// <summary>
        /// Interval in seconds between log messages when camera is lost.
        /// Set to 0 to disable throttling.
        /// </summary>
        public float LogInterval
        {
            get { return _logInterval; }
            set { _logInterval = value; }
        }

        /// <summary>
        /// Called by Unity each frame after Update.
        /// Override and call base if you need to add additional per-frame logic.
        /// </summary>
        protected virtual void LateUpdate()
        {
            Camera camera = FindCamera();

            if (camera == null)
            {
                HandleCameraLost();
                return;
            }

            // Check if we need to (re-)attach the hook
            if (NeedsHookAttachment(camera))
            {
                AttachHook(camera);
            }
        }

        /// <summary>
        /// Determines if we need to attach or re-attach the tracking hook.
        /// Override to add additional checks.
        /// </summary>
        protected virtual bool NeedsHookAttachment(Camera camera)
        {
            // No hook exists
            if (_trackingHook == null)
            {
                return true;
            }

            // Hook was destroyed (scene change, etc.)
            if (_trackingHook.gameObject == null)
            {
                return true;
            }

            // Camera changed
            if (_trackedCamera != camera)
            {
                return true;
            }

            // Hook is attached to wrong object
            if (_trackingHook.gameObject != camera.gameObject)
            {
                return true;
            }

            return false;
        }

        /// <summary>
        /// Attaches the tracking hook to the camera.
        /// </summary>
        private void AttachHook(Camera camera)
        {
            // Clean up old hook if it exists
            if (_trackingHook != null && _trackingHook.gameObject != null)
            {
                Destroy(_trackingHook);
                _trackingHook = null;
            }

            // Check if a hook already exists on this camera (from previous attachment)
            Component existingHook = FindExistingHook(camera);
            if (existingHook != null)
            {
                _trackingHook = existingHook;
                _trackedCamera = camera;
                OnCameraFound(camera, existingHook, isReused: true);
            }
            else
            {
                // Create new hook
                _trackingHook = CreateTrackingHook(camera);
                if (_trackingHook != null)
                {
                    _trackedCamera = camera;
                    OnCameraFound(camera, _trackingHook, isReused: false);
                }
            }
        }

        /// <summary>
        /// Handles the case when the camera is lost (scene transition, etc.).
        /// </summary>
        private void HandleCameraLost()
        {
            if (_trackedCamera != null)
            {
                // The _trackedCamera null check already makes this fire exactly once per
                // loss, so the interval could only ever suppress a real notification.
                // Gating it here meant a second camera loss within LogInterval skipped the
                // subclass's cleanup entirely - reticle left drawn over the menu, view
                // matrix never reset - on a gameplay/menu round trip inside 5 seconds.
                _lastLogTime = Time.unscaledTime;
                OnCameraLost();

                // Clear references but don't destroy hook - it will be destroyed with camera
                _trackedCamera = null;
                _trackingHook = null;
            }
        }

        /// <summary>
        /// Forces an immediate refresh of the camera tracking.
        /// Call this when you know the camera may have changed (e.g., after scene load).
        /// </summary>
        public void ForceRefresh()
        {
            _trackedCamera = null;
            _trackingHook = null;
        }

        /// <summary>
        /// Finds the camera to track. Override to implement game-specific camera discovery.
        /// Return null if no suitable camera is currently available.
        /// </summary>
        /// <returns>The camera to track, or null if not available</returns>
        protected abstract Camera FindCamera();

        /// <summary>
        /// Creates and attaches the tracking hook component to the camera.
        /// Override to create your game-specific tracking component.
        /// </summary>
        /// <param name="camera">The camera to attach to</param>
        /// <returns>The created component, or null if creation failed</returns>
        protected abstract Component CreateTrackingHook(Camera camera);

        /// <summary>
        /// Called when a camera is found and the hook is attached.
        /// Override to perform additional setup or logging.
        /// </summary>
        /// <param name="camera">The camera that was found</param>
        /// <param name="hook">The tracking hook that was attached</param>
        /// <param name="isReused">True if an existing hook was reused, false if newly created</param>
        protected virtual void OnCameraFound(Camera camera, Component hook, bool isReused)
        {
        }

        /// <summary>
        /// Called when the tracked camera is lost.
        /// Override to perform cleanup or logging.
        /// </summary>
        protected virtual void OnCameraLost()
        {
        }

        /// <summary>
        /// Finds an existing tracking hook on the camera.
        /// Override to check for your specific hook type.
        /// Default implementation returns null (no existing hook check).
        /// </summary>
        /// <param name="camera">The camera to check</param>
        /// <returns>The existing hook component, or null if none found</returns>
        protected virtual Component FindExistingHook(Camera camera)
        {
            return null;
        }

        /// <summary>
        /// Called when this component is destroyed.
        /// Cleans up the tracking hook if it still exists.
        /// </summary>
        protected virtual void OnDestroy()
        {
            if (_trackingHook != null && _trackingHook.gameObject != null)
            {
                Destroy(_trackingHook);
            }
        }
    }
}
