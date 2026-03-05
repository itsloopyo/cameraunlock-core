using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Computes eye position offset from head rotation, simulating that the head
    /// rotates around the neck pivot rather than the eye center.
    /// </summary>
    public static class NeckModel
    {
        /// <summary>
        /// Computes the eye position offset caused by rotating around the neck pivot.
        /// Formula: headRotation.Rotate(neckToEyes) - neckToEyes
        /// When head is neutral (identity), offset is zero.
        /// When head tilts right, eyes move left and slightly down.
        /// </summary>
        public static Vec3 ComputeOffset(Quat4 headRotation, NeckModelSettings settings)
        {
            if (!settings.Enabled)
            {
                return Vec3.Zero;
            }

            Vec3 neckToEyes = settings.NeckToEyes;
            Vec3 rotatedNeckToEyes = headRotation.Rotate(neckToEyes);
            return rotatedNeckToEyes - neckToEyes;
        }
    }
}
