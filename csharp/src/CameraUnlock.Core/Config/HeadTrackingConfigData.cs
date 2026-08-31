using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using CameraUnlock.Core.Ads;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Effects;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Default implementation of IHeadTrackingConfig that can be loaded from an INI file
    /// or configured programmatically.
    /// <para>
    /// Key matching runs through <see cref="ConfigKeySchema"/>, which is generated from
    /// data/config-schema.json together with the C++ table, so both halves of the library
    /// accept exactly the same spellings. Matching is section-less: a key is lowercased and
    /// stripped of '_' and '-', so the section a key sits under decides file layout and
    /// documentation, not parsing.
    /// </para>
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

        /// <summary>Key name for toggling positional (6DOF) tracking (e.g., "PageUp").</summary>
        public string PositionToggleKeyName { get; set; } = "PageUp";

        /// <summary>Key name for toggling the decoupled aim reticle (e.g., "Insert").</summary>
        public string ReticleToggleKeyName { get; set; } = "Insert";

        /// <summary>
        /// Key name for cycling the tracking mode. Empty by default: a mod that has no mode
        /// cycle leaves it unbound rather than advertising a key that does nothing.
        /// </summary>
        public string CycleTrackingModeKeyName { get; set; } = string.Empty;

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

        /// <summary>Whether positional (6DOF) tracking is applied.</summary>
        public bool PositionEnabled { get; set; } = true;

        /// <summary>
        /// Positional sensitivity, limits and inversion. The smoothing pair on this struct is
        /// recomposed from <see cref="LocalSmoothing"/> and <see cref="RemoteSmoothing"/> at
        /// the end of <see cref="ApplyValues"/>, so position and rotation cannot end up on
        /// different smoothing without a second set of keys nobody asked for.
        /// </summary>
        public PositionSettings Position { get; set; } = PositionSettings.Default;

        /// <summary>
        /// Metres from the neck pivot forward to the point the tracker watches. Feeds
        /// <see cref="CameraUnlock.Core.Processing.PositionProcessor.TrackerPivotForward"/>.
        /// </summary>
        public float TrackerPivotForward { get; set; } = 0.0f;

        /// <summary>
        /// Metres from the neck pivot up to the point the tracker watches. Feeds
        /// <see cref="CameraUnlock.Core.Processing.PositionProcessor.TrackerPivotUp"/>.
        /// </summary>
        public float TrackerPivotUp { get; set; } = 0.0f;

        /// <summary>
        /// What head tracking does while the sights are up. Parsed with <c>marker</c>
        /// ALLOWED, because this type cannot know whether the mod reading it ships two
        /// slots or three.
        /// <para>
        /// A two-slot mod therefore cannot take this field as it stands: the raw string is
        /// consumed by <c>ApplyValues</c> and not retained, so there is nothing left to
        /// re-parse. Such a mod either reads the key itself with
        /// <c>AdsModes.Parse(raw, allowMarker: false)</c> before handing the dictionary
        /// over, or maps <see cref="Ads.AdsMode.Marker"/> onto
        /// <see cref="Ads.AdsModes.Default"/> after loading. Left alone it would hold a
        /// mode it does not implement, and <c>SuspendsTracking(Marker)</c> is false, so
        /// tracking would stay live through ADS with a marker the mod never draws.
        /// </para>
        /// </summary>
        public AdsMode AdsMode { get; set; } = AdsModes.Default;

        /// <summary>
        /// A carried light that follows the head rather than the aim. Inert in a mod for a
        /// game with no carried light; see <see cref="HeadFollowLightSettings"/>.
        /// </summary>
        public HeadFollowLightSettings Light { get; set; } = new HeadFollowLightSettings();

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

            float posSensX = Position.SensitivityX;
            float posSensY = Position.SensitivityY;
            float posSensZ = Position.SensitivityZ;
            float limitX = Position.LimitX;
            float limitY = Position.LimitY;
            float limitYDown = Position.LimitYDown;
            bool sawLimitY = false;
            bool sawLimitYDown = false;
            string limitYSpelling = string.Empty;
            bool verticalLimitsWereSymmetric = Position.LimitY == Position.LimitYDown;
            float limitZ = Position.LimitZ;
            float limitZBack = Position.LimitZBack;
            bool invertPosX = Position.InvertX;
            bool invertPosY = Position.InvertY;
            bool invertPosZ = Position.InvertZ;

            var firstSpellingOf = new Dictionary<string, string>();

            foreach (var kvp in values)
            {
                string key = ConfigKeySchema.Resolve(kvp.Key);
                if (key == null) continue;

                string firstSpelling;
                if (firstSpellingOf.TryGetValue(key, out firstSpelling))
                {
                    WarnDuplicateConcept(log, firstSpelling, kvp.Key);
                }
                else
                {
                    firstSpellingOf[key] = kvp.Key;
                }

                string value = kvp.Value;

                int intVal;
                float floatVal;
                bool boolVal;

                switch (key)
                {
                    case ConfigKeySchema.Keys.UdpPort:
                        // Range-checked here rather than left to the socket. An
                        // out-of-range port reached UdpClient's constructor and threw
                        // ArgumentOutOfRangeException at plugin Awake(), killing the mod
                        // with a socket stack trace that names no config key.
                        //
                        // The bound is the actual protocol range, NOT the BepInEx
                        // binding's 1024 floor: Windows imposes no privileged-port
                        // restriction on a UDP bind, so a user running with udpport
                        // below 1024 has a working configuration today and rejecting it
                        // would break them.
                        if (ConfigParsingUtils.TryParseInt(value, out intVal))
                        {
                            if (intVal >= 1 && intVal <= 65535)
                            {
                                UdpPort = intVal;
                            }
                            else
                            {
                                log?.Invoke(string.Format(
                                    "Config key '{0}' has an out-of-range value '{1}' (expected 1-65535) - using {2}",
                                    kvp.Key, value, UdpPort.ToString(CultureInfo.InvariantCulture)));
                            }
                        }
                        break;

                    case ConfigKeySchema.Keys.EnableOnStartup:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            EnableOnStartup = boolVal;
                        break;

                    case ConfigKeySchema.Keys.YawSensitivity:
                        if (TryParseMagnitude(log, kvp.Key, value, yawSens, out floatVal))
                            yawSens = floatVal;
                        break;

                    case ConfigKeySchema.Keys.PitchSensitivity:
                        if (TryParseMagnitude(log, kvp.Key, value, pitchSens, out floatVal))
                            pitchSens = floatVal;
                        break;

                    case ConfigKeySchema.Keys.RollSensitivity:
                        if (TryParseMagnitude(log, kvp.Key, value, rollSens, out floatVal))
                            rollSens = floatVal;
                        break;

                    case ConfigKeySchema.Keys.InvertYaw:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertYaw = boolVal;
                        break;

                    case ConfigKeySchema.Keys.InvertPitch:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertPitch = boolVal;
                        break;

                    case ConfigKeySchema.Keys.InvertRoll:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertRoll = boolVal;
                        break;

                    case ConfigKeySchema.Keys.RecenterKey:
                        RecenterKeyName = value;
                        break;

                    case ConfigKeySchema.Keys.ToggleKey:
                        ToggleKeyName = value;
                        break;

                    case ConfigKeySchema.Keys.YawModeKey:
                        YawModeKeyName = value;
                        break;

                    case ConfigKeySchema.Keys.PositionToggleKey:
                        PositionToggleKeyName = value;
                        break;

                    case ConfigKeySchema.Keys.ReticleToggleKey:
                        ReticleToggleKeyName = value;
                        break;

                    case ConfigKeySchema.Keys.CycleTrackingModeKey:
                        CycleTrackingModeKeyName = value;
                        break;

                    case ConfigKeySchema.Keys.WorldSpaceYaw:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            WorldSpaceYaw = boolVal;
                        break;

                    case ConfigKeySchema.Keys.AimDecoupling:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            AimDecouplingEnabled = boolVal;
                        break;

                    case ConfigKeySchema.Keys.ShowReticle:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            ShowDecoupledReticle = boolVal;
                        break;

                    case ConfigKeySchema.Keys.ReticleColor:
                        float[] color;
                        if (ConfigParsingUtils.TryParseColor(value, out color))
                            ReticleColorRgba = color;
                        break;

                    case ConfigKeySchema.Keys.LocalSmoothing:
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

                    case ConfigKeySchema.Keys.RemoteSmoothing:
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

                    case ConfigKeySchema.Keys.PositionEnabled:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            PositionEnabled = boolVal;
                        break;

                    case ConfigKeySchema.Keys.PositionSensitivityX:
                        if (TryParseMagnitude(log, kvp.Key, value, posSensX, out floatVal))
                            posSensX = floatVal;
                        break;

                    case ConfigKeySchema.Keys.PositionSensitivityY:
                        if (TryParseMagnitude(log, kvp.Key, value, posSensY, out floatVal))
                            posSensY = floatVal;
                        break;

                    case ConfigKeySchema.Keys.PositionSensitivityZ:
                        if (TryParseMagnitude(log, kvp.Key, value, posSensZ, out floatVal))
                            posSensZ = floatVal;
                        break;

                    case ConfigKeySchema.Keys.PositionLimitX:
                        if (TryParseMagnitude(log, kvp.Key, value, limitX, out floatVal))
                            limitX = floatVal;
                        break;

                    case ConfigKeySchema.Keys.PositionLimitY:
                        if (TryParseMagnitude(log, kvp.Key, value, limitY, out floatVal))
                        {
                            limitY = floatVal;
                            sawLimitY = true;
                            limitYSpelling = kvp.Key;
                        }
                        break;

                    case ConfigKeySchema.Keys.PositionLimitYDown:
                        if (TryParseMagnitude(log, kvp.Key, value, limitYDown, out floatVal))
                        {
                            limitYDown = floatVal;
                            sawLimitYDown = true;
                        }
                        break;

                    case ConfigKeySchema.Keys.PositionLimitZ:
                        if (TryParseMagnitude(log, kvp.Key, value, limitZ, out floatVal))
                            limitZ = floatVal;
                        break;

                    case ConfigKeySchema.Keys.PositionLimitZBack:
                        if (TryParseMagnitude(log, kvp.Key, value, limitZBack, out floatVal))
                            limitZBack = floatVal;
                        break;

                    case ConfigKeySchema.Keys.InvertPositionX:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertPosX = boolVal;
                        break;

                    case ConfigKeySchema.Keys.InvertPositionY:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertPosY = boolVal;
                        break;

                    case ConfigKeySchema.Keys.InvertPositionZ:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            invertPosZ = boolVal;
                        break;

                    case ConfigKeySchema.Keys.TrackerPivotForward:
                        if (TryParseMagnitude(log, kvp.Key, value, TrackerPivotForward, out floatVal))
                            TrackerPivotForward = floatVal;
                        break;

                    case ConfigKeySchema.Keys.TrackerPivotUp:
                        if (TryParseMagnitude(log, kvp.Key, value, TrackerPivotUp, out floatVal))
                            TrackerPivotUp = floatVal;
                        break;

                    case ConfigKeySchema.Keys.AdsMode:
                        AdsMode = AdsModes.Parse(value);
                        // Every neighbouring key reports a value it could not use. Without
                        // this, the one typo the fail-to-default design exists to survive
                        // - AdsMode=trakced - hands the player stock ADS and says nothing,
                        // so there is no way to find out why the setting did nothing.
                        if (!string.Equals(value.Trim(), AdsModes.Value(AdsMode),
                                StringComparison.OrdinalIgnoreCase))
                        {
                            log?.Invoke(string.Format(
                                "Config key '{0}' has an unrecognised value '{1}' - using {2}. Valid values are paused, marker and tracked.",
                                kvp.Key, value, AdsModes.Value(AdsMode)));
                        }
                        break;

                    case ConfigKeySchema.Keys.LightFollowsHead:
                        if (ConfigParsingUtils.TryParseBool(value, out boolVal))
                            Light.FollowsHead = boolVal;
                        break;

                    case ConfigKeySchema.Keys.LightMultiplier:
                        if (ConfigParsingUtils.TryParseFloat(value, out floatVal))
                        {
                            if (floatVal >= 0f && floatVal <= HeadFollowLightSettings.MaxMultiplier)
                            {
                                Light.Multiplier = floatVal;
                            }
                            else
                            {
                                log?.Invoke(string.Format(
                                    "Config key '{0}' has an out-of-range value '{1}' (expected 0-{2}) - using {3}",
                                    kvp.Key, value,
                                    HeadFollowLightSettings.MaxMultiplier.ToString(CultureInfo.InvariantCulture),
                                    Light.Multiplier.ToString(CultureInfo.InvariantCulture)));
                            }
                        }
                        break;

                    case ConfigKeySchema.Keys.Smoothing:
                        WarnRetiredSmoothingKey(log, kvp.Key);
                        break;
                }
            }

            // A file that names one vertical limit means one vertical limit. The clamp is
            // [-LimitYDown, +LimitY], so leaving the down side at its default silently caps a
            // raised LimitY at 0.20m downward - the exact bug ~47 mod repos carry today, each
            // of which hand-mirrors the key or does not. Decided after the loop, so dictionary
            // order cannot change the outcome, and an explicit LimitYDown in the same
            // dictionary always wins.
            //
            // The mirror only fires when the vertical limits were already symmetric. A caller
            // that built PositionSettings with limitYDown != limitY picked that asymmetry to
            // keep the camera out of the player body, and mirroring would throw it away
            // without the file ever naming the down side.
            if (sawLimitY && !sawLimitYDown)
            {
                if (verticalLimitsWereSymmetric)
                {
                    limitYDown = limitY;
                }
                else
                {
                    log?.Invoke(string.Format(
                        "Config key '{0}' set the upward limit to {1}; the downward limit stays at {2} because this config was built with an asymmetric vertical limit. Name LimitYDown to change it.",
                        limitYSpelling,
                        limitY.ToString(CultureInfo.InvariantCulture),
                        limitYDown.ToString(CultureInfo.InvariantCulture)));
                }
            }

            Sensitivity = new SensitivitySettings(yawSens, pitchSens, rollSens, invertYaw, invertPitch, invertRoll);
            Position = new PositionSettings(
                posSensX, posSensY, posSensZ,
                limitX, limitY, limitYDown, limitZ, limitZBack,
                LocalSmoothing, RemoteSmoothing,
                invertPosX, invertPosY, invertPosZ);
        }

        // Limits, sensitivities and tracker-pivot distances are magnitudes. A negative one
        // is not a smaller value, it is a broken one: ClampToLimits calls
        // Clamp(y, -LimitYDown, LimitY), so a negative LimitY puts min above max and every
        // input on that axis comes back pinned to the bound, whatever the head does. Axis
        // direction has its own keys (InvertYaw/InvertPositionY/...), so nothing legitimate
        // is expressed by a minus sign here.
        //
        // Zero passes on purpose. A zero limit clamps the axis to [0, 0], which is a real
        // request to lock that axis, and a zero sensitivity is a real request to disable it.
        // Rejected values are reported and the previous value stands - clamping a negative
        // to zero would silently lock an axis the user meant to widen.
#if NULLABLE_ENABLED
        private static bool TryParseMagnitude(Action<string>? log, string key, string value, float current, out float result)
#else
        private static bool TryParseMagnitude(Action<string> log, string key, string value, float current, out float result)
#endif
        {
            if (!ConfigParsingUtils.TryParseFloat(value, out result))
            {
                result = current;
                return false;
            }

            if (result < 0f)
            {
                log?.Invoke(string.Format(
                    "Config key '{0}' has a negative value '{1}' - limits, sensitivities and tracker pivot distances must be zero or greater. Rejected, keeping {2}.",
                    key, value, current.ToString(CultureInfo.InvariantCulture)));
                result = current;
                return false;
            }

            return true;
        }

#if NULLABLE_ENABLED
        private static void WarnDuplicateConcept(Action<string>? log, string firstSpelling, string secondSpelling)
#else
        private static void WarnDuplicateConcept(Action<string> log, string firstSpelling, string secondSpelling)
#endif
        {
            log?.Invoke(string.Format(
                "Config keys '{0}' and '{1}' are two spellings of the same setting. Only one takes effect and which one is not defined - remove one.",
                firstSpelling, secondSpelling));
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
