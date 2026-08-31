using System;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Effects;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Effects
{
    /// <summary>
    /// Turns a carried light with the head instead of the aim, and puts the game's own
    /// rotation back once the frame is drawn. Unity half of
    /// <see cref="HeadFollowLightSettings"/>; the C++ half is
    /// cameraunlock/effects/head_follow_light.h.
    /// <para>
    /// Rotating and restoring around the render pass is what keeps the rest of the game
    /// reading its own values. It matters more than it looks. In R.E.P.O., the concrete
    /// case this was written against, <c>FlashlightLightAim</c> raycasts along the light
    /// every frame to publish the aim point other players see, and <c>PhysGrabber</c>'s
    /// interaction ray comes off the camera transform; both keep reading the game's own
    /// values because the turn exists only for the span of the render pass. Nothing in the
    /// game's update ever observes a turned light.
    /// </para>
    /// <para>
    /// Only the light is turned, never the held mesh. The player's hand does not move with
    /// their head.
    /// </para>
    /// <para>
    /// Finding the light is the caller's job and does not generalise; see the header note
    /// in the C++ twin for the five shapes the fleet ships. What this class owns is the
    /// scaling, the save and the restore.
    /// </para>
    /// </summary>
    public sealed class HeadFollowLight
    {
        private Transform _transform;
        private Quaternion _cleanRotation;
        private bool _isApplied;

        /// <summary>How far the light turns relative to the head.</summary>
        public float Multiplier { get; set; } = HeadFollowLightSettings.DefaultMultiplier;

        /// <summary>True between an Apply and its Restore.</summary>
        public bool IsApplied
        {
            get { return _isApplied; }
        }

        /// <summary>
        /// Scales a head rotation about its own axis, which is the axis-angle spelling of
        /// an unclamped slerp from identity. The arithmetic is
        /// <see cref="HeadFollowLightSettings.ScaleRotation"/>, shared with every other mod
        /// that holds its head pose as a rotation rather than as three angles.
        /// <para>
        /// Deliberately not <c>Quaternion.SlerpUnclamped</c>, which arrived in Unity 5.0:
        /// this assembly targets net35 for pre-2017.3 Unity, and a method that resolves at
        /// compile time against a reference stub and then is not there in the game is a
        /// MissingMethodException on the render thread.
        /// </para>
        /// </summary>
        public static Quaternion Scale(Quaternion headRotation, float multiplier)
        {
            Quat4 scaled = HeadFollowLightSettings.ScaleRotation(
                new Quat4(headRotation.x, headRotation.y, headRotation.z, headRotation.w),
                multiplier);
            return new Quaternion(scaled.X, scaled.Y, scaled.Z, scaled.W);
        }

        /// <summary>
        /// Turns the light by the scaled head rotation, on top of wherever the game has
        /// already aimed it. Call from the render hook, after the camera's tracked matrix
        /// has been written; pair with <see cref="Restore"/>.
        /// <para>
        /// A second Apply before a Restore is ignored, so a camera callback that fires
        /// twice in a frame cannot compound the rotation or lose the clean value.
        /// </para>
        /// </summary>
        public void ApplyDelta(Transform light, Quaternion headRotation)
        {
            if (_isApplied || light == null) return;

            _transform = light;
            _cleanRotation = light.rotation;
            light.rotation = Scale(headRotation, Multiplier) * _cleanRotation;
            _isApplied = true;
        }

        /// <summary>
        /// Puts the light back on the game's own rotation. Call after rendering. Safe to
        /// call when nothing was applied, which is what makes it correct in a teardown path.
        /// </summary>
        public void Restore()
        {
            if (!_isApplied) return;

            _isApplied = false;
            if (_transform != null)
            {
                _transform.rotation = _cleanRotation;
            }
            _transform = null;
        }

        /// <summary>
        /// The world-space rotation the view was turned by this frame, read back from the
        /// matrix the tracking controller wrote rather than recomposed from the tracking
        /// angles - so it matches whatever composition was applied, world-yaw or
        /// camera-local.
        /// <para>
        /// A Unity view matrix holds the camera's world basis transposed, looking down -Z:
        /// row 0 is right, row 1 is up, row 2 is negated forward.
        /// </para>
        /// </summary>
        public static Quaternion GetAppliedHeadRotation(Camera cam)
        {
            if (cam == null) throw new ArgumentNullException("cam");

            Matrix4x4 view = cam.worldToCameraMatrix;
            var forward = new Vector3(-view.m20, -view.m21, -view.m22);
            var up = new Vector3(view.m10, view.m11, view.m12);

            // A camera whose matrix has been reset, or one rendering a mirror or portal
            // pass with a custom view matrix, does not hand back a rotation basis at all.
            // Quaternion.LookRotation answers identity for a zero forward rather than
            // failing, and identity here is a full-magnitude delta - the whole camera
            // rotation, scaled up and written to the light. Report no rotation instead.
            if (!IsUsableBasis(forward) || !IsUsableBasis(up))
            {
                return Quaternion.identity;
            }

            Quaternion tracked = Quaternion.LookRotation(forward, up);
            return tracked * Quaternion.Inverse(cam.transform.rotation);
        }

        private static bool IsUsableBasis(Vector3 v)
        {
            float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;
            return lengthSquared > 1e-8f
                && !float.IsNaN(lengthSquared)
                && !float.IsInfinity(lengthSquared);
        }
    }
}
