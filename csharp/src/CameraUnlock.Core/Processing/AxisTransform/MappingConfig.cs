using System;

namespace CameraUnlock.Core.Processing.AxisTransform
{
    /// <summary>
    /// Complete axis mapping configuration for all three rotation axes.
    /// Combines individual AxisConfig for yaw, pitch, and roll.
    /// </summary>
    public class MappingConfig
    {
        /// <summary>
        /// Configuration for yaw axis (horizontal head turn).
        /// </summary>
        public AxisConfig YawConfig { get; set; }

        /// <summary>
        /// Configuration for pitch axis (vertical head tilt).
        /// </summary>
        public AxisConfig PitchConfig { get; set; }

        /// <summary>
        /// Configuration for roll axis (head tilt side to side).
        /// </summary>
        public AxisConfig RollConfig { get; set; }

        /// <summary>
        /// Creates a new MappingConfig with default configurations.
        /// </summary>
        public MappingConfig()
        {
            YawConfig = new AxisConfig
            {
                Source = AxisSource.Yaw
            };

            PitchConfig = new AxisConfig
            {
                Source = AxisSource.Pitch
            };

            RollConfig = new AxisConfig
            {
                Source = AxisSource.Roll
            };
        }

        /// <summary>
        /// Applies the mapping to raw tracking data array.
        /// </summary>
        /// <param name="rawData">Array of 6 floats: [yaw, pitch, roll, x, y, z].</param>
        /// <param name="yaw">Output: transformed yaw value.</param>
        /// <param name="pitch">Output: transformed pitch value.</param>
        /// <param name="roll">Output: transformed roll value.</param>
        /// <exception cref="ArgumentException">Thrown if rawData is null or has fewer than 6 elements.</exception>
        public void ApplyMapping(float[] rawData, out float yaw, out float pitch, out float roll)
        {
            if (rawData == null || rawData.Length < 6)
            {
                throw new ArgumentException("rawData must contain at least 6 elements [yaw, pitch, roll, x, y, z]", nameof(rawData));
            }

            yaw = ApplyAxisMapping(YawConfig, rawData);
            pitch = ApplyAxisMapping(PitchConfig, rawData);
            roll = ApplyAxisMapping(RollConfig, rawData);
        }

        /// <summary>
        /// Applies the mapping to individual rotation values (no translation).
        /// X/Y/Z sources will return 0 since no translation data is provided.
        /// </summary>
        /// <param name="rawYaw">Raw yaw input in degrees.</param>
        /// <param name="rawPitch">Raw pitch input in degrees.</param>
        /// <param name="rawRoll">Raw roll input in degrees.</param>
        /// <param name="yaw">Output: transformed yaw value.</param>
        /// <param name="pitch">Output: transformed pitch value.</param>
        /// <param name="roll">Output: transformed roll value.</param>
        public void ApplyMapping(
            float rawYaw, float rawPitch, float rawRoll,
            out float yaw, out float pitch, out float roll)
        {
            // Direct value access to avoid array allocation
            yaw = ApplyAxisMappingDirect(YawConfig, rawYaw, rawPitch, rawRoll);
            pitch = ApplyAxisMappingDirect(PitchConfig, rawYaw, rawPitch, rawRoll);
            roll = ApplyAxisMappingDirect(RollConfig, rawYaw, rawPitch, rawRoll);
        }

        /// <summary>
        /// Applies individual axis mapping without array allocation.
        /// </summary>
        private float ApplyAxisMappingDirect(AxisConfig config, float rawYaw, float rawPitch, float rawRoll)
        {
            float sourceValue;

            switch (config.Source)
            {
                case AxisSource.None:
                    return 0f;
                case AxisSource.Yaw:
                    sourceValue = rawYaw;
                    break;
                case AxisSource.Pitch:
                    sourceValue = rawPitch;
                    break;
                case AxisSource.Roll:
                    sourceValue = rawRoll;
                    break;
                default:
                    // X/Y/Z sources not available in rotation-only overload
                    sourceValue = 0f;
                    break;
            }

            return config.TransformValue(sourceValue);
        }

        /// <summary>
        /// Applies individual axis mapping.
        /// </summary>
        private float ApplyAxisMapping(AxisConfig config, float[] rawData)
        {
            if (config.Source == AxisSource.None)
            {
                return 0f;
            }

            float sourceValue;

            switch (config.Source)
            {
                case AxisSource.Yaw:
                    sourceValue = rawData[0];
                    break;
                case AxisSource.Pitch:
                    sourceValue = rawData[1];
                    break;
                case AxisSource.Roll:
                    sourceValue = rawData[2];
                    break;
                case AxisSource.X:
                    sourceValue = rawData[3];
                    break;
                case AxisSource.Y:
                    sourceValue = rawData[4];
                    break;
                case AxisSource.Z:
                    sourceValue = rawData[5];
                    break;
                default:
                    sourceValue = 0f;
                    break;
            }

            return config.TransformValue(sourceValue);
        }

        /// <summary>
        /// Resets all configurations to default values.
        /// </summary>
        public void ResetToDefault()
        {
            YawConfig = new AxisConfig
            {
                Source = AxisSource.Yaw,
                Sensitivity = 1.0f
            };

            PitchConfig = new AxisConfig
            {
                Source = AxisSource.Pitch,
                Sensitivity = 1.0f
            };

            RollConfig = new AxisConfig
            {
                Source = AxisSource.Roll,
                Sensitivity = 1.0f
            };
        }

        /// <summary>
        /// Loads a preset mapping configuration.
        /// </summary>
        /// <param name="preset">The preset to load.</param>
        public void LoadPreset(MappingPreset preset)
        {
            MappingPresets.ApplyPreset(this, preset);
        }

        /// <summary>
        /// Creates a copy of this configuration.
        /// </summary>
        /// <returns>A new MappingConfig with cloned settings.</returns>
        public MappingConfig Clone()
        {
            return new MappingConfig
            {
                YawConfig = YawConfig?.Clone() ?? new AxisConfig { Source = AxisSource.Yaw },
                PitchConfig = PitchConfig?.Clone() ?? new AxisConfig { Source = AxisSource.Pitch },
                RollConfig = RollConfig?.Clone() ?? new AxisConfig { Source = AxisSource.Roll }
            };
        }
    }
}
