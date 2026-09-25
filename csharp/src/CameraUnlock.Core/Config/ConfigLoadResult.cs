using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Load"/> returns. Nothing is logged by the owner.</summary>
    public sealed class ConfigLoadResult<TConfig> where TConfig : class
    {
        internal ConfigLoadResult(ConfigLoadStatus status, TConfig config, IList<CanonicalDiagnostic> diagnostics,
            IList<string> log, string reason)
        {
            Status = status;
            Config = config;
            Diagnostics = new ReadOnlyCollection<CanonicalDiagnostic>(diagnostics);
            Log = new ReadOnlyCollection<string>(log);
            Reason = reason;
        }

        public ConfigLoadStatus Status { get; }

        /// <summary>The settings the session runs on.</summary>
        public TConfig Config { get; }

        /// <summary>
        /// What the canonical reader and the table found in the file read, for a Canonical or a
        /// Migrated load; empty otherwise.
        /// </summary>
        public ReadOnlyCollection<CanonicalDiagnostic> Diagnostics { get; }

        /// <summary>
        /// Every line for the game's log, each naming the file. It can hold the line saying the
        /// settings are read from Path while a legacy file is also present, the line saying an
        /// unstamped file gets its section at the next save, the diagnostics' sentences, the line
        /// saying Path was created and from what, the import's lines (values it dropped, lines of
        /// the legacy file the new file does not carry), the error behind a Deferred,
        /// LegacyRefused or Unreadable load, and the read-only line. It starts with where
        /// Defaults.ini is and what happened to it, and ends with which rows took their value from
        /// Defaults.ini, which the file sets itself, which took the built-in value, and each value
        /// Defaults.ini holds that this game would take and cannot use.
        /// Returned rather than logged, so a game can load before its logger is up.
        /// </summary>
        public ReadOnlyCollection<string> Log { get; }

        /// <summary>
        /// For Deferred, LegacyRefused, Unreadable and ReadOnly, the message for the player, which the
        /// owner also hands its status sink once; empty otherwise. A message about Defaults.ini goes to
        /// the status sink after it and is not here.
        /// </summary>
        public string Reason { get; }
    }
}
