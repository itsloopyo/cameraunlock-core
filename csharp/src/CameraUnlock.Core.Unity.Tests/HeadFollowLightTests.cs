using CameraUnlock.Core.Effects;
using CameraUnlock.Core.Unity.Effects;
using UnityEngine;
using Xunit;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// The scaling and the save/restore that every head-follow light in the fleet shares.
    /// The find-the-light half is per game and is not here; see the header note in
    /// cameraunlock/effects/head_follow_light.h for the five shapes it takes.
    /// </summary>
    public class HeadFollowLightTests
    {
        private static float AngleDegrees(Quaternion q)
        {
            float w = q.w < 0f ? -q.w : q.w;
            if (w > 1f) w = 1f;
            return (float)(2.0 * System.Math.Acos(w) * 180.0 / System.Math.PI);
        }

        [Fact]
        public void Scale_LeadsTheHeadByTheMultiplier()
        {
            Quaternion head = Quaternion.Euler(0f, 20f, 0f);
            Quaternion led = HeadFollowLight.Scale(head, HeadFollowLightSettings.DefaultMultiplier);
            Assert.Equal(30f, AngleDegrees(led), 3);
        }

        [Fact]
        public void Scale_KeepsTheAxis()
        {
            Quaternion head = Quaternion.Euler(0f, 20f, 0f);
            Quaternion led = HeadFollowLight.Scale(head, 1.5f);

            // Same axis means the vector part stays parallel: only y is non-zero, and it
            // keeps the sign the head turn had.
            Assert.Equal(0f, led.x, 5);
            Assert.Equal(0f, led.z, 5);
            Assert.True(led.y * head.y > 0f);
        }

        // A multiplier of one has to be the identity operation, or every mod that pins the
        // beam to the aim gets a quietly different beam from the unmodded game.
        [Fact]
        public void Scale_OfOneIsTheHeadRotationItself()
        {
            Quaternion head = Quaternion.Euler(-8f, 23f, 11f);
            Quaternion led = HeadFollowLight.Scale(head, 1.0f);
            Assert.Equal(head.x, led.x, 5);
            Assert.Equal(head.y, led.y, 5);
            Assert.Equal(head.z, led.z, 5);
            Assert.Equal(head.w, led.w, 5);
        }

        // Zero is a real setting: it pins the beam to the aim, which is what the game does
        // unmodded.
        [Fact]
        public void Scale_OfZeroIsIdentity()
        {
            Quaternion led = HeadFollowLight.Scale(Quaternion.Euler(0f, 20f, 0f), 0f);
            Assert.Equal(1f, led.w, 5);
        }

        // A centred head has no axis to scale about. Returning identity is what stops an
        // indeterminate axis being multiplied up into a beam pointing anywhere.
        [Fact]
        public void Scale_OfIdentityIsIdentity()
        {
            Quaternion led = HeadFollowLight.Scale(Quaternion.identity, 1.5f);
            Assert.Equal(0f, led.x, 6);
            Assert.Equal(0f, led.y, 6);
            Assert.Equal(0f, led.z, 6);
            Assert.Equal(1f, led.w, 6);
        }

        // Combined poses, not one axis at a time: a formula that is right on single axes
        // and wrong on combinations is exactly the bug that survives testing.
        [Fact]
        public void Scale_HoldsOnCombinedPoses()
        {
            Quaternion head = Quaternion.Euler(-12f, 25f, 9f);
            float headAngle = AngleDegrees(head);
            Assert.Equal(headAngle * 1.5f, AngleDegrees(HeadFollowLight.Scale(head, 1.5f)), 2);
        }

        // q and -q are the same rotation. The half-angle read off w alone is not: from the
        // negative hemisphere it measures the long way round, so a 2 degree head turn came
        // back as 177 and the beam pointed behind the player. The result was unit length
        // and finite, so no guard downstream could see it.
        //
        // Reachable in play: GetAppliedHeadRotation multiplies LookRotation's output by the
        // inverse of Transform.rotation, and nothing couples the signs of those two.
        [Theory]
        [InlineData(1f)]
        [InlineData(2f)]
        [InlineData(20f)]
        [InlineData(120f)]
        public void Scale_IsInvariantUnderTheDoubleCover(float yaw)
        {
            Quaternion head = Quaternion.Euler(0f, yaw, 0f);
            var negated = new Quaternion(-head.x, -head.y, -head.z, -head.w);

            Quaternion fromPositive = HeadFollowLight.Scale(head, 1.5f);
            Quaternion fromNegative = HeadFollowLight.Scale(negated, 1.5f);

            Assert.Equal(AngleDegrees(fromPositive), AngleDegrees(fromNegative), 3);
            Assert.Equal(yaw * 1.5f, AngleDegrees(fromPositive), 3);
        }

        // The public entry point takes any Quat4, and dividing by the vector part's own
        // length is what keeps a slightly-off-unit input from coming back as a scaled
        // matrix on a Transform.
        [Fact]
        public void Scale_RenormalisesANonUnitInput()
        {
            Quaternion head = Quaternion.Euler(0f, 20f, 0f);
            var stretched = new Quaternion(head.x * 1.3f, head.y * 1.3f, head.z * 1.3f, head.w * 1.3f);

            Quaternion led = HeadFollowLight.Scale(stretched, 1.5f);
            float length = (float)System.Math.Sqrt(
                led.x * led.x + led.y * led.y + led.z * led.z + led.w * led.w);

            Assert.Equal(1f, length, 4);
            Assert.Equal(30f, AngleDegrees(led), 3);
        }

        // A camera whose view matrix is degenerate hands back no basis at all.
        // Quaternion.LookRotation answers identity for a zero forward rather than failing,
        // and identity there is a full-magnitude delta: the whole camera rotation, scaled
        // up and written to the light.
        [Fact]
        public void GetAppliedHeadRotation_ReportsNothingForADegenerateMatrix()
        {
            var cam = new Camera { worldToCameraMatrix = default(Matrix4x4) };
            cam.transform.rotation = Quaternion.Euler(0f, 90f, 0f);

            Assert.Equal(0f, AngleDegrees(HeadFollowLight.GetAppliedHeadRotation(cam)), 4);
        }

        // The untracked frame: the view matrix agrees with the transform, so there is no
        // head rotation to report.
        [Fact]
        public void GetAppliedHeadRotation_IsIdentityWhenTheViewMatchesTheTransform()
        {
            var cam = new Camera();
            cam.transform.rotation = Quaternion.Euler(10f, 35f, 0f);
            cam.ResetWorldToCameraMatrix();

            Assert.Equal(0f, AngleDegrees(HeadFollowLight.GetAppliedHeadRotation(cam)), 3);
        }

        [Fact]
        public void ApplyDelta_TurnsTheLightAndRestorePutsItBack()
        {
            var light = new Transform { rotation = Quaternion.Euler(0f, 90f, 0f) };
            Quaternion clean = light.rotation;

            var follower = new HeadFollowLight { Multiplier = 1.5f };
            follower.ApplyDelta(light, Quaternion.Euler(0f, 20f, 0f));

            Assert.True(follower.IsApplied);
            Assert.Equal(30f, AngleDegrees(Quaternion.Inverse(clean) * light.rotation), 2);

            follower.Restore();
            Assert.False(follower.IsApplied);
            Assert.Equal(clean.y, light.rotation.y, 5);
            Assert.Equal(clean.w, light.rotation.w, 5);
        }

        // A camera callback that fires twice in one frame must not compound the rotation,
        // nor overwrite the clean value it has to put back.
        [Fact]
        public void ApplyDelta_TwiceInAFrameIsIgnoredTheSecondTime()
        {
            var light = new Transform { rotation = Quaternion.identity };
            var follower = new HeadFollowLight { Multiplier = 1.5f };

            follower.ApplyDelta(light, Quaternion.Euler(0f, 20f, 0f));
            Quaternion afterFirst = light.rotation;
            follower.ApplyDelta(light, Quaternion.Euler(0f, 20f, 0f));

            Assert.Equal(afterFirst.y, light.rotation.y, 6);
            follower.Restore();
            Assert.Equal(1f, light.rotation.w, 5);
        }

        // A post-render callback that never arrives - the camera destroyed or the pipeline
        // swapped mid-frame - must not wedge the light. Applying again has to clear the
        // previous application rather than be ignored, or every later frame is dropped and
        // the beam stays on a stale rotation for the rest of the session.
        [Fact]
        public void ApplyDelta_AfterAMissedRestoreDoesNotWedge()
        {
            var light = new Transform { rotation = Quaternion.identity };
            Quaternion clean = light.rotation;
            var follower = new HeadFollowLight { Multiplier = 1.5f };

            follower.ApplyDelta(light, Quaternion.Euler(0f, 20f, 0f));
            // The restore never runs. The next frame arrives with the flag still set.
            follower.Restore();
            follower.ApplyDelta(light, Quaternion.Euler(0f, 20f, 0f));

            Assert.Equal(30f, AngleDegrees(Quaternion.Inverse(clean) * light.rotation), 2);
            follower.Restore();
            Assert.Equal(1f, light.rotation.w, 5);
        }

        // Safe in a teardown path, where the mod cannot know whether a frame was in flight.
        [Fact]
        public void Restore_WithNothingAppliedDoesNothing()
        {
            var follower = new HeadFollowLight();
            follower.Restore();
            Assert.False(follower.IsApplied);
        }

        [Fact]
        public void ApplyDelta_WithNoLightThisFrameLeavesNothingHeld()
        {
            var follower = new HeadFollowLight();
            follower.ApplyDelta(null, Quaternion.Euler(0f, 20f, 0f));
            Assert.False(follower.IsApplied);
        }
    }
}
