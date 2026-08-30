using System;
using BepInEx.Configuration;
using CameraUnlock.Core.Config;
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

        [Fact]
        public void Initialize_BindsRecenterKey()
        {
            ModConfig config = Initialized();

            Assert.NotNull(config.RecenterKey);
        }

        [Fact]
        public void RecenterKey_IsReadableAfterInitialize()
        {
            ModConfig config = Initialized();

            Assert.Equal(KeyCode.Home, config.RecenterKey.Value);
        }

        [Fact]
        public void Initialize_PutsRecenterKeyInTheHotkeysSection()
        {
            ConfigFile file = new ConfigFile();
            new ModConfig().Initialize(file);

            Assert.Contains(file.BoundDefinitions, d => d.Section == "Hotkeys" && d.Key == "RecenterKey");
        }

        [Fact]
        public void RecenterKeyChange_RaisesOnConfigChanged()
        {
            ModConfig config = Initialized();
            int changes = 0;
            config.OnConfigChanged += () => changes++;

            config.RecenterKey.Value = KeyCode.End;

            Assert.Equal(1, changes);
        }

        /// The INI path and the BepInEx path describe the same key to the same user, so they
        /// have to agree on whether it exists and what it defaults to.
        [Fact]
        public void RecenterKeyDefault_MatchesTheIniPathDefault()
        {
            ModConfig config = Initialized();

            Assert.Equal(new HeadTrackingConfigData().RecenterKeyName, config.RecenterKey.Value.ToString());
        }
    }
}
