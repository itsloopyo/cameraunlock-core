using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Interface for head tracking configuration.
    /// Implementations can be file-based (INI), BepInEx ConfigEntry-based, etc.
    /// </summary>
    public interface IHeadTrackingConfig
    {
        /// <summary>UDP port for OpenTrack data reception.</summary>
        int UdpPort { get; }

        /// <summary>Whether tracking is enabled on startup.</summary>
        bool EnableOnStartup { get; }

        /// <summary>Sensitivity settings for all axes.</summary>
        SensitivitySettings Sensitivity { get; }

        /// <summary>
        /// Key name for recentering (e.g., "Home", "F9").
        /// Framework-specific code should parse this into the appropriate key code type.
        /// </summary>
        string RecenterKeyName { get; }

        /// <summary>
        /// Key name for toggling tracking (e.g., "End", "F10").
        /// Framework-specific code should parse this into the appropriate key code type.
        /// </summary>
        string ToggleKeyName { get; }

        /// <summary>Whether aim decoupling is enabled.</summary>
        bool AimDecouplingEnabled { get; }

        /// <summary>Whether to show the decoupled crosshair/reticle.</summary>
        bool ShowDecoupledReticle { get; }

        /// <summary>
        /// Reticle color as RGBA floats (0-1 range).
        /// Returns array of 4 floats: [R, G, B, A].
        /// </summary>
        float[] ReticleColorRgba { get; }

        /// <summary>Smoothing factor (0.0 = none, 1.0 = maximum). Higher values add latency.</summary>
        float Smoothing { get; }
    }
}
