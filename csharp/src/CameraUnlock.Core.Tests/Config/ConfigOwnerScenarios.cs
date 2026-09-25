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
    /// Framework 3.5 and 4.7.2, which also kills a child copy of itself at each import step
    /// (<see cref="InterruptionLabels"/>). C# 7.3 and no test framework, so the net35 build can
    /// compile it.
    /// </summary>
    internal static class ConfigOwnerScenarios
    {
        private const string FileName = "CameraUnlock.ini";
        private const string LegacyName = "HeadTracking.ini";
        private const string Display = "Test Game";
        private const int HResultGenFailure = unchecked((int)0x8007001F);
        private const int HResultUnableToMoveReplacement = unchecked((int)0x80070498);

        // Line 5 holds a key the legacy reader does not read.
        private const string LegacyText = "; tuned by hand\r\n[General]\r\nPort = 5555\r\nYawWorld = false\r\n"
            + "Smoothng = 0.3\r\n[Position]\r\nPosition = false\r\n";

        // Set on every legacy file a scenario writes, so any write to it shows as a new time.
        private static readonly DateTime LegacyWriteTime = new DateTime(2020, 1, 2, 3, 4, 5, DateTimeKind.Utc);

        private static readonly List<KeyValuePair<string, Action<string>>> All = new List<KeyValuePair<string, Action<string>>>
        {
            Scenario("an-absent-file-is-created", AnAbsentFileIsCreated),
            Scenario("a-file-appearing-during-creation-defers", AFileAppearingDuringCreationDefers),
            Scenario("a-stamped-file-is-canonical", AStampedFileIsCanonical),
            Scenario("a-stamped-utf16-file-is-unreadable", AStampedUtf16FileIsUnreadable),
            Scenario("a-stamped-file-holding-a-nul-is-unreadable", AStampedFileHoldingANulIsUnreadable),
            Scenario("a-legacy-file-is-imported-and-left-as-it-was", ALegacyFileIsImportedAndLeftAsItWas),
            Scenario("a-utf16-legacy-file-is-imported", AUtf16LegacyFileIsImported),
            Scenario("a-legacy-file-holding-a-nul-is-imported", ALegacyFileHoldingANulIsImported),
            Scenario("a-second-load-rewrites-nothing", ASecondLoadRewritesNothing),
            Scenario("a-config-beside-a-legacy-file-is-read-and-the-import-never-runs",
                AConfigBesideALegacyFileIsReadAndTheImportNeverRuns),
            Scenario("an-unstamped-config-beside-a-legacy-file-is-canonical-and-stamped-by-a-save",
                AnUnstampedConfigBesideALegacyFileIsCanonicalAndStampedByASave),
            Scenario("an-unstamped-file-without-an-import-is-canonical-and-stamped-by-a-save",
                AnUnstampedFileWithoutAnImportIsCanonicalAndStampedByASave),
            Scenario("an-unreadable-config-beside-a-legacy-file-is-unreadable-and-never-imported",
                AnUnreadableConfigBesideALegacyFileIsUnreadableAndNeverImported),
            Scenario("a-dropped-value-is-logged", ADroppedValueIsLogged),
            Scenario("deleting-the-config-imports-the-legacy-file-again", DeletingTheConfigImportsTheLegacyFileAgain),
            Scenario("a-refused-import-is-legacy-refused", ARefusedImportIsLegacyRefused),
            Scenario("an-undecodable-import-defers", AnUndecodableImportDefers),
            Scenario("an-absent-import-defers", AnAbsentImportDefers),
            Scenario("a-read-only-legacy-file-is-imported-and-left-as-it-was", AReadOnlyLegacyFileIsImportedAndLeftAsItWas),
            Scenario("a-folder-that-cannot-be-written-defers", AFolderThatCannotBeWrittenDefers),
            Scenario("a-read-only-legacy-file-that-cannot-be-read-could-not-be-read",
                AReadOnlyLegacyFileThatCannotBeReadCouldNotBeRead),
            Scenario("a-legacy-file-held-denying-read-sharing-defers", ALegacyFileHeldDenyingReadSharingDefers),
            Scenario("a-config-held-denying-read-sharing-defers-and-nothing-is-imported",
                AConfigHeldDenyingReadSharingDefersAndNothingIsImported),
            Scenario("an-import-that-writes-the-file-defers", AnImportThatWritesTheFileDefers),
            Scenario("a-verify-mismatch-defers", AVerifyMismatchDefers),
            Scenario("a-value-no-codec-writes-defers", AValueNoCodecWritesDefers),
            Scenario("a-config-appearing-before-the-commit-defers", AConfigAppearingBeforeTheCommitDefers),
            Scenario("a-config-appearing-with-a-temporary-left-behind-defers", AConfigAppearingWithATemporaryLeftBehindDefers),
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
            Scenario("a-save-to-a-read-only-file-is-not-saved", ASaveToAReadOnlyFileIsNotSaved),
            Scenario("an-unfinished-save-is-uncertain", AnUnfinishedSaveIsUncertain),
            Scenario("reload-ignores-the-owners-own-writes", ReloadIgnoresTheOwnersOwnWrites),
            Scenario("reload-reads-an-unstamped-config-and-never-imports", ReloadReadsAnUnstampedConfigAndNeverImports),
            Scenario("reload-of-an-unreadable-file-keeps-the-settings", ReloadOfAnUnreadableFileKeepsTheSettings),
            Scenario("defaults-ini-absent-is-created-with-the-built-in-values", DefaultsIniAbsentIsCreatedWithTheBuiltInValues),
            Scenario("defaults-ini-under-a-missing-folder-is-not-created", DefaultsIniUnderAMissingFolderIsNotCreated),
            Scenario("defaults-ini-in-a-folder-that-denies-file-creation-is-not-created",
                DefaultsIniInAFolderThatDeniesFileCreationIsNotCreated),
            Scenario("a-packaged-game-reads-defaults-ini-and-never-creates-it", APackagedGameReadsDefaultsIniAndNeverCreatesIt),
            Scenario("defaults-ini-present-is-read", DefaultsIniPresentIsRead),
            Scenario("a-refused-value-is-told-only-where-the-game-takes-it", ARefusedValueIsToldOnlyWhereTheGameTakesIt),
            Scenario("an-unreadable-defaults-ini-gives-the-built-in-values", AnUnreadableDefaultsIniGivesTheBuiltInValues),
            Scenario("defaults-ini-appearing-during-creation-is-read", DefaultsIniAppearingDuringCreationIsRead),
            Scenario("a-migrated-game-writes-default-where-the-import-equals-it", AMigratedGameWritesDefaultWhereTheImportEqualsIt),
            Scenario("a-migrated-game-writes-a-value-where-defaults-ini-differs", AMigratedGameWritesAValueWhereDefaultsIniDiffers),
            Scenario("a-toggle-on-a-default-row-writes-its-value", AToggleOnADefaultRowWritesItsValue),
            Scenario("a-mode-change-from-default-writes-both-rows", AModeChangeFromDefaultWritesBothRows),
            Scenario("end-saves-nothing", EndSavesNothing),
            Scenario("a-save-after-defaults-ini-changed-keeps-default-rows", ASaveAfterDefaultsIniChangedKeepsDefaultRows),
            Scenario("reload-and-file-changed-follow-defaults-ini", ReloadAndFileChangedFollowDefaultsIni),
            Scenario("a-table-off-the-schema-default-is-refused-unless-per-game", ATableOffTheSchemaDefaultIsRefusedUnlessPerGame),
            Scenario("read-only-over-a-config-file", ReadOnlyOverAConfigFile),
            Scenario("read-only-over-a-legacy-file", ReadOnlyOverALegacyFile),
            Scenario("read-only-over-nothing", ReadOnlyOverNothing),
            Scenario("options-and-call-order-are-checked", OptionsAndCallOrderAreChecked),
        };

        public static IEnumerable<string> Names
        {
            get { return All.Select(s => s.Key); }
        }

        /// <summary>
        /// The steps of a first launch with a legacy file and no Defaults.ini, as the owner's internal
        /// hook names them: Defaults.ini's creation, then the import's.
        /// </summary>
        public static IEnumerable<string> InterruptionLabels
        {
            get
            {
                CheckedWriteStep[] writer =
                {
                    CheckedWriteStep.ReadTarget, CheckedWriteStep.CreateTemporary, CheckedWriteStep.WriteTemporary,
                    CheckedWriteStep.FlushTemporary, CheckedWriteStep.CloseTemporary, CheckedWriteStep.RecheckTarget,
                    CheckedWriteStep.Commit,
                };
                var labels = new List<string>();
                foreach (CheckedWriteStep step in writer) labels.Add("Defaults." + step);
                labels.AddRange(new[] { "Open", "Import", "Recheck" });
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
            new Rig(dir).PutLegacy(Ascii(LegacyText));
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
        /// whole and unwritten, Defaults.ini is absent before its commit and whole after it, the
        /// config file is absent before the commit and whole after it, and beside each at most the
        /// writer's temporaries. The next launch then ends where an uninterrupted one does.
        /// </summary>
        public static void CheckAfterInterruption(string label, string dir)
        {
            var rig = new Rig(dir);
            rig.ExpectLegacyKept(Ascii(LegacyText), label);
            if (label.StartsWith("Defaults.", StringComparison.Ordinal))
            {
                Expect(!File.Exists(rig.DefaultsPath), label + ": Defaults.ini exists though the child died before its commit");
            }
            else
            {
                ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
            }
            string global = Path.GetDirectoryName(rig.DefaultsPath);
            if (Directory.Exists(global))
            {
                foreach (string file in Directory.GetFiles(global))
                {
                    string name = Path.GetFileName(file);
                    if (name == "Defaults.ini") continue;
                    Expect(name.StartsWith("Defaults.ini.", StringComparison.Ordinal) && name.EndsWith(".tmp", StringComparison.Ordinal),
                        label + ": unexpected leftover " + name);
                }
            }
            bool committed = label == "Remember";
            if (committed)
            {
                ExpectBytes(rig.Path, MigratedBytes());
            }
            else
            {
                Expect(!File.Exists(rig.Path), label + ": " + FileName + " exists though the child died before the commit");
            }
            foreach (string file in Directory.GetFiles(dir))
            {
                string name = Path.GetFileName(file);
                if (name == FileName || name == LegacyName) continue;
                Expect(name.StartsWith(FileName + ".", StringComparison.Ordinal) && name.EndsWith(".tmp", StringComparison.Ordinal),
                    label + ": unexpected leftover " + name);
            }

            ConfigLoadResult<HeadTrackingConfigData> next = rig.Owner().Load();
            Expect(next.Status == (committed ? ConfigLoadStatus.Canonical : ConfigLoadStatus.Migrated),
                label + ": the next launch is " + next.Status);
            ExpectBytes(rig.Path, MigratedBytes());
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
            rig.ExpectLegacyKept(Ascii(LegacyText), label + ", after the next launch");
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
            ExpectBytes(rig.Path, Fresh());
            ExpectSame(load.Config, Defaults(), "the session runs on the defaults");
            Expect(rig.Legacy.Runs == 0, "no import runs when there is no legacy file");
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
            Expect(load.Diagnostics.Count == 0 && GameLines(load).Length == 0,
                "a clean file with no legacy file beside it draws nothing, got:\n" + string.Join("\n", load.Log.ToArray()));
            Expect(rig.Legacy.Runs == 0, "the import never runs while the config exists");
            ExpectBytes(rig.Path, canonical);
            ExpectListing(dir, FileName);
        }

        private static void AStampedUtf16FileIsUnreadable(string dir)
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
            Expect(rig.Legacy.Runs == 0, "the import never runs while the config exists");
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
            Expect(rig.Legacy.Runs == 0, "the import never runs while the config exists");
            ExpectBytes(rig.Path, bytes);
            ExpectListing(dir, FileName);
        }

        private static void ALegacyFileIsImportedAndLeftAsItWas(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            Expect(load.Diagnostics.Count == 0, "the new file reads back clean");
            ExpectLogLine(load, rig.Path + ": created from " + rig.LegacyPath + ", which is left as it was.");
            ExpectLogLine(load, rig.LegacyPath + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
            Expect(load.Log.Count(l => l.Contains("not carried")) == 1, "only the unread key is listed");
            Expect(rig.Legacy.Runs == 1 && rig.Legacy.Inputs[0].Path == rig.LegacyPath, "the import runs once on the legacy file");
            Expect(rig.Sink.Count == 0, "nothing is reported");
            ExpectImported(rig);
        }

        private static void AUtf16LegacyFileIsImported(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes(LegacyText)).ToArray());
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectLogLine(load, rig.LegacyPath + ": is saved as UTF-16, so its lines this build does not read are not listed; the "
                + "original keeps them.");
            Expect(!load.Log.Any(l => l.Contains("not carried")), "no line of a UTF-16 file is listed");
            Expect(rig.Legacy.Runs == 1 && rig.Sink.Count == 0, "one import and nothing reported");
            ExpectImported(rig);
        }

        private static void ALegacyFileHoldingANulIsImported(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText + "Extra=1\0\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectLogLine(load, rig.LegacyPath + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
            ExpectLogLine(load, rig.LegacyPath + ": not carried: [Position] Extra=1\0 on line 8, this build does not read it");
            Expect(load.Log.Count(l => l.Contains("not carried")) == 2, "only the unread keys are listed");
            Expect(rig.Legacy.Runs == 1 && rig.Sink.Count == 0, "one import and nothing reported");
            ExpectImported(rig);
        }

        private static void ASecondLoadRewritesNothing(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            DateTime written = File.GetLastWriteTimeUtc(rig.Path);

            var steps = new List<string>();
            rig.Hook = (step, path) => steps.Add(step);
            ConfigLoadResult<HeadTrackingConfigData> again = rig.Owner().Load();
            ExpectStatus(again, ConfigLoadStatus.Canonical);
            ExpectSame(again.Config, MigratedConfig(), "the imported values");
            Expect(steps.SequenceEqual(new[] { "Open" }), "the second launch only opens the file, got " + string.Join(", ", steps.ToArray()));
            Expect(rig.Legacy.Runs == 1, "the import does not run again");
            ExpectLogLine(again, rig.Path + ": settings are read from this file. " + rig.LegacyPath + " is left as it was and is not read.");
            Expect(GameLines(again).Length == 1, "that is the only line, got:\n" + string.Join("\n", again.Log.ToArray()));
            Expect(File.GetLastWriteTimeUtc(rig.Path) == written, "the config is not rewritten");
            ExpectImported(rig);
        }

        private static void AConfigBesideALegacyFileIsReadAndTheImportNeverRuns(string dir)
        {
            var rig = new Rig(dir);
            HeadTrackingConfigData chosen = Defaults();
            chosen.UdpPort = 6000;
            byte[] canonical = Render(chosen);
            File.WriteAllBytes(rig.Path, canonical);
            rig.PutLegacy(Ascii(LegacyText));
            var opened = new List<string>();
            rig.Hook = (step, path) => opened.Add(step + " " + path);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            ExpectSame(load.Config, chosen, "the config's values, not the legacy file's");
            Expect(rig.Legacy.Runs == 0, "the import never runs while the config exists");
            Expect(opened.Where(o => !o.StartsWith("Defaults.", StringComparison.Ordinal)).SequenceEqual(new[] { "Open " + rig.Path }),
                "only the config is opened, got " + string.Join(", ", opened.ToArray()));
            ExpectLogLine(load, rig.Path + ": settings are read from this file. " + rig.LegacyPath + " is left as it was and is not read.");
            Expect(GameLines(load).Length == 1, "that is the only line, got:\n" + string.Join("\n", load.Log.ToArray()));
            Expect(rig.Sink.Count == 0, "nothing is reported");
            ExpectBytes(rig.Path, canonical);
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
        }

        private static void AnUnstampedConfigBesideALegacyFileIsCanonicalAndStampedByASave(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            const string text = "; mine\r\n[General]\r\nWorldSpaceYaw=false\r\n";
            File.WriteAllBytes(rig.Path, Ascii(text));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Canonical);
            Expect(!load.Config.WorldSpaceYaw && load.Config.UdpPort != 5555, "the config's values, not the legacy file's");
            Expect(rig.Legacy.Runs == 0, "an unstamped config is never imported");
            ExpectLogLine(load, rig.Path + ": settings are read from this file. " + rig.LegacyPath + " is left as it was and is not read.");
            ExpectLogLine(load, rig.Path + ": has no [CameraUnlock] section. It is read as the canonical format, and the next save "
                + "adds the section.");

            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = true));
            ExpectBytes(rig.Path, Ascii("; mine\r\n[General]\r\nWorldSpaceYaw=true\r\n\r\n[CameraUnlock]\r\nConfigFormat=1\r\n"));
            Expect(rig.Legacy.Runs == 0 && rig.Sink.Count == 0, "no import and nothing reported");
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
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

        private static void AnUnreadableConfigBesideALegacyFileIsUnreadableAndNeverImported(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes("[General]\r\nWorldSpaceYaw=false\r\n")).ToArray();
            File.WriteAllBytes(rig.Path, utf16);
            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Unreadable);
            ExpectBytes(rig.Path, utf16);

            byte[] nul = Ascii("[General]\r\nWorldSpaceYaw=false\0\r\n");
            File.WriteAllBytes(rig.Path, nul);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Unreadable);
            ExpectContains(load.Reason, "line 2 holds a NUL byte");
            ExpectSame(load.Config, Defaults(), "the session runs on the defaults, not the legacy file's values");
            Expect(rig.Legacy.Runs == 0, "the legacy file is not imported while the config exists");
            ExpectBytes(rig.Path, nul);
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
        }

        private static void ADroppedValueIsLogged(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText + "Light = NaN\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            Expect(load.Config.Light.Multiplier == HeadFollowLightSettings.DefaultMultiplier, "N2 gives the default");
            ExpectLogLine(load, rig.LegacyPath + ": not carried: [Light] LightMultiplier=nan, it is not a finite number, so the default is used");
            Expect(load.Log.Count(l => l.Contains("not carried")) == 2, "the dropped value and the unread key");
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
        }

        private static void DeletingTheConfigImportsTheLegacyFileAgain(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = true));

            File.Delete(rig.Path);
            ConfigLoadResult<HeadTrackingConfigData> again = rig.Owner().Load();
            ExpectStatus(again, ConfigLoadStatus.Migrated);
            Expect(rig.Legacy.Runs == 2, "the next load imports the legacy file again");
            ExpectSame(again.Config, MigratedConfig(), "the legacy file's values, without the deleted save");
            ExpectImported(rig);
        }

        private static void ARefusedImportIsLegacyRefused(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config => ImportResult.Refused("Port=99999 is outside 1 to 65535");
            rig.PutLegacy(Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.LegacyRefused);
            string message = LegacyName + " was not imported into " + FileName + ": Port=99999 is outside 1 to 65535. The mod "
                + "tries again at the next launch and saves nothing this session.";
            Expect(load.Reason == message, "the player is told \"" + load.Reason + "\", expected \"" + message + "\"");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = true), "the settings file could not be used this session");
            ExpectNotImported(rig);
        }

        private static void AnUndecodableImportDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config => ImportResult.Undecodable("the file is not UTF-8");
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, LegacyName + " was not imported into " + FileName + ": the file is not UTF-8.");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotImported(rig);
        }

        private static void AnAbsentImportDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.Legacy.Result = config => ImportResult.Absent(new DroppedValue[0]);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the old settings reader could not find the file");
            ExpectLogLine(load, rig.LegacyPath + ": the old settings reader found no file, while the owner holds it open ("
                + LegacyText.Length.ToString(CultureInfo.InvariantCulture) + " bytes)");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotImported(rig);
        }

        private static void AReadOnlyLegacyFileIsImportedAndLeftAsItWas(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            File.SetAttributes(rig.LegacyPath, FileAttributes.ReadOnly);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            Expect(rig.Sink.Count == 0, "nothing is reported");
            Expect((File.GetAttributes(rig.LegacyPath) & FileAttributes.ReadOnly) != 0, "the legacy file is still read-only");
            ExpectImported(rig);
        }

        private static void AFolderThatCannotBeWrittenDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
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
            ExpectSame(load.Config, MigratedConfig(), "the session runs on what the import gave");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotImported(rig);

            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            ExpectImported(rig);
        }

        // The read-only attribute never refuses a read, so a denied read is not blamed on it.
        private static void AReadOnlyLegacyFileThatCannotBeReadCouldNotBeRead(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            File.SetAttributes(rig.LegacyPath, FileAttributes.ReadOnly);
            var legacy = new FileInfo(rig.LegacyPath);
            FileSecurity security = legacy.GetAccessControl();
            var deny = new FileSystemAccessRule(WindowsIdentity.GetCurrent().User, FileSystemRights.ReadData, AccessControlType.Deny);
            security.AddAccessRule(deny);
            legacy.SetAccessControl(security);
            ConfigLoadResult<HeadTrackingConfigData> load;
            try
            {
                load = rig.Owner().Load();
            }
            finally
            {
                security.RemoveAccessRule(deny);
                legacy.SetAccessControl(security);
            }
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, LegacyName + " was not imported into " + FileName + ": it could not be read (");
            ExpectSame(load.Config, Defaults(), "a legacy file that cannot be read cannot be imported, so the defaults");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotImported(rig);

            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            ExpectImported(rig);
        }

        private static void ALegacyFileHeldDenyingReadSharingDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load;
            using (new FileStream(rig.LegacyPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            {
                load = rig.Owner().Load();
            }
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            Expect(load.Reason == LegacyName + " was not imported into " + FileName + ": the file is in use by another program. "
                + "The mod tries again at the next launch and saves nothing this session.", "the reason was: " + load.Reason);
            Expect(load.Log.Any(line => line.StartsWith(rig.LegacyPath + ": could not be opened: ", StringComparison.Ordinal)),
                "the log names the legacy file that could not be opened");
            ExpectSame(load.Config, Defaults(), "a legacy file that cannot be opened cannot be imported, so the defaults");
            Expect(rig.Legacy.Runs == 0, "the import does not run");
            ExpectSunkOnce(rig, load.Reason);
            ExpectNotImported(rig);

            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Migrated);
            ExpectImported(rig);
        }

        private static void AConfigHeldDenyingReadSharingDefersAndNothingIsImported(string dir)
        {
            var rig = new Rig(dir);
            byte[] canonical = Render(Defaults());
            File.WriteAllBytes(rig.Path, canonical);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load;
            using (new FileStream(rig.Path, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            {
                load = rig.Owner().Load();
            }
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, FileName + " cannot be read: the file is in use by another program");
            Expect(rig.Legacy.Runs == 0, "the legacy file is not imported in place of a config that cannot be read");
            ExpectSunkOnce(rig, load.Reason);
            ExpectBytes(rig.Path, canonical);
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
        }

        private static void AnImportThatWritesTheFileDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            rig.Legacy.During = input => File.WriteAllBytes(input.Path, Ascii(LegacyText + "Light = 2.0\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "the file was changed by another program while it was read");
            ExpectSunkOnce(rig, load.Reason);
            ExpectBytes(rig.LegacyPath, Ascii(LegacyText + "Light = 2.0\r\n"));
            ExpectListing(dir, LegacyName);
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
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "[General] RotationEnabled=false cannot be converted");
            ExpectLogLine(load, rig.LegacyPath + ": [General] RotationEnabled reads back from the new format as true, not false");
            Expect(!load.Config.RotationEnabled && !load.Config.PositionEnabled, "the session runs on what the import gave");
            ExpectNotImported(rig);
        }

        private static void AValueNoCodecWritesDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText + "Light = 7.5\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            ExpectContains(load.Reason, "[Light] LightMultiplier=7.5 cannot be converted");
            Expect(load.Config.Light.Multiplier == 7.5f, "the session runs on what the import gave");
            ExpectNotImported(rig);
        }

        // Another program creating the config file between the import and the commit, at each point
        // of the create-if-absent write: before its first read, before its final check, and in the
        // gap between the check and the rename. The next launch reads that file and never imports.
        private static void AConfigAppearingBeforeTheCommitDefers(string dir)
        {
            foreach (string label in new[] { "Commit.ReadTarget", "Commit.RecheckTarget", "Commit.Commit" })
            {
                var rig = new Rig(dir);
                rig.PutLegacy(Ascii(LegacyText));
                rig.Hook = (step, path) =>
                {
                    if (step == label) File.WriteAllBytes(rig.Path, Ascii("theirs"));
                };
                ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
                ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
                ExpectStatus(load, ConfigLoadStatus.Deferred);
                Expect(load.Reason == AppearedReason("another program created the file at the same time"),
                    label + ": the reason was: " + load.Reason);
                ExpectLogLine(load, rig.Path + ": not created: TargetAppeared");
                ExpectSame(load.Config, MigratedConfig(), label + ": the session runs on what the import gave");
                ExpectSunkOnce(rig, load.Reason);
                ExpectNotSaved(owner.Save(c => c.WorldSpaceYaw = true), "the settings file could not be used this session");
                ExpectBytes(rig.Path, Ascii("theirs"));
                rig.ExpectLegacyKept();
                ExpectListing(dir, FileName, LegacyName);

                rig.Hook = null;
                ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.Canonical);
                Expect(rig.Legacy.Runs == 1, label + ": the next launch reads the file that appeared and does not import");
                ExpectBytes(rig.Path, Ascii("theirs"));
                rig.ExpectLegacyKept();
                File.Delete(rig.Path);
            }
        }

        // A temporary that cannot be removed after a config appeared at the commit: the player is
        // told the next launch reads that config, not that it tries the import again.
        private static void AConfigAppearingWithATemporaryLeftBehindDefers(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            rig.Hook = (step, path) =>
            {
                if (step == "Commit.RecheckTarget") File.WriteAllBytes(rig.Path, Ascii("theirs"));
                if (step == "Commit.RemoveTemporary") throw new IOException("injected");
            };
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Deferred);
            Expect(load.Reason == AppearedReason("it could not be written (injected)"), "the reason was: " + load.Reason);
            ExpectSunkOnce(rig, load.Reason);
            ExpectBytes(rig.Path, Ascii("theirs"));
            rig.ExpectLegacyKept();
            string[] temporaries = Directory.GetFiles(dir, FileName + ".*.tmp");
            Expect(temporaries.Length == 1, "one temporary is left, found " + temporaries.Length);
            File.Delete(temporaries[0]);
            ExpectListing(dir, FileName, LegacyName);
        }

        private static string AppearedReason(string why)
        {
            return LegacyName + " was not imported into " + FileName + ": " + why + ". The mod saves nothing this session and reads "
                + FileName + ", not " + LegacyName + ", at the next launch.";
        }

        // Design 4.5 step 1 (R3-2): while the owner holds the legacy file, the readers imports use
        // read it, and nothing can newly lock it, rename it or delete it.
        private static void TheHeldFileReadsAndRefusesExclusiveOpens(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
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
            ExpectImported(rig);
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
            ConfigOwnerOptions<HeadTrackingConfigData> positionOnly = Options(dir, path);
            positionOnly.Table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.RotationEnabled,
                ConfigConcepts.PositionEnabled).Select(ConfigConcepts.PositionEnabled).Writable();
            ArgumentException e = ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(positionOnly),
                "PositionEnabled is Writable and RotationEnabled is not");
            ExpectContains(e.Message, "the table marks [Position] PositionEnabled Writable but not [General] RotationEnabled");
            ConfigOwnerOptions<HeadTrackingConfigData> rotationOnly = Options(dir, path);
            rotationOnly.Table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.RotationEnabled,
                ConfigConcepts.PositionEnabled).Select(ConfigConcepts.RotationEnabled).Writable();
            e = ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(rotationOnly),
                "RotationEnabled is Writable and PositionEnabled is not");
            ExpectContains(e.Message, "the table marks [General] RotationEnabled Writable but not [Position] PositionEnabled");
            ExpectListing(dir);

            ConfigOwnerOptions<HeadTrackingConfigData> twoState = Options(dir, path);
            twoState.Table = HeadTrackingConfigTable.Create(ConfigConcepts.UdpPort, ConfigConcepts.PositionEnabled)
                .Select(ConfigConcepts.PositionEnabled).Writable();
            var owner = new ConfigOwner<HeadTrackingConfigData>(twoState);
            Expect(owner.Load().Status == ConfigLoadStatus.Created, "a table with one mode row is built and loads");
            string created = Encoding.ASCII.GetString(File.ReadAllBytes(path));
            Expect(created.Contains("PositionEnabled=default\r\n") && !created.Contains("RotationEnabled="),
                "the file holds PositionEnabled and no RotationEnabled:\n" + created);
            ExpectSaved(owner.Save(c => c.PositionEnabled = false));
            ExpectBytes(path, Ascii(created.Replace("PositionEnabled=default", "PositionEnabled=false")));
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
            ExpectBytes(rig.Path, Fresh());
            Expect(File.GetLastWriteTimeUtc(rig.Path) == written, "the file is not rewritten");
        }

        private static void ASaveConflictIsNotSaved(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            byte[] theirs = Fresh().Concat(Ascii("[Extra]\r\nNote=1\r\n")).ToArray();
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
            ExpectBytes(rig.Path, Fresh());
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
            ExpectBytes(rig.Path, Fresh());
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
            ExpectBytes(save.TemporaryPath, Ascii(Encoding.ASCII.GetString(Fresh())
                .Replace("WorldSpaceYaw=default", "WorldSpaceYaw=false")));
            File.Delete(save.TemporaryPath);
            ExpectListing(dir, FileName);
        }

        private static void ReloadIgnoresTheOwnersOwnWrites(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);
            Expect(!owner.FileChanged(), "the import's write is recorded");
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged, "the import's bytes reload as Unchanged");

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
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
        }

        private static void ReloadReadsAnUnstampedConfigAndNeverImports(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Migrated);

            string unstamped = Encoding.ASCII.GetString(WithoutStamp(File.ReadAllBytes(rig.Path)));
            File.WriteAllBytes(rig.Path, Ascii(unstamped.Replace("UdpPort=5555", "UdpPort=7000")));
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Applied && reload.Config.UdpPort == 7000,
                "an unstamped config is read as canonical, got " + reload.Status);
            Expect(rig.Legacy.Runs == 1, "the reload does not import");

            File.Delete(rig.Path);
            reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Unreadable && reload.Config == null,
                "a deleted config keeps the settings, got " + reload.Status);
            ExpectContains(reload.Reason, FileName + " is missing, so the current settings stay");
            Expect(rig.Legacy.Runs == 1, "the reload does not import the legacy file in place of a deleted config");
            ExpectSunkOnce(rig, reload.Reason);
            rig.ExpectLegacyKept();
            ExpectListing(dir, LegacyName);
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
            Expect(rig.Legacy.Runs == 0, "the config is never imported");
            ExpectBytes(rig.Path, utf16);
        }

        // Every row of the test table, in table order, as the created Defaults.ini gives it.
        private const string AllFromDefaultsIni = "UdpPort=4242; EnableOnStartup=true; WorldSpaceYaw=true; RotationEnabled=true; "
            + "PositionEnabled=true; ToggleKey=End, Ctrl+Shift+Y; LightMultiplier=1.5";

        private static void DefaultsIniAbsentIsCreatedWithTheBuiltInValues(string dir)
        {
            var rig = new Rig(dir);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
            ExpectBytes(rig.Path, Fresh());
            Expect(Encoding.ASCII.GetString(Fresh()).Contains("\r\nUdpPort=default\r\n"), "a fresh file writes default rows");
            ExpectSame(load.Config, Defaults(), "the session runs on the built-in values");
            ExpectLogLine(load, "Defaults.ini: " + rig.DefaultsPath + " (created with the built-in values)");
            ExpectLogLine(load, rig.Path + ": from Defaults.ini: " + AllFromDefaultsIni);
            Expect(load.Log[0] == "Defaults.ini: " + rig.DefaultsPath + " (created with the built-in values)",
                "the location line comes first");
            Expect(!load.Log.Any(l => l.Contains("set in this file") || l.Contains("built-in, not set")),
                "every row follows Defaults.ini:\n" + string.Join("\n", load.Log.ToArray()));
            Expect(rig.Sink.Count == 0, "nothing is reported");
            ExpectListing(Path.GetDirectoryName(rig.DefaultsPath), "Defaults.ini");

            ConfigLoadResult<HeadTrackingConfigData> again = rig.Owner().Load();
            ExpectStatus(again, ConfigLoadStatus.Canonical);
            ExpectLogLine(again, "Defaults.ini: " + rig.DefaultsPath + " (read)");
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
        }

        private static void DefaultsIniUnderAMissingFolderIsNotCreated(string dir)
        {
            var rig = new Rig(dir);
            string parent = Path.Combine(dir, "missing");
            string folder = Path.Combine(parent, "CameraUnlock");
            rig.Defaults = DefaultsFile.At(Path.Combine(folder, "Defaults.ini"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectLogLine(load, "Defaults.ini: " + folder + " was not created, because " + parent
                + " does not exist. Settings set to default use the built-in values.");
            Expect(load.Log.Count(l => l.StartsWith("Defaults.ini:", StringComparison.Ordinal)) == 1, "one Defaults.ini line");
            ExpectLogLine(load, rig.Path + ": built-in, not set in Defaults.ini: " + AllFromDefaultsIni);
            Expect(!Directory.Exists(parent), "no folder is created above the CameraUnlock folder");
            ExpectBytes(rig.Path, Fresh());
            ExpectSame(load.Config, Defaults(), "the built-in values");
            Expect(rig.Sink.Count == 0, "nothing is reported");
        }

        private static void DefaultsIniInAFolderThatDeniesFileCreationIsNotCreated(string dir)
        {
            var rig = new Rig(dir);
            string global = Path.GetDirectoryName(rig.DefaultsPath);
            Directory.CreateDirectory(global);
            var folder = new DirectoryInfo(global);
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
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectLogLine(load, "Defaults.ini: " + rig.DefaultsPath
                + " was not created: the folder cannot be written. Settings set to default use the built-in values.");
            ExpectListing(global);
            ExpectBytes(rig.Path, Fresh());
            ExpectSame(load.Config, Defaults(), "the built-in values");
            Expect(rig.Sink.Count == 0, "nothing is reported");
        }

        private static void APackagedGameReadsDefaultsIniAndNeverCreatesIt(string dir)
        {
            var rig = new Rig(dir);
            string roaming = Path.Combine(dir, "Roaming");
            Directory.CreateDirectory(roaming);
            var probe = new DefaultsProbe { Platform = DefaultsPlatform.Windows, KnownFolder = roaming, PackageResult = 15703 };
            rig.Defaults = DefaultsFile.Probed(probe);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectLogLine(load, @"Defaults.ini: not created, because this game runs as a packaged app (GetCurrentPackageFullName "
                + @"returned 15703); %AppData%\CameraUnlock\Defaults.ini is created by the next game that is not packaged, or by Lopari.");
            Expect(Directory.GetFileSystemEntries(roaming).Length == 0, "nothing is created in the roaming folder");
            Expect(rig.Sink.Count == 0, "nothing is reported");

            string folder = Path.Combine(roaming, "CameraUnlock");
            Directory.CreateDirectory(folder);
            byte[] theirs = Ascii("[Network]\r\nUdpPort=5000\r\n");
            File.WriteAllBytes(Path.Combine(folder, "Defaults.ini"), theirs);
            ConfigLoadResult<HeadTrackingConfigData> next = rig.Owner().Load();
            ExpectStatus(next, ConfigLoadStatus.Canonical);
            ExpectLogLine(next, @"Defaults.ini: %AppData%\CameraUnlock\Defaults.ini (read)");
            Expect(next.Config.UdpPort == 5000, "a packaged game reads the file that exists");
            ExpectBytes(Path.Combine(folder, "Defaults.ini"), theirs);
        }

        private static void DefaultsIniPresentIsRead(string dir)
        {
            var rig = new Rig(dir);
            byte[] global = Ascii("[Network]\r\nUdpPort=5000\r\n[General]\r\nAimDecoupling=false\r\n[Hotkeys]\r\nToggleKey=F8\r\n");
            rig.PutDefaults(global);
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectBytes(rig.Path, Fresh());
            ExpectBytes(rig.DefaultsPath, global);
            Expect(load.Config.UdpPort == 5000 && load.Config.ToggleKeyName == "F8", "the rows take Defaults.ini's values");
            ExpectLogLine(load, "Defaults.ini: " + rig.DefaultsPath + " (read)");
            ExpectLogLine(load, rig.Path + ": from Defaults.ini: UdpPort=5000; ToggleKey=F8");
            ExpectLogLine(load, rig.Path + ": built-in, not set in Defaults.ini: EnableOnStartup=true; WorldSpaceYaw=true; "
                + "RotationEnabled=true; PositionEnabled=true; LightMultiplier=1.5");
            Expect(!load.Log.Any(l => l.Contains("AimDecoupling")), "a key this table does not bind draws nothing");
            Expect(rig.Sink.Count == 0, "nothing is reported");

            File.WriteAllBytes(rig.Path, Encoding.ASCII.GetBytes(Encoding.ASCII.GetString(Fresh())
                .Replace("UdpPort=default", "UdpPort=6000").Replace("WorldSpaceYaw=default", "WorldSpaceYaw=true")));
            ConfigLoadResult<HeadTrackingConfigData> own = rig.Owner().Load();
            ExpectStatus(own, ConfigLoadStatus.Canonical);
            Expect(own.Config.UdpPort == 6000, "a value in the file wins over Defaults.ini");
            ExpectLogLine(own, rig.Path + ": set in this file, so Defaults.ini does not change them: UdpPort, WorldSpaceYaw.");
        }

        private static void ARefusedValueIsToldOnlyWhereTheGameTakesIt(string dir)
        {
            var rig = new Rig(dir);
            rig.PutDefaults(Ascii("[Hotkeys]\r\nToggleKey=Mouse4\r\nYawModeKey=Mouse5\r\n[Position]\r\nCollisionMargin=abc\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            Expect(load.Config.ToggleKeyName == "End, Ctrl+Shift+Y", "the refused key list gives the built-in");
            ExpectLogLine(load, "Defaults.ini: line 2: [Hotkeys] ToggleKey=Mouse4 is not read (Mouse4 is not one of the key names "
                + "this file takes), so the built-in End, Ctrl+Shift+Y is used.");
            Expect(load.Log.Count(l => l.StartsWith("Defaults.ini: line", StringComparison.Ordinal)) == 1,
                "rows this table does not bind draw no line:\n" + string.Join("\n", load.Log.ToArray()));
            ExpectSunkOnce(rig, "Defaults.ini: 1 setting cannot be used (ToggleKey=Mouse4), so this game uses its built-in "
                + "values for them. The log has the details.");

            File.WriteAllBytes(rig.Path, Ascii(Encoding.ASCII.GetString(Fresh()).Replace("ToggleKey=default", "ToggleKey=Home")));
            rig.Sink.Clear();
            ConfigLoadResult<HeadTrackingConfigData> own = rig.Owner().Load();
            ExpectStatus(own, ConfigLoadStatus.Canonical);
            Expect(!own.Log.Any(l => l.StartsWith("Defaults.ini: line", StringComparison.Ordinal)),
                "a row the file sets itself draws no line for Defaults.ini's value");
            Expect(rig.Sink.Count == 0, "nor a message");
        }

        private static void AnUnreadableDefaultsIniGivesTheBuiltInValues(string dir)
        {
            string text = "[Network]\r\nUdpPort=5000\r\n";
            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes(text)).ToArray();
            byte[] nul = Ascii(text + "\0\r\n");
            foreach (var test in new[]
            {
                new { Bytes = utf16, Why = "it is saved as UTF-16; save it as ANSI or UTF-8" },
                new { Bytes = nul, Why = "line 3 holds a NUL byte" },
            })
            {
                var rig = new Rig(dir);
                if (File.Exists(rig.Path)) File.Delete(rig.Path);
                rig.PutDefaults(test.Bytes);
                ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
                ExpectStatus(load, ConfigLoadStatus.Created);
                Expect(load.Config.UdpPort == 4242, "the built-in port");
                ExpectLogLine(load, "Defaults.ini: " + rig.DefaultsPath + " cannot be read: " + test.Why
                    + ". Settings set to default use the built-in values.");
                ExpectSunkOnce(rig, "Defaults.ini cannot be read: " + test.Why + ". Settings that use it take the built-in values.");
                ExpectBytes(rig.DefaultsPath, test.Bytes);
            }
        }

        private static void DefaultsIniAppearingDuringCreationIsRead(string dir)
        {
            var rig = new Rig(dir);
            byte[] theirs = Ascii("[Network]\r\nUdpPort=6000\r\n");
            rig.Hook = (step, path) =>
            {
                if (step == "Defaults.RecheckTarget") File.WriteAllBytes(rig.DefaultsPath, theirs);
            };
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            ExpectLogLine(load, "Defaults.ini: " + rig.DefaultsPath + " (created by another program at the same time, and read)");
            Expect(load.Config.UdpPort == 6000, "the other program's file is read");
            ExpectBytes(rig.DefaultsPath, theirs);
            ExpectListing(Path.GetDirectoryName(rig.DefaultsPath), "Defaults.ini");
            Expect(rig.Sink.Count == 0, "nothing is reported");
        }

        private static void AMigratedGameWritesDefaultWhereTheImportEqualsIt(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
            string migrated = Encoding.ASCII.GetString(File.ReadAllBytes(rig.Path));
            foreach (string line in new[]
            {
                "UdpPort=5555", "EnableOnStartup=default", "WorldSpaceYaw=false", "RotationEnabled=true", "PositionEnabled=false",
                "ToggleKey=default", "LightMultiplier=default",
            })
            {
                Expect(migrated.Contains("\r\n" + line + "\r\n"), "the migrated file holds " + line + ":\n" + migrated);
            }
            ExpectSame(load.Config, MigratedConfig(), "the imported values");
            ExpectLogLine(load, rig.Path + ": set in this file, so Defaults.ini does not change them: UdpPort, WorldSpaceYaw, "
                + "RotationEnabled, PositionEnabled.");
            ExpectLogLine(load, rig.Path + ": from Defaults.ini: EnableOnStartup=true; ToggleKey=End, Ctrl+Shift+Y; LightMultiplier=1.5");
            ExpectImported(rig);
        }

        private static void AMigratedGameWritesAValueWhereDefaultsIniDiffers(string dir)
        {
            var rig = new Rig(dir);
            rig.PutLegacy(Ascii(LegacyText));
            rig.PutDefaults(Ascii("[Hotkeys]\r\nToggleKey=F8\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Migrated);
            string migrated = Encoding.ASCII.GetString(File.ReadAllBytes(rig.Path));
            Expect(migrated.Contains("\r\nToggleKey=End, Ctrl+Shift+Y\r\n") && migrated.Contains("\r\nEnableOnStartup=default\r\n"),
                "the untouched key list is not what default gives here, so it is written as a value:\n" + migrated);
            Expect(load.Config.ToggleKeyName == "End, Ctrl+Shift+Y", "the player keeps the keys they had");
            rig.ExpectLegacyKept();
        }

        private static void AToggleOnADefaultRowWritesItsValue(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            DateTime defaultsTime = File.GetLastWriteTimeUtc(rig.DefaultsPath);
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = false);
            ExpectSaved(save);
            ExpectBytes(rig.Path, Ascii(Encoding.ASCII.GetString(Fresh()).Replace("WorldSpaceYaw=default", "WorldSpaceYaw=false")));
            Expect(save.Log.SequenceEqual(new[]
            {
                rig.Path + ": WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini.",
            }), "the save names the row it took off Defaults.ini, got:\n" + string.Join("\n", save.Log.ToArray()));
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
            Expect(File.GetLastWriteTimeUtc(rig.DefaultsPath) == defaultsTime, "Defaults.ini is not written");

            save = owner.Save(c => c.WorldSpaceYaw = true);
            ExpectSaved(save);
            Expect(save.Log.Count == 0, "a row that already holds a value draws no line");
            ExpectBytes(rig.Path, Ascii(Encoding.ASCII.GetString(Fresh()).Replace("WorldSpaceYaw=default", "WorldSpaceYaw=true")));
        }

        private static void AModeChangeFromDefaultWritesBothRows(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            ConfigSaveResult save = owner.Save(c => c.PositionEnabled = false);
            ExpectSaved(save);
            ExpectBytes(rig.Path, Ascii(Encoding.ASCII.GetString(Fresh()).Replace("RotationEnabled=default", "RotationEnabled=true")
                .Replace("PositionEnabled=default", "PositionEnabled=false")));
            Expect(save.Log.SequenceEqual(new[]
            {
                rig.Path + ": RotationEnabled=true is now set for this game, and no longer follows Defaults.ini.",
                rig.Path + ": PositionEnabled=false is now set for this game, and no longer follows Defaults.ini.",
            }), "the pair stops following Defaults.ini together, got:\n" + string.Join("\n", save.Log.ToArray()));
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
        }

        // End changes only the session: the mod changes its running config and calls nothing.
        private static void EndSavesNothing(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            load.Config.EnableOnStartup = false;
            Expect(!owner.FileChanged(), "nothing was written");
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged, "the file holds what the owner created");
            ExpectBytes(rig.Path, Fresh());
            ExpectBytes(rig.DefaultsPath, DefaultsIni.Render());
        }

        private static void ASaveAfterDefaultsIniChangedKeepsDefaultRows(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            File.WriteAllBytes(rig.DefaultsPath, Ascii("[Network]\r\nUdpPort=7000\r\n[General]\r\nEnableOnStartup=false\r\n"));
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = false);
            ExpectSaved(save);
            ExpectBytes(rig.Path, Ascii(Encoding.ASCII.GetString(Fresh()).Replace("WorldSpaceYaw=default", "WorldSpaceYaw=false")));
            Expect(rig.Sink.Count == 0, "nothing is reported");
        }

        private static void ReloadAndFileChangedFollowDefaultsIni(string dir)
        {
            var rig = new Rig(dir);
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ExpectStatus(owner.Load(), ConfigLoadStatus.Created);
            Expect(!owner.FileChanged(), "nothing changed yet");

            rig.PutDefaults(Ascii("[Network]\r\nUdpPort=7000\r\n"));
            File.SetLastWriteTimeUtc(rig.DefaultsPath, DateTime.UtcNow.AddSeconds(5));
            Expect(owner.FileChanged(), "an edit to Defaults.ini is seen");
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Applied && reload.Config.UdpPort == 7000,
                "the edit is applied, got " + reload.Status);
            Expect(reload.Log.Contains("Defaults.ini: " + rig.DefaultsPath + " (read)"), "the reload names the file it read");
            Expect(reload.Log.Contains(rig.Path + ": from Defaults.ini: UdpPort=7000"), "and the rows that took its values:\n"
                + string.Join("\n", reload.Log.ToArray()));
            Expect(!owner.FileChanged(), "the reload records the write time");
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged, "the same bytes again are Unchanged");

            byte[] utf16 = Encoding.Unicode.GetPreamble().Concat(Encoding.Unicode.GetBytes("[Network]\r\nUdpPort=8000\r\n")).ToArray();
            File.WriteAllBytes(rig.DefaultsPath, utf16);
            File.SetLastWriteTimeUtc(rig.DefaultsPath, DateTime.UtcNow.AddSeconds(10));
            Expect(owner.FileChanged(), "a save as UTF-16 is seen");
            reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Unchanged, "the values stay, got " + reload.Status);
            string keep = "Defaults.ini cannot be read: it is saved as UTF-16; save it as ANSI or UTF-8. Settings that use it keep the "
                + "values they had until the game restarts.";
            ExpectSunkOnce(rig, keep);
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged && rig.Sink.Count == 1, "the message comes once");
            Expect(!owner.FileChanged(), "the time is recorded");
            ConfigSaveResult save = owner.Save(c => c.WorldSpaceYaw = false);
            ExpectSaved(save);
            Expect(File.ReadAllText(rig.Path).Contains("\r\nUdpPort=default\r\n"), "an untouched row stays default");

            File.Delete(rig.DefaultsPath);
            Expect(owner.FileChanged(), "a deleted Defaults.ini is seen");
            rig.Sink.Clear();
            reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Unchanged, "the values stay, got " + reload.Status);
            ExpectSunkOnce(rig, "Defaults.ini is missing. Settings that use it keep the values they had until the game restarts.");
            Expect(owner.Reload().Status == ConfigReloadStatus.Unchanged && rig.Sink.Count == 1, "the message comes once");
            ExpectSaved(owner.Save(c => c.WorldSpaceYaw = true));
        }

        private static void ATableOffTheSchemaDefaultIsRefusedUnlessPerGame(string dir)
        {
            var rig = new Rig(dir);
            rig.Table = new ConfigTable<HeadTrackingConfigData>(() => new HeadTrackingConfigData { UdpPort = 5000 })
                .Concept(ConfigConcepts.UdpPort, c => c.UdpPort, (c, v) => c.UdpPort = v);
            ArgumentException e = ExpectThrows<ArgumentException>(() => rig.Owner(), "a row off the schema's default");
            ExpectContains(e.Message, "[Network] UdpPort defaults to 5000, and the schema to 4242.");

            rig.Table = new ConfigTable<HeadTrackingConfigData>(() => new HeadTrackingConfigData { UdpPort = 5000 })
                .Concept(ConfigConcepts.UdpPort, c => c.UdpPort, (c, v) => c.UdpPort = v).PerGame();
            rig.PutDefaults(Ascii("[Network]\r\nUdpPort=7000\r\n"));
            ConfigLoadResult<HeadTrackingConfigData> load = rig.Owner().Load();
            ExpectStatus(load, ConfigLoadStatus.Created);
            Expect(load.Config.UdpPort == 5000, "a PerGame row keeps its own default");
            Expect(File.ReadAllText(rig.Path).Contains("\r\nUdpPort=5000\r\n"), "and a fresh file writes its value");
            Expect(!load.Log.Any(l => l.StartsWith(rig.Path + ": ", StringComparison.Ordinal) && l.Contains("Defaults.ini")),
                "a PerGame row is named in no Defaults.ini line");
        }

        private static void ReadOnlyOverAConfigFile(string dir)
        {
            var rig = new Rig(dir) { Platform = PlatformID.Unix };
            rig.PutLegacy(Ascii(LegacyText));
            byte[] canonical = Ascii(Encoding.ASCII.GetString(Fresh()).Replace("UdpPort=default", "UdpPort=5000"));
            File.WriteAllBytes(rig.Path, canonical);
            rig.PutDefaults(Ascii("[General]\r\nWorldSpaceYaw=false\r\n"));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.ReadOnly);
            Expect(load.Config.UdpPort == 5000 && !load.Config.WorldSpaceYaw, "the file over Defaults.ini");
            ExpectLogLine(load, "Defaults.ini: " + rig.DefaultsPath + " (read)");
            ExpectLogLine(load, ReadOnlyLine);
            Expect(load.Reason == ReadOnlyLine, "the player is told, got: " + load.Reason);
            ExpectSunkOnce(rig, ReadOnlyLine);
            ExpectReadOnlySaves(rig, owner);

            File.WriteAllBytes(rig.Path, Ascii(Encoding.ASCII.GetString(canonical).Replace("UdpPort=5000", "UdpPort=6000")));
            File.SetLastWriteTimeUtc(rig.Path, DateTime.UtcNow.AddSeconds(5));
            Expect(owner.FileChanged(), "an edit is seen");
            ConfigReloadResult<HeadTrackingConfigData> reload = owner.Reload();
            Expect(reload.Status == ConfigReloadStatus.Applied && reload.Config.UdpPort == 6000, "and applied");
            rig.ExpectLegacyKept();
            ExpectListing(dir, FileName, LegacyName);
        }

        private static void ReadOnlyOverALegacyFile(string dir)
        {
            var rig = new Rig(dir) { Platform = PlatformID.Unix };
            rig.PutLegacy(Ascii(LegacyText));
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.ReadOnly);
            ExpectSame(load.Config, MigratedConfig(), "the import, read back in memory");
            ExpectLogLine(load, "Defaults.ini: no file at " + rig.DefaultsPath
                + "; on this system the mod reads Defaults.ini but does not create it. Settings set to default use the built-in values.");
            ExpectLogLine(load, rig.LegacyPath + ": not carried: [General] Smoothng=0.3 on line 5, this build does not read it");
            ExpectSunkOnce(rig, ReadOnlyLine);
            ExpectReadOnlySaves(rig, owner);
            Expect(!Directory.Exists(Path.GetDirectoryName(rig.DefaultsPath)), "no Defaults.ini folder is created");
            ExpectNotImported(rig);

            ExpectStatus(rig.Owner().Load(), ConfigLoadStatus.ReadOnly);
            Expect(rig.Legacy.Runs == 2, "the next start imports again");
        }

        private static void ReadOnlyOverNothing(string dir)
        {
            var rig = new Rig(dir) { Platform = PlatformID.Unix };
            ConfigOwner<HeadTrackingConfigData> owner = rig.Owner();
            ConfigLoadResult<HeadTrackingConfigData> load = owner.Load();
            ExpectStatus(load, ConfigLoadStatus.ReadOnly);
            ExpectSame(load.Config, Defaults(), "the built-in values");
            ExpectSunkOnce(rig, ReadOnlyLine);
            ExpectReadOnlySaves(rig, owner);
            Expect(owner.Reload().Status == ConfigReloadStatus.Unreadable, "a reload finds no file and writes none");
            ExpectListing(dir);
            Expect(!Directory.Exists(Path.GetDirectoryName(rig.DefaultsPath)), "no Defaults.ini folder is created");
        }

        private const string ReadOnlyLine = "Settings are read but not saved on this system: this version saves settings only on "
            + "Windows, including under Wine and Proton. Changes made in game last until the game closes.";

        private static void ExpectReadOnlySaves(Rig rig, ConfigOwner<HeadTrackingConfigData> owner)
        {
            byte[] before = File.Exists(rig.Path) ? File.ReadAllBytes(rig.Path) : null;
            rig.Sink.Clear();
            foreach (Action<HeadTrackingConfigData> change in new Action<HeadTrackingConfigData>[]
            {
                c => c.WorldSpaceYaw = !c.WorldSpaceYaw,
                c => c.PositionEnabled = !c.PositionEnabled,
                c => { },
            })
            {
                ConfigSaveResult save = owner.Save(change);
                Expect(save.Status == ConfigSaveStatus.NotSaved
                    && save.Reason == "Settings not saved: this version saves settings only on Windows.",
                    "every save is NotSaved, got " + save.Status + " (" + save.Reason + ")");
            }
            Expect(rig.Sink.Count == 3, "each is shown");
            if (before == null) Expect(!File.Exists(rig.Path), "no file is created");
            else ExpectBytes(rig.Path, before);
        }

        private static void OptionsAndCallOrderAreChecked(string dir)
        {
            string path = Path.Combine(dir, FileName);
            ConfigOwnerOptions<HeadTrackingConfigData> noDefaults = Options(dir, path);
            noDefaults.Defaults = null;
            ArgumentException missing = ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(noDefaults),
                "no Defaults");
            ExpectContains(missing.Message, "DefaultsFile.PerUser()");
            ExpectContains(missing.Message, "DefaultsFile.At(path)");
            ExpectThrows<ArgumentException>(() => DefaultsFile.At("Defaults.ini"), "a relative Defaults.ini path");
            ExpectThrows<ArgumentException>(() => DefaultsFile.At(""), "an empty Defaults.ini path");
            ExpectThrows<ArgumentNullException>(() => DefaultsFile.At(null), "a null Defaults.ini path");
            ExpectThrows<ArgumentNullException>(() => new ConfigOwner<HeadTrackingConfigData>(null), "null options");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(Options(dir, null)), "no path");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(Options(dir, FileName)), "a relative path");
            ConfigOwnerOptions<HeadTrackingConfigData> noTable = Options(dir, path);
            noTable.Table = null;
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(noTable), "no table");
            ConfigOwnerOptions<HeadTrackingConfigData> noHeader = Options(dir, path);
            noHeader.Header = null;
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(noHeader), "no header");
            ConfigOwnerOptions<HeadTrackingConfigData> badHeader = Options(dir, path);
            badHeader.Header = new RenderHeader("ABZÛU");
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(badHeader), "a header the renderer refuses");
            ConfigOwnerOptions<HeadTrackingConfigData> importWithoutSource = Options(dir, path);
            importWithoutSource.Import = new Legacy().Import;
            ArgumentException e = ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(importWithoutSource),
                "an import with no legacy file");
            ExpectContains(e.Message, "Import is set, but no LegacySourcePath names the file it reads");
            ConfigOwnerOptions<HeadTrackingConfigData> sourceWithoutImport = Options(dir, path);
            sourceWithoutImport.LegacySourcePath = Path.Combine(dir, LegacyName);
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(sourceWithoutImport),
                "a legacy file with no import");
            ConfigOwnerOptions<HeadTrackingConfigData> sourceIsPath = Options(dir, path);
            sourceIsPath.Import = new Legacy().Import;
            sourceIsPath.LegacySourcePath = path;
            ExpectThrows<ArgumentException>(() => new ConfigOwner<HeadTrackingConfigData>(sourceIsPath), "a legacy file that is the config");

            var owner = new ConfigOwner<HeadTrackingConfigData>(Options(dir, path));
            ExpectThrows<InvalidOperationException>(() => owner.Save(c => c.WorldSpaceYaw = false), "Save before Load");
            ExpectThrows<InvalidOperationException>(() => owner.Reload(), "Reload before Load");
            ExpectThrows<InvalidOperationException>(() => owner.FileChanged(), "FileChanged before Load");
            ExpectThrows<ArgumentNullException>(() => owner.Save(null), "a null change");
            ExpectListing(dir);
        }

        private static ConfigOwnerOptions<HeadTrackingConfigData> Options(string dir, string path)
        {
            return new ConfigOwnerOptions<HeadTrackingConfigData>
            {
                Path = path,
                Table = Table(),
                Header = new RenderHeader(Display),
                Defaults = DefaultsFile.At(ScratchDefaults(dir)),
            };
        }

        // Defaults.ini in a folder of the scratch directory, so a listing of the directory's files
        // is the config file and the legacy file alone.
        private static string ScratchDefaults(string dir)
        {
            return Path.Combine(Path.Combine(dir, "global"), "Defaults.ini");
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

        // With Defaults.ini at the built-in values, a row the import left at its default is written
        // default, and every other row, the tracking mode pair included, its value.
        private static byte[] MigratedBytes()
        {
            return Table().RenderMigration(MigratedConfig(), Defaults(), new RenderHeader(Display));
        }

        private static byte[] Fresh()
        {
            return Table().RenderFresh(new RenderHeader(Display));
        }

        // The log without the Defaults.ini lines.
        private static string[] GameLines(ConfigLoadResult<HeadTrackingConfigData> load)
        {
            return load.Log.Where(l => !l.Contains("Defaults.ini")).ToArray();
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
            public readonly string Dir;
            public readonly string Path;
            public readonly string LegacyPath;
            public readonly List<string> Sink = new List<string>();
            public readonly Legacy Legacy = new Legacy();
            public readonly string DefaultsPath;
            public Action<string, string> Hook;
            public bool WithImport = true;
            public DefaultsFile Defaults;
            public ConfigTable<HeadTrackingConfigData> Table;
            public PlatformID Platform = PlatformID.Win32NT;
            private byte[] _legacyBytes;

            public Rig(string dir)
            {
                Dir = dir;
                Path = System.IO.Path.Combine(dir, FileName);
                LegacyPath = System.IO.Path.Combine(dir, LegacyName);
                DefaultsPath = ScratchDefaults(dir);
            }

            public void PutDefaults(byte[] bytes)
            {
                Directory.CreateDirectory(System.IO.Path.GetDirectoryName(DefaultsPath));
                File.WriteAllBytes(DefaultsPath, bytes);
            }

            public void PutLegacy(byte[] bytes)
            {
                File.WriteAllBytes(LegacyPath, bytes);
                File.SetLastWriteTimeUtc(LegacyPath, LegacyWriteTime);
                _legacyBytes = bytes;
            }

            public void ExpectLegacyKept()
            {
                ExpectLegacyKept(_legacyBytes, "");
            }

            public void ExpectLegacyKept(byte[] bytes, string when)
            {
                ExpectBytes(LegacyPath, bytes);
                Expect(File.GetLastWriteTimeUtc(LegacyPath) == LegacyWriteTime,
                    when + (when.Length == 0 ? "" : ": ") + LegacyPath + " was written at " + File.GetLastWriteTimeUtc(LegacyPath).ToString("o"));
            }

            public ConfigOwner<HeadTrackingConfigData> Owner()
            {
                ConfigOwnerOptions<HeadTrackingConfigData> options = Options(Dir, Path);
                options.Import = WithImport ? Legacy.Import : null;
                options.LegacySourcePath = WithImport ? LegacyPath : null;
                options.StatusSink = message => Sink.Add(message);
                if (Defaults != null) options.Defaults = Defaults;
                if (Table != null) options.Table = Table;
                return new ConfigOwner<HeadTrackingConfigData>(options, (step, path) =>
                {
                    if (Hook != null) Hook(step, path);
                }, Platform);
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

                int port = 4242;
                bool yawWorld = true;
                bool position = true;
                float light = HeadFollowLightSettings.DefaultMultiplier;
                string section = "";
                foreach (string raw in File.ReadAllLines(input.Path))
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

        // An import that did not complete leaves the legacy file as it was and creates nothing.
        private static void ExpectNotImported(Rig rig)
        {
            rig.ExpectLegacyKept();
            ExpectListing(rig.Dir, LegacyName);
        }

        private static void ExpectImported(Rig rig)
        {
            ExpectBytes(rig.Path, MigratedBytes());
            rig.ExpectLegacyKept();
            ExpectListing(rig.Dir, FileName, LegacyName);
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
