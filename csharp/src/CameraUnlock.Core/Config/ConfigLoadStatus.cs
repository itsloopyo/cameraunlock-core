namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Load"/> found and did.</summary>
    public enum ConfigLoadStatus
    {
        /// <summary>
        /// The file at Path was read as a canonical file, stamped or not; the first save stamps an
        /// unstamped one. The legacy file is not read.
        /// </summary>
        Canonical = 0,

        /// <summary>
        /// The legacy file was imported into a new file at Path this launch. The legacy file is
        /// left as it was.
        /// </summary>
        Migrated = 1,

        /// <summary>
        /// There was no file at Path and no legacy file, and a file holding the defaults was
        /// created at Path.
        /// </summary>
        Created = 2,

        /// <summary>
        /// The file at Path could not be read or created, or the legacy file could not be read or
        /// imported, this launch. The legacy file is left as it was and Path is not created; a file
        /// at Path that could not be read is left as it was too. The session runs on the settings
        /// <see cref="ConfigLoadResult{TConfig}.Config"/> holds, nothing is saved this session, and
        /// the next launch tries again.
        /// </summary>
        Deferred = 3,

        /// <summary>
        /// The legacy import refused the legacy file, as the game's last pre-canonical build did.
        /// The game does what that build did on the refusal. The legacy file is left as it was, Path
        /// is not created, and nothing is saved this session.
        /// </summary>
        LegacyRefused = 4,

        /// <summary>
        /// The file at Path is one the canonical reader cannot read (saved as UTF-16, or holding a
        /// NUL byte). The session runs on the defaults and nothing is saved until the file is fixed.
        /// The legacy file is not read.
        /// </summary>
        Unreadable = 5,
    }
}
