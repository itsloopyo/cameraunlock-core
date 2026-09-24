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
        /// Every line for the game's log, each naming the file, in order: the diagnostics' sentences,
        /// the conversion's lines (values the import dropped, lines the new file does not carry,
        /// where the original is kept) and the error behind a Deferred or Unreadable load. Returned
        /// rather than logged, so a game can load before its logger is up.
        /// </summary>
        public ReadOnlyCollection<string> Log { get; }

        /// <summary>
        /// For Deferred, LegacyRefused and Unreadable, the message for the player, which the owner
        /// also hands its status sink once; empty otherwise.
        /// </summary>
        public string Reason { get; }
    }
}
