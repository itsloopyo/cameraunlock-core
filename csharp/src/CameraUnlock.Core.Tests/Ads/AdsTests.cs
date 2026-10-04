using CameraUnlock.Core.Ads;
using CameraUnlock.Core.Data;
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

        // ---- the lean hand-over ----------------------------------------------------

        private static void AssertVec(Vec3 expected, Vec3 actual)
        {
            Assert.Equal(expected.X, actual.X, 4);
            Assert.Equal(expected.Y, actual.Y, 4);
            Assert.Equal(expected.Z, actual.Z, 4);
        }

        private static readonly Vec3 Lean = new Vec3(0.25f, -0.05f, 0.1f);
        private static readonly Vec3 Forward = new Vec3(0f, 0f, 1f);

        [Fact]
        public void Handover_HipLeavesTheRigAlone()
        {
            var handover = new LeanHandover();
            LeanShares hip = handover.Update(Lean, Forward, false, false, true, 1000);
            AssertVec(Lean, hip.Camera);
            AssertVec(Vec3.Zero, hip.Rig);
            Assert.False(handover.Stop());
        }

        [Fact]
        public void Handover_SightsUpMovesTheLeanToTheRig()
        {
            var handover = new LeanHandover();
            handover.Update(Lean, Forward, true, false, true, 0);
            LeanShares up = handover.Update(Lean, Forward, true, false, true, AdsFade.LowerMs + 1);
            AssertVec(new Vec3(0.25f, -0.05f, 0f), up.Rig);
            AssertVec(new Vec3(0f, 0f, 0.1f), up.Camera);
        }

        [Fact]
        public void Handover_KeepsThePartAlongTheAimOnTheCamera()
        {
            var forward = new Vec3(0f, 0.6f, 0.8f);
            var sideways = new Vec3(0.25f, 0f, 0f);
            Vec3 lean = sideways + forward * 0.2f;
            var handover = new LeanHandover();
            handover.Update(lean, forward, true, false, true, 0);
            LeanShares up = handover.Update(lean, forward, true, false, true, AdsFade.LowerMs + 1);
            AssertVec(forward * 0.2f, up.Camera);
            AssertVec(sideways, up.Rig);

            var noRig = new LeanHandover();
            noRig.Update(lean, forward, true, false, false, 0);
            LeanShares eased = noRig.Update(lean, forward, true, false, false, AdsFade.LowerMs + 1);
            AssertVec(forward * 0.2f, eased.Camera);
            AssertVec(Vec3.Zero, eased.Rig);
        }

        [Fact]
        public void Handover_SharesAlwaysSumToTheLean()
        {
            var handover = new LeanHandover();
            bool split = false;
            for (ulong t = 0; t <= AdsFade.LowerMs + 10; t += 5)
            {
                LeanShares s = handover.Update(Lean, Forward, true, false, true, t);
                AssertVec(Lean, s.Camera + s.Rig);
                split |= s.Camera.X > 0.01f && s.Rig.X > 0.01f;
            }
            for (ulong t = 1000; t <= 1000 + AdsFade.RaiseMs + 10; t += 5)
            {
                LeanShares s = handover.Update(Lean, Forward, false, false, true, t);
                AssertVec(Lean, s.Camera + s.Rig);
            }
            Assert.True(split);
        }

        [Fact]
        public void Handover_TrueFreeLookKeepsTheLeanOnTheCamera()
        {
            var handover = new LeanHandover();
            var lean = new Vec3(0.25f, 0f, 0f);
            handover.Update(lean, Forward, true, true, true, 0);
            LeanShares up = handover.Update(lean, Forward, true, true, true, AdsFade.LowerMs + 1);
            AssertVec(lean, up.Camera);
            AssertVec(Vec3.Zero, up.Rig);

            LeanShares first = handover.Update(lean, Forward, true, false, true, 1000);
            Assert.Equal(0.25f, first.Camera.X, 4);
            LeanShares mid = handover.Update(lean, Forward, true, false, true, 1000 + AdsFade.LowerMs / 2);
            Assert.True(mid.Camera.X > 0.01f && mid.Rig.X > 0.01f);
        }

        [Fact]
        public void Handover_WithoutARigEasesTheLeanOut()
        {
            var handover = new LeanHandover();
            var lean = new Vec3(0.25f, 0f, 0f);
            handover.Update(lean, Forward, true, false, false, 0);
            LeanShares up = handover.Update(lean, Forward, true, false, false, AdsFade.LowerMs + 1);
            AssertVec(Vec3.Zero, up.Camera);
            AssertVec(Vec3.Zero, up.Rig);
            Assert.False(handover.Stop());
        }

        private static LeanShares AimedWithStop(Vec3 lean, bool trueFreeLook)
        {
            var handover = new LeanHandover();
            handover.SetForwardStop(0.12f);
            handover.Update(lean, Forward, true, trueFreeLook, true, 0);
            return handover.Update(lean, Forward, true, trueFreeLook, true, AdsFade.LowerMs + 1);
        }

        [Fact]
        public void Handover_ForwardStopHoldsTheEyeBehindTheSights()
        {
            var lean = new Vec3(0.1f, 0f, 0.3f);
            LeanShares locked = AimedWithStop(lean, false);
            Assert.Equal(0.12f, locked.Camera.Z, 4);
            Assert.Equal(0.1f, locked.Rig.X, 4);
            Assert.Equal(0.12f, AimedWithStop(lean, true).Camera.Z, 4);

            var hip = new LeanHandover();
            hip.SetForwardStop(0.12f);
            Assert.Equal(0.3f, hip.Update(lean, Forward, false, false, true, 0).Camera.Z, 4);

            Assert.Equal(-0.1f, AimedWithStop(new Vec3(0f, 0f, -0.1f), false).Camera.Z, 4);

            var easing = new LeanHandover();
            easing.SetForwardStop(0.12f);
            easing.Update(lean, Forward, true, false, true, 0);
            float mid = easing.Update(lean, Forward, true, false, true, AdsFade.LowerMs / 2).Camera.Z;
            Assert.True(mid < 0.3f && mid > 0.12f);
        }

        [Fact]
        public void Handover_StopReleasesTheRig()
        {
            var handover = new LeanHandover();
            var lean = new Vec3(0.25f, 0f, 0f);
            handover.Update(lean, Forward, true, false, true, 0);
            handover.Update(lean, Forward, true, false, true, AdsFade.LowerMs + 1);
            Assert.True(handover.Stop());
            Assert.False(handover.Stop());
            LeanShares after = handover.Update(lean, Forward, false, false, true, 5000);
            AssertVec(lean, after.Camera);
            AssertVec(Vec3.Zero, after.Rig);
        }

        // ---- the aim mode ----------------------------------------------------------

        [Fact]
        public void AimMode_DecodesTheThreeValues()
        {
            Assert.Equal(AimMode.SightsLocked, AimModes.Decode(false, false, false));
            // A config from before the marker holds TrueFreeLook alone and keeps its mode.
            Assert.Equal(AimMode.TrueFreeLook, AimModes.Decode(true, false, false));
            Assert.Equal(AimMode.SightsLocked, AimModes.Decode(false, true, false));
            Assert.Equal(AimMode.FreeLookMarker, AimModes.Decode(true, true, false));
            Assert.Equal(AimMode.StockSights, AimModes.Decode(false, false, true));
            Assert.Equal(AimMode.StockSights, AimModes.Decode(true, false, true));
            Assert.Equal(AimMode.StockSights, AimModes.Decode(false, true, true));
            Assert.Equal(AimMode.StockSights, AimModes.Decode(true, true, true));
        }

        [Fact]
        public void AimMode_CycleAndEncode()
        {
            Assert.Equal(AimMode.FreeLookMarker, AimModes.Next(AimMode.SightsLocked));
            Assert.Equal(AimMode.TrueFreeLook, AimModes.Next(AimMode.FreeLookMarker));
            Assert.Equal(AimMode.StockSights, AimModes.Next(AimMode.TrueFreeLook));
            Assert.Equal(AimMode.SightsLocked, AimModes.Next(AimMode.StockSights));

            AimMode mode = AimMode.SightsLocked;
            for (int step = 0; step < 8; step++)
            {
                bool trueFreeLook, freeLookMarker, stockSights;
                AimModes.Encode(mode, out trueFreeLook, out freeLookMarker, out stockSights);
                Assert.Equal(mode, AimModes.Decode(trueFreeLook, freeLookMarker, stockSights));
                Assert.False(!trueFreeLook && freeLookMarker);
                mode = AimModes.Next(mode);
            }

            bool free, marker, stock;
            AimModes.Encode(AimMode.SightsLocked, out free, out marker, out stock);
            Assert.True(!free && !marker && !stock);
            AimModes.Encode(AimMode.FreeLookMarker, out free, out marker, out stock);
            Assert.True(free && marker && !stock);
            AimModes.Encode(AimMode.TrueFreeLook, out free, out marker, out stock);
            Assert.True(free && !marker && !stock);
            AimModes.Encode(AimMode.StockSights, out free, out marker, out stock);
            Assert.True(!free && !marker && stock);
        }

        [Fact]
        public void AimMode_Labels()
        {
            Assert.Equal("Aim mode: sights locked", AimModes.Label(AimMode.SightsLocked));
            Assert.Equal("Aim mode: free look with marker", AimModes.Label(AimMode.FreeLookMarker));
            Assert.Equal("Aim mode: true free look", AimModes.Label(AimMode.TrueFreeLook));
            Assert.Equal("Aim mode: stock sights", AimModes.Label(AimMode.StockSights));
        }

        [Fact]
        public void AimMode_OnlyTheTwoFreeLookModesAreFreeLook()
        {
            Assert.True(AimModes.IsFreeLook(AimMode.FreeLookMarker));
            Assert.True(AimModes.IsFreeLook(AimMode.TrueFreeLook));
            Assert.False(AimModes.IsFreeLook(AimMode.SightsLocked));
            // A lean on its way out in stock sights is handed over as sights locked.
            Assert.False(AimModes.IsFreeLook(AimMode.StockSights));
        }

        [Fact]
        public void AimMode_MarkerShowsOnlyInFreeLookWithAMarker()
        {
            Assert.Equal(1.0f, AimModes.MarkerOpacity(AimMode.FreeLookMarker, 1.0f), 4);
            Assert.Equal(0.4f, AimModes.MarkerOpacity(AimMode.FreeLookMarker, 0.4f), 4);
            Assert.Equal(0.0f, AimModes.MarkerOpacity(AimMode.FreeLookMarker, 0.0f));
            Assert.Equal(0.0f, AimModes.MarkerOpacity(AimMode.SightsLocked, 1.0f));
            Assert.Equal(0.0f, AimModes.MarkerOpacity(AimMode.TrueFreeLook, 1.0f));
            Assert.Equal(0.0f, AimModes.MarkerOpacity(AimMode.StockSights, 1.0f));
        }

        [Fact]
        public void AimMode_AValueOutsideTheEnumThrows()
        {
            bool a, b, c;
            Assert.Throws<System.ArgumentOutOfRangeException>(() => AimModes.Next((AimMode)4));
            Assert.Throws<System.ArgumentOutOfRangeException>(() => AimModes.Label((AimMode)4));
            Assert.Throws<System.ArgumentOutOfRangeException>(() => AimModes.Encode((AimMode)(-1), out a, out b, out c));
            Assert.Throws<System.ArgumentOutOfRangeException>(() => AimModes.MarkerOpacity((AimMode)4, 1.0f));
            Assert.Throws<System.ArgumentOutOfRangeException>(() => AimModes.IsFreeLook((AimMode)4));
            Assert.Throws<System.ArgumentOutOfRangeException>(() => AimModes.StockSightsEngaged((AimMode)4, true));
        }

        // ---- stock sights ----------------------------------------------------------

        [Fact]
        public void StockSights_EngagesOnlyInItsModeWithTheSightsUp()
        {
            Assert.True(AimModes.StockSightsEngaged(AimMode.StockSights, true));
            Assert.False(AimModes.StockSightsEngaged(AimMode.StockSights, false));
            Assert.False(AimModes.StockSightsEngaged(AimMode.SightsLocked, true));
            Assert.False(AimModes.StockSightsEngaged(AimMode.FreeLookMarker, true));
            Assert.False(AimModes.StockSightsEngaged(AimMode.TrueFreeLook, true));
        }

        // The pose as a mod applies it: a second AdsFade fed StockSightsEngaged, whose
        // output scales yaw, pitch and the three lean axes and never roll.
        private const float HeadYaw = 20.0f;
        private const float HeadPitch = -8.0f;
        private const float HeadRoll = 6.0f;
        private static readonly Vec3 HeadLean = new Vec3(0.25f, -0.05f, 0.1f);

        private struct Pose
        {
            public float Yaw;
            public float Pitch;
            public float Roll;
            public Vec3 Lean;
        }

        private static Pose StockSightsPose(AdsFade fade, AimMode mode, bool aiming, ulong nowMs)
        {
            float share = fade.Update(AimModes.StockSightsEngaged(mode, aiming), nowMs);
            return new Pose { Yaw = HeadYaw * share, Pitch = HeadPitch * share, Roll = HeadRoll, Lean = HeadLean * share };
        }

        private static void AssertPose(float share, Pose pose)
        {
            Assert.Equal(HeadYaw * share, pose.Yaw, 4);
            Assert.Equal(HeadPitch * share, pose.Pitch, 4);
            Assert.Equal(HeadRoll, pose.Roll);
            AssertVec(HeadLean * share, pose.Lean);
        }

        [Fact]
        public void StockSights_EasesEverythingButRollOut()
        {
            var fade = new AdsFade();
            AssertPose(1.0f, StockSightsPose(fade, AimMode.StockSights, false, 1000));

            StockSightsPose(fade, AimMode.StockSights, true, 2000);
            Pose mid = StockSightsPose(fade, AimMode.StockSights, true, 2000 + AdsFade.LowerMs / 2);
            Assert.Equal(10.0f, mid.Yaw, 2);
            Assert.Equal(-4.0f, mid.Pitch, 2);
            Assert.Equal(0.125f, mid.Lean.X, 3);
            Assert.Equal(-0.025f, mid.Lean.Y, 3);
            Assert.Equal(0.05f, mid.Lean.Z, 3);
            Assert.Equal(HeadRoll, mid.Roll);

            Pose up = StockSightsPose(fade, AimMode.StockSights, true, 2000 + AdsFade.LowerMs);
            Assert.Equal(0.0f, up.Yaw);
            Assert.Equal(0.0f, up.Pitch);
            Assert.Equal(0.0f, up.Lean.X);
            Assert.Equal(0.0f, up.Lean.Y);
            Assert.Equal(0.0f, up.Lean.Z);
            Assert.Equal(HeadRoll, up.Roll);

            StockSightsPose(fade, AimMode.StockSights, false, 5000);
            AssertPose(1.0f, StockSightsPose(fade, AimMode.StockSights, false, 5000 + AdsFade.RaiseMs));
        }

        [Fact]
        public void StockSights_ReversalsContinue()
        {
            var button = new AdsFade();
            StockSightsPose(button, AimMode.StockSights, true, 0);
            Pose half = StockSightsPose(button, AimMode.StockSights, true, AdsFade.LowerMs / 2);
            Pose released = StockSightsPose(button, AimMode.StockSights, false, AdsFade.LowerMs / 2);
            Assert.Equal(half.Yaw, released.Yaw, 4);
            Assert.Equal(half.Lean.X, released.Lean.X, 4);

            // The mode key pressed with the sights up: into stock sights, then out of it.
            var key = new AdsFade();
            AssertPose(1.0f, StockSightsPose(key, AimMode.TrueFreeLook, true, 0));
            AssertPose(1.0f, StockSightsPose(key, AimMode.StockSights, true, 100));
            Pose easing = StockSightsPose(key, AimMode.StockSights, true, 100 + AdsFade.LowerMs / 2);
            Assert.True(easing.Yaw < HeadYaw && easing.Yaw > 0.0f);
            Pose steppedOut = StockSightsPose(key, AimMode.SightsLocked, true, 100 + AdsFade.LowerMs / 2);
            Assert.Equal(easing.Yaw, steppedOut.Yaw, 4);
            Assert.Equal(easing.Lean.Z, steppedOut.Lean.Z, 4);
            AssertPose(1.0f, StockSightsPose(key, AimMode.SightsLocked, true, 100 + AdsFade.LowerMs / 2 + AdsFade.RaiseMs));
        }

        [Fact]
        public void StockSights_TheOtherModesPassThePoseThrough()
        {
            foreach (AimMode mode in new[] { AimMode.SightsLocked, AimMode.FreeLookMarker, AimMode.TrueFreeLook })
            {
                var fade = new AdsFade();
                AssertPose(1.0f, StockSightsPose(fade, mode, false, 0));
                AssertPose(1.0f, StockSightsPose(fade, mode, true, 100));
                AssertPose(1.0f, StockSightsPose(fade, mode, true, 100 + AdsFade.LowerMs));
                AssertPose(1.0f, StockSightsPose(fade, mode, false, 1000));
            }
        }
    }
}
