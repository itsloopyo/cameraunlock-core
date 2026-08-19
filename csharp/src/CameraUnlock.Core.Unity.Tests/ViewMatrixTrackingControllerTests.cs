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

        [Fact]
        public void RecenterPressWhileDisabled_IsNotHonouredOnTheNextSession()
        {
            // The receive thread raises the request whenever a trailer press lands, whether
            // or not the mod is applying tracking. Only the applying branch consumed it, so a
            // press made with tracking OFF stayed latched and fired on the first frame of the
            // next session, anchoring it to whichever raw pose arrived first.
            //
            // Consuming it clears the mod-side centre to identity, matching the zeroed
            // stream the app is now sending. See StalePress_ClearsAnEarlierHotkeyCentre.
            _source.RecenterRequested = true;

            Time.AdvanceFrame();
            _controller.ProcessFrame(false);

            Assert.False(_source.RecenterRequested);

            _source.Yaw = 30f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            // The stale press must not have installed a centre: 30 degrees in still reads
            // as 30 degrees out.
            Assert.True(System.Math.Abs(_controller.LastTrackingYaw - 30f) < 1f,
                "a stale press centred the view, got " + _controller.LastTrackingYaw);
        }

        [Fact]
        public void StalePress_ClearsAnEarlierHotkeyCentre()
        {
            // Hotkey recenter installs a centre at 40. The player then presses CENTER on
            // the tracker while the mod is not applying tracking: the app zeroes its own
            // output, so the stream restarts at 0. Left installed, the 40 centre would be
            // subtracted from that zeroed stream and park the view at -40.
            _source.Yaw = 40f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }
            _controller.Recenter();

            _source.Yaw = 0f;
            _source.RecenterRequested = true;
            Time.AdvanceFrame();
            _controller.ProcessFrame(false);

            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw) < 1f,
                "a stale hotkey centre survived the press, got " + _controller.LastTrackingYaw);
        }

        [Fact]
        public void Recenter_ZeroesTheAppliedPosition()
        {
            _source.PositionX = 0.10f;
            _source.PositionY = 0.05f;
            _source.PositionZ = -0.08f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            // 6DOF only engages once a non-zero position sample has arrived, so the
            // uncentred offset must be visible before the recenter means anything.
            Assert.True(System.Math.Abs(_controller.LastTrackingPosition.X - 0.10f) < 0.005f,
                "position never reached the camera, got " + _controller.LastTrackingPosition.X);

            _controller.Recenter();
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingPosition.X) < 0.005f,
                "x did not centre, got " + _controller.LastTrackingPosition.X);
            Assert.True(System.Math.Abs(_controller.LastTrackingPosition.Z) < 0.005f,
                "z did not centre, got " + _controller.LastTrackingPosition.Z);
        }

        [Fact]
        public void RecenterPressWhileEnabled_IsStillHonoured()
        {
            // The drain must not swallow a live press. Guard against "fixing" the stale case
            // by never consuming at all.
            Time.AdvanceFrame();
            _controller.ProcessFrame(true);

            _source.RecenterRequested = true;
            Time.AdvanceFrame();
            _controller.ProcessFrame(true);

            Assert.False(_source.RecenterRequested);
        }

        [Fact]
        public void RemoteRecenterCallback_FiresOnlyForAPressConsumedWhileEnabled()
        {
            int fired = 0;
            _controller.OnRemoteRecenter = () => fired++;

            _source.RecenterRequested = true;
            Time.AdvanceFrame();
            _controller.ProcessFrame(false);
            Assert.Equal(0, fired);

            _source.RecenterRequested = true;
            Time.AdvanceFrame();
            _controller.ProcessFrame(true);
            Assert.Equal(1, fired);
        }

        private void Frame()
        {
            Time.AdvanceFrame();
            _controller.ProcessFrame(true);
        }

        [Fact]
        public void ProcessFrame_DoesNotCaptureCenter_ByDefault()
        {
            _source.Yaw = 40f;

            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw - 40f) < 1f,
                "the incoming pose should pass through uncentered, got " + _controller.LastTrackingYaw);
        }

        [Fact]
        public void TrackerSideCenter_WithoutTrailer_LandsAtZero()
        {
            // The opentrack case. opentrack has its own Center bind and sends no HCAM
            // trailer, so all the controller sees is the stream dropping to zero. A
            // session-start center capture would still be subtracting the pre-press pose,
            // parking the view at the negated drift until the player also hits the mod's
            // recenter hotkey.
            _source.Yaw = 40f;
            _source.Pitch = 20f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            _source.Yaw = 0f;
            _source.Pitch = 0f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw) < 1f,
                "expected yaw 0 after the tracker centered itself, got " + _controller.LastTrackingYaw);
            Assert.True(System.Math.Abs(_controller.LastTrackingPitch) < 1f,
                "expected pitch 0 after the tracker centered itself, got " + _controller.LastTrackingPitch);
        }

        [Fact]
        public void ProcessFrame_AutoRecenterOnConnect_CapturesTheCenter()
        {
            _controller.AutoRecenterOnConnect = true;
            _source.Yaw = 40f;

            // Move mid-ramp. BeginTrackingSession captures 40, then the transition-in
            // completing re-captures on the settled pose, so the second capture is the
            // one that must win.
            for (int i = 0; i < 10; i++)
            {
                _source.NewSample();
                Frame();
            }
            _source.Yaw = 55f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw) < 1f,
                "the settled pose should have become the centre, got " + _controller.LastTrackingYaw);

            _source.Yaw = 75f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw - 20f) < 1f,
                "the captured centre should be 55, got a residual of " + _controller.LastTrackingYaw);
        }

        [Fact]
        public void Recenter_ZeroesTheViewAndPinsTheCentre()
        {
            // With no capture on connect, the hotkey is the only thing that creates a
            // mod-side centre at all.
            _source.Yaw = 40f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            _controller.Recenter();
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw) < 1f,
                "the hotkey recenter did not zero the view, got " + _controller.LastTrackingYaw);

            _source.Yaw = 60f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw - 20f) < 1f,
                "the captured centre should be 40, got a residual of " + _controller.LastTrackingYaw);
        }

        [Fact]
        public void TrailerPress_CentersTheView_WithTheOptInOff()
        {
            // The trailer is the other surviving centring path and it must work on its
            // own, not because a session-start capture happened to run first.
            _source.Yaw = 40f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            _source.RecenterRequested = true;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw) < 1f,
                "the trailer press did not centre the view, got " + _controller.LastTrackingYaw);

            _source.Yaw = 60f;
            for (int i = 0; i < 120; i++)
            {
                _source.NewSample();
                Frame();
            }

            Assert.True(System.Math.Abs(_controller.LastTrackingYaw - 20f) < 1f,
                "the captured centre should be 40, got a residual of " + _controller.LastTrackingYaw);
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
