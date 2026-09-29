using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// What the engine's world query found along the lean direction. C# twin of
    /// cameraunlock::camera::LeanObstruction.
    /// </summary>
    public struct LeanObstruction
    {
        /// <summary>
        /// False when the query could not be performed at all. Distinct from a definite
        /// no-hit: see <see cref="LeanClamp.Apply"/> for why the two cannot share a value.
        /// </summary>
        public bool Queried;

        /// <summary>True when something blocks the lean. False with Queried is a definite clear path.</summary>
        public bool Blocked;

        /// <summary>From the eye along the lean direction, in the caller's world units. Only meaningful when Blocked.</summary>
        public float Distance;

        /// <summary>The query ran and nothing is in the way.</summary>
        public static LeanObstruction Clear => new LeanObstruction { Queried = true };

        /// <summary>The query ran and something is <paramref name="distance"/> along the lean.</summary>
        public static LeanObstruction Hit(float distance) =>
            new LeanObstruction { Queried = true, Blocked = true, Distance = distance };

        /// <summary>The query could not run.</summary>
        public static LeanObstruction Failed => new LeanObstruction();
    }

    /// <summary>
    /// The engine-side query. <paramref name="start"/> is the CLEAN eye position (before the
    /// lean), <paramref name="direction"/> a unit vector toward the lean target, and
    /// <paramref name="maxDistance"/> the magnitude of the lean plus the skin.
    /// </summary>
    public delegate LeanObstruction LeanQuery(Vec3 start, Vec3 direction, float maxDistance);

    /// <summary>C# twin of cameraunlock::camera::LeanClampSettings.</summary>
    public struct LeanClampSettings
    {
        /// <summary>
        /// How far off the blocking surface to hold the eye, in the caller's world units: a
        /// Unity mod working in metres passes 0.10, an Unreal mod working in centimetres 10.
        /// It must exceed the camera's near clip distance, or a wall held at the skin is
        /// culled and the player sees the room beyond it anyway. A query that already holds
        /// the eye off the surface itself (a swept sphere's centre, say) passes 0.
        /// </summary>
        public float Skin;

        /// <summary>
        /// How quickly the allowance reopens once an obstruction clears, on the fleet's 0-1
        /// smoothing scale. 0.9 is a 200ms time constant. Tightening is never smoothed.
        /// </summary>
        public float ReleaseSmoothing;

        /// <summary>A 0.10 skin and a 0.9 release, as the C++ defaults.</summary>
        public static LeanClampSettings Default => new LeanClampSettings { Skin = 0.10f, ReleaseSmoothing = 0.9f };
    }

    /// <summary>
    /// Keeps a 6DOF lean from putting the eye inside the level. C# twin of
    /// cameraunlock/camera/lean_clamp.h, and the two must keep the same behaviour case for
    /// case.
    /// <para>
    /// The problem splits in two, and only one half is engine-agnostic. Asking "is there
    /// anything between the eye and where the lean wants to go" is a physics query, different
    /// in every engine, and it stays with the mod as a <see cref="LeanQuery"/>. Deciding what
    /// to do with the answer is the same everywhere, and it is here.
    /// </para>
    /// <para>
    /// Stateful because the release is damped, so one instance per camera, and
    /// <see cref="Reset"/> whenever the camera cuts or the allowance carries a previous
    /// room's wall into the new one.
    /// </para>
    /// </summary>
    public sealed class LeanClamp
    {
        // Below this the offset has no reliable direction to query along, and the lean is
        // too small to reach anything regardless.
        private const float MinimumLean = 1e-4f;

        // How close the release has to get, as a fraction of the lean, before the allowance
        // is called full. Relative so it means the same thing whatever units the caller uses.
        private const float SettleFraction = 1e-3f;

        private float _allowed;
        // False means nothing is currently restricting the lean, which is not the same as an
        // allowance of zero.
        private bool _hasAllowance;

        public LeanClampSettings Settings { get; set; } = LeanClampSettings.Default;

        /// <summary>True when the last Apply held the eye short of where the tracker asked.</summary>
        public bool InContact { get; private set; }

        /// <summary>True when the last Apply could not run its query and passed the lean through unclamped.</summary>
        public bool LastQueryFailed { get; private set; }

        /// <summary>
        /// Returns the offset the world leaves room for, along the direction of
        /// <paramref name="desiredOffset"/>.
        /// <para>
        /// <paramref name="query"/> runs at most once per call and only when there is a lean
        /// to test. A null query is the feature switched off and passes the offset through.
        /// </para>
        /// <para>
        /// Tightening is instant and releasing is damped. Easing INTO a smaller allowance
        /// would let the eye sit inside the wall for the duration of the ease, which is the
        /// whole bug. Easing back OUT stops the view popping when an obstruction clears. The
        /// cost is that swinging from a blocked direction to a free one lags by the release
        /// time constant, because the allowance is one scalar along a direction that moves.
        /// </para>
        /// </summary>
        public Vec3 Apply(Vec3 eye, Vec3 desiredOffset, float deltaTime, LeanQuery query)
        {
            InContact = false;
            LastQueryFailed = false;

            float desired = desiredOffset.Magnitude;
            if (query == null) return desiredOffset;
            if (desired <= MinimumLean)
            {
                // Nothing to test, so nothing was blocking it either. Dropping the allowance
                // stops a wall the player has already backed away from rationing the next
                // lean through its release ease.
                _hasAllowance = false;
                return desiredOffset;
            }

            LeanClampSettings settings = Settings;
            Vec3 direction = desiredOffset * (1f / desired);
            LeanObstruction hit = query(eye, direction, desired + settings.Skin);

            if (!hit.Queried)
            {
                // Without the query there is no clamp, so the lean passes through, and that is
                // reported: a clamp that has quietly stopped clamping looks exactly like one
                // that never engaged.
                LastQueryFailed = true;
                _hasAllowance = false;
                return desiredOffset;
            }

            // A surface closer than the skin leaves nothing to give, including an eye that
            // starts inside geometry and a query that answers zero.
            float room = hit.Blocked ? hit.Distance - settings.Skin : desired;
            float target = room < 0f ? 0f : (room > desired ? desired : room);

            // With no allowance carried in, the frame starts at what the tracker asked for.
            // Easing up from zero instead would fade every lean in from a standing start.
            float allowed = _hasAllowance ? _allowed : desired;
            if (allowed > desired) allowed = desired;

            if (target < allowed)
            {
                allowed = target;
                _hasAllowance = true;
            }
            else if (_hasAllowance)
            {
                allowed += (target - allowed) *
                           SmoothingUtils.CalculateSmoothingFactor(settings.ReleaseSmoothing, deltaTime);
                // The ease is asymptotic, so without a settle the clamp reports contact
                // forever after one touch.
                if (desired - allowed <= desired * SettleFraction)
                {
                    allowed = desired;
                    _hasAllowance = false;
                }
            }
            else
            {
                // Never restricted, so there is nothing to ease away from. Easing anyway makes
                // a lean growing faster than the release rate look like one held off a wall.
                allowed = desired;
            }

            _allowed = allowed;
            InContact = allowed < desired;
            return direction * allowed;
        }

        /// <summary>Forget the current allowance. The next Apply takes its query answer outright.</summary>
        public void Reset()
        {
            _allowed = 0f;
            _hasAllowance = false;
            InContact = false;
            LastQueryFailed = false;
        }
    }
}
