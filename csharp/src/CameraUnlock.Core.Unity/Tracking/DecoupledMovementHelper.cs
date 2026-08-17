using System;
using CameraUnlock.Core.Math;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Helper for implementing decoupled look/movement in first-person games.
    ///
    /// Many FPS games use the camera or player body rotation to determine movement direction.
    /// When head tracking is applied to the camera, this couples look direction with movement,
    /// causing the player to walk where they're looking rather than where they're aiming.
    ///
    /// This helper implements the "split transform" pattern:
    /// - Player body transform: Controls movement direction (pure mouse/gamepad aim)
    /// - Camera transform: Controls view direction (aim + head tracking offset)
    ///
    /// Usage:
    /// 1. Track the "pure aim" yaw (mouse input only, without head tracking)
    /// 2. Apply tracking yaw to camera's local rotation (child of player body)
    /// 3. Keep player body at pure aim yaw (what movement system reads)
    /// 4. Apply tracking pitch/roll to player body (they don't affect horizontal movement)
    ///
    /// Prefer the overloads that take the camera's clean local rotation. The others read
    /// <c>localEulerAngles</c>, substitute the yaw component and write it back, and Unity's
    /// Euler getter is not round-trip stable: near a vertical look it can return (91, 180, 180)
    /// for the rotation that read as (89, 0, 0) a frame earlier, and writing that back with a
    /// new yaw snaps the camera through a 180 degree roll it can then latch in.
    ///
    /// Migrating is not a drop-in substitution. The deprecated overloads REPLACE the camera's
    /// local yaw; the replacements COMPOSE with it. They agree only when the clean rotation
    /// has no yaw of its own. If the game keeps local yaw on the camera (recoil, lean, weapon
    /// sway), the replacements preserve it - usually what you want, but it is a behaviour
    /// change. Pass a yaw-free clean rotation to reproduce the old semantics exactly.
    /// </summary>
    public static class DecoupledMovementHelper
    {
        /// <summary>
        /// Applies head tracking in decoupled mode, where tracking yaw doesn't affect movement.
        /// </summary>
        /// <param name="playerBodyTransform">The player body transform (parent, controls movement direction).</param>
        /// <param name="cameraTransform">The camera transform (child, controls view direction).</param>
        /// <param name="pureAimYaw">The pure aim yaw from mouse/gamepad input (degrees, 0-360).</param>
        /// <param name="trackingYaw">Head tracking yaw in degrees.</param>
        /// <param name="trackingPitch">Head tracking pitch in degrees.</param>
        /// <param name="trackingRoll">Head tracking roll in degrees.</param>
        /// <param name="invertPitch">Whether to invert pitch for natural head movement (default: true).</param>
        [Obsolete("Round-trips the camera's localEulerAngles, which Unity does not guarantee to be stable and which flips representation near a vertical look. Use the overload taking the camera's clean local rotation.")]
        public static void ApplyDecoupled(
            Transform playerBodyTransform,
            Transform cameraTransform,
            float pureAimYaw,
            float trackingYaw,
            float trackingPitch,
            float trackingRoll,
            bool invertPitch = true)
        {
            if (playerBodyTransform == null)
            {
                return;
            }

            ApplyBodyRotation(playerBodyTransform, pureAimYaw, trackingPitch, trackingRoll, invertPitch);

            // === CAMERA TRANSFORM ===
            // Apply tracking yaw as local rotation offset on camera
            // This allows looking around without affecting movement direction
            if (cameraTransform != null && cameraTransform != playerBodyTransform)
            {
                var cameraEuler = cameraTransform.localEulerAngles;
                // Keep current pitch (controlled by game's pitch system), add tracking yaw
                cameraTransform.localEulerAngles = new Vector3(cameraEuler.x, trackingYaw, cameraEuler.z);
            }
        }

        /// <summary>
        /// Applies head tracking in decoupled mode, composing the camera's yaw offset with
        /// quaternions against a caller-held clean rotation instead of round-tripping Euler.
        /// </summary>
        /// <param name="playerBodyTransform">The player body transform (parent, controls movement direction).</param>
        /// <param name="cameraTransform">The camera transform (child, controls view direction).</param>
        /// <param name="cleanCameraLocalRotation">The camera's local rotation with no tracking yaw
        /// applied - what the game's own pitch system produced this frame. Any yaw this rotation
        /// carries is composed with, not replaced; the deprecated Euler overload replaced it.</param>
        /// <param name="pureAimYaw">The pure aim yaw from mouse/gamepad input (degrees, 0-360).</param>
        /// <param name="trackingYaw">Head tracking yaw in degrees.</param>
        /// <param name="trackingPitch">Head tracking pitch in degrees.</param>
        /// <param name="trackingRoll">Head tracking roll in degrees.</param>
        /// <param name="invertPitch">Whether to invert pitch for natural head movement (default: true).</param>
        public static void ApplyDecoupled(
            Transform playerBodyTransform,
            Transform cameraTransform,
            Quaternion cleanCameraLocalRotation,
            float pureAimYaw,
            float trackingYaw,
            float trackingPitch,
            float trackingRoll,
            bool invertPitch = true)
        {
            if (playerBodyTransform == null)
            {
                return;
            }

            ApplyBodyRotation(playerBodyTransform, pureAimYaw, trackingPitch, trackingRoll, invertPitch);

            SetCameraYaw(cameraTransform, playerBodyTransform, cleanCameraLocalRotation, trackingYaw);
        }

        /// <summary>
        /// Fades out head tracking smoothly while maintaining decoupled mode.
        /// </summary>
        /// <param name="playerBodyTransform">The player body transform.</param>
        /// <param name="cameraTransform">The camera transform.</param>
        /// <param name="pureAimYaw">The pure aim yaw from mouse/gamepad input (degrees, 0-360).</param>
        /// <param name="lastTrackingYaw">The last tracking yaw that was applied.</param>
        /// <param name="lastTrackingPitch">The last tracking pitch that was applied.</param>
        /// <param name="lastTrackingRoll">The last tracking roll that was applied.</param>
        /// <param name="fadeProgress">Fade progress from 0 (full tracking) to 1 (no tracking).</param>
        /// <param name="invertPitch">Whether pitch was inverted during tracking.</param>
        [Obsolete("Round-trips the camera's localEulerAngles, which Unity does not guarantee to be stable and which flips representation near a vertical look. Use the overload taking the camera's clean local rotation.")]
        public static void ApplyDecoupledFadeOut(
            Transform playerBodyTransform,
            Transform cameraTransform,
            float pureAimYaw,
            float lastTrackingYaw,
            float lastTrackingPitch,
            float lastTrackingRoll,
            float fadeProgress,
            bool invertPitch = true)
        {
            if (playerBodyTransform == null)
            {
                return;
            }

            float fadedYaw = Mathf.Lerp(lastTrackingYaw, 0f, fadeProgress);
            float fadedPitch = Mathf.Lerp(lastTrackingPitch, 0f, fadeProgress);
            float fadedRoll = Mathf.Lerp(lastTrackingRoll, 0f, fadeProgress);

            ApplyBodyRotation(playerBodyTransform, pureAimYaw, fadedPitch, fadedRoll, invertPitch);

            // === CAMERA TRANSFORM ===
            if (cameraTransform != null && cameraTransform != playerBodyTransform)
            {
                var cameraEuler = cameraTransform.localEulerAngles;
                cameraTransform.localEulerAngles = new Vector3(cameraEuler.x, fadedYaw, cameraEuler.z);
            }
        }

        /// <summary>
        /// Fades out head tracking smoothly while maintaining decoupled mode, composing the
        /// camera's yaw offset against a caller-held clean rotation.
        /// </summary>
        /// <param name="playerBodyTransform">The player body transform.</param>
        /// <param name="cameraTransform">The camera transform.</param>
        /// <param name="cleanCameraLocalRotation">The camera's local rotation with no tracking yaw
        /// applied. Any yaw it carries is composed with, not replaced.</param>
        /// <param name="pureAimYaw">The pure aim yaw from mouse/gamepad input (degrees, 0-360).</param>
        /// <param name="lastTrackingYaw">The last tracking yaw that was applied.</param>
        /// <param name="lastTrackingPitch">The last tracking pitch that was applied.</param>
        /// <param name="lastTrackingRoll">The last tracking roll that was applied.</param>
        /// <param name="fadeProgress">Fade progress from 0 (full tracking) to 1 (no tracking).</param>
        /// <param name="invertPitch">Whether pitch was inverted during tracking.</param>
        public static void ApplyDecoupledFadeOut(
            Transform playerBodyTransform,
            Transform cameraTransform,
            Quaternion cleanCameraLocalRotation,
            float pureAimYaw,
            float lastTrackingYaw,
            float lastTrackingPitch,
            float lastTrackingRoll,
            float fadeProgress,
            bool invertPitch = true)
        {
            if (playerBodyTransform == null)
            {
                return;
            }

            float fadedYaw = Mathf.Lerp(lastTrackingYaw, 0f, fadeProgress);
            float fadedPitch = Mathf.Lerp(lastTrackingPitch, 0f, fadeProgress);
            float fadedRoll = Mathf.Lerp(lastTrackingRoll, 0f, fadeProgress);

            ApplyBodyRotation(playerBodyTransform, pureAimYaw, fadedPitch, fadedRoll, invertPitch);

            SetCameraYaw(cameraTransform, playerBodyTransform, cleanCameraLocalRotation, fadedYaw);
        }

        /// <summary>
        /// Resets the camera's local yaw offset to zero (used when tracking ends).
        /// </summary>
        /// <param name="cameraTransform">The camera transform.</param>
        /// <param name="playerBodyTransform">The player body transform (to ensure camera is different).</param>
        [Obsolete("Round-trips the camera's localEulerAngles, which Unity does not guarantee to be stable and which flips representation near a vertical look. Use the overload taking the camera's clean local rotation.")]
        public static void ResetCameraYawOffset(Transform cameraTransform, Transform playerBodyTransform)
        {
            if (cameraTransform != null && cameraTransform != playerBodyTransform)
            {
                var euler = cameraTransform.localEulerAngles;
                cameraTransform.localEulerAngles = new Vector3(euler.x, 0f, euler.z);
            }
        }

        /// <summary>
        /// Resets the camera's local yaw offset to zero by restoring the clean local rotation.
        /// </summary>
        /// <param name="cameraTransform">The camera transform.</param>
        /// <param name="playerBodyTransform">The player body transform (to ensure camera is different).</param>
        /// <param name="cleanCameraLocalRotation">The camera's local rotation with no tracking yaw
        /// applied. Restoring it leaves any yaw it carries intact, where the deprecated overload
        /// forced the camera's local yaw to zero.</param>
        public static void ResetCameraYawOffset(
            Transform cameraTransform,
            Transform playerBodyTransform,
            Quaternion cleanCameraLocalRotation)
        {
            SetCameraYaw(cameraTransform, playerBodyTransform, cleanCameraLocalRotation, 0f);
        }

        private static void ApplyBodyRotation(
            Transform playerBodyTransform,
            float pureAimYaw,
            float pitch,
            float roll,
            bool invertPitch)
        {
            // === PLAYER BODY TRANSFORM ===
            // Keep yaw at pure aim only (this is what movement systems read)
            // Apply pitch and roll here (they don't affect horizontal movement direction)
            float bodyYaw = NormalizeAngle(pureAimYaw);
            float bodyPitch = invertPitch ? -pitch : pitch;

            playerBodyTransform.localRotation = Quaternion.Euler(bodyPitch, bodyYaw, roll);
        }

        private static void SetCameraYaw(
            Transform cameraTransform,
            Transform playerBodyTransform,
            Quaternion cleanCameraLocalRotation,
            float yaw)
        {
            if (cameraTransform == null || cameraTransform == playerBodyTransform)
            {
                return;
            }

            // Unity's Euler(x, y, z) is Qy * Qx * Qz, so a left multiplication by the yaw
            // is what the Euler substitution was reaching for - without ever reading a
            // rotation back as Euler.
            //
            // It is NOT identical, and the difference is the one thing to know when
            // migrating. Writing Euler(ex, yaw, ez) yields Ry(yaw) * Rx * Rz, so it
            // REPLACES whatever yaw the camera's local rotation carried. This composes:
            // for a clean rotation of Ry(gy) * Rx * Rz it yields Ry(yaw + gy) * Rx * Rz,
            // preserving the game's own local yaw (recoil, lean, weapon sway) instead of
            // destroying it. The two agree exactly when the clean rotation has no yaw of
            // its own, which is the common case and the one the parameter asks for.
            cameraTransform.localRotation =
                Quaternion.AngleAxis(yaw, Vector3.up) * cleanCameraLocalRotation;
        }

        /// <summary>
        /// Normalizes an angle to the 0-360 range.
        /// </summary>
        private static float NormalizeAngle(float angle)
        {
            // Modulo rather than a subtract loop: a non-finite angle (reachable from a
            // reflected game field during a scene transition) never decreases and hung the
            // game at 100% CPU.
            float normalized = AngleUtils.NormalizeAngle(angle);
            return normalized < 0f ? normalized + 360f : normalized;
        }

        /// <summary>
        /// Converts an angle from 0-360 to -180 to +180 range.
        /// </summary>
        public static float ToSignedAngle(float angle)
        {
            if (angle > 180f) return angle - 360f;
            return angle;
        }
    }
}
