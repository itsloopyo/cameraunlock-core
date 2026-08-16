using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    /// <summary>
    /// The remote half of the two-parameter smoothing model. Everything else in the suite
    /// drives a real UDP receiver, which can only ever be fed from loopback, so every other
    /// connection-flag assertion is Assert.False. These are the ones that prove a source
    /// reporting REMOTE actually reaches the processors and actually changes the value the
    /// pipeline smooths with.
    /// </summary>
    public class RemoteConnectionPropagationTests
    {
        private const float FrameTime = 1f / 60f;

        private static HeadTrackingSession NewSession(
            FakeTrackingDataSource source,
            TrackingProcessor processor,
            PositionProcessor positionProcessor)
        {
            return new HeadTrackingSession(source, processor, positionProcessor)
            {
                StabilizationFrames = int.MaxValue
            };
        }

        [Fact]
        public void RemoteSource_FlagReachesBothProcessors()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = true, Yaw = 10f };
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();
            HeadTrackingSession session = NewSession(source, processor, positionProcessor);

            session.Update(FrameTime);

            Assert.True(session.IsRemoteConnection);
            Assert.True(processor.IsRemoteConnection);
            Assert.True(positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void LocalSource_FlagReachesBothProcessors()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = false, Yaw = 10f };
            var processor = new TrackingProcessor { IsRemoteConnection = true };
            var positionProcessor = new PositionProcessor { IsRemoteConnection = true };
            HeadTrackingSession session = NewSession(source, processor, positionProcessor);

            session.Update(FrameTime);

            Assert.False(session.IsRemoteConnection);
            Assert.False(processor.IsRemoteConnection);
            Assert.False(positionProcessor.IsRemoteConnection);
        }

        // A user switching between a local OpenTrack instance and a phone on WiFi must get
        // the other parameter without restarting the game.
        [Fact]
        public void LiveLocalToRemoteToLocalSwitch_IsTrackedEveryFrame()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = false, Yaw = 10f };
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();
            HeadTrackingSession session = NewSession(source, processor, positionProcessor);

            session.Update(FrameTime);
            Assert.False(session.IsRemoteConnection);

            source.IsRemoteConnection = true;
            source.NewSample();
            session.Update(FrameTime);
            Assert.True(session.IsRemoteConnection);
            Assert.True(processor.IsRemoteConnection);
            Assert.True(positionProcessor.IsRemoteConnection);

            source.IsRemoteConnection = false;
            source.NewSample();
            session.Update(FrameTime);
            Assert.False(session.IsRemoteConnection);
            Assert.False(processor.IsRemoteConnection);
            Assert.False(positionProcessor.IsRemoteConnection);
        }

        // The flag reaching the processor is only half the contract. This asserts the
        // selected VALUE actually changes, which is what the user feels.
        [Fact]
        public void ConnectionSwitch_ChangesTheEffectiveRotationSmoothing()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = false };
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();
            HeadTrackingSession session = NewSession(source, processor, positionProcessor);
            session.LocalSmoothing = 0f;
            session.RemoteSmoothing = 0.9f;

            float localStep = StepMagnitude(session, source, processor, isRemote: false);
            float remoteStep = StepMagnitude(session, source, processor, isRemote: true);

            Assert.True(localStep > remoteStep,
                $"A remote connection must smooth harder: local={localStep}, remote={remoteStep}");
            Assert.True(remoteStep > 0f, "Remote smoothing must still track the target");
            Assert.True(localStep < 20f, "Frame interpolation must still apply at smoothing 0");
        }

        private static float StepMagnitude(
            HeadTrackingSession session,
            FakeTrackingDataSource source,
            TrackingProcessor processor,
            bool isRemote)
        {
            source.IsRemoteConnection = isRemote;
            source.Yaw = 0f;
            source.NewSample();
            session.Reset();
            processor.Reset();
            session.Update(FrameTime);

            source.Yaw = 20f;
            source.NewSample();
            session.Update(FrameTime);
            return session.Rotation.Yaw;
        }

        [Fact]
        public void SessionSmoothingValues_SelectThroughGetEffectiveSmoothing()
        {
            var source = new FakeTrackingDataSource { Yaw = 5f };
            var session = NewSession(source, new TrackingProcessor(), new PositionProcessor());
            session.LocalSmoothing = 0.2f;
            session.RemoteSmoothing = 0.8f;

            source.IsRemoteConnection = true;
            session.Update(FrameTime);
            Assert.Equal(0.8f, SmoothingUtils.GetEffectiveSmoothing(
                session.LocalSmoothing, session.RemoteSmoothing, session.IsRemoteConnection));

            source.IsRemoteConnection = false;
            source.NewSample();
            session.Update(FrameTime);
            Assert.Equal(0.2f, SmoothingUtils.GetEffectiveSmoothing(
                session.LocalSmoothing, session.RemoteSmoothing, session.IsRemoteConnection));
        }

        // RemoteRecenter is the helper aimed at mods that hand-wire the pipeline, which is
        // exactly the population with no other component that owns the connection flag.
        [Fact]
        public void RemoteRecenter_PropagatesTheFlagEvenWithNoPendingRequest()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = true };
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();

            bool consumed = RemoteRecenter.TryConsume(
                source, processor, positionProcessor: positionProcessor);

            Assert.False(consumed);
            Assert.True(processor.IsRemoteConnection);
            Assert.True(positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void RemoteRecenter_TracksAConnectionSwitch()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = true };
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();

            RemoteRecenter.TryConsume(source, processor, positionProcessor: positionProcessor);
            Assert.True(processor.IsRemoteConnection);

            source.IsRemoteConnection = false;
            RemoteRecenter.TryConsume(source, processor, positionProcessor: positionProcessor);

            Assert.False(processor.IsRemoteConnection);
            Assert.False(positionProcessor.IsRemoteConnection);
        }

        [Fact]
        public void RemoteRecenter_StillConsumesTheRequestAndCenters()
        {
            var source = new FakeTrackingDataSource
            {
                IsRemoteConnection = true,
                RecenterRequested = true,
                Yaw = 30f,
            };
            var processor = new TrackingProcessor();

            bool consumed = RemoteRecenter.TryConsume(source, processor);

            Assert.True(consumed);
            Assert.False(source.RecenterRequested);
            Assert.True(processor.IsRemoteConnection);
        }

        [Fact]
        public void RemoteSource_PositionProcessorUsesRemoteSmoothing()
        {
            var source = new FakeTrackingDataSource { IsRemoteConnection = true, Yaw = 0f };
            var processor = new TrackingProcessor();
            var positionProcessor = new PositionProcessor();
            HeadTrackingSession session = NewSession(source, processor, positionProcessor);
            session.LocalSmoothing = 0f;
            session.RemoteSmoothing = 0.95f;
            session.PositionSettings =
                PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0f);

            session.Update(FrameTime);
            source.PositionX = 0.5f;
            source.NewSample();
            session.Update(FrameTime);
            float remoteStep = session.PositionOffset.X;

            source.IsRemoteConnection = false;
            source.PositionX = 0f;
            session.Reset();
            source.NewSample();
            session.Update(FrameTime);
            source.PositionX = 0.5f;
            source.NewSample();
            session.Update(FrameTime);
            float localStep = session.PositionOffset.X;

            Assert.True(localStep > remoteStep,
                $"Position smoothing must follow the connection too: local={localStep}, remote={remoteStep}");
        }
    }
}
