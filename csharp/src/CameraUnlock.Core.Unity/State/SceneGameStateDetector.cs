using System;
using CameraUnlock.Core.State;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace CameraUnlock.Core.Unity.State
{
    /// <summary>
    /// Game state detector for Unity games where menus, loading and customization screens
    /// are separate scenes. Classifies the active scene by case-insensitive substring
    /// patterns and treats Time.timeScale == 0 as paused.
    ///
    /// Usage:
    /// 1. Construct (optionally with extra game-specific patterns).
    /// 2. Poll IsInGameplay each frame; subscribe to GameplayStateChanged for scene
    ///    transitions (e.g. to invalidate camera caches).
    /// 3. Dispose on plugin shutdown to unsubscribe from sceneLoaded.
    /// </summary>
    public sealed class SceneGameStateDetector : IGameStateDetector
    {
        /// <summary>
        /// Scene-name substrings (lowercase) that mark non-gameplay scenes in most Unity games.
        /// </summary>
        public static readonly string[] DefaultNonGameplayPatterns = new string[]
        {
            "mainmenu",
            "menu",
            "splashscreen",
            "splash",
            "loading",
            "charactercustomization",
            "charactercreation",
            "customization",
            "intro",
            "credits"
        };

        private readonly string[] _nonGameplayPatterns;
        private readonly Action<string> _log;
        private string _currentSceneName;
        private string _currentSceneNameLower;
        private bool _isGameplayScene;
        private bool _isPaused;
        private bool _disposed;

        /// <summary>Suppress tracking while a non-gameplay scene is active.</summary>
        public bool DisableInMenuScenes { get; set; }

        /// <summary>Suppress tracking while the game is paused (Time.timeScale == 0).</summary>
        public bool DisableWhenPaused { get; set; }

        /// <summary>Fired when a scene load flips the gameplay/non-gameplay classification.</summary>
        public event Action<bool> GameplayStateChanged;

        /// <summary>Fired when the paused state changes. Pause is polled via IsInGameplay.</summary>
        public event Action<bool> PauseStateChanged;

        /// <summary>Whether the active scene is classified as gameplay.</summary>
        public bool IsGameplayScene => _isGameplayScene;

        /// <summary>Whether the game is paused (Time.timeScale == 0) as of the last check.</summary>
        public bool IsPaused => _isPaused;

        /// <summary>Name of the active scene.</summary>
        public string CurrentSceneName => _currentSceneName;

        /// <param name="nonGameplayPatterns">
        /// Lowercase scene-name substrings marking non-gameplay scenes.
        /// Null uses <see cref="DefaultNonGameplayPatterns"/>.
        /// </param>
        /// <param name="log">Optional logging callback for scene transition messages.</param>
        public SceneGameStateDetector(string[] nonGameplayPatterns = null, Action<string> log = null)
        {
            _nonGameplayPatterns = nonGameplayPatterns ?? DefaultNonGameplayPatterns;
            _log = log;
            DisableInMenuScenes = true;
            DisableWhenPaused = true;

            UpdateSceneCache(SceneManager.GetActiveScene().name);
            _isGameplayScene = ClassifyScene();

            SceneManager.sceneLoaded += OnSceneLoaded;

            _log?.Invoke($"SceneGameStateDetector initialized. Current scene: {_currentSceneName}, IsGameplay: {_isGameplayScene}");
        }

        public bool IsInGameplay
        {
            get
            {
                if (_disposed) return false;

                UpdatePauseState();

                if (DisableInMenuScenes && !_isGameplayScene)
                {
                    return false;
                }

                if (DisableWhenPaused && _isPaused)
                {
                    return false;
                }

                return true;
            }
        }

        public void InvalidateCache()
        {
            UpdateSceneCache(SceneManager.GetActiveScene().name);
            _isGameplayScene = ClassifyScene();
        }

        private void UpdateSceneCache(string sceneName)
        {
            _currentSceneName = sceneName;
            _currentSceneNameLower = sceneName?.ToLowerInvariant();
        }

        private void OnSceneLoaded(Scene scene, LoadSceneMode mode)
        {
            string previousScene = _currentSceneName;
            bool wasGameplay = _isGameplayScene;

            UpdateSceneCache(scene.name);
            _isGameplayScene = ClassifyScene();

            _log?.Invoke($"Scene loaded: {_currentSceneName} (from {previousScene}), IsGameplay: {_isGameplayScene}");

            if (wasGameplay != _isGameplayScene)
            {
                GameplayStateChanged?.Invoke(_isGameplayScene);
            }
        }

        private void UpdatePauseState()
        {
            bool isPaused = Time.timeScale == 0f;

            if (isPaused != _isPaused)
            {
                _isPaused = isPaused;
                PauseStateChanged?.Invoke(_isPaused);
            }
        }

        private bool ClassifyScene()
        {
            if (string.IsNullOrEmpty(_currentSceneNameLower))
            {
                return false;
            }

            foreach (string pattern in _nonGameplayPatterns)
            {
                if (_currentSceneNameLower.Contains(pattern))
                {
                    return false;
                }
            }

            return true;
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            SceneManager.sceneLoaded -= OnSceneLoaded;
        }
    }
}
