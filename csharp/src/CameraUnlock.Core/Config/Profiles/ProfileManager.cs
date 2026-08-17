using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Config.Profiles
{
    /// <summary>
    /// Manages multiple configuration profiles for head tracking.
    /// Handles loading, saving, creating, and deleting profiles.
    /// </summary>
    public class ProfileManager
    {
        private readonly string _profilesDirectory;
        private readonly Dictionary<string, ConfigProfile> _profiles;
#if NULLABLE_ENABLED
        private ConfigProfile? _activeProfile;
        private string? _activeProfileName;
#else
        private ConfigProfile _activeProfile;
        private string _activeProfileName;
#endif

        /// <summary>
        /// File extension for profile files.
        /// </summary>
        public const string ProfileExtension = ".profile";

        /// <summary>
        /// Gets the directory where profiles are stored.
        /// </summary>
        public string ProfilesDirectory => _profilesDirectory;

        /// <summary>
        /// Gets the currently active profile.
        /// </summary>
#if NULLABLE_ENABLED
        public ConfigProfile? ActiveProfile => _activeProfile;
#else
        public ConfigProfile ActiveProfile => _activeProfile;
#endif

        /// <summary>
        /// Gets the name of the currently active profile.
        /// </summary>
#if NULLABLE_ENABLED
        public string? ActiveProfileName => _activeProfileName;
#else
        public string ActiveProfileName => _activeProfileName;
#endif

        /// <summary>
        /// Gets all loaded profiles.
        /// Note: Returns IDictionary for compatibility with .NET 3.5/4.0. Callers should not modify.
        /// </summary>
        public IDictionary<string, ConfigProfile> Profiles => _profiles;

        /// <summary>
        /// Event raised when the active profile changes.
        /// </summary>
#if NULLABLE_ENABLED
        public event Action<string>? ProfileChanged;
#else
        public event Action<string> ProfileChanged;
#endif

        /// <summary>
        /// Creates a new ProfileManager.
        /// </summary>
        /// <param name="profilesDirectory">Directory to store profiles. Created if doesn't exist.</param>
        /// <exception cref="ArgumentException">Thrown if profilesDirectory is null or empty.</exception>
        public ProfileManager(string profilesDirectory)
        {
            if (string.IsNullOrEmpty(profilesDirectory))
            {
                throw new ArgumentException("Profiles directory cannot be null or empty", nameof(profilesDirectory));
            }

            _profilesDirectory = profilesDirectory;
            _profiles = new Dictionary<string, ConfigProfile>(StringComparer.OrdinalIgnoreCase);

            // Create directory if needed
            if (!Directory.Exists(_profilesDirectory))
            {
                Directory.CreateDirectory(_profilesDirectory);
            }

            // Load existing profiles
            LoadAllProfiles();

            // Create defaults if empty
            if (_profiles.Count == 0)
            {
                CreateDefaultProfiles();
            }
        }

        /// <summary>
        /// Loads all profiles from the profiles directory.
        /// </summary>
        /// <exception cref="InvalidOperationException">Thrown if any profile file is corrupted or cannot be loaded.</exception>
        public void LoadAllProfiles()
        {
            _profiles.Clear();

            if (!Directory.Exists(_profilesDirectory))
            {
                return;
            }

            foreach (var file in Directory.GetFiles(_profilesDirectory, "*" + ProfileExtension))
            {
                string profileName = Path.GetFileNameWithoutExtension(file);
                var profile = ProfileSerializer.ReadFromFile(file);

                // The filename is the identifier the dictionary is keyed on, and SaveProfile
                // writes back to GetProfilePath(profile.Name). If a user renames the file on
                // disk the two disagree, and the next save forks the profile into a second
                // list entry backed by the original filename.
                profile.Name = profileName;
                _profiles[profileName] = profile;
            }
        }

        /// <summary>
        /// Creates the default built-in profiles.
        /// </summary>
        public void CreateDefaultProfiles()
        {
            // Default profile
            var defaultProfile = new ConfigProfile("Default", "Default configuration for most games", "General")
            {
                IsDefault = true
            };
            defaultProfile.AxisMapping.ResetToDefault();
            _profiles["Default"] = defaultProfile;
            SaveProfile(defaultProfile);

            // Competitive FPS profile
            var fpsProfile = new ConfigProfile("FPS_Competitive", "Optimized for competitive FPS games", "FPS");
            fpsProfile.AxisMapping.LoadPreset(MappingPreset.Competitive);
            _profiles["FPS_Competitive"] = fpsProfile;
            SaveProfile(fpsProfile);

            // Simulation profile
            var simProfile = new ConfigProfile("Simulation", "Realistic head movement for simulation games", "Simulation");
            simProfile.AxisMapping.LoadPreset(MappingPreset.Simulation);
            _profiles["Simulation"] = simProfile;
            SaveProfile(simProfile);
        }

        /// <summary>
        /// Loads and activates a profile by name.
        /// </summary>
        /// <param name="profileName">Name of the profile to load.</param>
        /// <exception cref="KeyNotFoundException">Thrown if profile doesn't exist.</exception>
        public void LoadProfile(string profileName)
        {
            if (!_profiles.TryGetValue(profileName, out var profile))
            {
                throw new KeyNotFoundException("Profile not found: " + profileName);
            }

            _activeProfile = profile;
            _activeProfileName = profileName;

            ProfileChanged?.Invoke(profileName);
        }

        /// <summary>
        /// Saves a profile to disk.
        /// </summary>
        /// <param name="profile">The profile to save.</param>
        /// <exception cref="ArgumentNullException">Thrown if profile is null.</exception>
        /// <exception cref="InvalidOperationException">Thrown if profile is read-only.</exception>
        public void SaveProfile(ConfigProfile profile)
        {
            if (profile == null)
            {
                throw new ArgumentNullException(nameof(profile));
            }

            if (profile.IsReadOnly)
            {
                throw new InvalidOperationException("Cannot save read-only profile: " + profile.Name);
            }

            ValidateProfileName(profile.Name);

            profile.ModifiedDate = DateTime.Now;
            string filePath = GetProfilePath(profile.Name);
            ProfileSerializer.WriteToFile(profile, filePath);

            // Update in-memory cache
            _profiles[profile.Name] = profile;
        }

        /// <summary>
        /// Creates a new profile.
        /// </summary>
        /// <param name="name">Profile name.</param>
        /// <param name="description">Profile description.</param>
        /// <param name="gameName">Game name (default: "General").</param>
        /// <returns>The created profile.</returns>
        /// <exception cref="InvalidOperationException">Thrown if profile already exists.</exception>
        public ConfigProfile CreateProfile(string name, string description, string gameName = "General")
        {
            ValidateProfileName(name);

            if (_profiles.ContainsKey(name))
            {
                throw new InvalidOperationException("Profile already exists: " + name);
            }

            var profile = new ConfigProfile(name, description, gameName);
            profile.AxisMapping.ResetToDefault();

            _profiles[name] = profile;
            SaveProfile(profile);

            return profile;
        }

        /// <summary>
        /// Deletes a profile.
        /// </summary>
        /// <param name="profileName">Name of the profile to delete.</param>
        /// <exception cref="KeyNotFoundException">Thrown if profile doesn't exist.</exception>
        /// <exception cref="InvalidOperationException">Thrown if profile is protected.</exception>
        public void DeleteProfile(string profileName)
        {
            if (!_profiles.TryGetValue(profileName, out var profile))
            {
                throw new KeyNotFoundException("Profile not found: " + profileName);
            }

            if (profile.IsReadOnly || profile.IsDefault)
            {
                throw new InvalidOperationException("Cannot delete protected profile: " + profileName);
            }

            _profiles.Remove(profileName);

            string filePath = GetProfilePath(profileName);
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
            }

            // Switch to default if we deleted the active profile
            if (_activeProfileName != null &&
                _activeProfileName.Equals(profileName, StringComparison.OrdinalIgnoreCase))
            {
                if (_profiles.ContainsKey("Default"))
                {
                    LoadProfile("Default");
                }
                else if (_profiles.Count > 0)
                {
                    LoadProfile(_profiles.Keys.First());
                }
                else
                {
                    _activeProfile = null;
                    _activeProfileName = null;
                }
            }
        }

        /// <summary>
        /// Duplicates an existing profile with a new name.
        /// </summary>
        /// <param name="sourceName">Name of the profile to duplicate.</param>
        /// <param name="newName">Name for the new profile.</param>
        /// <returns>The duplicated profile.</returns>
        /// <exception cref="KeyNotFoundException">Thrown if source profile doesn't exist.</exception>
        /// <exception cref="InvalidOperationException">Thrown if target profile already exists.</exception>
        public ConfigProfile DuplicateProfile(string sourceName, string newName)
        {
            ValidateProfileName(newName);

            if (!_profiles.TryGetValue(sourceName, out var source))
            {
                throw new KeyNotFoundException("Source profile not found: " + sourceName);
            }

            if (_profiles.ContainsKey(newName))
            {
                throw new InvalidOperationException("Profile already exists: " + newName);
            }

            var duplicate = source.Clone(newName);
            duplicate.IsDefault = false;
            duplicate.IsReadOnly = false;

            _profiles[newName] = duplicate;
            SaveProfile(duplicate);

            return duplicate;
        }

        /// <summary>
        /// Gets all profile names, sorted alphabetically.
        /// </summary>
        /// <returns>List of profile names.</returns>
        public List<string> GetProfileNames()
        {
            var names = _profiles.Keys.ToList();
            names.Sort(StringComparer.OrdinalIgnoreCase);
            return names;
        }

        /// <summary>
        /// Gets a profile by name.
        /// </summary>
        /// <param name="name">Profile name.</param>
        /// <returns>The profile, or null if not found.</returns>
#if NULLABLE_ENABLED
        public ConfigProfile? GetProfile(string name)
#else
        public ConfigProfile GetProfile(string name)
#endif
        {
            _profiles.TryGetValue(name, out var profile);
            return profile;
        }

        /// <summary>
        /// Checks if a profile exists.
        /// </summary>
        /// <param name="name">Profile name.</param>
        /// <returns>True if the profile exists.</returns>
        public bool ProfileExists(string name)
        {
            return _profiles.ContainsKey(name);
        }

        /// <summary>
        /// Saves the current settings to the active profile.
        /// </summary>
        /// <param name="adapter">Game settings adapter to export from.</param>
        /// <exception cref="InvalidOperationException">Thrown if no active profile or profile is read-only.</exception>
        public void SaveCurrentToActiveProfile(IProfileSettings adapter)
        {
            if (_activeProfile == null)
            {
                throw new InvalidOperationException("No active profile");
            }

            if (_activeProfile.IsReadOnly)
            {
                throw new InvalidOperationException("Cannot save to read-only profile");
            }

            _activeProfile.ExportFromAdapter(adapter);
            SaveProfile(_activeProfile);
        }

        /// <summary>
        /// Applies the active profile to the game configuration.
        /// </summary>
        /// <param name="adapter">Game settings adapter to import to.</param>
        /// <exception cref="InvalidOperationException">Thrown if no active profile.</exception>
        public void ApplyActiveProfileToConfig(IProfileSettings adapter)
        {
            if (_activeProfile == null)
            {
                throw new InvalidOperationException("No active profile");
            }

            _activeProfile.ImportToAdapter(adapter);
        }

        /// <summary>
        /// Gets the full file path for a profile.
        /// </summary>
        private string GetProfilePath(string profileName)
        {
            return Path.Combine(_profilesDirectory, profileName + ProfileExtension);
        }

        // The profile name IS the filename, so it is a path component built from caller
        // input. Without this, "../../BepInEx/config/BepInEx" writes outside the profiles
        // directory and then vanishes from the listing, and a name holding ':' or '*'
        // throws out of File.WriteAllText only after the in-memory dictionary has already
        // been mutated, leaving a profile that ProfileExists reports but no file backs.
        private static void ValidateProfileName(string profileName)
        {
            if (string.IsNullOrEmpty(profileName) || profileName.Trim().Length == 0)
            {
                throw new ArgumentException("Profile name must not be empty", nameof(profileName));
            }

            char[] invalid = Path.GetInvalidFileNameChars();
            for (int i = 0; i < profileName.Length; i++)
            {
                for (int j = 0; j < invalid.Length; j++)
                {
                    if (profileName[i] == invalid[j])
                    {
                        throw new ArgumentException(
                            "Profile name contains an invalid filename character: " + profileName,
                            nameof(profileName));
                    }
                }
            }

            if (profileName == "." || profileName == "..")
            {
                throw new ArgumentException("Profile name must not be a directory reference", nameof(profileName));
            }
        }
    }
}
