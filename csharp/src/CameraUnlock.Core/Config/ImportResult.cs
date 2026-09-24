using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What a legacy import returns. The factories hold each status to its fields. The C++ twin is
    /// <c>cameraunlock::config::ImportResult</c>.
    /// </summary>
    public sealed class ImportResult
    {
        private ImportResult(ImportStatus status, string reason, DroppedValue[] dropped)
        {
            Status = status;
            Reason = reason;
            Dropped = new ReadOnlyCollection<DroppedValue>(dropped);
        }

        public ImportStatus Status { get; }

        /// <summary>For Refused and Undecodable, what the player is told; empty otherwise.</summary>
        public string Reason { get; }

        /// <summary>
        /// For Imported and Absent, the values the map dropped, in the order it met them; empty
        /// otherwise.
        /// </summary>
        public ReadOnlyCollection<DroppedValue> Dropped { get; }

        /// <exception cref="ArgumentNullException"><paramref name="dropped"/> or an item is null.</exception>
        public static ImportResult Imported(IEnumerable<DroppedValue> dropped)
        {
            return new ImportResult(ImportStatus.Imported, "", Copy(dropped));
        }

        /// <exception cref="ArgumentNullException"><paramref name="dropped"/> or an item is null.</exception>
        public static ImportResult Absent(IEnumerable<DroppedValue> dropped)
        {
            return new ImportResult(ImportStatus.Absent, "", Copy(dropped));
        }

        /// <exception cref="ArgumentNullException"><paramref name="reason"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="reason"/> is empty.</exception>
        public static ImportResult Refused(string reason)
        {
            return new ImportResult(ImportStatus.Refused, Checked(reason), new DroppedValue[0]);
        }

        /// <exception cref="ArgumentNullException"><paramref name="reason"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="reason"/> is empty.</exception>
        public static ImportResult Undecodable(string reason)
        {
            return new ImportResult(ImportStatus.Undecodable, Checked(reason), new DroppedValue[0]);
        }

        private static DroppedValue[] Copy(IEnumerable<DroppedValue> dropped)
        {
            if (dropped == null) throw new ArgumentNullException("dropped");
            var copy = new List<DroppedValue>(dropped);
            foreach (DroppedValue value in copy)
            {
                if (value == null) throw new ArgumentNullException("dropped", "a dropped value is null");
            }
            return copy.ToArray();
        }

        private static string Checked(string reason)
        {
            if (reason == null) throw new ArgumentNullException("reason");
            if (reason.Length == 0) throw new ArgumentException("a refused or undecodable import needs a reason", "reason");
            return reason;
        }
    }
}
