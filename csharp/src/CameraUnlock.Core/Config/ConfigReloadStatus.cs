namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Reload"/> found. Reload never writes.</summary>
    public enum ConfigReloadStatus
    {
        /// <summary>The file holds the bytes the owner last wrote, so there is nothing to apply.</summary>
        Unchanged = 0,

        /// <summary>The file was read and <see cref="ConfigReloadResult{TConfig}.Config"/> holds its settings.</summary>
        Applied = 1,

        /// <summary>
        /// The file has no [CameraUnlock] stamp and the game's legacy import reads it: an old file
        /// put back during the session. It was read through the import, is not written, and is
        /// converted at the next launch.
        /// </summary>
        LegacyReadOnly = 2,

        /// <summary>The file could not be read. The game keeps the settings it has.</summary>
        Unreadable = 3,
    }
}
