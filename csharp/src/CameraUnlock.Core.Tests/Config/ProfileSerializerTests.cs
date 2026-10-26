// Test files intentionally pass null to test exception handling
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type

using System;
using Xunit;
using CameraUnlock.Core.Config.Profiles;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Tests.Config
{
    public class ProfileSerializerTests
    {
        [Fact]
        public void Serialize_ThrowsOnNull()
        {
            Assert.Throws<ArgumentNullException>(() => ProfileSerializer.Serialize(null));
        }

        [Fact]
        public void Deserialize_ThrowsOnNull()
        {
            Assert.Throws<ArgumentNullException>(() => ProfileSerializer.Deserialize(null));
        }

        [Fact]
        public void Serialize_ProducesValidOutput()
        {
            var profile = new ConfigProfile("Test", "Test Description", "TestGame");

            string result = ProfileSerializer.Serialize(profile);

            Assert.Contains("Name=Test", result);
            Assert.Contains("Description=Test Description", result);
            Assert.Contains("GameName=TestGame", result);
        }

        [Fact]
        public void Serialize_IncludesHeader()
        {
            var profile = new ConfigProfile();

            string result = ProfileSerializer.Serialize(profile);

            Assert.StartsWith("# CameraUnlock Configuration Profile", result);
        }

        [Fact]
        public void Serialize_IncludesSettings()
        {
            var profile = new ConfigProfile();
            profile.SetSetting("TestKey", "TestValue");
            profile.SetSetting("FloatKey", 1.5f);

            string result = ProfileSerializer.Serialize(profile);

            Assert.Contains("Setting.TestKey=TestValue", result);
            Assert.Contains("Setting.FloatKey=", result);
        }

        [Fact]
        public void Serialize_IncludesAxisMapping()
        {
            var profile = new ConfigProfile();
            profile.AxisMapping.YawConfig.Sensitivity = 2.0f;
            profile.AxisMapping.YawConfig.Inverted = true;

            string result = ProfileSerializer.Serialize(profile);

            Assert.Contains("AxisMapping.Yaw.Sensitivity=2.0000", result);
            Assert.Contains("AxisMapping.Yaw.Inverted=True", result);
        }

        [Fact]
        public void Deserialize_RestoresName()
        {
            string content = "Name=TestProfile\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("TestProfile", profile.Name);
        }

        [Fact]
        public void Deserialize_RestoresDescription()
        {
            string content = "Description=Test Description\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("Test Description", profile.Description);
        }

        [Fact]
        public void Deserialize_RestoresGameName()
        {
            string content = "GameName=TestGame\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("TestGame", profile.GameName);
        }

        [Fact]
        public void Deserialize_RestoresIsDefault()
        {
            string content = "IsDefault=True\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.True(profile.IsDefault);
        }

        [Fact]
        public void Deserialize_RestoresIsReadOnly()
        {
            string content = "IsReadOnly=True\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.True(profile.IsReadOnly);
        }

        [Fact]
        public void Deserialize_RestoresSettings()
        {
            string content = "Setting.StringKey=TestValue\nSetting.IntKey=42\nSetting.FloatKey=1.5\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("TestValue", profile.GetSetting<string>("StringKey"));
            Assert.Equal(42, profile.GetSetting<int>("IntKey"));
            Assert.Equal(1.5f, profile.GetSetting<float>("FloatKey"), precision: 3);
        }

        [Fact]
        public void Deserialize_RestoresBoolSettings()
        {
            string content = "Setting.BoolTrue=True\nSetting.BoolFalse=False\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.True(profile.GetSetting<bool>("BoolTrue"));
            Assert.False(profile.GetSetting<bool>("BoolFalse"));
        }

        [Fact]
        public void Deserialize_IgnoresComments()
        {
            string content = "# This is a comment\nName=Test\n# Another comment\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("Test", profile.Name);
        }

        [Fact]
        public void Deserialize_IgnoresEmptyLines()
        {
            string content = "Name=Test\n\n\nDescription=Desc\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("Test", profile.Name);
            Assert.Equal("Desc", profile.Description);
        }

        [Fact]
        public void Deserialize_RestoresAxisMapping()
        {
            string content = @"
AxisMapping.Yaw.Sensitivity=2.0000
AxisMapping.Yaw.Inverted=True
AxisMapping.Pitch.Sensitivity=0.5000
AxisMapping.Roll.Source=Roll
";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal(2.0f, profile.AxisMapping.YawConfig.Sensitivity, precision: 3);
            Assert.True(profile.AxisMapping.YawConfig.Inverted);
            Assert.Equal(0.5f, profile.AxisMapping.PitchConfig.Sensitivity, precision: 3);
        }

        [Fact]
        public void Deserialize_KeepsTheDefault_ForANumberThatIsNotFinite()
        {
            string content = @"
AxisMapping.Yaw.Sensitivity=NaN
AxisMapping.Yaw.MaxLimit=Infinity
AxisMapping.Pitch.CurveStrength=-Infinity
Setting.Gain=NaN
";
            var defaults = new MappingConfig();

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal(defaults.YawConfig.Sensitivity, profile.AxisMapping.YawConfig.Sensitivity);
            Assert.Equal(defaults.YawConfig.MaxLimit, profile.AxisMapping.YawConfig.MaxLimit);
            Assert.Equal(defaults.PitchConfig.CurveStrength, profile.AxisMapping.PitchConfig.CurveStrength);
            Assert.Equal("NaN", profile.GetSetting<string>("Gain"));
        }

        [Fact]
        public void RoundTrip_PreservesAllData()
        {
            var original = new ConfigProfile("RoundTrip", "Round Trip Test", "TestGame");
            original.IsDefault = true;
            original.IsReadOnly = false;
            original.SetSetting("StringKey", "StringValue");
            original.SetSetting("IntKey", 123);
            original.SetSetting("FloatKey", 1.5f);
            original.SetSetting("BoolKey", true);
            original.AxisMapping.YawConfig.Sensitivity = 2.5f;
            original.AxisMapping.YawConfig.Inverted = true;
            original.AxisMapping.PitchConfig.MinLimit = -45f;
            original.AxisMapping.PitchConfig.MaxLimit = 45f;

            string serialized = ProfileSerializer.Serialize(original);
            var restored = ProfileSerializer.Deserialize(serialized);

            Assert.Equal(original.Name, restored.Name);
            Assert.Equal(original.Description, restored.Description);
            Assert.Equal(original.GameName, restored.GameName);
            Assert.Equal(original.IsDefault, restored.IsDefault);
            Assert.Equal(original.IsReadOnly, restored.IsReadOnly);
            Assert.Equal("StringValue", restored.GetSetting<string>("StringKey"));
            Assert.Equal(123, restored.GetSetting<int>("IntKey"));
            Assert.Equal(1.5f, restored.GetSetting<float>("FloatKey"), precision: 3);
            Assert.True(restored.GetSetting<bool>("BoolKey"));
            Assert.Equal(2.5f, restored.AxisMapping.YawConfig.Sensitivity, precision: 3);
            Assert.True(restored.AxisMapping.YawConfig.Inverted);
            Assert.Equal(-45f, restored.AxisMapping.PitchConfig.MinLimit, precision: 1);
            Assert.Equal(45f, restored.AxisMapping.PitchConfig.MaxLimit, precision: 1);
        }

        [Fact]
        public void Deserialize_HandlesLineContinuation()
        {
            // Windows and Unix line endings
            string windowsContent = "Name=Test\r\nDescription=Desc\r\n";
            string unixContent = "Name=Test\nDescription=Desc\n";

            var windowsProfile = ProfileSerializer.Deserialize(windowsContent);
            var unixProfile = ProfileSerializer.Deserialize(unixContent);

            Assert.Equal("Test", windowsProfile.Name);
            Assert.Equal("Test", unixProfile.Name);
        }

        [Fact]
        public void Deserialize_HandlesInvalidLines()
        {
            string content = "Name=Test\nInvalidLineNoEquals\nDescription=Desc\n";

            var profile = ProfileSerializer.Deserialize(content);

            Assert.Equal("Test", profile.Name);
            Assert.Equal("Desc", profile.Description);
        }
    }
}
