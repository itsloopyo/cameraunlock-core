namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The steps of <see cref="CheckedFileWriter.Write"/>, in order. The numbers match the
    /// C++ <c>cameraunlock::CheckedWriteStep</c>, which also has <c>None = 0</c>.
    /// </summary>
    public enum CheckedWriteStep
    {
        /// <summary>Reading the target's bytes and identity before anything is written.</summary>
        ReadTarget = 1,

        /// <summary>Creating the temporary beside the target, failing if the name exists.</summary>
        CreateTemporary = 2,

        WriteTemporary = 3,

        /// <summary>Handing the temporary's bytes to the disk.</summary>
        FlushTemporary = 4,

        CloseTemporary = 5,

        /// <summary>Reading the target's bytes and identity again, just before the commit.</summary>
        RecheckTarget = 6,

        /// <summary>Replacing the target with the temporary, or renaming it into place.</summary>
        Commit = 7,

        /// <summary>Deleting the temporary after a failure or a conflict.</summary>
        RemoveTemporary = 8,
    }
}
