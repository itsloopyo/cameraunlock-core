using CameraUnlock.Core.Ads;
using Xunit;

namespace CameraUnlock.Core.Tests.Ads
{
    /// <summary>
    /// The C# half of the aim-down-sights transition tests, the fade a mod rides to ease a
    /// positional lean out while the sights are up. Every case here is a bug that has
    /// shipped in this shape before: a transition that switches instead of easing, or steps
    /// when it is reversed.
    /// <para>
    /// Deliberately the same cases as cpp/tests/ads_tests.cpp. The two halves are a
    /// cross-language contract, and a case that only one of them checks is a case the two
    /// can drift apart on.
    /// </para>
    /// </summary>
    public class AdsTests
    {
        // ---- the transition --------------------------------------------------------

        [Fact]
        public void Fade_EndpointsAndShape()
        {
            var fade = new AdsFade();
            Assert.Equal(1.0f, fade.Update(false, 1000), 4);

            // The frame the sights start coming up is still full scale: the transition
            // eases out of rest, it does not step.
            Assert.Equal(1.0f, fade.Update(true, 0), 4);
            Assert.Equal(0.5f, fade.Update(true, AdsFade.LowerMs / 2), 3);
            Assert.Equal(0.0f, fade.Update(true, AdsFade.LowerMs), 4);
            Assert.Equal(0.0f, fade.Update(true, AdsFade.LowerMs + 5000), 4);

            Assert.Equal(0.0f, fade.Update(false, 10000), 4);
            Assert.Equal(1.0f, fade.Update(false, 10000 + AdsFade.RaiseMs), 4);
        }

        [Fact]
        public void Fade_RideOutNeverReverses()
        {
            var fade = new AdsFade();
            fade.Update(true, 0);
            float last = 1.1f;
            for (ulong t = 0; t <= AdsFade.LowerMs; t += 5)
            {
                float now = fade.Update(true, t);
                Assert.True(now <= last + 1e-4f);
                last = now;
            }
            Assert.Equal(0.0f, last, 4);
        }

        // A player who taps aim interrupts the transition half way. It has to turn round
        // from where it is, not from where it started, or the view jumps by the part that
        // had already faded.
        //
        // The bound is equality, not a tolerance. This case previously allowed 0.55, which
        // no implementation returning a value in [0,1] could violate at the half way
        // point, so it passed for as long as the reversal stepped by a clean half.
        [Fact]
        public void Fade_InterruptedHalfWayIsContinuous()
        {
            var fade = new AdsFade();
            fade.Update(true, 0);
            float half = fade.Update(true, AdsFade.LowerMs / 2);
            float resumed = fade.Update(false, AdsFade.LowerMs / 2);
            Assert.Equal(half, resumed, 4);
            Assert.Equal(1.0f, fade.Update(false, AdsFade.LowerMs / 2 + AdsFade.RaiseMs), 4);
        }

        // The worst reversal is the earliest one, and it is also the most common input
        // there is: a tap releases the aim button a frame after pressing it, with the pose
        // still all but fully applied.
        [Fact]
        public void Fade_TapDoesNotStepThePose()
        {
            var fade = new AdsFade();
            fade.Update(true, 0);
            float barely = fade.Update(true, 1);
            float resumed = fade.Update(false, 1);
            Assert.Equal(barely, resumed, 4);
            Assert.Equal(1.0f, fade.Update(false, 1 + AdsFade.RaiseMs), 4);
        }

        // An interrupted leg travels at the same RATE as a whole one, so a short reversal
        // finishes quickly rather than taking the full duration to cover a fraction of the
        // distance.
        [Fact]
        public void Fade_ReversalIsScaledToTheDistanceLeft()
        {
            var fade = new AdsFade();
            fade.Update(true, 0);
            float quarter = fade.Update(true, AdsFade.LowerMs / 4);
            fade.Update(false, AdsFade.LowerMs / 4);
            ulong remaining = (ulong)(AdsFade.RaiseMs * (1.0f - quarter));
            Assert.Equal(1.0f, fade.Update(false, AdsFade.LowerMs / 4 + remaining + 1), 4);
        }

        // A clock that steps backwards must not settle the transition instantly. The
        // subtraction is unsigned, so an unguarded one wraps to an enormous elapsed and the
        // fade lands on its target on the spot - a snap, in the one place this class exists
        // to prevent one. Clamped, the leg reports its own start until the clock catches
        // up, which is a far smaller wrong answer and a recoverable one.
        [Fact]
        public void Fade_SurvivesABackwardsClock()
        {
            var fade = new AdsFade();
            fade.Update(true, 1000);
            fade.Update(true, 1000 + AdsFade.LowerMs / 2);
            Assert.NotEqual(0.0f, fade.Update(true, 999), 4);
            Assert.Equal(0.0f, fade.Update(true, 1000 + AdsFade.LowerMs), 4);
        }

        [Fact]
        public void Fade_ResetDropsStraightBackToTheHip()
        {
            var fade = new AdsFade();
            fade.Update(true, 0);
            fade.Update(true, AdsFade.LowerMs);
            fade.Reset();
            Assert.Equal(1.0f, fade.Update(false, 5000), 4);
        }
    }
}
