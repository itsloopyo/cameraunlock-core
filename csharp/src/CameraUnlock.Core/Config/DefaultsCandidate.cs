namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One place Defaults.ini may be: the file, its CameraUnlock folder and that folder's parent,
    /// each also in the form the log shows, with the profile or home folder replaced.
    /// </summary>
    internal sealed class DefaultsCandidate
    {
        internal DefaultsCandidate(
            DefaultsCandidateKind kind, bool mayCreate, string path, string folder, string parent, string shown,
            string shownFolder, string shownParent)
        {
            Kind = kind;
            MayCreate = mayCreate;
            Path = path;
            Folder = folder;
            Parent = parent;
            Shown = shown;
            ShownFolder = shownFolder;
            ShownParent = shownParent;
        }

        internal DefaultsCandidateKind Kind { get; }

        /// <summary>Whether the folder and the file may be created here when no candidate's file exists.</summary>
        internal bool MayCreate { get; }

        internal string Path { get; }

        internal string Folder { get; }

        internal string Parent { get; }

        internal string Shown { get; }

        internal string ShownFolder { get; }

        internal string ShownParent { get; }
    }
}
