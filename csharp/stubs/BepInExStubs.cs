// Reference stubs for the BepInEx configuration API that CameraUnlock.Core.Unity.BepInEx
// compiles against. Compiled into an assembly named BepInEx by
// csharp/stubs/BepInEx/Stubs.BepInEx.csproj.
//
// This repo vendors no BepInEx, and it must build from a clean clone with no game and no
// sibling mod checkout, so there is nothing else to point the reference at. A mod that has
// a real BepInEx.dll passes -p:BepInExPath= and this project is not built at all.
//
// The assembly name is load-bearing. BepInEx 5.x declares BepInEx.Configuration.* in
// BepInEx.dll, so a reference emitted against this stub reads [BepInEx]
// BepInEx.Configuration.ConfigEntry`1 and binds against the real loader at runtime.
// (BepInEx 6 moved these types to BepInEx.Core.dll; core targets net472/net48 for BepInEx
// 5, which is what the fleet ships.)
//
// Shape is part of the contract, for the same reason the Unity stubs beside this file
// spell it out: the compiler bakes the member kind, the declaring type and the optional
// parameters into every reference, and nothing catches a mismatch until the plugin runs
// inside the game. Every signature here was read off BepInEx.dll 5.4.23.5 by reflection
// rather than written from memory:
//
//   - Value and SettingChanged are declared on ConfigEntry<T>, NOT on ConfigEntryBase.
//   - ConfigDescription's constructor really is (string, AcceptableValueBase = null,
//     params object[]), so a one-argument call at a use site stays valid.
//   - both ConfigDescription-taking Bind overloads default that parameter to null, so
//     config.Bind("General", "Enabled", true) is a legal three-argument call. Dropping
//     the default here compiled for a dev with a real BepInEx.dll and failed CS1501 on
//     a clean clone. An optional parameter is baked in at the CALL site, so a stub has
//     to reproduce the defaults as well as the parameter list.
//   - AcceptableValueRange<T> derives from AcceptableValueBase and constrains T to
//     IComparable.
//
// Only what core compiles against is stubbed. This is not a BepInEx reimplementation, and
// a member added here that the real assembly does not declare is worse than a missing one:
// it compiles and then throws at load.

using System;

namespace BepInEx.Configuration
{
    public abstract class AcceptableValueBase
    {
        protected AcceptableValueBase(Type valueType)
        {
            ValueType = valueType;
        }

        public Type ValueType { get; private set; }

        public abstract object Clamp(object value);
        public abstract bool IsValid(object value);
        public abstract string ToDescriptionString();
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

        public override object Clamp(object value) { throw new NotImplementedException(); }
        public override bool IsValid(object value) { throw new NotImplementedException(); }
        public override string ToDescriptionString() { throw new NotImplementedException(); }
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
        public ConfigDefinition Definition { get; private set; }
        public ConfigDescription Description { get; private set; }
        public Type SettingType { get; private set; }
        public object DefaultValue { get; private set; }
        public object BoxedValue { get; set; }
    }

    public sealed class ConfigEntry<T> : ConfigEntryBase
    {
        public T Value { get; set; }

        public event EventHandler SettingChanged;
    }

    public class ConfigFile
    {
        public ConfigEntry<T> Bind<T>(string section, string key, T defaultValue, ConfigDescription configDescription = null)
        {
            throw new NotImplementedException();
        }

        public ConfigEntry<T> Bind<T>(string section, string key, T defaultValue, string description)
        {
            throw new NotImplementedException();
        }

        public ConfigEntry<T> Bind<T>(ConfigDefinition configDefinition, T defaultValue, ConfigDescription configDescription = null)
        {
            throw new NotImplementedException();
        }

        public void Save() { throw new NotImplementedException(); }
    }
}
