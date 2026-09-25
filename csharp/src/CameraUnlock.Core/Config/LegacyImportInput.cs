using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>The file a legacy import reads, as the owner hands it over.</summary>
    public sealed class LegacyImportInput
    {
        /// <param name="path">The legacy file: the owner's <see cref="ConfigOwnerOptions{TConfig}.LegacySourcePath"/>.</param>
        /// <exception cref="ArgumentNullException"><paramref name="path"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="path"/> is empty.</exception>
        public LegacyImportInput(string path)
        {
            if (path == null) throw new ArgumentNullException("path");
            if (path.Length == 0) throw new ArgumentException("the legacy file's path is empty", "path");
            Path = path;
        }

        /// <summary>The legacy file. The import reads it and never writes it.</summary>
        public string Path { get; }
    }
}
