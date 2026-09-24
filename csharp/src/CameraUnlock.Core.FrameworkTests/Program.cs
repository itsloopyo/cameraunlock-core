using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Tests.Config;
using CameraUnlock.Core.Tests.Input;

namespace CameraUnlock.Core.Tests
{
    internal static class Program
    {
        private const string InterruptFlag = "--interrupt";
        private const string OwnerInterruptFlag = "--owner-interrupt";
        private const int SweepCount = 50000;

        private static readonly CheckedWriteStep[] InterruptibleSteps =
        {
            CheckedWriteStep.CreateTemporary,
            CheckedWriteStep.WriteTemporary,
            CheckedWriteStep.FlushTemporary,
            CheckedWriteStep.CloseTemporary,
            CheckedWriteStep.RecheckTarget,
            CheckedWriteStep.Commit,
        };

        private static int Main(string[] args)
        {
            if (args.Length == 3 && args[0] == InterruptFlag)
            {
                return DieDuring((CheckedWriteStep)Enum.Parse(typeof(CheckedWriteStep), args[1]), args[2]);
            }
            if (args.Length == 3 && args[0] == OwnerInterruptFlag)
            {
                ConfigOwnerScenarios.RunInterruptedChild(args[1], args[2]);
                return 0;
            }

#if NET35
            const string built = "net35";
#else
            const string built = "net472";
#endif
            Console.WriteLine("CheckedFileWriter scenarios, " + built + " build, CLR " + Environment.Version
                + ", mscorlib image " + typeof(object).Assembly.ImageRuntimeVersion);

            int failures = 0;
            foreach (string name in CheckedFileWriterScenarios.Names)
            {
                failures += Report(name, dir => CheckedFileWriterScenarios.Run(name, dir));
            }
            foreach (CheckedWriteStep step in InterruptibleSteps)
            {
                failures += Report("killed-during-" + step, dir => KilledDuring(step, dir));
            }

            Console.WriteLine("ConfigOwner scenarios");
            foreach (string name in ConfigOwnerScenarios.Names)
            {
                failures += Report(name, dir => ConfigOwnerScenarios.Run(name, dir));
            }
            foreach (string label in ConfigOwnerScenarios.InterruptionLabels)
            {
                failures += Report("conversion-killed-at-" + label, dir => ConversionKilledAt(label, dir));
            }

            Console.WriteLine("Canonical INI reader fixtures");
            string fixtures = CanonicalIniFixtures.FindRoot(AppDomain.CurrentDomain.BaseDirectory);
            foreach (string name in CanonicalIniFixtures.ReaderCases(fixtures))
            {
                failures += Report(name, dir => CanonicalIniFixtures.RunReaderCase(fixtures, name));
            }

            Console.WriteLine("INI editor fixtures");
            foreach (string name in IniEditorFixtures.Cases(fixtures))
            {
                failures += Report(name, dir => IniEditorFixtures.RunCase(fixtures, name));
            }

            Console.WriteLine("Key binding fixtures (unity rows)");
            foreach (string row in KeyBindingFixtures.UnityRows(fixtures))
            {
                failures += Report(row.Replace('\t', ' '), dir => KeyBindingFixtures.RunRow(row));
            }

            Console.WriteLine("Value codec fixtures");
            foreach (string row in ValueCodecFixtures.Rows(fixtures))
            {
                failures += Report(row.Replace('\t', ' '), dir => ValueCodecFixtures.RunRow(row));
            }

            Console.WriteLine("Config table fixtures");
            foreach (string name in ConfigTableFixtures.Cases(fixtures))
            {
                failures += Report(name, dir => ConfigTableFixtures.RunCase(fixtures, name));
            }

            Console.WriteLine("Head-tracking config table fixtures");
            failures += Report("all-concepts.ini", dir => HeadTrackingConfigTableFixtures.RunRender(fixtures));
            foreach (string name in HeadTrackingConfigTableFixtures.Cases)
            {
                failures += Report(name, dir => HeadTrackingConfigTableFixtures.RunCase(fixtures, name));
            }
            failures += Report("the cases move every field", dir => HeadTrackingConfigTableFixtures.RunCoverage(fixtures));

            Console.WriteLine("INI mutation fixtures");
            foreach (string name in IniMutationFixtures.Cases(fixtures))
            {
                failures += Report(name, dir => IniMutationFixtures.RunCase(fixtures, name));
            }

            Console.WriteLine("Float and double sweep");
            failures += Report("floats read back from their render", dir => ValueCodecFixtures.SweepFloats(SweepCount));
            failures += Report("doubles read back from their render", dir => ValueCodecFixtures.SweepDoubles(SweepCount));

            Console.WriteLine(failures == 0 ? "All passed." : failures + " FAILED.");
            return failures == 0 ? 0 : 1;
        }

        private static int Report(string name, Action<string> scenario)
        {
            string dir = CheckedFileWriterScenarios.CreateScratchDirectory();
            try
            {
                scenario(dir);
                Console.WriteLine("  [PASS] " + name);
                return 0;
            }
            catch (Exception e)
            {
                Console.WriteLine("  [FAIL] " + name + ": " + e);
                return 1;
            }
            finally
            {
                CheckedFileWriterScenarios.DeleteScratchDirectory(dir);
            }
        }

        // The process is killed at the start of the step, so every step before it has run.
        private static void KilledDuring(CheckedWriteStep step, string dir)
        {
            string target = Path.Combine(dir, "HeadTracking.ini");
            File.WriteAllBytes(target, Encoding.UTF8.GetBytes("a=1"));

            var start = new ProcessStartInfo(Assembly.GetEntryAssembly().Location,
                InterruptFlag + " " + step + " \"" + dir + "\"")
            {
                UseShellExecute = false,
            };
            using (Process child = Process.Start(start))
            {
                if (!child.WaitForExit(60000)) throw new InvalidOperationException("the child did not exit");
                if (child.ExitCode == 0) throw new InvalidOperationException("the child finished instead of dying");
            }

            if (File.ReadAllText(target) != "a=1") throw new InvalidOperationException("the target changed");
            string[] strays = Directory.GetFiles(dir).Where(p => p != target).ToArray();
            int expectedStrays = step == CheckedWriteStep.CreateTemporary ? 0 : 1;
            if (strays.Length != expectedStrays)
            {
                throw new InvalidOperationException(strays.Length + " files beside the target, expected " + expectedStrays);
            }
            foreach (string stray in strays)
            {
                if (!Path.GetFileName(stray).StartsWith("HeadTracking.ini.", StringComparison.Ordinal))
                {
                    throw new InvalidOperationException("unexpected leftover " + stray);
                }
                File.Delete(stray);
            }

            if (CheckedFileWriter.Write(target, Encoding.UTF8.GetBytes("a=1"), Encoding.UTF8.GetBytes("a=2"))
                != CheckedWriteOutcome.Committed)
            {
                throw new InvalidOperationException("the next write did not commit");
            }
        }

        // The child is killed at the start of the labelled conversion step, so every step before it has run.
        private static void ConversionKilledAt(string label, string dir)
        {
            ConfigOwnerScenarios.PrepareInterruption(dir);
            var start = new ProcessStartInfo(Assembly.GetEntryAssembly().Location,
                OwnerInterruptFlag + " " + label + " \"" + dir + "\"")
            {
                UseShellExecute = false,
            };
            using (Process child = Process.Start(start))
            {
                if (!child.WaitForExit(60000)) throw new InvalidOperationException("the child did not exit");
                if (child.ExitCode == 0) throw new InvalidOperationException("the child finished instead of dying");
            }
            ConfigOwnerScenarios.CheckAfterInterruption(label, dir);
        }

        private static int DieDuring(CheckedWriteStep step, string dir)
        {
            string target = Path.Combine(dir, "HeadTracking.ini");
            CheckedFileWriter.Write(target, Encoding.UTF8.GetBytes("a=1"), Encoding.UTF8.GetBytes("a=2"), (s, p) =>
            {
                if (s == step) Process.GetCurrentProcess().Kill();
            });
            return 0;
        }
    }
}
