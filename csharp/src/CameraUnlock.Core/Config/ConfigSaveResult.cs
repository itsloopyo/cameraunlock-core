using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigOwner{TConfig}.Save"/> returns. Nothing is logged by the owner.</summary>
    public sealed class ConfigSaveResult
    {
#if NULLABLE_ENABLED
        internal ConfigSaveResult(ConfigSaveStatus status, string reason, Exception? error, string? temporaryPath,
            IList<string> log)
#else
        internal ConfigSaveResult(ConfigSaveStatus status, string reason, Exception error, string temporaryPath,
            IList<string> log)
#endif
        {
            Status = status;
            Reason = reason;
            Error = error;
            TemporaryPath = temporaryPath;
            Log = new ReadOnlyCollection<string>(log);
        }

        public ConfigSaveStatus Status { get; }

        /// <summary>
        /// For NotSaved and Uncertain, the message for the player, which the owner also hands its
        /// status sink; empty for Saved.
        /// </summary>
        public string Reason { get; }

        /// <summary>
        /// The error that stopped the save, unchanged: a <see cref="CheckedWriteException"/> from the
        /// write, or the error reading the file. Null when there was none, e.g. for a conflict.
        /// </summary>
#if NULLABLE_ENABLED
        public Exception? Error { get; }
#else
        public Exception Error { get; }
#endif

        /// <summary>For Uncertain, the temporary holding the new contents, left in place; null otherwise.</summary>
#if NULLABLE_ENABLED
        public string? TemporaryPath { get; }
#else
        public string TemporaryPath { get; }
#endif

        /// <summary>The lines for the game's log, naming the file and the operation. Empty for Saved.</summary>
        public ReadOnlyCollection<string> Log { get; }
    }
}
