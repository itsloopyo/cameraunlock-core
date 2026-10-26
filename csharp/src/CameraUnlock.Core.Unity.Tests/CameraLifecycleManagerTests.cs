using Xunit;
using UnityEngine;
using CameraUnlock.Core.Unity.Tracking;

namespace CameraUnlock.Core.Unity.Tests
{
    public class CameraLifecycleManagerTests
    {
        private sealed class Manager : CameraLifecycleManager
        {
            public Camera Next;
            public int Lost;

            public void Frame() => LateUpdate();

            protected override Camera FindCamera() => Next;

            protected override Component CreateTrackingHook(Camera camera)
            {
                return new Component { gameObject = camera.gameObject };
            }

            protected override void OnCameraLost() => Lost++;
        }

        [Fact]
        public void ACameraThatGoesAway_IsLostOnce()
        {
            var manager = new Manager { Next = new Camera() };
            manager.Frame();
            manager.Next = null;
            manager.Frame();
            manager.Frame();

            Assert.Equal(1, manager.Lost);
            Assert.False(manager.IsTracking);
        }

        // A scene load destroys the camera and no other comes up: the destroyed camera reads as
        // null, and its loss was never reported, so the subclass's cleanup never ran.
        [Fact]
        public void ACameraDestroyedWithNoneToReplaceIt_IsLost()
        {
            var camera = new Camera();
            var manager = new Manager { Next = camera };
            manager.Frame();
            Object.Destroy(camera);
            manager.Next = null;
            manager.Frame();
            manager.Frame();

            Assert.Equal(1, manager.Lost);
            Assert.True(ReferenceEquals(manager.TrackedCamera, null));
        }
    }
}
