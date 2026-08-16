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
        public void SameTimestamp_OutputsSamePosition()
        {
            var interp = new PositionInterpolator();
            var pos = MakePos(0.1f, 0.2f, 0.05f, 1000);

            interp.Update(pos, DeltaTime);
            var result = interp.Update(pos, DeltaTime);

            Assert.Equal(0.1f, result.X, precision: 4);
            Assert.Equal(0.2f, result.Y, precision: 4);
            Assert.Equal(0.05f, result.Z, precision: 4);
        }

        [Fact]
        public void AfterTwoSamples_InterpolatesBetweenThem()
        {
            var interp = new PositionInterpolator();

            var pos1 = MakePos(0f, 0f, 0f, 1000);
            interp.Update(pos1, DeltaTime);

            // Simulate 3 frames passing
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pos1, DeltaTime);
            }

            // Second sample: X moved to 0.1
            var pos2 = MakePos(0.1f, 0f, 0f, 2000);
            interp.Update(pos2, DeltaTime);

            // Next frame — should be interpolating between pos1 and pos2
            var result = interp.Update(pos2, DeltaTime);

            Assert.True(result.X > 0f && result.X < 0.1f,
                $"Expected interpolated X between 0 and 0.1, got {result.X}");
        }

        [Fact]
        public void ExtrapolationHoldsThenExpires_WhenNoNewSample()
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

            // A short gap - a dropped packet or a Wi-Fi burst - is still a live
            // feed: extrapolate half a sample period past the target, then hold
            // there (from + (to - from) * 1.5).
            float held = 0f;
            int holdFrames = (int)(PositionInterpolator.ExtrapolationHoldSeconds / DeltaTime) - 2;
            for (int i = 0; i < holdFrames; i++)
            {
                held = interp.Update(pos2, DeltaTime).X;
            }
            Assert.Equal(0.15f, held, precision: 2);

            // Keep the feed stalled and the prediction expires: it converges
            // back to what the tracker actually reported. Holding 1.5x forever
            // left the view half again past where the head was.
            float settled = 0f;
            int decayFrames = (int)(PositionInterpolator.ExtrapolationDecaySeconds / DeltaTime) + 10;
            for (int i = 0; i < decayFrames; i++)
            {
                settled = interp.Update(pos2, DeltaTime).X;
            }
            Assert.Equal(0.1f, settled, precision: 2);
        }

        [Fact]
        public void NoExtrapolation_HoldsAtTarget()
        {
            var interp = new PositionInterpolator { MaxExtrapolationFraction = 0f };

            var pos1 = MakePos(0f, 0f, 0f, 1000);
            interp.Update(pos1, DeltaTime);
            for (int i = 0; i < 3; i++)
            {
                interp.Update(pos1, DeltaTime);
            }

            var pos2 = MakePos(0.1f, 0f, 0f, 2000);
            interp.Update(pos2, DeltaTime);

            float lastX = 0f;
            for (int i = 0; i < 100; i++)
            {
                var r = interp.Update(pos2, DeltaTime);
                lastX = r.X;
            }

            Assert.Equal(0.1f, lastX, precision: 3);
        }

        [Fact]
        public void Reset_ClearsState()
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
            Assert.Equal(0.05f, result2.X, precision: 4);
        }

        [Fact]
        public void HighFrequencyInput_PassesThrough()
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
