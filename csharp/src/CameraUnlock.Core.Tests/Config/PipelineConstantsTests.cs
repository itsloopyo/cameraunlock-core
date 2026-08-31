using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using CameraUnlock.Core.Ads;
using CameraUnlock.Core.Effects;
using CameraUnlock.Core.Math;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// The <c>constants</c> block of data/pipeline-conformance.json names a C++ and a C#
    /// symbol against every value, which reads as a cross-language gate. It was not one:
    /// <c>scripts/pipeline-vectors/run-vectors.mjs</c> reads only the <c>vectors</c> array,
    /// so nothing checked that the numbers in that block were the numbers the library
    /// ships.
    /// <para>
    /// That matters because the block is not decoration. It is what a Lua or Rust port
    /// pins its own restated copies against - cyberpunk-2077-headtracking's
    /// <c>tests/core_constants_test.lua</c> reads exactly these entries - so a stale value
    /// here silently walks every port off the shipped one.
    /// </para>
    /// <para>
    /// Every entry carrying a <c>csharp</c> symbol must be bound below. An entry added
    /// without one fails this test rather than shipping unchecked.
    /// </para>
    /// </summary>
    public class PipelineConstantsTests
    {
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

        // Keyed by the entry's `csharp` symbol, so the binding is checkable against the
        // file by eye rather than by position.
        private static Dictionary<string, double> ShippedValues()
        {
            return new Dictionary<string, double>
            {
                { "SmoothingUtils.DefaultLocalSmoothing", SmoothingUtils.DefaultLocalSmoothing },
                { "SmoothingUtils.DefaultRemoteSmoothing", SmoothingUtils.DefaultRemoteSmoothing },
                { "AdsFade.LowerMs", AdsFade.LowerMs },
                { "AdsFade.RaiseMs", AdsFade.RaiseMs },
                { "HeadFollowLightSettings.DefaultMultiplier", HeadFollowLightSettings.DefaultMultiplier },
                { "HeadFollowLightSettings.MaxMultiplier", HeadFollowLightSettings.MaxMultiplier },
            };
        }

        [Fact]
        public void ConformanceConstants_MatchTheShippedValues()
        {
            Dictionary<string, double> shipped = ShippedValues();
            var seen = new HashSet<string>();

            using (JsonDocument doc = JsonDocument.Parse(
                File.ReadAllText(Path.Combine(RepoRoot(), "data", "pipeline-conformance.json"))))
            {
                foreach (JsonProperty entry in doc.RootElement.GetProperty("constants").EnumerateObject())
                {
                    if (entry.Name.StartsWith("_", StringComparison.Ordinal)) continue;
                    if (!entry.Value.TryGetProperty("csharp", out JsonElement symbolElement)) continue;

                    string symbol = symbolElement.GetString()!;
                    seen.Add(symbol);

                    Assert.True(shipped.ContainsKey(symbol),
                        "data/pipeline-conformance.json entry '" + entry.Name + "' names the C# symbol '" +
                        symbol + "', but nothing in PipelineConstantsTests binds it to a shipped value");

                    // Compared at float precision: every constant here is a float in the
                    // library, and widening 0.15f to double gives 0.15000000596, which is
                    // not a drift from the 0.15 in the file.
                    JsonElement declared = entry.Value.GetProperty("value");
                    Assert.True((float)declared.GetDouble() == (float)shipped[symbol],
                        "constant '" + entry.Name + "': data/pipeline-conformance.json declares " +
                        declared.GetRawText() + ", " + symbol + " is " + shipped[symbol] +
                        ". Language ports pin their restated copies against that file, so move both or neither.");
                }
            }

            foreach (string symbol in shipped.Keys)
            {
                Assert.True(seen.Contains(symbol),
                    "PipelineConstantsTests binds '" + symbol +
                    "', but no entry in data/pipeline-conformance.json names it");
            }
        }
    }
}
