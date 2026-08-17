using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Utility class for composing camera rotations with head tracking.
    /// Provides standardized rotation composition patterns used across mods.
    /// </summary>
    public static class CameraRotationComposer
    {
        /// <summary>
        /// Composes a rotation by applying head tracking additively on top of the game's rotation.
        /// This is the most common pattern: yaw in world space, pitch/roll in local space.
        ///
        /// PITCH SIGN: positive headPitch looks UP. The negation below is what establishes that,
        /// because a positive rotation about Unity's +X pitches the nose DOWN. Pair this with
        /// <see cref="GetTrackingOnlyRotationMatchingAdditive"/>, NOT with
        /// <see cref="GetTrackingOnlyRotation"/>, which uses the opposite pitch sign.
        /// </summary>
        /// <param name="gameRotation">The game's current camera rotation.</param>
        /// <param name="headYaw">Head tracking yaw in degrees.</param>
        /// <param name="headPitch">Head tracking pitch in degrees.</param>
        /// <param name="headRoll">Head tracking roll in degrees.</param>
        /// <returns>Combined rotation with head tracking applied additively.</returns>
        public static Quaternion ComposeAdditive(
            Quaternion gameRotation,
            float headYaw,
            float headPitch,
            float headRoll)
        {
            // Yaw is applied in world space (rotate around world up)
            Quaternion yawWorld = Quaternion.AngleAxis(headYaw, Vector3.up);

            // Pitch and roll are applied in local space (relative to current orientation)
            // Note: pitch is negated because positive head pitch is looking up
            Quaternion pitchRollLocal = Quaternion.Euler(-headPitch, 0f, headRoll);

            // Combine: world yaw * game rotation * local pitch/roll
            return yawWorld * gameRotation * pitchRollLocal;
        }

        /// <summary>
        /// Composes a rotation using explicit Y-X-Z axis ordering.
        /// Use this when you have access to clean pitch/yaw values (e.g., from reflection).
        /// This avoids Euler angle gimbal lock issues.
        /// </summary>
        /// <param name="gameYaw">The game's yaw as a quaternion.</param>
        /// <param name="gamePitch">The game's pitch in degrees.</param>
        /// <param name="trackYaw">Head tracking yaw in degrees.</param>
        /// <param name="trackPitch">Head tracking pitch in degrees.</param>
        /// <param name="trackRoll">Head tracking roll in degrees.</param>
        /// <returns>Combined rotation using proper axis chaining.</returns>
        public static Quaternion ComposeYXZ(
            Quaternion gameYaw,
            float gamePitch,
            float trackYaw,
            float trackPitch,
            float trackRoll)
        {
            // Combine game pitch with tracking pitch
            float combinedPitch = gamePitch + trackPitch;

            // Start with game's yaw, add tracking yaw
            Quaternion yawQ = gameYaw * Quaternion.AngleAxis(trackYaw, Vector3.up);

            // Apply combined pitch around local right axis
            Quaternion pitchQ = Quaternion.AngleAxis(combinedPitch, Vector3.right);

            // Apply roll around local forward axis
            Quaternion rollQ = Quaternion.AngleAxis(trackRoll, Vector3.forward);

            // Chain: yaw first, then pitch, then roll
            return yawQ * pitchQ * rollQ;
        }

        /// <summary>
        /// Composes a rotation using explicit Y-X-Z axis ordering with pitch clamping.
        /// Use this when you have access to clean pitch/yaw values and need pitch limits.
        /// </summary>
        /// <param name="gameYaw">The game's yaw as a quaternion.</param>
        /// <param name="gamePitch">The game's pitch in degrees.</param>
        /// <param name="trackYaw">Head tracking yaw in degrees.</param>
        /// <param name="trackPitch">Head tracking pitch in degrees.</param>
        /// <param name="trackRoll">Head tracking roll in degrees.</param>
        /// <param name="minPitch">Minimum pitch angle in degrees.</param>
        /// <param name="maxPitch">Maximum pitch angle in degrees.</param>
        /// <returns>Combined rotation using proper axis chaining with pitch clamped.</returns>
        public static Quaternion ComposeYXZClamped(
            Quaternion gameYaw,
            float gamePitch,
            float trackYaw,
            float trackPitch,
            float trackRoll,
            float minPitch,
            float maxPitch)
        {
            // Combine and clamp pitch
            float combinedPitch = Mathf.Clamp(gamePitch + trackPitch, minPitch, maxPitch);

            // Start with game's yaw, add tracking yaw
            Quaternion yawQ = gameYaw * Quaternion.AngleAxis(trackYaw, Vector3.up);

            // Apply clamped pitch around local right axis
            Quaternion pitchQ = Quaternion.AngleAxis(combinedPitch, Vector3.right);

            // Apply roll around local forward axis
            Quaternion rollQ = Quaternion.AngleAxis(trackRoll, Vector3.forward);

            return yawQ * pitchQ * rollQ;
        }

        /// <summary>
        /// Extracts the tracking-only rotation (without game rotation contribution).
        ///
        /// PITCH SIGN: this uses the RAW pitch sign, matching <see cref="ComposeYXZ"/> and
        /// <see cref="ComposeYXZClamped"/>, where positive pitch looks DOWN. It is the opposite
        /// of <see cref="ComposeAdditive"/>. Deriving a reticle from this while composing the
        /// view with ComposeAdditive gives a reticle that travels down as the view goes up, at
        /// twice the true rate on the vertical axis - use
        /// <see cref="GetTrackingOnlyRotationMatchingAdditive"/> for that pairing.
        /// </summary>
        /// <param name="trackYaw">Head tracking yaw in degrees.</param>
        /// <param name="trackPitch">Head tracking pitch in degrees.</param>
        /// <param name="trackRoll">Head tracking roll in degrees.</param>
        /// <returns>Quaternion representing only the tracking rotation.</returns>
        public static Quaternion GetTrackingOnlyRotation(
            float trackYaw,
            float trackPitch,
            float trackRoll)
        {
            Quaternion yawQ = Quaternion.AngleAxis(trackYaw, Vector3.up);
            Quaternion pitchQ = Quaternion.AngleAxis(trackPitch, Vector3.right);
            Quaternion rollQ = Quaternion.AngleAxis(trackRoll, Vector3.forward);
            return yawQ * pitchQ * rollQ;
        }

        /// <summary>
        /// The tracking-only rotation that pairs with <see cref="ComposeAdditive"/> - the same
        /// composition with an identity game rotation, negated pitch included. Use this when
        /// the view is composed with ComposeAdditive and the reticle/aim offset is derived from
        /// the tracking rotation alone.
        /// </summary>
        /// <param name="trackYaw">Head tracking yaw in degrees.</param>
        /// <param name="trackPitch">Head tracking pitch in degrees (positive looks up).</param>
        /// <param name="trackRoll">Head tracking roll in degrees.</param>
        /// <returns>Quaternion representing only the tracking rotation.</returns>
        public static Quaternion GetTrackingOnlyRotationMatchingAdditive(
            float trackYaw,
            float trackPitch,
            float trackRoll)
        {
            return ComposeAdditive(Quaternion.identity, trackYaw, trackPitch, trackRoll);
        }

        /// <summary>
        /// Composes a rotation for first-person view where the camera follows look direction.
        /// Uses the simple additive pattern with negated pitch for natural head movement.
        /// </summary>
        /// <param name="baseLookRotation">The base look rotation (e.g., from player input).</param>
        /// <param name="headYaw">Head tracking yaw in degrees.</param>
        /// <param name="headPitch">Head tracking pitch in degrees.</param>
        /// <param name="headRoll">Head tracking roll in degrees.</param>
        /// <returns>Combined first-person camera rotation.</returns>
        public static Quaternion ComposeFirstPerson(
            Quaternion baseLookRotation,
            float headYaw,
            float headPitch,
            float headRoll)
        {
            return ComposeAdditive(baseLookRotation, headYaw, headPitch, headRoll);
        }
    }
}
