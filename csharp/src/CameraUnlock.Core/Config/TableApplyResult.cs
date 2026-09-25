namespace CameraUnlock.Core.Config
{
    /// <summary>What the Apply that takes effective defaults found, and where each row's value came from.</summary>
    internal sealed class TableApplyResult
    {
        internal TableApplyResult(ApplyReport report, ConfigValueSource[] sources)
        {
            Report = report;
            Sources = sources;
        }

        internal ApplyReport Report { get; }

        /// <summary>One per row, in table order.</summary>
        internal ConfigValueSource[] Sources { get; }
    }
}
