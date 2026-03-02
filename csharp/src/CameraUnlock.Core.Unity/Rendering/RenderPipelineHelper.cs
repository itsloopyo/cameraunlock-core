using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Rendering
{
    /// <summary>
    /// Provides unified render callback registration for both SRP (URP/HDRP) and Legacy render pipelines.
    /// Automatically detects which pipeline is active and registers appropriate callbacks.
    ///
    /// Usage:
    /// 1. Call RegisterCallbacks() with your pre/post render methods
    /// 2. Call UnregisterCallbacks() when done
    ///
    /// Note: On older Unity (pre-2019 without SRP), only legacy callbacks are used.
    /// SRP detection and callback registration uses reflection to avoid compile-time
    /// dependencies on SRP types which may not exist in all Unity versions.
    /// </summary>
    public static class RenderPipelineHelper
    {
        private static bool _isRegistered;
        private static bool _usingSRP;

        // Stored callbacks for unregistration - use Camera.CameraCallback to match Unity's delegate type
        private static Camera.CameraCallback _legacyPreRender;
        private static Camera.CameraCallback _legacyPostRender;

        // For SRP, we use reflection to avoid compile-time dependency
        private static object _srpPreRenderDelegate;
        private static object _srpPostRenderDelegate;

        private static bool _srpCheckDone;
        private static bool _srpAvailable;

        /// <summary>
        /// Returns true if using a Scriptable Render Pipeline (URP/HDRP).
        /// Returns false for Legacy/Built-in render pipeline.
        /// Uses reflection to check, so works even when SRP types aren't available at compile time.
        /// </summary>
        public static bool IsSRP
        {
            get
            {
                // Check if SRP is available and in use via reflection
                if (!_srpCheckDone)
                {
                    _srpAvailable = CheckSRPAvailable();
                    _srpCheckDone = true;
                }

                if (!_srpAvailable)
                {
                    return false;
                }

                // Check if currentRenderPipeline is set
                var graphicsSettingsType = Type.GetType("UnityEngine.Rendering.GraphicsSettings, UnityEngine.CoreModule");
                if (graphicsSettingsType == null)
                {
                    graphicsSettingsType = Type.GetType("UnityEngine.Rendering.GraphicsSettings, UnityEngine");
                }

                if (graphicsSettingsType != null)
                {
                    var prop = graphicsSettingsType.GetProperty("currentRenderPipeline",
                        System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public);
                    if (prop != null)
                    {
                        return prop.GetValue(null, null) != null;
                    }
                }

                return false;
            }
        }

        /// <summary>
        /// Returns true if callbacks are currently registered.
        /// </summary>
        public static bool IsRegistered
        {
            get { return _isRegistered; }
        }

        /// <summary>
        /// Registers render callbacks. Automatically detects SRP vs Legacy pipeline.
        ///
        /// For SRP: Uses RenderPipelineManager.beginCameraRendering/endCameraRendering
        /// For Legacy: Uses Camera.onPreCull/Camera.onPostRender
        /// </summary>
        /// <param name="onPreRender">Called before camera renders. Use to modify camera/view matrix.</param>
        /// <param name="onPostRender">Called after camera renders. Use to restore state.</param>
        /// <exception cref="InvalidOperationException">Thrown if callbacks are already registered.</exception>
        /// <exception cref="ArgumentNullException">Thrown if either callback is null.</exception>
        public static void RegisterCallbacks(Action<Camera> onPreRender, Action<Camera> onPostRender)
        {
            if (_isRegistered)
            {
                throw new InvalidOperationException("Callbacks already registered. Call UnregisterCallbacks() first.");
            }

            if (onPreRender == null)
            {
                throw new ArgumentNullException("onPreRender");
            }

            if (onPostRender == null)
            {
                throw new ArgumentNullException("onPostRender");
            }

            _usingSRP = IsSRP;

            if (_usingSRP)
            {
                // SRP path - use reflection
                RegisterSRPCallbacks(onPreRender, onPostRender);
            }
            else
            {
                // Legacy path - wrap Action<Camera> in Camera.CameraCallback
                _legacyPreRender = new Camera.CameraCallback(onPreRender);
                _legacyPostRender = new Camera.CameraCallback(onPostRender);

                Camera.onPreCull += _legacyPreRender;
                Camera.onPostRender += _legacyPostRender;
            }

            _isRegistered = true;
        }

        /// <summary>
        /// Unregisters all render callbacks.
        /// Safe to call even if not registered.
        /// </summary>
        public static void UnregisterCallbacks()
        {
            if (!_isRegistered)
            {
                return;
            }

            if (_usingSRP)
            {
                UnregisterSRPCallbacks();
            }
            else
            {
                if (_legacyPreRender != null)
                {
                    Camera.onPreCull -= _legacyPreRender;
                    _legacyPreRender = null;
                }
                if (_legacyPostRender != null)
                {
                    Camera.onPostRender -= _legacyPostRender;
                    _legacyPostRender = null;
                }
            }

            _isRegistered = false;
        }

        /// <summary>
        /// Checks if SRP types are available in this Unity build.
        /// </summary>
        private static bool CheckSRPAvailable()
        {
            var type = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine.CoreModule");
            if (type == null)
            {
                type = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine");
            }
            return type != null;
        }

        /// <summary>
        /// Registers SRP callbacks using reflection.
        /// </summary>
        private static void RegisterSRPCallbacks(Action<Camera> onPreRender, Action<Camera> onPostRender)
        {
            var rpmType = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine.CoreModule");
            if (rpmType == null)
            {
                rpmType = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine");
            }

            if (rpmType == null)
            {
                throw new InvalidOperationException("SRP detected but RenderPipelineManager type not found.");
            }

            var beginEvent = rpmType.GetEvent("beginCameraRendering");
            var endEvent = rpmType.GetEvent("endCameraRendering");

            if (beginEvent == null || endEvent == null)
            {
                throw new InvalidOperationException("SRP detected but beginCameraRendering/endCameraRendering events not found.");
            }

            // Get the delegate type for the event (Action<ScriptableRenderContext, Camera>)
            var delegateType = beginEvent.EventHandlerType;

            // Create a wrapper delegate that ignores the context and just calls our Action<Camera>
            // The event handler signature is: (ScriptableRenderContext context, Camera camera)
            // We create a dynamic method that ignores the first parameter

            var method = new System.Reflection.Emit.DynamicMethod(
                "SRPPreRenderWrapper",
                typeof(void),
                new[] { typeof(object), typeof(Camera) },
                typeof(RenderPipelineHelper).Module);

            var il = method.GetILGenerator();
            il.Emit(System.Reflection.Emit.OpCodes.Ldarg_1);
            il.Emit(System.Reflection.Emit.OpCodes.Call, onPreRender.Method);
            il.Emit(System.Reflection.Emit.OpCodes.Ret);

            // This approach is complex - let's use a simpler wrapper class
            // Actually, let's just create a lambda-compatible approach

            // Simpler: store the Action<Camera> in a static field and use a static method as handler
            _storedPreRender = onPreRender;
            _storedPostRender = onPostRender;

            // Create delegates using the SRP wrapper methods
            _srpPreRenderDelegate = Delegate.CreateDelegate(delegateType, typeof(RenderPipelineHelper), "SRPPreRenderHandler");
            _srpPostRenderDelegate = Delegate.CreateDelegate(delegateType, typeof(RenderPipelineHelper), "SRPPostRenderHandler");

            beginEvent.AddEventHandler(null, (Delegate)_srpPreRenderDelegate);
            endEvent.AddEventHandler(null, (Delegate)_srpPostRenderDelegate);
        }

        // Store the actual callbacks
        private static Action<Camera> _storedPreRender;
        private static Action<Camera> _storedPostRender;

        // These methods are called via reflection by the SRP events
        // Signature must match: void Handler(ScriptableRenderContext context, Camera camera)
        // Since we can't reference ScriptableRenderContext, we use 'object'
        private static void SRPPreRenderHandler(object context, Camera camera)
        {
            if (_storedPreRender != null)
            {
                _storedPreRender(camera);
            }
        }

        private static void SRPPostRenderHandler(object context, Camera camera)
        {
            if (_storedPostRender != null)
            {
                _storedPostRender(camera);
            }
        }

        /// <summary>
        /// Unregisters SRP callbacks using reflection.
        /// </summary>
        private static void UnregisterSRPCallbacks()
        {
            var rpmType = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine.CoreModule");
            if (rpmType == null)
            {
                rpmType = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine");
            }

            if (rpmType == null)
            {
                throw new InvalidOperationException("SRP was registered but RenderPipelineManager type no longer found.");
            }

            var beginEvent = rpmType.GetEvent("beginCameraRendering");
            var endEvent = rpmType.GetEvent("endCameraRendering");

            if (beginEvent != null && _srpPreRenderDelegate != null)
            {
                beginEvent.RemoveEventHandler(null, (Delegate)_srpPreRenderDelegate);
            }

            if (endEvent != null && _srpPostRenderDelegate != null)
            {
                endEvent.RemoveEventHandler(null, (Delegate)_srpPostRenderDelegate);
            }

            _srpPreRenderDelegate = null;
            _srpPostRenderDelegate = null;
            _storedPreRender = null;
            _storedPostRender = null;
        }
    }
}
