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

        // Stored callback for the standalone beginCameraRendering subscription
        private static Action<Camera> _storedBeginCameraRendering;
        private static Delegate _beginCameraRenderingDelegate;

        /// <summary>
        /// Subscribes to SRP's RenderPipelineManager.beginCameraRendering when the type exists
        /// in this Unity build; no-op otherwise. Unlike RegisterCallbacks, this does not pick
        /// between SRP and legacy - subscribe Camera.onPreCull separately to cover legacy
        /// pipelines. A given Unity build only invokes one of the two paths per frame, so
        /// subscribing to both is safe and avoids relying on pipeline detection.
        /// </summary>
        /// <param name="onBeginCameraRendering">Called with the camera about to render.</param>
        /// <exception cref="ArgumentNullException">Thrown if the callback is null.</exception>
        /// <exception cref="InvalidOperationException">Thrown if a callback is already subscribed.</exception>
        public static void AddBeginCameraRendering(Action<Camera> onBeginCameraRendering)
        {
            if (onBeginCameraRendering == null)
            {
                throw new ArgumentNullException("onBeginCameraRendering");
            }

            if (_storedBeginCameraRendering != null)
            {
                throw new InvalidOperationException("A beginCameraRendering callback is already subscribed. Call RemoveBeginCameraRendering() first.");
            }

            var rpmType = GetRenderPipelineManagerType();
            if (rpmType == null)
            {
                return;
            }

            var beginEvent = rpmType.GetEvent("beginCameraRendering");
            if (beginEvent == null)
            {
                return;
            }

            var delegateType = beginEvent.EventHandlerType;
            var invokeParams = delegateType.GetMethod("Invoke").GetParameters();
            var paramTypes = new Type[invokeParams.Length];
            for (int i = 0; i < invokeParams.Length; i++)
            {
                paramTypes[i] = invokeParams[i].ParameterType;
            }

            _storedBeginCameraRendering = onBeginCameraRendering;
            _beginCameraRenderingDelegate = CreateSRPWrapperDelegate(
                "SRPBeginCameraRenderingWrapper", paramTypes, delegateType, "_storedBeginCameraRendering");
            beginEvent.AddEventHandler(null, _beginCameraRenderingDelegate);
        }

        /// <summary>
        /// Removes the beginCameraRendering subscription. Safe to call when not subscribed.
        /// </summary>
        public static void RemoveBeginCameraRendering()
        {
            if (_storedBeginCameraRendering == null)
            {
                return;
            }

            if (_beginCameraRenderingDelegate != null)
            {
                var rpmType = GetRenderPipelineManagerType();
                if (rpmType != null)
                {
                    var beginEvent = rpmType.GetEvent("beginCameraRendering");
                    if (beginEvent != null)
                    {
                        beginEvent.RemoveEventHandler(null, _beginCameraRenderingDelegate);
                    }
                }
            }

            _beginCameraRenderingDelegate = null;
            _storedBeginCameraRendering = null;
        }

        /// <summary>
        /// Resolves the RenderPipelineManager type, or null when this Unity build has no SRP.
        /// </summary>
        private static Type GetRenderPipelineManagerType()
        {
            var rpmType = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine.CoreModule");
            if (rpmType == null)
            {
                rpmType = Type.GetType("UnityEngine.Rendering.RenderPipelineManager, UnityEngine");
            }
            return rpmType;
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
        /// Registers SRP callbacks using reflection and IL-emitted wrapper delegates.
        /// The SRP events have signature Action&lt;ScriptableRenderContext, Camera&gt; where
        /// ScriptableRenderContext is a value type. We use DynamicMethod to emit wrappers
        /// with the exact parameter types, avoiding the struct/object binding mismatch
        /// that Delegate.CreateDelegate cannot handle.
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

            var delegateType = beginEvent.EventHandlerType;

            // Get the actual parameter types from the delegate's Invoke method.
            // This gives us (ScriptableRenderContext, Camera) with the real struct type,
            // which DynamicMethod needs to match for valid delegate binding.
            var invokeParams = delegateType.GetMethod("Invoke").GetParameters();
            var paramTypes = new Type[invokeParams.Length];
            for (int i = 0; i < invokeParams.Length; i++)
            {
                paramTypes[i] = invokeParams[i].ParameterType;
            }

            _storedPreRender = onPreRender;
            _storedPostRender = onPostRender;

            _srpPreRenderDelegate = CreateSRPWrapperDelegate("SRPPreRenderWrapper", paramTypes, delegateType, "_storedPreRender");
            _srpPostRenderDelegate = CreateSRPWrapperDelegate("SRPPostRenderWrapper", paramTypes, delegateType, "_storedPostRender");

            beginEvent.AddEventHandler(null, (Delegate)_srpPreRenderDelegate);
            endEvent.AddEventHandler(null, (Delegate)_srpPostRenderDelegate);
        }

        // Store the actual callbacks
        private static Action<Camera> _storedPreRender;
        private static Action<Camera> _storedPostRender;

        /// <summary>
        /// Emits a DynamicMethod matching the SRP event signature (ScriptableRenderContext, Camera),
        /// loads the stored Action&lt;Camera&gt; from the named static field, and invokes it with just the Camera.
        /// </summary>
        private static Delegate CreateSRPWrapperDelegate(string name, Type[] paramTypes, Type delegateType, string fieldName)
        {
            var dm = new System.Reflection.Emit.DynamicMethod(
                name,
                typeof(void),
                paramTypes,
                typeof(RenderPipelineHelper).Module,
                true);

            var field = typeof(RenderPipelineHelper).GetField(fieldName,
                System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
            var invokeMethod = typeof(Action<Camera>).GetMethod("Invoke");

            var il = dm.GetILGenerator();
            il.Emit(System.Reflection.Emit.OpCodes.Ldsfld, field);
            il.Emit(System.Reflection.Emit.OpCodes.Ldarg_1);
            il.Emit(System.Reflection.Emit.OpCodes.Callvirt, invokeMethod);
            il.Emit(System.Reflection.Emit.OpCodes.Ret);

            return dm.CreateDelegate(delegateType);
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
