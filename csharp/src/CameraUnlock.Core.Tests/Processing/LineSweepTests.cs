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
            // Casts from this many on cannot run.
            public int FailFrom = -1;
            // The cast gives no surface normal, as X-Ray's rain ray pick gives none.
            public bool NoNormals;
            public int Casts;

            public LineHit Cast(Vec3 s, Vec3 d, float length)
            {
                bool fail = Fail || (FailFrom >= 0 && Casts >= FailFrom);
                Casts++;
                if (fail) return new LineHit();
                LineHit best = LineHit.Miss;
                foreach (Box b in Boxes) best = Nearer(best, CastBox(b, s, d, length));
                foreach (Wall w in Walls) best = Nearer(best, CastWall(w, s, d, length));
                if (NoNormals) best.Normal = Vec3.Zero;
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

        // A plane through `point` whose normal is `degrees` off the lean (+z), tilted about the
        // y axis, facing the eye.
        private static Wall TiltedWall(float degrees, Vec3 point)
        {
            double a = degrees * System.Math.PI / 180.0;
            var n = new Vec3((float)System.Math.Sin(a), 0f, -(float)System.Math.Cos(a));
            return new Wall(n, Vec3.Dot(n, point));
        }

        [Fact]
        public void NoNormalWallFacingTheLean()
        {
            var world = new World { NoNormals = true };
            world.Walls.Add(new Wall(new Vec3(0f, 0f, -1f), -0.35f));
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.3f, Forward);
            Assert.True(o.Blocked);
            Near(0.25f, o.Distance - 0.1f, 1e-3f);
            Assert.Equal(1 + 4 + 2 * sweep.Settings.RingRays, world.Casts);
        }

        [Fact]
        public void NoNormalObliqueWall()
        {
            var world = new World { NoNormals = true };
            world.Walls.Add(TiltedWall(60f, new Vec3(0f, 0f, 0.5f)));
            LeanObstruction o = Ask(MakeSweep(world), 0.45f, Forward);
            Assert.True(o.Blocked);
            Near(0.3f, o.Distance - 0.1f, 1e-3f);
        }

        [Fact]
        public void NoNormalTiltedBothWays()
        {
            var world = new World { NoNormals = true };
            // Normal 40 degrees off the lean, out of the plane of either basis axis.
            var n = new Vec3(0.4f, 0.3f, -(float)System.Math.Sqrt(0.75));
            world.Walls.Add(new Wall(n, Vec3.Dot(n, new Vec3(0f, 0f, 0.6f))));
            LeanObstruction o = Ask(MakeSweep(world), 0.55f, Forward);
            Assert.True(o.Blocked);
            Near(0.6f - 0.1f / System.Math.Abs(n.Z), o.Distance - 0.1f, 1e-3f);
        }

        [Fact]
        public void NoNormalSteepWallTakesTheWorstAngle()
        {
            var world = new World { NoNormals = true };
            // 75.7 degrees, just past the worst angle allowed (75.5), and not so steep that the
            // ring ray on its near side stops the sphere first.
            world.Walls.Add(TiltedWall(75.7f, new Vec3(0f, 0f, 0.6f)));
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.55f, Forward);
            Assert.True(o.Blocked);
            Near(0.6f - 0.1f / sweep.Settings.MinCosine, o.Distance - 0.1f, 1e-3f);

            world.Walls[0] = TiltedWall(80f, new Vec3(0f, 0f, 0.6f));
            Vec3 u, v;
            LineSweep.PerpendicularBasis(Forward, out u, out v);
            float c;
            Assert.True(sweep.MeasuredCosine(sweep.Settings, Vec3.Zero, Forward, u, v, 0.6f, out c));
            Assert.Equal(sweep.Settings.MinCosine, c);
        }

        [Fact]
        public void NoNormalAtCentimetreScale()
        {
            var world = new World { NoNormals = true };
            world.Walls.Add(TiltedWall(30f, new Vec3(0f, 0f, 300f)));
            LeanObstruction o = Ask(MakeSweep(world, 15f), 295f, Forward);
            Assert.True(o.Blocked);
            Near(300f - 15f / (float)System.Math.Cos(System.Math.PI / 6.0), o.Distance - 15f, 0.05f);
        }

        [Fact]
        public void WallWithANormalCastsNoMore()
        {
            var world = new World();
            world.Walls.Add(new Wall(new Vec3(0f, 0f, -1f), -0.35f));
            LineSweep sweep = MakeSweep(world);
            Ask(sweep, 0.3f, Forward);
            Assert.Equal(1 + 2 * sweep.Settings.RingRays, world.Casts);
        }

        [Fact]
        public void NoNormalInsideCornerTakesTheWorstAngle()
        {
            var world = new World { NoNormals = true };
            // Two walls meeting in a vertical inside corner 0.6 ahead, each 45 degrees off the
            // lean. A flat reading of the corner would see a wall facing the lean and hold the
            // centre 0.1 off the corner, 0.07 off each wall.
            world.Walls.Add(TiltedWall(45f, new Vec3(0f, 0f, 0.6f)));
            world.Walls.Add(TiltedWall(-45f, new Vec3(0f, 0f, 0.6f)));
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.55f, Forward);
            Assert.True(o.Blocked);
            Near(0.6f - 0.1f / sweep.Settings.MinCosine, o.Distance - 0.1f, 1e-3f);
        }

        [Fact]
        public void NoNormalEdgeTakesTheWorstAngle()
        {
            var world = new World { NoNormals = true };
            // The centre ray meets a box's face 1 cm from its edge, so one ray beside it passes
            // the box.
            world.Boxes.Add(new Box(new Vec3(-0.01f, -1f, 0.6f), new Vec3(1f, 1f, 0.8f)));
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.55f, Forward);
            Assert.True(o.Blocked);
            Near(0.6f - 0.1f / sweep.Settings.MinCosine, o.Distance - 0.1f, 1e-3f);
        }

        [Fact]
        public void NoNormalThinObjectBeforeAWallTakesTheWorstAngle()
        {
            var world = new World { NoNormals = true };
            // A slat 1 cm wide straight ahead with a wall 2 cm behind it. The rays beside the
            // centre pass the slat and meet the wall, which looks like a bulge toward the eye but
            // says nothing about the slat's own faces.
            world.Boxes.Add(new Box(new Vec3(-0.005f, -1f, 0.6f), new Vec3(0.005f, 1f, 1f)));
            world.Walls.Add(new Wall(new Vec3(0f, 0f, -1f), -0.62f));
            LineSweep sweep = MakeSweep(world);
            LeanObstruction o = Ask(sweep, 0.55f, Forward);
            Assert.True(o.Blocked);
            Near(0.6f - 0.1f / sweep.Settings.MinCosine, o.Distance - 0.1f, 1e-3f);
        }

        [Theory]
        [InlineData(1)]
        [InlineData(2)]
        [InlineData(3)]
        [InlineData(4)]
        public void NoNormalFailedMeasureIsUnanswered(int failFrom)
        {
            var world = new World { NoNormals = true, FailFrom = failFrom };
            world.Walls.Add(new Wall(new Vec3(0f, 0f, -1f), -0.35f));
            Assert.False(Ask(MakeSweep(world), 0.3f, Forward).Queried);
        }
    }
}
