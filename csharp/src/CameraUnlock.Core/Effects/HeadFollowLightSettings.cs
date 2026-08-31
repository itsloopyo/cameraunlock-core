using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Effects
{
    /// <summary>
    /// A carried light that follows the head instead of the aim: the numbers and the
    /// config surface. Engine-free twin of cameraunlock/effects/head_follow_light.h; the
    /// Unity application side is CameraUnlock.Core.Unity.Effects.HeadFollowLight.
    /// <para>
    /// When the player carries a directional light - a flashlight, a torch, a headlamp, a
    /// flare - it is UNLOCKED from the aim and turned with the head. The rest of the
    /// doctrine says the game's own aim, projectile and raycast code keeps reading the
    /// clean camera rotation and we move what is DRAWN to match; the beam is drawn, so the
    /// beam moves.
    /// </para>
    /// </summary>
    public sealed class HeadFollowLightSettings
    {
        /// <summary>
        /// How far the light turns relative to the head. It LEADS the view rather than
        /// matching it: a player who turns their head keeps their eyes on what they turned
        /// towards, so their gaze sits past the centre of the screen, and a light matched
        /// to the view alone lands short of what they are actually looking at. The miss is
        /// largest exactly when the head is turned furthest, which is when a light is most
        /// likely to be the reason they turned.
        /// <para>Changing this is a breaking change; add a new field instead.</para>
        /// </summary>
        public const float DefaultMultiplier = 1.5f;

        /// <summary>
        /// Anything past this is a mistyped number rather than a setting: at 5x a
        /// twenty-degree head turn puts the beam a full quadrant off the view, which is
        /// already past useful. A value outside [0, this] is REJECTED and the previous one
        /// stands, with a line in the log naming the key - not clamped, because silently
        /// running at 5 when the file says 8 is a setting that does not do what it says.
        /// Zero is inside the range and is a real request to pin the beam to the aim,
        /// which is what the game does unmodded.
        /// </summary>
        public const float MaxMultiplier = 5.0f;

        /// <summary>Point a carried light where the head is looking rather than where the body is aiming.</summary>
        public bool FollowsHead { get; set; } = true;

        /// <summary>How far the light turns relative to the head.</summary>
        public float Multiplier { get; set; } = DefaultMultiplier;

        /// <summary>
        /// Scales a head rotation about its own axis, which is the axis-angle spelling of
        /// an unclamped slerp from identity. The engine-free half, so every mod that holds
        /// its head pose as a ROTATION leads the beam by the same arithmetic.
        /// <para>
        /// A mod that holds the pose as three Euler angles uses
        /// <c>cameraunlock::effects::ScaleHeadEuler</c> instead, and the two are NOT
        /// interchangeable: scaling three angles by k is not scaling one axis-angle by k
        /// unless exactly one of the three is non-zero. Pick the one matching how the mod
        /// already composes the pose onto the camera, so the beam and the view agree with
        /// each other.
        /// </para>
        /// <para>
        /// A rotation of no consequence comes back as identity rather than as an
        /// indeterminate axis scaled by the multiplier: a centred head has no axis, and
        /// multiplying up whatever the floating point noise happens to name would point
        /// the beam anywhere.
        /// </para>
        /// </summary>
        public static Quat4 ScaleRotation(Quat4 head, float multiplier)
        {
            float x = head.X, y = head.Y, z = head.Z, w = head.W;

            // q and -q are the same rotation; the half-angle read off w is not. From the
            // negative hemisphere it measures the long way round, so a 2 degree head turn
            // scales to 177 degrees and the beam points behind the player - and the result
            // is unit length and finite, so nothing downstream catches it. Unity hands
            // this both hemispheres: LookRotation emits a negative w whenever the matrix
            // trace is not positive, and Transform.rotation is whatever the game stored.
            if (w < 0f)
            {
                x = -x;
                y = -y;
                z = -z;
                w = -w;
            }

            float axisLength = (float)System.Math.Sqrt(x * x + y * y + z * z);
            if (!(axisLength > 1e-6f))
            {
                return Quat4.Identity;
            }

            // atan2 of the vector part against w, rather than acos of w alone: it stays
            // well conditioned near w = 1, which is where a head barely off centre lives,
            // and it takes the axis length from the vector part instead of assuming the
            // caller passed a unit quaternion. Dividing by that same length renormalises,
            // so a slightly-off-unit input cannot come back as a scaled matrix on a
            // Transform.
            float halfAngle = (float)System.Math.Atan2(axisLength, w);
            float scaledHalf = halfAngle * multiplier;
            float k = (float)System.Math.Sin(scaledHalf) / axisLength;

            var scaled = new Quat4(x * k, y * k, z * k, (float)System.Math.Cos(scaledHalf));
            return IsFinite(scaled.X) && IsFinite(scaled.Y) && IsFinite(scaled.Z) && IsFinite(scaled.W)
                ? scaled
                : Quat4.Identity;
        }

        private static bool IsFinite(float value)
        {
            return !float.IsNaN(value) && !float.IsInfinity(value);
        }
    }
}
