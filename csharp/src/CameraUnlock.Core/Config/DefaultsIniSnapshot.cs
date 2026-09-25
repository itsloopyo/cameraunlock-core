using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="DefaultsIni.Read"/> read from Defaults.ini's bytes.</summary>
    internal sealed class DefaultsIniSnapshot
    {
        private readonly DefaultsIniValue[] values;

#if NULLABLE_ENABLED
        internal DefaultsIniSnapshot(string? unreadable, string? formatLine, DefaultsIniValue[] values, bool pairRefused)
#else
        internal DefaultsIniSnapshot(string unreadable, string formatLine, DefaultsIniValue[] values, bool pairRefused)
#endif
        {
            Unreadable = unreadable;
            FormatLine = formatLine;
            this.values = values;
            PairRefused = pairRefused;
        }

        /// <summary>
        /// Why nothing was read, in the owner's words for an unreadable file (<c>it is saved as
        /// UTF-16; save it as ANSI or UTF-8</c>, <c>line 3 holds a NUL byte</c>), or null when the
        /// file was read. An unreadable file holds every concept absent.
        /// </summary>
#if NULLABLE_ENABLED
        internal string? Unreadable { get; }
#else
        internal string Unreadable { get; }
#endif

        /// <summary>The line naming a ConfigFormat newer than this build's, or null.</summary>
#if NULLABLE_ENABLED
        internal string? FormatLine { get; }
#else
        internal string FormatLine { get; }
#endif

        /// <summary>
        /// True when RotationEnabled and PositionEnabled were refused together, so both are
        /// refused, or absent, and <see cref="DefaultsIni.PairLine"/> gives their one line.
        /// </summary>
        internal bool PairRefused { get; }

        /// <exception cref="ArgumentNullException"><paramref name="concept"/> is null.</exception>
        internal DefaultsIniValue Value(ConceptDescriptor concept)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            return values[Array.IndexOf(ConfigConcepts.All, concept)];
        }
    }
}
