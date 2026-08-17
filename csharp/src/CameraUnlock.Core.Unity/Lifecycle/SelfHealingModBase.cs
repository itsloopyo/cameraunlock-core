using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Lifecycle
{
    /// <summary>
    /// Base class for mods that automatically recreate themselves when destroyed.
    /// This pattern survives scene changes and other Unity lifecycle events.
    ///
    /// Usage:
    /// 1. Create a derived class that overrides Initialize() and OnModDestroyed()
    /// 2. Call CreateMod{T}() from your entry point
    /// </summary>
    public abstract class SelfHealingModBase : MonoBehaviour
    {
        private static GameObject _modObject;
        private static GameObject _recreatorObject;
        private static SelfHealingModBase _instance;
        private static bool _recreateScheduled;
        private static Type _modType;

        /// <summary>
        /// Gets the current mod instance.
        /// </summary>
        public static SelfHealingModBase Instance
        {
            get { return _instance; }
        }

        /// <summary>
        /// Called once when the mod is first created.
        /// Override to perform one-time initialization.
        /// </summary>
        protected abstract void Initialize();

        /// <summary>
        /// Called when the mod is being destroyed.
        /// Override to clean up resources.
        /// </summary>
        protected virtual void OnModDestroyed() { }

        /// <summary>
        /// Creates the mod if it doesn't exist.
        /// Call this from your entry point (e.g., patched game code).
        /// </summary>
        /// <typeparam name="T">The concrete mod type.</typeparam>
        /// <param name="name">Name for the mod GameObject.</param>
        /// <returns>The mod instance.</returns>
        public static T CreateMod<T>(string name = "HeadTrackingMod") where T : SelfHealingModBase
        {
            if (_instance != null)
            {
                return (T)_instance;
            }

            _modType = typeof(T);

            // Reuse the host when only the component was destroyed. Allocating unconditionally
            // left the old GameObject behind with its own recreator still running Update, so a
            // session accumulated one leaked per-frame callback per heal.
            if (_modObject == null)
            {
                _modObject = new GameObject(name);
                UnityEngine.Object.DontDestroyOnLoad(_modObject);
            }

            _instance = _modObject.AddComponent<T>();

            // The recreator lives on its OWN object, not the mod's. Hosted on the mod object it
            // died with it, and a destroyed mod GameObject then left _recreateScheduled true
            // with nothing left to act on it - head tracking permanently dead, silently.
            if (_recreatorObject == null)
            {
                _recreatorObject = new GameObject(name + "Recreator");
                UnityEngine.Object.DontDestroyOnLoad(_recreatorObject);
                _recreatorObject.AddComponent<ModRecreator>().SetModType<T>(name);
            }

            return (T)_instance;
        }

        /// <summary>
        /// Unity Awake callback.
        /// </summary>
        protected virtual void Awake()
        {
            Initialize();
        }

        /// <summary>
        /// Unity OnDestroy callback.
        /// </summary>
        protected virtual void OnDestroy()
        {
            OnModDestroyed();
            _instance = null;
            _recreateScheduled = true;

            // _modObject is deliberately left set. If only this component was destroyed the
            // host is still alive and gets reused; if the host itself went, Unity's fake null
            // makes the reuse check in CreateMod see it as gone and allocate a fresh one.
        }

        /// <summary>
        /// Schedules the mod to be recreated.
        /// Called automatically when the mod is destroyed.
        /// </summary>
        public static void ScheduleRecreate()
        {
            _recreateScheduled = true;
        }

        /// <summary>
        /// Checks if recreate is scheduled and performs it.
        /// Called by ModRecreator each frame.
        /// </summary>
        internal static void CheckRecreate<T>(string name) where T : SelfHealingModBase
        {
            if (_recreateScheduled && _instance == null)
            {
                _recreateScheduled = false;
                CreateMod<T>(name);
            }
        }

        /// <summary>
        /// Helper component that monitors for mod destruction and triggers recreation.
        /// </summary>
        private class ModRecreator : MonoBehaviour
        {
            private Action _checkRecreateAction;

            public void SetModType<T>(string name) where T : SelfHealingModBase
            {
                // Cache the delegate once to avoid per-frame reflection
                _checkRecreateAction = () => CheckRecreate<T>(name);
            }

            private void Update()
            {
                if (_instance == null && _checkRecreateAction != null)
                {
                    _checkRecreateAction();
                }
            }
        }
    }
}
