using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Settings for positional tracking: per-axis sensitivity, limits, smoothing, and inversion.
    /// The connection flag that selects between the two smoothing values is NOT here: it is
    /// runtime state, not a user setting, so it lives on
    /// <see cref="CameraUnlock.Core.Processing.PositionProcessor"/> as IsRemoteConnection and
    /// a settings rebuild never has to re-supply it.
    ///
    /// There is exactly ONE constructor, and it takes the full asymmetric form. An earlier
    /// design paired a 9-float symmetric constructor with a 10-float asymmetric one; when
    /// the single Smoothing field became LocalSmoothing + RemoteSmoothing the asymmetric
    /// overload landed on 9 required floats too, which is the arity the pre-migration
    /// asymmetric constructor took. Every stale positional call still compiled and silently
    /// rebound one slot to the left, turning a forward-lean limit into a smoothing value.
    /// Keeping a single constructor at 10 required floats, with
    /// <see cref="Symmetric(float, float, float, float, float, float, float, float, float, bool, bool, bool)"/>
    /// at 9, means no stale call site can bind to anything.
    /// </summary>
    public struct PositionSettings
    {
        /// <summary>X-axis (lateral) sensitivity multiplier.</summary>
        public float SensitivityX { get; }

        /// <summary>Y-axis (vertical) sensitivity multiplier.</summary>
        public float SensitivityY { get; }

        /// <summary>Z-axis (depth) sensitivity multiplier.</summary>
        public float SensitivityZ { get; }

        /// <summary>Maximum X displacement in meters.</summary>
        public float LimitX { get; }

        /// <summary>Maximum Y displacement in meters (upward).</summary>
        public float LimitY { get; }

        /// <summary>Maximum Y displacement in meters (downward). Restricts how far below eye height the camera can move.</summary>
        public float LimitYDown { get; }

        /// <summary>Maximum Z displacement in meters (forward).</summary>
        public float LimitZ { get; }

        /// <summary>Maximum Z displacement in meters (backward). Restricts how far back the camera can move.</summary>
        public float LimitZBack { get; }

        /// <summary>User smoothing applied when the tracker runs on this machine (loopback).</summary>
        public float LocalSmoothing { get; }

        /// <summary>User smoothing applied when the tracker is a remote device on the network.</summary>
        public float RemoteSmoothing { get; }

        /// <summary>Invert X axis.</summary>
        public bool InvertX { get; }

        /// <summary>Invert Y axis.</summary>
        public bool InvertY { get; }

        /// <summary>Invert Z axis.</summary>
        public bool InvertZ { get; }

        /// <summary>
        /// Default settings: sensitivity=1.0, limits=(0.30, 0.20 up/down, 0.40 forward, 0.10 back),
        /// localSmoothing=0.0, remoteSmoothing=0.15.
        /// </summary>
        public static PositionSettings Default
        {
            get
            {
                return Symmetric(
                    1.0f, 1.0f, 1.0f,
                    0.30f, 0.20f, 0.40f, 0.10f,
                    SmoothingUtils.DefaultLocalSmoothing, SmoothingUtils.DefaultRemoteSmoothing,
                    false, false, false);
            }
        }

        /// <param name="limitY">Maximum upward displacement in meters.</param>
        /// <param name="limitYDown">Maximum downward displacement in meters. Crouching range
        /// (down) is usually tighter than standing range (up) to stop the camera clipping into
        /// the player body.</param>
        /// <param name="limitZ">Maximum forward displacement in meters.</param>
        /// <param name="limitZBack">Maximum backward displacement in meters.</param>
        public PositionSettings(
            float sensitivityX, float sensitivityY, float sensitivityZ,
            float limitX, float limitY, float limitYDown, float limitZ, float limitZBack,
            float localSmoothing, float remoteSmoothing,
            bool invertX = false, bool invertY = false, bool invertZ = false)
        {
            SensitivityX = sensitivityX;
            SensitivityY = sensitivityY;
            SensitivityZ = sensitivityZ;
            LimitX = limitX;
            LimitY = limitY;
            LimitYDown = limitYDown;
            LimitZ = limitZ;
            LimitZBack = limitZBack;
            LocalSmoothing = localSmoothing;
            RemoteSmoothing = remoteSmoothing;
            InvertX = invertX;
            InvertY = invertY;
            InvertZ = invertZ;
        }

        /// <summary>
        /// Builds settings whose vertical limit is the same up and down: <paramref name="limitY"/>
        /// is mirrored into <see cref="LimitYDown"/>.
        /// </summary>
        public static PositionSettings Symmetric(
            float sensitivityX, float sensitivityY, float sensitivityZ,
            float limitX, float limitY, float limitZ, float limitZBack,
            float localSmoothing, float remoteSmoothing,
            bool invertX = false, bool invertY = false, bool invertZ = false)
        {
            return new PositionSettings(
                sensitivityX, sensitivityY, sensitivityZ,
                limitX, limitY, limitY, limitZ, limitZBack,
                localSmoothing, remoteSmoothing,
                invertX, invertY, invertZ);
        }

        /// <summary>
        /// Returns a copy carrying the given smoothing pair, leaving every other field alone.
        /// Owners that keep the two smoothing values as their own state recompose them onto
        /// each incoming settings object through this, so a settings assignment can never
        /// clobber smoothing and call order stops mattering.
        /// </summary>
        public PositionSettings WithSmoothing(float localSmoothing, float remoteSmoothing)
        {
            return new PositionSettings(
                SensitivityX, SensitivityY, SensitivityZ,
                LimitX, LimitY, LimitYDown, LimitZ, LimitZBack,
                localSmoothing, remoteSmoothing,
                InvertX, InvertY, InvertZ);
        }
    }
}
