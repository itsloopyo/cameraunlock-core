namespace CameraUnlock.Core.State
{
    /// <summary>
    /// Represents the current game context/state for head tracking decisions.
    /// Used to categorize game states beyond the binary IsInGameplay check.
    /// </summary>
    public enum GameContext
    {
        /// <summary>
        /// Game state could not be determined.
        /// </summary>
        Unknown = 0,

        /// <summary>
        /// Player is in a menu (main menu, pause menu, settings, etc.).
        /// Head tracking should typically be disabled.
        /// </summary>
        Menu,

        /// <summary>
        /// Game is loading (level transitions, asset loading, etc.).
        /// Head tracking should be disabled.
        /// </summary>
        Loading,

        /// <summary>
        /// Player is in active gameplay with camera control.
        /// Head tracking should be enabled.
        /// </summary>
        Gameplay,

        /// <summary>
        /// Player is in a cutscene or scripted sequence.
        /// Head tracking should typically be disabled.
        /// </summary>
        Cutscene
    }
}
