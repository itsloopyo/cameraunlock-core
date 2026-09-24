namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The approved rule by which a legacy import's map left a value out of the migrated file.
    /// The numbers match the C++ <c>cameraunlock::config::DropRule</c>.
    /// </summary>
    public enum DropRule
    {
        /// <summary>
        /// N1: a hotkey code outside 0x01-0xFE imports as unbound. Only native imports meet it,
        /// since no C# import reads virtual-key codes.
        /// </summary>
        KeyCodeOutOfRange = 1,

        /// <summary>
        /// N2: a non-finite float or double imports as the row's default
        /// (<see cref="LegacyNormalisations"/>).
        /// </summary>
        NonFiniteNumber = 2,

        /// <summary>
        /// A sensitivity, deadzone, response curve or axis inversion the player set away from the
        /// shipped default. The tracker shapes the pose; the mod no longer does. A shipped default
        /// that is not identity moves into the mod's axis conversion instead and is not dropped.
        /// </summary>
        PoseShaping = 3,

        /// <summary>A reticle setting: mods no longer draw or toggle a reticle.</summary>
        Reticle = 4,

        /// <summary>
        /// A feature that shipped disabled pending verification now follows the mod's default.
        /// The map records one only where the legacy value differs from that default.
        /// </summary>
        FollowsDefault = 5,
    }
}
