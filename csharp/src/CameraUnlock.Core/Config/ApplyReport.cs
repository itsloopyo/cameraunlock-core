using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What <see cref="ConfigTable{TConfig}.Apply"/> found beyond the reader's own diagnostics
    /// (<see cref="CanonicalIni.Diagnostics"/>).
    /// </summary>
    public sealed class ApplyReport
    {
        internal ApplyReport(CanonicalDiagnostic[] diagnostics)
        {
            Diagnostics = new ReadOnlyCollection<CanonicalDiagnostic>(diagnostics);
        }

        /// <summary>Ordered by first line, then kind.</summary>
        public ReadOnlyCollection<CanonicalDiagnostic> Diagnostics { get; }
    }
}
