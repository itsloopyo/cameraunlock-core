using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What became of creating Defaults.ini at one candidate, with the reason for a failed folder
    /// or file in the owner's words for an I/O error. The default value is <see cref="DefaultsCreation.NotTried"/>.
    /// </summary>
    internal readonly struct DefaultsCreationOutcome
    {
        /// <exception cref="ArgumentNullException"><paramref name="why"/> is null.</exception>
        internal DefaultsCreationOutcome(DefaultsCreation kind, string why)
        {
            if (why == null) throw new ArgumentNullException("why");
            Kind = kind;
            Why = why;
        }

        internal DefaultsCreation Kind { get; }

        /// <summary>Why the folder or the file could not be created; set only for those two outcomes.</summary>
        internal string Why { get; }
    }
}
