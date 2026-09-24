namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Save"/> did.</summary>
    public enum ConfigSaveStatus
    {
        /// <summary>The file holds the new values, or already held them and nothing was written.</summary>
        Saved = 0,

        /// <summary>Nothing was written and the file is as it was.</summary>
        NotSaved = 1,

        /// <summary>
        /// Windows started replacing the file and did not finish, so the file may be missing or
        /// renamed. The new contents are in <see cref="ConfigSaveResult.TemporaryPath"/>.
        /// </summary>
        Uncertain = 2,
    }
}
