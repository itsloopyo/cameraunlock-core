using Xunit;
using CameraUnlock.Core.Input;

namespace CameraUnlock.Core.Tests.Input
{
    public class HotkeyHandlerTests
    {
        private const int ToggleKey = CommonKeyCodes.End;
        private const int RecenterKey = CommonKeyCodes.Home;

        [Fact]
        public void Constructor_SetsDefaultEnabledTrue()
        {
            var handler = CreateHandler();

            Assert.True(handler.IsEnabled);
        }

        [Fact]
        public void SetToggleKey_UpdatesKeyBinding()
        {
            var handler = CreateHandler();
            handler.SetToggleKey(ToggleKey);

            handler.Update(0f);

            // No event fired because key wasn't pressed
            Assert.Equal(0, handler.ToggleCount);
        }

        [Fact]
        public void SetRecenterKey_UpdatesKeyBinding()
        {
            var handler = CreateHandler();
            handler.SetRecenterKey(RecenterKey);

            handler.Update(0f);

            Assert.Equal(0, handler.RecenterCount);
        }

        [Fact]
        public void Update_WhenToggleKeyPressed_TogglsEnabled()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            handler.SetToggleKey(ToggleKey);
            Assert.True(handler.IsEnabled);

            handler.Update(0f);

            Assert.False(handler.IsEnabled);
        }

        [Fact]
        public void Update_WhenToggleKeyPressed_IncrementsToggleCount()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            handler.SetToggleKey(ToggleKey);

            handler.Update(0f);

            Assert.Equal(1, handler.ToggleCount);
        }

        [Fact]
        public void Update_WhenToggleKeyPressed_FiresOnToggledEvent()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            handler.SetToggleKey(ToggleKey);

            bool eventFired = false;
            bool eventValue = true;
            handler.OnToggled += (enabled) =>
            {
                eventFired = true;
                eventValue = enabled;
            };

            handler.Update(0f);

            Assert.True(eventFired);
            Assert.False(eventValue); // Was true, now false
        }

        [Fact]
        public void Update_WhenRecenterKeyPressed_IncrementsRecenterCount()
        {
            int pressedKey = RecenterKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            handler.SetRecenterKey(RecenterKey);

            handler.Update(0f);

            Assert.Equal(1, handler.RecenterCount);
        }

        [Fact]
        public void Update_WhenRecenterKeyPressed_FiresOnRecenterEvent()
        {
            int pressedKey = RecenterKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            handler.SetRecenterKey(RecenterKey);

            bool eventFired = false;
            handler.OnRecenter += () => eventFired = true;

            handler.Update(0f);

            Assert.True(eventFired);
        }

        [Fact]
        public void Update_RespectsCooldown()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0.5f);
            handler.SetToggleKey(ToggleKey);

            handler.Update(1.0f);     // First press at t=1.0 (after initial cooldown period)
            handler.Update(1.1f);     // Second press at t=1.1s (within cooldown)
            handler.Update(1.2f);     // Third press at t=1.2s (within cooldown)

            Assert.Equal(1, handler.ToggleCount); // Only first should count
        }

        [Fact]
        public void Update_AllowsAfterCooldown()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0.3f);
            handler.SetToggleKey(ToggleKey);

            handler.Update(1.0f);     // First press at t=1.0
            handler.Update(1.5f);     // Second press at t=1.5s (after cooldown)

            Assert.Equal(2, handler.ToggleCount);
        }

        [Fact]
        public void Update_WhenTextInputActive_IgnoresKeys()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, () => true, 0f);
            handler.SetToggleKey(ToggleKey);

            handler.Update(0f);

            Assert.Equal(0, handler.ToggleCount); // Should be ignored
            Assert.True(handler.IsEnabled);       // Should not toggle
        }

        [Fact]
        public void Update_WhenTextInputInactive_ProcessesKeys()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, () => false, 0f);
            handler.SetToggleKey(ToggleKey);

            handler.Update(0f);

            Assert.Equal(1, handler.ToggleCount);
        }

        [Fact]
        public void Update_WhenKeyNotSet_DoesNotFire()
        {
            int pressedKey = ToggleKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            // Don't set toggle key

            handler.Update(0f);

            Assert.Equal(0, handler.ToggleCount);
        }

        [Fact]
        public void Toggle_InvertsEnabledState()
        {
            var handler = CreateHandler();
            Assert.True(handler.IsEnabled);

            handler.Toggle();

            Assert.False(handler.IsEnabled);
        }

        [Fact]
        public void Toggle_ReturnsNewState()
        {
            var handler = CreateHandler();

            bool result = handler.Toggle();

            Assert.False(result);
        }

        [Fact]
        public void Toggle_IncrementsToggleCount()
        {
            var handler = CreateHandler();

            handler.Toggle();
            handler.Toggle();

            Assert.Equal(2, handler.ToggleCount);
        }

        [Fact]
        public void Toggle_FiresOnToggledEvent()
        {
            var handler = CreateHandler();
            bool eventFired = false;
            handler.OnToggled += (enabled) => eventFired = true;

            handler.Toggle();

            Assert.True(eventFired);
        }

        [Fact]
        public void ResetCounts_ClearsToggleCount()
        {
            var handler = CreateHandler();
            handler.Toggle();
            handler.Toggle();
            Assert.Equal(2, handler.ToggleCount);

            handler.ResetCounts();

            Assert.Equal(0, handler.ToggleCount);
        }

        [Fact]
        public void ResetCounts_ClearsRecenterCount()
        {
            int pressedKey = RecenterKey;
            var handler = new HotkeyHandler(key => key == pressedKey, null, 0f);
            handler.SetRecenterKey(RecenterKey);
            handler.Update(0f);
            Assert.Equal(1, handler.RecenterCount);

            handler.ResetCounts();

            Assert.Equal(0, handler.RecenterCount);
        }

        [Fact]
        public void IsEnabled_CanBeSetDirectly()
        {
            var handler = CreateHandler();

            handler.IsEnabled = false;
            Assert.False(handler.IsEnabled);

            handler.IsEnabled = true;
            Assert.True(handler.IsEnabled);
        }

        [Fact]
        public void Constructor_WithListener_CallsListenerOnToggle()
        {
            int pressedKey = ToggleKey;
            var listener = new TestListener();
            var handler = new HotkeyHandler(
                key => key == pressedKey,
                null,
                listener,
                0f);
            handler.SetToggleKey(ToggleKey);

            handler.Update(0f);

            Assert.True(listener.ToggleCalled);
            Assert.False(listener.LastToggleValue); // Started enabled, now disabled
        }

        [Fact]
        public void Constructor_WithListener_CallsListenerOnRecenter()
        {
            int pressedKey = RecenterKey;
            var listener = new TestListener();
            var handler = new HotkeyHandler(
                key => key == pressedKey,
                null,
                listener,
                0f);
            handler.SetRecenterKey(RecenterKey);

            handler.Update(0f);

            Assert.True(listener.RecenterCalled);
        }

        [Fact]
        public void Toggle_CallsListenerOnToggle()
        {
            var listener = new TestListener();
            var handler = new HotkeyHandler(_ => false, null, listener, 0f);

            handler.Toggle();

            Assert.True(listener.ToggleCalled);
        }

        private static HotkeyHandler CreateHandler()
        {
            return new HotkeyHandler(_ => false, null, 0.3f);
        }

        private class TestListener : IHotkeyListener
        {
            public bool ToggleCalled { get; private set; }
            public bool LastToggleValue { get; private set; }
            public bool RecenterCalled { get; private set; }

            public void OnHotkeyToggle(bool enabled)
            {
                ToggleCalled = true;
                LastToggleValue = enabled;
            }

            public void OnHotkeyRecenter()
            {
                RecenterCalled = true;
            }
        }
    }
}
