using System;
using System.Collections.Generic;
using System.IO;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Default implementation of IHeadTrackingConfig that can be loaded from an INI file
    /// or configured programmatically.
    /// </summary>
    public class HeadTrackingConfigData : IHeadTrackingConfig
    {
        /// <inheritdoc />
        public int UdpPort { get; set; } = OpenTrackReceiver.DefaultPort;

        /// <inheritdoc />
        public bool EnableOnStartup { get; set; } = true;

        /// <inheritdoc />
        public SensitivitySettings Sensitivity { get; set; } = SensitivitySettings.Default;

        /// <inheritdoc />
        public string RecenterKeyName { get; set; } = "Home";

        /// <inheritdoc />
        public string ToggleKeyName { get; set; } = "End";

        /// <inheritdoc />
        public bool AimDecouplingEnabled { get; set; } = true;

        /// <inheritdoc />
        public bool ShowDecoupledReticle { get; set; } = true;

        /// <inheritdoc />
        public float[] ReticleColorRgba { get; set; } = new float[] { 1f, 1f, 1f, 1f };

        /// <inheritdoc />
        public float Smoothing { get; set; } = 0f;

        /// <summary>
        /// Creates a new config with default values.
        /// </summary>
        public HeadTrackingConfigData()
        {
        }

        /// <summary>
        /// Loads configuration from an INI file. Returns defaults for missing values.
        /// </summary>
        /// <param name="filePath">Path to the config file.</param>
        /// <param name="log">Optional logging action.</param>
        /// <returns>Loaded configuration.</returns>
#if NULLABLE_ENABLED
        public static HeadTrackingConfigData LoadFromFile(string filePath, Action<string>? log = null)
#else
        public static HeadTrackingConfigData LoadFromFile(string filePath, Action<string> log = null)
#endif
        {
            var config = new HeadTrackingConfigData();

            try
            {
                var values = ConfigParsingUtils.ParseIniFile(filePath);
                if (values.Count == 0)
                {
                    log?.Invoke("No config file found, using defaults");
                    return config;
                }

                config.ApplyValues(values, log);
                log?.Invoke("Config loaded successfully");
            }
            catch (Exception ex)
            {
                log?.Invoke(string.Format("Config load error (using defaults): {0}", ex.Message));
            }

            return config;
        }

        /// <summary>
        /// Applies values from a dictionary to this config.
        /// </summary>
#if NULLABLE_ENABLED
        public void ApplyValues(Dictionary<string, string> values, Action<string>? log = null)
#else
        public void ApplyValues(Dictionary<string, string> values, Action<string> log = null)
#endif
        {
            float yawSens = Sensitivity.Yaw;
            float pitchSens = Sensitivity.Pitch;
            float rollSens = Sensitivity.Roll;
            bool invertYaw = Sensitivity.InvertYaw;
            bool invertPitch = Sensitivity.InvertPitch;
            bool invertRoll = Sensitivity.InvertRoll;

            foreach (var kvp in values)
            {
                string key = kvp.Key.ToLowerInvariant().Replace("_", "").Replace("-", "");
                string value = kvp.Value;

                int intVal;
                float floatVal;
                bool boolVal;

                switch (key)
                {
                    case "udpport":
                    case "port":
                        if (ConfigParsingUtils.TryParseInt(value, out intVal))
                            UdpPort = intVal;
                        break;

                    case "enableonstartup":
                    case "enabled":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            EnableOnStartup = boolVal;
                        break;

                    case "yawsensitivity":
                    case "yawsens":
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                            yawSens = floatVal;
                        break;

                    case "pitchsensitivity":
                    case "pitchsens":
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                            pitchSens = floatVal;
                        break;

                    case "rollsensitivity":
                    case "rollsens":
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                            rollSens = floatVal;
                        break;

                    case "invertyaw":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertYaw = boolVal;
                        break;

                    case "invertpitch":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertPitch = boolVal;
                        break;

                    case "invertroll":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertRoll = boolVal;
                        break;

                    case "recenterkey":
                    case "centerkey":
                        RecenterKeyName = value;
                        break;

                    case "togglekey":
                        ToggleKeyName = value;
                        break;

                    case "aimdecoupling":
                    case "decoupleaim":
                    case "aimdecouple":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            AimDecouplingEnabled = boolVal;
                        break;

                    case "showreticle":
                    case "showdecoupledreticle":
                    case "showcrosshair":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            ShowDecoupledReticle = boolVal;
                        break;

                    case "reticlecolor":
                    case "crosshaircolor":
                        float[] color;
                        if (ConfigParsingUtils.TryParseColor(value, out color))
                            ReticleColorRgba = color;
                        break;

                    case "smoothing":
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                            Smoothing = System.Math.Max(0f, System.Math.Min(1f, floatVal));
                        break;
                }
            }

            Sensitivity = new SensitivitySettings(yawSens, pitchSens, rollSens, invertYaw, invertPitch, invertRoll);
        }

        /// <summary>
        /// Gets the default config file path next to the specified assembly.
        /// </summary>
        public static string GetDefaultConfigPath(System.Reflection.Assembly assembly, string fileName = "HeadTracking.cfg")
        {
            string dir = ConfigParsingUtils.GetAssemblyDirectory(assembly);
            return Path.Combine(dir, fileName);
        }
    }
}
