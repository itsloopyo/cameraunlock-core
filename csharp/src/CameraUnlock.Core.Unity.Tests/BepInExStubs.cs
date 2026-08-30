// NOT the shared reference stubs. csharp/stubs/BepInExStubs.cs is a REFERENCE assembly -
// every body throws NotImplementedException, because its only job is to make the compiler
// emit the right member references for a plugin that will meet the real BepInEx loader at
// runtime. Binding a config entry against it throws on the first call.
//
// This file is the opposite: a working fake, so HeadTrackingConfigBase can be compiled from
// source and EXECUTED here. ConfigFile really stores what was bound and ConfigEntry<T>
// really raises SettingChanged, which is what lets a test assert on the entries a mod's
// .cfg ends up with.
//
// This file is compiled ONLY into the test assembly. It never ships.

using System;
using System.Collections.Generic;

namespace UnityEngine
{
    /// Values match UnityEngine.KeyCode so a test reading one is reading the number a mod
    /// would find in its .cfg.
    public enum KeyCode
    {
        None = 0,
        Insert = 277,
        Home = 278,
        End = 279,
        PageUp = 280,
    }
}

namespace BepInEx.Configuration
{
    public abstract class AcceptableValueBase
    {
        protected AcceptableValueBase(Type valueType)
        {
            ValueType = valueType;
        }

        public Type ValueType { get; private set; }
    }

    public class AcceptableValueRange<T> : AcceptableValueBase where T : IComparable
    {
        public AcceptableValueRange(T minValue, T maxValue) : base(typeof(T))
        {
            MinValue = minValue;
            MaxValue = maxValue;
        }

        public T MinValue { get; private set; }
        public T MaxValue { get; private set; }
    }

    public class ConfigDescription
    {
        public ConfigDescription(string description, AcceptableValueBase acceptableValues = null, params object[] tags)
        {
            Description = description;
            AcceptableValues = acceptableValues;
            Tags = tags;
        }

        public string Description { get; private set; }
        public AcceptableValueBase AcceptableValues { get; private set; }
        public object[] Tags { get; private set; }
    }

    public class ConfigDefinition
    {
        public ConfigDefinition(string section, string key)
        {
            Section = section;
            Key = key;
        }

        public string Section { get; private set; }
        public string Key { get; private set; }
    }

    public abstract class ConfigEntryBase
    {
        public ConfigDefinition Definition { get; internal set; }
        public ConfigDescription Description { get; internal set; }
    }

    public sealed class ConfigEntry<T> : ConfigEntryBase
    {
        private T _value;

        public T Value
        {
            get { return _value; }
            set
            {
                _value = value;
                EventHandler handler = SettingChanged;
                if (handler != null) handler(this, EventArgs.Empty);
            }
        }

        public event EventHandler SettingChanged;
    }

    public class ConfigFile
    {
        private readonly List<ConfigDefinition> _bound = new List<ConfigDefinition>();

        /// Every definition passed to Bind, in bind order. A test asserts on this to see the
        /// sections and keys a user's .cfg would contain.
        public IList<ConfigDefinition> BoundDefinitions
        {
            get { return _bound; }
        }

        public ConfigEntry<T> Bind<T>(string section, string key, T defaultValue, ConfigDescription configDescription)
        {
            return Bind(new ConfigDefinition(section, key), defaultValue, configDescription);
        }

        public ConfigEntry<T> Bind<T>(string section, string key, T defaultValue, string description)
        {
            return Bind(new ConfigDefinition(section, key), defaultValue, new ConfigDescription(description));
        }

        public ConfigEntry<T> Bind<T>(ConfigDefinition configDefinition, T defaultValue, ConfigDescription configDescription)
        {
            _bound.Add(configDefinition);
            ConfigEntry<T> entry = new ConfigEntry<T>();
            entry.Definition = configDefinition;
            entry.Description = configDescription;
            entry.Value = defaultValue;
            return entry;
        }
    }
}
