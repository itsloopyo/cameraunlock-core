using System;

namespace CameraUnlock.Core.Processing.AxisTransform
{
    /// <summary>
    /// Predefined mapping presets for common use cases.
    /// </summary>
    public enum MappingPreset
    {
        /// <summary>
        /// Default 1:1 mapping with no modifications.
        /// </summary>
        Default,

        /// <summary>
        /// Pitch axis inverted (for flight sim controls or personal preference).
        /// </summary>
        InvertedPitch,

        /// <summary>
        /// Roll axis disabled (for games that don't support roll or where it's distracting).
        /// </summary>
        NoRoll,

        /// <summary>
        /// Higher sensitivity on all axes (1.5x yaw/pitch, 1.2x roll).
        /// Good for users who want faster response.
        /// </summary>
        HighSensitivity,

        /// <summary>
        /// Lower sensitivity on all axes (0.7x yaw/pitch, 0.5x roll).
        /// Good for precise aiming or users with large head movements.
        /// </summary>
        LowSensitivity,

        /// <summary>
        /// Optimized for competitive FPS games.
        /// Fast yaw with quadratic curve, moderate pitch, no roll, small deadzone.
        /// </summary>
        Competitive,

        /// <summary>
        /// Realistic movement for simulation games.
        /// All axes with S-curve smoothing for natural feel.
        /// </summary>
        Simulation
    }

    /// <summary>
    /// Utility class for applying mapping presets.
    /// </summary>
    public static class MappingPresets
    {
        /// <summary>
        /// Applies a preset to the given mapping configuration.
        /// </summary>
        /// <param name="config">The configuration to modify.</param>
        /// <param name="preset">The preset to apply.</param>
        /// <exception cref="ArgumentNullException">Thrown if config is null.</exception>
        public static void ApplyPreset(MappingConfig config, MappingPreset preset)
        {
            if (config == null)
            {
                throw new ArgumentNullException(nameof(config));
            }

            // Start with defaults
            config.ResetToDefault();

            switch (preset)
            {
                case MappingPreset.Default:
                    // Already reset to default
                    break;

                case MappingPreset.InvertedPitch:
                    config.PitchConfig.Inverted = true;
                    break;

                case MappingPreset.NoRoll:
                    config.RollConfig.Source = AxisSource.None;
                    break;

                case MappingPreset.HighSensitivity:
                    config.YawConfig.Sensitivity = 1.5f;
                    config.PitchConfig.Sensitivity = 1.5f;
                    config.RollConfig.Sensitivity = 1.2f;
                    break;

                case MappingPreset.LowSensitivity:
                    config.YawConfig.Sensitivity = 0.7f;
                    config.PitchConfig.Sensitivity = 0.7f;
                    config.RollConfig.Sensitivity = 0.5f;
                    break;

                case MappingPreset.Competitive:
                    // Fast yaw with quadratic curve for precision
                    config.YawConfig.Sensitivity = 1.2f;
                    config.YawConfig.SensitivityCurve = SensitivityCurve.Quadratic;
                    config.YawConfig.CurveStrength = 0.5f;
                    config.YawConfig.DeadzoneMin = 0.5f;

                    // Moderate pitch
                    config.PitchConfig.Sensitivity = 0.9f;
                    config.PitchConfig.DeadzoneMin = 0.5f;

                    // No roll for competitive
                    config.RollConfig.Source = AxisSource.None;
                    break;

                case MappingPreset.Simulation:
                    // All axes with S-curve for realistic feel
                    config.YawConfig.SensitivityCurve = SensitivityCurve.SCurve;
                    config.YawConfig.CurveStrength = 0.7f;

                    config.PitchConfig.SensitivityCurve = SensitivityCurve.SCurve;
                    config.PitchConfig.CurveStrength = 0.7f;

                    config.RollConfig.SensitivityCurve = SensitivityCurve.SCurve;
                    config.RollConfig.CurveStrength = 0.7f;
                    break;
            }
        }

        /// <summary>
        /// Creates a new MappingConfig with the specified preset applied.
        /// </summary>
        /// <param name="preset">The preset to apply.</param>
        /// <returns>A new MappingConfig with the preset applied.</returns>
        public static MappingConfig CreateFromPreset(MappingPreset preset)
        {
            var config = new MappingConfig();
            ApplyPreset(config, preset);
            return config;
        }
    }
}
