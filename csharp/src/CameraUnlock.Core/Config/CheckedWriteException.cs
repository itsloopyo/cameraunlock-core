using System;
using System.IO;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A step of <see cref="CheckedFileWriter.Write"/> failed. <see cref="Exception.InnerException"/>
    /// is the error that step raised, unchanged.
    /// </summary>
    public sealed class CheckedWriteException : IOException
    {
#if NULLABLE_ENABLED
        internal CheckedWriteException(
            string message, CheckedWriteStep step, string targetPath, string? temporaryPath,
            bool temporaryRemoved, bool outcomeUncertain, Exception error, Exception? cleanupError,
            Exception? completionError)
#else
        internal CheckedWriteException(
            string message, CheckedWriteStep step, string targetPath, string temporaryPath,
            bool temporaryRemoved, bool outcomeUncertain, Exception error, Exception cleanupError,
            Exception completionError)
#endif
            : base(message, error)
        {
            Step = step;
            TargetPath = targetPath;
            TemporaryPath = temporaryPath;
            TemporaryRemoved = temporaryRemoved;
            OutcomeUncertain = outcomeUncertain;
            CleanupError = cleanupError;
            CompletionError = completionError;
        }

        /// <summary>
        /// The step that failed. <see cref="CheckedWriteStep.RemoveTemporary"/> when the
        /// target had changed and the temporary built for it then could not be deleted.
        /// </summary>
        public CheckedWriteStep Step { get; }

        /// <summary>The target, as a full path.</summary>
        public string TargetPath { get; }

        /// <summary>
        /// The temporary this call created, or null when it failed before creating one. A file
        /// already sitting at that name is never counted as created, so it is never deleted.
        /// </summary>
#if NULLABLE_ENABLED
        public string? TemporaryPath { get; }
#else
        public string TemporaryPath { get; }
#endif

        /// <summary>
        /// True once the temporary has been deleted. False with a null
        /// <see cref="TemporaryPath"/>, when <see cref="OutcomeUncertain"/> is set, and when the
        /// deletion itself failed (see <see cref="CleanupError"/>).
        /// </summary>
        public bool TemporaryRemoved { get; }

        /// <summary>
        /// Windows reported that it could not finish a replacement it had started, and the writer
        /// could not finish it either: a file was at the target path, or moving the temporary
        /// there failed (see <see cref="CompletionError"/>). The target may be missing or
        /// renamed, and the temporary, which holds the new contents, is left at
        /// <see cref="TemporaryPath"/> because it may be the only copy.
        /// </summary>
        public bool OutcomeUncertain { get; }

        /// <summary>
        /// What went wrong deleting the temporary after the failure, or else closing it, or null.
        /// The message names both when both failed.
        /// </summary>
#if NULLABLE_ENABLED
        public Exception? CleanupError { get; }
#else
        public Exception CleanupError { get; }
#endif

        /// <summary>
        /// What went wrong moving the temporary into the place of a replacement Windows could not
        /// finish, or null when that move was not tried. Set only with
        /// <see cref="OutcomeUncertain"/>.
        /// </summary>
#if NULLABLE_ENABLED
        public Exception? CompletionError { get; }
#else
        public Exception CompletionError { get; }
#endif
    }
}
