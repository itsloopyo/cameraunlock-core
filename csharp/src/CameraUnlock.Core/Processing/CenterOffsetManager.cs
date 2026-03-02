using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Manages the center offset for head tracking recentering.
    /// Performance-critical methods are aggressively inlined on supported frameworks.
    /// </summary>
    public sealed class CenterOffsetManager
    {
        private TrackingPose _centerOffset;
        private Quat4 _centerQuaternionInverse = Quat4.Identity;
        private bool _hasValidCenter;

        /// <summary>
        /// The current center offset.
        /// </summary>
        public TrackingPose CenterOffset => _centerOffset;

        /// <summary>
        /// True if a center has been set.
        /// </summary>
        public bool HasValidCenter => _hasValidCenter;

        /// <summary>
        /// Sets the center offset to the specified pose.
        /// </summary>
        public void SetCenter(TrackingPose pose)
        {
            _centerOffset = new TrackingPose(pose.Yaw, pose.Pitch, pose.Roll, 0);
            _centerQuaternionInverse = QuaternionUtils.FromYawPitchRoll(pose.Yaw, pose.Pitch, pose.Roll).Inverse;
            _hasValidCenter = true;
        }

        /// <summary>
        /// Sets the center offset using individual components.
        /// </summary>
        public void SetCenter(float yaw, float pitch, float roll)
        {
            _centerOffset = new TrackingPose(yaw, pitch, roll, 0);
            _centerQuaternionInverse = QuaternionUtils.FromYawPitchRoll(yaw, pitch, roll).Inverse;
            _hasValidCenter = true;
        }

        /// <summary>
        /// Applies the offset to a pose, returning the relative pose.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public TrackingPose ApplyOffset(TrackingPose pose)
        {
            if (!_hasValidCenter)
            {
                return pose;
            }
            return pose.SubtractOffset(_centerOffset);
        }

        /// <summary>
        /// Applies the offset to individual values.
        /// </summary>
        /// <param name="yaw">Input yaw.</param>
        /// <param name="pitch">Input pitch.</param>
        /// <param name="roll">Input roll.</param>
        /// <param name="outYaw">Output yaw with offset applied.</param>
        /// <param name="outPitch">Output pitch with offset applied.</param>
        /// <param name="outRoll">Output roll with offset applied.</param>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public void ApplyOffset(float yaw, float pitch, float roll, out float outYaw, out float outPitch, out float outRoll)
        {
            if (!_hasValidCenter)
            {
                outYaw = yaw;
                outPitch = pitch;
                outRoll = roll;
                return;
            }
            outYaw = yaw - _centerOffset.Yaw;
            outPitch = pitch - _centerOffset.Pitch;
            outRoll = roll - _centerOffset.Roll;
        }

#if NETSTANDARD2_0
        /// <summary>
        /// Applies the offset to individual values (tuple version for modern runtimes).
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public (float Yaw, float Pitch, float Roll) ApplyOffsetTuple(float yaw, float pitch, float roll)
        {
            ApplyOffset(yaw, pitch, roll, out float y, out float p, out float r);
            return (y, p, r);
        }
#endif

        /// <summary>
        /// Applies the center offset in quaternion space: centerInverse * inputQ.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public Quat4 ApplyOffsetQuat(Quat4 inputQ)
        {
            if (!_hasValidCenter)
            {
                return inputQ;
            }
            return _centerQuaternionInverse * inputQ;
        }

        /// <summary>
        /// Composes an additional relative offset into the existing center.
        /// Used by Recenter() to add the current smoothed rotation as an offset.
        /// </summary>
        public void ComposeAdditionalOffset(Quat4 relativeQ)
        {
            _centerQuaternionInverse = relativeQ.Inverse * _centerQuaternionInverse;
            QuaternionUtils.ToEulerYXZ(_centerQuaternionInverse.Inverse, out float yaw, out float pitch, out float roll);
            _centerOffset = new TrackingPose(yaw, pitch, roll, 0);
            _hasValidCenter = true;
        }

        /// <summary>
        /// Resets the center offset.
        /// </summary>
        public void Reset()
        {
            _centerOffset = default;
            _centerQuaternionInverse = Quat4.Identity;
            _hasValidCenter = false;
        }
    }
}
