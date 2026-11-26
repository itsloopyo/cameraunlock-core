using System;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    /// <summary>
    /// Covers tracker-pivot compensation with a NON-IDENTITY rotation. Every other
    /// PositionProcessor test passes <see cref="Quat4.Identity"/>, which makes the
    /// artifact identically zero, so this whole path had no coverage at all - which is
    /// how a sign error that DOUBLED the artifact instead of removing it survived.
    /// </summary>
    public class PivotCompensationTests
    {
        private const float Dt = 1f / 60f;
        private const float ArmMetres = 0.10f;

        // Wide limits: these tests are about the pivot term, not the box clamp.
        private static PositionSettings Unclamped()
        {
            return PositionSettings.Symmetric(
                sensitivityX: 1f, sensitivityY: 1f, sensitivityZ: 1f,
                limitX: 10f, limitY: 10f, limitZ: 10f, limitZBack: 10f,
                localSmoothing: 0f, remoteSmoothing: 0f);
        }

        private static PositionProcessor NewProcessor(float arm)
        {
            return new PositionProcessor
            {
                Settings = Unclamped(),
                TrackerPivotForward = arm
            };
        }

        /// <summary>
        /// The displacement a tracker ACTUALLY reports for a head rotation, derived
        /// independently of the implementation: the face point sits `arm` metres forward
        /// of the neck (forward is -z), and the tracker reports where it moved to.
        /// </summary>
        private static Vec3 ReportedArcFor(Quat4 rotation, float arm)
        {
            Vec3 facePoint = new Vec3(0f, 0f, -arm);
            return rotation.Rotate(facePoint) - facePoint;
        }

        [Theory]
        [InlineData(15f)]
        [InlineData(30f)]
        [InlineData(-30f)]
        [InlineData(60f)]
        public void PureRotation_ArcIsRemoved(float yawDegrees)
        {
            var processor = NewProcessor(ArmMetres);
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(yawDegrees, 0f, 0f);

            // The head only rotated - it did not lean - so the tracker reports exactly
            // the arc and the compensated output must be ~zero.
            Vec3 arc = ReportedArcFor(rotation, ArmMetres);
            var raw = new PositionData(arc.X, arc.Y, arc.Z, 1000L);

            Vec3 result = processor.Process(raw, rotation, Dt);

            Assert.Equal(0f, result.X, precision: 4);
            Assert.Equal(0f, result.Y, precision: 4);
            Assert.Equal(0f, result.Z, precision: 4);
        }

        [Fact]
        public void PitchRotation_ArcIsRemoved()
        {
            var processor = NewProcessor(ArmMetres);
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(0f, 25f, 0f);

            Vec3 arc = ReportedArcFor(rotation, ArmMetres);
            var raw = new PositionData(arc.X, arc.Y, arc.Z, 1000L);

            Vec3 result = processor.Process(raw, rotation, Dt);

            Assert.Equal(0f, result.X, precision: 4);
            Assert.Equal(0f, result.Y, precision: 4);
            Assert.Equal(0f, result.Z, precision: 4);
        }

        [Fact]
        public void GenuineLean_SurvivesCompensation()
        {
            var processor = NewProcessor(ArmMetres);
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);

            // A real 5cm forward lean (negative z) on top of the rotation arc.
            Vec3 lean = new Vec3(0f, 0f, -0.05f);
            Vec3 arc = ReportedArcFor(rotation, ArmMetres);
            var raw = new PositionData(arc.X + lean.X, arc.Y + lean.Y, arc.Z + lean.Z, 1000L);

            Vec3 result = processor.Process(raw, rotation, Dt);

            Assert.Equal(lean.X, result.X, precision: 4);
            Assert.Equal(lean.Z, result.Z, precision: 4);
        }

        [Fact]
        public void WrongSign_WouldDoubleTheArtifact()
        {
            // Pins the actual defect. With the pivot built as +z the artifact came out
            // negated, so `pos - artifact` ADDED it: the output was twice the arc rather
            // than zero. Verified here against the arithmetic rather than the old code.
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Vec3 arc = ReportedArcFor(rotation, ArmMetres);

            Vec3 backwardsPivot = new Vec3(0f, 0f, ArmMetres);
            Vec3 negatedArtifact = rotation.Rotate(backwardsPivot) - backwardsPivot;

            // R(-v) - (-v) == -(R(v) - v), exactly.
            Assert.Equal(-arc.X, negatedArtifact.X, precision: 5);
            Assert.Equal(-arc.Z, negatedArtifact.Z, precision: 5);

            // So subtracting it doubles rather than cancels.
            Vec3 doubled = arc - negatedArtifact;
            Assert.Equal(2f * arc.X, doubled.X, precision: 5);
            Assert.True(System.Math.Abs(doubled.X) > System.Math.Abs(arc.X));
        }

        [Fact]
        public void ArmOfZero_DisablesCompensationEntirely()
        {
            var processor = NewProcessor(0f);
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            var raw = new PositionData(0.02f, 0.01f, -0.03f, 1000L);

            Vec3 result = processor.Process(raw, rotation, Dt);

            Assert.Equal(0.02f, result.X, precision: 4);
            Assert.Equal(0.01f, result.Y, precision: 4);
            Assert.Equal(-0.03f, result.Z, precision: 4);
        }

        [Fact]
        public void DefaultArmIsZero_SoCompensationIsOptIn()
        {
            // The default must not carry a guessed arm length: the correct value depends
            // on the tracker app, and both previous defaults were chosen while the
            // compensation was inverted.
            Assert.Equal(0f, new PositionProcessor().TrackerPivotForward);
        }

        [Fact]
        public void SensitivityDoesNotScaleTheArtifact()
        {
            // The arc is physical, so doubling sensitivity must double the RESIDUAL lean
            // and leave the compensation itself unchanged. If the caller passed a
            // sensitivity-scaled rotation, the arc would be over-removed instead.
            var processor = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(
                    sensitivityX: 2f, sensitivityY: 2f, sensitivityZ: 2f,
                    limitX: 10f, limitY: 10f, limitZ: 10f, limitZBack: 10f,
                    localSmoothing: 0f, remoteSmoothing: 0f),
                TrackerPivotForward = ArmMetres
            };

            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Vec3 lean = new Vec3(0f, 0f, -0.05f);
            Vec3 arc = ReportedArcFor(rotation, ArmMetres);
            var raw = new PositionData(arc.X + lean.X, arc.Y + lean.Y, arc.Z + lean.Z, 1000L);

            Vec3 result = processor.Process(raw, rotation, Dt);

            Assert.Equal(2f * lean.Z, result.Z, precision: 4);
        }

        [Fact]
        public void ClampAppliesBeforeSmoothing_NoWindup()
        {
            // A large input must not drive the smoothing state outside the limits, or the
            // output stays pinned at the limit after the head has already returned.
            var processor = new PositionProcessor
            {
                Settings = PositionSettings.Default,
                TrackerPivotForward = 0f
            };
            var identity = Quat4.Identity;

            // Well outside LimitX (0.30), held long enough to saturate the smoothing.
            for (int i = 0; i < 120; i++)
            {
                processor.Process(new PositionData(5f, 0f, 0f, 1000L + i), identity, Dt);
            }

            // Back to centre. Without the pre-clamp the state sat at ~5 and took many
            // frames of decay before dropping below the limit.
            Vec3 result = processor.Process(new PositionData(0f, 0f, 0f, 2000L), identity, Dt);

            Assert.True(result.X < PositionSettings.Default.LimitX,
                "output should leave the limit immediately, got " +
                result.X.ToString(System.Globalization.CultureInfo.InvariantCulture));
        }
    }
}
