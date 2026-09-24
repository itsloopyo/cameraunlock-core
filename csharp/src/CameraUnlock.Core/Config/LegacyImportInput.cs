using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>The files a legacy import reads, as the migration driver hands them over.</summary>
    public sealed class LegacyImportInput
    {
        /// <param name="path">The config file the owner reads and writes.</param>
        /// <param name="legacySourcePath">The pre-canonical file where it is another file: a
        /// BepInEx plugin's <c>&lt;GUID&gt;.cfg</c> beside its <c>&lt;GUID&gt;.ini</c>. Null when the
        /// legacy file is the one at <paramref name="path"/>.</param>
        /// <exception cref="ArgumentNullException"><paramref name="path"/> is null.</exception>
        /// <exception cref="ArgumentException">A path is empty.</exception>
#if NULLABLE_ENABLED
        public LegacyImportInput(string path, string? legacySourcePath)
#else
        public LegacyImportInput(string path, string legacySourcePath)
#endif
        {
            if (path == null) throw new ArgumentNullException("path");
            if (path.Length == 0) throw new ArgumentException("the config path is empty", "path");
            if (legacySourcePath != null && legacySourcePath.Length == 0)
            {
                throw new ArgumentException("the legacy source path is empty", "legacySourcePath");
            }
            Path = path;
            LegacySourcePath = legacySourcePath;
        }

        public string Path { get; }

#if NULLABLE_ENABLED
        public string? LegacySourcePath { get; }
#else
        public string LegacySourcePath { get; }
#endif
    }
}
