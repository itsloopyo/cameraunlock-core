using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
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

        /// <summary>
        /// Key name for toggling world-space vs camera-local yaw (e.g., "PageDown").
        /// Framework-specific code parses this into the appropriate key code type.
        /// </summary>
        public string YawModeKeyName { get; set; } = "PageDown";

        /// <summary>
        /// Yaw mode at startup. true = horizon-locked yaw (rotates around world up
        /// regardless of pitch). false = camera-local yaw (rotates around the camera's
        /// current up axis, producing leaning/rolling at extreme pitches).
        /// </summary>
        public bool WorldSpaceYaw { get; set; } = true;

        /// <inheritdoc />
        public bool AimDecouplingEnabled { get; set; } = true;

        /// <inheritdoc />
        public bool ShowDecoupledReticle { get; set; } = true;

        /// <inheritdoc />
        public float[] ReticleColorRgba { get; set; } = new float[] { 1f, 1f, 1f, 1f };

        /// <inheritdoc />
        public float LocalSmoothing { get; set; } = SmoothingUtils.DefaultLocalSmoothing;

        /// <inheritdoc />
        public float RemoteSmoothing { get; set; } = SmoothingUtils.DefaultRemoteSmoothing;

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
                        // Range-checked here rather than left to the socket. An
                        // out-of-range port reached UdpClient's constructor and threw
                        // ArgumentOutOfRangeException at plugin Awake(), killing the mod
                        // with a socket stack trace that names no config key. The BepInEx
                        // path already binds this to AcceptableValueRange(1024, 65535);
                        // the INI path is the same boundary and had nothing.
                        if (ConfigParsingUtils.TryParseInt(value, out intVal))
                        {
                            if (intVal >= 1024 && intVal <= 65535)
                            {
                                UdpPort = intVal;
                            }
                            else
                            {
                                log?.Invoke(string.Format(
                                    "Config key '{0}' has an out-of-range value '{1}' (expected 1024-65535) - using {2}",
                                    key, value, UdpPort.ToString(CultureInfo.InvariantCulture)));
                            }
                        }
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

                    case "yawmodekey":
                        YawModeKeyName = value;
                        break;

                    case "worldspaceyaw":
                    case "horizonlockedyaw":
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            WorldSpaceYaw = boolVal;
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

                    case "localsmoothing":
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                        {
                            LocalSmoothing = MathUtils.Clamp01(floatVal);
                        }
                        else
                        {
                            LocalSmoothing = SmoothingUtils.DefaultLocalSmoothing;
                            WarnUnusable(log, "LocalSmoothing", value, LocalSmoothing);
                        }
                        break;

                    case "remotesmoothing":
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                        {
                            RemoteSmoothing = MathUtils.Clamp01(floatVal);
                        }
                        else
                        {
                            RemoteSmoothing = SmoothingUtils.DefaultRemoteSmoothing;
                            WarnUnusable(log, "RemoteSmoothing", value, RemoteSmoothing);
                        }
                        break;

                    case "smoothing":
                    case "smoothingfactor":
                        WarnRetiredSmoothingKey(log, kvp.Key);
                        break;
                }
            }

            Sensitivity = new SensitivitySettings(yawSens, pitchSens, rollSens, invertYaw, invertPitch, invertRoll);
        }

#if NULLABLE_ENABLED
        private static void WarnUnusable(Action<string>? log, string key, string value, float fallback)
#else
        private static void WarnUnusable(Action<string> log, string key, string value, float fallback)
#endif
        {
            log?.Invoke(string.Format(
                "Config key '{0}' has an unusable value '{1}' (not a finite number) - using {2}",
                key, value, fallback.ToString(System.Globalization.CultureInfo.InvariantCulture)));
        }

        // Warned once per process rather than once per load: mods reload config on a
        // hotkey or a file watcher, and repeating this every reload buries it.
        private static bool _warnedRetiredSmoothingKey;

#if NULLABLE_ENABLED
        private static void WarnRetiredSmoothingKey(Action<string>? log, string key)
#else
        private static void WarnRetiredSmoothingKey(Action<string> log, string key)
#endif
        {
            if (_warnedRetiredSmoothingKey) return;
            _warnedRetiredSmoothingKey = true;

            // Deliberately NOT migrated into the new keys. The old single value carried a
            // hidden 0.15 floor, so the number in an existing config does not mean what it
            // used to: copying 0.8 across would silently hand a local user smoothing they
            // never chose under the new semantics, and copying it into only one of the two
            // would be a guess about which connection they were on.
            log?.Invoke(string.Format(
                "Config key '{0}' has been retired and is IGNORED. Smoothing is now two keys: " +
                "LocalSmoothing (default {1}, applies to a tracker on this machine) and " +
                "RemoteSmoothing (default {2}, applies to a tracker on the network). The old " +
                "value is not migrated because the semantics changed - it carried a hidden " +
                "{2} floor that no longer exists. Set the two new keys.",
                key,
                SmoothingUtils.DefaultLocalSmoothing.ToString(System.Globalization.CultureInfo.InvariantCulture),
                SmoothingUtils.DefaultRemoteSmoothing.ToString(System.Globalization.CultureInfo.InvariantCulture)));
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
