using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="DefaultsLocation.Resolve"/> made of a probe: the candidates in order, or why there is none.</summary>
    internal sealed class DefaultsResolution
    {
        internal DefaultsResolution(
            DefaultsPlatform platform, DefaultsCandidate[] candidates, string noLocation, string hostUnusable,
            string unixFolder, string wine, int packageResult)
        {
            Platform = platform;
            Candidates = new ReadOnlyCollection<DefaultsCandidate>(candidates);
            NoLocation = noLocation;
            HostUnusable = hostUnusable;
            UnixFolder = unixFolder;
            Wine = wine;
            PackageResult = packageResult;
        }

        internal DefaultsPlatform Platform { get; }

        internal ReadOnlyCollection<DefaultsCandidate> Candidates { get; }

        /// <summary>Why there is no candidate; empty when there is one.</summary>
        internal string NoLocation { get; }

        /// <summary>Under Wine, why there is no host candidate; empty otherwise.</summary>
        internal string HostUnusable { get; }

        /// <summary>Under Wine, the host folder's Unix text the probe converts; empty when none is converted.</summary>
        internal string UnixFolder { get; }

        /// <summary>Under Wine, <c>Wine &lt;version&gt; on &lt;system&gt;</c>, without the system when Wine gave none.</summary>
        internal string Wine { get; }

        /// <summary>
        /// On Windows, what GetCurrentPackageFullName returned, and <see cref="DefaultsLocation.NoPackage"/>
        /// when kernel32 has no such function, as under Wine and natively, where it is not asked.
        /// </summary>
        internal int PackageResult { get; }
    }
}
