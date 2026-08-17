using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Manages static camera callbacks that need to survive GameObject destruction.
    /// In BepInEx plugins and similar modding scenarios, the plugin's GameObject may be
    /// destroyed during scene transitions, but the camera callbacks need to persist.
    ///
    /// This class stores callbacks as static fields and manages their registration/unregistration
    /// with the Unity Camera and Canvas systems.
    ///
    /// Usage:
    /// 1. Create instance in plugin Awake
    /// 2. Register callbacks using RegisterPreCull, RegisterPreRender, RegisterWillRenderCanvases
    /// 3. Dispose on application quit (NOT on GameObject destroy)
    ///
    /// Pass the owning UnityEngine.Object (normally the plugin MonoBehaviour whose instance
    /// method the callback is) as the optional owner argument. Without it, a callback whose
    /// target was destroyed on a scene transition throws MissingReferenceException on its first
    /// property access - and Unity ABORTS the whole multicast invocation at the throwing
    /// delegate, silently killing every other subscriber's camera hook, other mods included.
    /// </summary>
    public sealed class CameraCallbackLifecycle : IDisposable
    {
        // Static storage to survive GameObject destruction
        private static Camera.CameraCallback _staticPreCullCallback;
        private static Camera.CameraCallback _staticPreRenderCallback;
        private static Action _staticWillRenderCanvasesCallback;

        // Owner liveness. A separate flag is needed because a destroyed owner and "no owner
        // supplied" both read as null through Unity's == operator.
        private static UnityEngine.Object _preCullOwner;
        private static bool _hasPreCullOwner;
        private static UnityEngine.Object _preRenderOwner;
        private static bool _hasPreRenderOwner;
        private static UnityEngine.Object _willRenderCanvasesOwner;
        private static bool _hasWillRenderCanvasesOwner;

        // Track whether we own the current static callbacks
        private bool _ownsPreCull;
        private bool _ownsPreRender;
        private bool _ownsWillRenderCanvases;
        private bool _disposed;

        /// <summary>
        /// Returns true if this instance owns the preCull callback.
        /// </summary>
        public bool HasPreCull => _ownsPreCull && !_disposed;

        /// <summary>
        /// Returns true if this instance owns the preRender callback.
        /// </summary>
        public bool HasPreRender => _ownsPreRender && !_disposed;

        /// <summary>
        /// Returns true if this instance owns the willRenderCanvases callback.
        /// </summary>
        public bool HasWillRenderCanvases => _ownsWillRenderCanvases && !_disposed;

        /// <summary>
        /// Returns true if this instance has been disposed.
        /// </summary>
        public bool IsDisposed => _disposed;

        /// <summary>
        /// Registers a callback that will be called before each camera culls the scene.
        /// The callback receives the Camera that is about to cull.
        /// This is the ideal place to modify view matrices for head tracking.
        /// </summary>
        /// <param name="callback">The callback to register.</param>
        /// <param name="owner">Optional owning object. When supplied and later destroyed, the
        /// callback unregisters itself instead of throwing out of Unity's invocation list.</param>
        /// <exception cref="ArgumentNullException">Thrown when callback is null.</exception>
        /// <exception cref="ObjectDisposedException">Thrown when this instance is disposed.</exception>
        /// <exception cref="InvalidOperationException">Thrown when a preCull callback is already registered.</exception>
        public void RegisterPreCull(Camera.CameraCallback callback, UnityEngine.Object owner = null)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(CameraCallbackLifecycle));
            }

            if (callback == null)
            {
                throw new ArgumentNullException(nameof(callback));
            }

            if (_staticPreCullCallback != null)
            {
                throw new InvalidOperationException("A preCull callback is already registered. Call UnregisterPreCull first or Dispose the previous lifecycle.");
            }

            _staticPreCullCallback = callback;
            _preCullOwner = owner;
            _hasPreCullOwner = owner != null;
            Camera.onPreCull += OnPreCullWrapper;
            _ownsPreCull = true;
        }

        /// <summary>
        /// Unregisters the preCull callback if this instance owns it.
        /// </summary>
        public void UnregisterPreCull()
        {
            if (!_ownsPreCull)
            {
                return;
            }

            ClearPreCull();
            _ownsPreCull = false;
        }

        private static void OnPreCullWrapper(Camera cam)
        {
            if (_hasPreCullOwner && _preCullOwner == null)
            {
                ClearPreCull();
                return;
            }

            // Null-conditional for the same reason the class exists at all. Unity
            // snapshots the multicast invocation list before dispatching, so a
            // subscriber earlier in the list that calls ForceCleanupAll (public, and
            // documented for exactly the unhandled-exception case) nulls this while
            // our wrapper is still in the snapshot. Throwing here would abort the
            // rest of the invocation and take every other mod's camera hook with it.
            _staticPreCullCallback?.Invoke(cam);
        }

        private static void ClearPreCull()
        {
            if (_staticPreCullCallback == null)
            {
                return;
            }

            Camera.onPreCull -= OnPreCullWrapper;
            _staticPreCullCallback = null;
            _preCullOwner = null;
            _hasPreCullOwner = false;
        }

        /// <summary>
        /// Registers a callback that will be called before each camera renders.
        /// The callback receives the Camera that is about to render.
        /// This is called after culling and is useful for final adjustments.
        /// </summary>
        /// <param name="callback">The callback to register.</param>
        /// <param name="owner">Optional owning object. When supplied and later destroyed, the
        /// callback unregisters itself instead of throwing out of Unity's invocation list.</param>
        /// <exception cref="ArgumentNullException">Thrown when callback is null.</exception>
        /// <exception cref="ObjectDisposedException">Thrown when this instance is disposed.</exception>
        /// <exception cref="InvalidOperationException">Thrown when a preRender callback is already registered.</exception>
        public void RegisterPreRender(Camera.CameraCallback callback, UnityEngine.Object owner = null)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(CameraCallbackLifecycle));
            }

            if (callback == null)
            {
                throw new ArgumentNullException(nameof(callback));
            }

            if (_staticPreRenderCallback != null)
            {
                throw new InvalidOperationException("A preRender callback is already registered. Call UnregisterPreRender first or Dispose the previous lifecycle.");
            }

            _staticPreRenderCallback = callback;
            _preRenderOwner = owner;
            _hasPreRenderOwner = owner != null;
            Camera.onPreRender += OnPreRenderWrapper;
            _ownsPreRender = true;
        }

        /// <summary>
        /// Unregisters the preRender callback if this instance owns it.
        /// </summary>
        public void UnregisterPreRender()
        {
            if (!_ownsPreRender)
            {
                return;
            }

            ClearPreRender();
            _ownsPreRender = false;
        }

        private static void OnPreRenderWrapper(Camera cam)
        {
            if (_hasPreRenderOwner && _preRenderOwner == null)
            {
                ClearPreRender();
                return;
            }

            _staticPreRenderCallback?.Invoke(cam);
        }

        private static void ClearPreRender()
        {
            if (_staticPreRenderCallback == null)
            {
                return;
            }

            Camera.onPreRender -= OnPreRenderWrapper;
            _staticPreRenderCallback = null;
            _preRenderOwner = null;
            _hasPreRenderOwner = false;
        }

        /// <summary>
        /// Registers a callback that will be called right before canvases are rendered.
        /// This is the last opportunity to modify UI element positions.
        /// </summary>
        /// <param name="callback">The callback to register.</param>
        /// <param name="owner">Optional owning object. When supplied and later destroyed, the
        /// callback unregisters itself instead of throwing out of Unity's invocation list.</param>
        /// <exception cref="ArgumentNullException">Thrown when callback is null.</exception>
        /// <exception cref="ObjectDisposedException">Thrown when this instance is disposed.</exception>
        /// <exception cref="InvalidOperationException">Thrown when a willRenderCanvases callback is already registered.</exception>
        public void RegisterWillRenderCanvases(Action callback, UnityEngine.Object owner = null)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(CameraCallbackLifecycle));
            }

            if (callback == null)
            {
                throw new ArgumentNullException(nameof(callback));
            }

            if (_staticWillRenderCanvasesCallback != null)
            {
                throw new InvalidOperationException("A willRenderCanvases callback is already registered. Call UnregisterWillRenderCanvases first or Dispose the previous lifecycle.");
            }

            _staticWillRenderCanvasesCallback = callback;
            _willRenderCanvasesOwner = owner;
            _hasWillRenderCanvasesOwner = owner != null;
            Canvas.willRenderCanvases += OnWillRenderCanvasesWrapper;
            _ownsWillRenderCanvases = true;
        }

        /// <summary>
        /// Unregisters the willRenderCanvases callback if this instance owns it.
        /// </summary>
        public void UnregisterWillRenderCanvases()
        {
            if (!_ownsWillRenderCanvases)
            {
                return;
            }

            ClearWillRenderCanvases();
            _ownsWillRenderCanvases = false;
        }

        /// <summary>
        /// Wrapper for willRenderCanvases that calls our static callback.
        /// This is needed because Canvas.willRenderCanvases uses Canvas.WillRenderCanvases delegate
        /// which has a different signature than System.Action.
        /// </summary>
        private static void OnWillRenderCanvasesWrapper()
        {
            if (_hasWillRenderCanvasesOwner && _willRenderCanvasesOwner == null)
            {
                ClearWillRenderCanvases();
                return;
            }

            _staticWillRenderCanvasesCallback?.Invoke();
        }

        private static void ClearWillRenderCanvases()
        {
            if (_staticWillRenderCanvasesCallback == null)
            {
                return;
            }

            Canvas.willRenderCanvases -= OnWillRenderCanvasesWrapper;
            _staticWillRenderCanvasesCallback = null;
            _willRenderCanvasesOwner = null;
            _hasWillRenderCanvasesOwner = false;
        }

        /// <summary>
        /// Disposes of this lifecycle, unregistering all callbacks.
        /// Call this on application quit, NOT on GameObject destroy.
        /// </summary>
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            UnregisterPreCull();
            UnregisterPreRender();
            UnregisterWillRenderCanvases();

            _disposed = true;
        }

        /// <summary>
        /// Forces cleanup of all static callbacks regardless of ownership.
        /// Use this only in emergency situations (e.g., unhandled exceptions during shutdown).
        /// </summary>
        public static void ForceCleanupAll()
        {
            ClearPreCull();
            ClearPreRender();
            ClearWillRenderCanvases();
        }
    }
}
