using UnityEngine;

namespace CameraUnlock.Core.Unity.Patching
{
    /// <summary>
    /// Static helpers for camera rotation patching patterns.
    /// Provides the standard prefix/postfix pattern for applying head tracking rotation:
    /// - Prefix: Remove the previous frame's tracking offset
    /// - Postfix: Apply the current frame's tracking offset
    ///
    /// This pattern ensures the game's camera logic runs against the "base" rotation
    /// without head tracking, then head tracking is re-applied afterwards.
    /// </summary>
    public static class CameraPatching
    {
        /// <summary>
        /// Removes the head tracking offset from the camera's local rotation.
        /// Call this in your Harmony prefix before the game's camera logic runs.
        /// </summary>
        /// <param name="cameraTransform">The camera transform to modify.</param>
        /// <param name="appliedInverse">The inverse of the previously applied rotation.
        /// Pass the value stored from the last ApplyHeadTrackingOffset call.</param>
        /// <param name="wasApplied">Whether an offset was actually applied last frame.
        /// If false, no modification is made.</param>
        public static void RemoveHeadTrackingOffset(
            Transform cameraTransform,
            Quaternion appliedInverse,
            bool wasApplied)
        {
            if (cameraTransform == null || !wasApplied)
            {
                return;
            }

            cameraTransform.localRotation = cameraTransform.localRotation * appliedInverse;
        }

        /// <summary>
        /// Applies the head tracking offset to the camera's local rotation.
        /// Call this in your Harmony postfix after the game's camera logic runs.
        /// </summary>
        /// <param name="cameraTransform">The camera transform to modify.</param>
        /// <param name="trackingRotation">The head tracking rotation to apply.</param>
        /// <param name="appliedInverse">Outputs the inverse of the applied rotation.
        /// Store this value for the next frame's RemoveHeadTrackingOffset call.</param>
        public static void ApplyHeadTrackingOffset(
            Transform cameraTransform,
            Quaternion trackingRotation,
            out Quaternion appliedInverse)
        {
            appliedInverse = Quaternion.identity;

            if (cameraTransform == null)
            {
                return;
            }

            cameraTransform.localRotation = cameraTransform.localRotation * trackingRotation;
            appliedInverse = Quaternion.Inverse(trackingRotation);
        }

        /// <summary>
        /// Applies head tracking with world-space yaw and local-space pitch/roll.
        /// This is a common pattern for first-person games where yaw should be world-aligned.
        /// </summary>
        /// <param name="cameraTransform">The camera transform to modify.</param>
        /// <param name="yawDegrees">Yaw rotation in degrees (world space).</param>
        /// <param name="pitchDegrees">Pitch rotation in degrees (local space).</param>
        /// <param name="rollDegrees">Roll rotation in degrees (local space).</param>
        /// <param name="appliedInverse">Outputs the inverse for removal next frame.</param>
        public static void ApplyAdditiveHeadTracking(
            Transform cameraTransform,
            float yawDegrees,
            float pitchDegrees,
            float rollDegrees,
            out Quaternion appliedInverse)
        {
            appliedInverse = Quaternion.identity;

            if (cameraTransform == null)
            {
                return;
            }

            // Compose rotation: yaw (world) * current * pitch * roll (local)
            Quaternion yaw = Quaternion.AngleAxis(yawDegrees, Vector3.up);
            Quaternion pitch = Quaternion.AngleAxis(pitchDegrees, Vector3.right);
            Quaternion roll = Quaternion.AngleAxis(rollDegrees, Vector3.forward);

            Quaternion localOffset = pitch * roll;
            Quaternion worldYaw = yaw;

            // The world yaw is converted into the camera's own space and applied on the RIGHT,
            // like the pitch/roll offset, because that is the only form RemoveHeadTrackingOffset
            // can undo: it right-multiplies the stored inverse. Left-multiplying the yaw and
            // right-multiplying its inverse yields Qy*R*Qy^-1, which equals R only when the
            // camera's rotation commutes with the yaw - i.e. only when it is level. The
            // conjugation also carries the parent's rotation, so this is correct on a parented
            // camera, where writing .rotation directly was not.
            Quaternion worldRotation = cameraTransform.rotation;
            Quaternion localYaw = Quaternion.Inverse(worldRotation) * worldYaw * worldRotation;
            Quaternion applied = localYaw * localOffset;

            cameraTransform.localRotation = cameraTransform.localRotation * applied;

            appliedInverse = Quaternion.Inverse(applied);
        }

        /// <summary>
        /// Calculates the aim direction after head tracking has been applied.
        /// This is the direction the game "intends" to aim, before head tracking offset.
        /// </summary>
        /// <param name="cameraTransform">The camera transform with head tracking applied.</param>
        /// <param name="appliedInverse">The inverse of the applied tracking rotation.</param>
        /// <returns>The aim direction in world space.</returns>
        public static Vector3 GetAimDirection(Transform cameraTransform, Quaternion appliedInverse)
        {
            if (cameraTransform == null)
            {
                return Vector3.forward;
            }

            // Remove tracking rotation to get base aim direction
            Quaternion baseRotation = cameraTransform.rotation * appliedInverse;
            return baseRotation * Vector3.forward;
        }

        /// <summary>
        /// Calculates the aim direction using the inverse tracking rotation.
        /// Alternative signature that takes just the rotation values.
        /// </summary>
        /// <param name="currentRotation">The current camera rotation (with tracking applied).</param>
        /// <param name="trackingRotation">The tracking rotation that was applied.</param>
        /// <returns>The aim direction in world space.</returns>
        public static Vector3 GetAimDirection(Quaternion currentRotation, Quaternion trackingRotation)
        {
            Quaternion baseRotation = currentRotation * Quaternion.Inverse(trackingRotation);
            return baseRotation * Vector3.forward;
        }
    }
}
