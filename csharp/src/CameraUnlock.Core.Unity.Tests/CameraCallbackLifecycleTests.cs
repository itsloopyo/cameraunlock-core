using System;
using Xunit;
using UnityEngine;
using CameraUnlock.Core.Unity.Tracking;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// The class deliberately keeps a static delegate alive across GameObject destruction, and
    /// that delegate is normally an instance method on the plugin MonoBehaviour. Once a scene
    /// transition destroys the plugin, the first Unity property access inside the callback
    /// throws MissingReferenceException - and Unity ABORTS the multicast invocation at the
    /// throwing delegate, so every subscriber registered after it silently stops being called,
    /// other mods' camera hooks included.
    /// </summary>
    public class CameraCallbackLifecycleTests : IDisposable
    {
        public CameraCallbackLifecycleTests()
        {
            Reset();
        }

        public void Dispose()
        {
            Reset();
        }

        private static void Reset()
        {
            CameraCallbackLifecycle.ForceCleanupAll();
            Camera.onPreCull = null;
            Camera.onPreRender = null;
            Canvas.willRenderCanvases = null;
        }

        [Fact]
        public void ForceCleanupAll_ThenDispose_DoesNotTearDownTheNewOwnersCallback()
        {
            // ForceCleanupAll is static and cannot reach instances, so `first` went on
            // believing it owned the slot. Its Dispose then unregistered SECOND's
            // callback - a mod shutting down silently killed a live overlay's camera
            // hook, which is the failure this whole class exists to prevent.
            var first = new CameraCallbackLifecycle();
            first.RegisterPreCull(cam => { });

            CameraCallbackLifecycle.ForceCleanupAll();

            int secondCalls = 0;
            var second = new CameraCallbackLifecycle();
            second.RegisterPreCull(cam => secondCalls++);

            first.Dispose();

            Assert.True(second.HasPreCull);
            Assert.NotNull(Camera.onPreCull);
            Camera.onPreCull(new Camera());
            Assert.Equal(1, secondCalls);
        }

        [Fact]
        public void ForceCleanupAll_ClearsTheOriginalOwnersHasFlag()
        {
            var lifecycle = new CameraCallbackLifecycle();
            lifecycle.RegisterPreCull(cam => { });
            Assert.True(lifecycle.HasPreCull);

            CameraCallbackLifecycle.ForceCleanupAll();

            // It no longer owns anything, and must not claim otherwise.
            Assert.False(lifecycle.HasPreCull);
        }

        [Fact]
        public void SecondLifecycleCanClaimEverySlotAfterForceCleanupAll()
        {
            var first = new CameraCallbackLifecycle();
            first.RegisterPreCull(cam => { });
            first.RegisterPreRender(cam => { });
            first.RegisterWillRenderCanvases(() => { });

            CameraCallbackLifecycle.ForceCleanupAll();

            var second = new CameraCallbackLifecycle();
            second.RegisterPreCull(cam => { });
            second.RegisterPreRender(cam => { });
            second.RegisterWillRenderCanvases(() => { });

            first.Dispose();

            Assert.True(second.HasPreCull);
            Assert.True(second.HasPreRender);
            Assert.True(second.HasWillRenderCanvases);
        }

        [Fact]
        public void PreCull_LiveOwner_InvokesTheCallback()
        {
            var owner = new UnityEngine.Object();
            int calls = 0;

            new CameraCallbackLifecycle().RegisterPreCull(cam => calls++, owner);
            Camera.onPreCull(new Camera());

            Assert.Equal(1, calls);
        }

        [Fact]
        public void PreCull_NoOwnerSupplied_KeepsInvoking()
        {
            int calls = 0;

            new CameraCallbackLifecycle().RegisterPreCull(cam => calls++);
            Camera.onPreCull(new Camera());
            Camera.onPreCull(new Camera());

            Assert.Equal(2, calls);
        }

        [Fact]
        public void PreCull_DestroyedOwner_StopsInvokingAndUnsubscribes()
        {
            var owner = new UnityEngine.Object();
            int calls = 0;

            new CameraCallbackLifecycle().RegisterPreCull(cam => calls++, owner);
            Camera.onPreCull(new Camera());
            Assert.Equal(1, calls);

            owner.MarkDestroyed();
            Camera.onPreCull(new Camera());

            Assert.Equal(1, calls);
            Assert.Null(Camera.onPreCull);
        }

        [Fact]
        public void PreCull_DestroyedOwner_LeavesLaterSubscribersRunning()
        {
            var owner = new UnityEngine.Object();

            new CameraCallbackLifecycle().RegisterPreCull(
                cam => throw new InvalidOperationException("a dead owner's callback must never run"),
                owner);

            int otherModCalls = 0;
            Camera.onPreCull += cam => otherModCalls++;

            owner.MarkDestroyed();
            Camera.onPreCull(new Camera());

            Assert.Equal(1, otherModCalls);
        }

        [Fact]
        public void PreCull_UnregisterAfterOwnerDied_AllowsAFreshRegistration()
        {
            var owner = new UnityEngine.Object();
            var lifecycle = new CameraCallbackLifecycle();
            lifecycle.RegisterPreCull(cam => { }, owner);

            owner.MarkDestroyed();
            Camera.onPreCull(new Camera());
            lifecycle.UnregisterPreCull();

            int calls = 0;
            new CameraCallbackLifecycle().RegisterPreCull(cam => calls++);
            Camera.onPreCull(new Camera());

            Assert.Equal(1, calls);
        }

        [Fact]
        public void PreRender_DestroyedOwner_StopsInvoking()
        {
            var owner = new UnityEngine.Object();
            int calls = 0;

            new CameraCallbackLifecycle().RegisterPreRender(cam => calls++, owner);
            Camera.onPreRender(new Camera());
            Assert.Equal(1, calls);

            owner.MarkDestroyed();
            Camera.onPreRender(new Camera());

            Assert.Equal(1, calls);
        }

        [Fact]
        public void WillRenderCanvases_DestroyedOwner_StopsInvoking()
        {
            var owner = new UnityEngine.Object();
            int calls = 0;

            new CameraCallbackLifecycle().RegisterWillRenderCanvases(() => calls++, owner);
            Canvas.willRenderCanvases();
            Assert.Equal(1, calls);

            owner.MarkDestroyed();
            Canvas.willRenderCanvases();

            Assert.Equal(1, calls);
        }

        [Fact]
        public void RegisterPreCull_Twice_Throws()
        {
            new CameraCallbackLifecycle().RegisterPreCull(cam => { });

            Assert.Throws<InvalidOperationException>(
                () => new CameraCallbackLifecycle().RegisterPreCull(cam => { }));
        }

        [Fact]
        public void Dispose_UnregistersTheCallback()
        {
            int calls = 0;
            var lifecycle = new CameraCallbackLifecycle();
            lifecycle.RegisterPreCull(cam => calls++);

            lifecycle.Dispose();

            Assert.Null(Camera.onPreCull);
            Assert.Equal(0, calls);
        }
    }
}
