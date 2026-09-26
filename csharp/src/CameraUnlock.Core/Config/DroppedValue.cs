using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>One legacy value a legacy import's map did not carry, for the migration log.</summary>
    public sealed class DroppedValue
    {
        /// <exception cref="ArgumentNullException">A text is null.</exception>
        /// <exception cref="ArgumentOutOfRangeException"><paramref name="rule"/> is not a
        /// <see cref="DropRule"/>.</exception>
        public DroppedValue(DropRule rule, string section, string key, string value)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (value == null) throw new ArgumentNullException("value");
            Reason(rule);
            Rule = rule;
            Section = section;
            Key = key;
            Value = value;
        }

        public DropRule Rule { get; }

        public string Section { get; }

        public string Key { get; }

        /// <summary>The value as the import read it, e.g. "nan".</summary>
        public string Value { get; }

        /// <summary>
        /// The line the migration logs, e.g. <c>not carried: [Smoothing] RemoteSmoothing=nan, it
        /// is not a finite number, so the default is used</c>.
        /// </summary>
        public string Describe()
        {
            return "not carried: [" + Section + "] " + Key + "=" + Value + ", " + Reason(Rule);
        }

        private static string Reason(DropRule rule)
        {
            switch (rule)
            {
                case DropRule.NonFiniteNumber:
                    return "it is not a finite number, so the default is used";
                case DropRule.PoseShaping:
                    return "sensitivity, scales, deadzones, response curves and axis inversion are set in the tracker now, not in this mod";
                case DropRule.Reticle:
                    return "this mod no longer draws or toggles a reticle";
                case DropRule.FollowsDefault:
                    return "this setting now follows the mod's default";
                case DropRule.KeyCodeOutOfRange:
                    return "it is not a key code from 0x01 to 0xFE, so the action is unbound";
                case DropRule.ModifierKey:
                    return "it is a Ctrl, Shift or Alt key, which goes down before the key of any chord made with it, so it is unbound";
                default:
                    throw new ArgumentOutOfRangeException("rule", rule, "drop rule " + (int)rule + " is not a DropRule");
            }
        }
    }
}
