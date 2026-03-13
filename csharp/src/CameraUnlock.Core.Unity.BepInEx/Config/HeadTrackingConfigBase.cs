using System;
using BepInEx.Configuration;
using UnityEngine;

namespace CameraUnlock.Core.Unity.BepInEx.Config
{
    /// <summary>
    /// Abstract base class for BepInEx head tracking configuration.
    /// Provides common configuration bindings with caching for per-frame access.
    ///
    /// Usage:
    /// 1. Create a subclass in your mod
    /// 2. Call Initialize() with your plugin's ConfigFile
    /// 3. Access cached values directly for per-frame reads
    /// 4. Override OnConfigChanged() to propagate changes to your systems
    /// </summary>
    public abstract class HeadTrackingConfigBase
    {
        // Network settings
        public ConfigEntry<int> UdpPort { get; private set; }

        // General settings
        public ConfigEntry<bool> EnableOnStartup { get; private set; }

        // Sensitivity settings
        public ConfigEntry<float> YawSensitivity { get; private set; }
        public ConfigEntry<float> PitchSensitivity { get; private set; }
        public ConfigEntry<float> RollSensitivity { get; private set; }

        // Inversion settings
        public ConfigEntry<bool> InvertYaw { get; private set; }
        public ConfigEntry<bool> InvertPitch { get; private set; }
        public ConfigEntry<bool> InvertRoll { get; private set; }

        // Hotkey settings
        public ConfigEntry<KeyCode> RecenterKey { get; private set; }
        public ConfigEntry<KeyCode> ToggleKey { get; private set; }
        public ConfigEntry<KeyCode> PositionToggleKey { get; private set; }
        public ConfigEntry<KeyCode> ReticleToggleKey { get; private set; }

        // Aim decoupling settings
        public ConfigEntry<bool> EnableAimDecoupling { get; private set; }
        public ConfigEntry<bool> ShowDecoupledCrosshair { get; private set; }

        // Cached values for per-frame access
        public float CachedYawSensitivity { get; private set; }
        public float CachedPitchSensitivity { get; private set; }
        public float CachedRollSensitivity { get; private set; }
        public bool CachedInvertYaw { get; private set; }
        public bool CachedInvertPitch { get; private set; }
        public bool CachedInvertRoll { get; private set; }
        public bool CachedEnableAimDecoupling { get; private set; }
        public bool CachedShowDecoupledCrosshair { get; private set; }

        /// <summary>
        /// Event fired when any configuration value changes.
        /// </summary>
        public event Action OnConfigChanged;

        /// <summary>
        /// Default UDP port for OpenTrack.
        /// </summary>
        protected virtual int DefaultUdpPort => 4242;

        /// <summary>
        /// Default enable on startup value.
        /// </summary>
        protected virtual bool DefaultEnableOnStartup => true;

        /// <summary>
        /// Default sensitivity values (1.0 = normal).
        /// </summary>
        protected virtual float DefaultSensitivity => 1.0f;

        /// <summary>
        /// Default recenter hotkey.
        /// </summary>
        protected virtual KeyCode DefaultRecenterKey => KeyCode.Home;

        /// <summary>
        /// Default toggle hotkey.
        /// </summary>
        protected virtual KeyCode DefaultToggleKey => KeyCode.End;

        /// <summary>
        /// Default position toggle hotkey.
        /// </summary>
        protected virtual KeyCode DefaultPositionToggleKey => KeyCode.PageUp;

        /// <summary>
        /// Default reticle toggle hotkey.
        /// </summary>
        protected virtual KeyCode DefaultReticleToggleKey => KeyCode.Insert;

        /// <summary>
        /// Whether aim decoupling is enabled by default.
        /// </summary>
        protected virtual bool DefaultEnableAimDecoupling => true;

        /// <summary>
        /// Whether to show decoupled crosshair by default.
        /// </summary>
        protected virtual bool DefaultShowDecoupledCrosshair => true;

        /// <summary>
        /// Initializes all configuration bindings.
        /// Must be called from plugin Awake() before other components are initialized.
        /// </summary>
        /// <param name="config">The plugin's ConfigFile instance</param>
        public virtual void Initialize(ConfigFile config)
        {
            if (config == null)
                throw new ArgumentNullException(nameof(config));

            // Network section
            UdpPort = config.Bind(
                "Network",
                "UdpPort",
                DefaultUdpPort,
                new ConfigDescription(
                    "UDP port for OpenTrack data. Requires game restart to take effect.",
                    new AcceptableValueRange<int>(1024, 65535)
                )
            );

            // General section
            EnableOnStartup = config.Bind(
                "General",
                "EnableOnStartup",
                DefaultEnableOnStartup,
                "Enable head tracking automatically when the game loads"
            );

            // Sensitivity section
            YawSensitivity = config.Bind(
                "Sensitivity",
                "YawSensitivity",
                DefaultSensitivity,
                new ConfigDescription(
                    "Yaw (left/right) sensitivity multiplier",
                    new AcceptableValueRange<float>(0.1f, 3.0f)
                )
            );

            PitchSensitivity = config.Bind(
                "Sensitivity",
                "PitchSensitivity",
                DefaultSensitivity,
                new ConfigDescription(
                    "Pitch (up/down) sensitivity multiplier",
                    new AcceptableValueRange<float>(0.1f, 3.0f)
                )
            );

            RollSensitivity = config.Bind(
                "Sensitivity",
                "RollSensitivity",
                DefaultSensitivity,
                new ConfigDescription(
                    "Roll (tilt) sensitivity multiplier",
                    new AcceptableValueRange<float>(0.1f, 3.0f)
                )
            );

            // Inversion section
            InvertYaw = config.Bind(
                "Inversion",
                "InvertYaw",
                false,
                "Invert yaw axis. When enabled, looking left moves camera right."
            );

            InvertPitch = config.Bind(
                "Inversion",
                "InvertPitch",
                false,
                "Invert pitch axis. When enabled, looking up moves camera down."
            );

            InvertRoll = config.Bind(
                "Inversion",
                "InvertRoll",
                false,
                "Invert roll axis. When enabled, tilting left tilts camera right."
            );

            // Hotkeys section
            RecenterKey = config.Bind(
                "Hotkeys",
                "RecenterKey",
                DefaultRecenterKey,
                "Key to recenter head tracking (set current position as center)"
            );

            ToggleKey = config.Bind(
                "Hotkeys",
                "ToggleKey",
                DefaultToggleKey,
                "Key to toggle head tracking on/off"
            );

            PositionToggleKey = config.Bind(
                "Hotkeys",
                "PositionToggleKey",
                DefaultPositionToggleKey,
                "Key to toggle position (6DOF) tracking on/off"
            );

            ReticleToggleKey = config.Bind(
                "Hotkeys",
                "ReticleToggleKey",
                DefaultReticleToggleKey,
                "Key to toggle the decoupled aim reticle on/off"
            );

            // Aim decoupling section
            EnableAimDecoupling = config.Bind(
                "Aim Decoupling",
                "EnableAimDecoupling",
                DefaultEnableAimDecoupling,
                "Decouple aim direction from head tracking. When enabled, projectiles and attacks " +
                "go where your mouse aims, not where your head is looking."
            );

            ShowDecoupledCrosshair = config.Bind(
                "Aim Decoupling",
                "ShowDecoupledCrosshair",
                DefaultShowDecoupledCrosshair,
                "Move crosshair to show actual aim position when decoupled. When disabled, " +
                "crosshair stays centered but aim still goes to mouse position."
            );

            // Subscribe to config change events
            SubscribeToChanges();

            // Initialize cached values
            RefreshCache();

            // Allow subclasses to bind additional settings
            OnInitialize(config);
        }

        /// <summary>
        /// Override to bind additional game-specific configuration entries.
        /// Called after all base settings are bound.
        /// </summary>
        /// <param name="config">The plugin's ConfigFile instance</param>
        protected virtual void OnInitialize(ConfigFile config)
        {
        }

        /// <summary>
        /// Subscribes to SettingChanged events for all config entries.
        /// </summary>
        private void SubscribeToChanges()
        {
            YawSensitivity.SettingChanged += HandleSettingChanged;
            PitchSensitivity.SettingChanged += HandleSettingChanged;
            RollSensitivity.SettingChanged += HandleSettingChanged;
            InvertYaw.SettingChanged += HandleSettingChanged;
            InvertPitch.SettingChanged += HandleSettingChanged;
            InvertRoll.SettingChanged += HandleSettingChanged;
            EnableAimDecoupling.SettingChanged += HandleSettingChanged;
            ShowDecoupledCrosshair.SettingChanged += HandleSettingChanged;
        }

        private void HandleSettingChanged(object sender, EventArgs e)
        {
            RefreshCache();
            OnConfigChanged?.Invoke();
        }

        /// <summary>
        /// Refreshes all cached config values from their ConfigEntry sources.
        /// Called once at initialization and on any config change.
        /// </summary>
        public void RefreshCache()
        {
            CachedYawSensitivity = YawSensitivity.Value;
            CachedPitchSensitivity = PitchSensitivity.Value;
            CachedRollSensitivity = RollSensitivity.Value;
            CachedInvertYaw = InvertYaw.Value;
            CachedInvertPitch = InvertPitch.Value;
            CachedInvertRoll = InvertRoll.Value;
            CachedEnableAimDecoupling = EnableAimDecoupling.Value;
            CachedShowDecoupledCrosshair = ShowDecoupledCrosshair.Value;

            // Allow subclasses to refresh their custom caches
            OnRefreshCache();
        }

        /// <summary>
        /// Override to refresh any game-specific cached values.
        /// Called after base cache values are refreshed.
        /// </summary>
        protected virtual void OnRefreshCache()
        {
        }
    }
}
