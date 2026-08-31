namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// What the ADS fade does to the frame's head pose, and the one axis it leaves alone.
    /// C# twin of cameraunlock/ads/ads_blend.h.
    /// <para>
    /// <paramref name="scale"/> is <see cref="AdsFade"/>'s: 1 at the hip, 0 with the sights
    /// up, easing between the two across the transition. <c>absolute</c> is this frame's
    /// head pose at the engine boundary; <c>relative</c> is the same pose measured from the
    /// frame the sights came up on (<see cref="AdsEntryPose"/>).
    /// </para>
    /// <list type="bullet">
    /// <item><description><see cref="AdsMode.Paused"/>: the pose fades to nothing and stays
    /// there, so the sight picture is the game's own.</description></item>
    /// <item><description><see cref="AdsMode.Marker"/> and <see cref="AdsMode.Tracked"/>:
    /// it fades into the entry-relative pose, which is identity at the moment the sights
    /// come up, so the swing onto the aim is the same one <see cref="AdsMode.Paused"/>
    /// makes and head tracking carries on from there rather than from
    /// centre.</description></item>
    /// </list>
    /// <para>
    /// <b>ROLL is in neither fade.</b> What raising the sights buys is a sight picture down
    /// the barrel, and a head tilt moves neither the eye off the barrel nor the aim point
    /// off the middle of the frame: a pure roll leaves the camera's forward vector exactly
    /// where it was and turns the whole picture about it, gun and irons included. Fading it
    /// out levels a tilt the player is actively holding and leans it back in as the weapon
    /// drops, two horizon jolts per aim, buying nothing. <see cref="AdsEntryPose"/> already
    /// applies that rule in the tracked modes, where relative roll IS the absolute roll;
    /// <see cref="AdsMode.Paused"/> was the odd one out, and in a game whose cockpit turns
    /// with the head, giving the eye a fixed frame to read the horizon against, it read as
    /// roll switching off the moment the sights came up.
    /// </para>
    /// </summary>
    public static class AdsPoseBlend
    {
        /// <summary>Blends this frame's pose for the mode and fade scale given.</summary>
        public static AdsPose Blend(AdsMode mode, float scale, AdsPose absolute, AdsPose relative)
        {
            // Roll is the absolute one in both branches. See the note above.
            if (mode == AdsMode.Paused)
            {
                // The lean rides the same fade as the rotation rather than being cut at the
                // edge: the sights sit on the muzzle line, so an eye offset from it moves
                // the sight picture off the target, and cutting it in one frame is the jolt
                // the fade exists to remove.
                return new AdsPose(
                    absolute.Pitch * scale,
                    absolute.Yaw * scale,
                    absolute.Roll,
                    absolute.X * scale,
                    absolute.Y * scale,
                    absolute.Z * scale);
            }

            float rest = 1.0f - scale;
            return new AdsPose(
                absolute.Pitch * scale + relative.Pitch * rest,
                absolute.Yaw * scale + relative.Yaw * rest,
                absolute.Roll,
                absolute.X * scale + relative.X * rest,
                absolute.Y * scale + relative.Y * rest,
                absolute.Z * scale + relative.Z * rest);
        }
    }
}
