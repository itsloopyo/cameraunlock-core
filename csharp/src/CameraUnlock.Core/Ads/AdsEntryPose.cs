namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// One frame's head pose at the engine boundary, in the caller's own units: engine
    /// degrees and engine position units, whatever the camera boundary hands over.
    /// <para>
    /// Immutable, and readonly where the framework supports it, matching
    /// <see cref="Data.Vec3"/> and <see cref="Data.Quat4"/>. This is public API: a mutable
    /// shape could not be tightened later without breaking any consumer that had written
    /// to a field.
    /// </para>
    /// </summary>
#if !NET35 && !NET40
    public readonly struct AdsPose
#else
    public struct AdsPose
#endif
    {
        public readonly float Pitch;
        public readonly float Yaw;
        public readonly float Roll;
        public readonly float X;
        public readonly float Y;
        public readonly float Z;

        public AdsPose(float pitch, float yaw, float roll, float x, float y, float z)
        {
            Pitch = pitch;
            Yaw = yaw;
            Roll = roll;
            X = x;
            Y = y;
            Z = z;
        }
    }

    /// <summary>
    /// The pose the tracked ADS modes feed the camera: whatever the head is doing, measured
    /// from the pose the sights came up on. C# twin of cameraunlock/ads/entry_pose.h.
    /// <para>
    /// Both tracked modes make the same swing onto the aim point that
    /// <see cref="AdsMode.Paused"/> makes, and then keep tracking from there. That falls out
    /// of feeding poses RELATIVE to the entry frame: at the moment the sights come up the
    /// relative pose is identity, which is the same place the paused fade arrives at, and
    /// from there the head moves the view again. Lowering the weapon hands back the
    /// absolute pose, so the view swings back by exactly the angle the head is holding.
    /// </para>
    /// <para>
    /// Four rules, none of them decoration. Each was wrong in the first cut of the
    /// reference mod, and not one of them is visible from a settings or a gate test:
    /// </para>
    /// <list type="bullet">
    /// <item><description><b>Yaw, pitch and position go relative; roll stays absolute.</b>
    /// Yaw and pitch are the aim axes and zeroing them is the whole point of the snap. Roll
    /// moves no aim point, so zeroing it yanks a head tilt the player is actively holding
    /// back to level and leans it in again as they move: two horizon jolts per aim, buying
    /// nothing.</description></item>
    /// <item><description><b>Yaw uses the shortest-angle delta.</b> It arrives wrapped into
    /// -180..180, so a plain subtraction reads a 10 degree move across the seam as -350 and
    /// whips the view a full turn the wrong way. Pitch is bounded by the tracker's own asin
    /// and cannot wrap, so it stays a plain difference.</description></item>
    /// <item><description><b>Capture from a LIVE rotation.</b> Interpolators are reset on
    /// suppressed frames and return nothing until a fresh packet lands; capturing then
    /// freezes a pre-suppression pose and holds the whole aim at that offset. The path that
    /// hits it is: aim, open a menu or press the ADS key, move your head, come back with
    /// the sights still up.</description></item>
    /// <item><description><b>Drop the entry pose wherever tracking is suppressed</b> - menu,
    /// cinematic, master toggle, tracker dropout - so the next aim re-enters cleanly instead
    /// of resuming against a pose from before the suppression.</description></item>
    /// </list>
    /// <para>Pure: no clock, no game, no logging. This only ever subtracts.</para>
    /// </summary>
    public sealed class AdsEntryPose
    {
        private bool _have;
        private AdsPose _entry;

        /// <summary>
        /// Called once per frame with this frame's absolute pose. <paramref name="live"/>
        /// says the rotation is a real sample rather than the nothing a suppressed frame
        /// publishes.
        /// </summary>
        public AdsPose Relative(bool aiming, bool live, AdsPose absolute)
        {
            if (!aiming)
            {
                Reset();
                return absolute;
            }
            if (!_have)
            {
                if (!live) return absolute;
                _entry = absolute;
                _have = true;
            }

            return new AdsPose(
                absolute.Pitch - _entry.Pitch,
                ShortestDeltaDegrees(absolute.Yaw, _entry.Yaw),
                absolute.Roll,
                absolute.X - _entry.X,
                absolute.Y - _entry.Y,
                absolute.Z - _entry.Z);
        }

        /// <summary>Drops the entry pose. Called on every frame tracking is suppressed.</summary>
        public void Reset()
        {
            _have = false;
        }

        /// <summary>True while an entry pose is captured.</summary>
        public bool HasEntry
        {
            get { return _have; }
        }

        /// <summary>
        /// Signed difference wrapped into -180..180, so a move across the seam is the short
        /// way round. Modulo rather than a subtract-until loop: a NaN would spin that loop
        /// forever on the render thread.
        /// </summary>
        public static float ShortestDeltaDegrees(float a, float b)
        {
            float d = (a - b + 180.0f) % 360.0f;
            if (d < 0.0f) d += 360.0f;
            return d - 180.0f;
        }
    }
}
