using System;
using BepInEx.Configuration;
using CameraUnlock.Core.State;
using UnityEngine;

namespace CameraUnlock.Core.Unity.BepInEx.Input
{
    /// <summary>
    /// Handles hotkey input for toggling head tracking and recentering.
    /// Designed for BepInEx mods with ConfigEntry-based hotkey settings.
    ///
    /// Usage:
    /// 1. Add as a component to your plugin's GameObject
    /// 2. Call Initialize() with your config entries
    /// 3. Set IsInputBlocked to your game-specific text input check
    /// 4. Subscribe to OnRecenter and OnToggle events
    /// </summary>
    public class BepInExHotkeyHandler : MonoBehaviour
    {
        private ConfigEntry<KeyCode> _recenterKey;
        private ConfigEntry<KeyCode> _toggleKey;

        // Cached hotkey values to avoid ConfigEntry.Value overhead per frame
        private KeyCode _cachedRecenterKey;
        private KeyCode _cachedToggleKey;

        /// <summary>
        /// Function to check if text input is active (chat, console, etc.).
        /// Set this to your game-specific check.
        /// When returns true, hotkeys are blocked.
        /// </summary>
        public Func<bool> IsInputBlocked { get; set; }

        /// <summary>
        /// Event fired when the recenter hotkey is pressed.
        /// </summary>
        public event Action OnRecenter;

        /// <summary>
        /// Event fired when the toggle hotkey is pressed.
        /// Parameter is the new enabled state.
        /// </summary>
        public event Action<bool> OnToggle;

        /// <summary>
        /// If true, automatically toggles TrackingState when toggle key is pressed.
        /// Default is true.
        /// </summary>
        public bool AutoToggleTrackingState { get; set; } = true;

        /// <summary>
        /// Initializes the hotkey handler with ConfigEntry bindings.
        /// </summary>
        /// <param name="recenterKey">
        /// ConfigEntry for the recenter hotkey, or null for a mod that has none.
        /// The tracker owns the centre, so a mod that keeps no centre of its own has
        /// nothing to bind; passing null leaves the recenter key unbound and
        /// <see cref="OnRecenter"/> never fires.
        /// </param>
        /// <param name="toggleKey">ConfigEntry for toggle hotkey</param>
        public void Initialize(ConfigEntry<KeyCode> recenterKey, ConfigEntry<KeyCode> toggleKey)
        {
            _recenterKey = recenterKey;
            _toggleKey = toggleKey ?? throw new ArgumentNullException(nameof(toggleKey));

            CacheHotkeys();

            // Subscribe to config changes
            if (_recenterKey != null)
            {
                _recenterKey.SettingChanged += HandleSettingChanged;
            }
            _toggleKey.SettingChanged += HandleSettingChanged;
        }

        /// <summary>
        /// Initializes with only a toggle binding, for a mod with no recenter hotkey.
        /// </summary>
        public void Initialize(ConfigEntry<KeyCode> toggleKey)
        {
            Initialize(null, toggleKey);
        }

        /// <summary>
        /// Initializes the hotkey handler with direct KeyCode values.
        /// Use this if you don't have ConfigEntry bindings.
        /// </summary>
        /// <param name="recenterKey">Recenter hotkey</param>
        /// <param name="toggleKey">Toggle hotkey</param>
        public void Initialize(KeyCode recenterKey, KeyCode toggleKey)
        {
            _cachedRecenterKey = recenterKey;
            _cachedToggleKey = toggleKey;
        }

        private void HandleSettingChanged(object sender, EventArgs e)
        {
            CacheHotkeys();
        }

        private void CacheHotkeys()
        {
            if (_recenterKey != null)
            {
                _cachedRecenterKey = _recenterKey.Value;
            }
            if (_toggleKey != null)
            {
                _cachedToggleKey = _toggleKey.Value;
            }
        }

        /// <summary>
        /// Sets the recenter hotkey directly.
        /// </summary>
        public void SetRecenterKey(KeyCode key)
        {
            _cachedRecenterKey = key;
        }

        /// <summary>
        /// Sets the toggle hotkey directly.
        /// </summary>
        public void SetToggleKey(KeyCode key)
        {
            _cachedToggleKey = key;
        }

        private void Update()
        {
            // Block hotkeys during text input
            if (IsInputBlocked != null && IsInputBlocked())
            {
                return;
            }

            // Check for recenter key
            if (_cachedRecenterKey != KeyCode.None && UnityEngine.Input.GetKeyDown(_cachedRecenterKey))
            {
                HandleRecenter();
            }

            // Check for toggle key
            if (_cachedToggleKey != KeyCode.None && UnityEngine.Input.GetKeyDown(_cachedToggleKey))
            {
                HandleToggle();
            }
        }

        private void HandleRecenter()
        {
            OnRecenter?.Invoke();
        }

        private void HandleToggle()
        {
            bool newState;

            if (AutoToggleTrackingState)
            {
                newState = TrackingState.Toggle();
            }
            else
            {
                newState = !TrackingState.IsEnabled;
            }

            OnToggle?.Invoke(newState);
        }

        private void OnDestroy()
        {
            // Unsubscribe from config changes
            if (_recenterKey != null)
            {
                _recenterKey.SettingChanged -= HandleSettingChanged;
            }
            if (_toggleKey != null)
            {
                _toggleKey.SettingChanged -= HandleSettingChanged;
            }
        }
    }
}
