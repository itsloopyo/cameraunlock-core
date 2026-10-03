using System;

namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// The aim mode of a shooter with positional tracking, in the order its key cycles (see
    /// the shooter-ads-handling skill). C# twin of cameraunlock/ads/aim_mode.h. It is stored
    /// as two config bools, [Position] TrueFreeLook and [Position] FreeLookMarker, a pair as
    /// RotationEnabled and PositionEnabled are the tracking mode.
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
    }

    /// <summary>
    /// The aim mode's config pair, its cycle, its labels and the marker's opacity.
    /// </summary>
    public static class AimModes
    {
        /// <summary>
        /// The marker bit means nothing without free look: FreeLookMarker alone is sights
        /// locked with no marker. TrueFreeLook alone is true free look, which is also what a
        /// config written before the marker existed holds.
        /// </summary>
        public static AimMode Decode(bool trueFreeLook, bool freeLookMarker)
        {
            if (!trueFreeLook) return AimMode.SightsLocked;
            return freeLookMarker ? AimMode.FreeLookMarker : AimMode.TrueFreeLook;
        }

        /// <summary>The pair a mode is saved as. Never false with true.</summary>
        public static void Encode(AimMode mode, out bool trueFreeLook, out bool freeLookMarker)
        {
            Require(mode);
            trueFreeLook = mode != AimMode.SightsLocked;
            freeLookMarker = mode == AimMode.FreeLookMarker;
        }

        /// <summary>
        /// The mode the key steps to: sights locked, free look with a marker, true free look
        /// and round again.
        /// </summary>
        public static AimMode Next(AimMode mode)
        {
            switch (mode)
            {
                case AimMode.SightsLocked: return AimMode.FreeLookMarker;
                case AimMode.FreeLookMarker: return AimMode.TrueFreeLook;
                case AimMode.TrueFreeLook: return AimMode.SightsLocked;
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
            if (mode < AimMode.SightsLocked || mode > AimMode.TrueFreeLook) throw NotAMode(mode);
        }

        private static ArgumentOutOfRangeException NotAMode(AimMode mode)
        {
            return new ArgumentOutOfRangeException("mode", (int)mode + " is not an aim mode");
        }
    }
}
