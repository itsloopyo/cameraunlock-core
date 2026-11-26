using System;
using System.Threading;

namespace CameraUnlock.Core.State
{
    /// <summary>
    /// Global state management for head tracking enabled/disabled state.
    /// Lock-free implementation using Interlocked for optimal per-frame performance.
    /// Thread-safe for concurrent access from multiple threads.
    /// </summary>
    public static class TrackingState
    {
        // Using int with Interlocked for atomic operations (0 = disabled, 1 = enabled)
        private static int _enabledState;

        /// <summary>
        /// Event fired when the tracking state changes.
        /// The bool parameter indicates the new enabled state.
        /// </summary>
#if NULLABLE_ENABLED
        public static event Action<bool>? OnStateChanged;
#else
        public static event Action<bool> OnStateChanged;
#endif

        /// <summary>
        /// Whether head tracking is currently enabled.
        /// Lock-free read using volatile semantics via Interlocked.
        /// </summary>
        public static bool IsEnabled => Interlocked.CompareExchange(ref _enabledState, 0, 0) == 1;

        /// <summary>
        /// Initializes the state from configuration.
        /// Called from plugin Awake() after config is loaded.
        /// Does not fire the OnStateChanged event.
        /// </summary>
        /// <param name="enabled">Initial enabled state from config</param>
        public static void Initialize(bool enabled)
        {
            Interlocked.Exchange(ref _enabledState, enabled ? 1 : 0);
        }

        /// <summary>
        /// Toggles the enabled state and returns the new value.
        /// Uses compare-exchange loop for atomic toggle.
        /// Fires OnStateChanged event with the new state.
        /// </summary>
        /// <returns>The new enabled state after toggling</returns>
        public static bool Toggle()
        {
            int original;
            int newValue;
            do
            {
                original = Interlocked.CompareExchange(ref _enabledState, 0, 0);
                newValue = original == 0 ? 1 : 0;
            } while (Interlocked.CompareExchange(ref _enabledState, newValue, original) != original);

            bool newState = newValue == 1;
            OnStateChanged?.Invoke(newState);
            return newState;
        }

        /// <summary>
        /// Explicitly enables head tracking.
        /// Fires OnStateChanged event if state actually changed.
        /// </summary>
        public static void Enable()
        {
            int previous = Interlocked.Exchange(ref _enabledState, 1);
            if (previous == 0)
            {
                OnStateChanged?.Invoke(true);
            }
        }

        /// <summary>
        /// Explicitly disables head tracking.
        /// Fires OnStateChanged event if state actually changed.
        /// </summary>
        public static void Disable()
        {
            int previous = Interlocked.Exchange(ref _enabledState, 0);
            if (previous == 1)
            {
                OnStateChanged?.Invoke(false);
            }
        }

        /// <summary>
        /// Sets the enabled state explicitly.
        /// Fires OnStateChanged event if state actually changed.
        /// </summary>
        /// <param name="enabled">The desired enabled state</param>
        public static void SetEnabled(bool enabled)
        {
            if (enabled)
            {
                Enable();
            }
            else
            {
                Disable();
            }
        }
    }
}
