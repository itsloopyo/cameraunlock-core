using System;
using System.Collections.Generic;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A pose-shaping setting a frozen legacy reader read (approved change pose_shaping): a
    /// sensitivity, unit scale, deadzone, response curve or axis inversion, which the canonical
    /// format has no row for. <see cref="Record(bool, bool, string, string, ICollection{PoseShapingValue}, ICollection{DroppedValue})"/>
    /// adds the effective legacy value beside the value the game shipped to the import's
    /// pose-shaping list. Equal, the conversion folds the shipped value into the mod's own axis
    /// code and nothing is dropped. Different, the player set it, and it is also added to the
    /// dropped values as <see cref="DropRule.PoseShaping"/>. The map sets no runtime member from
    /// it. The C++ twin is <c>cameraunlock::config::LegacyPoseShaping</c>.
    /// </summary>
    public static class LegacyPoseShaping
    {
        /// <exception cref="ArgumentNullException">A text or a collection is null.</exception>
        public static void Record(bool value, bool shipped, string section, string key,
            ICollection<PoseShapingValue> poseShaping, ICollection<DroppedValue> dropped)
        {
            Add(value ? "true" : "false", shipped ? "true" : "false", value == shipped, section, key, poseShaping, dropped);
        }

        /// <exception cref="ArgumentNullException">A text or a collection is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="shipped"/> is not finite.</exception>
        public static void Record(float value, float shipped, string section, string key,
            ICollection<PoseShapingValue> poseShaping, ICollection<DroppedValue> dropped)
        {
            CheckShipped(!float.IsNaN(shipped) && !float.IsInfinity(shipped), section, key);
            Add(Text(value), FloatCodec.RenderFinite(shipped), value == shipped, section, key, poseShaping, dropped);
        }

        /// <exception cref="ArgumentNullException">A text or a collection is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="shipped"/> is not finite.</exception>
        public static void Record(double value, double shipped, string section, string key,
            ICollection<PoseShapingValue> poseShaping, ICollection<DroppedValue> dropped)
        {
            CheckShipped(!double.IsNaN(shipped) && !double.IsInfinity(shipped), section, key);
            Add(Text(value), DoubleCodec.RenderFinite(shipped), value == shipped, section, key, poseShaping, dropped);
        }

        private static string Text(float value)
        {
            if (float.IsNaN(value)) return "nan";
            if (float.IsInfinity(value)) return value > 0 ? "inf" : "-inf";
            return FloatCodec.RenderFinite(value);
        }

        private static string Text(double value)
        {
            if (double.IsNaN(value)) return "nan";
            if (double.IsInfinity(value)) return value > 0 ? "inf" : "-inf";
            return DoubleCodec.RenderFinite(value);
        }

        private static void CheckShipped(bool finite, string section, string key)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (!finite) throw new ArgumentException("[" + section + "] " + key + ": the shipped value is not finite", "shipped");
        }

        private static void Add(string value, string shipped, bool folded, string section, string key,
            ICollection<PoseShapingValue> poseShaping, ICollection<DroppedValue> dropped)
        {
            if (poseShaping == null) throw new ArgumentNullException("poseShaping");
            if (dropped == null) throw new ArgumentNullException("dropped");
            poseShaping.Add(new PoseShapingValue(section, key, value, shipped, folded));
            if (!folded) dropped.Add(new DroppedValue(DropRule.PoseShaping, section, key, value));
        }
    }
}
