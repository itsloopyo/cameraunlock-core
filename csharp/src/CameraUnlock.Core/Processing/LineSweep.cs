using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>What one line cast found. C# twin of cameraunlock::camera::LineHit.</summary>
    public struct LineHit
    {
        /// <summary>False when the cast could not run at all.</summary>
        public bool Queried;

        /// <summary>True when the cast met something within its length.</summary>
        public bool Hit;

        /// <summary>From the cast's start along its direction, in the caller's units.</summary>
        public float Distance;

        /// <summary>
        /// The surface normal at the hit. Either facing is fine; only its angle to the cast is
        /// used. A zero normal is read as the worst angle allowed.
        /// </summary>
        public Vec3 Normal;

        /// <summary>The cast ran and met nothing.</summary>
        public static LineHit Miss => new LineHit { Queried = true };

        /// <summary>The cast ran and met a surface <paramref name="distance"/> along it.</summary>
        public static LineHit At(float distance, Vec3 normal) =>
            new LineHit { Queried = true, Hit = true, Distance = distance, Normal = normal };
    }

    /// <summary>
    /// The engine's line cast. <paramref name="direction"/> is a unit vector,
    /// <paramref name="length"/> how far to look. It must skip the player's own body: the
    /// eye starts inside it.
    /// </summary>
    public delegate LineHit LineCast(Vec3 start, Vec3 direction, float length);

    /// <summary>C# twin of cameraunlock::camera::LineSweepSettings.</summary>
    public struct LineSweepSettings
    {
        /// <summary>The sphere's radius, in the caller's units. Must equal the clamp's skin.</summary>
        public float Radius;

        /// <summary>Rays in the ring around the centre ray. Each costs two casts (the probe and the ray).</summary>
        public int RingRays;

        /// <summary>
        /// The floor on the cosine between the lean and a surface's normal. Holding the eye
        /// Radius off a flat surface met at an angle means tracing radius / cos further along
        /// the lean, which has no bound at grazing incidence. 0.25 is 75 degrees off the
        /// normal, past which the eye slides along the surface rather than into it.
        /// </summary>
        public float MinCosine;

        /// <summary>A 0.10 radius, 8 ring rays and a 0.25 cosine floor, as the C++ defaults.</summary>
        public static LineSweepSettings Default => new LineSweepSettings { Radius = 0.10f, RingRays = 8, MinCosine = 0.25f };
    }

    /// <summary>
    /// A swept sphere for <see cref="LeanClamp"/>, built out of line casts. C# twin of
    /// cameraunlock/camera/lean_line_sweep.h, and the two must keep the same behaviour case
    /// for case.
    /// <para>
    /// One line along the lean guards the centre of the eye's path and nothing around it: the
    /// eye can pass a door frame's edge or a table corner a few millimetres off the line, and
    /// the near plane then culls it. This approximates the sphere the eye needs kept clear
    /// with one ray down the centre (exact for any flat surface, at any angle), a ring of rays
    /// around it on the sphere's equator (edges and corners the centre ray passes beside), and
    /// a short sideways probe before each ring ray, so a ring ray never starts inside a
    /// surface already closer to the eye than the radius.
    /// </para>
    /// <para>
    /// The radius is the clamp's skin: the sweep hands back the travel the sphere allows PLUS
    /// the radius, and the clamp takes the skin off again, so set <see cref="LineSweepSettings.Radius"/>
    /// and <see cref="LeanClampSettings.Skin"/> to the same number.
    /// </para>
    /// </summary>
    public sealed class LineSweep
    {
        private const float TwoPi = 6.28318530717958647692f;

        private readonly LineCast _cast;

        public LineSweep(LineCast cast)
        {
            _cast = cast;
            Query = Sweep;
        }

        public LineSweepSettings Settings { get; set; } = LineSweepSettings.Default;

        /// <summary>
        /// The <see cref="LeanQuery"/> to hand to <see cref="LeanClamp.Apply"/>. Held once, so
        /// passing it allocates nothing per frame. Any cast that cannot run makes the whole
        /// query unanswered, which the clamp reports rather than treating as clear.
        /// </summary>
        public LeanQuery Query { get; }

        private LeanObstruction Sweep(Vec3 start, Vec3 direction, float maxDistance)
        {
            LineSweepSettings settings = Settings;
            float r = settings.Radius;
            float minCos = settings.MinCosine;
            float lean = maxDistance - r;

            // The furthest the sphere's centre may travel, starting from the whole lean.
            float travel = lean;

            // Centre ray. For a flat surface at distance d whose normal is at cos c to the
            // lean, the sphere touches it once its centre has gone d - r / c.
            LineHit centre = _cast(start, direction, lean + r / minCos);
            if (!centre.Queried) return LeanObstruction.Failed;
            if (centre.Hit)
            {
                float c = System.Math.Abs(Vec3.Dot(direction, centre.Normal));
                if (!(c > minCos)) c = minCos;
                float t = centre.Distance - r / c;
                if (t < travel) travel = t;
            }

            Vec3 u, v;
            PerpendicularBasis(direction, out u, out v);
            int n = settings.RingRays;
            for (int k = 0; k < n; k++)
            {
                float angle = TwoPi * k / n;
                Vec3 side = u * (float)System.Math.Cos(angle) + v * (float)System.Math.Sin(angle);

                // Where on the ring this ray starts. A surface closer beside the eye than the
                // radius pulls it in, halfway to that surface, so it starts in open space and
                // runs parallel to the lean.
                float offset = r;
                LineHit probe = _cast(start, side, r);
                if (!probe.Queried) return LeanObstruction.Failed;
                if (probe.Hit && probe.Distance < r) offset = probe.Distance * 0.5f;

                // A point at `offset` off the centre line is on the sphere's surface
                // sqrt(r^2 - offset^2) ahead of the centre, so the sphere touches what this ray
                // meets at d once its centre has gone d minus that.
                float ahead = (float)System.Math.Sqrt(r * r - offset * offset);
                LineHit ray = _cast(start + side * offset, direction, lean + ahead);
                if (!ray.Queried) return LeanObstruction.Failed;
                if (ray.Hit)
                {
                    float t = ray.Distance - ahead;
                    if (t < travel) travel = t;
                }
            }

            if (travel >= lean) return LeanObstruction.Clear;
            // LeanClamp subtracts its skin (== r) from this.
            return LeanObstruction.Hit((travel > 0f ? travel : 0f) + r);
        }

        // Two unit vectors perpendicular to d and to each other, crossed with the world axis
        // least aligned with d so the cross product never degenerates.
        private static void PerpendicularBasis(Vec3 d, out Vec3 u, out Vec3 v)
        {
            float ax = System.Math.Abs(d.X), ay = System.Math.Abs(d.Y), az = System.Math.Abs(d.Z);
            Vec3 helper = (ax <= ay && ax <= az) ? new Vec3(1f, 0f, 0f)
                        : (ay <= az) ? new Vec3(0f, 1f, 0f)
                        : new Vec3(0f, 0f, 1f);
            Vec3 a = Vec3.Cross(d, helper);
            u = a * (1f / a.Magnitude);
            v = Vec3.Cross(d, u);
        }
    }
}
