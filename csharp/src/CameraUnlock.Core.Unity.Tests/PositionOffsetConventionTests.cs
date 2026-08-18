using Xunit;
using UnityEngine;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Unity.Tracking;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// Locks the sign convention every Unity-side position boundary consumes: the offset
    /// arrives straight off PositionProcessor, where NEGATIVE z is the forward lean.
    ///
    /// The two ViewMatrixModifier paths reach the camera through different spaces - view
    /// space for the composed overloads, transform space for the decomposed ones - and they
    /// disagreed on z, so the same offset moved the camera forward in one yaw mode and
    /// backward in the other. Mods papered over the decomposed path with
    /// PositionSettings.InvertZ, which inverts BEFORE the processor's clamp and therefore
    /// handed the forward lean the 0.10m backward budget and the backward lean the 0.40m
    /// forward one.
    /// </summary>
    public class PositionOffsetConventionTests
    {
        private const float ForwardLean = -0.4f;

        /// The world point the view matrix places at the view-space origin.
        private static Vector3 CameraWorldPosition(Camera cam)
        {
            Matrix4x4 inv = cam.worldToCameraMatrix.inverse;
            return new Vector3(inv.m03, inv.m13, inv.m23);
        }

        [Fact]
        public void ComposedPath_MovesTheCameraForwardOnNegativeZ()
        {
            var cam = new Camera();

            ViewMatrixModifier.ApplyHeadRotation(cam, 0f, 0f, 0f, new Vector3(0f, 0f, ForwardLean));

            Vector3 moved = CameraWorldPosition(cam);
            Assert.Equal(0f, moved.x, 4);
            Assert.Equal(0f, moved.y, 4);
            Assert.Equal(0.4f, moved.z, 4);
        }

        [Fact]
        public void DecomposedPath_MovesTheCameraForwardOnNegativeZ()
        {
            var cam = new Camera();

            ViewMatrixModifier.ApplyHeadRotationDecomposed(cam, 0f, 0f, 0f, new Vector3(0f, 0f, ForwardLean));

            Vector3 moved = CameraWorldPosition(cam);
            Assert.Equal(0f, moved.x, 4);
            Assert.Equal(0f, moved.y, 4);
            Assert.Equal(0.4f, moved.z, 4);
        }

        [Fact]
        public void BothPaths_AgreeWhenTheGameCameraIsRotated()
        {
            // Yaw 90 degrees: the game camera faces world +x, so a forward lean has to move
            // the camera along +x in both yaw modes. Toggling WorldSpaceYaw at runtime picks
            // between these two paths, so any disagreement flips the lean mid-session.
            Quaternion facingEast = Quaternion.Euler(0f, 90f, 0f);
            var offset = new Vector3(0f, 0f, ForwardLean);

            var composed = new Camera();
            composed.transform.rotation = facingEast;
            ViewMatrixModifier.ApplyHeadRotation(composed, 0f, 0f, 0f, offset);

            var decomposed = new Camera();
            decomposed.transform.rotation = facingEast;
            ViewMatrixModifier.ApplyHeadRotationDecomposed(decomposed, 0f, 0f, 0f, offset);

            Vector3 a = CameraWorldPosition(composed);
            Vector3 b = CameraWorldPosition(decomposed);

            Assert.Equal(0.4f, a.x, 4);
            Assert.Equal(a.x, b.x, 4);
            Assert.Equal(a.y, b.y, 4);
            Assert.Equal(a.z, b.z, 4);
        }

        [Fact]
        public void PositionApplicator_ProjectsNegativeZAsForward()
        {
            var forwardLean = new Vec3(0f, 0f, ForwardLean);

            Vector3 cameraLocal = PositionApplicator.ToCameraLocalWorld(forwardLean, Quaternion.identity);
            Vector3 horizonLocked = PositionApplicator.ToHorizonLockedWorld(forwardLean, Quaternion.identity);

            Assert.Equal(0.4f, cameraLocal.z, 4);
            Assert.Equal(0.4f, horizonLocked.z, 4);
        }

        [Fact]
        public void PositionApplicator_HorizonLockedKeepsForwardLevelWhenPitched()
        {
            // Pitched 45 degrees down, the horizon-locked lean must stay on the horizontal
            // plane - the flip must not leak into the vertical component.
            Vector3 offset = PositionApplicator.ToHorizonLockedWorld(
                new Vec3(0f, 0f, ForwardLean), Quaternion.Euler(45f, 0f, 0f));

            Assert.Equal(0.4f, offset.z, 4);
            Assert.Equal(0f, offset.y, 4);
        }
    }
}
