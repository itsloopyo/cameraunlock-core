using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Reload"/> returns. Nothing is logged by the owner.</summary>
    public sealed class ConfigReloadResult<TConfig> where TConfig : class
    {
#if NULLABLE_ENABLED
        internal ConfigReloadResult(ConfigReloadStatus status, TConfig? config, IList<CanonicalDiagnostic> diagnostics,
            IList<string> log, string reason)
#else
        internal ConfigReloadResult(ConfigReloadStatus status, TConfig config, IList<CanonicalDiagnostic> diagnostics,
            IList<string> log, string reason)
#endif
        {
            Status = status;
            Config = config;
            Diagnostics = new ReadOnlyCollection<CanonicalDiagnostic>(diagnostics);
            Log = new ReadOnlyCollection<string>(log);
            Reason = reason;
        }

        public ConfigReloadStatus Status { get; }

        /// <summary>
        /// For Applied, the settings read; null for Unchanged and Unreadable, where the game keeps
        /// the settings it has.
        /// </summary>
#if NULLABLE_ENABLED
        public TConfig? Config { get; }
#else
        public TConfig Config { get; }
#endif

        /// <summary>What the canonical reader and the table found, for Applied; empty otherwise.</summary>
        public ReadOnlyCollection<CanonicalDiagnostic> Diagnostics { get; }

        /// <summary>Every line for the game's log, each naming the file, in order.</summary>
        public ReadOnlyCollection<string> Log { get; }

        /// <summary>
        /// For Unreadable, the message for the player, which the owner also hands its status sink;
        /// empty otherwise.
        /// </summary>
        public string Reason { get; }
    }
}
