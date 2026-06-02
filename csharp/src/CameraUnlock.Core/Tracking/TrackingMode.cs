namespace CameraUnlock.Core.Tracking
{
    /// <summary>
    /// Which tracking channels are applied to the camera.
    /// </summary>
    public enum TrackingMode
    {
        /// <summary>Full 6DOF: head rotation and positional offset.</summary>
        RotationAndPosition = 0,

        /// <summary>Head rotation only (3DOF).</summary>
        RotationOnly = 1,

        /// <summary>Positional offset only (3DOF).</summary>
        PositionOnly = 2
    }

    /// <summary>
    /// Human-readable names for <see cref="TrackingMode"/> values, for hotkey and log messages.
    /// </summary>
    public static class TrackingModeExtensions
    {
        public static string Description(this TrackingMode mode)
        {
            switch (mode)
            {
                case TrackingMode.RotationAndPosition: return "6DOF (rotation + position)";
                case TrackingMode.RotationOnly: return "rotation only";
                case TrackingMode.PositionOnly: return "position only";
                default: return mode.ToString();
            }
        }
    }
}
