using System;
using CameraUnlock.Core.Unity.Extensions;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Aim
{
    /// <summary>
    /// Centralized state manager for aim decoupling in Unity.
    /// Tracks the current head tracking rotation and provides methods to compute
    /// aim direction and screen offset for decoupled aim UI.
    ///
    /// <example>
    /// Usage in a head tracking mod:
    /// <code>
    /// // In camera patch, store tracking rotation:
    /// AimDecouplingState.Instance.UpdateTracking(trackingQuaternion, trackingEuler);
    ///
    /// // In UI controller, check if active and get offset:
    /// if (AimDecouplingState.Instance.IsActive)
    /// {
    ///     Vector2 offset = AimDecouplingState.Instance.GetScreenOffset(camera);
    ///     reticle.position = screenCenter + offset;
    /// }
    /// </code>
    /// </example>
    ///
    /// ASSUMES THE TRANSFORM-MODIFICATION APPROACH. <see cref="GetAimDirection"/> and
    /// <see cref="GetScreenOffset"/> undo the tracking rotation off camera.transform.rotation,
    /// which is only correct when the tracking was written INTO the transform.
    ///
    /// Under the view-matrix approach - the primary technique for this library - the transform
    /// still holds the clean base rotation and worldToCameraMatrix holds base*track, so
    /// WorldToScreenPoint applies the inverse a second time and the reticle lands at twice the
    /// true offset. Use <see cref="GetAimDirectionForViewMatrix"/> /
    /// <see cref="GetScreenOffsetForViewMatrix"/> there, or
    /// <c>ViewMatrixTrackingController.TryGetAimNdcOffset</c>, which does the projection from
    /// the controller's own composition.
    /// </summary>
    public class AimDecouplingState
    {
        /// <summary>
        /// Default singleton instance. Can be replaced for testing or custom scenarios.
        /// </summary>
        public static AimDecouplingState Instance { get; set; } = new AimDecouplingState();

        // Per-frame cache for IsActive to avoid repeated magnitude calculation
        private int _cachedIsActiveFrame = -1;
        private bool _cachedIsActive;

        /// <summary>
        /// Whether aim decoupling is enabled via configuration.
        /// </summary>
        public bool Enabled { get; set; } = true;

        /// <summary>
        /// Minimum rotation magnitude (sum of abs values) before decoupling activates.
        /// Prevents jitter when head is nearly centered.
        /// </summary>
        public float MinRotationThreshold { get; set; } = 0.5f;

        /// <summary>
        /// The last head tracking rotation quaternion applied to the camera.
        /// Set this from your camera patch after applying tracking.
        /// </summary>
        public Quaternion LastTrackingQuaternion { get; private set; } = Quaternion.identity;

        /// <summary>
        /// The last head tracking euler angles (pitch, yaw, roll) in degrees.
        /// Set this from your camera patch after applying tracking.
        /// </summary>
        public Vector3 LastTrackingEuler { get; private set; } = Vector3.zero;

        /// <summary>
        /// Optional delegate to check if tracking data is fresh/valid.
        /// If null, freshness is not checked.
        /// </summary>
        public Func<bool> IsTrackingDataFresh { get; set; }

        /// <summary>
        /// Whether aim decoupling is currently active.
        /// Returns true if enabled, data is fresh, and rotation exceeds threshold.
        /// Result is cached per-frame to avoid repeated magnitude calculations.
        /// </summary>
        public bool IsActive
        {
            get
            {
                int currentFrame = Time.frameCount;
                if (_cachedIsActiveFrame == currentFrame)
                {
                    return _cachedIsActive;
                }

                _cachedIsActiveFrame = currentFrame;
                _cachedIsActive = ComputeIsActive();
                return _cachedIsActive;
            }
        }

        private bool ComputeIsActive()
        {
            if (!Enabled)
            {
                return false;
            }

            if (IsTrackingDataFresh != null && !IsTrackingDataFresh())
            {
                return false;
            }

            float magnitude = Mathf.Abs(LastTrackingEuler.x)
                            + Mathf.Abs(LastTrackingEuler.y)
                            + Mathf.Abs(LastTrackingEuler.z);
            return magnitude > MinRotationThreshold;
        }

        /// <summary>
        /// Updates the stored tracking rotation.
        /// Call this from your camera patch after applying head tracking rotation.
        /// </summary>
        /// <param name="trackingQuaternion">The quaternion rotation applied to the camera.</param>
        /// <param name="trackingEuler">The euler angles (pitch, yaw, roll) used.</param>
        public void UpdateTracking(Quaternion trackingQuaternion, Vector3 trackingEuler)
        {
            LastTrackingQuaternion = trackingQuaternion;
            LastTrackingEuler = trackingEuler;
        }

        /// <summary>
        /// Updates the stored tracking rotation from euler angles.
        /// Constructs the quaternion internally.
        /// </summary>
        /// <param name="pitch">Pitch in degrees (x rotation).</param>
        /// <param name="yaw">Yaw in degrees (y rotation).</param>
        /// <param name="roll">Roll in degrees (z rotation).</param>
        public void UpdateTracking(float pitch, float yaw, float roll)
        {
            LastTrackingEuler = new Vector3(pitch, yaw, roll);
            LastTrackingQuaternion = Quaternion.Euler(pitch, yaw, roll);
        }

        /// <summary>
        /// Resets tracking state to identity (no rotation).
        /// Call when tracking is disabled or recentered.
        /// </summary>
        public void Reset()
        {
            LastTrackingQuaternion = Quaternion.identity;
            LastTrackingEuler = Vector3.zero;
        }

        /// <summary>
        /// Computes the aim direction in world space.
        /// This is where the player's aim should go (opposite of head movement).
        ///
        /// TRANSFORM-MODIFICATION APPROACH ONLY: it undoes the tracking rotation off
        /// camera.transform.rotation, so the transform must be the one carrying the tracking.
        /// If tracking lives in worldToCameraMatrix instead, use
        /// <see cref="GetAimDirectionForViewMatrix"/>.
        /// </summary>
        /// <param name="camera">The camera to use for world space transformation.</param>
        /// <returns>World-space direction vector for aiming.</returns>
        public Vector3 GetAimDirection(Camera camera)
        {
            if (camera == null)
            {
                return Vector3.forward;
            }

            // Inverse of tracking rotation applied to forward gives local aim direction
            Vector3 localAim = Quaternion.Inverse(LastTrackingQuaternion) * Vector3.forward;

            // Transform to world space using camera's current rotation
            return camera.transform.rotation * localAim;
        }

        /// <summary>
        /// The aim direction for the VIEW-MATRIX approach, where camera.transform still holds
        /// the game's clean rotation and only worldToCameraMatrix carries the head tracking.
        /// The transform's own forward IS the aim direction; undoing the tracking rotation
        /// again (as <see cref="GetAimDirection"/> does) doubles the offset.
        /// </summary>
        /// <param name="camera">The camera to use for world space transformation.</param>
        /// <returns>World-space direction vector for aiming.</returns>
        public Vector3 GetAimDirectionForViewMatrix(Camera camera)
        {
            if (camera == null)
            {
                return Vector3.forward;
            }

            return camera.transform.rotation * Vector3.forward;
        }

        /// <summary>
        /// Computes screen offset in pixels from screen center for aim UI positioning.
        /// Handles the case where aim direction is behind the camera by clamping to edge.
        ///
        /// TRANSFORM-MODIFICATION APPROACH ONLY - see <see cref="GetAimDirection"/>.
        /// </summary>
        /// <param name="camera">The camera to use for projection.</param>
        /// <returns>Offset in pixels from screen center.</returns>
        public Vector2 GetScreenOffset(Camera camera)
        {
            if (camera == null)
            {
                return Vector2.zero;
            }

            return ProjectAimDirection(camera, GetAimDirection(camera));
        }

        /// <summary>
        /// Screen offset for the VIEW-MATRIX approach: projects the transform's clean forward
        /// through the head-tracked view matrix, which is exactly where the aim point lands
        /// on screen.
        /// </summary>
        /// <param name="camera">The camera to use for projection.</param>
        /// <returns>Offset in pixels from screen center.</returns>
        public Vector2 GetScreenOffsetForViewMatrix(Camera camera)
        {
            if (camera == null)
            {
                return Vector2.zero;
            }

            return ProjectAimDirection(camera, GetAimDirectionForViewMatrix(camera));
        }

        private static Vector2 ProjectAimDirection(Camera camera, Vector3 aimDirection)
        {
            // Project aim point onto screen
            Vector3 aimWorldPoint = camera.transform.position + aimDirection * 10f;
            Vector3 screenPoint = camera.WorldToScreenPoint(aimWorldPoint);

            float halfWidth = Screen.width * 0.5f;
            float halfHeight = Screen.height * 0.5f;

            // screenPoint.z > 0 means the point is in front of the camera
            if (screenPoint.z > 0f)
            {
                return new Vector2(screenPoint.x - halfWidth, screenPoint.y - halfHeight);
            }

            // Aim is behind camera - clamp to screen edge using shared helper
            return UnityAimHelper.ClampToScreenEdge(aimDirection, camera, halfWidth, halfHeight);
        }
    }
}
