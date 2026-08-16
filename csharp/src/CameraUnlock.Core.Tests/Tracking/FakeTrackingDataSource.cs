using CameraUnlock.Core.Data;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tests.Tracking
{
    /// <summary>
    /// Scriptable <see cref="ITrackingDataSource"/> for driving the pipeline without a
    /// socket.
    ///
    /// This exists because the remote half of the smoothing model was untestable: every
    /// owner took the concrete OpenTrackReceiver, and a receiver bound to a UDP port can
    /// only ever be fed from loopback, so every connection-flag assertion in the suite was
    /// Assert.False. Nothing proved that a source reporting a REMOTE connection reaches
    /// either processor, which is the half that selects RemoteSmoothing.
    /// </summary>
    internal sealed class FakeTrackingDataSource : ITrackingDataSource
    {
        private long _timestamp = 1;

        public bool IsReceiving { get; set; } = true;
        public bool IsRemoteConnection { get; set; }
        public bool IsFailed { get; set; }
        public bool DataFresh { get; set; } = true;
        public bool RecenterRequested { get; set; }
        public int RecenterCalls { get; private set; }

        public float Yaw { get; set; }
        public float Pitch { get; set; }
        public float Roll { get; set; }

        public float PositionX { get; set; }
        public float PositionY { get; set; }
        public float PositionZ { get; set; }

        /// <summary>Advances the sample timestamp so the pipeline sees a new sample.</summary>
        public void NewSample()
        {
            _timestamp++;
        }

        public bool IsDataFresh(int maxAgeMs = OpenTrackReceiver.DefaultMaxDataAgeMs)
        {
            return DataFresh;
        }

        public TrackingPose GetLatestPose()
        {
            return new TrackingPose(Yaw, Pitch, Roll, _timestamp);
        }

        public PositionData GetLatestPosition()
        {
            return new PositionData(PositionX, PositionY, PositionZ, _timestamp);
        }

        public void GetRawRotation(out float yaw, out float pitch, out float roll)
        {
            yaw = Yaw;
            pitch = Pitch;
            roll = Roll;
        }

        public bool TryConsumeRecenterRequest()
        {
            bool requested = RecenterRequested;
            RecenterRequested = false;
            return requested;
        }

        public void Recenter()
        {
            RecenterCalls++;
        }
    }
}
