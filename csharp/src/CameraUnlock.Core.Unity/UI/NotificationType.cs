namespace CameraUnlock.Core.Unity.UI
{
    /// <summary>
    /// Type of notification to display, affects visual styling.
    /// </summary>
    public enum NotificationType
    {
        /// <summary>Default notification style (white text).</summary>
        Info,
        /// <summary>Success/enabled notification (green text).</summary>
        Success,
        /// <summary>Warning/disabled notification (yellow text).</summary>
        Warning,
        /// <summary>Error notification (red text).</summary>
        Error
    }
}
