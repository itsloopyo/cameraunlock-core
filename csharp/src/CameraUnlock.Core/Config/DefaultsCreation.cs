namespace CameraUnlock.Core.Config
{
    /// <summary>What became of creating Defaults.ini at one candidate.</summary>
    internal enum DefaultsCreation
    {
        /// <summary>Not tried.</summary>
        NotTried = 0,

        /// <summary>The file was created with the built-in values.</summary>
        Created = 1,

        /// <summary>Another program created the file between the check and the commit, and its file is read.</summary>
        Appeared = 2,

        /// <summary>The folder's parent does not exist, so the folder was not created.</summary>
        ParentMissing = 3,

        /// <summary>The folder could not be created.</summary>
        FolderFailed = 4,

        /// <summary>The folder is there and the file could not be created in it.</summary>
        FileFailed = 5,
    }
}
