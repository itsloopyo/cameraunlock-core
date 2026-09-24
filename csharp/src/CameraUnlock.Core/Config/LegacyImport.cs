using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Runs a game's frozen legacy reader on the pre-canonical file, through the API the
    /// published build read it with, into a copy of that build's config type from its own
    /// defaults, then maps that into <paramref name="config"/>. Writes nothing.
    /// </summary>
    public delegate ImportResult LegacyImportRun<TConfig>(LegacyImportInput input, TConfig config) where TConfig : class;

    /// <summary>
    /// A game's legacy import: the reader its last pre-canonical build ran, frozen in the game's
    /// repo, which the migration driver runs once on a file that has no [CameraUnlock] stamp. The
    /// C++ twin is <c>cameraunlock::config::LegacyImport</c>.
    /// </summary>
    public sealed class LegacyImport<TConfig> where TConfig : class
    {
        /// <param name="run">The import.</param>
        /// <param name="keys">Every key the frozen reader reads, reads outside the reader included.
        /// The driver logs each other key line of the legacy file as not carried, and the
        /// differential corpus mutates each one.</param>
        /// <exception cref="ArgumentNullException">An argument or a key is null.</exception>
        public LegacyImport(LegacyImportRun<TConfig> run, IEnumerable<LegacyKey> keys)
        {
            if (run == null) throw new ArgumentNullException("run");
            if (keys == null) throw new ArgumentNullException("keys");
            var copy = new List<LegacyKey>(keys);
            foreach (LegacyKey key in copy)
            {
                if (key == null) throw new ArgumentNullException("keys", "a legacy key is null");
            }
            Run = run;
            Keys = new ReadOnlyCollection<LegacyKey>(copy);
        }

        public LegacyImportRun<TConfig> Run { get; }

        public ReadOnlyCollection<LegacyKey> Keys { get; }
    }
}
