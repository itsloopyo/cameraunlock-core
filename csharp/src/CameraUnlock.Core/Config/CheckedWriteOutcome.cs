namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What <see cref="CheckedFileWriter.Write"/> did. Anything other than
    /// <see cref="Committed"/> means the file on disk was not the one the caller built the
    /// candidate from, so nothing was written and the candidate's temporary was removed.
    /// The numbers match the C++ <c>cameraunlock::CheckedWriteStatus</c>.
    /// </summary>
    public enum CheckedWriteOutcome
    {
        Committed = 0,

        /// <summary>The target's bytes are not the expected ones.</summary>
        TargetChanged = 1,

        /// <summary>The target was expected to be absent and a file is there.</summary>
        TargetAppeared = 2,

        /// <summary>The target was expected to exist and is gone.</summary>
        TargetMissing = 3,

        /// <summary>
        /// The target holds the expected bytes, but it is no longer the same file: something
        /// replaced it between the writer's first read and its final check.
        /// </summary>
        TargetReplaced = 4,
    }
}
