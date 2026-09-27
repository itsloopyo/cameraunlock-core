namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The approved rule by which a legacy import's map left a value out of the migrated file.
    /// The numbers match the C++ <c>cameraunlock::config::DropRule</c>.
    /// </summary>
    public enum DropRule
    {
        /// <summary>
        /// N2: a non-finite float or double imports as the row's default
        /// (<see cref="LegacyNormalisations"/>), and on a row that follows Defaults.ini is written
        /// <c>default</c>.
        /// </summary>
        NonFiniteNumber = 1,

        /// <summary>
        /// A sensitivity, unit scale, deadzone, response curve or axis inversion the player set away
        /// from the shipped default. The tracker shapes the pose; the mod no longer does. A shipped
        /// unit scale, and any other shipped default that is not identity, moves into the mod's axis
        /// conversion instead and is not dropped.
        /// </summary>
        PoseShaping = 2,

        /// <summary>A reticle setting: mods no longer draw or toggle a reticle.</summary>
        Reticle = 3,

        /// <summary>
        /// A feature that shipped disabled pending verification now follows the mod's default.
        /// The map records one only where the legacy value differs from that default.
        /// </summary>
        FollowsDefault = 4,

        /// <summary>
        /// N1: a hotkey code outside 0x01-0xFE imports as unbound (C++
        /// <c>LegacyVirtualKeyToBindings</c>). Only native imports meet it: no C# import reads
        /// virtual-key codes.
        /// </summary>
        KeyCodeOutOfRange = 5,

        /// <summary>
        /// N3: a hotkey bound to a Ctrl, Shift or Alt key on its own imports as unbound (C++
        /// <c>LegacyVirtualKeyToBindings</c>, C# <see cref="LegacyNormalisations.KeyCodeToBindings"/>).
        /// </summary>
        ModifierKey = 6,

        /// <summary>
        /// An aim decoupling switch set to false, which ran coupled aim. Aim is always decoupled
        /// now, so the setting has no row. The map records one only where the legacy value is
        /// false; a true value changes nothing.
        /// </summary>
        CoupledAim = 7,

        /// <summary>
        /// A position switch set to false, one that also kept the mode hotkey off the position
        /// modes (the setting PositionAllowed was added for). The tracking mode is the only way
        /// position is switched off now, so the map writes the rotation-only mode (RotationEnabled
        /// true, PositionEnabled false) and records the switch (<see cref="LegacyPositionSwitch"/>).
        /// A true value changes nothing and is not recorded.
        /// </summary>
        PositionSwitchOff = 8,

        /// <summary>
        /// A neck pivot distance (TrackerPivotForward, TrackerPivotUp or another spelling) the
        /// player changed from the value the game shipped. The tracker is authoritative, so the
        /// setting has no row (<see cref="LegacyTrackerPivot"/>). A value equal to the shipped one
        /// is not recorded.
        /// </summary>
        TrackerPivot = 9,
    }
}
