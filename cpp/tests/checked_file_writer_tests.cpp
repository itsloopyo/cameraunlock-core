// WriteFileChecked against real files and real Windows handles, scenario for scenario with
// the C# CheckedFileWriterScenarios. Failures are injected through the internal
// WriteFileCheckedWithFault seam, and the interruption scenarios kill a child copy of this
// executable partway through a write.

#include <cameraunlock/config/checked_file_writer.h>

#include "../src/config/checked_file_writer_internal.h"

#include <iostream>
#include <string>

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cameraunlock::CheckedWriteResult;
using cameraunlock::CheckedWriteStatus;
using cameraunlock::CheckedWriteStep;
using cameraunlock::WriteFileChecked;
using cameraunlock::detail::CheckedWriteFault;
using cameraunlock::detail::WriteFileCheckedWithFault;

// CameraUnlock.Core.Config's CheckedWriteOutcome and CheckedWriteStep carry the same numbers.
static_assert(static_cast<int>(CheckedWriteStatus::Committed) == 0, "Committed");
static_assert(static_cast<int>(CheckedWriteStatus::TargetChanged) == 1, "TargetChanged");
static_assert(static_cast<int>(CheckedWriteStatus::TargetAppeared) == 2, "TargetAppeared");
static_assert(static_cast<int>(CheckedWriteStatus::TargetMissing) == 3, "TargetMissing");
static_assert(static_cast<int>(CheckedWriteStatus::TargetReplaced) == 4, "TargetReplaced");
static_assert(static_cast<int>(CheckedWriteStep::ReadTarget) == 1, "ReadTarget");
static_assert(static_cast<int>(CheckedWriteStep::CreateTemporary) == 2, "CreateTemporary");
static_assert(static_cast<int>(CheckedWriteStep::WriteTemporary) == 3, "WriteTemporary");
static_assert(static_cast<int>(CheckedWriteStep::FlushTemporary) == 4, "FlushTemporary");
static_assert(static_cast<int>(CheckedWriteStep::CloseTemporary) == 5, "CloseTemporary");
static_assert(static_cast<int>(CheckedWriteStep::RecheckTarget) == 6, "RecheckTarget");
static_assert(static_cast<int>(CheckedWriteStep::Commit) == 7, "Commit");
static_assert(static_cast<int>(CheckedWriteStep::RemoveTemporary) == 8, "RemoveTemporary");

constexpr wchar_t kFileName[] = L"HeadTracking.ini";
constexpr int kKilledExitCode = 3;

const CheckedWriteStep kStepsBeforeCommit[] = {
    CheckedWriteStep::ReadTarget,     CheckedWriteStep::CreateTemporary, CheckedWriteStep::WriteTemporary,
    CheckedWriteStep::FlushTemporary, CheckedWriteStep::CloseTemporary,  CheckedWriteStep::RecheckTarget,
    CheckedWriteStep::Commit,
};

int g_failures = 0;
std::string g_scenario;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << g_scenario << ": " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << g_scenario << ": " << name << "\n";
        ++g_failures;
    }
}

std::string StepName(CheckedWriteStep step) { return cameraunlock::CheckedWriteStepName(step); }

std::wstring RandomName() {
    std::random_device random;
    return std::to_wstring(random()) + std::to_wstring(random());
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("cannot create " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

bool HoldsBytes(const fs::path& path, const std::string& bytes) {
    return fs::exists(path) && ReadBytes(path) == bytes;
}

bool ListingIs(const fs::path& dir, std::vector<std::wstring> names) {
    std::vector<std::wstring> actual;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) actual.push_back(entry.path().filename().wstring());
    }
    std::sort(actual.begin(), actual.end());
    std::sort(names.begin(), names.end());
    return actual == names;
}

CheckedWriteFault FailAt(CheckedWriteStep failing, DWORD error = ERROR_GEN_FAILURE) {
    return [failing, error](CheckedWriteStep step, const std::wstring&) -> std::uint32_t {
        return step == failing ? error : 0;
    };
}

CheckedWriteFault OnStep(CheckedWriteStep at, std::function<void()> action) {
    return [at, action](CheckedWriteStep step, const std::wstring&) -> std::uint32_t {
        if (step == at) action();
        return 0;
    };
}

void CreatesAnAbsentTarget(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    const CheckedWriteResult r = WriteFileChecked(target.wstring(), std::nullopt, "[General]\r\nEnabled=true\r\n");
    Check(r.Committed() && r.failed_step == CheckedWriteStep::None && r.error == 0, "creation commits");
    Check(HoldsBytes(target, "[General]\r\nEnabled=true\r\n"), "the new file holds the candidate");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void ReplacesAnExistingTarget(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "old=1\n");
    Check(WriteFileChecked(target.wstring(), "old=1\n", "new=2\n").Committed(), "replacement commits");
    Check(HoldsBytes(target, "new=2\n"), "the target holds the candidate");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void EmptyFilesAreFiles(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    Check(WriteFileChecked(target.wstring(), std::nullopt, "").Committed(), "an empty file is created");
    Check(HoldsBytes(target, ""), "and is empty");
    Check(WriteFileChecked(target.wstring(), std::nullopt, "x").status == CheckedWriteStatus::TargetAppeared,
          "an empty file is not an absent one");
    Check(WriteFileChecked(target.wstring(), std::string(), "x").Committed(), "an empty file is replaced");
    Check(WriteFileChecked(target.wstring(), "x", "").Committed(), "a file is replaced by an empty one");
    Check(HoldsBytes(target, ""), "which is empty");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void RunsItsStepsInOrderBesideTheTarget(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    std::vector<CheckedWriteStep> steps;
    std::vector<std::wstring> paths;
    WriteFileCheckedWithFault(target.wstring(), "a=1", "a=2",
                              [&](CheckedWriteStep step, const std::wstring& path) -> std::uint32_t {
                                  steps.push_back(step);
                                  paths.push_back(path);
                                  return 0;
                              });
    Check(std::equal(steps.begin(), steps.end(), std::begin(kStepsBeforeCommit), std::end(kStepsBeforeCommit)),
          "steps run in order");
    if (paths.size() != 7) return;

    const fs::path temporary = paths[1];
    const std::wstring name = temporary.filename().wstring();
    const std::wstring prefix = std::wstring(kFileName) + L".";
    bool hex = name.size() == prefix.size() + 32 + 4;
    for (std::size_t i = prefix.size(); hex && i < prefix.size() + 32; ++i) {
        hex = (name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'f');
    }
    Check(temporary.parent_path() == dir, "the temporary is a sibling of the target");
    Check(hex && name.compare(0, prefix.size(), prefix) == 0 && name.compare(name.size() - 4, 4, L".tmp") == 0,
          "the temporary is named <file>.<32 hex>.tmp");
    Check(paths[2] == paths[1] && paths[3] == paths[1] && paths[4] == paths[1], "temporary steps act on the temporary");
    Check(paths[0] == target.wstring() && paths[5] == target.wstring() && paths[6] == target.wstring(),
          "target steps act on the target");

    std::wstring second;
    WriteFileCheckedWithFault(target.wstring(), "a=2", "a=3",
                              [&](CheckedWriteStep step, const std::wstring& path) -> std::uint32_t {
                                  if (step == CheckedWriteStep::CreateTemporary) second = path;
                                  return 0;
                              });
    Check(second != paths[1], "each call picks a new temporary name");
    Check(HoldsBytes(target, "a=3"), "both writes committed");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void StaleExpectedBytesWriteNothing(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "on disk");
    std::vector<CheckedWriteStep> steps;
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), "read earlier", "new", [&](CheckedWriteStep step, const std::wstring&) -> std::uint32_t {
            steps.push_back(step);
            return 0;
        });
    Check(r.status == CheckedWriteStatus::TargetChanged, "a stale expectation is TargetChanged");
    Check(steps == std::vector<CheckedWriteStep>{CheckedWriteStep::ReadTarget}, "nothing past the first read runs");
    Check(r.temporary_path.empty(), "no temporary was made");
    Check(HoldsBytes(target, "on disk"), "the target is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void PresentWhenExpectedAbsentWritesNothing(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "theirs");
    Check(WriteFileChecked(target.wstring(), std::nullopt, "mine").status == CheckedWriteStatus::TargetAppeared,
          "a file where none was expected is TargetAppeared");
    Check(HoldsBytes(target, "theirs"), "the file is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AbsentWhenExpectedPresentWritesNothing(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    Check(WriteFileChecked(target.wstring(), "was here", "mine").status == CheckedWriteStatus::TargetMissing,
          "no file where one was expected is TargetMissing");
    Check(WriteFileChecked((dir / L"no such folder" / kFileName).wstring(), "was here", "mine").status ==
              CheckedWriteStatus::TargetMissing,
          "a missing folder is a missing file");
    Check(ListingIs(dir, {}), "nothing is written");
}

void TargetEditedBeforeTheRecheck(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), "a=1", "a=2", OnStep(CheckedWriteStep::RecheckTarget, [&] { WriteBytes(target, "a=9"); }));
    Check(r.status == CheckedWriteStatus::TargetChanged, "an edit between the reads is TargetChanged");
    Check(!r.temporary_path.empty() && r.temporary_removed && r.cleanup_error == 0, "its temporary is removed");
    Check(HoldsBytes(target, "a=9"), "the edit stands");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void TargetSwappedForIdenticalBytesBeforeTheRecheck(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    const fs::path other = dir / L"other.ini";
    WriteBytes(target, "a=1");
    const CheckedWriteResult r =
        WriteFileCheckedWithFault(target.wstring(), "a=1", "a=2", OnStep(CheckedWriteStep::RecheckTarget, [&] {
                                      WriteBytes(other, "a=1");
                                      fs::remove(target);
                                      fs::rename(other, target);
                                  }));
    Check(r.status == CheckedWriteStatus::TargetReplaced, "a different file with the same bytes is TargetReplaced");
    Check(HoldsBytes(target, "a=1"), "the replacement stands");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void TargetDeletedBeforeTheRecheck(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), "a=1", "a=2", OnStep(CheckedWriteStep::RecheckTarget, [&] { fs::remove(target); }));
    Check(r.status == CheckedWriteStatus::TargetMissing, "a deletion between the reads is TargetMissing");
    Check(ListingIs(dir, {}), "nothing is written");
}

void TargetAppearedBeforeTheRecheck(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), std::nullopt, "mine",
        OnStep(CheckedWriteStep::RecheckTarget, [&] { WriteBytes(target, "theirs"); }));
    Check(r.status == CheckedWriteStatus::TargetAppeared, "a file created between the reads is TargetAppeared");
    Check(HoldsBytes(target, "theirs"), "that file is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void TargetAppearedAfterTheRecheckIsNotOverwritten(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), std::nullopt, "mine", OnStep(CheckedWriteStep::Commit, [&] { WriteBytes(target, "theirs"); }));
    Check(r.status == CheckedWriteStatus::TargetAppeared, "a file created after the last check is TargetAppeared");
    Check(r.temporary_removed, "its temporary is removed");
    Check(HoldsBytes(target, "theirs"), "that file is not overwritten");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void ExpectInjected(const CheckedWriteResult& r, CheckedWriteStep failing) {
    const std::string step = StepName(failing);
    Check(r.status == CheckedWriteStatus::Failed && r.failed_step == failing, step + ": fails at that step");
    Check(r.error == ERROR_GEN_FAILURE, step + ": carries the injected error unchanged");
    const bool made = failing != CheckedWriteStep::ReadTarget && failing != CheckedWriteStep::CreateTemporary;
    Check(made == !r.temporary_path.empty(), step + ": temporary_path is set only once one was made");
    Check(made == r.temporary_removed, step + ": a temporary it made is removed");
    Check(!r.outcome_uncertain && r.cleanup_error == 0, step + ": nothing else went wrong");
}

void EveryStepFailingOverAnExistingTarget(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    for (CheckedWriteStep failing : kStepsBeforeCommit) {
        ExpectInjected(WriteFileCheckedWithFault(target.wstring(), "a=1", "a=2", FailAt(failing)), failing);
        Check(HoldsBytes(target, "a=1"), StepName(failing) + ": the target is unchanged");
        Check(ListingIs(dir, {kFileName}), StepName(failing) + ": nothing else is left");
    }
}

void EveryStepFailingOverAnAbsentTarget(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    for (CheckedWriteStep failing : kStepsBeforeCommit) {
        ExpectInjected(WriteFileCheckedWithFault(target.wstring(), std::nullopt, "a=2", FailAt(failing)), failing);
        Check(ListingIs(dir, {}), StepName(failing) + ": nothing is left");
    }
}

void AFailedRemovalIsReportedNotHidden(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), "a=1", "a=2", [](CheckedWriteStep step, const std::wstring&) -> std::uint32_t {
            if (step == CheckedWriteStep::Commit) return ERROR_GEN_FAILURE;
            if (step == CheckedWriteStep::RemoveTemporary) return ERROR_WRITE_PROTECT;
            return 0;
        });
    Check(r.failed_step == CheckedWriteStep::Commit && r.error == ERROR_GEN_FAILURE, "the first error is kept");
    Check(r.cleanup_error == ERROR_WRITE_PROTECT && !r.temporary_removed, "the removal error is carried");
    Check(HoldsBytes(r.temporary_path, "a=2"), "the leftover temporary is named");
    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    fs::remove(r.temporary_path);
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AConflictWhoseRemovalFailsIsReported(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), "a=1", "a=2", [&](CheckedWriteStep step, const std::wstring&) -> std::uint32_t {
            if (step == CheckedWriteStep::RecheckTarget) WriteBytes(target, "a=9");
            return step == CheckedWriteStep::RemoveTemporary ? ERROR_WRITE_PROTECT : 0;
        });
    Check(r.status == CheckedWriteStatus::TargetChanged, "the conflict is still reported");
    Check(r.cleanup_error == ERROR_WRITE_PROTECT && !r.temporary_removed && !r.temporary_path.empty(),
          "with the removal error and the leftover temporary");
    Check(HoldsBytes(target, "a=9"), "the edit stands");
    fs::remove(r.temporary_path);
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AnUnfinishedReplacementKeepsTheTemporary(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    for (DWORD error : {static_cast<DWORD>(ERROR_UNABLE_TO_MOVE_REPLACEMENT),
                        static_cast<DWORD>(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)}) {
        const CheckedWriteResult r =
            WriteFileCheckedWithFault(target.wstring(), "a=1", "a=2", FailAt(CheckedWriteStep::Commit, error));
        Check(r.failed_step == CheckedWriteStep::Commit && r.error == error && r.outcome_uncertain,
              "ERROR_UNABLE_TO_MOVE_REPLACEMENT(_2) is uncertain");
        Check(!r.temporary_removed && r.cleanup_error == 0, "the temporary is kept, not deleted");
        Check(HoldsBytes(r.temporary_path, "a=2"), "and holds the new contents");
        fs::remove(r.temporary_path);
    }
    const CheckedWriteResult creation =
        WriteFileCheckedWithFault((dir / L"absent.ini").wstring(), std::nullopt, "a=2",
                                  FailAt(CheckedWriteStep::Commit, ERROR_UNABLE_TO_MOVE_REPLACEMENT));
    Check(!creation.outcome_uncertain && creation.temporary_removed, "a rename into place is never uncertain");
    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void ATakenTemporaryNameIsNotDeleted(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    fs::path taken;
    const CheckedWriteResult r = WriteFileCheckedWithFault(
        target.wstring(), "a=1", "a=2", [&](CheckedWriteStep step, const std::wstring& path) -> std::uint32_t {
            if (step == CheckedWriteStep::CreateTemporary) {
                taken = path;
                WriteBytes(taken, "not the writer's");
            }
            return 0;
        });
    Check(r.failed_step == CheckedWriteStep::CreateTemporary && r.error == ERROR_FILE_EXISTS,
          "CREATE_NEW refuses the existing name");
    Check(r.temporary_path.empty() && !r.temporary_removed, "a file the writer did not create is not its temporary");
    Check(HoldsBytes(taken, "not the writer's"), "that file is untouched");
    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    Check(ListingIs(dir, {kFileName, taken.filename().wstring()}), "nothing else is left");
}

HANDLE OpenShared(const fs::path& path, DWORD access, DWORD share) {
    return CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void AHandleWithoutShareDeleteFailsTheCommit(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    const HANDLE other = OpenShared(target, GENERIC_READ, FILE_SHARE_READ);
    Check(other != INVALID_HANDLE_VALUE, "another handle is open");
    const CheckedWriteResult r = WriteFileChecked(target.wstring(), "a=1", "a=2");
    CloseHandle(other);
    Check(r.failed_step == CheckedWriteStep::Commit && r.error == ERROR_SHARING_VIOLATION,
          "the commit fails with the OS sharing violation");
    Check(r.temporary_removed, "the temporary is removed");
    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AnExclusiveHandleFailsTheRead(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    const HANDLE other = OpenShared(target, GENERIC_READ | GENERIC_WRITE, 0);
    Check(other != INVALID_HANDLE_VALUE, "another handle is open");
    const CheckedWriteResult r = WriteFileChecked(target.wstring(), "a=1", "a=2");
    CloseHandle(other);
    Check(r.failed_step == CheckedWriteStep::ReadTarget && r.error == ERROR_SHARING_VIOLATION,
          "the read fails with the OS sharing violation");
    Check(r.temporary_path.empty(), "no temporary was made");
    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void AReadOnlyTargetFailsAndStaysReadOnly(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY);
    const CheckedWriteResult r = WriteFileChecked(target.wstring(), "a=1", "a=2");
    Check(r.failed_step == CheckedWriteStep::Commit && r.error == ERROR_ACCESS_DENIED,
          "the commit fails with the OS access-denied error");
    Check(r.temporary_removed, "the temporary is removed");
    Check((GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_READONLY) != 0, "the read-only attribute is left alone");
    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void HiddenAndSystemAttributesAreKept(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");
    SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    Check(WriteFileChecked(target.wstring(), "a=1", "a=2").Committed(), "a hidden system file is replaced");
    const DWORD after = GetFileAttributesW(target.c_str());
    Check((after & FILE_ATTRIBUTE_HIDDEN) != 0 && (after & FILE_ATTRIBUTE_SYSTEM) != 0, "its attributes survive");
    Check(HoldsBytes(target, "a=2"), "the target holds the candidate");
    Check(ListingIs(dir, {kFileName}), "nothing else is left");
}

void UnrelatedTmpAndBakFilesAreUntouched(const fs::path& dir) {
    const fs::path target = dir / kFileName;
    const std::wstring file = kFileName;
    const std::vector<std::wstring> bystanders = {
        file + L".tmp", file + L".bak", L"HeadTracking.tmp", L"HeadTracking.bak",
        file + L".00000000000000000000000000000000.tmp",
    };
    for (const auto& name : bystanders) WriteBytes(dir / name, "keep " + fs::path(name).string());
    WriteBytes(target, "a=1");
    std::vector<std::wstring> everything = bystanders;
    everything.push_back(file);

    Check(WriteFileChecked(target.wstring(), "a=1", "a=2").Committed(), "commits");
    Check(ListingIs(dir, everything), "after a commit");
    Check(WriteFileCheckedWithFault(target.wstring(), "a=2", "a=3", FailAt(CheckedWriteStep::Commit)).failed_step ==
              CheckedWriteStep::Commit,
          "fails");
    Check(ListingIs(dir, everything), "after a failure");
    Check(WriteFileChecked(target.wstring(), "stale", "a=3").status == CheckedWriteStatus::TargetChanged, "conflicts");
    Check(ListingIs(dir, everything), "after a conflict");
    Check(WriteFileCheckedWithFault(target.wstring(), "a=2", "a=3",
                                    OnStep(CheckedWriteStep::RecheckTarget, [&] { WriteBytes(target, "a=9"); }))
                  .status == CheckedWriteStatus::TargetChanged,
          "conflicts after making its temporary");
    Check(ListingIs(dir, everything), "after a conflict with a temporary");

    bool intact = true;
    for (const auto& name : bystanders) intact = intact && HoldsBytes(dir / name, "keep " + fs::path(name).string());
    Check(intact, "every bystander holds its own bytes");
    Check(HoldsBytes(target, "a=9"), "the target holds the last edit");
}

void ANonAsciiPath(const fs::path& dir) {
    const fs::path folder = dir / L"Ünïcødé 日本語 ✓";
    fs::create_directory(folder);
    const std::wstring name = L"Kopf Ω €.ini";
    const fs::path target = folder / name;
    Check(WriteFileChecked(target.wstring(), std::nullopt, "a=1").Committed(), "creates");
    Check(WriteFileChecked(target.wstring(), "a=1", "a=2").Committed(), "replaces");
    const CheckedWriteResult r =
        WriteFileCheckedWithFault(target.wstring(), "a=2", "a=3", FailAt(CheckedWriteStep::Commit));
    const std::wstring prefix = target.wstring() + L".";
    Check(r.failed_step == CheckedWriteStep::Commit && r.temporary_removed &&
              r.temporary_path.compare(0, prefix.size(), prefix) == 0,
          "fails cleanly, reporting the temporary spelled as given");
    Check(HoldsBytes(target, "a=2"), "the target holds the last commit");
    Check(ListingIs(folder, {name}), "nothing else is left");
}

void ArgumentsAreCheckedBeforeAnyIo(const fs::path& dir) {
    bool empty = false;
    try {
        WriteFileChecked(L"", std::nullopt, "a");
    } catch (const std::invalid_argument&) {
        empty = true;
    }
    Check(empty, "an empty path throws std::invalid_argument");
    bool folder = false;
    try {
        WriteFileChecked(dir.wstring() + L"\\", std::nullopt, "a");
    } catch (const std::invalid_argument&) {
        folder = true;
    }
    Check(folder, "a folder path throws std::invalid_argument");
    Check(ListingIs(dir, {}), "nothing is written");
}

void Names() {
    Check(std::string(cameraunlock::CheckedWriteStatusName(CheckedWriteStatus::TargetReplaced)) == "TargetReplaced" &&
              std::string(cameraunlock::CheckedWriteStatusName(CheckedWriteStatus::Failed)) == "Failed",
          "status names are the C# spellings");
    Check(StepName(CheckedWriteStep::RecheckTarget) == "RecheckTarget" && StepName(CheckedWriteStep::None) == "None",
          "step names are the C# spellings");
}

// The child is killed at the start of the step, so every step before it has run.
void KilledDuring(CheckedWriteStep step, const fs::path& dir) {
    const fs::path target = dir / kFileName;
    WriteBytes(target, "a=1");

    std::vector<wchar_t> exe(32768);
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    Check(length > 0 && length < exe.size(), "found this executable");
    std::wstring command = L"\"" + std::wstring(exe.data(), length) + L"\" --checked-write-interrupt " +
                           std::to_wstring(static_cast<int>(step));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(exe.data(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        dir.c_str(), &startup, &process) != FALSE;
    Check(started, "started the child");
    if (!started) return;
    const bool exited = WaitForSingleObject(process.hProcess, 60000) == WAIT_OBJECT_0;
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Check(exited && code == kKilledExitCode, "the child was killed partway through");

    Check(HoldsBytes(target, "a=1"), "the target is unchanged");
    std::vector<fs::path> strays;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path() != target) strays.push_back(entry.path());
    }
    const std::size_t expected = step == CheckedWriteStep::CreateTemporary ? 0 : 1;
    const std::wstring prefix = std::wstring(kFileName) + L".";
    bool named = strays.size() == expected;
    for (const auto& stray : strays) {
        named = named && stray.filename().wstring().compare(0, prefix.size(), prefix) == 0;
        fs::remove(stray);
    }
    Check(named, "at most the child's own temporary is left beside it");
    Check(WriteFileChecked(target.wstring(), "a=1", "a=2").Committed(), "the next write commits");
}

void RunScenario(const std::string& name, const std::function<void(const fs::path&)>& body) {
    g_scenario = name;
    const fs::path dir = fs::temp_directory_path() / (L"cu-checked-writer-" + RandomName());
    fs::create_directories(dir);
    try {
        body(dir);
    } catch (const std::exception& e) {
        Check(false, std::string("threw ") + e.what());
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(dir);
}

}  // namespace

int RunCheckedFileWriterInterruptChild(const char* step) {
    const int killAt = std::atoi(step);
    WriteFileCheckedWithFault(kFileName, "a=1", "a=2",
                              [killAt](CheckedWriteStep s, const std::wstring&) -> std::uint32_t {
                                  if (static_cast<int>(s) == killAt) TerminateProcess(GetCurrentProcess(), kKilledExitCode);
                                  return 0;
                              });
    return 0;
}

int RunCheckedFileWriterTests() {
    std::cout << "\nCheckedFileWriter:\n";
    g_failures = 0;
    RunScenario("creates-an-absent-target", CreatesAnAbsentTarget);
    RunScenario("replaces-an-existing-target", ReplacesAnExistingTarget);
    RunScenario("empty-files-are-files", EmptyFilesAreFiles);
    RunScenario("runs-its-steps-in-order-beside-the-target", RunsItsStepsInOrderBesideTheTarget);
    RunScenario("stale-expected-bytes-write-nothing", StaleExpectedBytesWriteNothing);
    RunScenario("present-when-expected-absent-writes-nothing", PresentWhenExpectedAbsentWritesNothing);
    RunScenario("absent-when-expected-present-writes-nothing", AbsentWhenExpectedPresentWritesNothing);
    RunScenario("target-edited-before-the-recheck", TargetEditedBeforeTheRecheck);
    RunScenario("target-swapped-for-identical-bytes-before-the-recheck", TargetSwappedForIdenticalBytesBeforeTheRecheck);
    RunScenario("target-deleted-before-the-recheck", TargetDeletedBeforeTheRecheck);
    RunScenario("target-appeared-before-the-recheck", TargetAppearedBeforeTheRecheck);
    RunScenario("target-appeared-after-the-recheck-is-not-overwritten", TargetAppearedAfterTheRecheckIsNotOverwritten);
    RunScenario("every-step-failing-over-an-existing-target", EveryStepFailingOverAnExistingTarget);
    RunScenario("every-step-failing-over-an-absent-target", EveryStepFailingOverAnAbsentTarget);
    RunScenario("a-failed-removal-is-reported-not-hidden", AFailedRemovalIsReportedNotHidden);
    RunScenario("a-conflict-whose-removal-fails-is-reported", AConflictWhoseRemovalFailsIsReported);
    RunScenario("an-unfinished-replacement-keeps-the-temporary", AnUnfinishedReplacementKeepsTheTemporary);
    RunScenario("a-taken-temporary-name-is-not-deleted", ATakenTemporaryNameIsNotDeleted);
    RunScenario("a-handle-without-share-delete-fails-the-commit", AHandleWithoutShareDeleteFailsTheCommit);
    RunScenario("an-exclusive-handle-fails-the-read", AnExclusiveHandleFailsTheRead);
    RunScenario("a-read-only-target-fails-and-stays-read-only", AReadOnlyTargetFailsAndStaysReadOnly);
    RunScenario("hidden-and-system-attributes-are-kept", HiddenAndSystemAttributesAreKept);
    RunScenario("unrelated-tmp-and-bak-files-are-untouched", UnrelatedTmpAndBakFilesAreUntouched);
    RunScenario("a-non-ascii-path", ANonAsciiPath);
    RunScenario("arguments-are-checked-before-any-io", ArgumentsAreCheckedBeforeAnyIo);
    RunScenario("names", [](const fs::path&) { Names(); });
    for (CheckedWriteStep step : kStepsBeforeCommit) {
        if (step == CheckedWriteStep::ReadTarget) continue;
        RunScenario("killed-during-" + StepName(step), [step](const fs::path& dir) { KilledDuring(step, dir); });
    }
    return g_failures;
}

#else

int RunCheckedFileWriterInterruptChild(const char*) { return 1; }
int RunCheckedFileWriterTests() { return 0; }

#endif  // _WIN32
