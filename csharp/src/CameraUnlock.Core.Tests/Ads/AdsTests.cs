using CameraUnlock.Core.Ads;
using Xunit;

namespace CameraUnlock.Core.Tests.Ads
{
    /// <summary>
    /// The C# half of the shared aim-down-sights module. Every case here is a bug that has
    /// shipped in this shape before: the value strings a settings file has to keep meaning
    /// the same thing across releases, the transition that must ease rather than switch,
    /// the entry pose whose seam and capture timing are invisible from any settings or gate
    /// test a mod can write, and the one axis the blend leaves alone.
    /// <para>
    /// Deliberately the same cases as cpp/tests/ads_tests.cpp. The two halves are a
    /// cross-language contract, and a case that only one of them checks is a case the two
    /// can drift apart on.
    /// </para>
    /// </summary>
    public class AdsTests
    {
        private static AdsPose Pose(float pitch, float yaw, float roll,
            float x = 0f, float y = 0f, float z = 0f)
        {
            return new AdsPose(pitch, yaw, roll, x, y, z);
        }

        // ---- the cycle -------------------------------------------------------------

        [Fact]
        public void Default_IsTheModeThatCannotBeWrong()
        {
            Assert.Equal(AdsMode.Paused, AdsModes.Default);
        }

        [Fact]
        public void ThreeSlotCycle_WalksPausedMarkerTracked()
        {
            Assert.Equal(AdsMode.Marker, AdsModes.Next(AdsMode.Paused));
            Assert.Equal(AdsMode.Tracked, AdsModes.Next(AdsMode.Marker));
            Assert.Equal(AdsMode.Paused, AdsModes.Next(AdsMode.Tracked));
        }

        [Fact]
        public void TwoSlotCycle_NeverReachesMarker()
        {
            Assert.Equal(AdsMode.Tracked, AdsModes.NextTwoSlot(AdsMode.Paused));
            Assert.Equal(AdsMode.Paused, AdsModes.NextTwoSlot(AdsMode.Tracked));
        }

        [Theory]
        [InlineData(AdsMode.Paused, "paused", "ADS: tracking paused", "Tracking paused")]
        [InlineData(AdsMode.Marker, "marker", "ADS: tracking on, aim marker shown", "Tracking on, aim marker shown")]
        [InlineData(AdsMode.Tracked, "tracked", "ADS: tracking on, no aim marker", "Tracking on, no aim marker")]
        public void Strings_AreTheCrossModContract(AdsMode mode, string value, string toast, string label)
        {
            Assert.Equal(value, AdsModes.Value(mode));
            Assert.Equal(toast, AdsModes.Toast(mode));
            Assert.Equal(label, AdsModes.Label(mode));
        }

        [Fact]
        public void OnlyPaused_SuspendsTracking()
        {
            Assert.True(AdsModes.SuspendsTracking(AdsMode.Paused));
            Assert.False(AdsModes.SuspendsTracking(AdsMode.Marker));
            Assert.False(AdsModes.SuspendsTracking(AdsMode.Tracked));
        }

        // ---- parsing ---------------------------------------------------------------

        [Theory]
        [InlineData(AdsMode.Paused)]
        [InlineData(AdsMode.Marker)]
        [InlineData(AdsMode.Tracked)]
        public void Parse_RoundTripsEveryValueTheFileCanHold(AdsMode mode)
        {
            Assert.Equal(mode, AdsModes.Parse(AdsModes.Value(mode)));
        }

        // The trimmed set is the same one cpp/include/cameraunlock/ads/ads_mode.h trims,
        // character for character. It used to be string.Trim() here and an explicit
        // " \t\r\n" there, so a value carrying a newline or a non-breaking space parsed as
        // tracked in a C# mod and fell back to paused in a C++ one, from the same file.
        [Theory]
        [InlineData("  tracked \r\n")]
        [InlineData("\ttracked\t")]
        [InlineData("\r\ntracked")]
        [InlineData("\vtracked\f")]
        public void Parse_TrimsTheSameWhitespaceAsTheCppHalf(string text)
        {
            Assert.Equal(AdsMode.Tracked, AdsModes.Parse(text));
        }

        // Deliberately NOT trimmed, on either side: these are not whitespace, and a value
        // that carries one is a file this parser should fail toward the default on.
        [Theory]
        [InlineData("\u00a0tracked")]
        [InlineData("\u3000tracked")]
        public void Parse_DoesNotTrimNonAsciiSpace(string text)
        {
            Assert.Equal(AdsModes.Default, AdsModes.Parse(text));
        }

        [Fact]
        public void Parse_FoldsCase()
        {
            Assert.Equal(AdsMode.Marker, AdsModes.Parse("MARKER"));
        }

        // The migration path: a mode renamed since an older release wrote the file must
        // land on stock ADS, not on whichever branch happens to be last.
        [Theory]
        [InlineData(null)]
        [InlineData("")]
        [InlineData("snap")]
        [InlineData("trackedish")]
        public void Parse_FallsBackToTheDefault(string text)
        {
            Assert.Equal(AdsModes.Default, AdsModes.Parse(text));
        }

        [Fact]
        public void Parse_TwoSlotModReadsMarkerAsTheDefault()
        {
            Assert.Equal(AdsModes.Default, AdsModes.Parse("marker", allowMarker: false));
        }

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

        // ---- the entry pose --------------------------------------------------------

        [Fact]
        public void EntryPose_HipFirePassesTheAbsolutePoseThrough()
        {
            var entry = new AdsEntryPose();
            AdsPose hip = entry.Relative(false, true, Pose(5f, -12f, 3f, 1f, 2f, 3f));
            Assert.Equal(5f, hip.Pitch, 4);
            Assert.Equal(-12f, hip.Yaw, 4);
            Assert.Equal(3f, hip.Roll, 4);
            Assert.Equal(1f, hip.X, 4);
            Assert.False(entry.HasEntry);
        }

        [Fact]
        public void EntryPose_EntryFrameIsIdentity()
        {
            var entry = new AdsEntryPose();
            AdsPose first = entry.Relative(true, true, Pose(20f, -35f, 0f, 4f, 5f, 6f));
            Assert.True(entry.HasEntry);
            Assert.Equal(0f, first.Pitch, 4);
            Assert.Equal(0f, first.Yaw, 4);
            Assert.Equal(0f, first.X, 4);
            Assert.Equal(0f, first.Y, 4);
            Assert.Equal(0f, first.Z, 4);
        }

        // Roll moves no aim point, so zeroing it would yank a head tilt the player is
        // actively holding back to level and lean it in again as they move.
        [Fact]
        public void EntryPose_RollIsNeverMadeRelative()
        {
            var entry = new AdsEntryPose();
            Assert.Equal(14f, entry.Relative(true, true, Pose(0f, 0f, 14f)).Roll, 4);
            Assert.Equal(-6f, entry.Relative(true, true, Pose(0f, 0f, -6f)).Roll, 4);
        }

        // Yaw arrives wrapped into -180..180, so a plain subtraction reads a 10 degree
        // move across the seam as -350 and whips the view a full turn the wrong way.
        [Fact]
        public void EntryPose_YawCrossesTheSeamTheShortWay()
        {
            var up = new AdsEntryPose();
            up.Relative(true, true, Pose(0f, 175f, 0f));
            Assert.Equal(10f, up.Relative(true, true, Pose(0f, -175f, 0f)).Yaw, 4);

            var down = new AdsEntryPose();
            down.Relative(true, true, Pose(0f, -175f, 0f));
            Assert.Equal(-10f, down.Relative(true, true, Pose(0f, 175f, 0f)).Yaw, 4);
        }

        [Fact]
        public void EntryPose_PitchAndPositionGoRelative()
        {
            var entry = new AdsEntryPose();
            entry.Relative(true, true, Pose(10f, 0f, 0f, 3f, -1f, 2f));
            AdsPose relative = entry.Relative(true, true, Pose(-5f, 0f, 0f, 4f, -3f, 2.5f));
            Assert.Equal(-15f, relative.Pitch, 4);
            Assert.Equal(1f, relative.X, 4);
            Assert.Equal(-2f, relative.Y, 4);
            Assert.Equal(0.5f, relative.Z, 4);
        }

        // The path that hits this: aim, open a menu, move your head, close it with the
        // sights still up. Capturing on a dead frame would hold the whole aim at a
        // pre-suppression offset.
        [Fact]
        public void EntryPose_CaptureWaitsForALiveRotation()
        {
            var entry = new AdsEntryPose();
            entry.Relative(true, false, Pose(9f, 9f, 0f));
            Assert.False(entry.HasEntry);

            AdsPose relative = entry.Relative(true, true, Pose(30f, -20f, 0f));
            Assert.True(entry.HasEntry);
            Assert.Equal(0f, relative.Pitch, 4);
            Assert.Equal(0f, relative.Yaw, 4);
        }

        [Fact]
        public void EntryPose_LoweringTheWeaponDropsIt()
        {
            var entry = new AdsEntryPose();
            entry.Relative(true, true, Pose(10f, 40f, 0f));
            AdsPose down = entry.Relative(false, true, Pose(12f, 45f, 0f));
            Assert.False(entry.HasEntry);
            Assert.Equal(45f, down.Yaw, 4);
            Assert.Equal(0f, entry.Relative(true, true, Pose(12f, 45f, 0f)).Yaw, 4);
        }

        // ---- the blend -------------------------------------------------------------

        // The two poses carry DIFFERENT rolls throughout this section. With them equal -
        // which is how these cases were first written - a blend that reads roll from the
        // relative pose, or folds it into the fade, passes every one of them.
        [Theory]
        [InlineData(AdsMode.Paused)]
        [InlineData(AdsMode.Marker)]
        [InlineData(AdsMode.Tracked)]
        public void Blend_AtTheHipIsTheHeadPoseUntouched(AdsMode mode)
        {
            AdsPose absolute = Pose(5f, -12f, 3f, 1f, 2f, 3f);
            AdsPose blended = AdsPoseBlend.Blend(mode, 1.0f, absolute, Pose(0f, 0f, 7f));
            Assert.Equal(5f, blended.Pitch, 4);
            Assert.Equal(-12f, blended.Yaw, 4);
            Assert.Equal(3f, blended.Roll, 4);
            Assert.Equal(3f, blended.Z, 4);
        }

        // Sights fully up in paused: the view is the game's again, apart from the tilt the
        // player is holding.
        [Fact]
        public void Blend_PausedKeepsRollAndDropsTheRest()
        {
            AdsPose absolute = Pose(5f, -12f, 3f, 1f, 2f, 3f);
            AdsPose blended = AdsPoseBlend.Blend(AdsMode.Paused, 0.0f, absolute, Pose(0f, 0f, 7f));
            Assert.Equal(0f, blended.Pitch, 4);
            Assert.Equal(0f, blended.Yaw, 4);
            Assert.Equal(0f, blended.X, 4);
            Assert.Equal(0f, blended.Y, 4);
            Assert.Equal(0f, blended.Z, 4);
            Assert.Equal(3f, blended.Roll, 4);
        }

        // And halfway through the fade the tilt is still whole: it does not sag toward
        // level and come back.
        [Fact]
        public void Blend_PausedDoesNotFadeRollThroughTheTransition()
        {
            AdsPose absolute = Pose(8f, -20f, 3f, 0f, 0f, 4f);
            AdsPose blended = AdsPoseBlend.Blend(AdsMode.Paused, 0.5f, absolute, Pose(0f, 0f, 7f));
            Assert.Equal(4f, blended.Pitch, 4);
            Assert.Equal(-10f, blended.Yaw, 4);
            Assert.Equal(2f, blended.Z, 4);
            Assert.Equal(3f, blended.Roll, 4);
        }

        // The tracked modes land on the entry-relative pose, whose roll is the absolute one
        // already, so the two branches agree about roll and about nothing else.
        [Theory]
        [InlineData(AdsMode.Marker)]
        [InlineData(AdsMode.Tracked)]
        public void Blend_TrackedLandsOnTheEntryRelativePose(AdsMode mode)
        {
            AdsPose absolute = Pose(5f, -12f, 3f, 1f, 2f, 3f);
            AdsPose relative = Pose(2f, -4f, 7f, 0.5f, 0.5f, 1f);
            AdsPose blended = AdsPoseBlend.Blend(mode, 0.0f, absolute, relative);
            Assert.Equal(2f, blended.Pitch, 4);
            Assert.Equal(-4f, blended.Yaw, 4);
            Assert.Equal(0.5f, blended.X, 4);
            Assert.Equal(1f, blended.Z, 4);
            Assert.Equal(3f, blended.Roll, 4);
        }

        // Mid-fade, which is the only place the interpolation curve itself is observable.
        // Tested at the two endpoints alone, ANY curve passes.
        [Fact]
        public void Blend_TrackedIsLinearInTheScale()
        {
            AdsPose absolute = Pose(5f, -12f, 3f, 1f, 2f, 3f);
            AdsPose relative = Pose(2f, -4f, 7f, 0.5f, 0.5f, 1f);
            AdsPose blended = AdsPoseBlend.Blend(AdsMode.Tracked, 0.25f, absolute, relative);
            Assert.Equal(5f * 0.25f + 2f * 0.75f, blended.Pitch, 4);
            Assert.Equal(-12f * 0.25f + -4f * 0.75f, blended.Yaw, 4);
            Assert.Equal(1f * 0.25f + 0.5f * 0.75f, blended.X, 4);
            Assert.Equal(3f, blended.Roll, 4);
        }
    }
}
