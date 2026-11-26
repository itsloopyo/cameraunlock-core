using System;
using System.Collections.Generic;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A legacy neck pivot distance (approved change tracker_pivot): TrackerPivotForward,
    /// TrackerPivotUp or another spelling of either, which the canonical format has no row for.
    /// The map sets no runtime member from it. The C++ twin is
    /// <c>cameraunlock::config::LegacyTrackerPivot</c>.
    /// </summary>
    public static class LegacyTrackerPivot
    {
        /// <summary>
        /// Compares <paramref name="value"/>, the effective legacy pivot, with
        /// <paramref name="shipped"/>, the effective pivot the game shipped. Both are the distance
        /// the pipeline ran on, so 0 where the game's own switch had compensation off. Equal, the
        /// player never changed it and nothing is recorded. Different, it is added to
        /// <paramref name="dropped"/> as <see cref="DropRule.TrackerPivot"/>.
        /// </summary>
        /// <exception cref="ArgumentNullException">A text or the collection is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="shipped"/> is not finite.</exception>
        public static void Record(float value, float shipped, string section, string key, ICollection<DroppedValue> dropped)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (dropped == null) throw new ArgumentNullException("dropped");
            if (float.IsNaN(shipped) || float.IsInfinity(shipped))
            {
                throw new ArgumentException("[" + section + "] " + key + ": the shipped value is not finite", "shipped");
            }
            if (value == shipped) return;
            dropped.Add(new DroppedValue(DropRule.TrackerPivot, section, key, LegacyPoseShaping.Text(value)));
        }
    }
}
