#if NETCOREAPP
#nullable disable
#pragma warning disable CA1416
#endif
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Effects;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// <see cref="ConfigOwner{TConfig}"/> against real files. The same source runs under xunit on
    /// net8.0 (ConfigOwnerTests) and in the CameraUnlock.Core.FrameworkTests console on .NET
    /// Framework 3.5 and 4.7.2, which also kills a child copy of itself at each conversion step
    /// (<see cref="InterruptionLabels"/>). C# 7.3 and no test framework, so the net35 build can
    /// compile it.
    /// </summary>
    internal static class ConfigOwnerScenarios
    {
        private const string FileName = "HeadTracking.ini";
        private const string Display = "Test Game";
        private const int HResultGenFailure = unchecked((int)0x8007001F);
        private const int HResultUnableToMoveReplacement = unchecked((int)0x80070498);

        // Line 5 holds a key the legacy reader does not read.
        private const string LegacyText = "; tuned by hand\r\n[General]\r\nPort = 5555\r\nYawWorld = false\r\n"
            + "Smoothng = 0.3\r\n[Position]\r\nPosition = false\r\n";

        private static readonly List<KeyValuePair<string, Action<string>>> All = new List<KeyValuePair<string, Action<string>>>
        {
            Scenario("an-absent-file-is-created", AnAbsentFileIsCreated),
            Scenario("a-file-appearing-during-creation-defers", AFileAppearingDuringCreationDefers),
            Scenario("a-stamped-file-is-canonical", AStampedFileIsCanonical),
            Scenario("a-stamped-utf16-file-is-unreadable-and-never-migrated", AStampedUtf16FileIsUnreadableAndNeverMigrated),
            Scenario("a-stamped-file-holding-a-nul-is-unreadable", AStampedFileHoldingANulIsUnreadable),
            Scenario("an-unstamped-file-with-an-import-is-migrated", AnUnstampedFileWithAnImportIsMigrated),
            Scenario("an-unstamped-utf16-file-with-an-import-is-migrated", AnUnstampedUtf16FileWithAnImportIsMigrated),
            Scenario("an-unstamped-file-holding-a-nul-with-an-import-is-migrated", AnUnstampedFileHoldingANulWithAnImportIsMigrated),
            Scenario("a-second-load-rewrites-nothing", ASecondLoadRewritesNothing),
            Scenario("an-unstamped-file-without-an-import-is-canonical-and-stamped-by-a-save",
                AnUnstampedFileWithoutAnImportIsCanonicalAndStampedByASave),
            Scenario("an-unstamped-unreadable-file-without-an-import-is-unreadable", AnUnstampedUnreadableFileWithoutAnImportIsUnreadable),
            Scenario("a-dropped-value-is-logged", ADroppedValueIsLogged),
            Scenario("a-deleted-stamp-migrates-again-into-pre-canonical-last", ADeletedStampMigratesAgainIntoPreCanonicalLast),
            Scenario("a-bepinex-source-is-migrated-and-left-byte-for-byte", ABepInExSourceIsMigratedAndLeftByteForByte),
            Scenario("a-bepinex-ini-is-read-as-canonical-and-neither-file-is-created", ABepInExIniIsReadAsCanonicalAndNeitherFileIsCreated),
            Scenario("a-refused-import-is-legacy-refused", ARefusedImportIsLegacyRefused),
            Scenario("an-undecodable-import-defers", AnUndecodableImportDefers),
            Scenario("an-absent-import-defers", AnAbsentImportDefers),
            Scenario("a-read-only-file-defers", AReadOnlyFileDefers),
            Scenario("a-folder-that-cannot-be-written-defers", AFolderThatCannotBeWrittenDefers),
            Scenario("a-file-held-denying-read-sharing-defers", AFileHeldDenyingReadSharingDefers),
            Scenario("an-import-that-writes-the-file-defers", AnImportThatWritesTheFileDefers),
            Scenario("a-failed-copy-defers", AFailedCopyDefers),
            Scenario("a-copy-that-does-not-read-back-defers", ACopyThatDoesNotReadBackDefers),
            Scenario("a-verify-mismatch-defers", AVerifyMismatchDefers),
            Scenario("a-value-no-codec-writes-defers", AValueNoCodecWritesDefers),
            Scenario("a-file-changed-before-the-commit-defers", AFileChangedBeforeTheCommitDefers),
            Scenario("the-held-file-reads-and-refuses-exclusive-opens", TheHeldFileReadsAndRefusesExclusiveOpens),
            Scenario("a-newer-config-format-refuses-saves", ANewerConfigFormatRefusesSaves),
            Scenario("a-save-writes-a-missing-or-unreadable-config-format", ASaveWritesAMissingOrUnreadableConfigFormat),
            Scenario("a-save-writes-only-the-changed-row", ASaveWritesOnlyTheChangedRow),
            Scenario("a-mode-change-writes-both-rows", AModeChangeWritesBothRows),
            Scenario("a-table-marking-one-mode-row-writable-is-refused", ATableMarkingOneModeRowWritableIsRefused),
            Scenario("a-save-with-nothing-changed-writes-nothing", ASaveWithNothingChangedWritesNothing),
            Scenario("a-save-conflict-is-not-saved", ASaveConflictIsNotSaved),
            Scenario("a-change-to-a-row-that-is-not-writable-throws", AChangeToARowThatIsNotWritableThrows),
            Scenario("a-save-of-a-missing-file-creates-nothing", ASaveOfAMissingFileCreatesNothing),
            Scenario("a-save-to-a-legacy-file-is-refused", ASaveToALegacyFileIsRefused),
            Scenario("a-save-to-a-read-only-file-is-not-saved", ASaveToAReadOnlyFileIsNotSaved),
            Scenario("an-unfinished-save-is-uncertain", AnUnfinishedSaveIsUncertain),
            Scenario("reload-ignores-the-owners-own-writes", ReloadIgnoresTheOwnersOwnWrites),
            Scenario("reload-of-an-old-file-is-read-only", ReloadOfAnOldFileIsReadOnly),
            Scenario("reload-of-an-unreadable-file-keeps-the-settings", ReloadOfAnUnreadableFileKeepsTheSettings),
            Scenario("reload-of-an-old-file-the-import-cannot-find-keeps-the-settings",
                ReloadOfAnOldFileTheImportCannotFindKeepsTheSettings),
            Scenario("reload-of-an-old-file-changed-during-the-import-keeps-the-settings",
                ReloadOfAnOldFileChangedDuringTheImportKeepsTheSettings),
            Scenario("options-and-call-order-are-checked", OptionsAndCallOrderAreChecked),
        };

        public static IEnumerable<string> Names
        {
            get { return All.Select(s => s.Key); }
        }

        /// <summary>The steps of an in-place conversion, as the owner's internal hook names them.</summary>
        public static IEnumerable<string> InterruptionLabels
        {
            get
            {
                var labels = new List<string> { "Open", "Import", "Recheck" };
                CheckedWriteStep[] writer =
                {
                    CheckedWriteStep.ReadTarget, CheckedWriteStep.CreateTemporary, CheckedWriteStep.WriteTemporary,
                    CheckedWriteStep.FlushTemporary, CheckedWriteStep.CloseTemporary, CheckedWriteStep.RecheckTarget,
                    CheckedWriteStep.Commit,
                };
                foreach (CheckedWriteStep step in writer) labels.Add("Copy." + step);
                labels.Add("ReadBack");
                foreach (CheckedWriteStep step in writer) labels.Add("Commit." + step);
                labels.Add("Remember");
                return labels;
            }
        }

        public static void Run(string name, string directory)
        {
            foreach (KeyValuePair<string, Action<string>> scenario in All)
            {
                if (scenario.Key == name)
                {
                    scenario.Value(directory);
                    return;
                }
            }
            throw new ArgumentException("no scenario named " + name, nameof(name));
        }

        public static void PrepareInterruption(string dir)
        {
            File.WriteAllBytes(Path.Combine(dir, FileName), Ascii(LegacyText));
        }

        /// <summary>The child's side: the process ends at the start of the labelled step.</summary>
        public static void RunInterruptedChild(string label, string dir)
        {
            var rig = new Rig(dir);
            rig.Hook = (step, path) =>
            {
                if (step == label) Process.GetCurrentProcess().Kill();
            };
            rig.Owner().Load();
        }

        /// <summary>
        /// The parent's side, after the child died at <paramref name="label"/>: the legacy file is
        /// whole, or the new file is, and beside it at most the copy and the writer's temporaries.
        /// The next launch then ends where an uninterrupted one does.
        /// </summary>
        public static void CheckAfterInterruption(string label, string dir)
        {
            string target = Path.Combine(dir, FileName);
            string copy = target + ".pre-canonical";
            bool committed = label == "Remember";
            ExpectBytes(target, committed ? MigratedBytes() : Ascii(LegacyText));
            foreach (string file in Directory.GetFiles(dir))
            {
                string name = Path.GetFileName(file);
                if (file == target) continue;
                if (file == copy)
                {
                    ExpectBytes(copy, Ascii(LegacyText));
                    continue;
                }
                Expect(name.StartsWith(FileName + ".", StringComparison.Ordinal) && name.EndsWith(".tmp", StringComparison.Ordinal),
                    label + ": unexpected leftover " + name);
            }

            var rig = new Rig(dir);
            ConfigLoadResult<HeadTrackingConfigData> next = rig.Owner().Load();
            Expect(next.Status == (committed ? ConfigLoadStatus.Canonical : ConfigLoadStatus.Migrated),
                label + ": the next launch is " + next.Status);
            ExpectBytes(target, MigratedBytes());
            ExpectBytes(copy, Ascii(LegacyText));
            Expect(!File.Exists(target + ".pre-canonical.last"), label + ": no .pre-canonical.last");
        }

        public static string CreateScratchDirectory()
        {
            string path = Path.Combine(Path.GetTempPath(), "cu-config-owner-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(path);
            return path;
        }

        public static void DeleteScratchDirectory(string path)
        {
            foreach (string file in Directory.GetFiles(path, "*", SearchOption.AllDirectories))
            {
                File.SetAttributes(file, FileAttributes.Normal);
            }
            Directory.Delete(path, true);
        }

        private static KeyValuePair<string, Action<string>> Scenario(string name, Action<string> body)
        {
            return new KeyValuePair<string, Action<string>>(name, body);
        }

        private static void AnAbsentFileIsCreated(string dir)
        {
            var rig = new Rig(dir);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectBytes(rig.Path, Render(Defaults()));
            ExpectSame(load.Config, Defaults(), "the session runs on the defaults");
            Expect(rig.Legacy.Runs == 0, "no import runs for an absent file");
            Expect(rig.Sink.Count == 0, "nothing is reported");
            ExpectListing(dir, FileName);
        }

        private static void AFileAppearingDuringCreationDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.Hook = (step, path) =>
            {
                if (step == "Create.RecheckTarget") File.WriteAllBytes(rig.Path, Ascii("theirs"));
            };
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "another program created the file at the same time");
            ExpectSame(load.Config, Defaults(), "the session runs on the defaults");
            ExpectBytes(rig.Path, Ascii("theirs"));
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = false), "the settings file could not be used this session");
            ExpectBytes(rig.Path, Ascii("theirs"));
            ExpectListing(dir, FileName);
        }

        private static void AStampedFileIsCanonical(string dir)
        {
            var rig = new Rig(dir);
            HeadTrackingConfigData chosen = Defaults();
            chosen.WorldSpaceYaw = false;
            chosen.UdpPort = 5000;
            byte[] canonical = Render(chosen);
            File.WriteAllBytes(rig.Path, canonical);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            ExpectSame(load.Config, chosen, "the file's values");
            Expect(load.Diagnostics.Count == 0 && load.Log.Count == 0, "a clean file draws nothing");
            Expect(rig.Legacy.Runs == 0, "the import never runs on a stamped file");
            ExpectBytes(rig.Path, canonical);
            ExpectListing(dir, FileName);
        }

        private static void AStampedUtf16FileIsUnreadableAndNeverMigrated(string dir)
        {
            var rig = new Rig(dir);
            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes(Encoding.ASCII.GetString(Render(Defaults()))))
                .ToArray();
            File.WriteAllBytes(rig.Path, utf16);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Unreadable);
            ExpectContains(load.Reason, "it is saved as UTF-16; save it as ANSI or UTF-8");
            ExpectSame(load.Config, Defaults(), "the session runs on the defaults");
            Expect(rig.Legacy.Runs == 0, "the import never runs on a stamped file");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = false), "the settings file could not be used this session");
            ExpectBytes(rig.Path, utf16);
            ExpectListing(dir, FileName);
        }

        private static void AStampedFileHoldingANulIsUnreadable(string dir)
        {
            var rig = new Rig(dir);
            byte[] bytes = Render(Defaults()).Concat(Ascii("\0\r\n")).ToArray();
            File.WriteAllBytes(rig.Path, bytes);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Unreadable);
            int line = Encoding.ASCII.GetString(bytes).Split('\n').Length - 1;
            ExpectContains(load.Reason, "line " + line.ToString(CultureInfo.InvariantCulture) + " holds a NUL byte");
            Expect(rig.Legacy.Runs == 0, "the import never runs on a stamped file");
            ExpectBytes(rig.Path, bytes);
            ExpectListing(dir, FileName);
        }

        private static void AnUnstampedFileWithAnImportIsMigrated(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectBytes(rig.Path, MigratedBytes());
            ExpectBytes(rig.Path + ".pre-canonical", Ascii(LegacyText));
            Expect(load.Diagnostics.Count == 0, "the new file reads back clean");
            ExpectLogLine(load, rig.Path + ": converted to the canonical format. The original is kept in " + rig.Path
                + ".pre-canonical.");
            ExpectLogLine(load, rig.Path + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
            Expect(load.Log.Count(l => l.Contains("not carried")) == 1, "only the unread key is listed");
            Expect(rig.Legacy.Runs == 1 && rig.Legacy.Inputs[0].Path == rig.Path && rig.Legacy.Inputs[0].LegacySourcePath == null,
                "the import runs once on the file itself");
            Expect(rig.Sink.Count == 0, "nothing is reported");
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        private static void AnUnstampedUtf16FileWithAnImportIsMigrated(string dir)
        {
            var rig = new Rig(dir);
            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes(LegacyText)).ToArray();
            File.WriteAllBytes(rig.Path, utf16);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectBytes(rig.Path, MigratedBytes());
            ExpectBytes(rig.Path + ".pre-canonical", utf16);
            ExpectLogLine(load, rig.Path + ": is saved as UTF-16, so its lines this build does not read are not listed; the "
                + "original keeps them.");
            Expect(!load.Log.Any(l => l.Contains("not carried")), "no line of a UTF-16 file is listed");
            Expect(rig.Legacy.Runs == 1 && rig.Sink.Count == 0, "one import and nothing reported");
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        private static void AnUnstampedFileHoldingANulWithAnImportIsMigrated(string dir)
        {
            var rig = new Rig(dir);
            byte[] nul = Ascii(LegacyText + "Extra=1\0\r\n");
            File.WriteAllBytes(rig.Path, nul);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectBytes(rig.Path, MigratedBytes());
            ExpectBytes(rig.Path + ".pre-canonical", nul);
            ExpectLogLine(load, rig.Path + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
            ExpectLogLine(load, rig.Path + ": not carried: [Position] Extra=1\0 on line 8, this build does not read it");
            Expect(load.Log.Count(l => l.Contains("not carried")) == 2, "only the unread keys are listed");
            Expect(rig.Legacy.Runs == 1 && rig.Sink.Count == 0, "one import and nothing reported");
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        private static void ASecondLoadRewritesNothing(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            DateTime written = File.GetLastWriteTimeUtc(rig.Path);
            DateTime copied = File.GetLastWriteTimeUtc(rig.Path + ".pre-canonical");

            var steps = new List<string>();
            rig.Hook = (step, path) => steps.Add(step);
            ConfigLoadResult<HeadTrackingConfigData> again = rig.Owner().Load();
            ExpectStatus(again, ConfigLoadStatus.Canonical);
            ExpectSame(again.Config, MigratedConfig(), "the migrated values");
            Expect(steps.SequenceEqual(new[] { "Open" }), "the second launch only opens the file, got " + string.Join(", ", steps.ToArray()));
            Expect(rig.Legacy.Runs == 1, "the import does not run again");
            ExpectBytes(rig.Path, MigratedBytes());
            Expect(File.GetLastWriteTimeUtc(rig.Path) == written && File.GetLastWriteTimeUtc(rig.Path + ".pre-canonical") == copied,
                "neither file is rewritten");
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        private static void AnUnstampedFileWithoutAnImportIsCanonicalAndStampedByASave(string dir)
        {
            var rig = new Rig(dir) { WithImport = false };
            const string text = "; mine\r\n[General]\r\nWorldSpaceYaw=false\r\n";
            File.WriteAllBytes(rig.Path, Ascii(text));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            Expect(!load.Config.WorldSpaceYaw, "the file's value is read");
            ExpectLogLine(load, rig.Path + ": has no [CameraUnlock] section. It is read as the canonical format, and the next save "
                + "adds the section.");
            ExpectBytes(rig.Path, Ascii(text));

            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = true));
            ExpectBytes(rig.Path, Ascii("; mine\r\n[General]\r\nWorldSpaceYaw=true\r\n\r\n[CameraUnlock]\r\nConfigFormat=1\r\n"));
            Expect(CanonicalIni.HasStamp(File.ReadAllBytes(rig.Path)), "the save stamped the file");
            ExpectListing(dir, FileName);
        }

        private static void AnUnstampedUnreadableFileWithoutAnImportIsUnreadable(string dir)
        {
            var rig = new Rig(dir) { WithImport = false };
            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes("[General]\r\nWorldSpaceYaw=false\r\n")).ToArray();
            File.WriteAllBytes(rig.Path, utf16);
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Unreadable);
            ExpectBytes(rig.Path, utf16);

            byte[] nul = Ascii("[General]\r\nWorldSpaceYaw=false\0\r\n");
            File.WriteAllBytes(rig.Path, nul);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Unreadable);
            ExpectContains(load.Reason, "line 2 holds a NUL byte");
            ExpectBytes(rig.Path, nul);
            ExpectListing(dir, FileName);
        }

        private static void ADroppedValueIsLogged(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText + "Light = NaN\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            Expect(load.Config.Light.Multiplier == HeadFollowLightSettings.DefaultMultiplier, "N2 gives the default");
            ExpectLogLine(load, rig.Path + ": not carried: [Light] LightMultiplier=nan, it is not a finite number, so the default is used");
            Expect(load.Log.Count(l => l.Contains("not carried")) == 2, "the dropped value and the unread key");
        }

        private static void ADeletedStampMigratesAgainIntoPreCanonicalLast(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);

            byte[] stampless = WithoutStamp(File.ReadAllBytes(rig.Path));
            File.WriteAllBytes(rig.Path, stampless);
            ConfigLoadResult<HeadTrackingConfigData> again = rig.Owner().Load();
            ExpectStatus(again, ConfigLoadStatus.Migrated);
            ExpectLogLine(again, rig.Path + ": converted to the canonical format. The original is kept in " + rig.Path
                + ".pre-canonical.last.");
            ExpectBytes(rig.Path + ".pre-canonical", Ascii(LegacyText));
            ExpectBytes(rig.Path + ".pre-canonical.last", stampless);
            Expect(CanonicalIni.HasStamp(File.ReadAllBytes(rig.Path)), "stamped again");

            byte[] later = WithoutStamp(File.ReadAllBytes(rig.Path)).Concat(Ascii("[Extra]\r\nNote=1\r\n")).ToArray();
            File.WriteAllBytes(rig.Path, later);
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            ExpectBytes(rig.Path + ".pre-canonical", Ascii(LegacyText));
            ExpectBytes(rig.Path + ".pre-canonical.last", later);

            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            ExpectBytes(rig.Path + ".pre-canonical.last", later);
            ExpectListing(dir, FileName, FileName + ".pre-canonical", FileName + ".pre-canonical.last");
        }

        private static void ABepInExSourceIsMigratedAndLeftByteForByte(string dir)
        {
            string cfg = Path.Combine(dir, "com.test.plugin.cfg");
            var rig = new Rig(dir, "com.test.plugin.ini") { LegacySource = cfg };
            File.WriteAllBytes(cfg, Ascii(LegacyText));
            DateTime cfgTime = File.GetLastWriteTimeUtc(cfg);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectBytes(rig.Path, MigratedBytes());
            ExpectBytes(cfg, Ascii(LegacyText));
            Expect(File.GetLastWriteTimeUtc(cfg) == cfgTime, "the .cfg is not written");
            Expect(rig.Legacy.Inputs[0].Path == rig.Path && rig.Legacy.Inputs[0].LegacySourcePath == cfg,
                "the import is handed both paths");
            ExpectLogLine(load, rig.Path + ": created from " + cfg + ", which is left as it was.");
            ExpectLogLine(load, cfg + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
            ExpectListing(dir, "com.test.plugin.cfg", "com.test.plugin.ini");

            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Canonical);
            Expect(rig.Legacy.Runs == 1, "a present .ini is never imported");

            File.Delete(rig.Path);
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            Expect(rig.Legacy.Runs == 2, "deleting the .ini converts the .cfg again");
            ExpectBytes(cfg, Ascii(LegacyText));
            ExpectListing(dir, "com.test.plugin.cfg", "com.test.plugin.ini");
        }

        private static void ABepInExIniIsReadAsCanonicalAndNeitherFileIsCreated(string dir)
        {
            string cfg = Path.Combine(dir, "com.test.plugin.cfg");
            var rig = new Rig(dir, "com.test.plugin.ini") { LegacySource = cfg };
            ConfigLoadResult<HeadTrackingConfigData> created = rig.Owner().Load();
            ExpectStatus(created, ConfigLoadStatus.Created);
            ExpectBytes(rig.Path, Render(Defaults()));
            ExpectListing(dir, "com.test.plugin.ini");

            File.WriteAllBytes(cfg, Ascii(LegacyText));
            File.WriteAllBytes(rig.Path, WithoutStamp(Render(Defaults())));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            Expect(rig.Legacy.Runs == 0, "the import reads only the .cfg");
            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = false));
            Expect(CanonicalIni.HasStamp(File.ReadAllBytes(rig.Path)), "the save stamps the .ini");
            ExpectBytes(cfg, Ascii(LegacyText));
        }

        private static void ARefusedImportIsLegacyRefused(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config => ImportResult.Refused("Port=99999 is outside 1 to 65535");
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.LegacyRefused);
            ExpectContains(load.Reason, "Port=99999 is outside 1 to 65535");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = true), "the settings file could not be used this session");
            ExpectUntouched(rig);
        }

        private static void AnUndecodableImportDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config => ImportResult.Undecodable("the file is not UTF-8");
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the file is not UTF-8");
            ExpectSunkOnce(rig, load.Reason);
            ExpectUntouched(rig);
        }

        private static void AnAbsentImportDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config => ImportResult.Absent(new DroppedValue[0]);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the old settings reader could not find the file");
            ExpectLogLine(load, rig.Path + ": the old settings reader found no file, while the owner holds it open ("
                + LegacyText.Length.ToString(CultureInfo.InvariantCulture) + " bytes)");
            ExpectSunkOnce(rig, load.Reason);
            ExpectUntouched(rig);
        }

        private static void AReadOnlyFileDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            File.SetAttributes(rig.Path, FileAttributes.ReadOnly);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the file is read-only");
            ExpectSame(load.Config, MigratedConfig(), "the session runs on what the import gave");
            ExpectSunkOnce(rig, load.Reason);
            ExpectBytes(rig.Path, Ascii(LegacyText));
            ExpectBytes(rig.Path + ".pre-canonical", Ascii(LegacyText));
            ExpectListing(dir, FileName, FileName + ".pre-canonical");

            File.SetAttributes(rig.Path, FileAttributes.Normal);
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            ExpectBytes(rig.Path, MigratedBytes());
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        private static void AFolderThatCannotBeWrittenDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            var folder = new DirectoryInfo(dir);
            DirectorySecurity security = folder.GetAccessControl();
            var deny = new FileSystemAccessRule(WindowsIdentity.GetCurrent().User, FileSystemRights.CreateFiles, AccessControlType.Deny);
            security.AddAccessRule(deny);
            folder.SetAccessControl(security);
            ConfigLoadResult<HeadTrackingConfigData> load;
            try
            {
                load = rig.Owner().Load();
            }
            finally
            {
                security.RemoveAccessRule(deny);
                folder.SetAccessControl(security);
            }
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the folder cannot be written");
            ExpectSunkOnce(rig, load.Reason);
            ExpectUntouched(rig);
        }

        private static void AFileHeldDenyingReadSharingDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load;
            using (new FileStream(rig.Path, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            {
                load = rig.Owner().Load();
            }
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the file is in use by another program");
            ExpectSame(load.Config, Defaults(), "a file that cannot be opened cannot be classified, so the defaults");
            Expect(rig.Legacy.Runs == 0, "the import does not run");
            ExpectSunkOnce(rig, load.Reason);
            ExpectUntouched(rig);
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
        }

        private static void AnImportThatWritesTheFileDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            rig.Legacy.During = input => File.WriteAllBytes(input.Path, Ascii(LegacyText + "Light = 2.0\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the file was changed by another program while it was read");
            ExpectSunkOnce(rig, load.Reason);
            ExpectBytes(rig.Path, Ascii(LegacyText + "Light = 2.0\r\n"));
            ExpectListing(dir, FileName);
        }

        private static void AFailedCopyDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            rig.Hook = (step, path) =>
            {
                if (step == "Copy.WriteTemporary") throw new IOException("injected copy failure", HResultGenFailure);
            };
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "injected copy failure");
            Expect(load.Log.Any(l => l.Contains(rig.Path + ".pre-canonical") && l.Contains("WriteTemporary")),
                "the log names the copy and the step");
            ExpectSunkOnce(rig, load.Reason);
            ExpectUntouched(rig);
        }

        private static void ACopyThatDoesNotReadBackDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            rig.Hook = (step, path) =>
            {
                if (step == "ReadBack") File.WriteAllBytes(path, Ascii("damaged"));
            };
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the copy of the original file could not be written");
            ExpectLogLine(load, rig.Path + ".pre-canonical: does not hold the bytes just written to it");
            ExpectSunkOnce(rig, load.Reason);
            ExpectBytes(rig.Path, Ascii(LegacyText));
        }

        private static void AVerifyMismatchDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config =>
            {
                config.RotationEnabled = false;
                config.PositionEnabled = false;
                return ImportResult.Imported(new DroppedValue[0]);
            };
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "[General] RotationEnabled=false cannot be converted");
            ExpectLogLine(load, rig.Path + ": [General] RotationEnabled reads back from the new format as true, not false");
            Expect(!load.Config.RotationEnabled && !load.Config.PositionEnabled, "the session runs on what the import gave");
            ExpectUntouched(rig);
        }

        private static void AValueNoCodecWritesDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText + "Light = 7.5\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "[Light] LightMultiplier=7.5 cannot be converted");
            Expect(load.Config.Light.Multiplier == 7.5f, "the session runs on what the import gave");
            ExpectBytes(rig.Path, Ascii(LegacyText + "Light = 7.5\r\n"));
            ExpectListing(dir, FileName);
        }

        private static void AFileChangedBeforeTheCommitDefers(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            rig.Hook = (step, path) =>
            {
                if (step == "Commit.RecheckTarget") File.WriteAllBytes(rig.Path, Ascii("edited"));
            };
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the file was changed by another program at the same time");
            ExpectBytes(rig.Path, Ascii("edited"));
            ExpectBytes(rig.Path + ".pre-canonical", Ascii(LegacyText));
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        // Design 4.5 step 1 (R3-2): while the owner holds the legacy file, the readers imports use
        // read it, and nothing can newly lock it, rename it or delete it.
        private static void TheHeldFileReadsAndRefusesExclusiveOpens(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            string[] lines = null;
            string streamed = null;
            var refused = new List<string>();
            rig.Legacy.During = input =>
            {
                lines = File.ReadAllLines(input.Path);
                using (var reader = new StreamReader(input.Path)) streamed = reader.ReadToEnd();
                refused.Add(Refusal(() => new FileStream(input.Path, FileMode.Open, FileAccess.Read, FileShare.None).Dispose()));
                refused.Add(Refusal(() => File.Delete(input.Path)));
                refused.Add(Refusal(() => File.Move(input.Path, input.Path + ".moved")));
            };
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            Expect(lines != null && lines.Length == 7 && lines[2] == "Port = 5555", "File.ReadAllLines reads the held file");
            Expect(streamed == LegacyText, "a StreamReader reads the held file");
            Expect(refused.All(r => r != null), "a FileShare.None open, a delete and a rename all fail: "
                + string.Join(" | ", refused.Select(r => r ?? "succeeded").ToArray()));
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
        }

        private static void ANewerConfigFormatRefusesSaves(string dir)
        {
            var rig = new Rig(dir);
            byte[] newer = Ascii(Encoding.ASCII.GetString(Render(Defaults())).Replace("ConfigFormat=1", "ConfigFormat=2"));
            File.WriteAllBytes(rig.Path, newer);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            Expect(load.Diagnostics.Any(d => d.Kind == CanonicalDiagnosticKind.ConfigFormatNewer), "the reader warns");
            Expect(rig.Legacy.Runs == 0, "a stamped file is never imported");
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = false), FileName + " was written by a newer version of the mod");
            ExpectSunkOnce(rig, "Settings not saved: " + FileName + " was written by a newer version of the mod.");
            ExpectBytes(rig.Path, newer);
        }

        private static void ASaveWritesAMissingOrUnreadableConfigFormat(string dir)
        {
            string canonical = Encoding.ASCII.GetString(Render(Defaults()));
            string missing = canonical.Replace("ConfigFormat=1\r\n", "");
            string yawOff = canonical.Replace("WorldSpaceYaw=true", "WorldSpaceYaw=false");
            var cases = new[]
            {
                new { File = missing, Kind = CanonicalDiagnosticKind.ConfigFormatMissing,
                    Saved = yawOff.Replace("ConfigFormat=1\r\n", "").Replace("[CameraUnlock]\r\n", "[CameraUnlock]\r\nConfigFormat=1\r\n") },
                new { File = canonical.Replace("ConfigFormat=1", "ConfigFormat=abc"), Kind = CanonicalDiagnosticKind.ConfigFormatInvalid,
                    Saved = yawOff },
            };
            foreach (var test in cases)
            {
                var rig = new Rig(dir);
                File.WriteAllBytes(rig.Path, Ascii(test.File));
                ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
                ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
                ExpectStatus(load, ConfigLoadStatus.Canonical);
                Expect(load.Diagnostics.Any(d => d.Kind == test.Kind), "the reader warns " + test.Kind);
                ExpectSaved(owner.Save(c => c.WorldSpaceYaw = true));
                ExpectBytes(rig.Path, Ascii(test.File));
                ExpectSaved(owner.Save(c => c.WorldSpaceYaw = false));
                ExpectBytes(rig.Path, Ascii(test.Saved));
                Expect(rig.Owner().Load().Diagnostics.Count == 0, "the saved file reads clean");
                Expect(rig.Legacy.Runs == 0 && rig.Sink.Count == 0, "no import and nothing reported");
                ExpectListing(dir, FileName);
            }
        }

        private static void ASaveWritesOnlyTheChangedRow(string dir)
        {
            var rig = new Rig(dir);
            string canonical = Encoding.ASCII.GetString(Render(Defaults()));
            string mine = canonical.Replace("[General]\r\n", "[General]\r\n; my note\r\nMyOwnKey=1\r\n");
            File.WriteAllBytes(rig.Path, Ascii(mine));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            Expect(load.Diagnostics.Any(d => d.Kind == CanonicalDiagnosticKind.UnknownKey), "the unknown key is reported");

            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = false));
            ExpectBytes(rig.Path, Ascii(mine.Replace("WorldSpaceYaw=true", "WorldSpaceYaw=false")));
            ExpectListing(dir, FileName);
        }

        private static void AModeChangeWritesBothRows(string dir)
        {
            var rig = new Rig(dir);
            string canonical = Encoding.ASCII.GetString(Render(Defaults()));
            Expect(canonical.Contains("RotationEnabled=true\r\n"), "the render holds RotationEnabled");
            string without = canonical.Replace("RotationEnabled=true\r\n", "");
            File.WriteAllBytes(rig.Path, Ascii(without));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Canonical);

            ExpectSaved(owner.Save(c => c.PositionEnabled = false));
            string saved = Encoding.ASCII.GetString(File.ReadAllBytes(rig.Path));
            Expect(saved.Contains("PositionEnabled=false\r\n") && saved.Contains("RotationEnabled=true\r\n"),
                "both rows of the mode are written:\n" + saved);
        }

        private static void ATableMarkingOneModeRowWritableIsRefused(string dir)
        {
            string path = Path.Combine(dir, FileName);
            ConfigOwnerOptions<HeadTrackingConfigData> positionOnly = Options(path);
            positionOnly.Table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.RotationEnabled,
                ConfigConcepts.PositionEnabled).Select(ConfigConcepts.PositionEnabled).Writable();
            ArgumentException e = ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(positionOnly),
                "PositionEnabled is Writable and RotationEnabled is not");
            ExpectContains(e.Message, "the table marks [Position] PositionEnabled Writable but not [General] RotationEnabled");
            ConfigOwnerOptions<HeadTrackingConfigData> rotationOnly = Options(path);
            rotationOnly.Table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.RotationEnabled,
                ConfigConcepts.PositionEnabled).Select(ConfigConcepts.RotationEnabled).Writable();
            e = ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(rotationOnly),
                "RotationEnabled is Writable and PositionEnabled is not");
            ExpectContains(e.Message, "the table marks [General] RotationEnabled Writable but not [Position] PositionEnabled");
            ExpectListing(dir);

            ConfigOwnerOptions<HeadTrackingConfigData> twoState = Options(path);
            twoState.Table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.PositionEnabled)
                .Select(ConfigConcepts.PositionEnabled).Writable();
            var owner = new ConfigOwner<HeadTrackingConfigData>(twoState);
            Expect(owner.Load().Status == ConfigLoadStatus.Created, "a table with one mode row is built and loads");
            string created = Encoding.ASCII.GetString(File.ReadAllBytes(path));
            Expect(created.Contains("PositionEnabled=true\r\n") && !created.Contains("RotationEnabled="),
                "the file holds PositionEnabled and no RotationEnabled:\n" + created);
            ExpectSaved(owner.Save(c => c.PositionEnabled = false));
            ExpectBytes(path, Ascii(created.Replace("PositionEnabled=true", "PositionEnabled=false")));
        }

        private static void ASaveWithNothingChangedWritesNothing(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            DateTime written = File.GetLastWriteTimeUtc(rig.Path);
            var steps = new List<string>();
            rig.Hook = (step, path) => steps.Add(step);
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = true);
            ExpectSaved(save);
            Expect(steps.Count == 0, "the writer never ran, got " + string.Join(", ", steps.ToArray()));
            ExpectBytes(rig.Path, Render(Defaults()));
            Expect(File.GetLastWriteTimeUtc(rig.Path) == written, "the file is not rewritten");
        }

        private static void ASaveConflictIsNotSaved(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            byte[] theirs = Render(Defaults()).Concat(Ascii("[Extra]\r\nNote=1\r\n")).ToArray();
            rig.Hook = (step, path) =>
            {
                if (step == "Save.RecheckTarget") File.WriteAllBytes(rig.Path, theirs);
            };
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = false);
            ExpectNotSaved(save, "the file was changed by another program at the same time");
            Expect(save.Error == null, "a conflict carries no error");
            ExpectSunkOnce(rig, save.Reason);
            ExpectBytes(rig.Path, theirs);
            ExpectListing(dir, FileName);
        }

        private static void AChangeToARowThatIsNotWritableThrows(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            InvalidOperationException e = ExpectThrows<InvalidOperationException>(
                () => owner.Save(c => c.EnableOnStartup = !c.EnableOnStartup), "EnableOnStartup is not Writable");
            ExpectContains(e.Message, "[General] EnableOnStartup");
            ExpectBytes(rig.Path, Render(Defaults()));
            Expect(rig.Sink.Count == 0, "a programming error is thrown, not reported");
        }

        private static void ASaveOfAMissingFileCreatesNothing(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            File.Delete(rig.Path);
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = false),
                "the settings file is missing; it is created again at the next launch");
            ExpectListing(dir);
        }

        private static void ASaveToALegacyFileIsRefused(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = true),
                FileName + " is in the old settings format; it is converted at the next launch");
            ExpectBytes(rig.Path, Ascii(LegacyText));
        }

        private static void ASaveToAReadOnlyFileIsNotSaved(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            File.SetAttributes(rig.Path, FileAttributes.ReadOnly);
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = false);
            ExpectNotSaved(save, "the file is read-only");
            Expect(save.Error is CheckedWriteException, "the writer's error is carried, got " + save.Error);
            Expect(save.Log.Any(l => l.Contains(rig.Path) && l.Contains("Commit")), "the log names the file and the step");
            ExpectBytes(rig.Path, Render(Defaults()));
            ExpectListing(dir, FileName);
        }

        private static void AnUnfinishedSaveIsUncertain(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            rig.Hook = (step, path) =>
            {
                if (step == "Save.Commit") throw new IOException("injected", HResultUnableToMoveReplacement);
            };
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = false);
            Expect(save.Status == ConfigSaveStatus.Uncertain, "an unfinished replacement is Uncertain, got " + save.Status);
            Expect(save.TemporaryPath != null && File.Exists(save.TemporaryPath), "the temporary is kept and named");
            ExpectContains(save.Reason, rig.Path);
            ExpectContains(save.Reason, save.TemporaryPath);
            ExpectSunkOnce(rig, save.Reason);
            ExpectBytes(save.TemporaryPath, Ascii(Encoding.ASCII.GetString(Render(Defaults()))
                .Replace("WorldSpaceYaw=true", "WorldSpaceYaw=false")));
            File.Delete(save.TemporaryPath);
            ExpectListing(dir, FileName);
        }

        private static void ReloadIgnoresTheOwnersOwnWrites(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            Expect(!owner.FileChanged(), "the conversion's write is recorded");
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged, "the conversion's bytes reload as Unchanged");

            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = true));
            Expect(!owner.FileChanged(), "the save's write is recorded");
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged, "the save's bytes reload as Unchanged");

            string saved = Encoding.ASCII.GetString(File.ReadAllBytes(rig.Path));
            File.WriteAllBytes(rig.Path, Ascii(saved.Replace("UdpPort=5555", "UdpPort=6000")));
            File.SetLastWriteTimeUtc(rig.Path, File.GetLastWriteTimeUtc(rig.Path).AddSeconds(5));
            Expect(owner.FileChanged(), "an outside edit is seen");
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Applied && reload.Config.UdpPort == 6000 && reload.Config.WorldSpaceYaw,
                "an outside edit is applied");
            Expect(!owner.FileChanged(), "the reload records the write time");
            Expect(rig.Legacy.Runs == 1 && rig.Sink.Count == 0, "no import and nothing reported");
        }

        private static void ReloadOfAnOldFileIsReadOnly(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText.Replace("5555", "7000")));
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.LegacyReadOnly && reload.Config.UdpPort == 7000,
                "an old file put back is read through the import, got " + reload.Status);
            ExpectContains(reload.Reason, "converted at the next launch");
            Expect(rig.Legacy.Runs == 2, "the import ran again");
            ExpectBytes(rig.Path, Ascii(LegacyText.Replace("5555", "7000")));
            ExpectListing(dir, FileName, FileName + ".pre-canonical");
            Expect(rig.Sink.Count == 0, "a read-only reload is not a failure");
        }

        private static void ReloadOfAnUnreadableFileKeepsTheSettings(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes(Encoding.ASCII.GetString(Render(Defaults()))))
                .ToArray();
            File.WriteAllBytes(rig.Path, utf16);
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Unreadable && reload.Config == null, "Unreadable, with no settings");
            ExpectContains(reload.Reason, "it is saved as UTF-16");
            ExpectSunkOnce(rig, reload.Reason);
            Expect(rig.Legacy.Runs == 0, "a stamped file is never imported");
            ExpectBytes(rig.Path, utf16);
        }

        private static void ReloadOfAnOldFileTheImportCannotFindKeepsTheSettings(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            rig.Legacy.Result = config => ImportResult.Absent(new DroppedValue[0]);
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Unreadable && reload.Config == null,
                "an import that finds no file leaves the settings, got " + reload.Status);
            ExpectContains(reload.Reason, "the old settings reader could not find the file");
            Expect(reload.Log.Contains(rig.Path + ": not reloaded: the old settings reader found no file, while the owner holds it "
                + "open (" + LegacyText.Length.ToString(CultureInfo.InvariantCulture) + " bytes)"), "the log gives both views");
            ExpectSunkOnce(rig, reload.Reason);
            Expect(rig.Legacy.Runs == 2, "the import ran on the reload");
            ExpectBytes(rig.Path, Ascii(LegacyText));
        }

        private static void ReloadOfAnOldFileChangedDuringTheImportKeepsTheSettings(string dir)
        {
            var rig = new Rig(dir);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            File.WriteAllBytes(rig.Path, Ascii(LegacyText));
            string changed = LegacyText.Replace("5555", "7000");
            rig.Legacy.During = input => File.WriteAllBytes(input.Path, Ascii(changed));
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Unreadable && reload.Config == null,
                "a file changed while the import read it leaves the settings, got " + reload.Status);
            ExpectContains(reload.Reason, "the file was changed by another program while it was read");
            ExpectSunkOnce(rig, reload.Reason);
            ExpectBytes(rig.Path, Ascii(changed));

            rig.Legacy.During = null;
            rig.Sink.Clear();
            reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.LegacyReadOnly && reload.Config.UdpPort == 7000,
                "the next reload reads the settled file, got " + reload.Status);
            Expect(rig.Sink.Count == 0, "nothing more is reported");
        }

        private static void OptionsAndCallOrderAreChecked(string dir)
        {
            string path = Path.Combine(dir, FileName);
            ExpectThrows<ArgumentNullException>(() => new ConfigOwner<HeadTrackingConfigData>(null), "null options");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(Options(null)), "no path");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(Options(FileName)), "a relative path");
            ConfigOwnerOptions<HeadTrackingConfigData> noTable = Options(path);
            noTable.Table = null;
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(noTable), "no table");
            ConfigOwnerOptions<HeadTrackingConfigData> noHeader = Options(path);
            noHeader.Header = null;
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(noHeader), "no header");
            ConfigOwnerOptions<HeadTrackingConfigData> badHeader = Options(path);
            badHeader.Header = new RenderHeader("ABZÛU");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(badHeader), "a header the renderer refuses");
            ConfigOwnerOptions<HeadTrackingConfigData> sourceWithoutImport = Options(path);
            sourceWithoutImport.LegacySourcePath = Path.Combine(dir, "x.cfg");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(sourceWithoutImport),
                "a legacy source with no import");
            ConfigOwnerOptions<HeadTrackingConfigData> sourceIsPath = Options(path);
            sourceIsPath.Import = new Legacy().Import;
            sourceIsPath.LegacySourcePath = path;
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(sourceIsPath), "a legacy source that is the file");

            var owner = new ConfigOwner<HeadTrackingConfigData>(Options(path));
            ExpectThrows<InvalidOperationException>(() => owner.Save(c => c.WorldSpaceYaw = false), "Save before Load");
            ExpectThrows<InvalidOperationException>(() => owner.Reload(), "Reload before Load");
            ExpectThrows<InvalidOperationException>(() => owner.FileChanged(), "FileChanged before Load");
            ExpectThrows<ArgumentNullException>(() => owner.Save(null), "a null change");
            ExpectListing(dir);
        }

        private static ConfigOwnerOptions<HeadTrackingConfigData> Options(string path)
        {
            return new ConfigOwnerOptions<HeadTrackingConfigData> { Path = path, Table = Table(), Header = new RenderHeader(Display) };
        }

        private static ConfigTable<HeadTrackingConfigData> Table()
        {
            return HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.EnableOnStartup, ConfigConcepts.WorldSpaceYaw,
                    ConfigConcepts.RotationEnabled, ConfigConcepts.PositionEnabled, ConfigConcepts.ToggleKey,
                    ConfigConcepts.LightMultiplier)
                .Select(ConfigConcepts.WorldSpaceYaw).Writable()
                .Select(ConfigConcepts.RotationEnabled).Writable()
                .Select(ConfigConcepts.PositionEnabled).Writable();
        }

        private static HeadTrackingConfigData Defaults()
        {
            HeadTrackingConfigData config = new HeadTrackingConfigData();
            Table().Apply(CanonicalIni.Parse(new byte[0]), config);
            return config;
        }

        private static HeadTrackingConfigData MigratedConfig()
        {
            HeadTrackingConfigData config = Defaults();
            config.UdpPort = 5555;
            config.WorldSpaceYaw = false;
            config.RotationEnabled = true;
            config.PositionEnabled = false;
            return config;
        }

        private static byte[] MigratedBytes()
        {
            return Render(MigratedConfig());
        }

        private static byte[] Render(HeadTrackingConfigData config)
        {
            return Table().Render(config, new RenderHeader(Display));
        }

        private static byte[] WithoutStamp(byte[] canonical)
        {
            string text = Encoding.ASCII.GetString(canonical);
            const string stamp = "[CameraUnlock]\r\n; Written by the mod. Leave this section in place.\r\nConfigFormat=1\r\n\r\n";
            Expect(text.Contains(stamp), "the render holds the stamp block");
            return Ascii(text.Replace(stamp, ""));
        }

        private sealed class Rig
        {
            public readonly string Path;
            public readonly List<string> Sink = new List<string>();
            public readonly Legacy Legacy = new Legacy();
            public Action<string, string> Hook;
            public string LegacySource;
            public bool WithImport = true;

            public Rig(string dir) : this(dir, FileName)
            {
            }

            public Rig(string dir, string name)
            {
                Path = System.IO.Path.Combine(dir, name);
            }

            public ConfigOwner<HeadTrackingConfigData> Owner()
            {
                ConfigOwnerOptions<HeadTrackingConfigData> options = Options(Path);
                options.Import = WithImport ? Legacy.Import : null;
                options.LegacySourcePath = LegacySource;
                options.StatusSink = message => Sink.Add(message);
                return new ConfigOwner<HeadTrackingConfigData>(options, (step, path) =>
                {
                    if (Hook != null) Hook(step, path);
                });
            }
        }

        // A flat legacy reader of the kind the fleet's imports freeze: File.ReadAllLines, one
        // section header per line, the published build's defaults, and N2 on the light multiplier.
        private sealed class Legacy
        {
            public readonly List<LegacyImportInput> Inputs = new List<LegacyImportInput>();
            public readonly LegacyImport<HeadTrackingConfigData> Import;
            public Func<HeadTrackingConfigData, ImportResult> Result;
            public Action<LegacyImportInput> During;

            public Legacy()
            {
                Import = new LegacyImport<HeadTrackingConfigData>(Run, new[]
                {
                    new LegacyKey("General", "Port"),
                    new LegacyKey("General", "YawWorld"),
                    new LegacyKey("Position", "Position"),
                    new LegacyKey("", "Light"),
                });
            }

            public int Runs
            {
                get { return Inputs.Count; }
            }

            private ImportResult Run(LegacyImportInput input, HeadTrackingConfigData config)
            {
                Inputs.Add(input);
                if (During != null) During(input);
                if (Result != null) return Result(config);

                string path = input.LegacySourcePath ?? input.Path;
                int port = 4242;
                bool yawWorld = true;
                bool position = true;
                float light = HeadFollowLightSettings.DefaultMultiplier;
                string section = "";
                foreach (string raw in File.ReadAllLines(path))
                {
                    string line = raw.Trim();
                    if (line.StartsWith("[", StringComparison.Ordinal) && line.EndsWith("]", StringComparison.Ordinal))
                    {
                        section = line.Substring(1, line.Length - 2);
                        continue;
                    }
                    int equals = line.IndexOf('=');
                    if (equals < 0 || line.StartsWith(";", StringComparison.Ordinal)) continue;
                    string key = line.Substring(0, equals).Trim();
                    string value = line.Substring(equals + 1).Trim();
                    if (section == "General" && key == "Port") port = int.Parse(value, CultureInfo.InvariantCulture);
                    if (section == "General" && key == "YawWorld") yawWorld = bool.Parse(value);
                    if (section == "Position" && key == "Position") position = bool.Parse(value);
                    if (key == "Light") light = float.Parse(value, CultureInfo.InvariantCulture);
                }

                var dropped = new List<DroppedValue>();
                config.UdpPort = port;
                config.WorldSpaceYaw = yawWorld;
                config.RotationEnabled = true;
                config.PositionEnabled = position;
                float multiplier = LegacyNormalisations.FiniteOrDefault(light, HeadFollowLightSettings.DefaultMultiplier, "Light",
                    "LightMultiplier", dropped);
                config.Light = new HeadFollowLightSettings { FollowsHead = config.Light.FollowsHead, Multiplier = multiplier };
                return ImportResult.Imported(dropped);
            }
        }

        private static void ExpectStatus(ConfigLoadResult<HeadTrackingConfigData> load, ConfigLoadStatus status)
        {
            Expect(load.Status == status, "expected " + status + ", got " + load.Status + " (" + load.Reason + "); log:\n"
                + string.Join("\n", load.Log.ToArray()));
        }

        private static void ExpectSaved(ConfigSaveResult save)
        {
            Expect(save.Status == ConfigSaveStatus.Saved && save.Reason.Length == 0,
                "expected Saved, got " + save.Status + " (" + save.Reason + ")");
        }

        private static void ExpectNotSaved(ConfigSaveResult save, string why)
        {
            Expect(save.Status == ConfigSaveStatus.NotSaved, "expected NotSaved, got " + save.Status);
            ExpectContains(save.Reason, "Settings not saved: " + why);
        }

        private static void ExpectSunkOnce(Rig rig, string message)
        {
            Expect(rig.Sink.Count == 1 && rig.Sink[0] == message,
                "the sink got [" + string.Join(" | ", rig.Sink.ToArray()) + "], expected [" + message + "]");
        }

        // A deferred conversion leaves the legacy file as it was and writes no copy.
        private static void ExpectUntouched(Rig rig)
        {
            ExpectBytes(rig.Path, Ascii(LegacyText));
            ExpectListing(System.IO.Path.GetDirectoryName(rig.Path), FileName);
        }

        private static void ExpectLogLine(ConfigLoadResult<HeadTrackingConfigData> load, string line)
        {
            Expect(load.Log.Contains(line), "the log lacks \"" + line + "\"; it holds:\n" + string.Join("\n", load.Log.ToArray()));
        }

        private static void ExpectSame(HeadTrackingConfigData actual, HeadTrackingConfigData expected, string what)
        {
            ConfigTable<HeadTrackingConfigData> table = Table();
            for (int i = 0; i < table.RowCount; i++)
            {
                Expect(table.RowEqual(i, actual, expected), what + ": " + table.RowName(i) + " is "
                    + table.RowValueText(i, actual) + ", expected " + table.RowValueText(i, expected));
            }
        }

        // The failure's message, or null when the action succeeded.
        private static string Refusal(Action action)
        {
            try
            {
                action();
                return null;
            }
            catch (IOException e)
            {
                return e.Message;
            }
            catch (UnauthorizedAccessException e)
            {
                return e.Message;
            }
        }

        private static void ExpectContains(string text, string part)
        {
            Expect(text != null && text.Contains(part), "\"" + text + "\" does not contain \"" + part + "\"");
        }

        private static T ExpectThrows<T>(Action action, string what) where T : Exception
        {
            try
            {
                action();
            }
            catch (T e)
            {
                return e;
            }
            throw new InvalidOperationException(what + ": expected " + typeof(T).Name + ", nothing was thrown");
        }

        private static void ExpectBytes(string path, byte[] expected)
        {
            byte[] actual = File.ReadAllBytes(path);
            Expect(actual.SequenceEqual(expected), path + " holds \"" + Encoding.UTF8.GetString(actual) + "\", expected \""
                + Encoding.UTF8.GetString(expected) + "\"");
        }

        private static void ExpectListing(string dir, params string[] names)
        {
            string[] actual = Directory.GetFiles(dir).Select(p => Path.GetFileName(p)).OrderBy(n => n, StringComparer.Ordinal).ToArray();
            string[] expected = names.OrderBy(n => n, StringComparer.Ordinal).ToArray();
            Expect(actual.SequenceEqual(expected), dir + " holds [" + string.Join(", ", actual) + "], expected ["
                + string.Join(", ", expected) + "]");
        }

        private static void Expect(bool condition, string what)
        {
            if (!condition) throw new InvalidOperationException(what);
        }

        private static byte[] Ascii(string text)
        {
            return Encoding.ASCII.GetBytes(text);
        }
    }
}
