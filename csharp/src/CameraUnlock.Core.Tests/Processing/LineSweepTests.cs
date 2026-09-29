using System.Collections.Generic;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Processing;
using Xunit;

namespace CameraUnlock.Core.Tests.Processing
{
    /// <summary>
    /// Case for case with cpp/tests/lean_line_sweep_tests.cpp, against the same analytic world
    /// of boxes and planes. The case the sweep exists for is EdgeBesideTheCentreLineBlocks:
    /// one line down the middle of the lean passes a door frame's edge and reports clear.
    /// </summary>
    public class LineSweepTests
    {
        private struct Box
        {
            public Vec3 Min, Max;
            public Box(Vec3 min, Vec3 max) { Min = min; Max = max; }
        }

        // A half-space: everything on the side of the plane the normal points away from.
        private struct Wall
        {
            public Vec3 Normal; // unit, facing the eye
            public float Offset; // plane: dot(normal, p) == offset
            public Wall(Vec3 normal, float offset) { Normal = normal; Offset = offset; }
        }

        private sealed class World
        {
            public readonly List<Box> Boxes = new List<Box>();
            public readonly List<Wall> Walls = new List<Wall>();
            public bool Fail;
            public int Casts;

            public LineHit Cast(Vec3 s, Vec3 d, float length)
            {
                Casts++;
                if (Fail) return new LineHit();
                LineHit best = LineHit.Miss;
                foreach (Box b in Boxes) best = Nearer(best, CastBox(b, s, d, length));
                foreach (Wall w in Walls) best = Nearer(best, CastWall(w, s, d, length));
                return best;
            }

            private static LineHit Nearer(LineHit best, LineHit h) =>
                h.Hit && (!best.Hit || h.Distance < best.Distance) ? h : best;
        }

        // Slab test. A ray starting inside a box does not hit it, as with a physics engine's
        // cast against a solid it starts in.
        private static LineHit CastBox(Box b, Vec3 s, Vec3 d, float length)
        {
            float tmin = 0f, tmax = length;
            int axis = -1;
            float[] so = { s.X, s.Y, s.Z }, dd = { d.X, d.Y, d.Z };
            float[] lo = { b.Min.X, b.Min.Y, b.Min.Z }, hi = { b.Max.X, b.Max.Y, b.Max.Z };
            for (int i = 0; i < 3; i++)
            {
                if (System.Math.Abs(dd[i]) < 1e-9f)
                {
                    if (so[i] < lo[i] || so[i] > hi[i]) return LineHit.Miss;
                    continue;
                }
                float t1 = (lo[i] - so[i]) / dd[i], t2 = (hi[i] - so[i]) / dd[i];
                if (t1 > t2) { float swap = t1; t1 = t2; t2 = swap; }
                if (t1 > tmin) { tmin = t1; axis = i; }
                tmax = System.Math.Min(tmax, t2);
                if (tmin > tmax) return LineHit.Miss;
            }
            if (axis < 0) return LineHit.Miss; // started inside
            return LineHit.At(tmin, new Vec3(axis == 0 ? 1f : 0f, axis == 1 ? 1f : 0f, axis == 2 ? 1f : 0f));
        }

        private static LineHit CastWall(Wall w, Vec3 s, Vec3 d, float length)
        {
            float denom = Vec3.Dot(w.Normal, d);
            if (denom >= 0f) return LineHit.Miss;
            float t = (w.Offset - Vec3.Dot(w.Normal, s)) / denom;
            if (t < 0f || t > length) return LineHit.Miss;
            return LineHit.At(t, w.Normal);
        }

        private static readonly Vec3 Forward = new Vec3(0f, 0f, 1f);

        private static LineSweep MakeSweep(World world, float radius = 0.1f)
        {
            LineSweepSettings settings = LineSweepSettings.Default;
            settings.Radius = radius;
            return new LineSweep(world.Cast) { Settings = settings };
        }

        // LeanClamp's call: the lean plus the skin.
        private static LeanObstruction Ask(LineSweep sweep, float lean, Vec3 dir) =>
            sweep.Query(Vec3.Zero, dir, lean + sweep.Settings.Radius);

        private static void Near(float expected, float actual, float eps = 1e-4f) =>
            Assert.InRange(actual, expected - eps, expected + eps);

        [Fact]
        public void OpenSpaceIsClear()
        {
            var world = new World();
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.3f, Forward);
            Assert.True(o.Queried && !o.Blocked);
            Assert.Equal(1 + 2 * sweep.Settings.RingRays, world.Casts);
        }

        [Fact]
        public void WallFacingTheLean()
        {
            var world = new World();
            world.Walls.Add(new Wall(new Vec3(0f, 0f, -1f), -0.35f)); // plane z = 0.35
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.3f, Forward);
            Assert.True(o.Blocked);
            Near(0.25f, o.Distance - sweep.Settings.Radius);

            world.Walls[0] = new Wall(new Vec3(0f, 0f, -1f), -1f);
            Assert.False(Ask(sweep, 0.3f, Forward).Blocked);
        }

        [Fact]
        public void ObliqueWallHoldsTheRadiusAlongItsNormal()
        {
            var world = new World();
            // Plane through (0, 0, 0.5) with its normal 60 degrees off the lean.
            var n = new Vec3((float)System.Math.Sin(1.04719755), 0f, -(float)System.Math.Cos(1.04719755));
            world.Walls.Add(new Wall(n, Vec3.Dot(n, new Vec3(0f, 0f, 0.5f))));
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.45f, Forward);
            // Centre stops at 0.5 - 0.1 / cos(60) = 0.3, where it is 0.1 off the plane.
            Assert.True(o.Blocked);
            Near(0.3f, o.Distance - 0.1f, 1e-3f);
            var at = new Vec3(0f, 0f, o.Distance - 0.1f);
            Near(0.1f, System.Math.Abs(Vec3.Dot(n, at) - world.Walls[0].Offset), 1e-3f);
        }

        [Fact]
        public void EdgeBesideTheCentreLineBlocks()
        {
            var world = new World();
            // A door frame: its face at z = 0.2, starting 5 cm to the side of the lean.
            world.Boxes.Add(new Box(new Vec3(0.05f, -1f, 0.2f), new Vec3(1f, 1f, 0.4f)));
            LineSweep sweep = MakeSweep(world);

            Assert.False(world.Cast(Vec3.Zero, Forward, 1f).Hit);

            LeanObstruction o = Ask(sweep, 0.3f, Forward);
            Assert.True(o.Blocked);
            Assert.True(o.Distance - 0.1f <= 0.2f + 1e-4f);
        }

        [Fact]
        public void SlidingAlongAWallBesideTheEye()
        {
            var world = new World();
            // A wall running along the lean, 4 cm to the side: closer than the radius.
            world.Boxes.Add(new Box(new Vec3(0.04f, -1f, -1f), new Vec3(1f, 1f, 2f)));
            LeanObstruction o = Ask(MakeSweep(world), 0.3f, Forward);
            Assert.True(o.Queried && !o.Blocked);
        }

        [Fact]
        public void FailedCastIsUnanswered()
        {
            var world = new World { Fail = true };
            Assert.False(Ask(MakeSweep(world), 0.3f, Forward).Queried);
        }

        [Fact]
        public void ClampStopsAtTheSweep()
        {
            var world = new World();
            world.Boxes.Add(new Box(new Vec3(0.05f, -1f, 0.2f), new Vec3(1f, 1f, 0.4f)));
            LineSweep sweep = MakeSweep(world);
            var clamp = new LeanClamp
            {
                Settings = new LeanClampSettings { Skin = sweep.Settings.Radius, ReleaseSmoothing = 0.9f }
            };
            Vec3 output = clamp.Apply(Vec3.Zero, new Vec3(0f, 0f, 0.3f), 0.016f, sweep.Query);
            Assert.True(clamp.InContact);
            Assert.True(output.Z <= 0.2f + 1e-4f && output.Z > 0f);
        }

        [Fact]
        public void LeanInAnyDirection()
        {
            var world = new World();
            world.Walls.Add(new Wall(new Vec3(0f, -1f, 0f), -0.3f)); // ceiling at y = 0.3
            LeanObstruction o = Ask(MakeSweep(world), 0.25f, new Vec3(0f, 1f, 0f));
            Assert.True(o.Blocked);
            Near(0.2f, o.Distance - 0.1f);
        }
    }
}
