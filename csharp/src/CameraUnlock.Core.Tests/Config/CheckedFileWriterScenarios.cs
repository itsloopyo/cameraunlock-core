#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// <see cref="CheckedFileWriter"/> against real files and real Windows handles. The same
    /// source runs under xunit on net8.0 (CheckedFileWriterTests) and in the
    /// CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5 and 4.7.2, because the
    /// writer is File.Replace and File.Move, and each runtime has its own implementation of
    /// those. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class CheckedFileWriterScenarios
    {
        private const string FileName = "HeadTracking.ini";
        private const int HResultGenFailure = unchecked((int)0x8007001F);
        private const int HResultSharingViolation = unchecked((int)0x80070020);

        private static readonly CheckedWriteStep[] StepsBeforeCommit =
        {
            CheckedWriteStep.ReadTarget,
            CheckedWriteStep.CreateTemporary,
            CheckedWriteStep.WriteTemporary,
            CheckedWriteStep.FlushTemporary,
            CheckedWriteStep.CloseTemporary,
            CheckedWriteStep.RecheckTarget,
            CheckedWriteStep.Commit,
        };

        private static readonly List<KeyValuePair<string, Action<string>>> All = new List<KeyValuePair<string, Action<string>>>
        {
            Scenario("creates-an-absent-target", CreatesAnAbsentTarget),
            Scenario("replaces-an-existing-target", ReplacesAnExistingTarget),
            Scenario("empty-files-are-files", EmptyFilesAreFiles),
            Scenario("runs-its-steps-in-order-beside-the-target", RunsItsStepsInOrderBesideTheTarget),
            Scenario("stale-expected-bytes-write-nothing", StaleExpectedBytesWriteNothing),
            Scenario("present-when-expected-absent-writes-nothing", PresentWhenExpectedAbsentWritesNothing),
            Scenario("absent-when-expected-present-writes-nothing", AbsentWhenExpectedPresentWritesNothing),
            Scenario("target-edited-before-the-recheck", TargetEditedBeforeTheRecheck),
            Scenario("target-swapped-for-identical-bytes-before-the-recheck", TargetSwappedForIdenticalBytesBeforeTheRecheck),
            Scenario("target-deleted-before-the-recheck", TargetDeletedBeforeTheRecheck),
            Scenario("target-appeared-before-the-recheck", TargetAppearedBeforeTheRecheck),
            Scenario("target-appeared-after-the-recheck-is-not-overwritten", TargetAppearedAfterTheRecheckIsNotOverwritten),
            Scenario("every-step-failing-over-an-existing-target", EveryStepFailingOverAnExistingTarget),
            Scenario("every-step-failing-over-an-absent-target", EveryStepFailingOverAnAbsentTarget),
            Scenario("a-failed-removal-is-reported-not-hidden", AFailedRemovalIsReportedNotHidden),
            Scenario("a-conflict-whose-removal-fails-throws", AConflictWhoseRemovalFailsThrows),
            Scenario("an-unfinished-replacement-keeps-the-temporary", AnUnfinishedReplacementKeepsTheTemporary),
            Scenario("a-taken-temporary-name-is-not-deleted", ATakenTemporaryNameIsNotDeleted),
            Scenario("a-handle-without-share-delete-fails-the-commit", AHandleWithoutShareDeleteFailsTheCommit),
            Scenario("an-exclusive-handle-fails-the-read", AnExclusiveHandleFailsTheRead),
            Scenario("a-read-only-target-fails-and-stays-read-only", AReadOnlyTargetFailsAndStaysReadOnly),
            Scenario("hidden-and-system-attributes-are-kept", HiddenAndSystemAttributesAreKept),
            Scenario("unrelated-tmp-and-bak-files-are-untouched", UnrelatedTmpAndBakFilesAreUntouched),
            Scenario("a-non-ascii-path", ANonAsciiPath),
            Scenario("arguments-are-checked-before-any-io", ArgumentsAreCheckedBeforeAnyIo),
        };

        public static IEnumerable<string> Names
        {
            get { return All.Select(s => s.Key); }
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

        public static string CreateScratchDirectory()
        {
            string path = Path.Combine(Path.GetTempPath(), "cu-checked-writer-" + Guid.NewGuid().ToString("N"));
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

        private static void CreatesAnAbsentTarget(string dir)
        {
            string target = Path.Combine(dir, FileName);
            Expect(CheckedFileWriter.Write(target, null, Utf8("[General]\r\nEnabled=true\r\n")) == CheckedWriteOutcome.Committed,
                "creation commits");
            ExpectBytes(target, "[General]\r\nEnabled=true\r\n");
            ExpectListing(dir, FileName);
        }

        private static void ReplacesAnExistingTarget(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("old=1\n"));
            Expect(CheckedFileWriter.Write(target, Utf8("old=1\n"), Utf8("new=2\n")) == CheckedWriteOutcome.Committed,
                "replacement commits");
            ExpectBytes(target, "new=2\n");
            ExpectListing(dir, FileName);
        }

        private static void EmptyFilesAreFiles(string dir)
        {
            string target = Path.Combine(dir, FileName);
            Expect(CheckedFileWriter.Write(target, null, new byte[0]) == CheckedWriteOutcome.Committed,
                "an empty file is created");
            ExpectBytes(target, "");
            Expect(CheckedFileWriter.Write(target, null, Utf8("x")) == CheckedWriteOutcome.TargetAppeared,
                "an empty file is not an absent one");
            Expect(CheckedFileWriter.Write(target, new byte[0], Utf8("x")) == CheckedWriteOutcome.Committed,
                "an empty file is replaced");
            Expect(CheckedFileWriter.Write(target, Utf8("x"), new byte[0]) == CheckedWriteOutcome.Committed,
                "a file is replaced by an empty one");
            ExpectBytes(target, "");
            ExpectListing(dir, FileName);
        }

        private static void RunsItsStepsInOrderBesideTheTarget(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            var seen = new List<CheckedWriteStep>();
            var paths = new List<string>();
            CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"), (step, path) =>
            {
                seen.Add(step);
                paths.Add(path);
            });
            Expect(seen.SequenceEqual(StepsBeforeCommit), "steps run in order: " + Join(seen));

            string temporary = paths[1];
            string name = Path.GetFileName(temporary);
            Expect(Path.GetDirectoryName(temporary) == dir, "the temporary is a sibling of the target");
            Expect(name.Length == FileName.Length + 1 + 32 + 4
                    && name.StartsWith(FileName + ".", StringComparison.Ordinal)
                    && name.EndsWith(".tmp", StringComparison.Ordinal)
                    && name.Substring(FileName.Length + 1, 32).All(c => (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')),
                "the temporary is named <file>.<32 hex>.tmp, got " + name);
            for (int i = 1; i <= 4; i++) Expect(paths[i] == temporary, "temporary steps act on the temporary");
            Expect(paths[0] == target && paths[5] == target && paths[6] == target, "target steps act on the target");

            var second = new List<string>();
            CheckedFileWriter.Write(target, Utf8("a=2"), Utf8("a=3"), (step, path) =>
            {
                if (step == CheckedWriteStep.CreateTemporary) second.Add(path);
            });
            Expect(second[0] != temporary, "each call picks a new temporary name");
            ExpectBytes(target, "a=3");
            ExpectListing(dir, FileName);
        }

        private static void StaleExpectedBytesWriteNothing(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("on disk"));
            var seen = new List<CheckedWriteStep>();
            Expect(CheckedFileWriter.Write(target, Utf8("read earlier"), Utf8("new"), (step, path) => seen.Add(step))
                    == CheckedWriteOutcome.TargetChanged,
                "a stale expectation is TargetChanged");
            Expect(seen.SequenceEqual(new[] { CheckedWriteStep.ReadTarget }), "nothing past the first read runs");
            ExpectBytes(target, "on disk");
            ExpectListing(dir, FileName);
        }

        private static void PresentWhenExpectedAbsentWritesNothing(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("theirs"));
            Expect(CheckedFileWriter.Write(target, null, Utf8("mine")) == CheckedWriteOutcome.TargetAppeared,
                "a file where none was expected is TargetAppeared");
            ExpectBytes(target, "theirs");
            ExpectListing(dir, FileName);
        }

        private static void AbsentWhenExpectedPresentWritesNothing(string dir)
        {
            string target = Path.Combine(dir, FileName);
            Expect(CheckedFileWriter.Write(target, Utf8("was here"), Utf8("mine")) == CheckedWriteOutcome.TargetMissing,
                "no file where one was expected is TargetMissing");
            ExpectListing(dir);

            string nested = Path.Combine(Path.Combine(dir, "no such folder"), FileName);
            Expect(CheckedFileWriter.Write(nested, Utf8("was here"), Utf8("mine")) == CheckedWriteOutcome.TargetMissing,
                "a missing folder is a missing file");
            ExpectListing(dir);
        }

        private static void TargetEditedBeforeTheRecheck(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            Expect(CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"), (step, path) =>
                {
                    if (step == CheckedWriteStep.RecheckTarget) File.WriteAllBytes(target, Utf8("a=9"));
                }) == CheckedWriteOutcome.TargetChanged,
                "an edit between the reads is TargetChanged");
            ExpectBytes(target, "a=9");
            ExpectListing(dir, FileName);
        }

        private static void TargetSwappedForIdenticalBytesBeforeTheRecheck(string dir)
        {
            string target = Path.Combine(dir, FileName);
            string other = Path.Combine(dir, "other.ini");
            File.WriteAllBytes(target, Utf8("a=1"));
            Expect(CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"), (step, path) =>
                {
                    if (step != CheckedWriteStep.RecheckTarget) return;
                    File.WriteAllBytes(other, Utf8("a=1"));
                    File.Delete(target);
                    File.Move(other, target);
                }) == CheckedWriteOutcome.TargetReplaced,
                "a different file with the same bytes is TargetReplaced");
            ExpectBytes(target, "a=1");
            ExpectListing(dir, FileName);
        }

        private static void TargetDeletedBeforeTheRecheck(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            Expect(CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"), (step, path) =>
                {
                    if (step == CheckedWriteStep.RecheckTarget) File.Delete(target);
                }) == CheckedWriteOutcome.TargetMissing,
                "a deletion between the reads is TargetMissing");
            ExpectListing(dir);
        }

        private static void TargetAppearedBeforeTheRecheck(string dir)
        {
            string target = Path.Combine(dir, FileName);
            Expect(CheckedFileWriter.Write(target, null, Utf8("mine"), (step, path) =>
                {
                    if (step == CheckedWriteStep.RecheckTarget) File.WriteAllBytes(target, Utf8("theirs"));
                }) == CheckedWriteOutcome.TargetAppeared,
                "a file created between the reads is TargetAppeared");
            ExpectBytes(target, "theirs");
            ExpectListing(dir, FileName);
        }

        private static void TargetAppearedAfterTheRecheckIsNotOverwritten(string dir)
        {
            string target = Path.Combine(dir, FileName);
            Expect(CheckedFileWriter.Write(target, null, Utf8("mine"), (step, path) =>
                {
                    if (step == CheckedWriteStep.Commit) File.WriteAllBytes(target, Utf8("theirs"));
                }) == CheckedWriteOutcome.TargetAppeared,
                "a file created after the last check is TargetAppeared, not overwritten");
            ExpectBytes(target, "theirs");
            ExpectListing(dir, FileName);
        }

        private static void EveryStepFailingOverAnExistingTarget(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            foreach (CheckedWriteStep failing in StepsBeforeCommit)
            {
                CheckedWriteException e = ExpectFailure(
                    () => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"), FailAt(failing)), failing);
                ExpectInjected(e, failing);
                ExpectBytes(target, "a=1");
                ExpectListing(dir, FileName);
            }
        }

        private static void EveryStepFailingOverAnAbsentTarget(string dir)
        {
            string target = Path.Combine(dir, FileName);
            foreach (CheckedWriteStep failing in StepsBeforeCommit)
            {
                CheckedWriteException e = ExpectFailure(
                    () => CheckedFileWriter.Write(target, null, Utf8("a=2"), FailAt(failing)), failing);
                ExpectInjected(e, failing);
                ExpectListing(dir);
            }
        }

        private static void AFailedRemovalIsReportedNotHidden(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            var removal = new IOException("injected removal failure", HResultGenFailure);
            CheckedWriteException e = ExpectFailure(() => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"),
                (step, path) =>
                {
                    if (step == CheckedWriteStep.Commit) throw new IOException("injected commit failure", HResultGenFailure);
                    if (step == CheckedWriteStep.RemoveTemporary) throw removal;
                }), CheckedWriteStep.Commit);
            Expect(e.CleanupError == removal, "the removal error is carried");
            Expect(!e.TemporaryRemoved, "TemporaryRemoved is false");
            Expect(e.InnerException.Message == "injected commit failure", "the first error stays the inner exception");
            Expect(e.Message.Contains("injected removal failure") && e.Message.Contains(e.TemporaryPath),
                "the message names the leftover temporary and why");
            ExpectBytes(e.TemporaryPath, "a=2");
            ExpectBytes(target, "a=1");
            File.Delete(e.TemporaryPath);
            ExpectListing(dir, FileName);
        }

        private static void AConflictWhoseRemovalFailsThrows(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            var removal = new IOException("injected removal failure", HResultGenFailure);
            CheckedWriteException e = ExpectFailure(() => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"),
                (step, path) =>
                {
                    if (step == CheckedWriteStep.RecheckTarget) File.WriteAllBytes(target, Utf8("a=9"));
                    if (step == CheckedWriteStep.RemoveTemporary) throw removal;
                }), CheckedWriteStep.RemoveTemporary);
            Expect(e.InnerException == removal, "the removal error is the inner exception");
            Expect(e.Message.Contains(CheckedWriteOutcome.TargetChanged.ToString()), "the message names the conflict");
            Expect(!e.TemporaryRemoved && e.TemporaryPath != null, "the leftover temporary is named");
            ExpectBytes(target, "a=9");
            File.Delete(e.TemporaryPath);
            ExpectListing(dir, FileName);
        }

        private static void AnUnfinishedReplacementKeepsTheTemporary(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            foreach (int hresult in new[] { unchecked((int)0x80070498), unchecked((int)0x80070499) })
            {
                CheckedWriteException e = ExpectFailure(() => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"),
                    (step, path) =>
                    {
                        if (step == CheckedWriteStep.Commit) throw new IOException("injected", hresult);
                    }), CheckedWriteStep.Commit);
                Expect(e.OutcomeUncertain, "ERROR_UNABLE_TO_MOVE_REPLACEMENT(_2) is uncertain");
                Expect(!e.TemporaryRemoved && e.CleanupError == null, "the temporary is kept, not deleted");
                Expect(e.Message.Contains(e.TemporaryPath), "the message says where the new contents are");
                ExpectBytes(e.TemporaryPath, "a=2");
                File.Delete(e.TemporaryPath);
            }

            CheckedWriteException creation = ExpectFailure(() => CheckedFileWriter.Write(
                Path.Combine(dir, "absent.ini"), null, Utf8("a=2"), (step, path) =>
                {
                    if (step == CheckedWriteStep.Commit) throw new IOException("injected", unchecked((int)0x80070498));
                }), CheckedWriteStep.Commit);
            Expect(!creation.OutcomeUncertain && creation.TemporaryRemoved, "a rename into place is never uncertain");
            ExpectBytes(target, "a=1");
            ExpectListing(dir, FileName);
        }

        private static void ATakenTemporaryNameIsNotDeleted(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            string taken = null;
            CheckedWriteException e = ExpectFailure(() => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2"),
                (step, path) =>
                {
                    if (step != CheckedWriteStep.CreateTemporary) return;
                    taken = path;
                    File.WriteAllBytes(path, Utf8("not the writer's"));
                }), CheckedWriteStep.CreateTemporary);
            Expect(e.InnerException is IOException, "CreateNew refuses the existing name");
            Expect(e.TemporaryPath == null && !e.TemporaryRemoved, "a file the writer did not create is not its temporary");
            ExpectBytes(taken, "not the writer's");
            ExpectBytes(target, "a=1");
            ExpectListing(dir, FileName, Path.GetFileName(taken));
        }

        private static void AHandleWithoutShareDeleteFailsTheCommit(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            using (new FileStream(target, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                CheckedWriteException e = ExpectFailure(
                    () => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2")), CheckedWriteStep.Commit);
                Expect(Marshal.GetHRForException(e.InnerException) == HResultSharingViolation,
                    "the OS sharing violation is the inner exception, got " + e.InnerException);
                Expect(e.TemporaryRemoved, "the temporary is removed");
            }
            ExpectBytes(target, "a=1");
            ExpectListing(dir, FileName);
        }

        private static void AnExclusiveHandleFailsTheRead(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            using (new FileStream(target, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
            {
                CheckedWriteException e = ExpectFailure(
                    () => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2")), CheckedWriteStep.ReadTarget);
                Expect(Marshal.GetHRForException(e.InnerException) == HResultSharingViolation,
                    "the OS sharing violation is the inner exception, got " + e.InnerException);
                Expect(e.TemporaryPath == null, "no temporary was made");
            }
            ExpectBytes(target, "a=1");
            ExpectListing(dir, FileName);
        }

        private static void AReadOnlyTargetFailsAndStaysReadOnly(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            File.SetAttributes(target, FileAttributes.ReadOnly);
            CheckedWriteException e = ExpectFailure(
                () => CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2")), CheckedWriteStep.Commit);
            Expect(e.InnerException is UnauthorizedAccessException,
                "the OS access-denied error is the inner exception, got " + e.InnerException);
            Expect(e.TemporaryRemoved, "the temporary is removed");
            Expect((File.GetAttributes(target) & FileAttributes.ReadOnly) != 0, "the read-only attribute is left alone");
            ExpectBytes(target, "a=1");
            ExpectListing(dir, FileName);
        }

        private static void HiddenAndSystemAttributesAreKept(string dir)
        {
            string target = Path.Combine(dir, FileName);
            File.WriteAllBytes(target, Utf8("a=1"));
            File.SetAttributes(target, FileAttributes.Hidden | FileAttributes.System);
            Expect(CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2")) == CheckedWriteOutcome.Committed,
                "a hidden system file is replaced");
            FileAttributes after = File.GetAttributes(target);
            Expect((after & FileAttributes.Hidden) != 0 && (after & FileAttributes.System) != 0,
                "its attributes survive, got " + after);
            ExpectBytes(target, "a=2");
            ExpectListing(dir, FileName);
        }

        private static void UnrelatedTmpAndBakFilesAreUntouched(string dir)
        {
            string target = Path.Combine(dir, FileName);
            string[] bystanders =
            {
                FileName + ".tmp",
                FileName + ".bak",
                "HeadTracking.tmp",
                "HeadTracking.bak",
                FileName + ".00000000000000000000000000000000.tmp",
            };
            foreach (string name in bystanders) File.WriteAllBytes(Path.Combine(dir, name), Utf8("keep " + name));
            File.WriteAllBytes(target, Utf8("a=1"));
            string[] everything = bystanders.Concat(new[] { FileName }).ToArray();

            Expect(CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2")) == CheckedWriteOutcome.Committed, "commits");
            ExpectListing(dir, everything);
            ExpectFailure(() => CheckedFileWriter.Write(target, Utf8("a=2"), Utf8("a=3"), FailAt(CheckedWriteStep.Commit)),
                CheckedWriteStep.Commit);
            ExpectListing(dir, everything);
            Expect(CheckedFileWriter.Write(target, Utf8("stale"), Utf8("a=3")) == CheckedWriteOutcome.TargetChanged,
                "conflicts");
            ExpectListing(dir, everything);
            Expect(CheckedFileWriter.Write(target, Utf8("a=2"), Utf8("a=3"), (step, path) =>
                {
                    if (step == CheckedWriteStep.RecheckTarget) File.WriteAllBytes(target, Utf8("a=9"));
                }) == CheckedWriteOutcome.TargetChanged,
                "conflicts after making its temporary");
            ExpectListing(dir, everything);

            foreach (string name in bystanders) ExpectBytes(Path.Combine(dir, name), "keep " + name);
            ExpectBytes(target, "a=9");
        }

        private static void ANonAsciiPath(string dir)
        {
            string folder = Path.Combine(dir, "Ünïcødé 日本語 ✓");
            Directory.CreateDirectory(folder);
            string name = "Kopf Ω €.ini";
            string target = Path.Combine(folder, name);
            Expect(CheckedFileWriter.Write(target, null, Utf8("a=1")) == CheckedWriteOutcome.Committed, "creates");
            Expect(CheckedFileWriter.Write(target, Utf8("a=1"), Utf8("a=2")) == CheckedWriteOutcome.Committed, "replaces");
            CheckedWriteException e = ExpectFailure(
                () => CheckedFileWriter.Write(target, Utf8("a=2"), Utf8("a=3"), FailAt(CheckedWriteStep.Commit)),
                CheckedWriteStep.Commit);
            Expect(e.TargetPath == target && e.TemporaryPath.StartsWith(target + ".", StringComparison.Ordinal),
                "the paths it reports are spelled as given");
            ExpectBytes(target, "a=2");
            ExpectListing(folder, name);
        }

        private static void ArgumentsAreCheckedBeforeAnyIo(string dir)
        {
            string target = Path.Combine(dir, FileName);
            ExpectThrows<ArgumentNullException>(() => CheckedFileWriter.Write(null, null, Utf8("a")), "null path");
            ExpectThrows<ArgumentNullException>(() => CheckedFileWriter.Write(target, null, null), "null candidate");
            ExpectThrows<ArgumentException>(() => CheckedFileWriter.Write("", null, Utf8("a")), "empty path");
            ExpectThrows<ArgumentException>(
                () => CheckedFileWriter.Write(dir + Path.DirectorySeparatorChar, null, Utf8("a")), "a folder path");
            ExpectListing(dir);
        }

        private static Action<CheckedWriteStep, string> FailAt(CheckedWriteStep failing)
        {
            return (step, path) =>
            {
                if (step == failing) throw new IOException("injected failure at " + step, HResultGenFailure);
            };
        }

        private static void ExpectInjected(CheckedWriteException e, CheckedWriteStep failing)
        {
            Expect(e.InnerException is IOException && e.InnerException.Message == "injected failure at " + failing,
                failing + ": the injected error is the inner exception, unchanged");
            bool made = failing != CheckedWriteStep.ReadTarget && failing != CheckedWriteStep.CreateTemporary;
            Expect(made == (e.TemporaryPath != null), failing + ": TemporaryPath is set only once one was made");
            Expect(made == e.TemporaryRemoved, failing + ": a temporary it made is removed");
            Expect(!e.OutcomeUncertain && e.CleanupError == null, failing + ": nothing else went wrong");
            Expect(e.Message.Contains(e.TargetPath) && e.Message.Contains(failing.ToString()),
                failing + ": the message names the file and the step");
        }

        private static CheckedWriteException ExpectFailure(Func<CheckedWriteOutcome> write, CheckedWriteStep step)
        {
            CheckedWriteException e = ExpectThrows<CheckedWriteException>(() => write(), step.ToString());
            Expect(e.Step == step, "failed at " + step + ", got " + e.Step + ": " + e.Message);
            return e;
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

        private static void ExpectBytes(string path, string text)
        {
            byte[] actual = File.ReadAllBytes(path);
            Expect(actual.SequenceEqual(Utf8(text)),
                path + " holds \"" + Encoding.UTF8.GetString(actual) + "\", expected \"" + text + "\"");
        }

        private static void ExpectListing(string dir, params string[] names)
        {
            string[] actual = Directory.GetFiles(dir).Select(p => Path.GetFileName(p)).OrderBy(n => n, StringComparer.Ordinal).ToArray();
            string[] expected = names.OrderBy(n => n, StringComparer.Ordinal).ToArray();
            Expect(actual.SequenceEqual(expected), dir + " holds [" + Join(actual) + "], expected [" + Join(expected) + "]");
        }

        private static void Expect(bool condition, string what)
        {
            if (!condition) throw new InvalidOperationException(what);
        }

        private static string Join<T>(IEnumerable<T> items)
        {
            return string.Join(", ", items.Select(i => i.ToString()).ToArray());
        }

        private static byte[] Utf8(string text)
        {
            return new UTF8Encoding(false).GetBytes(text);
        }
    }
}
