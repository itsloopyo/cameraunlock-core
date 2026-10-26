using Xunit;
using UnityEngine;
using CameraUnlock.Core.Unity.UI;

namespace CameraUnlock.Core.Unity.Tests
{
    public class UIElementOffsetControllerTests
    {
        private sealed class Controller : UIElementOffsetController
        {
            public int Applied;
            public int Restored;

            public void Frame() => LateUpdate();
            public void Disable() => OnDisable();

            protected override bool TryAcquireTarget() => true;
            protected override void CaptureOriginalState() { }
            protected override void ApplyOffset(Vector2 offset) => Applied++;
            protected override void RestoreOriginalState() => Restored++;
            protected override void ClearReferences() { }
        }

        // LateUpdate stops with the component, so an element left offset stayed where it was.
        [Fact]
        public void DisabledWhileOffset_PutsTheElementBackOnce()
        {
            bool decoupled = true;
            var controller = new Controller
            {
                IsDecouplingActive = () => decoupled,
                GetScreenOffset = () => new Vector2(10f, 5f),
            };
            controller.Frame();
            Assert.Equal(1, controller.Applied);

            controller.Disable();
            Assert.Equal(1, controller.Restored);

            decoupled = false;
            controller.Frame();
            Assert.Equal(1, controller.Restored);
        }

        [Fact]
        public void DisabledWhileNotOffset_LeavesTheElementAlone()
        {
            var controller = new Controller { IsDecouplingActive = () => false };
            controller.Frame();

            controller.Disable();

            Assert.Equal(0, controller.Restored);
        }
    }
}
