using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Tracks the game's intended camera rotation separately from head tracking rotation.
    /// This separation is essential for:
    /// - Aim decoupling (reticle/crosshair positioning)
    /// - Effects that should follow aim direction, not view direction
    /// - Raycasts that should use the game's intended direction
    /// - UI elements that need the base aim point
    /// </summary>
    public class BaseRotationTracker
    {
        private Quaternion _baseRotation = Quaternion.identity;
        private Quaternion _headTrackingRotation = Quaternion.identity;
        private Transform _trackedTransform;
        private bool _hasValidData;

        /// <summary>
        /// Gets the game's intended rotation (world space), without head tracking.
        /// This is where the game "wants" the camera to point.
        /// </summary>
        public Quaternion BaseRotation => _baseRotation;

        /// <summary>
        /// Gets the current head tracking rotation being applied.
        /// </summary>
        public Quaternion HeadTrackingRotation => _headTrackingRotation;

        /// <summary>
        /// Gets the combined rotation (base * head tracking).
        /// This is what the camera actually shows.
        /// </summary>
        public Quaternion CombinedRotation => _baseRotation * _headTrackingRotation;

        /// <summary>
        /// Gets the forward direction of the base rotation (aim direction).
        /// Use this for reticle positioning and aim-related calculations.
        /// </summary>
        public Vector3 BaseForward => _baseRotation * Vector3.forward;

        /// <summary>
        /// Gets the forward direction of the combined rotation (view direction).
        /// </summary>
        public Vector3 CombinedForward => CombinedRotation * Vector3.forward;

        /// <summary>
        /// Gets whether valid tracking data has been set.
        /// </summary>
        public bool HasValidData => _hasValidData;

        /// <summary>
        /// Gets the currently tracked transform (may be null).
        /// </summary>
        public Transform TrackedTransform => _trackedTransform;

        /// <summary>
        /// Updates the tracked rotations. Call this in your camera patch.
        /// </summary>
        /// <param name="cameraTransform">The camera transform being modified.</param>
        /// <param name="gameWantedRotation">The game's intended local rotation (before head tracking).</param>
        /// <param name="headTrackingRotation">The head tracking rotation to apply.</param>
        public void Update(Transform cameraTransform, Quaternion gameWantedRotation, Quaternion headTrackingRotation)
        {
            _trackedTransform = cameraTransform;
            _headTrackingRotation = headTrackingRotation;

            // Calculate world-space base rotation
            if (cameraTransform != null && cameraTransform.parent != null)
            {
                _baseRotation = cameraTransform.parent.rotation * gameWantedRotation;
            }
            else
            {
                _baseRotation = gameWantedRotation;
            }

            _hasValidData = true;
        }

        /// <summary>
        /// Updates with only base rotation (when head tracking is disabled).
        /// </summary>
        /// <param name="cameraTransform">The camera transform.</param>
        /// <param name="gameWantedRotation">The game's intended local rotation.</param>
        public void UpdateBaseOnly(Transform cameraTransform, Quaternion gameWantedRotation)
        {
            Update(cameraTransform, gameWantedRotation, Quaternion.identity);
        }

        /// <summary>
        /// Gets the local rotation that would result from applying base rotation only.
        /// Useful for temporarily removing head tracking.
        /// </summary>
        /// <returns>Local rotation without head tracking.</returns>
        public Quaternion GetBaseLocalRotation()
        {
            if (_trackedTransform == null)
            {
                return _baseRotation;
            }

            if (_trackedTransform.parent != null)
            {
                return Quaternion.Inverse(_trackedTransform.parent.rotation) * _baseRotation;
            }

            return _baseRotation;
        }

        /// <summary>
        /// Gets the local rotation with head tracking applied.
        /// </summary>
        /// <returns>Local rotation with head tracking.</returns>
        public Quaternion GetCombinedLocalRotation()
        {
            Quaternion combined = CombinedRotation;

            if (_trackedTransform == null)
            {
                return combined;
            }

            if (_trackedTransform.parent != null)
            {
                return Quaternion.Inverse(_trackedTransform.parent.rotation) * combined;
            }

            return combined;
        }

        /// <summary>
        /// Projects the base forward direction onto the screen using the given camera.
        /// Use this for positioning reticles/crosshairs.
        /// </summary>
        /// <param name="camera">The camera to project with.</param>
        /// <param name="distance">Distance to project forward (default 100).</param>
        /// <returns>Screen position where the base aim direction intersects.</returns>
        public Vector3 ProjectBaseForwardToScreen(Camera camera, float distance = 100f)
        {
            if (camera == null || !_hasValidData)
            {
                return new Vector3(Screen.width * 0.5f, Screen.height * 0.5f, 0f);
            }

            Vector3 worldPoint = camera.transform.position + BaseForward * distance;
            return camera.WorldToScreenPoint(worldPoint);
        }

        /// <summary>
        /// Calculates the angular difference between base aim and current view.
        /// Useful for UI offset calculations.
        /// </summary>
        /// <returns>Angle in degrees between base and combined forward directions.</returns>
        public float GetAimViewAngle()
        {
            if (!_hasValidData)
            {
                return 0f;
            }

            return Vector3.Angle(BaseForward, CombinedForward);
        }

        /// <summary>
        /// Resets all tracked data.
        /// </summary>
        public void Reset()
        {
            _baseRotation = Quaternion.identity;
            _headTrackingRotation = Quaternion.identity;
            _trackedTransform = null;
            _hasValidData = false;
        }
    }
}
