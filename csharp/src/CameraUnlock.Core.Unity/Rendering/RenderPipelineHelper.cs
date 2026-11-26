using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Rendering
{
    /// <summary>
    /// Provides unified render callback registration for both SRP (URP/HDRP) and Legacy render pipelines.
    ///
    /// Usage:
    /// 1. Call RegisterCallbacks() with your pre/post render methods
    /// 2. Call UnregisterCallbacks() when done
    ///
    /// Every hook goes through reflection: SRP types may not exist in a given Unity build, and
    /// SRP-only Unity 6 builds strip the legacy Camera hooks, so a direct reference to either
    /// throws MissingMethodException at JIT time.
    /// </summary>
    public static class RenderPipelineHelper
    {
        private static bool _isRegistered;
        private static bool _registeredSRP;

        private static Delegate _legacyPreRender;
        private static Delegate _legacyPostRender;

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
        /// Registers render callbacks on both pipelines: SRP's
        /// RenderPipelineManager.beginCameraRendering/endCameraRendering where this Unity build has
        /// them, and the legacy Camera.onPreCull/Camera.onPostRender where it has those. A frame is
        /// rendered by one pipeline, which fires only its own pair, so this relies on no pipeline
        /// detection (GraphicsSettings.currentRenderPipeline is missing before Unity 2019.3).
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

            _registeredSRP = CheckSRPAvailable();
            if (_registeredSRP)
            {
                RegisterSRPCallbacks(onPreRender, onPostRender);
            }

            _legacyPreRender = AddLegacyCameraCallback("onPreCull", onPreRender);
            _legacyPostRender = AddLegacyCameraCallback("onPostRender", onPostRender);

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

            if (_registeredSRP)
            {
                UnregisterSRPCallbacks();
                _registeredSRP = false;
            }

            RemoveLegacyCameraCallback("onPreCull", _legacyPreRender);
            RemoveLegacyCameraCallback("onPostRender", _legacyPostRender);
            _legacyPreRender = null;
            _legacyPostRender = null;

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

        // Stored callback for the standalone onPreCull subscription.
        private static Action<Camera> _storedOnPreCull;
        private static Delegate _onPreCullDelegate;

        /// <summary>
        /// Subscribes to the legacy Camera.onPreCull hook via reflection when this Unity build has
        /// it; no-op otherwise. SRP-only builds (Unity 6 URP/HDRP) strip it entirely, so a direct
        /// <c>Camera.onPreCull += ...</c> throws MissingMethodException at JIT time the moment the
        /// calling method runs - going through reflection keeps the reference out of compiled IL.
        /// Pair with AddBeginCameraRendering to cover both pipelines.
        ///
        /// Unity declares onPreCull as a public static delegate FIELD, not an event, so it has to
        /// be resolved with GetField and combined by hand. Resolving it with GetEvent returns null
        /// and silently subscribes nothing - which is exactly how this shipped broken.
        /// </summary>
        /// <param name="onPreCull">Called with the camera about to render.</param>
        /// <exception cref="ArgumentNullException">Thrown if the callback is null.</exception>
        /// <exception cref="InvalidOperationException">Thrown if a callback is already subscribed.</exception>
        public static void AddOnPreCull(Action<Camera> onPreCull)
        {
            if (onPreCull == null)
            {
                throw new ArgumentNullException("onPreCull");
            }

            if (_storedOnPreCull != null)
            {
                throw new InvalidOperationException("An onPreCull callback is already subscribed. Call RemoveOnPreCull() first.");
            }

            _onPreCullDelegate = AddLegacyCameraCallback("onPreCull", onPreCull);
            if (_onPreCullDelegate != null)
            {
                _storedOnPreCull = onPreCull;
            }
        }

        /// <summary>
        /// Removes the onPreCull subscription. Safe to call when not subscribed.
        /// </summary>
        public static void RemoveOnPreCull()
        {
            if (_storedOnPreCull == null)
            {
                return;
            }

            RemoveLegacyCameraCallback("onPreCull", _onPreCullDelegate);
            _onPreCullDelegate = null;
            _storedOnPreCull = null;
        }

        /// <summary>
        /// Adds a callback to a legacy static Camera hook (onPreCull, onPostRender) and returns the
        /// delegate added, or null when this Unity build has no such hook (SRP-only Unity 6).
        /// Unity declares these as public static delegate FIELDS, not events, so they are resolved
        /// with GetField and combined by hand; resolving one with GetEvent returns null and
        /// silently subscribes nothing.
        /// </summary>
        private static Delegate AddLegacyCameraCallback(string hookName, Action<Camera> callback)
        {
            var field = typeof(Camera).GetField(hookName,
                System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static);
            if (field != null)
            {
                var added = Delegate.CreateDelegate(field.FieldType, callback.Target, callback.Method);
                field.SetValue(null, Delegate.Combine((Delegate)field.GetValue(null), added));
                return added;
            }

            var hookEvent = typeof(Camera).GetEvent(hookName,
                System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static);
            if (hookEvent == null)
            {
                return null;
            }

            var handler = Delegate.CreateDelegate(hookEvent.EventHandlerType, callback.Target, callback.Method);
            hookEvent.AddEventHandler(null, handler);
            return handler;
        }

        private static void RemoveLegacyCameraCallback(string hookName, Delegate added)
        {
            if (added == null)
            {
                return;
            }

            var field = typeof(Camera).GetField(hookName,
                System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static);
            if (field != null)
            {
                field.SetValue(null, Delegate.Remove((Delegate)field.GetValue(null), added));
                return;
            }

            typeof(Camera).GetEvent(hookName,
                System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static)
                .RemoveEventHandler(null, added);
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
            return GetRenderPipelineManagerType() != null;
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
            var rpmType = GetRenderPipelineManagerType();
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
        /// Builds a delegate of the SRP event's own type (ScriptableRenderContext, Camera) that reads
        /// the stored Action&lt;Camera&gt; from the named static field on every call and invokes it with
        /// just the Camera. Bound to a closed generic forwarder over the event's real context type
        /// rather than emitted, since some Unity Mono profiles (Neon White, Unity 2020.3 URP) throw
        /// PlatformNotSupportedException for System.Reflection.Emit.DynamicMethod.
        /// </summary>
        private static Delegate CreateSRPWrapperDelegate(string name, Type[] paramTypes, Type delegateType, string fieldName)
        {
            if (paramTypes.Length != 2 || paramTypes[1] != typeof(Camera))
            {
                throw new InvalidOperationException(name + ": the SRP event " + delegateType + " does not take (context, Camera).");
            }

            var field = typeof(RenderPipelineHelper).GetField(fieldName,
                System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
            if (field == null || field.FieldType != typeof(Action<Camera>))
            {
                throw new InvalidOperationException(name + ": no static Action<Camera> field " + fieldName + ".");
            }

            Func<Action<Camera>> read = () => (Action<Camera>)field.GetValue(null);
            var forwarderType = typeof(SrpCameraForwarder<>).MakeGenericType(paramTypes[0]);
            var forwarder = Activator.CreateInstance(forwarderType, read);
            return Delegate.CreateDelegate(delegateType, forwarder, forwarderType.GetMethod("Invoke"));
        }

        /// <summary>Forwards an SRP camera event to the stored Action&lt;Camera&gt;, dropping the context.</summary>
        private sealed class SrpCameraForwarder<TContext>
        {
            private readonly Func<Action<Camera>> _read;

            public SrpCameraForwarder(Func<Action<Camera>> read)
            {
                _read = read;
            }

            public void Invoke(TContext context, Camera camera)
            {
                _read()(camera);
            }
        }

        /// <summary>
        /// Unregisters SRP callbacks using reflection.
        /// </summary>
        private static void UnregisterSRPCallbacks()
        {
            var rpmType = GetRenderPipelineManagerType();
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
