using System;
using System.Reflection;
using Xunit;
using UnityEngine;
using CameraUnlock.Core.Unity.Rendering;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// The SRP camera events take (ScriptableRenderContext, Camera), a struct type the helper only
    /// meets at run time. The wrapper it subscribes must be built without System.Reflection.Emit:
    /// some Unity Mono profiles (Neon White, Unity 2020.3 URP) throw PlatformNotSupportedException
    /// for DynamicMethod, which stopped the whole mod from starting there.
    /// </summary>
    public class RenderPipelineHelperTests : IDisposable
    {
        private struct FakeRenderContext { public int Frame; }

        private static readonly FieldInfo StoredPreRender =
            typeof(RenderPipelineHelper).GetField("_storedPreRender", BindingFlags.Static | BindingFlags.NonPublic);

        public void Dispose()
        {
            StoredPreRender.SetValue(null, null);
        }

        private static Delegate Wrap(Type delegateType, string field)
        {
            MethodInfo create = typeof(RenderPipelineHelper).GetMethod("CreateSRPWrapperDelegate", BindingFlags.Static | BindingFlags.NonPublic);
            ParameterInfo[] ps = delegateType.GetMethod("Invoke").GetParameters();
            Type[] types = new Type[ps.Length];
            for (int i = 0; i < ps.Length; i++) types[i] = ps[i].ParameterType;
            try
            {
                return (Delegate)create.Invoke(null, new object[] { "Test", types, delegateType, field });
            }
            catch (TargetInvocationException e)
            {
                throw e.InnerException;
            }
        }

        [Fact]
        public void The_wrapper_has_the_event_type_and_passes_the_camera_to_the_stored_callback()
        {
            Camera seen = null;
            int calls = 0;
            StoredPreRender.SetValue(null, (Action<Camera>)(c => { seen = c; calls++; }));
            Camera camera = new Camera();

            var wrapper = (Action<FakeRenderContext, Camera>)Wrap(typeof(Action<FakeRenderContext, Camera>), "_storedPreRender");
            wrapper(new FakeRenderContext { Frame = 3 }, camera);

            Assert.Equal(1, calls);
            Assert.Same(camera, seen);
        }

        [Fact]
        public void The_wrapper_reads_the_stored_callback_at_each_call()
        {
            var wrapper = (Action<FakeRenderContext, Camera>)Wrap(typeof(Action<FakeRenderContext, Camera>), "_storedPreRender");
            int second = 0;
            StoredPreRender.SetValue(null, (Action<Camera>)(c => second++));
            wrapper(default(FakeRenderContext), null);
            Assert.Equal(1, second);
        }

        [Fact]
        public void An_event_that_does_not_take_a_context_and_a_camera_is_refused()
        {
            var e = Assert.Throws<InvalidOperationException>(() => Wrap(typeof(Action<Camera[]>), "_storedPreRender"));
            Assert.Contains("does not take (context, Camera)", e.Message);
        }
    }
}
