using System;
using System.Reflection;
using Xunit;
using CameraUnlock.Core.Reflection;

namespace CameraUnlock.Core.Tests.Reflection
{
    public class CompiledGettersTests
    {
        private class FakeGameType
        {
#pragma warning disable 414
            public static FakeGameType? Instance = null;
#pragma warning restore 414
            public int health = 42;
            public string Name => "player";
        }

        [Fact]
        public void ForStaticField_ReadsCurrentValue()
        {
            FieldInfo field = typeof(FakeGameType).GetField("Instance", BindingFlags.Public | BindingFlags.Static)!;
            Func<object> getter = CompiledGetters.ForStaticField(field);

            FakeGameType.Instance = null;
            Assert.Null(getter());

            var instance = new FakeGameType();
            FakeGameType.Instance = instance;
            Assert.Same(instance, getter());
        }

        [Fact]
        public void ForInstanceField_ReadsFieldFromInstance()
        {
            FieldInfo field = typeof(FakeGameType).GetField("health", BindingFlags.Public | BindingFlags.Instance)!;
            Func<object, object> getter = CompiledGetters.ForInstanceField(field);

            var instance = new FakeGameType { health = 7 };
            Assert.Equal(7, getter(instance));
        }

        [Fact]
        public void ForInstanceProperty_ReadsPropertyFromInstance()
        {
            PropertyInfo property = typeof(FakeGameType).GetProperty("Name", BindingFlags.Public | BindingFlags.Instance)!;
            Func<object, object> getter = CompiledGetters.ForInstanceProperty(property);

            Assert.Equal("player", getter(new FakeGameType()));
        }
    }
}
