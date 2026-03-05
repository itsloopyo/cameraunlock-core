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

        /// <summary>Maximum Y displacement in meters.</summary>
        public float LimitY { get; }

        /// <summary>Maximum Z displacement in meters.</summary>
        public float LimitZ { get; }

        /// <summary>Smoothing factor (0 = instant, 1 = very slow).</summary>
        public float Smoothing { get; }

        /// <summary>Invert X axis.</summary>
        public bool InvertX { get; }

        /// <summary>Invert Y axis.</summary>
        public bool InvertY { get; }

        /// <summary>Invert Z axis.</summary>
        public bool InvertZ { get; }

        /// <summary>Default settings: sensitivity=1.0, limits=(0.15, 0.10, 0.20), smoothing=0.15.</summary>
        public static PositionSettings Default => new PositionSettings(
            1.0f, 1.0f, 1.0f,
            0.15f, 0.10f, 0.20f,
            0.15f,
            false, false, false
        );

        public PositionSettings(
            float sensitivityX, float sensitivityY, float sensitivityZ,
            float limitX, float limitY, float limitZ,
            float smoothing,
            bool invertX = false, bool invertY = false, bool invertZ = false)
        {
            SensitivityX = sensitivityX;
            SensitivityY = sensitivityY;
            SensitivityZ = sensitivityZ;
            LimitX = limitX;
            LimitY = limitY;
            LimitZ = limitZ;
            Smoothing = smoothing;
            InvertX = invertX;
            InvertY = invertY;
            InvertZ = invertZ;
        }
    }
}
