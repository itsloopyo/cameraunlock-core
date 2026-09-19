using BepInEx.Configuration;
using CameraUnlock.Core.Unity.BepInEx.Config;
using UnityEngine;
using Xunit;

namespace CameraUnlock.Core.Unity.Tests
{
    /// A mod's subclass as the fleet writes it: it binds its own extras and never touches
    /// the hotkey entries the base class owns.
    public class HeadTrackingConfigBaseTests
    {
        private sealed class ModConfig : HeadTrackingConfigBase
        {
        }

        private static ModConfig Initialized()
        {
            ModConfig config = new ModConfig();
            config.Initialize(new ConfigFile());
            return config;
        }

        /// The tracker app owns the centre. A bound RecenterKey writes "RecenterKey = Home"
        /// into every player's cfg for a key that does nothing.
        [Fact]
        public void Initialize_BindsNoRecenterKey()
        {
            ConfigFile file = new ConfigFile();
            new ModConfig().Initialize(file);

            Assert.DoesNotContain(file.BoundDefinitions, d => d.Key == "RecenterKey");
        }

        [Fact]
        public void HotkeyChange_RaisesOnConfigChanged()
        {
            ModConfig config = Initialized();
            int changes = 0;
            config.OnConfigChanged += () => changes++;

            config.ToggleKey.Value = KeyCode.Insert;

            Assert.Equal(1, changes);
        }
    }
}
