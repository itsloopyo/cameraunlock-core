using UnityEngine;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Extension methods for converting between CameraUnlock.Core types and Unity types.
    /// </summary>
    public static class UnityTypeExtensions
    {
        /// <summary>
        /// Converts a CameraUnlock Quat4 to a Unity Quaternion.
        /// </summary>
        public static Quaternion ToUnity(this Quat4 q)
        {
            return new Quaternion(q.X, q.Y, q.Z, q.W);
        }

        /// <summary>
        /// Converts a Unity Quaternion to a CameraUnlock Quat4.
        /// </summary>
        public static Quat4 ToQuat4(this Quaternion q)
        {
            return new Quat4(q.x, q.y, q.z, q.w);
        }

        /// <summary>
        /// Converts a CameraUnlock Vec3 to a Unity Vector3.
        /// </summary>
        public static Vector3 ToUnity(this Vec3 v)
        {
            return new Vector3(v.X, v.Y, v.Z);
        }

        /// <summary>
        /// Converts a Unity Vector3 to a CameraUnlock Vec3.
        /// </summary>
        public static Vec3 ToVec3(this Vector3 v)
        {
            return new Vec3(v.x, v.y, v.z);
        }

        /// <summary>
        /// Converts a TrackingPose to a Unity Vector3 (pitch, yaw, roll order for Euler).
        /// </summary>
        public static Vector3 ToEulerVector(this TrackingPose pose)
        {
            return new Vector3(pose.Pitch, pose.Yaw, pose.Roll);
        }

        /// <summary>
        /// Rotates a Unity Vector3 by a CameraUnlock Quat4.
        /// </summary>
        public static Vector3 Rotate(this Quat4 q, Vector3 v)
        {
            return q.ToUnity() * v;
        }
    }
}
