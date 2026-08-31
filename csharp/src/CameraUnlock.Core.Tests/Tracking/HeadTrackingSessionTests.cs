// Null-argument constructor tests intentionally pass null literals
#pragma warning disable CS8625

using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    public class HeadTrackingSessionTests : IDisposable
    {
        private const int TestPort = 14245;
        private const float FrameTime = 1f / 60f;

        private readonly OpenTrackReceiver _receiver;
        private readonly TrackingProcessor _processor;
        private readonly PositionProcessor _positionProcessor;
        private readonly HeadTrackingSession _session;

        public HeadTrackingSessionTests()
        {
            _receiver = new OpenTrackReceiver();
            _processor = new TrackingProcessor();
            _positionProcessor = new PositionProcessor();
            _session = new HeadTrackingSession(_receiver, _processor, _positionProcessor);
        }

        public void Dispose()
        {
            _receiver.Dispose();
        }

        [Fact]
        public void Constructor_NullReceiver_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new HeadTrackingSession(null, _processor, _positionProcessor));
        }

        [Fact]
        public void Constructor_NullProcessor_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new HeadTrackingSession(_receiver, null, _positionProcessor));
        }

        [Fact]
        public void Constructor_NullPositionProcessor_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new HeadTrackingSession(_receiver, _processor, null));
        }

        [Fact]
        public void Mode_Default_IsRotationAndPosition()
        {
            Assert.Equal(TrackingMode.RotationAndPosition, _session.Mode);
            Assert.True(_session.RotationActive);
            Assert.True(_session.PositionActive);
        }

        [Fact]
        public void CycleMode_AdvancesThroughAllModesAndWraps()
        {
            Assert.Equal(TrackingMode.RotationOnly, _session.CycleMode());
            Assert.Equal(TrackingMode.PositionOnly, _session.CycleMode());
            Assert.Equal(TrackingMode.RotationAndPosition, _session.CycleMode());
        }

        [Fact]
        public void RotationOnly_DisablesPosition()
        {
            _session.Mode = TrackingMode.RotationOnly;

            Assert.True(_session.RotationActive);
            Assert.False(_session.PositionActive);
        }

        [Fact]
        public void PositionOnly_DisablesRotation()
        {
            _session.Mode = TrackingMode.PositionOnly;

            Assert.False(_session.RotationActive);
            Assert.True(_session.PositionActive);
        }

        [Fact]
        public void Update_NoDataEverReceived_ReturnsFalse()
        {
            bool result = _session.Update(FrameTime);

            Assert.False(result);
            Assert.False(_session.IsHolding);
        }

        [Fact]
        public void Update_WithFreshData_ReturnsTrueAndProducesRotation()
        {
            StartReceiverAndSend(yaw: 30.0, pitch: 10.0, roll: 5.0);

            bool result = _session.Update(FrameTime);

            Assert.True(result);
            Assert.False(_session.IsHolding);
            // Pipeline applies interpolation and smoothing, so just verify the sign and
            // that the pose is non-trivial.
            Assert.True(_session.Rotation.Yaw > 0f);
            Assert.True(_session.Rotation.Pitch > 0f);
        }

        [Fact]
        public void Update_RotationOnlyMode_PositionOffsetIsZero()
        {
            _session.Mode = TrackingMode.RotationOnly;
            StartReceiverAndSend(yaw: 30.0, pitch: 0.0, roll: 0.0);

            _session.Update(FrameTime);

            Assert.Equal(Vec3.Zero, _session.PositionOffset);
        }

        [Fact]
        public void Update_AfterDataLoss_HoldsLastPose()
        {
            StartReceiverAndSend(yaw: 30.0, pitch: 10.0, roll: 0.0);

            // Process fresh data over several frames to settle smoothing.
            for (int i = 0; i < 30; i++)
            {
                _session.Update(FrameTime);
            }
            var heldYaw = _session.Rotation.Yaw;

            // Simulate tracking loss by waiting out the 500ms freshness window.
            Thread.Sleep(600);

            bool result = _session.Update(FrameTime);

            Assert.True(result);
            Assert.True(_session.IsHolding);
            Assert.Equal(heldYaw, _session.Rotation.Yaw);
        }

        [Fact]
        public void Update_AutoRecenters_AfterStabilizationFrames()
        {
            _session.StabilizationFrames = 5;
            _session.AutoRecenterOnConnect = true;
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 40.0, pitch: 0.0, roll: 0.0);

            // Run past the stabilization window; the recenter fires on frame 5 and the
            // pose should collapse toward zero afterwards.
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw) < 5f,
                $"Expected yaw near 0 after auto-recenter, got {_session.Rotation.Yaw}");
        }

        [Fact]
        public void Update_DoesNotAutoRecenter_ByDefault()
        {
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 40.0, pitch: 0.0, roll: 0.0);

            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw - 40f) < 1f,
                $"Expected the incoming pose to pass through uncentered, got {_session.Rotation.Yaw}");
        }

        [Fact]
        public void TrackerSideCenter_WithoutTrailer_LandsAtZero()
        {
            // The opentrack case. opentrack has its own Center bind and no HCAM
            // trailer, so the only thing the session sees is the stream dropping to
            // zero. A session-start center capture would still be subtracting the
            // pre-press pose, parking the view at the negated drift and forcing the
            // player to hit the mod's hotkey as well. Identity by default means the
            // one press in opentrack is enough.
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 40.0, pitch: 20.0, roll: 10.0);
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            SendTestPacket(TestPort, 0.0, 0.0, 0.0);
            WaitForRotation(0f, 0f, 0f);
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw) < 1f,
                $"Expected yaw 0 after the tracker centered itself, got {_session.Rotation.Yaw}");
            Assert.True(System.Math.Abs(_session.Rotation.Pitch) < 1f,
                $"Expected pitch 0 after the tracker centered itself, got {_session.Rotation.Pitch}");
            Assert.True(System.Math.Abs(_session.Rotation.Roll) < 1f,
                $"Expected roll 0 after the tracker centered itself, got {_session.Rotation.Roll}");
        }

        [Fact]
        public void TrackerSideCenter_WithoutTrailer_LandsAtZeroInPosition()
        {
            // Recenter() centers rotation AND position. Rotation landing at zero says
            // nothing about the position center, which is captured separately through
            // PositionProcessor.SetCenter.
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 0.0, pitch: 0.0, roll: 0.0, x: 10.0, y: 5.0, z: -8.0);
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            float uncentered = _session.PositionOffset.X;

            SendTestPacket(TestPort, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
            WaitForRotation(0f, 0f, 0f);
            for (int i = 0; i < 120; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.PositionOffset.X) < 0.005f,
                $"Expected x 0 after the tracker centered itself, got {_session.PositionOffset.X}");
            Assert.True(uncentered > 0.09f && uncentered < 0.11f,
                $"The lean never reached the pipeline, so the assertions above are trivial: {uncentered}");
            Assert.True(System.Math.Abs(_session.PositionOffset.Y) < 0.005f,
                $"Expected y 0 after the tracker centered itself, got {_session.PositionOffset.Y}");
            Assert.True(System.Math.Abs(_session.PositionOffset.Z) < 0.005f,
                $"Expected z 0 after the tracker centered itself, got {_session.PositionOffset.Z}");
        }

        [Fact]
        public void Recenter_ZeroesTheCurrentPoseAndPosition()
        {
            // With no capture on connect, the hotkey is the only thing that creates a
            // mod-side center at all. Gutting Recenter() must not go unnoticed.
            // Pure yaw: centering is quaternion composition, so with pitch and roll
            // also offset the residual does not decompose into an independent yaw
            // delta and the second assertion below would be measuring the wrong thing.
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 40.0, pitch: 0.0, roll: 0.0, x: 10.0, y: 5.0, z: -8.0);
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            _session.Recenter();
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw) < 1f,
                $"Expected yaw 0 after the hotkey recenter, got {_session.Rotation.Yaw}");
            Assert.True(System.Math.Abs(_session.PositionOffset.Z) < 0.005f,
                $"Expected z 0 after the hotkey recenter, got {_session.PositionOffset.Z}");

            // Pin the captured centre itself, not just "output happens to be zero":
            // a further 20 degrees of yaw must read as 20, not 60.
            SendTestPacket(TestPort, 60.0, 0.0, 0.0, 10.0, 5.0, -8.0);
            WaitForData();
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw - 20f) < 1f,
                $"Expected yaw 20 relative to the captured centre, got {_session.Rotation.Yaw}");
        }

        [Fact]
        public void Recenter_DisarmsTheAutomaticRecenter()
        {
            // A deliberate recenter is the definitive answer to where centre is.
            // Left armed, the automatic one fires the moment the player next
            // holds still for long enough and silently replaces it - so holding
            // a pose after a manual recenter must NOT collapse it to zero the
            // way Update_AutoRecenters_AfterStabilizationFrames expects when no
            // one has recentered by hand.
            _session.StabilizationFrames = 5;
            _session.AutoRecenterOnConnect = true;
            _session.Log = _ => { };
            StartReceiverAndSend(yaw: 0.0, pitch: 0.0, roll: 0.0);
            _session.Update(FrameTime);

            _session.Recenter();

            SendTestPacket(TestPort, 40.0, 0.0, 0.0);
            WaitForRotation(40f, 0f, 0f);
            for (int i = 0; i < 60; i++)
            {
                _session.Update(FrameTime);
            }

            Assert.True(System.Math.Abs(_session.Rotation.Yaw) > 20f,
                $"The automatic recenter stole the manual centre: yaw collapsed to {_session.Rotation.Yaw}");
        }

        [Fact]
        public void SmoothingSetters_ReachThePositionProcessorSettings()
        {
            _session.LocalSmoothing = 0.25f;
            _session.RemoteSmoothing = 0.75f;

            Assert.Equal(0.25f, _processor.LocalSmoothing);
            Assert.Equal(0.75f, _processor.RemoteSmoothing);
            Assert.Equal(0.25f, _positionProcessor.Settings.LocalSmoothing);
            Assert.Equal(0.75f, _positionProcessor.Settings.RemoteSmoothing);
        }

        // The two smoothing values live inside PositionSettings, which is assigned
        // wholesale, so without a structural owner a settings assignment silently reset
        // position smoothing while rotation smoothing survived on its own processor. The
        // session owns the pair and recomposes it, so both orders must land identically -
        // a config-reload handler naturally does smoothing first, which was the unsafe
        // order under the old contract.
        private static PositionSettings ProbeSettings()
        {
            return PositionSettings.Symmetric(2f, 2f, 2f, 0.31f, 0.21f, 0.41f, 0.11f, 0f, 0f);
        }

        private void AssertSmoothingSurvivedSettings()
        {
            Assert.Equal(0.25f, _session.LocalSmoothing);
            Assert.Equal(0.75f, _session.RemoteSmoothing);
            Assert.Equal(0.25f, _processor.LocalSmoothing);
            Assert.Equal(0.75f, _processor.RemoteSmoothing);
            Assert.Equal(0.25f, _positionProcessor.Settings.LocalSmoothing);
            Assert.Equal(0.75f, _positionProcessor.Settings.RemoteSmoothing);

            // Everything else in the assigned struct must still have landed.
            Assert.Equal(2f, _positionProcessor.Settings.SensitivityX);
            Assert.Equal(0.31f, _positionProcessor.Settings.LimitX);
            Assert.Equal(0.41f, _positionProcessor.Settings.LimitZ);
            Assert.Equal(0.11f, _positionProcessor.Settings.LimitZBack);
        }

        [Fact]
        public void SmoothingThenPositionSettings_SmoothingSurvives()
        {
            _session.LocalSmoothing = 0.25f;
            _session.RemoteSmoothing = 0.75f;

            _session.PositionSettings = ProbeSettings();

            AssertSmoothingSurvivedSettings();
        }

        [Fact]
        public void PositionSettingsThenSmoothing_SmoothingSurvives()
        {
            _session.PositionSettings = ProbeSettings();

            _session.LocalSmoothing = 0.25f;
            _session.RemoteSmoothing = 0.75f;

            AssertSmoothingSurvivedSettings();
        }

        [Fact]
        public void Update_ReassertsSmoothingOverADirectProcessorWrite()
        {
            // The caller keeps its own reference to the PositionProcessor, so a settings
            // assignment made straight on the processor bypasses the session entirely.
            // The session getter must not be left lying about the effective state.
            _session.LocalSmoothing = 0.25f;
            _session.RemoteSmoothing = 0.75f;
            _positionProcessor.Settings = ProbeSettings();

            StartReceiverAndSend(yaw: 10.0, pitch: 0.0, roll: 0.0);
            _session.Update(FrameTime);

            Assert.Equal(0.25f, _positionProcessor.Settings.LocalSmoothing);
            Assert.Equal(0.75f, _positionProcessor.Settings.RemoteSmoothing);
            Assert.Equal(0.25f, _session.LocalSmoothing);
            Assert.Equal(0.75f, _session.RemoteSmoothing);
        }

        [Fact]
        public void Update_PushesReceiverConnectionFlagOntoBothProcessors()
        {
            // Pre-poison both processors. If Update() merely leaves the flag alone
            // (the ViewMatrixTrackingController bug: never propagated at all), these
            // stay true. A loopback sender is local, so a correct Update overwrites
            // both from the receiver every frame.
            _processor.IsRemoteConnection = true;
            _positionProcessor.IsRemoteConnection = true;

            StartReceiverAndSend(yaw: 10.0, pitch: 0.0, roll: 0.0);
            _session.Update(FrameTime);

            Assert.False(_session.IsRemoteConnection, "A loopback sender is a local connection");
            Assert.False(_processor.IsRemoteConnection, "Rotation processor must be fed from the receiver every update");
            Assert.False(_positionProcessor.IsRemoteConnection, "Position processor must be fed from the receiver every update");
        }

        [Fact]
        public void Update_ReassertsConnectionFlagEveryFrame()
        {
            StartReceiverAndSend(yaw: 10.0, pitch: 0.0, roll: 0.0);
            _session.Update(FrameTime);

            // Something else stomps the flag between frames (a mod writing it itself,
            // a stale cached settings push). The next Update must correct it rather
            // than sampling the connection once and trusting it forever.
            _processor.IsRemoteConnection = true;
            _positionProcessor.IsRemoteConnection = true;

            _session.Update(FrameTime);

            Assert.False(_processor.IsRemoteConnection);
            Assert.False(_positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void Reset_ClearsHeldPose()
        {
            StartReceiverAndSend(yaw: 30.0, pitch: 0.0, roll: 0.0);
            _session.Update(FrameTime);

            _session.Reset();
            // Wait out freshness so Update has neither fresh data nor a held pose.
            Thread.Sleep(600);

            bool result = _session.Update(FrameTime);

            Assert.False(result);
        }

        private void StartReceiverAndSend(double yaw, double pitch, double roll,
            double x = 0.0, double y = 0.0, double z = 0.0)
        {
            Assert.True(_receiver.Start(TestPort), $"Failed to bind test UDP port {TestPort}");
            SendTestPacket(TestPort, yaw, pitch, roll, x, y, z);
            WaitForData();
        }

        private void WaitForData()
        {
            for (int i = 0; i < 50; i++)
            {
                if (_receiver.IsDataFresh()) return;
                Thread.Sleep(10);
            }
            Assert.Fail("Receiver never reported fresh data");
        }

        /// <summary>
        /// Waits until the receiver actually reports the sent rotation, rather than
        /// merely reporting that some data is fresh. A test that sends a second
        /// packet cannot use freshness: the first packet's data is still fresh,
        /// because the Update loop between them costs microseconds of wall time,
        /// so WaitForData returns before the new datagram has landed and the
        /// assertions then run against the previous pose.
        /// </summary>
        private void WaitForRotation(float expectedYaw, float expectedPitch, float expectedRoll)
        {
            for (int i = 0; i < 200; i++)
            {
                _receiver.GetRawRotation(out float yaw, out float pitch, out float roll);
                if (System.Math.Abs(yaw - expectedYaw) < 0.01f &&
                    System.Math.Abs(pitch - expectedPitch) < 0.01f &&
                    System.Math.Abs(roll - expectedRoll) < 0.01f)
                {
                    return;
                }
                Thread.Sleep(5);
            }
            _receiver.GetRawRotation(out float lastYaw, out float lastPitch, out float lastRoll);
            Assert.Fail($"Receiver never reported ({expectedYaw}, {expectedPitch}, {expectedRoll}); last saw ({lastYaw}, {lastPitch}, {lastRoll})");
        }

        /// <summary>
        /// Sends a test OpenTrack packet (48 bytes, 6 doubles: x, y, z, yaw, pitch, roll).
        /// </summary>
        private static void SendTestPacket(int port, double yaw, double pitch, double roll,
            double x = 0.0, double y = 0.0, double z = 0.0)
        {
            byte[] packet = new byte[48];

            Array.Copy(BitConverter.GetBytes(x), 0, packet, 0, 8);
            Array.Copy(BitConverter.GetBytes(y), 0, packet, 8, 8);
            Array.Copy(BitConverter.GetBytes(z), 0, packet, 16, 8);
            Array.Copy(BitConverter.GetBytes(yaw), 0, packet, 24, 8);
            Array.Copy(BitConverter.GetBytes(pitch), 0, packet, 32, 8);
            Array.Copy(BitConverter.GetBytes(roll), 0, packet, 40, 8);

            using (var client = new UdpClient())
            {
                client.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, port));
            }
        }
    }
}
