namespace CameraUnlock.Core.Config
{
    /// <summary>What a mod runs on, as far as finding Defaults.ini goes.</summary>
    internal enum DefaultsPlatform
    {
        /// <summary>Windows, including a Wine that hides its exports.</summary>
        Windows = 0,

        /// <summary>A Windows program under Wine or Proton: ntdll exports <c>wine_get_version</c>.</summary>
        Wine = 1,

        /// <summary>Linux or macOS with a native runtime: <c>Environment.OSVersion.Platform</c> is not Win32NT.</summary>
        Native = 2,
    }
}
