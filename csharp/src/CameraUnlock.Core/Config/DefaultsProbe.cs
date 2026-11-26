namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What the probes found, and all <see cref="DefaultsLocation.Resolve"/> reads. Every text is
    /// empty when the probe found nothing, so an unset variable and an empty one are the same.
    /// </summary>
    internal sealed class DefaultsProbe
    {
        internal DefaultsProbe()
        {
            KnownFolder = string.Empty;
            WineVersion = string.Empty;
            HostSystem = string.Empty;
            WineHomeDir = string.Empty;
            WineHostXdgConfigHome = string.Empty;
            XdgConfigHome = string.Empty;
            Home = string.Empty;
            DosFileName = string.Empty;
        }

        internal DefaultsPlatform Platform { get; set; }

        /// <summary>The roaming AppData known folder, on Windows and under Wine.</summary>
        internal string KnownFolder { get; set; }

        /// <summary>
        /// On Windows, what <c>GetCurrentPackageFullName</c> returned for a zero length, or null
        /// when kernel32 has no such function.
        /// </summary>
        internal int? PackageResult { get; set; }

        /// <summary>Under Wine, what <c>wine_get_version</c> returned.</summary>
        internal string WineVersion { get; set; }

        /// <summary>Under Wine, the system name <c>wine_get_host_version</c> gave, e.g. Linux or Darwin.</summary>
        internal string HostSystem { get; set; }

        /// <summary>Under Wine, the WINEHOMEDIR variable: the Unix home as an NT path.</summary>
        internal string WineHomeDir { get; set; }

        /// <summary>Under Wine, the WINE_HOST_XDG_CONFIG_HOME variable.</summary>
        internal string WineHostXdgConfigHome { get; set; }

        /// <summary>Under Wine and natively, the XDG_CONFIG_HOME variable.</summary>
        internal string XdgConfigHome { get; set; }

        /// <summary>Natively, the HOME variable.</summary>
        internal string Home { get; set; }

        /// <summary>
        /// Under Wine, true when the host folder's Unix text could not be turned into bytes in
        /// Wine's Unix code page.
        /// </summary>
        internal bool CodePageFailed { get; set; }

        /// <summary>Under Wine, what <c>wine_get_dos_file_name</c> returned for the host folder's Unix text.</summary>
        internal string DosFileName { get; set; }
    }
}
