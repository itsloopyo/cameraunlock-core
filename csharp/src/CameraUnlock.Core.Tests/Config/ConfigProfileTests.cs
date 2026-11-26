// Test files intentionally pass null to test exception handling
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type

using System;
using System.Collections.Generic;
using Xunit;
using CameraUnlock.Core.Config.Profiles;

namespace CameraUnlock.Core.Tests.Config
{
    public class ConfigProfileTests
    {
        [Fact]
        public void Constructor_Default_SetsDefaultValues()
        {
            var profile = new ConfigProfile();

            Assert.Equal("New Profile", profile.Name);
            Assert.Equal("", profile.Description);
            Assert.Equal("General", profile.GameName);
            Assert.False(profile.IsDefault);
            Assert.False(profile.IsReadOnly);
            Assert.NotNull(profile.Settings);
            Assert.Empty(profile.Settings);
            Assert.NotNull(profile.AxisMapping);
        }

        [Fact]
        public void Constructor_WithName_SetsName()
        {
            var profile = new ConfigProfile("TestProfile");

            Assert.Equal("TestProfile", profile.Name);
        }

        [Fact]
        public void Constructor_WithNameAndDescription_SetsBoth()
        {
            var profile = new ConfigProfile("TestProfile", "Test Description", "TestGame");

            Assert.Equal("TestProfile", profile.Name);
            Assert.Equal("Test Description", profile.Description);
            Assert.Equal("TestGame", profile.GameName);
        }

        [Fact]
        public void SetSetting_StoresValue()
        {
            var profile = new ConfigProfile();

            profile.SetSetting("TestKey", "TestValue");

            Assert.True(profile.Settings.ContainsKey("TestKey"));
            Assert.Equal("TestValue", profile.Settings["TestKey"]);
        }

        [Fact]
        public void SetSetting_UpdatesModifiedDate()
        {
            var profile = new ConfigProfile();
            var originalDate = profile.ModifiedDate;

            System.Threading.Thread.Sleep(10); // Ensure time difference
            profile.SetSetting("TestKey", "TestValue");

            Assert.True(profile.ModifiedDate >= originalDate);
        }

        [Fact]
        public void GetSetting_ReturnsStoredValue()
        {
            var profile = new ConfigProfile();
            profile.SetSetting("IntKey", 42);

            int result = profile.GetSetting<int>("IntKey");

            Assert.Equal(42, result);
        }

        [Fact]
        public void GetSetting_ReturnsDefaultWhenKeyNotFound()
        {
            var profile = new ConfigProfile();

            int result = profile.GetSetting<int>("NonExistentKey", 99);

            Assert.Equal(99, result);
        }

        [Fact]
        public void GetSetting_ConvertsTypes()
        {
            var profile = new ConfigProfile();
            profile.SetSetting("FloatKey", 3.14f);

            double result = profile.GetSetting<double>("FloatKey");

            Assert.Equal(3.14, result, precision: 2);
        }

        [Fact]
        public void Clone_CreatesNewProfileWithNewName()
        {
            var original = new ConfigProfile("Original", "Original Description", "TestGame");
            original.SetSetting("Key1", "Value1");

            var clone = original.Clone("Clone");

            Assert.Equal("Clone", clone.Name);
            Assert.Contains("(Copy)", clone.Description);
            Assert.Equal("TestGame", clone.GameName);
        }

        [Fact]
        public void Clone_CopiesSettings()
        {
            var original = new ConfigProfile("Original");
            original.SetSetting("Key1", "Value1");
            original.SetSetting("Key2", 42);

            var clone = original.Clone("Clone");

            Assert.Equal("Value1", clone.GetSetting<string>("Key1"));
            Assert.Equal(42, clone.GetSetting<int>("Key2"));
        }

        [Fact]
        public void Clone_CreatesIndependentCopy()
        {
            var original = new ConfigProfile("Original");
            original.SetSetting("Key1", "Value1");

            var clone = original.Clone("Clone");
            clone.SetSetting("Key1", "Modified");

            Assert.Equal("Value1", original.GetSetting<string>("Key1"));
            Assert.Equal("Modified", clone.GetSetting<string>("Key1"));
        }

        [Fact]
        public void Clone_SetsIsDefaultFalse()
        {
            var original = new ConfigProfile("Original");
            original.IsDefault = true;

            var clone = original.Clone("Clone");

            Assert.False(clone.IsDefault);
        }

        [Fact]
        public void Clone_SetsIsReadOnlyFalse()
        {
            var original = new ConfigProfile("Original");
            original.IsReadOnly = true;

            var clone = original.Clone("Clone");

            Assert.False(clone.IsReadOnly);
        }

        [Fact]
        public void Clone_ThrowsOnNullName()
        {
            var profile = new ConfigProfile();

            Assert.Throws<ArgumentException>(() => profile.Clone(null));
        }

        [Fact]
        public void Clone_ThrowsOnEmptyName()
        {
            var profile = new ConfigProfile();

            Assert.Throws<ArgumentException>(() => profile.Clone(""));
        }

        [Fact]
        public void ExportFromAdapter_ThrowsOnNull()
        {
            var profile = new ConfigProfile();

            Assert.Throws<ArgumentNullException>(() => profile.ExportFromAdapter(null));
        }

        [Fact]
        public void ImportToAdapter_ThrowsOnNull()
        {
            var profile = new ConfigProfile();

            Assert.Throws<ArgumentNullException>(() => profile.ImportToAdapter(null));
        }

        [Fact]
        public void ExportFromAdapter_CapturesSettings()
        {
            var profile = new ConfigProfile();
            var adapter = new TestProfileAdapter();
            adapter.TestSettings["Sensitivity"] = 1.5f;
            adapter.TestSettings["Enabled"] = true;

            profile.ExportFromAdapter(adapter);

            Assert.Equal(1.5f, profile.GetSetting<float>("Sensitivity"));
            Assert.True(profile.GetSetting<bool>("Enabled"));
            Assert.Equal("TestGame", profile.GameName);
        }

        [Fact]
        public void ImportToAdapter_AppliesSettings()
        {
            var profile = new ConfigProfile();
            profile.SetSetting("Sensitivity", 2.0f);
            profile.SetSetting("Enabled", false);

            var adapter = new TestProfileAdapter();
            profile.ImportToAdapter(adapter);

            Assert.Equal(2.0f, adapter.TestSettings["Sensitivity"]);
            Assert.False((bool)adapter.TestSettings["Enabled"]);
            Assert.True(adapter.SaveCalled);
        }

        private class TestProfileAdapter : IProfileSettings
        {
            public string GameName => "TestGame";
            public Dictionary<string, object> TestSettings { get; } = new Dictionary<string, object>();
            public bool SaveCalled { get; private set; }

            public Dictionary<string, object> ExportSettings() => new Dictionary<string, object>(TestSettings);

            public void ImportSettings(Dictionary<string, object> settings)
            {
                TestSettings.Clear();
                foreach (var kvp in settings)
                {
                    TestSettings[kvp.Key] = kvp.Value;
                }
            }

            public void SaveConfig()
            {
                SaveCalled = true;
            }
        }
    }
}
