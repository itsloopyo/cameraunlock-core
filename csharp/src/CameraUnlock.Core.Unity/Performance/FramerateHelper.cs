using UnityEngine;

namespace CameraUnlock.Core.Unity.Performance
{
    /// <summary>
    /// Helper for managing framerate settings in Unity games.
    /// Many Unity games have framerate caps that negatively impact head tracking.
    /// </summary>
    public static class FramerateHelper
    {
        private static int _originalTargetFrameRate = -1;
        private static int _originalVSyncCount = -1;
        private static bool _settingsSaved;

        /// <summary>
        /// Gets whether settings have been modified.
        /// </summary>
        public static bool IsModified
        {
            get { return _settingsSaved; }
        }

        /// <summary>
        /// Gets the current target framerate.
        /// </summary>
        public static int CurrentTargetFrameRate
        {
            get { return Application.targetFrameRate; }
        }

        /// <summary>
        /// Gets the current VSync count.
        /// </summary>
        public static int CurrentVSyncCount
        {
            get { return QualitySettings.vSyncCount; }
        }

        /// <summary>
        /// Unlocks the framerate by setting targetFrameRate to -1 and disabling VSync.
        /// Saves original settings for restoration.
        /// </summary>
        public static void UnlockFramerate()
        {
            SaveOriginalSettings();
            Application.targetFrameRate = -1;
            QualitySettings.vSyncCount = 0;
        }

        /// <summary>
        /// Sets a specific target framerate and disables VSync.
        /// Saves original settings for restoration.
        /// </summary>
        /// <param name="targetFps">Target framerate (use -1 for unlimited).</param>
        public static void SetTargetFramerate(int targetFps)
        {
            SaveOriginalSettings();
            Application.targetFrameRate = targetFps;
            QualitySettings.vSyncCount = 0;
        }

        /// <summary>
        /// Sets a specific target framerate with optional VSync control.
        /// Saves original settings for restoration.
        /// </summary>
        /// <param name="targetFps">Target framerate (use -1 for unlimited).</param>
        /// <param name="vSyncCount">VSync count (0 = off, 1 = every frame, 2 = every other frame).</param>
        public static void SetTargetFramerate(int targetFps, int vSyncCount)
        {
            SaveOriginalSettings();
            Application.targetFrameRate = targetFps;
            QualitySettings.vSyncCount = vSyncCount;
        }

        /// <summary>
        /// Restores the original framerate settings that were saved before modifications.
        /// </summary>
        public static void RestoreOriginalSettings()
        {
            if (!_settingsSaved)
            {
                return;
            }

            Application.targetFrameRate = _originalTargetFrameRate;
            QualitySettings.vSyncCount = _originalVSyncCount;
            _settingsSaved = false;
        }

        private static void SaveOriginalSettings()
        {
            if (_settingsSaved)
            {
                return;
            }

            _originalTargetFrameRate = Application.targetFrameRate;
            _originalVSyncCount = QualitySettings.vSyncCount;
            _settingsSaved = true;
        }
    }
}
