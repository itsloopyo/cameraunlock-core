using System;
using System.Collections.Generic;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A legacy position switch that also kept the mode hotkey off the position modes (approved
    /// change position_switch_off, the setting PositionAllowed stood for). The tracking mode is the
    /// only way positional tracking is switched off now. Call
    /// <see cref="Record(bool, string, string, HeadTrackingConfigData, ICollection{DroppedValue})"/>
    /// after the map has written the tracking mode. The C++ twin is
    /// <c>cameraunlock::config::LegacyPositionSwitch</c>.
    /// </summary>
    public static class LegacyPositionSwitch
    {
        /// <summary>
        /// For a <paramref name="value"/> of false, writes the rotation-only tracking mode
        /// (<see cref="HeadTrackingConfigData.RotationEnabled"/> true,
        /// <see cref="HeadTrackingConfigData.PositionEnabled"/> false), whatever the map wrote
        /// there, and adds the switch to <paramref name="dropped"/> as
        /// <see cref="DropRule.PositionSwitchOff"/>. True changes nothing and records nothing.
        /// </summary>
        /// <exception cref="ArgumentNullException">A text, the config or the collection is null.</exception>
        public static void Record(bool value, string section, string key, HeadTrackingConfigData config,
            ICollection<DroppedValue> dropped)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (config == null) throw new ArgumentNullException("config");
            if (dropped == null) throw new ArgumentNullException("dropped");
            if (value) return;
            config.RotationEnabled = true;
            config.PositionEnabled = false;
            dropped.Add(new DroppedValue(DropRule.PositionSwitchOff, section, key, "false"));
        }
    }
}
