using System.Diagnostics;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Tests.Processing
{
    public class PositionInterpolatorTests
    {
        private const float DeltaTime = 1f / 120f; // 120Hz frame rate

        private static PositionData MakePos(float x, float y, float z, long timestamp)
        {
            return new PositionData(x, y, z, timestamp);
        }

        [Fact]
        public void FirstSample_ReturnsRawPosition()
        {
            var interp = new PositionInterpolator();
            var pos = MakePos(0.1f, 0.2f, 0.05f, 1000);

            var result = interp.Update(pos, DeltaTime);

            Assert.Equal(0.1f, result.X);
            Assert.Equal(0.2f, result.Y);
            Assert.Equal(0.05f, result.Z);
        }

        [Fact]
        public void InvalidPosition_ReturnsUnmodified()
        {
            var interp = new PositionInterpolator();
            var invalid = new PositionData(0.1f, 0.2f, 0.3f, 0);

            var result = interp.Update(invalid, DeltaTime);

            Assert.Equal(0.1f, result.X);
            Assert.Equal(0.2f, result.Y);
            Assert.Equal(0.3f, result.Z);
            Assert.Equal(0, result.TimestampTicks);
        }

        [Fact]
        public void SameTimestamp_ReturnsRawPosition_NoExtrapolation()
        {
            var interp = new PositionInterpolator();
            var pos = MakePos(0.1f, 0.2f, 0.05f, 1000);

            interp.Update(pos, DeltaTime);
            var result = interp.Update(pos, DeltaTime);

            Assert.Equal(0.1f, result.X);
            Assert.Equal(0.2f, result.Y);
            Assert.Equal(0.05f, result.Z);
        }

        [Fact]
        public void AfterTwoSamples_ExtrapolatesBetweenThem()
        {
            var interp = new PositionInterpolator();

            var pos1 = MakePos(0f, 0f, 0f, 1000);
            interp.Update(pos1, DeltaTime);

            // Simulate 4 frames passing
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pos1, DeltaTime);
            }

            // Second sample: X moved to 0.1
            var pos2 = MakePos(0.1f, 0f, 0f, 2000);
            interp.Update(pos2, DeltaTime);

            // Next frame with same timestamp should extrapolate
            var result = interp.Update(pos2, DeltaTime);

            Assert.True(result.X > 0.1f, $"Expected extrapolated X > 0.1, got {result.X}");
        }

        [Fact]
        public void Extrapolation_CapsAtMaxTime()
        {
            var interp = new PositionInterpolator { MaxExtrapolationTime = 0.05f };

            var pos1 = MakePos(0f, 0f, 0f, 1000);
            interp.Update(pos1, DeltaTime);

            for (int i = 0; i < 3; i++)
            {
                interp.Update(pos1, DeltaTime);
            }

            var pos2 = MakePos(0.1f, 0f, 0f, 2000);
            interp.Update(pos2, DeltaTime);

            float resultAtMedium = 0f;
            float resultAtLong = 0f;

            for (int i = 0; i < 10; i++)
            {
                var r = interp.Update(pos2, DeltaTime);
                resultAtMedium = r.X;
            }

            for (int i = 0; i < 100; i++)
            {
                var r = interp.Update(pos2, DeltaTime);
                resultAtLong = r.X;
            }

            Assert.Equal(resultAtMedium, resultAtLong, precision: 3);
        }

        [Fact]
        public void Reset_ClearsVelocityState()
        {
            var interp = new PositionInterpolator();

            var pos1 = MakePos(0f, 0f, 0f, 1000);
            interp.Update(pos1, DeltaTime);
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pos1, DeltaTime);
            }
            var pos2 = MakePos(0.1f, 0f, 0f, 2000);
            interp.Update(pos2, DeltaTime);

            interp.Reset();

            var pos3 = MakePos(0.05f, 0.05f, 0.05f, 3000);
            var result = interp.Update(pos3, DeltaTime);

            Assert.Equal(0.05f, result.X);
            Assert.Equal(0.05f, result.Y);
            Assert.Equal(0.05f, result.Z);

            var result2 = interp.Update(pos3, DeltaTime);
            Assert.Equal(0.05f, result2.X);
        }

        [Fact]
        public void HighFrequencyInput_NearZeroExtrapolation()
        {
            var interp = new PositionInterpolator();

            float x = 0f;
            PositionData result = default;
            for (int i = 0; i < 20; i++)
            {
                x += 0.005f;
                var pos = MakePos(x, 0f, 0f, 1000 + i);
                result = interp.Update(pos, DeltaTime);
            }

            Assert.Equal(x, result.X, precision: 5);
        }
    }
}
