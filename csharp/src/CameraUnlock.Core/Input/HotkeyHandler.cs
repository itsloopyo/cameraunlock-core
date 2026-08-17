using System;

namespace CameraUnlock.Core.Input
{
    /// <summary>
    /// Interface for receiving hotkey callbacks.
    /// Use this instead of events for Unity 2018 Mono compatibility.
    /// </summary>
    public interface IHotkeyListener
    {
        /// <summary>
        /// Called when the toggle key is pressed.
        /// </summary>
        /// <param name="enabled">The new enabled state.</param>
        void OnHotkeyToggle(bool enabled);

        /// <summary>
        /// Called when the recenter key is pressed.
        /// </summary>
        void OnHotkeyRecenter();
    }

    /// <summary>
    /// Delegate for checking if a key is pressed this frame.
    /// </summary>
    /// <param name="keyCode">The key code to check (framework-specific).</param>
    /// <returns>True if the key was pressed down this frame.</returns>
    public delegate bool KeyDownCheck(int keyCode);

    /// <summary>
    /// Delegate for checking if text input is currently active (e.g., typing in a text field).
    /// </summary>
    /// <returns>True if text input is active and hotkeys should be ignored.</returns>
    public delegate bool TextInputActiveCheck();

    /// <summary>
    /// Delegate for toggle events. Defined here to avoid cross-assembly System.Action issues on Unity 2018 Mono.
    /// </summary>
    /// <param name="enabled">The new enabled state.</param>
    public delegate void ToggleEventHandler(bool enabled);

    /// <summary>
    /// Delegate for recenter events. Defined here to avoid cross-assembly System.Action issues on Unity 2018 Mono.
    /// </summary>
    public delegate void RecenterEventHandler();

    /// <summary>
    /// Framework-agnostic hotkey handler with cooldown and text input blocking.
    /// Supports both event-based (for newer Unity) and interface-based (for Unity 2018 Mono) callbacks.
    /// </summary>
    public sealed class HotkeyHandler
    {
        private readonly KeyDownCheck _keyDownCheck;
#if NULLABLE_ENABLED
        private readonly TextInputActiveCheck? _textInputCheck;
        private readonly IHotkeyListener? _listener;
#else
        private readonly TextInputActiveCheck _textInputCheck;
        private readonly IHotkeyListener _listener;
#endif
        private readonly float _cooldownSeconds;

        private int _toggleKeyCode;
        private int _recenterKeyCode;
        private float _lastToggleTime;
        private float _lastRecenterTime;

        // Tracking state
        private bool _isEnabled = true;
        private int _toggleCount;
        private int _recenterCount;

        /// <summary>
        /// Whether tracking is currently enabled.
        /// </summary>
        public bool IsEnabled
        {
            get { return _isEnabled; }
            set { _isEnabled = value; }
        }

        /// <summary>
        /// Number of times toggle has been pressed.
        /// </summary>
        public int ToggleCount { get { return _toggleCount; } }

        /// <summary>
        /// Number of times recenter has been pressed.
        /// </summary>
        public int RecenterCount { get { return _recenterCount; } }

        /// <summary>
        /// Fired when the toggle key is pressed. Parameter is the new enabled state.
        /// Uses custom delegate type to avoid Unity 2018 Mono cross-assembly issues.
        /// </summary>
#if NULLABLE_ENABLED
        public event ToggleEventHandler? OnToggled;
#else
        public event ToggleEventHandler OnToggled;
#endif

        /// <summary>
        /// Fired when the toggle key is pressed.
        /// </summary>
        [Obsolete("Use OnToggled or IHotkeyListener instead")]
#if NULLABLE_ENABLED
        public event RecenterEventHandler? OnToggle;
#else
        public event RecenterEventHandler OnToggle;
#endif

        /// <summary>
        /// Fired when the recenter key is pressed.
        /// Uses custom delegate type to avoid Unity 2018 Mono cross-assembly issues.
        /// </summary>
#if NULLABLE_ENABLED
        public event RecenterEventHandler? OnRecenter;
#else
        public event RecenterEventHandler OnRecenter;
#endif

        /// <summary>
        /// Creates a new hotkey handler with event-based callbacks.
        /// </summary>
        /// <param name="keyDownCheck">Function to check if a key was pressed this frame.</param>
        /// <param name="textInputCheck">Function to check if text input is active (can be null to disable check).</param>
        /// <param name="cooldownSeconds">Minimum time between key activations (default 0.3s).</param>
#if NULLABLE_ENABLED
        public HotkeyHandler(KeyDownCheck keyDownCheck, TextInputActiveCheck? textInputCheck = null, float cooldownSeconds = 0.3f)
#else
        public HotkeyHandler(KeyDownCheck keyDownCheck, TextInputActiveCheck textInputCheck = null, float cooldownSeconds = 0.3f)
#endif
            : this(keyDownCheck, textInputCheck, null, cooldownSeconds)
        {
        }

        /// <summary>
        /// Creates a new hotkey handler with interface-based callbacks (Unity 2018 compatible).
        /// </summary>
        /// <param name="keyDownCheck">Function to check if a key was pressed this frame.</param>
        /// <param name="textInputCheck">Function to check if text input is active (can be null to disable check).</param>
        /// <param name="listener">Interface to receive callbacks (recommended for Unity 2018).</param>
        /// <param name="cooldownSeconds">Minimum time between key activations (default 0.3s).</param>
#if NULLABLE_ENABLED
        public HotkeyHandler(KeyDownCheck keyDownCheck, TextInputActiveCheck? textInputCheck, IHotkeyListener? listener, float cooldownSeconds = 0.3f)
#else
        public HotkeyHandler(KeyDownCheck keyDownCheck, TextInputActiveCheck textInputCheck, IHotkeyListener listener, float cooldownSeconds = 0.3f)
#endif
        {
            _keyDownCheck = keyDownCheck;
            _textInputCheck = textInputCheck;
            _listener = listener;
            _cooldownSeconds = cooldownSeconds;
        }

        /// <summary>
        /// Sets the toggle tracking hotkey.
        /// </summary>
        /// <param name="keyCode">Framework-specific key code (e.g., Unity KeyCode cast to int).</param>
        public void SetToggleKey(int keyCode)
        {
            _toggleKeyCode = keyCode;
        }

        /// <summary>
        /// Sets the recenter hotkey.
        /// </summary>
        /// <param name="keyCode">Framework-specific key code.</param>
        public void SetRecenterKey(int keyCode)
        {
            _recenterKeyCode = keyCode;
        }

        /// <summary>
        /// Checks for hotkey input. Call this every frame from Update().
        /// </summary>
        /// <param name="currentTime">Current time in seconds (e.g., Time.time in Unity).</param>
        public void Update(float currentTime)
        {
            // Skip if text input is active
            if (_textInputCheck != null && _textInputCheck())
            {
                return;
            }

            // Check toggle key
            if (_toggleKeyCode != 0 && _keyDownCheck(_toggleKeyCode))
            {
                // A negative delta means the caller's clock went backwards (a mod passing
                // Time.timeSinceLevelLoad, which restarts on every load). Treating that as
                // "cooldown not elapsed" left the hotkey dead for as long as the old
                // timestamp was large - minutes of play with no way to diagnose it.
                float sinceToggle = currentTime - _lastToggleTime;
                if (sinceToggle < 0f || sinceToggle >= _cooldownSeconds)
                {
                    _lastToggleTime = currentTime;
                    _toggleCount++;
                    _isEnabled = !_isEnabled;

                    // Notify via interface (Unity 2018 compatible)
                    if (_listener != null)
                    {
                        _listener.OnHotkeyToggle(_isEnabled);
                    }

                    // Fire event
                    if (OnToggled != null) OnToggled(_isEnabled);

                    // Fire legacy event for compatibility
                    #pragma warning disable 618
                    if (OnToggle != null) OnToggle();
                    #pragma warning restore 618
                }
            }

            // Check recenter key
            if (_recenterKeyCode != 0 && _keyDownCheck(_recenterKeyCode))
            {
                float sinceRecenter = currentTime - _lastRecenterTime;
                if (sinceRecenter < 0f || sinceRecenter >= _cooldownSeconds)
                {
                    _lastRecenterTime = currentTime;
                    _recenterCount++;

                    // Notify via interface (Unity 2018 compatible)
                    if (_listener != null)
                    {
                        _listener.OnHotkeyRecenter();
                    }

                    // Fire event
                    if (OnRecenter != null) OnRecenter();
                }
            }
        }

        /// <summary>
        /// Toggles the enabled state and fires events.
        /// </summary>
        /// <returns>The new enabled state.</returns>
        public bool Toggle()
        {
            _toggleCount++;
            _isEnabled = !_isEnabled;

            if (_listener != null)
            {
                _listener.OnHotkeyToggle(_isEnabled);
            }

            if (OnToggled != null) OnToggled(_isEnabled);
            #pragma warning disable 618
            if (OnToggle != null) OnToggle();
            #pragma warning restore 618

            return _isEnabled;
        }

        /// <summary>
        /// Resets the toggle and recenter counts.
        /// </summary>
        public void ResetCounts()
        {
            _toggleCount = 0;
            _recenterCount = 0;
        }
    }

    /// <summary>
    /// Common key codes matching Unity KeyCode enum for convenience.
    /// Use these or cast your framework key codes to int.
    /// </summary>
    public static class CommonKeyCodes
    {
        public const int None = 0;
        public const int Home = 278;
        public const int End = 279;
        public const int F1 = 282;
        public const int F2 = 283;
        public const int F3 = 284;
        public const int F4 = 285;
        public const int F5 = 286;
        public const int F6 = 287;
        public const int F7 = 288;
        public const int F8 = 289;
        public const int F9 = 290;
        public const int F10 = 291;
        public const int F11 = 292;
        public const int F12 = 293;
    }
}
