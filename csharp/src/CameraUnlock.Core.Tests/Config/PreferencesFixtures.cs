#if NETCOREAPP
#nullable disable
#pragma warning disable CA1416
#endif
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// data/fixtures/canonical-ini/preferences, which cpp/tests/preferences_fixture_tests.cpp runs
    /// too: what a launcher reads for each of its four preferences, what the mod runs on, and what
    /// the mod's own Save writes for a change. The same source runs under xunit on net8.0
    /// (PreferencesFixtureTests) and in the CameraUnlock.Core.FrameworkTests console on .NET
    /// Framework 3.5 and 4.7.2. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class PreferencesFixtures
    {
        private const string Invalid = "invalid";
        private const string Missing = "missing";
        private const string TrackingModeName = "tracking_mode";
        private const string WorldSpaceYawName = "world_space_yaw";
        private const string TrueFreeLookName = "true_free_look";
        private const string LaunchEnabledName = "launch_enabled";

        private static readonly RenderHeader Header = new RenderHeader("Fixture Game");

        public static string[] Cases(string root)
        {
            string[] names = Directory.GetDirectories(Path.Combine(root, "preferences"))
                .Select(d => Path.GetFileName(d))
                .ToArray();
            Array.Sort(names, StringComparer.Ordinal);
            return names;
        }

        /// <summary>
        /// Throws when the case's input does not read as its preference rows, through the table and
        /// through the owner's Load, when Load writes the file, or when the owner's Save of the
        /// case's change, on a copy in <paramref name="scratch"/>, does not give expected.ini byte
        /// for byte, or expected.ini does not read as the changed rows.
        /// </summary>
        public static void RunCase(string root, string name, string scratch)
        {
            string dir = Path.Combine(Path.Combine(root, "preferences"), name);
            Case fixture = ReadCase(Path.Combine(dir, "case.tsv"));
            byte[] input = File.ReadAllBytes(Path.Combine(dir, "input.ini"));
            string expectedPath = Path.Combine(dir, "expected.ini");
            if (File.Exists(expectedPath) != (fixture.Change != null))
            {
                throw new InvalidOperationException("expected.ini must exist exactly when case.tsv has a change row");
            }

            SameRows(Rows(fixture.ThreeState, input, Applied(fixture.ThreeState, input)), fixture.Preferences,
                "input.ini through the table");

            string target = Path.Combine(scratch, "HeadTracking.ini");
            File.WriteAllBytes(target, input);
            var owner = new ConfigOwner<HeadTrackingConfigData>(new ConfigOwnerOptions<HeadTrackingConfigData>
            {
                Path = target,
                Table = Table(fixture.ThreeState, true),
                Header = Header,
                Defaults = DefaultsFile.At(Path.Combine(Path.Combine(scratch, "global"), "Defaults.ini")),
            });
            ConfigLoadResult<HeadTrackingConfigData> loaded = owner.Load();
            if (loaded.Status != ConfigLoadStatus.Canonical)
            {
                throw new InvalidOperationException("the owner loads the file as " + loaded.Status + ", not Canonical");
            }
            if (!Same(File.ReadAllBytes(target), input)) throw new InvalidOperationException("the owner's Load wrote the file");
            SameRows(Rows(fixture.ThreeState, input, loaded.Config), fixture.Preferences, "input.ini through the owner's Load");

            if (fixture.Change == null) return;
            string preference = fixture.Change[0];
            string value = fixture.Change[1];
            List<string> after = ChangedRows(fixture, preference, value);

            ConfigSaveResult saved = owner.Save(c => SetPreference(c, fixture.ThreeState, preference, value));
            if (saved.Status != ConfigSaveStatus.Saved)
            {
                throw new InvalidOperationException("the change is not saved: " + saved.Status + ": " + saved.Reason);
            }
            byte[] expected = File.ReadAllBytes(expectedPath);
            byte[] written = File.ReadAllBytes(target);
            if (!Same(written, expected))
            {
                throw new InvalidOperationException("the owner's Save writes other bytes than expected.ini:\n"
                    + Encoding.UTF8.GetString(written));
            }
            SameRows(Rows(fixture.ThreeState, expected, Applied(fixture.ThreeState, expected)), after,
                "expected.ini through the table");
        }

        private sealed class Case
        {
            public bool ThreeState;
            public readonly List<string> Preferences = new List<string>();
            public string[] Change;
        }

        private static Case ReadCase(string path)
        {
            var fixture = new Case();
            bool binds = false;
            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(path)).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string[] row = line.Split('\t');
                if (!binds)
                {
                    if (row.Length != 2 || row[0] != "binds" || (row[1] != "three-state" && row[1] != "two-state"))
                    {
                        throw new InvalidOperationException("the first row of " + path + " is not binds three-state or two-state");
                    }
                    fixture.ThreeState = row[1] == "three-state";
                    binds = true;
                }
                else if (row.Length == 4 && row[0] == "preference" && fixture.Change == null)
                {
                    fixture.Preferences.Add(row[1] + "\t" + row[2] + "\t" + row[3]);
                }
                else if (row.Length == 3 && row[0] == "change" && fixture.Change == null)
                {
                    fixture.Change = new[] { row[1], row[2] };
                }
                else
                {
                    throw new InvalidOperationException("malformed row in " + path + ": " + line);
                }
            }
            if (!binds) throw new InvalidOperationException(path + " has no binds row");
            return fixture;
        }

        // The rows a fixture mod binds; the Writable table is the one its owner saves through.
        private static ConfigTable<HeadTrackingConfigData> Table(bool threeState, bool writable)
        {
            ConfigTable<HeadTrackingConfigData> table = threeState
                ? HeadTrackingConfigTable.Create(ConfigConcepts.EnableOnStartup, ConfigConcepts.WorldSpaceYaw,
                    ConfigConcepts.RotationEnabled, ConfigConcepts.PositionEnabled, ConfigConcepts.TrueFreeLook)
                : HeadTrackingConfigTable.Create(ConfigConcepts.EnableOnStartup, ConfigConcepts.WorldSpaceYaw,
                    ConfigConcepts.PositionEnabled, ConfigConcepts.TrueFreeLook);
            if (!writable) return table;
            table.Select(ConfigConcepts.EnableOnStartup).Writable()
                .Select(ConfigConcepts.WorldSpaceYaw).Writable()
                .Select(ConfigConcepts.PositionEnabled).Writable()
                .Select(ConfigConcepts.TrueFreeLook).Writable();
            if (threeState) table.Select(ConfigConcepts.RotationEnabled).Writable();
            return table;
        }

        private static HeadTrackingConfigData Applied(bool threeState, byte[] bytes)
        {
            var config = new HeadTrackingConfigData();
            Table(threeState, false).Apply(CanonicalIni.Parse(bytes), config);
            return config;
        }

        // One "preference, raw, mod" row per preference, in case.tsv's order.
        private static List<string> Rows(bool threeState, byte[] bytes, HeadTrackingConfigData mod)
        {
            CanonicalIni doc = CanonicalIni.Parse(bytes);
            if (!doc.IsReadable) throw new InvalidOperationException("the file is unreadable: " + doc.Status);
            string rawMode;
            if (threeState)
            {
                string rotation = RawBool(doc, ConfigConcepts.RotationEnabled);
                string position = RawBool(doc, ConfigConcepts.PositionEnabled);
                if (rotation == Invalid || position == Invalid) rawMode = Invalid;
                else if (rotation == Missing || position == Missing) rawMode = Missing;
                else rawMode = ModeName(TrackingModeChannels.Decode(rotation == "true", position == "true"));
            }
            else
            {
                string position = RawBool(doc, ConfigConcepts.PositionEnabled);
                rawMode = position == Invalid || position == Missing
                    ? position
                    : ModeName(TrackingModeChannels.Decode(true, position == "true"));
            }
            return new List<string>
            {
                TrackingModeName + "\t" + rawMode + "\t" + ModeName(TrackingModeChannels.Decode(mod.RotationEnabled, mod.PositionEnabled)),
                WorldSpaceYawName + "\t" + RawBool(doc, ConfigConcepts.WorldSpaceYaw) + "\t" + BoolName(mod.WorldSpaceYaw),
                TrueFreeLookName + "\t" + RawBool(doc, ConfigConcepts.TrueFreeLook) + "\t" + BoolName(mod.TrueFreeLook),
                LaunchEnabledName + "\t" + RawBool(doc, ConfigConcepts.EnableOnStartup) + "\t" + BoolName(mod.EnableOnStartup),
            };
        }

        private static string RawBool(CanonicalIni doc, ConceptDescriptor concept)
        {
            CanonicalValue value = doc.Find(concept.Section, concept.Key);
            if (value == null) return Missing;
            bool parsed;
            string error;
            return new BoolCodec().TryParse(value.Value, out parsed, out error) ? BoolName(parsed) : Invalid;
        }

        private static string BoolName(bool value)
        {
            return value ? "true" : "false";
        }

        // The names are preference_modes' in data/pipeline-conformance.json; TrackingModeChannels is
        // held to its pairs by TrackingModeChannelsTests.
        private static string ModeName(TrackingMode? mode)
        {
            if (mode == null) return Invalid;
            switch (mode.Value)
            {
                case TrackingMode.RotationAndPosition: return "both";
                case TrackingMode.RotationOnly: return "rotation";
                case TrackingMode.PositionOnly: return "position";
            }
            throw new InvalidOperationException("no name for " + mode.Value);
        }

        private static TrackingMode ModeOf(string name, bool threeState)
        {
            switch (name)
            {
                case "both": return TrackingMode.RotationAndPosition;
                case "rotation": return TrackingMode.RotationOnly;
                case "position":
                    if (threeState) return TrackingMode.PositionOnly;
                    break;
            }
            throw new InvalidOperationException(name + " is not a tracking mode of a " + (threeState ? "three" : "two")
                + "-state mod");
        }

        private static bool BoolOf(string name)
        {
            if (name == "true") return true;
            if (name == "false") return false;
            throw new InvalidOperationException(name + " is not true or false");
        }

        // The preference rows with the changed one's raw and mod values both the new value. The new
        // value has to differ from what the mod runs on, or the owner's Save would write nothing.
        private static List<string> ChangedRows(Case fixture, string preference, string value)
        {
            var rows = new List<string>();
            bool found = false;
            foreach (string row in fixture.Preferences)
            {
                string[] fields = row.Split('\t');
                if (fields[0] != preference)
                {
                    rows.Add(row);
                    continue;
                }
                if (fields[2] == value) throw new InvalidOperationException("the change sets " + preference + " to the value the mod runs on");
                rows.Add(preference + "\t" + value + "\t" + value);
                found = true;
            }
            if (!found) throw new InvalidOperationException("the change names " + preference + ", which is not a preference");
            return rows;
        }

        private static void SetPreference(HeadTrackingConfigData config, bool threeState, string preference, string value)
        {
            switch (preference)
            {
                case TrackingModeName:
                    bool rotation;
                    bool position;
                    TrackingModeChannels.Encode(ModeOf(value, threeState), out rotation, out position);
                    if (threeState) config.RotationEnabled = rotation;
                    config.PositionEnabled = position;
                    return;
                case WorldSpaceYawName:
                    config.WorldSpaceYaw = BoolOf(value);
                    return;
                case TrueFreeLookName:
                    config.TrueFreeLook = BoolOf(value);
                    return;
                case LaunchEnabledName:
                    config.EnableOnStartup = BoolOf(value);
                    return;
            }
            throw new InvalidOperationException(preference + " is not a preference");
        }

        private static void SameRows(List<string> actual, List<string> expected, string what)
        {
            bool same = actual.Count == expected.Count;
            for (int i = 0; same && i < actual.Count; i++) same = actual[i] == expected[i];
            if (!same)
            {
                throw new InvalidOperationException(what + " differs.\n  expected:\n    " + string.Join("\n    ", expected.ToArray())
                    + "\n  actual:\n    " + string.Join("\n    ", actual.ToArray()));
            }
        }

        private static bool Same(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (a[i] != b[i]) return false;
            }
            return true;
        }
    }
}
