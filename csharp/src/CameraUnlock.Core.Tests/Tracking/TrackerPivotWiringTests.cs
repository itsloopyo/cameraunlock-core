using System.IO;
using Xunit;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    /// <summary>
    /// TrackerPivotForward and TrackerPivotUp were parsed under ten accepted spellings,
    /// stored on the config, logged as "Config loaded successfully" and then read by
    /// nothing: the session kept its PositionProcessor private and exposed no pivot, so a
    /// mod holding only the session could not apply either key at all.
    /// </summary>
    public class TrackerPivotWiringTests
    {
        private const float Dt = 1f / 60f;

        private static PositionSettings Unclamped()
        {
            return PositionSettings.Symmetric(
                sensitivityX: 1f, sensitivityY: 1f, sensitivityZ: 1f,
                limitX: 10f, limitY: 10f, limitZ: 10f, limitZBack: 10f,
                localSmoothing: 0f, remoteSmoothing: 0f);
        }

        private static HeadTrackingConfigData LoadIni(params string[] lines)
        {
            string path = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName() + ".cfg");
            File.WriteAllLines(path, lines);
            try
            {
                return HeadTrackingConfigData.LoadFromFile(path);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Theory]
        [InlineData("TrackerPivotForward", "PivotUp")]
        [InlineData("PivotForward", "NeckPivotUp")]
        [InlineData("neck_pivot_forward", "neck-model-height")]
        public void ConfigFileKeys_ReachThePositionProcessorThroughTheSession(string forwardKey, string upKey)
        {
            var config = LoadIni(forwardKey + " = 0.10", upKey + " = 0.05");

            Assert.Equal(0.10f, config.TrackerPivotForward, precision: 4);
            Assert.Equal(0.05f, config.TrackerPivotUp, precision: 4);

            var positionProcessor = new PositionProcessor { Settings = Unclamped() };
            var session = new HeadTrackingSession(
                new FakeTrackingDataSource(), new TrackingProcessor(), positionProcessor);

            session.TrackerPivotForward = config.TrackerPivotForward;
            session.TrackerPivotUp = config.TrackerPivotUp;

            Assert.Equal(0.10f, positionProcessor.TrackerPivotForward, precision: 4);
            Assert.Equal(0.05f, positionProcessor.TrackerPivotUp, precision: 4);
            Assert.Equal(0.10f, session.TrackerPivotForward, precision: 4);
            Assert.Equal(0.05f, session.TrackerPivotUp, precision: 4);
        }

        /// <summary>
        /// The wiring changes what the processor computes, not just what it stores: a head
        /// that only rotated reports the arc its face point traced, and the configured
        /// pivot is what removes it.
        /// </summary>
        [Fact]
        public void ConfiguredPivot_RemovesTheRotationArcTheDefaultLeavesIn()
        {
            var config = LoadIni("PivotForward = 0.10");

            var positionProcessor = new PositionProcessor { Settings = Unclamped() };
            var session = new HeadTrackingSession(
                new FakeTrackingDataSource(), new TrackingProcessor(), positionProcessor);

            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Vec3 facePoint = new Vec3(0f, 0f, -0.10f);
            Vec3 arc = rotation.Rotate(facePoint) - facePoint;
            var raw = new PositionData(arc.X, arc.Y, arc.Z, 1000L);

            Vec3 uncompensated = positionProcessor.Process(raw, rotation, Dt);
            Assert.True(System.Math.Abs(uncompensated.X) > 0.01f);

            positionProcessor.Reset();
            session.TrackerPivotForward = config.TrackerPivotForward;

            Vec3 compensated = positionProcessor.Process(raw, rotation, Dt);
            Assert.Equal(0f, compensated.X, precision: 4);
            Assert.Equal(0f, compensated.Y, precision: 4);
            Assert.Equal(0f, compensated.Z, precision: 4);
        }
    }
}
