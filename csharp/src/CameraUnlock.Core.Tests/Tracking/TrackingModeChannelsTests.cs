using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;
using CameraUnlock.Core.Tracking;
using Xunit;

namespace CameraUnlock.Core.Tests.Tracking
{
    /// <summary>
    /// Holds the hand-written <see cref="TrackingModeChannels"/> to
    /// <c>preference_modes.tracking_mode</c> in data/pipeline-conformance.json. The runtime
    /// is not generated from the file, so editing either side alone fails here.
    /// </summary>
    public class TrackingModeChannelsTests
    {
        private sealed class DeclaredMode
        {
            public string Name = "";
            public TrackingMode Mode;
            public bool RotationEnabled;
            public bool PositionEnabled;
        }

        // Keyed by the entry's `csharp` symbol, the way PipelineConstantsTests binds.
        private static readonly Dictionary<string, TrackingMode> Bindings = new Dictionary<string, TrackingMode>
        {
            { "TrackingMode.RotationAndPosition", TrackingMode.RotationAndPosition },
            { "TrackingMode.RotationOnly", TrackingMode.RotationOnly },
            { "TrackingMode.PositionOnly", TrackingMode.PositionOnly },
        };

        private static string RepoRoot([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo? dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile)!);
            while (dir != null && !File.Exists(Path.Combine(dir.FullName, "data", "pipeline-conformance.json")))
            {
                dir = dir.Parent;
            }
            if (dir == null)
            {
                throw new InvalidOperationException("no data/pipeline-conformance.json above " + sourceFile);
            }
            return dir.FullName;
        }

        // In file order, which is the cycle order.
        private static List<DeclaredMode> DeclaredModes()
        {
            var modes = new List<DeclaredMode>();
            using (JsonDocument doc = JsonDocument.Parse(
                File.ReadAllText(Path.Combine(RepoRoot(), "data", "pipeline-conformance.json"))))
            {
                JsonElement block = doc.RootElement.GetProperty("preference_modes").GetProperty("tracking_mode");

                string[] channels = block.GetProperty("channels").EnumerateArray().Select(c => c.GetString()!).ToArray();
                Assert.Equal(new[] { "RotationEnabled", "PositionEnabled" }, channels);

                foreach (JsonElement entry in block.GetProperty("modes").EnumerateArray())
                {
                    string name = entry.GetProperty("name").GetString()!;
                    string symbol = entry.GetProperty("csharp").GetString()!;
                    Assert.True(Bindings.ContainsKey(symbol),
                        "preference_modes.tracking_mode '" + name + "' names the C# symbol '" + symbol +
                        "', but TrackingModeChannelsTests binds nothing to it");
                    modes.Add(new DeclaredMode
                    {
                        Name = name,
                        Mode = Bindings[symbol],
                        RotationEnabled = entry.GetProperty("RotationEnabled").GetBoolean(),
                        PositionEnabled = entry.GetProperty("PositionEnabled").GetBoolean(),
                    });
                }
            }
            return modes;
        }

        [Fact]
        public void EveryTrackingModeIsDeclaredExactlyOnce()
        {
            List<DeclaredMode> declared = DeclaredModes();

            Assert.Equal(
                Enum.GetValues(typeof(TrackingMode)).Cast<TrackingMode>().OrderBy(m => m),
                declared.Select(d => d.Mode).OrderBy(m => m));
            Assert.Equal(declared.Count, declared.Select(d => d.Name).Distinct().Count());
        }

        [Fact]
        public void Encode_WritesThePairTheFileDeclares()
        {
            foreach (DeclaredMode d in DeclaredModes())
            {
                TrackingModeChannels.Encode(d.Mode, out bool rotation, out bool position);

                Assert.True(rotation == d.RotationEnabled && position == d.PositionEnabled,
                    "Encode(" + d.Mode + ") wrote RotationEnabled=" + rotation + " PositionEnabled=" + position +
                    ", data/pipeline-conformance.json declares '" + d.Name + "' as " +
                    d.RotationEnabled + "/" + d.PositionEnabled);
            }
        }

        [Fact]
        public void Decode_NamesTheModeForEveryDeclaredPair_AndNothingForAnyOtherPair()
        {
            List<DeclaredMode> declared = DeclaredModes();

            foreach (bool rotation in new[] { true, false })
            {
                foreach (bool position in new[] { true, false })
                {
                    DeclaredMode? match = declared.SingleOrDefault(
                        d => d.RotationEnabled == rotation && d.PositionEnabled == position);

                    TrackingMode? decoded = TrackingModeChannels.Decode(rotation, position);

                    Assert.True(decoded == match?.Mode,
                        "Decode(" + rotation + ", " + position + ") returned " +
                        (decoded.HasValue ? decoded.Value.ToString() : "unrepresentable") +
                        ", data/pipeline-conformance.json declares " +
                        (match == null ? "no mode for that pair" : "'" + match.Name + "'"));
                }
            }
        }

        [Fact]
        public void Decode_ReportsBothChannelsOffAsUnrepresentable()
        {
            Assert.Null(TrackingModeChannels.Decode(false, false));
        }

        [Fact]
        public void EncodeThenDecode_ReturnsTheSameMode()
        {
            foreach (TrackingMode mode in Enum.GetValues(typeof(TrackingMode)))
            {
                TrackingModeChannels.Encode(mode, out bool rotation, out bool position);
                Assert.Equal(mode, TrackingModeChannels.Decode(rotation, position));
            }
        }

        // TrackingModeChannels has no cycle of its own: HeadTrackingSession.CycleMode already
        // defines one, and the file's array order is held to it here.
        [Fact]
        public void SessionCycleMode_StepsThroughTheFileInArrayOrder()
        {
            List<DeclaredMode> declared = DeclaredModes();

            using (var receiver = new OpenTrackReceiver())
            {
                var session = new HeadTrackingSession(receiver, new TrackingProcessor(), new PositionProcessor());
                for (int i = 0; i < declared.Count; i++)
                {
                    DeclaredMode next = declared[(i + 1) % declared.Count];
                    session.Mode = declared[i].Mode;

                    Assert.True(session.CycleMode() == next.Mode,
                        "CycleMode from '" + declared[i].Name + "' should land on '" + next.Name +
                        "', the next entry in data/pipeline-conformance.json, but landed on " + session.Mode);
                }
            }
        }

        private static TrackingMode? DecodeParsed(Dictionary<string, string> values)
        {
            var config = new HeadTrackingConfigData();
            config.ApplyValues(values);
            return TrackingModeChannels.Decode(config.RotationEnabled, config.PositionEnabled);
        }

        [Fact]
        public void OmittedChannels_TakeTheirDefaults()
        {
            Assert.Equal(TrackingMode.RotationAndPosition, DecodeParsed(new Dictionary<string, string>()));
            Assert.Equal(TrackingMode.RotationOnly,
                DecodeParsed(new Dictionary<string, string> { { "PositionEnabled", "false" } }));
            Assert.Equal(TrackingMode.PositionOnly,
                DecodeParsed(new Dictionary<string, string> { { "RotationEnabled", "false" } }));
        }

        [Fact]
        public void BothChannelsOffInAParsedConfig_StaysOffAndDecodesAsUnrepresentable()
        {
            var config = new HeadTrackingConfigData();
            config.ApplyValues(new Dictionary<string, string>
            {
                { "RotationEnabled", "false" },
                { "PositionEnabled", "false" },
            });

            Assert.Null(TrackingModeChannels.Decode(config.RotationEnabled, config.PositionEnabled));
            Assert.False(config.RotationEnabled);
            Assert.False(config.PositionEnabled);
        }
    }
}
