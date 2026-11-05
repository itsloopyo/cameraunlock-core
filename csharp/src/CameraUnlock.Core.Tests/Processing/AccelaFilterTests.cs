using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using CameraUnlock.Core.Processing;
using Xunit;

namespace CameraUnlock.Core.Tests.Processing
{
    /// <summary>
    /// AccelaFilter against data/accela-cases.json, the outputs Hypari's Rust filter gives on the
    /// same inputs, which the C++ port matches too.
    /// </summary>
    public class AccelaFilterTests
    {
        // The port makes the Rust filter's operations in the same order; on a 64-bit JIT it
        // matches to the bit, and this leaves room only for the last digit of a double.
        private const double Tolerance = 1e-9;

        private static string CasesPath([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo? dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile)!);
            while (dir != null && !File.Exists(Path.Combine(dir.FullName, "data", "accela-cases.json")))
            {
                dir = dir.Parent;
            }
            if (dir == null)
            {
                throw new InvalidOperationException("no data/accela-cases.json above " + sourceFile);
            }
            return Path.Combine(dir.FullName, "data", "accela-cases.json");
        }

        private static AccelaPose Pose(JsonElement e)
        {
            return new AccelaPose(
                e.GetProperty("yaw").GetDouble(),
                e.GetProperty("pitch").GetDouble(),
                e.GetProperty("roll").GetDouble(),
                e.GetProperty("x").GetDouble(),
                e.GetProperty("y").GetDouble(),
                e.GetProperty("z").GetDouble());
        }

        private static double Worst(AccelaPose a, AccelaPose b)
        {
            // Yaw near the +-180 seam is compared the short way round.
            double yaw = System.Math.Abs(a.Yaw - b.Yaw);
            if (yaw > 180.0) yaw = 360.0 - yaw;
            double worst = yaw;
            foreach (double d in new[]
                     {
                         System.Math.Abs(a.Pitch - b.Pitch), System.Math.Abs(a.Roll - b.Roll), System.Math.Abs(a.X - b.X),
                         System.Math.Abs(a.Y - b.Y), System.Math.Abs(a.Z - b.Z),
                     })
            {
                worst = System.Math.Max(worst, d);
            }
            return worst;
        }

        [Fact]
        public void GivesTheOutputsInTheSharedFixture()
        {
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(CasesPath()));
            JsonElement cases = doc.RootElement.GetProperty("cases");
            Assert.True(cases.GetArrayLength() >= 8, "data/accela-cases.json has at least eight cases");
            foreach (JsonElement c in cases.EnumerateArray())
            {
                string name = c.GetProperty("name").GetString()!;
                var filter = new AccelaFilter(c.GetProperty("amount").GetDouble());
                int i = 0;
                foreach (JsonElement s in c.GetProperty("samples").EnumerateArray())
                {
                    AccelaPose got = filter.Filter(Pose(s.GetProperty("input")), s.GetProperty("time").GetDouble());
                    double worst = Worst(got, Pose(s.GetProperty("output")));
                    Assert.True(worst <= Tolerance, $"{name}: sample {i} is off by {worst}");
                    i++;
                }
                Assert.True(i > 0, $"{name} has samples");
            }
        }

        [Fact]
        public void ASmallStepEasesPartOfTheWay()
        {
            var filter = new AccelaFilter();
            filter.Filter(new AccelaPose(), 1.0);
            AccelaPose output = filter.Filter(new AccelaPose(2.0, 0.0, 0.0, 1.0, 0.0, 0.0), 1.0 + 1.0 / 60.0);
            Assert.InRange(output.Yaw, 1e-6, 2.0 - 1e-6);
            Assert.InRange(output.X, 1e-6, 1.0 - 1e-6);
        }

        [Fact]
        public void ResetPassesTheNextInputThrough()
        {
            var filter = new AccelaFilter();
            filter.Filter(new AccelaPose(), 1.0);
            filter.Reset();
            AccelaPose output = filter.Filter(new AccelaPose(25.0, 0.0, 0.0, 0.0, 0.0, -3.0), 1.5);
            Assert.Equal(25.0, output.Yaw, 4);
            Assert.Equal(-3.0, output.Z, 12);
        }

        [Fact]
        public void SetAmountKeepsTheStateAndClamps()
        {
            var filter = new AccelaFilter();
            filter.Filter(new AccelaPose(10.0, 0.0, 0.0, 0.0, 0.0, 0.0), 1.0);
            filter.SetAmount(3.0);
            Assert.Equal(1.0, filter.Amount);
            AccelaPose output = filter.Filter(new AccelaPose(10.0, 0.0, 0.0, 0.0, 0.0, 0.0), 1.0 + 1.0 / 60.0);
            Assert.Equal(10.0, output.Yaw, 4);
        }
    }
}
