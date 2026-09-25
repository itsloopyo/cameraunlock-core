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
        private ImportResult(ImportStatus status, string reason, DroppedValue[] dropped, PoseShapingValue[] poseShaping)
        {
            Status = status;
            Reason = reason;
            Dropped = new ReadOnlyCollection<DroppedValue>(dropped);
            PoseShaping = new ReadOnlyCollection<PoseShapingValue>(poseShaping);
        }

        public ImportStatus Status { get; }

        /// <summary>For Refused and Undecodable, what the player is told; empty otherwise.</summary>
        public string Reason { get; }

        /// <summary>
        /// For Imported and Absent, the values the map dropped, in the order it met them; empty
        /// otherwise.
        /// </summary>
        public ReadOnlyCollection<DroppedValue> Dropped { get; }

        /// <summary>
        /// For Imported and Absent, every pose-shaping setting the frozen reader read, in the order
        /// the map met them (<see cref="LegacyPoseShaping"/>); empty otherwise.
        /// </summary>
        public ReadOnlyCollection<PoseShapingValue> PoseShaping { get; }

        /// <exception cref="ArgumentNullException"><paramref name="dropped"/> or an item is null.</exception>
        public static ImportResult Imported(IEnumerable<DroppedValue> dropped)
        {
            return Imported(dropped, new PoseShapingValue[0]);
        }

        /// <exception cref="ArgumentNullException">A collection or an item is null.</exception>
        public static ImportResult Imported(IEnumerable<DroppedValue> dropped, IEnumerable<PoseShapingValue> poseShaping)
        {
            return new ImportResult(ImportStatus.Imported, "", Copy(dropped, "dropped"), Copy(poseShaping, "poseShaping"));
        }

        /// <exception cref="ArgumentNullException"><paramref name="dropped"/> or an item is null.</exception>
        public static ImportResult Absent(IEnumerable<DroppedValue> dropped)
        {
            return Absent(dropped, new PoseShapingValue[0]);
        }

        /// <exception cref="ArgumentNullException">A collection or an item is null.</exception>
        public static ImportResult Absent(IEnumerable<DroppedValue> dropped, IEnumerable<PoseShapingValue> poseShaping)
        {
            return new ImportResult(ImportStatus.Absent, "", Copy(dropped, "dropped"), Copy(poseShaping, "poseShaping"));
        }

        /// <exception cref="ArgumentNullException"><paramref name="reason"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="reason"/> is empty.</exception>
        public static ImportResult Refused(string reason)
        {
            return new ImportResult(ImportStatus.Refused, Checked(reason), new DroppedValue[0], new PoseShapingValue[0]);
        }

        /// <exception cref="ArgumentNullException"><paramref name="reason"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="reason"/> is empty.</exception>
        public static ImportResult Undecodable(string reason)
        {
            return new ImportResult(ImportStatus.Undecodable, Checked(reason), new DroppedValue[0], new PoseShapingValue[0]);
        }

        private static T[] Copy<T>(IEnumerable<T> items, string name) where T : class
        {
            if (items == null) throw new ArgumentNullException(name);
            var copy = new List<T>(items);
            foreach (T item in copy)
            {
                if (item == null) throw new ArgumentNullException(name, "an item of " + name + " is null");
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
