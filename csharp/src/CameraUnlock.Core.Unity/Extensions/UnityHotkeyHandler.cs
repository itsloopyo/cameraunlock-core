using System;
using CameraUnlock.Core.Input;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Unity-specific hotkey handler that wraps CameraUnlock.Core.Input.HotkeyHandler.
    /// Pre-configured with Unity's Input.GetKeyDown and Time.time.
    /// Default hotkeys: Home = Recenter, End = Toggle.
    /// </summary>
    public sealed class UnityHotkeyHandler
    {
        private readonly HotkeyHandler _handler;

        /// <summary>
        /// Whether tracking is currently enabled.
        /// </summary>
        public bool IsEnabled
        {
            get { return _handler.IsEnabled; }
            set { _handler.IsEnabled = value; }
        }

        /// <summary>
        /// Number of times toggle has been pressed.
        /// </summary>
        public int ToggleCount { get { return _handler.ToggleCount; } }

        /// <summary>
        /// Number of times recenter has been pressed.
        /// </summary>
        public int RecenterCount { get { return _handler.RecenterCount; } }

        /// <summary>
        /// Fired when the toggle key is pressed. Parameter is the new enabled state.
        /// Uses custom delegate type to avoid Unity 2018 Mono cross-assembly issues.
        /// </summary>
        public event ToggleEventHandler OnToggled
        {
            add { _handler.OnToggled += value; }
            remove { _handler.OnToggled -= value; }
        }

        /// <summary>
        /// Fired when the toggle key is pressed.
        /// </summary>
        [Obsolete("Use OnToggled or IHotkeyListener instead")]
        public event RecenterEventHandler OnToggle
        {
            #pragma warning disable 618
            add { _handler.OnToggle += value; }
            remove { _handler.OnToggle -= value; }
            #pragma warning restore 618
        }

        /// <summary>
        /// Fired when the recenter key is pressed.
        /// Uses custom delegate type to avoid Unity 2018 Mono cross-assembly issues.
        /// </summary>
        public event RecenterEventHandler OnRecenter
        {
            add { _handler.OnRecenter += value; }
            remove { _handler.OnRecenter -= value; }
        }

        /// <summary>
        /// Creates a Unity hotkey handler with default keys (Home = Recenter, End = Toggle).
        /// </summary>
        /// <param name="cooldownSeconds">Minimum time between key activations (default 0.3s).</param>
        public UnityHotkeyHandler(float cooldownSeconds = 0.3f)
            : this(KeyCode.End, KeyCode.Home, null, cooldownSeconds)
        {
        }

        /// <summary>
        /// Creates a Unity hotkey handler with custom keys (event-based).
        /// </summary>
        /// <param name="toggleKey">Key to toggle head tracking on/off.</param>
        /// <param name="recenterKey">Key to recenter head tracking.</param>
        /// <param name="cooldownSeconds">Minimum time between key activations (default 0.3s).</param>
        public UnityHotkeyHandler(KeyCode toggleKey, KeyCode recenterKey, float cooldownSeconds = 0.3f)
            : this(toggleKey, recenterKey, null, cooldownSeconds)
        {
        }

        /// <summary>
        /// Creates a Unity hotkey handler with interface-based callbacks (Unity 2018 compatible).
        /// </summary>
        /// <param name="toggleKey">Key to toggle head tracking on/off.</param>
        /// <param name="recenterKey">Key to recenter head tracking.</param>
        /// <param name="listener">Interface to receive callbacks (recommended for Unity 2018).</param>
        /// <param name="cooldownSeconds">Minimum time between key activations (default 0.3s).</param>
        public UnityHotkeyHandler(KeyCode toggleKey, KeyCode recenterKey, IHotkeyListener listener, float cooldownSeconds = 0.3f)
        {
            _handler = new HotkeyHandler(
                keyCode => UnityEngine.Input.GetKeyDown((KeyCode)keyCode),
                IsTextInputActive,
                listener,
                cooldownSeconds);

            _handler.SetToggleKey((int)toggleKey);
            _handler.SetRecenterKey((int)recenterKey);
        }

        /// <summary>
        /// Sets the toggle tracking hotkey.
        /// </summary>
        public void SetToggleKey(KeyCode key)
        {
            _handler.SetToggleKey((int)key);
        }

        /// <summary>
        /// Sets the recenter hotkey.
        /// </summary>
        public void SetRecenterKey(KeyCode key)
        {
            _handler.SetRecenterKey((int)key);
        }

        /// <summary>
        /// Checks for hotkey input. Call this every frame from Update().
        /// </summary>
        public void Update()
        {
            // Unscaled: Time.time stops advancing at timeScale 0, so the hotkey cooldown
            // never expires while the game is paused and every press after the first is
            // rejected - in menus, which is exactly where users go to toggle the mod.
            _handler.Update(Time.unscaledTime);
        }

        /// <summary>
        /// Toggles the enabled state and fires events.
        /// </summary>
        /// <returns>The new enabled state.</returns>
        public bool Toggle()
        {
            return _handler.Toggle();
        }

        /// <summary>
        /// Resets the toggle and recenter counts.
        /// </summary>
        public void ResetCounts()
        {
            _handler.ResetCounts();
        }

        /// <summary>
        /// Checks if text input is currently active in Unity's GUI system.
        /// </summary>
        private static bool IsTextInputActive()
        {
            // GUIUtility.keyboardControl > 0 means a text field or similar control has focus
            return GUIUtility.keyboardControl > 0;
        }
    }
}
