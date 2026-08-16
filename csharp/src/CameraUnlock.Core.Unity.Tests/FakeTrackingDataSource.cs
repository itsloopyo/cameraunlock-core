using CameraUnlock.Core.Data;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// Scriptable <see cref="ITrackingDataSource"/> so the Unity-side controller can be
    /// driven without a socket, and in particular can be told to report a REMOTE
    /// connection. A real receiver bound to a UDP port can only ever be fed from loopback.
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
