namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What <see cref="DefaultsLocation.Choose"/> decided: the candidate to read, or the one to
    /// try creating next, and once nothing is left to try, the log line and the in-game message.
    /// </summary>
    internal sealed class DefaultsChoice
    {
        internal DefaultsChoice(int read, int create, string line, string message)
        {
            Read = read;
            Create = create;
            Line = line;
            Message = message;
        }

        /// <summary>The index of the candidate whose file is read, or -1.</summary>
        internal int Read { get; }

        /// <summary>The index of the candidate to create next, or -1. The caller creates it and chooses again.</summary>
        internal int Create { get; }

        /// <summary>
        /// The one log line about where Defaults.ini is, for a file that then reads; empty while
        /// <see cref="Create"/> names a candidate.
        /// </summary>
        internal string Line { get; }

        /// <summary>The in-game message when two files exist and one is not read; empty otherwise.</summary>
        internal string Message { get; }
    }
}
