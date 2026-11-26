using System;

namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// The aim mode of a shooter with positional tracking, in the order its key cycles (see
    /// the shooter-ads-handling skill). C# twin of cameraunlock/ads/aim_mode.h. It is stored
    /// as three config bools, [Position] TrueFreeLook, FreeLookMarker and StockSights, a set
    /// as RotationEnabled and PositionEnabled are the tracking mode.
    /// </summary>
    public enum AimMode
    {
        /// <summary>The eye stays on the sight line while aiming.</summary>
        SightsLocked = 0,

        /// <summary>
        /// The weapon stays put in the world, the head moves freely around it, and the mod
        /// draws an aim marker where the round will land while the sights are up.
        /// </summary>
        FreeLookMarker = 1,

        /// <summary>The same with no marker.</summary>
        TrueFreeLook = 2,

        /// <summary>
        /// While the sights are up the head's yaw, pitch and lean ease out and only roll
        /// stays, so the sight picture is the game's own. At the hip tracking is whole.
        /// </summary>
        StockSights = 3,
    }

    /// <summary>
    /// The aim mode's config values, its cycle, its labels, the marker's opacity and when
    /// stock sights eases the pose out.
    /// </summary>
    public static class AimModes
    {
        /// <summary>
        /// StockSights wins over the other two. Without it the marker bit means nothing
        /// without free look: FreeLookMarker alone is sights locked with no marker.
        /// TrueFreeLook alone is true free look, which is also what a config written before
        /// the marker existed holds, and a config written before stock sights existed has no
        /// StockSights and is one of the other three.
        /// </summary>
        public static AimMode Decode(bool trueFreeLook, bool freeLookMarker, bool stockSights)
        {
            if (stockSights) return AimMode.StockSights;
            if (!trueFreeLook) return AimMode.SightsLocked;
            return freeLookMarker ? AimMode.FreeLookMarker : AimMode.TrueFreeLook;
        }

        /// <summary>
        /// The three values a mode is saved as. Stock sights is false, false, true, and the
        /// marker is never set without free look.
        /// </summary>
        public static void Encode(AimMode mode, out bool trueFreeLook, out bool freeLookMarker, out bool stockSights)
        {
            Require(mode);
            trueFreeLook = IsFreeLook(mode);
            freeLookMarker = mode == AimMode.FreeLookMarker;
            stockSights = mode == AimMode.StockSights;
        }

        /// <summary>
        /// Whether the lean stays honest on the camera while aiming, which is what the two
        /// free look modes share. Stock sights eases the whole lean out instead, so while it
        /// is fading it handles the lean as sights locked does.
        /// </summary>
        public static bool IsFreeLook(AimMode mode)
        {
            Require(mode);
            return mode == AimMode.FreeLookMarker || mode == AimMode.TrueFreeLook;
        }

        /// <summary>
        /// Whether the head's yaw, pitch and lean are eased out this frame. Feed it to an
        /// <see cref="AdsFade"/> of its own: the fade's output is the share of those five that
        /// reaches the view, 1 at the hip and 0 with the sights up, and a press of the mode
        /// key mid-aim rides the fade like the aim button does. Roll is never scaled by it.
        /// </summary>
        public static bool StockSightsEngaged(AimMode mode, bool aiming)
        {
            Require(mode);
            return aiming && mode == AimMode.StockSights;
        }

        /// <summary>
        /// The mode the key steps to: sights locked, free look with a marker, true free look,
        /// stock sights and round again.
        /// </summary>
        public static AimMode Next(AimMode mode)
        {
            switch (mode)
            {
                case AimMode.SightsLocked: return AimMode.FreeLookMarker;
                case AimMode.FreeLookMarker: return AimMode.TrueFreeLook;
                case AimMode.TrueFreeLook: return AimMode.StockSights;
                case AimMode.StockSights: return AimMode.SightsLocked;
                default: throw NotAMode(mode);
            }
        }

        /// <summary>
        /// The line a mod shows and logs when the mode changes, the same in every mod.
        /// </summary>
        public static string Label(AimMode mode)
        {
            switch (mode)
            {
                case AimMode.SightsLocked: return "Aim mode: sights locked";
                case AimMode.FreeLookMarker: return "Aim mode: free look with marker";
                case AimMode.TrueFreeLook: return "Aim mode: true free look";
                case AimMode.StockSights: return "Aim mode: stock sights";
                default: throw NotAMode(mode);
            }
        }

        /// <summary>
        /// How opaque the aim marker is drawn. <paramref name="sightsUp"/> is the mod's own
        /// fade for the sights, 0 at the hip and 1 with them fully up, which is the other way
        /// round from <see cref="AdsFade"/>'s scale.
        /// </summary>
        public static float MarkerOpacity(AimMode mode, float sightsUp)
        {
            Require(mode);
            return mode == AimMode.FreeLookMarker ? sightsUp : 0.0f;
        }

        private static void Require(AimMode mode)
        {
            if (mode < AimMode.SightsLocked || mode > AimMode.StockSights) throw NotAMode(mode);
        }

        private static ArgumentOutOfRangeException NotAMode(AimMode mode)
        {
            return new ArgumentOutOfRangeException("mode", (int)mode + " is not an aim mode");
        }
    }
}
