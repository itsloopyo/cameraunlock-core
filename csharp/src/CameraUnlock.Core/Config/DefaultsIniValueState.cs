namespace CameraUnlock.Core.Config
{
    /// <summary>What Defaults.ini holds for one canonical concept.</summary>
    internal enum DefaultsIniValueState
    {
        /// <summary>The file has no line for the concept's section and key.</summary>
        Absent = 0,

        /// <summary>The value passed every check, so a game may take it.</summary>
        Accepted = 1,

        /// <summary>The value failed a check, so every game uses its built-in value instead.</summary>
        Refused = 2,
    }
}
