#nullable enable
using System;
using Il2CppInterop.Runtime;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.Video;

namespace CameraUnlock.Core.Unity.Il2Cpp
{
    /// <summary>
    /// Dev-only: kills splash/intro VideoPlayers so the game boots straight to the menu.
    /// Polls for scene changes instead of subscribing to SceneManager.sceneLoaded - managed
    /// delegates cannot be subscribed to Il2Cpp events from injected types.
    ///
    /// Register with ClassInjector.RegisterTypeInIl2Cpp and AddComponent on a
    /// DontDestroyOnLoad host object. Set <see cref="Log"/> before adding the component.
    /// </summary>
    public class FastBootBehaviour : MonoBehaviour
    {
        public static Action<string>? Log { get; set; }

        public FastBootBehaviour(IntPtr ptr) : base(ptr) { }

        private int _lastSceneHandle;
        private bool _hasLastScene;

        private void Update()
        {
            if (Time.frameCount % 30 != 0) return;

            Scene scene = SceneManager.GetActiveScene();
            if (_hasLastScene && scene.handle == _lastSceneHandle) return;
            _lastSceneHandle = scene.handle;
            _hasLastScene = true;

            KillAllVideoPlayers(scene.name);
        }

        private static void KillAllVideoPlayers(string sceneLabel)
        {
            var objects = Resources.FindObjectsOfTypeAll(Il2CppType.Of<VideoPlayer>());
            if (objects == null || objects.Length == 0) return;

            int killed = 0;
            for (int i = 0; i < objects.Length; i++)
            {
                var vp = objects[i] != null ? objects[i].TryCast<VideoPlayer>() : null;
                if (vp == null) continue;
                vp.playOnAwake = false;
                if (vp.isPlaying) vp.Stop();
                vp.enabled = false;
                killed++;
            }
            if (killed > 0)
            {
                Log?.Invoke($"[FastBoot] {sceneLabel}: disabled {killed} VideoPlayer(s).");
            }
        }
    }
}
