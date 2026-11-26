using System.Collections.Generic;

namespace CameraUnlock.Core.State
{
    /// <summary>
    /// Common menu scene names used across many Unity games.
    /// Use these as a starting point and add game-specific scenes as needed.
    /// </summary>
    public static class CommonMenuScenes
    {
        /// <summary>
        /// Default set of menu scene names commonly used in Unity games.
        /// Case-insensitive matching is recommended.
        /// </summary>
        public static string[] Default { get; } = new[]
        {
            "MainMenu",
            "Menu",
            "Main Menu",
            "StartMenu",
            "Start Menu",
            "TitleScreen",
            "Title",
            "Credits",
            "Loading",
            "LoadingScreen",
            "Splash",
            "Intro",
            "Cutscene",
            "Options",
            "Settings",
            "Pause",
            "PauseMenu",
            "GameOver",
            "Game Over",
            "EndGame",
            "Victory",
            "Defeat"
        };

        /// <summary>
        /// Creates a new HashSet with the default menu scene names.
        /// </summary>
        /// <returns>HashSet with case-insensitive string comparison.</returns>
        public static HashSet<string> CreateDefaultSet()
        {
            return new HashSet<string>(Default, System.StringComparer.OrdinalIgnoreCase);
        }
    }
}
