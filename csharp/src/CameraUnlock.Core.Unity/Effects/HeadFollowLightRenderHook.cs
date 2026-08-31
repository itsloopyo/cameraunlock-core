using System;
using CameraUnlock.Core.Unity.Rendering;
using CameraUnlock.Core.Unity.Tracking;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Effects
{
    /// <summary>
    /// Drives a <see cref="HeadFollowLight"/> off Unity's render callbacks, so the light is
    /// turned only for the span of the main camera's render pass and is back on the game's
    /// own rotation before anything else looks at it.
    /// <para>
    /// Subscription goes through <see cref="RenderPipelineHelper"/> rather than
    /// <c>Camera.onPreCull</c> / <c>Camera.onPostRender</c> directly. Those two never fire
    /// under URP or HDRP, so a direct subscription is a silent no-op on any SRP game - the
    /// light simply never turns, with nothing in the log - and an SRP-only Unity 6 build
    /// strips the legacy accessor outright, which makes a direct reference a
    /// MissingMethodException at JIT time. The helper picks the right pair per pipeline.
    /// </para>
    /// <para>
    /// The helper's pre/post pair is a single global registration, so one of these may be
    /// attached at a time per process and <see cref="Attach"/> throws if something else
    /// already holds it. That is deliberate: a second registration silently replacing the
    /// first is how a light ends up stuck rotated with no diagnostic.
    /// </para>
    /// <para>
    /// <see cref="Attach"/> must come after the tracking controller has registered its own
    /// render hook, so that by the time these fire the camera already carries the tracked
    /// matrix this frame's world is rendered with.
    /// </para>
    /// <para>
    /// The light resolver is called every frame rather than cached here. Games destroy and
    /// rebuild the light across a level load or a respawn, and a pointer captured once
    /// survives as a dead reference; a resolver that does its own caching is free to, and
    /// one that does not stays correct.
    /// </para>
    /// </summary>
    public sealed class HeadFollowLightRenderHook
    {
        private readonly ViewMatrixTrackingController _controller;
        private readonly Func<Transform> _resolveLight;
        private readonly Func<bool> _isGameplayActive;
        private readonly HeadFollowLight _light = new HeadFollowLight();
        private bool _attached;

        /// <param name="controller">The controller whose camera and tracked matrix this rides.</param>
        /// <param name="resolveLight">Finds the light's transform, or null when there is none this frame.</param>
        /// <param name="isGameplayActive">
        /// The mod's own gameplay gate. A light turned during a menu, a cutscene or a
        /// loading screen is a light turned against a camera the player is not looking
        /// through.
        /// </param>
        public HeadFollowLightRenderHook(
            ViewMatrixTrackingController controller,
            Func<Transform> resolveLight,
            Func<bool> isGameplayActive)
        {
            if (controller == null) throw new ArgumentNullException("controller");
            if (resolveLight == null) throw new ArgumentNullException("resolveLight");
            if (isGameplayActive == null) throw new ArgumentNullException("isGameplayActive");

            _controller = controller;
            _resolveLight = resolveLight;
            _isGameplayActive = isGameplayActive;
        }

        /// <summary>How far the light turns relative to the head.</summary>
        public float Multiplier
        {
            get { return _light.Multiplier; }
            set { _light.Multiplier = value; }
        }

        public void Attach()
        {
            RenderPipelineHelper.RegisterCallbacks(RotateForRender, RestoreAfterRender);
            _attached = true;
        }

        public void Detach()
        {
            if (_attached)
            {
                RenderPipelineHelper.UnregisterCallbacks();
                _attached = false;
            }
            _light.Restore();
        }

        private void RotateForRender(Camera cam)
        {
            // Defensive, and it is load-bearing: the post callback below is the only thing
            // that clears the applied flag, and it does not run if the camera is destroyed
            // or the pipeline changes mid-frame. Without this the light stays wedged on a
            // stale rotation and every later frame is ignored, for the rest of the session.
            // Restore is a no-op when nothing is applied.
            _light.Restore();

            if (cam != _controller.MainCamera) return;
            if (!_controller.IsApplyingTracking || !_isGameplayActive()) return;

            _light.ApplyDelta(_resolveLight(), HeadFollowLight.GetAppliedHeadRotation(cam));
        }

        // Restores on ANY camera's post callback, not just the main one. Only the main
        // camera ever applies, and cameras render one at a time - pre, render, post, then
        // the next - so there is nothing to restore early. Testing the camera here instead
        // is what made a mid-frame camera swap wedge the light.
        private void RestoreAfterRender(Camera cam)
        {
            _light.Restore();
        }
    }
}
