using System;
using BepInEx.Configuration;
using CameraUnlock.Core.State;
using UnityEngine;

namespace CameraUnlock.Core.Unity.BepInEx.Input
{
    /// <summary>
    /// Handles the hotkey that toggles head tracking on and off.
    /// Designed for BepInEx mods with ConfigEntry-based hotkey settings.
    ///
    /// Usage:
    /// 1. Add as a component to your plugin's GameObject
    /// 2. Call Initialize() with your config entry
    /// 3. Set IsInputBlocked to your game-specific text input check
    /// 4. Subscribe to OnToggle
    /// </summary>
    public class BepInExHotkeyHandler : MonoBehaviour
    {
        private ConfigEntry<KeyCode> _toggleKey;

        // Cached hotkey value to avoid ConfigEntry.Value overhead per frame
        private KeyCode _cachedToggleKey;

        /// <summary>
        /// Function to check if text input is active (chat, console, etc.).
        /// Set this to your game-specific check.
        /// When returns true, hotkeys are blocked.
        /// </summary>
        public Func<bool> IsInputBlocked { get; set; }

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
        /// Initializes the hotkey handler with a ConfigEntry binding.
        /// </summary>
        /// <param name="toggleKey">ConfigEntry for toggle hotkey</param>
        public void Initialize(ConfigEntry<KeyCode> toggleKey)
        {
            _toggleKey = toggleKey ?? throw new ArgumentNullException(nameof(toggleKey));
            _cachedToggleKey = _toggleKey.Value;
            _toggleKey.SettingChanged += HandleSettingChanged;
        }

        /// <summary>
        /// Initializes the hotkey handler with a direct KeyCode value.
        /// Use this if you don't have a ConfigEntry binding.
        /// </summary>
        /// <param name="toggleKey">Toggle hotkey</param>
        public void Initialize(KeyCode toggleKey)
        {
            _cachedToggleKey = toggleKey;
        }

        private void HandleSettingChanged(object sender, EventArgs e)
        {
            _cachedToggleKey = _toggleKey.Value;
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

            if (_cachedToggleKey != KeyCode.None && UnityEngine.Input.GetKeyDown(_cachedToggleKey))
            {
                HandleToggle();
            }
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
            if (_toggleKey != null)
            {
                _toggleKey.SettingChanged -= HandleSettingChanged;
            }
        }
    }
}
