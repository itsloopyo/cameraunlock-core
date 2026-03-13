using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Modifies a camera's view matrix to apply head tracking rotation.
    /// This affects ONLY rendering - game logic (movement, aiming, physics) remains unchanged.
    ///
    /// This is the key technique for head tracking that doesn't interfere with gameplay:
    /// - The camera transform remains unchanged (game logic uses transform)
    /// - Only the worldToCameraMatrix is modified (rendering uses this)
    /// - Result: you can look around with your head while the game aims where it always did
    /// </summary>
    public static class ViewMatrixModifier
    {
        /// <summary>
        /// Applies head tracking rotation to the camera's view matrix.
        /// Call this from Camera.onPreCull or Camera.onPreRender.
        /// </summary>
        /// <param name="cam">The camera to modify.</param>
        /// <param name="headRotation">The head tracking rotation quaternion.</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void ApplyHeadRotation(Camera cam, Quaternion headRotation)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            // Reset the matrix so Unity recalculates it from the camera's transform.
            // This prevents accumulation from our previous frame's modification.
            cam.ResetWorldToCameraMatrix();

            // Read the fresh matrix (calculated from camera's transform)
            Matrix4x4 gameViewMatrix = cam.worldToCameraMatrix;

            // Create rotation matrix from head tracking
            Matrix4x4 headRotMatrix = Matrix4x4.Rotate(headRotation);

            // Apply head tracking: rotate the view in camera space
            // Multiplication order: headRot * gameView means head rotation is applied
            // in camera space, which is what we want for natural head movement.
            cam.worldToCameraMatrix = headRotMatrix * gameViewMatrix;
        }

        /// <summary>
        /// Applies head tracking rotation to the camera's view matrix using Euler angles.
        /// Roll is inverted to match view-space conventions (negative roll = tilt right).
        /// </summary>
        /// <param name="cam">The camera to modify.</param>
        /// <param name="yaw">Head tracking yaw in degrees (positive = look right).</param>
        /// <param name="pitch">Head tracking pitch in degrees (positive = look up).</param>
        /// <param name="roll">Head tracking roll in degrees (positive = tilt left).</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void ApplyHeadRotation(Camera cam, float yaw, float pitch, float roll)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            // Note: Roll is inverted (negative) in view-space convention
            // Positive roll input means tilt head left, which should tilt view right
            Quaternion headRotation = Quaternion.Euler(pitch, yaw, -roll);
            ApplyHeadRotation(cam, headRotation);
        }

        /// <summary>
        /// Applies head tracking rotation with world-space yaw and camera-local pitch/roll.
        /// This is the correct rotation order for natural head tracking:
        /// - Yaw rotates around world up (gravity) - turning your head left/right is always horizontal
        /// - Pitch/roll rotate around camera's local axes - looking up/down and tilting
        ///
        /// The decomposition prevents "leaning" artifacts when looking up/down while yawed.
        /// </summary>
        /// <param name="cam">The camera to modify.</param>
        /// <param name="yaw">Head tracking yaw in degrees (positive = look right).</param>
        /// <param name="pitch">Head tracking pitch in degrees (positive = look up).</param>
        /// <param name="roll">Head tracking roll in degrees (positive = tilt left).</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void ApplyHeadRotationDecomposed(Camera cam, float yaw, float pitch, float roll)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            // World-space yaw: rotates around world up vector (gravity-aligned)
            Quaternion worldYaw = Quaternion.AngleAxis(yaw, Vector3.up);

            // Local pitch/roll: applied in camera's local space
            // Note: Roll is NOT inverted here - caller should handle sign convention
            Quaternion localPitchRoll = Quaternion.Euler(pitch, 0, roll);

            // Get camera's current rotation (game's intended view direction)
            Quaternion baseRotation = cam.transform.rotation;

            // Compose: worldYaw * baseRotation * localPitchRoll
            // This applies yaw in world space, preserves base rotation, then applies pitch/roll locally
            Quaternion finalRotation = worldYaw * baseRotation * localPitchRoll;

            // Build view matrix from the final rotation and camera position
            // View matrix = inverse of camera's world transform
            Matrix4x4 rotationMatrix = Matrix4x4.Rotate(Quaternion.Inverse(finalRotation));
            Matrix4x4 translationMatrix = Matrix4x4.Translate(-cam.transform.position);
            Matrix4x4 viewMatrix = rotationMatrix * translationMatrix;

            // Unity cameras look down -Z, so flip the Z row
            viewMatrix.m20 = -viewMatrix.m20;
            viewMatrix.m21 = -viewMatrix.m21;
            viewMatrix.m22 = -viewMatrix.m22;
            viewMatrix.m23 = -viewMatrix.m23;

            cam.worldToCameraMatrix = viewMatrix;
        }

        /// <summary>
        /// Applies head tracking rotation with world-space yaw and camera-local pitch/roll,
        /// plus a neck pivot offset that compensates for the eye orbiting around the neck.
        /// </summary>
        /// <param name="cam">The camera to modify.</param>
        /// <param name="yaw">Head tracking yaw in degrees (positive = look right).</param>
        /// <param name="pitch">Head tracking pitch in degrees (positive = look up).</param>
        /// <param name="roll">Head tracking roll in degrees (positive = tilt left).</param>
        /// <param name="neckPivotToEyes">Offset from neck pivot to eyes in local space (0, up, forward).</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void ApplyHeadRotationDecomposed(Camera cam, float yaw, float pitch, float roll, Vector3 neckPivotToEyes)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            Quaternion worldYaw = Quaternion.AngleAxis(yaw, Vector3.up);
            Quaternion localPitchRoll = Quaternion.Euler(pitch, 0, roll);
            Quaternion baseRotation = cam.transform.rotation;
            Quaternion finalRotation = worldYaw * baseRotation * localPitchRoll;

            // Neck model: eyes orbit around the neck pivot when the head rotates.
            // Compute the world-space displacement of the eye position.
            Quaternion headRotation = Quaternion.Inverse(baseRotation) * finalRotation;
            Vector3 eyeMovement = (headRotation * neckPivotToEyes) - neckPivotToEyes;
            Vector3 cameraPos = cam.transform.position + baseRotation * eyeMovement;

            Matrix4x4 rotationMatrix = Matrix4x4.Rotate(Quaternion.Inverse(finalRotation));
            Matrix4x4 translationMatrix = Matrix4x4.Translate(-cameraPos);
            Matrix4x4 viewMatrix = rotationMatrix * translationMatrix;

            viewMatrix.m20 = -viewMatrix.m20;
            viewMatrix.m21 = -viewMatrix.m21;
            viewMatrix.m22 = -viewMatrix.m22;
            viewMatrix.m23 = -viewMatrix.m23;

            cam.worldToCameraMatrix = viewMatrix;
        }

        /// <summary>
        /// Applies head tracking rotation using Euler angles with a neck pivot offset.
        /// </summary>
        /// <param name="cam">The camera to modify.</param>
        /// <param name="yaw">Head tracking yaw in degrees (positive = look right).</param>
        /// <param name="pitch">Head tracking pitch in degrees (positive = look up).</param>
        /// <param name="roll">Head tracking roll in degrees (positive = tilt left).</param>
        /// <param name="neckPivotToEyes">Offset from neck pivot to eyes in local space (0, up, forward).</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void ApplyHeadRotation(Camera cam, float yaw, float pitch, float roll, Vector3 neckPivotToEyes)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            Quaternion headRotation = Quaternion.Euler(pitch, yaw, -roll);

            cam.ResetWorldToCameraMatrix();
            Matrix4x4 gameViewMatrix = cam.worldToCameraMatrix;

            // Neck model: eyes orbit around the neck pivot when the head rotates.
            Vector3 eyeMovement = (headRotation * neckPivotToEyes) - neckPivotToEyes;
            Quaternion baseRotation = cam.transform.rotation;
            Vector3 neckWorldOffset = baseRotation * eyeMovement;

            Matrix4x4 headRotMatrix = Matrix4x4.Rotate(headRotation);
            Matrix4x4 neckTranslation = Matrix4x4.Translate(cam.worldToCameraMatrix.MultiplyVector(-neckWorldOffset));

            cam.worldToCameraMatrix = neckTranslation * headRotMatrix * gameViewMatrix;
        }

        /// <summary>
        /// Sets the view matrix to match the camera's current transform.
        /// Use this to keep the camera in "manual matrix mode" when head tracking is disabled,
        /// ensuring consistent behavior (some games behave differently when matrix is auto vs manual).
        /// </summary>
        /// <param name="cam">The camera to sync.</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void SyncMatrixToTransform(Camera cam)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            // Compute view matrix from current transform
            Matrix4x4 camToWorld = cam.transform.localToWorldMatrix;
            Matrix4x4 viewMatrix = camToWorld.inverse;

            // Unity cameras use -Z as forward, so flip the third row
            viewMatrix.m20 = -viewMatrix.m20;
            viewMatrix.m21 = -viewMatrix.m21;
            viewMatrix.m22 = -viewMatrix.m22;
            viewMatrix.m23 = -viewMatrix.m23;

            cam.worldToCameraMatrix = viewMatrix;
        }

        /// <summary>
        /// Resets the camera's view matrix to its default state (auto-calculated from transform).
        /// Call this when transitioning to menus or disabling head tracking.
        /// Note: This exits "manual matrix mode" - Unity will auto-update the matrix each frame.
        /// </summary>
        /// <param name="cam">The camera to reset.</param>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static void Reset(Camera cam)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            cam.ResetWorldToCameraMatrix();
        }

        /// <summary>
        /// Gets the current head rotation contribution from a modified view matrix.
        /// Useful for debugging or when you need to know what rotation was applied.
        /// </summary>
        /// <param name="cam">The camera to query.</param>
        /// <returns>The head rotation that would need to be applied to get from default to current matrix.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Quaternion GetAppliedRotation(Camera cam)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam), "Camera cannot be null");
            }

            // Get current modified matrix
            Matrix4x4 currentMatrix = cam.worldToCameraMatrix;

            // Get what the default matrix would be
            cam.ResetWorldToCameraMatrix();
            Matrix4x4 defaultMatrix = cam.worldToCameraMatrix;

            // Restore the modified matrix
            cam.worldToCameraMatrix = currentMatrix;

            // Calculate the difference: current = headRot * default
            // So: headRot = current * inverse(default)
            Matrix4x4 headRotMatrix = currentMatrix * defaultMatrix.inverse;

            return headRotMatrix.rotation;
        }
    }
}
