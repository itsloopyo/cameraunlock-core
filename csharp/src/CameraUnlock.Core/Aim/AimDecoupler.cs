using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Aim
{
    /// <summary>
    /// Computes decoupled aim direction from head tracking rotation.
    /// Used to keep aim stable while the camera follows head movement.
    /// </summary>
    public static class AimDecoupler
    {
        /// <summary>
        /// Computes the aim direction using the inverse quaternion method.
        ///
        /// When head tracking is applied to camera:
        ///   finalCameraRotation = trackingRotation * baseCameraRotation
        ///
        /// To get original aim direction in the new camera's frame:
        ///   aimDirection = inverse(trackingRotation) * forward
        ///
        /// This is the most accurate method, matching how the tracking was applied.
        /// </summary>
        /// <param name="trackingRotation">The head tracking rotation that was applied to the camera.</param>
        /// <returns>The aim direction in local space (relative to current camera rotation).</returns>
        public static Vec3 ComputeAimDirectionLocal(Quat4 trackingRotation)
        {
            return trackingRotation.Inverse.Rotate(Vec3.Forward);
        }

        /// <summary>
        /// Computes aim direction using separate yaw/pitch/roll angles.
        /// Uses the standard head tracking rotation order: Yaw(Y) * Pitch(X) * Roll(Z).
        /// </summary>
        /// <param name="yawDegrees">Yaw angle in degrees (horizontal head turn).</param>
        /// <param name="pitchDegrees">Pitch angle in degrees (vertical head tilt).</param>
        /// <param name="rollDegrees">Roll angle in degrees (head tilt side to side).</param>
        /// <returns>The aim direction in local space.</returns>
        public static Vec3 ComputeAimDirectionLocal(float yawDegrees, float pitchDegrees, float rollDegrees)
        {
            Quat4 trackingRotation = QuaternionUtils.FromYawPitchRoll(yawDegrees, pitchDegrees, rollDegrees);
            return trackingRotation.Inverse.Rotate(Vec3.Forward);
        }

        /// <summary>
        /// Computes aim direction (out params version for Unity compatibility).
        /// </summary>
        /// <param name="yawDegrees">Yaw angle in degrees.</param>
        /// <param name="pitchDegrees">Pitch angle in degrees.</param>
        /// <param name="rollDegrees">Roll angle in degrees.</param>
        /// <param name="aimX">Output: X component of aim direction.</param>
        /// <param name="aimY">Output: Y component of aim direction.</param>
        /// <param name="aimZ">Output: Z component of aim direction.</param>
        public static void ComputeAimDirection(
            float yawDegrees, float pitchDegrees, float rollDegrees,
            out float aimX, out float aimY, out float aimZ)
        {
            Vec3 aim = ComputeAimDirectionLocal(yawDegrees, pitchDegrees, rollDegrees);
            aimX = aim.X;
            aimY = aim.Y;
            aimZ = aim.Z;
        }

        /// <summary>
        /// Computes the inverse tracking quaternion for aim direction calculation.
        /// Useful when you need to transform the aim direction into world space yourself.
        ///
        /// Usage in Unity:
        ///   Quat4 inverse = AimDecoupler.ComputeInverseTracking(yaw, pitch, roll);
        ///   Vector3 aimWorld = camera.rotation * new Quaternion(inverse.X, inverse.Y, inverse.Z, inverse.W) * Vector3.forward;
        /// </summary>
        public static Quat4 ComputeInverseTracking(float yawDegrees, float pitchDegrees, float rollDegrees)
        {
            return QuaternionUtils.FromYawPitchRoll(yawDegrees, pitchDegrees, rollDegrees).Inverse;
        }

        /// <summary>
        /// Computes inverse tracking quaternion (out params version for Unity compatibility).
        /// </summary>
        public static void ComputeInverseTracking(
            float yawDegrees, float pitchDegrees, float rollDegrees,
            out float qx, out float qy, out float qz, out float qw)
        {
            Quat4 inv = ComputeInverseTracking(yawDegrees, pitchDegrees, rollDegrees);
            qx = inv.X;
            qy = inv.Y;
            qz = inv.Z;
            qw = inv.W;
        }

    }
}
