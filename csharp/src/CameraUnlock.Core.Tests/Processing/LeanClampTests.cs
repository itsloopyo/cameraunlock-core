using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using Xunit;

namespace CameraUnlock.Core.Tests.Processing
{
    /// <summary>
    /// Case for case with cpp/tests/lean_clamp_tests.cpp. The two things most likely to be
    /// "tidied" later are the two that make the clamp work: the asymmetry between tightening
    /// and releasing, and the difference between a query that answered "clear" and one that
    /// could not answer at all.
    /// </summary>
    public class LeanClampTests
    {
        private const float Dt = 0.016f;

        private sealed class World
        {
            public bool Queried = true;
            public bool Blocked;
            public float Distance;
            public int Calls;

            public LeanObstruction Query(Vec3 start, Vec3 direction, float maxDistance)
            {
                Calls++;
                return new LeanObstruction { Queried = Queried, Blocked = Blocked, Distance = Distance };
            }
        }

        private static LeanClamp MakeClamp(float skin, float releaseSmoothing = 0.9f)
        {
            return new LeanClamp { Settings = new LeanClampSettings { Skin = skin, ReleaseSmoothing = releaseSmoothing } };
        }

        private static void Near(float expected, float actual, float eps = 1e-4f)
        {
            Assert.InRange(actual, expected - eps, expected + eps);
        }

        [Fact]
        public void NullQueryPassesThrough()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            Vec3 output = clamp.Apply(new Vec3(1f, 2f, 3f), new Vec3(0.3f, 0f, 0f), Dt, null);

            Near(0.3f, output.X);
            Near(0f, output.Y);
            Near(0f, output.Z);
            Assert.False(clamp.InContact);
            Assert.False(clamp.LastQueryFailed);
        }

        [Fact]
        public void ClearPathPassesThrough()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World();
            Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(0f, 0f, 0.4f), Dt, world.Query);

            Assert.Equal(1, world.Calls);
            Near(0.4f, output.Z);
            Assert.False(clamp.InContact);
        }

        [Fact]
        public void BlockedLeanStopsShortBySkin()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Blocked = true, Distance = 0.25f };
            Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            Near(0.15f, output.X);
            Assert.True(clamp.InContact);
        }

        [Fact]
        public void DirectionIsPreserved()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Blocked = true, Distance = 0.30f };
            Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(0.3f, 0.4f, 0f), Dt, world.Query);

            Near(0.20f, output.Magnitude);
            Near(0.12f, output.X);
            Near(0.16f, output.Y);
        }

        [Fact]
        public void NoRoomCollapsesToZero()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Blocked = true, Distance = 0.04f };
            Near(0f, clamp.Apply(Vec3.Zero, new Vec3(0.3f, 0f, 0f), Dt, world.Query).Magnitude);

            clamp.Reset();
            world.Distance = 0f;
            Near(0f, clamp.Apply(Vec3.Zero, new Vec3(0.3f, 0f, 0f), Dt, world.Query).Magnitude);
        }

        [Fact]
        public void TighteningIsInstant()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World();
            clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            world.Blocked = true;
            world.Distance = 0.15f;
            Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            Near(0.05f, output.X);
        }

        [Fact]
        public void ReleaseIsDamped()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Blocked = true, Distance = 0.15f };
            clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            world.Blocked = false;
            Vec3 first = clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);
            Assert.True(first.X > 0.05f, "the allowance opens once the obstruction clears");
            Assert.True(first.X < 0.4f, "the allowance does not jump straight back to full");
            Assert.True(clamp.InContact);

            for (int frame = 0; frame < 240; frame++)
                clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);
            Vec3 settled = clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            Near(0.4f, settled.X, 1e-3f);
            Assert.False(clamp.InContact);
        }

        [Fact]
        public void FailedQueryIsReportedNotAbsorbed()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Queried = false };
            Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            Near(0.4f, output.X);
            Assert.True(clamp.LastQueryFailed);
            Assert.False(clamp.InContact);

            world.Queried = true;
            clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);
            Assert.False(clamp.LastQueryFailed);
        }

        [Fact]
        public void NeutralPoseClearsTheAllowance()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Blocked = true, Distance = 0.12f };
            clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);

            int callsBefore = world.Calls;
            Vec3 neutral = clamp.Apply(Vec3.Zero, Vec3.Zero, Dt, world.Query);
            Near(0f, neutral.Magnitude);
            Assert.Equal(callsBefore, world.Calls);

            world.Blocked = false;
            Near(0.4f, clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query).X);
        }

        [Fact]
        public void GrowingLeanInOpenSpaceNeverReportsContact()
        {
            LeanClamp clamp = MakeClamp(10f);
            var world = new World();

            for (int frame = 1; frame <= 30; frame++)
            {
                float reach = 30f * (frame / 30f);
                Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(reach, 0f, 0f), Dt, world.Query);
                Near(reach, output.X, 1e-3f);
                Assert.False(clamp.InContact);
            }
        }

        [Fact]
        public void ReleaseSettlesInEngineUnits()
        {
            LeanClamp clamp = MakeClamp(10f);
            var world = new World { Blocked = true, Distance = 15f };
            clamp.Apply(Vec3.Zero, new Vec3(30f, 0f, 0f), Dt, world.Query);
            Assert.True(clamp.InContact);

            world.Blocked = false;
            for (int frame = 0; frame < 120; frame++)
                clamp.Apply(Vec3.Zero, new Vec3(30f, 0f, 0f), Dt, world.Query);

            Assert.False(clamp.InContact);
        }

        [Fact]
        public void ResetDropsTheAllowance()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            var world = new World { Blocked = true, Distance = 0.12f };
            clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query);
            Assert.True(clamp.InContact);

            clamp.Reset();
            Assert.False(clamp.InContact);

            world.Blocked = false;
            Near(0.4f, clamp.Apply(Vec3.Zero, new Vec3(0.4f, 0f, 0f), Dt, world.Query).X);
        }

        [Fact]
        public void QueryIsAskedToReachPastTheLeanByTheSkin()
        {
            LeanClamp clamp = MakeClamp(0.10f);
            float asked = 0f;
            clamp.Apply(Vec3.Zero, new Vec3(0.3f, 0f, 0f), Dt, (start, direction, maxDistance) =>
            {
                asked = maxDistance;
                return LeanObstruction.Clear;
            });

            Near(0.40f, asked);
        }
    }
}
