namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Settings for positional tracking: per-axis sensitivity, limits, smoothing, and inversion.
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

        /// <summary>User smoothing factor (0 = frame interpolation only, 1 = heavy smoothing).</summary>
        public float Smoothing { get; }

        /// <summary>Invert X axis.</summary>
        public bool InvertX { get; }

        /// <summary>Invert Y axis.</summary>
        public bool InvertY { get; }

        /// <summary>Invert Z axis.</summary>
        public bool InvertZ { get; }

        /// <summary>Default settings: sensitivity=1.0, limits=(0.30, 0.20, 0.40/0.10back), smoothing=0.15.</summary>
        public static PositionSettings Default => new PositionSettings(
            1.0f, 1.0f, 1.0f,
            0.30f, 0.20f, 0.40f, 0.10f,
            0.15f,
            false, false, false
        );

        public PositionSettings(
            float sensitivityX, float sensitivityY, float sensitivityZ,
            float limitX, float limitY, float limitZ, float limitZBack,
            float smoothing,
            bool invertX = false, bool invertY = false, bool invertZ = false)
            : this(sensitivityX, sensitivityY, sensitivityZ,
                   limitX, limitY, limitY, limitZ, limitZBack,
                   smoothing, invertX, invertY, invertZ)
        {
        }

        /// <summary>
        /// Overload with an asymmetric vertical limit. Crouching range (down) is usually
        /// tighter than standing range (up) to stop the camera clipping into the player body.
        /// </summary>
        public PositionSettings(
            float sensitivityX, float sensitivityY, float sensitivityZ,
            float limitX, float limitY, float limitYDown, float limitZ, float limitZBack,
            float smoothing,
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
            Smoothing = smoothing;
            InvertX = invertX;
            InvertY = invertY;
            InvertZ = invertZ;
        }
    }
}
