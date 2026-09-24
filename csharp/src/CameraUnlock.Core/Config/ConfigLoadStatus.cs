namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Load"/> found and did.</summary>
    public enum ConfigLoadStatus
    {
        /// <summary>
        /// The file was read as a canonical file: stamped, or unstamped in a game with no legacy
        /// import for it, which the first save stamps.
        /// </summary>
        Canonical = 0,

        /// <summary>A legacy file was converted to the canonical format this launch.</summary>
        Migrated = 1,

        /// <summary>There was no file, and one holding the defaults was created.</summary>
        Created = 2,

        /// <summary>
        /// The file could not be converted, read or created this launch. It is left as it was, the
        /// session runs on the settings <see cref="ConfigLoadResult{TConfig}.Config"/> holds, nothing
        /// is saved this session, and the next launch tries again.
        /// </summary>
        Deferred = 3,

        /// <summary>
        /// The legacy import refused the file, as the game's last pre-canonical build did. The game
        /// does what that build did on the refusal; the file is left as it was and nothing is saved
        /// this session.
        /// </summary>
        LegacyRefused = 4,

        /// <summary>
        /// A file the canonical reader cannot read (saved as UTF-16, or holding a NUL byte) that is
        /// stamped, or that no legacy import reads. The session runs on the defaults and nothing is
        /// saved until the file is fixed.
        /// </summary>
        Unreadable = 5,
    }
}
