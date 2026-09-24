namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What a game's legacy import established. The numbers match the C++
    /// <c>cameraunlock::config::ImportStatus</c>.
    /// </summary>
    public enum ImportStatus
    {
        /// <summary>The frozen reader read the file and the map filled the config.</summary>
        Imported = 0,

        /// <summary>
        /// The frozen reader refuses the file, as the published build did, e.g. a value outside
        /// the range that build accepted. The session runs as that build ran on a refusal, the
        /// file stays as it is, and the next launch tries again.
        /// </summary>
        Refused = 1,

        /// <summary>
        /// The frozen reader opened the file and could not decode it. The session runs on the
        /// defaults that build ran on, and the file stays as it is.
        /// </summary>
        Undecodable = 2,

        /// <summary>
        /// The frozen reader found no file, although the driver holds it open. The map filled the
        /// config from what that build ran on without a file.
        /// </summary>
        Absent = 3,
    }
}
