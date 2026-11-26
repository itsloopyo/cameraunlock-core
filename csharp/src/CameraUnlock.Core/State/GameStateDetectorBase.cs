using System;
using System.Collections.Generic;

namespace CameraUnlock.Core.State
{
    /// <summary>
    /// Base class for game state detection with caching and throttling.
    /// Subclasses implement the actual detection logic for their specific framework.
    /// </summary>
    public abstract class GameStateDetectorBase : IGameStateDetector
    {
        /// <summary>Default interval between state checks.</summary>
        public const float DefaultCheckIntervalSeconds = 0.1f;

        private readonly float _checkIntervalSeconds;
        private readonly GetCurrentTime _getTime;
        private readonly HashSet<string> _menuSceneNames;

        private bool _cachedIsInGameplay;
        private float _lastCheckTime;
        private bool _disposed;
        private string _cachedSceneName;
        private bool _cachedIsMenuScene;

        /// <summary>
        /// Creates a new game state detector.
        /// </summary>
        /// <param name="getTime">Function to get current time for throttling.</param>
        /// <param name="checkIntervalSeconds">Minimum seconds between checks.</param>
        protected GameStateDetectorBase(GetCurrentTime getTime, float checkIntervalSeconds = DefaultCheckIntervalSeconds)
        {
            _getTime = getTime ?? throw new ArgumentNullException(nameof(getTime));
            _checkIntervalSeconds = checkIntervalSeconds;
            _menuSceneNames = CommonMenuScenes.CreateDefaultSet();
            _cachedSceneName = string.Empty;
        }

        /// <summary>
        /// Gets whether the player is currently in active gameplay.
        /// Uses caching to reduce overhead.
        /// </summary>
        public bool IsInGameplay
        {
            get
            {
                if (_disposed) return false;

                float currentTime = _getTime();
                if (currentTime - _lastCheckTime < _checkIntervalSeconds)
                {
                    return _cachedIsInGameplay;
                }

                _lastCheckTime = currentTime;
                _cachedIsInGameplay = CheckGameplayState();
                return _cachedIsInGameplay;
            }
        }

        /// <summary>
        /// Forces a cache invalidation.
        /// </summary>
        public void InvalidateCache()
        {
            _lastCheckTime = 0f;
        }

        /// <summary>
        /// Performs the actual gameplay state check.
        /// Override to add additional game-specific checks.
        /// </summary>
        protected virtual bool CheckGameplayState()
        {
            // Check scene name first
            string currentScene = GetCurrentSceneName();
            if (!string.Equals(_cachedSceneName, currentScene, StringComparison.Ordinal))
            {
                _cachedSceneName = currentScene ?? string.Empty;
                _cachedIsMenuScene = !string.IsNullOrEmpty(_cachedSceneName) &&
                                     _menuSceneNames.Contains(_cachedSceneName);
            }

            if (_cachedIsMenuScene)
            {
                return false;
            }

            if (IsGamePaused())
            {
                return false;
            }

            // Check cursor state (menus typically show cursor)
            if (IsCursorVisible())
            {
                return false;
            }

            if (IsMenuVisible())
            {
                return false;
            }

            // Check if inventory/UI overlay is open
            if (IsInventoryOpen())
            {
                return false;
            }

            // Check if text input is active (chat, console, etc.)
            if (IsTextInputActive())
            {
                return false;
            }

            if (IsPlayerDead())
            {
                return false;
            }

            if (!HasCameraControl())
            {
                return false;
            }

            return true;
        }

        /// <summary>
        /// Gets the current scene name. Override for framework-specific implementation.
        /// </summary>
        protected abstract string GetCurrentSceneName();

        /// <summary>
        /// Checks if the game is paused. Override for framework-specific implementation.
        /// Default returns false.
        /// </summary>
        protected virtual bool IsGamePaused() => false;

        /// <summary>
        /// Checks if the cursor is visible (indicating menu/UI state).
        /// Override for framework-specific implementation.
        /// Default returns false.
        /// </summary>
        protected virtual bool IsCursorVisible() => false;

        /// <summary>
        /// Checks if a menu is visible (main menu, pause menu, etc.).
        /// Override to implement game-specific menu detection.
        /// Default returns false.
        /// </summary>
        protected virtual bool IsMenuVisible() => false;

        /// <summary>
        /// Checks if an inventory or UI overlay is open.
        /// Override to implement game-specific inventory detection.
        /// Default returns false.
        /// </summary>
        protected virtual bool IsInventoryOpen() => false;

        /// <summary>
        /// Checks if text input is active (chat, console, text fields, etc.).
        /// Override to implement game-specific text input detection.
        /// Default returns false.
        /// </summary>
        protected virtual bool IsTextInputActive() => false;

        /// <summary>
        /// Checks if the player is dead.
        /// Override to implement game-specific death detection.
        /// Default returns false.
        /// </summary>
        protected virtual bool IsPlayerDead() => false;

        /// <summary>
        /// Checks if the player has camera control.
        /// Override to implement game-specific checks (e.g., cutscenes without camera control).
        /// Default returns true.
        /// </summary>
        protected virtual bool HasCameraControl() => true;

        /// <summary>
        /// Adds a scene name to the list of known menu scenes.
        /// </summary>
        public void AddMenuScene(string sceneName)
        {
            if (string.IsNullOrEmpty(sceneName))
                throw new ArgumentException("Scene name cannot be null or empty", nameof(sceneName));

            _menuSceneNames.Add(sceneName);

            if (string.Equals(_cachedSceneName, sceneName, StringComparison.OrdinalIgnoreCase))
            {
                _cachedIsMenuScene = true;
            }
        }

        /// <summary>
        /// Removes a scene name from the menu scene list.
        /// </summary>
        public bool RemoveMenuScene(string sceneName)
        {
            if (string.IsNullOrEmpty(sceneName))
                throw new ArgumentException("Scene name cannot be null or empty", nameof(sceneName));

            bool removed = _menuSceneNames.Remove(sceneName);

            if (removed && string.Equals(_cachedSceneName, sceneName, StringComparison.OrdinalIgnoreCase))
            {
                _cachedIsMenuScene = false;
            }

            return removed;
        }

        /// <summary>
        /// Clears all menu scene names.
        /// </summary>
        public void ClearMenuScenes()
        {
            _menuSceneNames.Clear();
            _cachedIsMenuScene = false;
        }

        /// <summary>
        /// Disposes of the detector.
        /// </summary>
        public void Dispose()
        {
            if (!_disposed)
            {
                _disposed = true;
                OnDispose();
            }
        }

        /// <summary>
        /// Called when disposing. Override to clean up resources.
        /// </summary>
        protected virtual void OnDispose() { }
    }
}
