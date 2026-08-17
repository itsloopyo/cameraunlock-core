using System;
using Xunit;
using UnityEngine;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Unity.Tracking;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// Executable coverage for the Unity-side controller.
    ///
    /// This is the class where a silent connection-flag propagation bug already hid once:
    /// it owns both processors from construction, so a mod routing everything through it
    /// has no other way to feed IsRemoteConnection, and without the push both processors
    /// stay local forever and RemoteSmoothing is dead config. Nothing executed this code
    /// before, because the test assembly referenced only CameraUnlock.Core.
    /// </summary>
    public class ViewMatrixTrackingControllerTests
    {
        private readonly FakeTrackingDataSource _source;
        private readonly TrackingProcessor _processor;
        private readonly PositionProcessor _positionProcessor;
        private readonly PoseInterpolator _interpolator;
        private readonly PositionInterpolator _positionInterpolator;
        private readonly ViewMatrixTrackingController _controller;
        private readonly Camera _camera;

        public ViewMatrixTrackingControllerTests()
        {
            Time.Reset();

            _source = new FakeTrackingDataSource();
            _processor = new TrackingProcessor();
            _positionProcessor = new PositionProcessor();
            _interpolator = new PoseInterpolator();
            _positionInterpolator = new PositionInterpolator();
            _camera = new Camera();

            _controller = new ViewMatrixTrackingController(
                _source, _processor, _interpolator,
                _positionProcessor, _positionInterpolator,
                () => _camera);
        }

        private void Frame()
        {
            Time.AdvanceFrame();
            _controller.ProcessFrame(true);
        }

        [Fact]
        public void ProcessFrame_RemoteSource_PushesFlagOntoBothProcessors()
        {
            _source.IsRemoteConnection = true;
            _processor.IsRemoteConnection = false;
            _positionProcessor.IsRemoteConnection = false;

            Frame();

            Assert.True(_controller.IsRemoteConnection);
            Assert.True(_processor.IsRemoteConnection);
            Assert.True(_positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void ProcessFrame_LocalSource_PushesFlagOntoBothProcessors()
        {
            _source.IsRemoteConnection = false;
            // Pre-poison. A controller that never propagates leaves these true.
            _processor.IsRemoteConnection = true;
            _positionProcessor.IsRemoteConnection = true;

            Frame();

            Assert.False(_controller.IsRemoteConnection);
            Assert.False(_processor.IsRemoteConnection);
            Assert.False(_positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void ProcessFrame_LiveConnectionSwitch_IsTrackedEveryFrame()
        {
            _source.IsRemoteConnection = false;
            Frame();
            Assert.False(_controller.IsRemoteConnection);

            _source.IsRemoteConnection = true;
            _source.NewSample();
            Frame();
            Assert.True(_controller.IsRemoteConnection);
            Assert.True(_processor.IsRemoteConnection);
            Assert.True(_positionProcessor.IsRemoteConnection);

            _source.IsRemoteConnection = false;
            _source.NewSample();
            Frame();
            Assert.False(_controller.IsRemoteConnection);
            Assert.False(_processor.IsRemoteConnection);
            Assert.False(_positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void ProcessFrame_ReassertsFlagAfterSomethingElseStompsIt()
        {
            _source.IsRemoteConnection = false;
            Frame();

            _processor.IsRemoteConnection = true;
            _positionProcessor.IsRemoteConnection = true;

            _source.NewSample();
            Frame();

            Assert.False(_processor.IsRemoteConnection);
            Assert.False(_positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void ProcessFrame_NotReceiving_DoesNotApplyTracking()
        {
            _source.IsReceiving = false;

            Time.AdvanceFrame();
            bool applied = _controller.ProcessFrame(true);

            Assert.False(applied);
        }

        [Fact]
        public void ProcessFrame_Disabled_DoesNotApplyTracking()
        {
            Time.AdvanceFrame();
            bool applied = _controller.ProcessFrame(false);

            Assert.False(applied);
        }

        [Fact]
        public void ProcessFrame_Receiving_AppliesTrackingAndProducesRotation()
        {
            _source.Yaw = 20f;
            _source.Pitch = 8f;

            bool applied = false;
            for (int i = 0; i < 90; i++)
            {
                _source.NewSample();
                Time.AdvanceFrame();
                applied = _controller.ProcessFrame(true);
            }

            Assert.True(applied);
            Assert.True(_controller.IsApplyingTracking);
            // Centering happens on the first frame, so the settled pose sits near zero;
            // what matters is that the pipeline ran end to end without a Unity call
            // throwing.
            Assert.False(float.IsNaN(_controller.LastTrackingYaw));
            Assert.False(float.IsNaN(_controller.LastTrackingPitch));
            Assert.False(float.IsNaN(_controller.LastTrackingRoll));
        }

        [Fact]
        public void ProcessFrame_ConsumesRemoteRecenterAndRaisesTheCallback()
        {
            int callbacks = 0;
            _controller.OnRemoteRecenter = () => callbacks++;

            Frame();

            _source.RecenterRequested = true;
            _source.NewSample();
            Frame();

            Assert.Equal(1, callbacks);
            Assert.False(_source.RecenterRequested);
        }

        [Fact]
        public void ProcessFrame_NoRecenterRequest_DoesNotRaiseTheCallback()
        {
            int callbacks = 0;
            _controller.OnRemoteRecenter = () => callbacks++;

            Frame();
            _source.NewSample();
            Frame();

            Assert.Equal(0, callbacks);
        }

        // The flag reaching the processor is only half of it. This asserts the value the
        // pipeline actually smooths with changes when the connection does.
        [Fact]
        public void ConnectionSwitch_ChangesTheEffectiveSmoothingUsed()
        {
            _processor.LocalSmoothing = 0f;
            _processor.RemoteSmoothing = 0.95f;

            _source.IsRemoteConnection = false;
            Frame();
            Assert.Equal(0f, SmoothingUtils.GetEffectiveSmoothing(
                _processor.LocalSmoothing, _processor.RemoteSmoothing, _processor.IsRemoteConnection));

            _source.IsRemoteConnection = true;
            _source.NewSample();
            Frame();
            Assert.Equal(0.95f, SmoothingUtils.GetEffectiveSmoothing(
                _processor.LocalSmoothing, _processor.RemoteSmoothing, _processor.IsRemoteConnection));
        }

        // worldToCameraMatrix is a sticky override, so the controller has to remember WHICH
        // camera it wrote to. Resetting whatever the resolver returns now leaves the previous
        // camera permanently head-rotated the moment the game switches cameras, and a recenter
        // cannot clear it because that only changes the delta.
        private ViewMatrixTrackingController NewController(Func<Camera> resolver)
        {
            return new ViewMatrixTrackingController(
                _source, _processor, _interpolator, _positionProcessor, _positionInterpolator, resolver);
        }

        private void ApplyFrameTo(ViewMatrixTrackingController controller, Camera cam)
        {
            _source.NewSample();
            Time.AdvanceFrame();
            controller.ProcessFrame(true);
            Camera.onPreCull(cam);
        }

        [Fact]
        public void CameraSwitch_ResetsTheCameraTheMatrixWasActuallyWrittenTo()
        {
            var gameplayCam = new Camera();
            var cutsceneCam = new Camera();
            Camera resolved = gameplayCam;

            _source.Yaw = 20f;
            var controller = NewController(() => resolved);
            controller.Enable();
            try
            {
                ApplyFrameTo(controller, gameplayCam);
                Assert.Equal(0, gameplayCam.ResetWorldToCameraMatrixCalls);

                resolved = cutsceneCam;
                ApplyFrameTo(controller, cutsceneCam);

                Assert.Equal(1, gameplayCam.ResetWorldToCameraMatrixCalls);
                Assert.Equal(0, cutsceneCam.ResetWorldToCameraMatrixCalls);
            }
            finally
            {
                controller.Disable();
            }
        }

        [Fact]
        public void Disable_ResetsTheAppliedCameraNotTheCurrentlyResolvedOne()
        {
            var gameplayCam = new Camera();
            var menuCam = new Camera();
            Camera resolved = gameplayCam;

            _source.Yaw = 20f;
            var controller = NewController(() => resolved);
            controller.Enable();
            ApplyFrameTo(controller, gameplayCam);

            resolved = menuCam;
            Time.AdvanceFrame();
            controller.Disable();

            Assert.Equal(1, gameplayCam.ResetWorldToCameraMatrixCalls);
            Assert.Equal(0, menuCam.ResetWorldToCameraMatrixCalls);
        }

        [Fact]
        public void Disable_AppliedCameraWasDestroyed_DoesNotTouchIt()
        {
            var gameplayCam = new Camera();

            _source.Yaw = 20f;
            var controller = NewController(() => gameplayCam);
            controller.Enable();
            ApplyFrameTo(controller, gameplayCam);

            gameplayCam.MarkDestroyed();
            controller.Disable();

            Assert.Equal(0, gameplayCam.ResetWorldToCameraMatrixCalls);
        }

        [Fact]
        public void ResetState_ClearsAppliedTracking()
        {
            _source.Yaw = 15f;
            Frame();
            _source.NewSample();
            Frame();

            _controller.ResetState();

            Assert.False(_controller.IsApplyingTracking);
            Assert.Equal(0f, _controller.LastTrackingYaw);
            Assert.Equal(0f, _controller.LastTrackingPitch);
            Assert.Equal(0f, _controller.LastTrackingRoll);
        }
    }
}
