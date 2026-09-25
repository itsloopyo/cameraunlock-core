using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Where a <see cref="ConfigOwner{TConfig}"/> finds Defaults.ini, the file every concept row
    /// not marked PerGame takes its default from. A mod passes <see cref="PerUser"/>; a test passes
    /// <see cref="At"/> with a scratch path, so no test reads or creates the player's own file. The
    /// C++ twin is cameraunlock::config::DefaultsFile.
    /// </summary>
    public sealed class DefaultsFile
    {
#if NULLABLE_ENABLED
        private readonly string? _path;
        private readonly DefaultsProbe? _probe;

        private DefaultsFile(string? path, DefaultsProbe? probe)
#else
        private readonly string _path;
        private readonly DefaultsProbe _probe;

        private DefaultsFile(string path, DefaultsProbe probe)
#endif
        {
            _path = path;
            _probe = probe;
        }

        /// <summary>
        /// The player's own Defaults.ini: <c>%AppData%\CameraUnlock\Defaults.ini</c> on Windows,
        /// found through the roaming AppData known folder; under Wine or Proton the host's config
        /// folder where Wine maps it, else the prefix's AppData; natively on Linux and macOS the
        /// first of <c>$XDG_CONFIG_HOME/CameraUnlock</c> (or <c>~/.config/CameraUnlock</c>) and
        /// <c>~/Library/Application Support/CameraUnlock</c> that holds one. Load creates it where
        /// none exists, on Windows and under Wine, except in a packaged app.
        /// </summary>
        public static DefaultsFile PerUser()
        {
            return new DefaultsFile(null, null);
        }

        /// <summary>
        /// Defaults.ini at <paramref name="path"/>, used as it is, for a test. Load creates it when it
        /// is absent as it would the player's own: its folder one level only, so the folder's parent
        /// must exist, then the file with the built-in values, never over a file that appears
        /// meanwhile. The log shows the path as it is.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="path"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="path"/> is empty or not absolute.</exception>
        public static DefaultsFile At(string path)
        {
            if (path == null) throw new ArgumentNullException("path");
            if (path.Length == 0 || !System.IO.Path.IsPathRooted(path))
            {
                throw new ArgumentException("DefaultsFile.At takes an absolute path, and '" + path + "' is not one", "path");
            }
            return new DefaultsFile(System.IO.Path.GetFullPath(path), null);
        }

        /// <summary>The resolver's input given, not probed, so a test can stand for any machine.</summary>
        internal static DefaultsFile Probed(DefaultsProbe probe)
        {
            if (probe == null) throw new ArgumentNullException("probe");
            return new DefaultsFile(null, probe);
        }

        /// <summary>
        /// The candidates. An <see cref="At"/> path is one candidate, created when absent only where
        /// <paramref name="writes"/> is true, which it is on Windows and under Wine.
        /// </summary>
        internal DefaultsResolution Resolve(bool writes)
        {
            if (_probe != null) return DefaultsLocation.Resolve(_probe);
            if (_path == null) return DefaultsLocation.Resolve(DefaultsLocation.Probe());

            string folder = System.IO.Path.GetDirectoryName(_path) ?? _path;
            string parent = System.IO.Path.GetDirectoryName(folder) ?? folder;
            DefaultsPlatform platform = writes ? DefaultsPlatform.Windows : DefaultsPlatform.Native;
            DefaultsCandidateKind kind = writes ? DefaultsCandidateKind.Windows : DefaultsCandidateKind.Native;
            var candidate = new DefaultsCandidate(kind, writes, _path, folder, parent, _path, folder, parent);
            return new DefaultsResolution(platform, new[] { candidate }, string.Empty, string.Empty, string.Empty,
                string.Empty, DefaultsLocation.NoPackage);
        }
    }
}
