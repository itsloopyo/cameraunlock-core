using CameraUnlock.Core.Data;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Static utility for projecting tracker position offsets into Unity world space.
    /// Two modes: horizon-locked (offset stays level regardless of camera pitch)
    /// and camera-local (offset follows where you're looking).
    ///
    /// Both take the offset in the library's convention, straight off
    /// <see cref="CameraUnlock.Core.Processing.PositionProcessor"/>: NEGATIVE z is the
    /// forward lean. Unity's transform +z is forward, so the flip happens here, at the
    /// engine boundary, and a caller must not pre-flip it with InvertZ - inversion is
    /// applied before the processor's clamp, which would hand the forward lean the tight
    /// backward budget (0.10m) and the backward lean the generous forward one (0.40m).
    /// </summary>
    public static class PositionApplicator
    {
        /// <summary>
        /// Projects a tracker position offset into world space using horizon-locked axes.
        /// X = lateral (flatRight), Y = vertical (world up), Z = depth (flatForward).
        /// The horizontal forward is derived from <paramref name="gameRotation"/> with Y zeroed,
        /// so leaning left/right stays on the horizon even when the camera is pitched.
        /// </summary>
        /// <param name="offset">Processor-space position offset (X = right, Y = up, NEGATIVE Z = forward).</param>
        /// <param name="gameRotation">The game camera's world rotation (before head tracking).</param>
        /// <returns>World-space offset vector to add to the camera position.</returns>
        public static Vector3 ToHorizonLockedWorld(Vec3 offset, Quaternion gameRotation)
        {
            Vector3 flatForward = gameRotation * Vector3.forward;
            flatForward.y = 0f;
            float len = flatForward.magnitude;
            if (len > 0.001f)
                flatForward /= len;
            else
                flatForward = Vector3.forward;

            Vector3 flatRight = new Vector3(flatForward.z, 0f, -flatForward.x);

            return flatRight * offset.X
                 + Vector3.up * offset.Y
                 + flatForward * -offset.Z;
        }

        /// <summary>
        /// Projects a tracker position offset into world space using the camera's full rotation.
        /// Forward means camera-forward (toward whatever you're looking at), so leaning in
        /// moves toward the viewed object regardless of pitch.
        /// </summary>
        /// <param name="offset">Processor-space position offset (X = right, Y = up, NEGATIVE Z = forward).</param>
        /// <param name="gameRotation">The game camera's world rotation (before head tracking).</param>
        /// <returns>World-space offset vector to add to the camera position.</returns>
        public static Vector3 ToCameraLocalWorld(Vec3 offset, Quaternion gameRotation)
        {
            return gameRotation * new Vector3(offset.X, offset.Y, -offset.Z);
        }
    }
}
