namespace CameraUnlock.Core.Config
{
    /// <summary>Where a row's value came from when a table applied a file.</summary>
    internal enum ConfigValueSource
    {
        /// <summary>The file's own value for the row.</summary>
        File = 0,

        /// <summary>The effective default, which Defaults.ini gave.</summary>
        DefaultsIni = 1,

        /// <summary>The effective default, which is the table's own default.</summary>
        BuiltIn = 2,
    }
}
