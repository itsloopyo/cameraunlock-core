using Xunit;
using UnityEngine;
using CameraUnlock.Core.Unity.Tracking;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// The end-render callback is not guaranteed to fire for every begin-render callback: the
    /// camera can be disabled between cull and render, something can call Camera.Render()
    /// directly, or a render-texture pass can short-circuit. When that happens the transform is
    /// left holding the tracked pose, and re-capturing it as the next frame's clean base is how
    /// a 15 degree head yaw becomes 30, then 45.
    /// </summary>
    public class TransformFrameStateTests
    {
        private static readonly Quaternion Clean = Quaternion.Euler(0f, 30f, 0f);
        private static readonly Quaternion Tracked = Quaternion.Euler(0f, 45f, 0f);

        [Fact]
        public void BeginFrame_FirstCallOfFrame_CapturesAndAllowsApply()
        {
            var state = new TransformFrameState();
            var transform = new Transform { rotation = Clean, position = new Vector3(1f, 2f, 3f) };

            Assert.True(state.BeginFrame(transform, 1));
            Assert.True(Quaternion.Angle(Clean, state.StoredRotation) < 0.1f);
        }

        [Fact]
        public void BeginFrame_SecondPassOfSameFrame_DoesNotAllowASecondApply()
        {
            var state = new TransformFrameState();
            var transform = new Transform { rotation = Clean };

            state.BeginFrame(transform, 1);
            state.SetRotation(transform, Tracked);

            Assert.False(state.BeginFrame(transform, 1));
        }

        [Fact]
        public void Restore_ReturnsTheCleanRotationAndPosition()
        {
            var state = new TransformFrameState();
            var transform = new Transform { rotation = Clean, position = new Vector3(1f, 2f, 3f) };

            state.BeginFrame(transform, 1);
            state.SetRotation(transform, Tracked);
            state.SetPosition(transform, new Vector3(9f, 9f, 9f));
            state.Restore(transform, 1);

            Assert.True(Quaternion.Angle(Clean, transform.rotation) < 0.1f);
            Assert.Equal(1f, transform.position.x, 4);
            Assert.Equal(2f, transform.position.y, 4);
            Assert.Equal(3f, transform.position.z, 4);
        }

        [Fact]
        public void BeginFrame_AfterASkippedRestore_RestoresBeforeRecapturing()
        {
            var state = new TransformFrameState();
            var transform = new Transform { rotation = Clean };

            state.BeginFrame(transform, 1);
            state.SetRotation(transform, Tracked);
            // Restore(transform, 1) deliberately never runs.

            state.BeginFrame(transform, 2);

            Assert.True(Quaternion.Angle(Clean, state.StoredRotation) < 0.1f);
            Assert.True(Quaternion.Angle(Clean, transform.rotation) < 0.1f);
        }

        [Fact]
        public void BeginFrame_AfterASkippedRestore_DoesNotCompoundAcrossFrames()
        {
            var state = new TransformFrameState();
            var transform = new Transform { rotation = Clean };

            for (int frame = 1; frame <= 10; frame++)
            {
                state.BeginFrame(transform, frame);
                state.SetRotation(transform, Quaternion.Euler(0f, 30f, 0f) * Quaternion.Euler(0f, 15f, 0f));
            }

            state.BeginFrame(transform, 11);

            // Ten skipped restores in a row still leave the base at the game's 30 degrees,
            // not at 30 + 10 * 15.
            Assert.True(Quaternion.Angle(Clean, state.StoredRotation) < 0.1f);
        }

        [Fact]
        public void BeginFrame_AfterASkippedRestore_RestoresPositionToo()
        {
            var state = new TransformFrameState();
            var transform = new Transform { position = new Vector3(1f, 2f, 3f) };

            state.BeginFrame(transform, 1);
            state.SetPosition(transform, new Vector3(1f, 2.5f, 3f));

            state.BeginFrame(transform, 2);

            Assert.Equal(2f, state.StoredPosition.y, 4);
            Assert.Equal(2f, transform.position.y, 4);
        }
    }
}
