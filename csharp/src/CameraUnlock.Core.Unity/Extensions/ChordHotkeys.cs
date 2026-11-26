using UnityEngine;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Ctrl+Shift+letter chord hotkeys for keyboards without a nav cluster.
    /// Letters come from the T/Y/U/G/H/J block (a 2x3 cluster in the middle of the
    /// keyboard, easy to find by touch). Ctrl+Shift chords are universally avoided
    /// by game bindings, so the same chord maps to the same action in every mod.
    /// </summary>
    public static class ChordHotkeys
    {
        /// <summary>Chord letter for recenter (Ctrl+Shift+T).</summary>
        public const KeyCode RecenterLetter = KeyCode.T;

        /// <summary>Chord letter for toggling tracking (Ctrl+Shift+Y).</summary>
        public const KeyCode ToggleLetter = KeyCode.Y;

        /// <summary>Chord letter for toggling/cycling position tracking (Ctrl+Shift+G).</summary>
        public const KeyCode PositionLetter = KeyCode.G;

        /// <summary>Chord letter for a mod's fourth toggle (Ctrl+Shift+H).</summary>
        public const KeyCode FourthToggleLetter = KeyCode.H;

        /// <summary>Chord letter for a mod's fifth toggle (Ctrl+Shift+U).</summary>
        public const KeyCode FifthToggleLetter = KeyCode.U;

        /// <summary>Chord letter for a mod's sixth toggle (Ctrl+Shift+J).</summary>
        public const KeyCode SixthToggleLetter = KeyCode.J;

        /// <summary>
        /// Returns true on the frame either the action's primary key or its
        /// Ctrl+Shift chord alternative is pressed.
        /// </summary>
        public static bool IsActionPressed(KeyCode primaryKey, KeyCode chordLetter)
        {
            return UnityEngine.Input.GetKeyDown(primaryKey) || IsPressed(chordLetter);
        }

        /// <summary>
        /// Returns true on the frame the chord letter is pressed down while both
        /// Ctrl and Shift (either side) are held.
        /// </summary>
        public static bool IsPressed(KeyCode letter)
        {
            if (!UnityEngine.Input.GetKeyDown(letter))
            {
                return false;
            }

            bool ctrl = UnityEngine.Input.GetKey(KeyCode.LeftControl) || UnityEngine.Input.GetKey(KeyCode.RightControl);
            bool shift = UnityEngine.Input.GetKey(KeyCode.LeftShift) || UnityEngine.Input.GetKey(KeyCode.RightShift);
            return ctrl && shift;
        }
    }
}
