using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// The lean split between its two carriers (see <see cref="LeanHandover"/>).
    /// C# twin of cameraunlock::ads::LeanShares.
    /// </summary>
    public struct LeanShares
    {
        /// <summary>Moves the rendered view and nothing else.</summary>
        public Vec3 Camera;

        /// <summary>
        /// Moves the transform the camera, the arms, the weapon and the round's start point
        /// all hang off, so the sights stay on the eye and the round leaves from it.
        /// </summary>
        public Vec3 Rig;
    }

    /// <summary>
    /// Hands a positional lean over from the camera to the rig as the sights come up, and
    /// back as they come down, riding <see cref="AdsFade"/>. C# twin of
    /// cameraunlock/ads/lean_handover.h (see the shooter-ads-handling skill, "Carry the lean
    /// on the rig").
    /// <para>
    /// The lean goes in whole and already clamped against the world: the clamp runs once,
    /// before the split, because the rig carries the muzzle and the start point and an
    /// unclamped share would shoot through the wall. Both shares come back in the space the
    /// lean went in, so a mod converts the rig's share through world space when its rig's
    /// local frame is not the camera's; the two shares always sum to the lean, so the eye
    /// lands in the same place whichever carrier holds it.
    /// </para>
    /// <para>
    /// Only the camera's share opens a gap between the eye and the round, so it is the only
    /// share the aim hook and the reticle are handed.
    /// </para>
    /// </summary>
    public sealed class LeanHandover
    {
        private readonly AdsFade _fade = new AdsFade();
        private bool _rigEngaged;

        /// <summary>
        /// Once per rendered frame the lean is applied. <paramref name="aiming"/> is the ADS
        /// state for this frame, polled rather than latched. In true free look the camera
        /// keeps the lean through the aim. <paramref name="rigAvailable"/> is false wherever
        /// the rig must stay where the game puts it (mounted in a vehicle seat, say): the
        /// lean then eases out on the sights instead, and the rig carries nothing.
        /// </summary>
        public LeanShares Update(Vec3 lean, bool aiming, bool trueFreeLook, bool rigAvailable, ulong nowMs)
        {
            float cameraShare = _fade.Update(aiming && !trueFreeLook, nowMs);
            var shares = new LeanShares
            {
                Camera = lean * cameraShare,
                Rig = rigAvailable ? lean * (1.0f - cameraShare) : Vec3.Zero,
            };
            _rigEngaged = shares.Rig.X != 0.0f || shares.Rig.Y != 0.0f || shares.Rig.Z != 0.0f;
            return shares;
        }

        /// <summary>
        /// Every path that stops the lean - position off, tracking suspended, a menu, a camera
        /// cut - calls this instead of <see cref="Update"/>. Returns true when the last Update
        /// left a share on the rig, which the mod then puts back where the game had it,
        /// because nothing in the game resets it. The next Update starts at the hip.
        /// </summary>
        public bool Stop()
        {
            bool release = _rigEngaged;
            _rigEngaged = false;
            _fade.Reset();
            return release;
        }
    }
}
