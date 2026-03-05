using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
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

            Vec3 result = proc.Process(invalid, Quat4.Identity, false, DeltaTime);

            Assert.Equal(0f, result.X);
            Assert.Equal(0f, result.Y);
            Assert.Equal(0f, result.Z);
        }

        [Fact]
        public void Process_ZeroInput_NeckModelDisabled_ReturnsZero()
        {
            var proc = new PositionProcessor
            {
                NeckModelSettings = NeckModelSettings.Disabled
            };

            Vec3 result = proc.Process(MakePos(0f, 0f, 0f), Quat4.Identity, false, DeltaTime);

            Assert.Equal(0f, result.X, precision: 5);
            Assert.Equal(0f, result.Y, precision: 5);
            Assert.Equal(0f, result.Z, precision: 5);
        }

        [Fact]
        public void Centering_SubtractsOffset()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 1f, 1f, 1f, 0f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            // Set center at (0.05, 0.03, 0.02)
            proc.SetCenter(MakePos(0.05f, 0.03f, 0.02f));

            // Input at (0.10, 0.06, 0.04) should produce (0.05, 0.03, 0.02)
            Vec3 result = proc.Process(MakePos(0.10f, 0.06f, 0.04f), Quat4.Identity, false, DeltaTime);

            Assert.Equal(0.05f, result.X, precision: 4);
            Assert.Equal(0.03f, result.Y, precision: 4);
            Assert.Equal(0.02f, result.Z, precision: 4);
        }

        [Fact]
        public void Sensitivity_ScalesPerAxis()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(2f, 0.5f, 1.5f, 1f, 1f, 1f, 0f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, false, DeltaTime);

            Assert.Equal(0.20f, result.X, precision: 4);
            Assert.Equal(0.05f, result.Y, precision: 4);
            Assert.Equal(0.15f, result.Z, precision: 4);
        }

        [Fact]
        public void Inversion_NegatesPerAxis()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 1f, 1f, 1f, 0f, invertX: true, invertY: false, invertZ: true),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, false, DeltaTime);

            Assert.Equal(-0.10f, result.X, precision: 4);
            Assert.Equal(0.10f, result.Y, precision: 4);
            Assert.Equal(-0.10f, result.Z, precision: 4);
        }

        [Fact]
        public void BoxClamp_EnforcesLimits()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 0.05f, 0.03f, 0.08f, 0f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            // Input well beyond limits
            Vec3 result = proc.Process(MakePos(0.50f, -0.50f, 0.50f), Quat4.Identity, false, DeltaTime);

            Assert.Equal(0.05f, result.X, precision: 5);
            Assert.Equal(-0.03f, result.Y, precision: 5);
            Assert.Equal(0.08f, result.Z, precision: 5);
        }

        [Fact]
        public void BoxClamp_NegativeLimits()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 0.10f, 0.10f, 0.10f, 0f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            // Negative values should be clamped to -limit
            Vec3 result = proc.Process(MakePos(-0.50f, -0.50f, -0.50f), Quat4.Identity, false, DeltaTime);

            Assert.Equal(-0.10f, result.X, precision: 5);
            Assert.Equal(-0.10f, result.Y, precision: 5);
            Assert.Equal(-0.10f, result.Z, precision: 5);
        }

        [Fact]
        public void Smoothing_ConvergesToTarget()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 1f, 1f, 1f, 0.5f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            // Feed same position for many frames
            Vec3 result = Vec3.Zero;
            for (int i = 0; i < 300; i++)
            {
                result = proc.Process(MakePos(0.10f, 0.05f, 0.08f), Quat4.Identity, false, DeltaTime);
            }

            // After 300 frames at 60Hz (5 seconds), should converge
            Assert.Equal(0.10f, result.X, precision: 3);
            Assert.Equal(0.05f, result.Y, precision: 3);
            Assert.Equal(0.08f, result.Z, precision: 3);
        }

        [Fact]
        public void NeckModel_AddsOffset()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 1f, 1f, 1f, 0f),
                NeckModelSettings = new NeckModelSettings(true, 0.10f, 0.08f)
            };

            // Zero tracker position, but rotation produces neck model offset
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(30f, 0f, 0f);
            Vec3 result = proc.Process(MakePos(0f, 0f, 0f), rotation, false, DeltaTime);

            // Neck model should produce non-zero offset
            Assert.True(System.Math.Abs(result.X) > 0.001f || System.Math.Abs(result.Z) > 0.001f,
                $"Expected non-zero position from neck model, got ({result.X}, {result.Y}, {result.Z})");
        }

        [Fact]
        public void TotalPositionIsClamped()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 0.02f, 0.02f, 0.02f, 0f),
                NeckModelSettings = new NeckModelSettings(true, 0.10f, 0.08f)
            };

            // Large rotation + position should be clamped
            Quat4 rotation = QuaternionUtils.FromYawPitchRoll(45f, 0f, 0f);
            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), rotation, false, DeltaTime);

            // All components should be within [-0.02, 0.02]
            Assert.True(result.X >= -0.02f && result.X <= 0.02f,
                $"X {result.X} exceeds limit 0.02");
            Assert.True(result.Y >= -0.02f && result.Y <= 0.02f,
                $"Y {result.Y} exceeds limit 0.02");
            Assert.True(result.Z >= -0.02f && result.Z <= 0.02f,
                $"Z {result.Z} exceeds limit 0.02");
        }

        [Fact]
        public void Reset_ClearsAllState()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 1f, 1f, 1f, 0f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            proc.SetCenter(MakePos(0.05f, 0.05f, 0.05f));
            proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, false, DeltaTime);

            proc.Reset();

            // After reset, center should be zero — same input should give raw values
            Vec3 result = proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, false, DeltaTime);
            Assert.Equal(0.10f, result.X, precision: 4);
            Assert.Equal(0.10f, result.Y, precision: 4);
            Assert.Equal(0.10f, result.Z, precision: 4);
        }

        [Fact]
        public void ResetSmoothing_PreservesCenter()
        {
            var proc = new PositionProcessor
            {
                Settings = new PositionSettings(1f, 1f, 1f, 1f, 1f, 1f, 0.5f),
                NeckModelSettings = NeckModelSettings.Disabled
            };

            proc.SetCenter(MakePos(0.05f, 0.05f, 0.05f));
            proc.Process(MakePos(0.10f, 0.10f, 0.10f), Quat4.Identity, false, DeltaTime);

            proc.ResetSmoothing();

            // Center should still be subtracted; first frame after reset should snap to target
            Vec3 result = proc.Process(MakePos(0.15f, 0.15f, 0.15f), Quat4.Identity, false, DeltaTime);
            Assert.Equal(0.10f, result.X, precision: 4);
            Assert.Equal(0.10f, result.Y, precision: 4);
            Assert.Equal(0.10f, result.Z, precision: 4);
        }
    }
}
