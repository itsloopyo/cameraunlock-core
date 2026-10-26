using System.Collections.Generic;

namespace CameraUnlock.Core.Config.Profiles
{
    /// <summary>
    /// Interface for game-specific profile settings adapters.
    /// Implementations map between game config systems (e.g., BepInEx ConfigEntry) and generic profiles.
    /// </summary>
    public interface IProfileSettings
    {
        /// <summary>
        /// Exports current game configuration to a dictionary of settings.
        /// Keys should be stable setting names, values should be serializable types
        /// (string, int, float, bool, enum names as strings).
        /// </summary>
        /// <returns>Dictionary of setting name to value.</returns>
        Dictionary<string, object> ExportSettings();

        /// <summary>
        /// Imports settings from a dictionary into the game configuration.
        /// Implementations should handle missing keys gracefully (use defaults).
        /// </summary>
        /// <param name="settings">Dictionary of setting name to value.</param>
        void ImportSettings(Dictionary<string, object> settings);

        /// <summary>
        /// Saves the current configuration to persistent storage.
        /// Called after ImportSettings to persist changes.
        /// </summary>
        void SaveConfig();

        /// <summary>
        /// Gets the name of the game this adapter is for.
        /// Used for profile display and filtering.
        /// </summary>
        string GameName { get; }
    }
}
