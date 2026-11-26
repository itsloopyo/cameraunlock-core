using System;
using System.Collections.Generic;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Config.Profiles
{
    /// <summary>
    /// A configuration profile containing all head tracking settings.
    /// Profiles can be saved, loaded, and shared between games.
    /// </summary>
    public class ConfigProfile
    {
        /// <summary>
        /// Unique name of the profile. Used as filename and identifier.
        /// </summary>
        public string Name { get; set; }

        /// <summary>
        /// Human-readable description of the profile.
        /// </summary>
        public string Description { get; set; }

        /// <summary>
        /// The game this profile was created for. "General" for universal profiles.
        /// </summary>
        public string GameName { get; set; }

        /// <summary>
        /// When the profile was first created.
        /// </summary>
        public DateTime CreatedDate { get; set; }

        /// <summary>
        /// When the profile was last modified.
        /// </summary>
        public DateTime ModifiedDate { get; set; }

        /// <summary>
        /// Whether this is a default/built-in profile.
        /// Default profiles cannot be deleted.
        /// </summary>
        public bool IsDefault { get; set; }

        /// <summary>
        /// Whether this profile is read-only.
        /// Read-only profiles cannot be modified or deleted.
        /// </summary>
        public bool IsReadOnly { get; set; }

        /// <summary>
        /// Game-specific settings as a dictionary.
        /// Keys are setting names, values are the setting values.
        /// </summary>
        public Dictionary<string, object> Settings { get; set; }

        /// <summary>
        /// Axis mapping configuration for this profile.
        /// </summary>
        public MappingConfig AxisMapping { get; set; }

        /// <summary>
        /// Creates a new profile with default values.
        /// </summary>
        public ConfigProfile()
        {
            Name = "New Profile";
            Description = "";
            GameName = "General";
            CreatedDate = DateTime.Now;
            ModifiedDate = DateTime.Now;
            Settings = new Dictionary<string, object>();
            AxisMapping = new MappingConfig();
        }

        /// <summary>
        /// Creates a new profile with the specified name.
        /// </summary>
        /// <param name="name">Profile name.</param>
        public ConfigProfile(string name) : this()
        {
            Name = name;
        }

        /// <summary>
        /// Creates a new profile with name and description.
        /// </summary>
        /// <param name="name">Profile name.</param>
        /// <param name="description">Profile description.</param>
        /// <param name="gameName">Game name (default: "General").</param>
        public ConfigProfile(string name, string description, string gameName = "General") : this()
        {
            Name = name;
            Description = description;
            GameName = gameName;
        }

        /// <summary>
        /// Exports settings from a game config adapter into this profile.
        /// </summary>
        /// <param name="adapter">The game-specific settings adapter.</param>
        /// <exception cref="ArgumentNullException">Thrown if adapter is null.</exception>
        public void ExportFromAdapter(IProfileSettings adapter)
        {
            if (adapter == null)
            {
                throw new ArgumentNullException(nameof(adapter));
            }

            Settings = adapter.ExportSettings();
            GameName = adapter.GameName;
            ModifiedDate = DateTime.Now;
        }

        /// <summary>
        /// Imports settings from this profile into a game config adapter.
        /// </summary>
        /// <param name="adapter">The game-specific settings adapter.</param>
        /// <exception cref="ArgumentNullException">Thrown if adapter is null.</exception>
        public void ImportToAdapter(IProfileSettings adapter)
        {
            if (adapter == null)
            {
                throw new ArgumentNullException(nameof(adapter));
            }

            adapter.ImportSettings(Settings);
            adapter.SaveConfig();
        }

        /// <summary>
        /// Creates a clone of this profile with a new name.
        /// </summary>
        /// <param name="newName">Name for the cloned profile.</param>
        /// <returns>A new profile with copied settings.</returns>
        /// <exception cref="ArgumentException">Thrown if newName is null or empty.</exception>
        public ConfigProfile Clone(string newName)
        {
            if (string.IsNullOrEmpty(newName))
            {
                throw new ArgumentException("New name cannot be null or empty", nameof(newName));
            }

            var clone = new ConfigProfile
            {
                Name = newName,
                Description = Description + " (Copy)",
                GameName = GameName,
                CreatedDate = DateTime.Now,
                ModifiedDate = DateTime.Now,
                IsDefault = false,
                IsReadOnly = false,
                Settings = new Dictionary<string, object>(Settings),
                AxisMapping = AxisMapping?.Clone() ?? new MappingConfig()
            };

            return clone;
        }

        /// <summary>
        /// Gets a setting value by key.
        /// </summary>
        /// <typeparam name="T">Expected type of the setting.</typeparam>
        /// <param name="key">Setting key.</param>
        /// <param name="defaultValue">Default value if setting doesn't exist.</param>
        /// <returns>The setting value or default if key not found.</returns>
        /// <exception cref="InvalidCastException">Thrown if the setting exists but cannot be converted to type T.</exception>
#if NULLABLE_ENABLED
        public T? GetSetting<T>(string key, T? defaultValue = default)
#else
        public T GetSetting<T>(string key, T defaultValue = default(T))
#endif
        {
            if (Settings == null || !Settings.TryGetValue(key, out object value))
            {
                return defaultValue;
            }

            if (value is T typedValue)
            {
                return typedValue;
            }

            // Convert - let exceptions propagate if conversion fails
            // If a setting exists but has wrong type, that's a data integrity issue
            return (T)Convert.ChangeType(value, typeof(T));
        }

        /// <summary>
        /// Sets a setting value.
        /// </summary>
        /// <param name="key">Setting key.</param>
        /// <param name="value">Setting value.</param>
        public void SetSetting(string key, object value)
        {
            if (Settings == null)
            {
                Settings = new Dictionary<string, object>();
            }

            Settings[key] = value;
            ModifiedDate = DateTime.Now;
        }
    }
}
