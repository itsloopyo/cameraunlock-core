using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    public class PositionProcessorTests
    {
        private const float DeltaTime = 1f / 60f;

        private static PositionData MakePos(float x, float y, float z)
        {
            return new PositionData(x, y, z, 1000);
        }

        [Fact]
        public void Process_InvalidData_ReturnsZero()
        {
            var proc = new PositionProcessor();
            var invalid = new PositionData(1f, 2f, 3f, 0);

            Vec3 result = proc.Process(invalid, Quat4.Identity, DeltaTime);

            Assert.Equal(0f, result.X);
            Assert.Equal(0f, result.Y);
            Assert.Equal(0f, result.Z);
        }

        [Fact]
        public void Process_ZeroInput_ReturnsZero()
        {
            var proc = new PositionProcessor();

            Vec3 result = proc.Process(MakePos(0f, 0f, 0f), Quat4.Identity, DeltaTime);

            Assert.Equal(0f, result.X, precision: 5);
            Assert.Equal(0f, result.Y, precision: 5);
            Assert.Equal(0f, result.Z, precision: 5);
        }

        [Fact]
        public void Centering_SubtractsOffset()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0f),
            };

            // Set center at (0.05, 0.03, 0.02)
            proc.SetCenter(MakePos(0.05f, 0.03f, 0.02f));

            // Input at (0.10, 0.06, 0.04) should produce (0.05, 0.03, 0.02)
            Vec3 result = proc.Process(MakePos(0.10f, 0.06f, 0.04f), Quat4.Identity, DeltaTime);

            Assert.Equal(0.05f, result.X, precision: 4);
            Assert.Equal(0.03f, result.Y, precision: 4);
            Assert.Equal(0.02f, result.Z, precision: 4);
        }

        [Fact]
        public void Sensitivity_ScalesPerAxis()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(2f, 0.5f, 1.5f, 1f, 1f, 1f, 1f, 0f, 0f),
            };

            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, DeltaTime);

            Assert.Equal(0.20f, result.X, precision: 4);
            Assert.Equal(0.05f, result.Y, precision: 4);
            Assert.Equal(0.15f, result.Z, precision: 4);
        }

        [Fact]
        public void Inversion_NegatesPerAxis()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0f, invertX: true, invertY: false, invertZ: true),
            };

            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, DeltaTime);

            Assert.Equal(-0.10f, result.X, precision: 4);
            Assert.Equal(0.10f, result.Y, precision: 4);
            Assert.Equal(-0.10f, result.Z, precision: 4);
        }

        [Fact]
        public void BoxClamp_EnforcesLimits()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 0.05f, 0.03f, 0.08f, 0.08f, 0f, 0f),
            };

            // Input well beyond limits
            Vec3 result = proc.Process(MakePos(0.50f, -0.50f, 0.50f), Quat4.Identity, DeltaTime);

            Assert.Equal(0.05f, result.X, precision: 5);
            Assert.Equal(-0.03f, result.Y, precision: 5);
            Assert.Equal(0.08f, result.Z, precision: 5);
        }

        [Fact]
        public void BoxClamp_NegativeLimits()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 0.10f, 0.10f, 0.10f, 0.10f, 0f, 0f),
            };

            // Negative values should be clamped to -limit
            Vec3 result = proc.Process(MakePos(-0.50f, -0.50f, -0.50f), Quat4.Identity, DeltaTime);

            Assert.Equal(-0.10f, result.X, precision: 5);
            Assert.Equal(-0.10f, result.Y, precision: 5);
            Assert.Equal(-0.10f, result.Z, precision: 5);
        }

        [Fact]
        public void BoxClamp_AsymmetricYLimits()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 0.30f, 0.15f, 0.05f, 0.40f, 0.10f, 0f, 0f),
            };

            Vec3 up = proc.Process(MakePos(0f, 0.50f, 0f), Quat4.Identity, DeltaTime);
            Assert.Equal(0.15f, up.Y, precision: 5);

            proc.ResetSmoothing();
            Vec3 down = proc.Process(MakePos(0f, -0.50f, 0f), Quat4.Identity, DeltaTime);
            Assert.Equal(-0.05f, down.Y, precision: 5);
        }

        // Negative z is the forward lean. The asymmetry exists so the camera has generous
        // forward travel and cannot clip backwards through the player model, so a
        // transposed pair is not a cosmetic bug: it gives forward the backward budget.
        // The C++ suite asserts these same numbers in TestZClampAsymmetry.
        [Fact]
        public void BoxClamp_AsymmetricZLimits_ForwardIsNegativeAndGetsLimitZ()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 0.30f, 0.20f, 0.40f, 0.10f, 0f, 0f),
            };

            Vec3 forward = proc.Process(MakePos(0f, 0f, -5f), Quat4.Identity, DeltaTime);
            Assert.Equal(-0.40f, forward.Z, precision: 5);

            proc.ResetSmoothing();
            Vec3 backward = proc.Process(MakePos(0f, 0f, 5f), Quat4.Identity, DeltaTime);
            Assert.Equal(0.10f, backward.Z, precision: 5);
        }

        [Fact]
        public void BoxClamp_SymmetricFactory_MirrorsLimitYDown()
        {
            var settings = PositionSettings.Symmetric(1f, 1f, 1f, 0.30f, 0.20f, 0.40f, 0.10f, 0f, 0f);

            Assert.Equal(settings.LimitY, settings.LimitYDown);
        }

        [Fact]
        public void Smoothing_ConvergesToTarget()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0.5f, 0.5f),
            };

            // Feed same position for many frames
            Vec3 result = Vec3.Zero;
            for (int i = 0; i < 300; i++)
            {
                result = proc.Process(MakePos(0.10f, 0.05f, 0.08f), Quat4.Identity, DeltaTime);
            }

            // After 300 frames at 60Hz (5 seconds), should converge
            Assert.Equal(0.10f, result.X, precision: 3);
            Assert.Equal(0.05f, result.Y, precision: 3);
            Assert.Equal(0.08f, result.Z, precision: 3);
        }

        [Fact]
        public void Smoothing_LocalConnection_UsesLocalSmoothingVerbatim()
        {
            // Local 0 is not floored; remote 0.9 is much heavier. Same input, same
            // settings, only the connection flag differs.
            var settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0.9f);
            var local = new PositionProcessor { Settings = settings, IsRemoteConnection = false };
            var remote = new PositionProcessor { Settings = settings, IsRemoteConnection = true };

            local.Process(MakePos(0f, 0f, 0f), Quat4.Identity, DeltaTime);
            remote.Process(MakePos(0f, 0f, 0f), Quat4.Identity, DeltaTime);

            Vec3 localStep = local.Process(MakePos(0.10f, 0f, 0f), Quat4.Identity, DeltaTime);
            Vec3 remoteStep = remote.Process(MakePos(0.10f, 0f, 0f), Quat4.Identity, DeltaTime);

            Assert.True(localStep.X > remoteStep.X,
                $"Local (unfloored 0) must react faster than remote 0.9: local={localStep.X}, remote={remoteStep.X}");
            Assert.True(localStep.X > 0f, "Must move toward the target");
            Assert.True(localStep.X < 0.10f, "Frame interpolation must still apply at smoothing 0");
        }

        [Fact]
        public void Smoothing_ConnectionFlipsLocalToRemote_ChangesResponse()
        {
            var settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0.9f);
            var proc = new PositionProcessor { Settings = settings, IsRemoteConnection = false };

            proc.Process(MakePos(0f, 0f, 0f), Quat4.Identity, DeltaTime);
            Vec3 asLocal = proc.Process(MakePos(0.10f, 0f, 0f), Quat4.Identity, DeltaTime);

            proc.ResetSmoothing();
            proc.IsRemoteConnection = true;
            proc.Process(MakePos(0f, 0f, 0f), Quat4.Identity, DeltaTime);
            Vec3 asRemote = proc.Process(MakePos(0.10f, 0f, 0f), Quat4.Identity, DeltaTime);

            Assert.True(asLocal.X > asRemote.X,
                $"Flipping to a remote connection must apply RemoteSmoothing: local={asLocal.X}, remote={asRemote.X}");
        }

        // The sole constructor and the Symmetric factory are the only two ways a value
        // ever gets into PositionSettings, and both take a long run of same-typed floats.
        // A distinct sentinel per slot is what makes an accidental one-slot shift show up
        // as a failure instead of as a plausible-looking number.
        [Fact]
        public void Ctor_PinsEverySlotToItsOwnField()
        {
            var s = new PositionSettings(
                1.01f, 1.02f, 1.03f,
                2.01f, 2.02f, 2.03f, 2.04f, 2.05f,
                3.01f, 3.02f,
                invertX: true, invertY: false, invertZ: true);

            Assert.Equal(1.01f, s.SensitivityX);
            Assert.Equal(1.02f, s.SensitivityY);
            Assert.Equal(1.03f, s.SensitivityZ);
            Assert.Equal(2.01f, s.LimitX);
            Assert.Equal(2.02f, s.LimitY);
            Assert.Equal(2.03f, s.LimitYDown);
            Assert.Equal(2.04f, s.LimitZ);
            Assert.Equal(2.05f, s.LimitZBack);
            Assert.Equal(3.01f, s.LocalSmoothing);
            Assert.Equal(3.02f, s.RemoteSmoothing);
            Assert.True(s.InvertX);
            Assert.False(s.InvertY);
            Assert.True(s.InvertZ);
        }

        [Fact]
        public void Symmetric_PinsEverySlotToItsOwnField_AndMirrorsLimitY()
        {
            var s = PositionSettings.Symmetric(
                1.01f, 1.02f, 1.03f,
                2.01f, 2.02f, 2.04f, 2.05f,
                3.01f, 3.02f,
                invertX: false, invertY: true, invertZ: false);

            Assert.Equal(1.01f, s.SensitivityX);
            Assert.Equal(1.02f, s.SensitivityY);
            Assert.Equal(1.03f, s.SensitivityZ);
            Assert.Equal(2.01f, s.LimitX);
            Assert.Equal(2.02f, s.LimitY);
            Assert.Equal(2.02f, s.LimitYDown);
            Assert.Equal(2.04f, s.LimitZ);
            Assert.Equal(2.05f, s.LimitZBack);
            Assert.Equal(3.01f, s.LocalSmoothing);
            Assert.Equal(3.02f, s.RemoteSmoothing);
            Assert.False(s.InvertX);
            Assert.True(s.InvertY);
            Assert.False(s.InvertZ);
        }

        [Fact]
        public void WithSmoothing_ReplacesOnlyTheSmoothingPair()
        {
            var s = new PositionSettings(
                1.01f, 1.02f, 1.03f,
                2.01f, 2.02f, 2.03f, 2.04f, 2.05f,
                3.01f, 3.02f,
                invertX: true, invertY: true, invertZ: false);

            PositionSettings r = s.WithSmoothing(0.42f, 0.84f);

            Assert.Equal(0.42f, r.LocalSmoothing);
            Assert.Equal(0.84f, r.RemoteSmoothing);
            Assert.Equal(1.01f, r.SensitivityX);
            Assert.Equal(1.02f, r.SensitivityY);
            Assert.Equal(1.03f, r.SensitivityZ);
            Assert.Equal(2.01f, r.LimitX);
            Assert.Equal(2.02f, r.LimitY);
            Assert.Equal(2.03f, r.LimitYDown);
            Assert.Equal(2.04f, r.LimitZ);
            Assert.Equal(2.05f, r.LimitZBack);
            Assert.True(r.InvertX);
            Assert.True(r.InvertY);
            Assert.False(r.InvertZ);
        }

        [Fact]
        public void Default_UsesZeroLocalAndFifteenHundredthsRemote()
        {
            PositionSettings d = PositionSettings.Default;

            Assert.Equal(0f, d.LocalSmoothing);
            Assert.Equal(0.15f, d.RemoteSmoothing);
        }

        [Fact]
        public void Reset_ClearsAllState()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0f, 0f),
            };

            proc.SetCenter(MakePos(0.05f, 0.05f, 0.05f));
            proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, DeltaTime);

            proc.Reset();

            // After reset, center should be zero — same input should give raw values
            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, DeltaTime);
            Assert.Equal(0.10f, result.X, precision: 4);
            Assert.Equal(0.10f, result.Y, precision: 4);
            Assert.Equal(0.10f, result.Z, precision: 4);
        }

        [Fact]
        public void ResetSmoothing_PreservesCenter()
        {
            var proc = new PositionProcessor
            {
                Settings = PositionSettings.Symmetric(1f, 1f, 1f, 1f, 1f, 1f, 1f, 0.5f, 0.5f),
            };

            proc.SetCenter(MakePos(0.05f, 0.05f, 0.05f));
            proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, DeltaTime);

            proc.ResetSmoothing();

            // Center should still be subtracted; first frame after reset should snap to target
            Vec3 result = proc.Process(MakePos(0.15f, 0.15f, 0.15f), Quat4.Identity, DeltaTime);
            Assert.Equal(0.10f, result.X, precision: 4);
            Assert.Equal(0.10f, result.Y, precision: 4);
            Assert.Equal(0.10f, result.Z, precision: 4);
        }
    }
}
