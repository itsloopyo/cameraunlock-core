namespace CameraUnlock.Core.Config
{
    /// <summary>Where a Defaults.ini candidate comes from.</summary>
    internal enum DefaultsCandidateKind
    {
        /// <summary>The roaming AppData known folder on Windows.</summary>
        Windows = 0,

        /// <summary>The host's config folder, seen from inside Wine through a drive letter.</summary>
        WineHost = 1,

        /// <summary>The roaming AppData known folder inside the Wine prefix.</summary>
        WinePrefix = 2,

        /// <summary>A config folder on native Linux or macOS.</summary>
        Native = 3,
    }
}
