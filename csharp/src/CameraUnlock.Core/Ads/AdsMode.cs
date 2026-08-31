using System;

namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// What head tracking does while the sights are up.
    /// <para>
    /// Every mod in the fleet that covers a game with an aim-down-sights state ships the
    /// same cycle, the same value strings, the same toast wording and the same key, so a
    /// player who learns this in one shooter knows it in all of them. That is only true if
    /// there is exactly one copy of the strings, which is this file and its C++ twin
    /// cameraunlock/ads/ads_mode.h: a mod that spells its own "tracking on, aim marker
    /// shown" has already drifted.
    /// </para>
    /// <para>
    /// The config key is always <c>ads_mode</c> (spelled <c>AdsMode</c> in an INI), it
    /// always defaults to <see cref="Paused"/>, and a settings-panel dropdown carries the
    /// same order as the cycle so the key and the panel walk the modes the same way.
    /// </para>
    /// </summary>
    public enum AdsMode
    {
        /// <summary>
        /// Tracking stands down for as long as the sights are up. Stock ADS,
        /// indistinguishable from an unmodded game.
        /// </summary>
        Paused = 0,

        /// <summary>
        /// Tracking stays live through the aim, and the mod draws its own marker at the
        /// projected impact point.
        /// </summary>
        Marker = 1,

        /// <summary>Tracking stays live, nothing drawn.</summary>
        Tracked = 2,
    }

    /// <summary>
    /// The cross-mod ADS vocabulary: value strings, toasts, panel labels, the cycle and the
    /// parser. C# twin of the free functions in cameraunlock/ads/ads_mode.h.
    ///
    /// <para>
    /// <b>Two slots or three.</b> A game whose own ADS reticle is an aim indicator at the
    /// impact point AND is reachable by the mod's reticle compensation while the sights are
    /// up ships TWO slots, <see cref="AdsMode.Paused"/> and <see cref="AdsMode.Tracked"/>,
    /// and never mentions <see cref="AdsMode.Marker"/> anywhere: not in its config
    /// validation, not in its panel, not in its README. Everything else ships three. A
    /// scope's own reticle does not count, because it is only honest while the eye sits
    /// exactly on the optic, which is precisely what head tracking breaks. This class
    /// carries all three names; which of them a given mod offers is that mod's cycle
    /// choice, not this enum's.
    /// </para>
    /// </summary>
    public static class AdsModes
    {
        /// <summary>
        /// The mode that cannot be wrong: raising the sights removes the head rotation from
        /// the view, which by itself swings the view onto the point the reticle was
        /// marking, and from there the game owns the sight picture.
        /// </summary>
        public const AdsMode Default = AdsMode.Paused;

        /// <summary>
        /// The string a config file holds. Cross-mod contract: never localised, never
        /// renamed to match a game's own wording.
        /// </summary>
        public static string Value(AdsMode mode)
        {
            switch (mode)
            {
                case AdsMode.Paused: return "paused";
                case AdsMode.Marker: return "marker";
                case AdsMode.Tracked: return "tracked";
            }
            return "paused";
        }

        /// <summary>What the key press says it did.</summary>
        public static string Toast(AdsMode mode)
        {
            switch (mode)
            {
                case AdsMode.Paused: return "ADS: tracking paused";
                case AdsMode.Marker: return "ADS: tracking on, aim marker shown";
                case AdsMode.Tracked: return "ADS: tracking on, no aim marker";
            }
            return "ADS: tracking paused";
        }

        /// <summary>The label a settings panel shows against each value.</summary>
        public static string Label(AdsMode mode)
        {
            switch (mode)
            {
                case AdsMode.Paused: return "Tracking paused";
                case AdsMode.Marker: return "Tracking on, aim marker shown";
                case AdsMode.Tracked: return "Tracking on, no aim marker";
            }
            return "Tracking paused";
        }

        /// <summary>
        /// Three-slot cycle. A two-slot mod uses <see cref="NextTwoSlot"/> rather than
        /// shipping a third slot that only cycles back to the first.
        /// </summary>
        public static AdsMode Next(AdsMode mode)
        {
            switch (mode)
            {
                case AdsMode.Paused: return AdsMode.Marker;
                case AdsMode.Marker: return AdsMode.Tracked;
                case AdsMode.Tracked: return AdsMode.Paused;
            }
            return Default;
        }

        /// <summary>Two-slot cycle, for a game whose own ADS reticle the mod can drive.</summary>
        public static AdsMode NextTwoSlot(AdsMode mode)
        {
            return mode == AdsMode.Paused ? AdsMode.Tracked : AdsMode.Paused;
        }

        /// <summary>
        /// True when this mode stands tracking down for the duration of the aim, which is
        /// the one branch that closes a mod's tracking gate on the sights coming up.
        /// Named rather than spelled <c>mode == Paused</c> in every mod, so the rule has a
        /// single definition on both sides of the library.
        /// </summary>
        public static bool SuspendsTracking(AdsMode mode)
        {
            return mode == AdsMode.Paused;
        }

        /// <summary>
        /// Anything that is not one of the three values is the DEFAULT, not whichever
        /// branch happens to be last. That covers a typo in a hand-edited file, and it is
        /// also the migration path for a mode renamed since an older release wrote the
        /// setting: the player lands on stock ADS rather than on head tracking through
        /// their sights that they never asked for.
        /// <para>
        /// <paramref name="allowMarker"/> is false for a two-slot mod, where the string
        /// <c>marker</c> must not resolve to anything: a config written by a three-slot
        /// sibling, or by an older release of the same mod, otherwise selects a mode that
        /// does not exist.
        /// </para>
        /// </summary>
#if NULLABLE_ENABLED
        public static AdsMode Parse(string? text, bool allowMarker = true)
#else
        public static AdsMode Parse(string text, bool allowMarker = true)
#endif
        {
            if (text == null) return Default;
            string trimmed = TrimAsciiWhitespace(text);
            if (string.Equals(trimmed, "paused", StringComparison.OrdinalIgnoreCase)) return AdsMode.Paused;
            if (allowMarker && string.Equals(trimmed, "marker", StringComparison.OrdinalIgnoreCase)) return AdsMode.Marker;
            if (string.Equals(trimmed, "tracked", StringComparison.OrdinalIgnoreCase)) return AdsMode.Tracked;
            return Default;
        }

        // The same six characters cpp/include/cameraunlock/ads/ads_mode.h trims, at both
        // ends. Deliberately NOT string.Trim(), which folds in every Unicode space: this
        // parser and its C++ twin read the same config key out of the same file, and one
        // trimming a non-breaking space that the other does not is one mod tracking
        // through ADS while its sibling stands down.
        private static string TrimAsciiWhitespace(string text)
        {
            int start = 0;
            int end = text.Length;
            while (start < end && IsAsciiWhitespace(text[start])) start++;
            while (end > start && IsAsciiWhitespace(text[end - 1])) end--;
            return text.Substring(start, end - start);
        }

        private static bool IsAsciiWhitespace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        }
    }
}
