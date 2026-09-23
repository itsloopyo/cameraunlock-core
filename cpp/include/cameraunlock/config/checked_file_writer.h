#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cameraunlock {

/// What WriteFileChecked did. Committed through TargetReplaced carry the numbers of
/// CameraUnlock.Core.Config's CheckedWriteOutcome. Failed is where the C# writer throws
/// CheckedWriteException instead.
enum class CheckedWriteStatus {
    Committed = 0,
    /// The target's bytes are not the expected ones.
    TargetChanged = 1,
    /// The target was expected to be absent and a file is there.
    TargetAppeared = 2,
    /// The target was expected to exist and is gone.
    TargetMissing = 3,
    /// The target holds the expected bytes, but it is no longer the same file: something
    /// replaced it between the writer's first read and its final check.
    TargetReplaced = 4,
    /// A step failed; see CheckedWriteResult::failed_step and error.
    Failed = 5,
};

/// The steps of WriteFileChecked, in order. The numbers match CameraUnlock.Core.Config's
/// CheckedWriteStep, which has no None.
enum class CheckedWriteStep {
    None = 0,
    /// Reading the target's bytes and identity before anything is written.
    ReadTarget = 1,
    /// Creating the temporary beside the target, failing if the name exists.
    CreateTemporary = 2,
    WriteTemporary = 3,
    /// FlushFileBuffers on the temporary.
    FlushTemporary = 4,
    CloseTemporary = 5,
    /// Reading the target's bytes and identity again, just before the commit.
    RecheckTarget = 6,
    /// ReplaceFileW over an existing target, MoveFileExW into place for an absent one.
    Commit = 7,
    /// Deleting the temporary after a failure or a conflict.
    RemoveTemporary = 8,
};

/// The C# enum member's spelling, e.g. "TargetChanged".
const char* CheckedWriteStatusName(CheckedWriteStatus status);
const char* CheckedWriteStepName(CheckedWriteStep step);

struct CheckedWriteResult {
    CheckedWriteStatus status = CheckedWriteStatus::Failed;
    /// The step that failed and the Win32 error it gave. None and 0 unless status is Failed.
    CheckedWriteStep failed_step = CheckedWriteStep::None;
    std::uint32_t error = 0;
    /// ReplaceFileW reported ERROR_UNABLE_TO_MOVE_REPLACEMENT or _2: it could not finish a
    /// replacement it had started, so the target may be missing or renamed. The temporary
    /// holds the new contents, may be the only copy, and is left at temporary_path.
    bool outcome_uncertain = false;
    /// The temporary this call created, empty when it created none. A file already sitting
    /// at that name is never counted as created, so it is never deleted.
    std::wstring temporary_path;
    /// True once the temporary has been deleted after a failure or conflict. False when
    /// none was created, when outcome_uncertain is set and when deleting it failed.
    bool temporary_removed = false;
    /// The Win32 error from deleting the temporary, or else from closing it, or 0.
    std::uint32_t cleanup_error = 0;

    bool Committed() const { return status == CheckedWriteStatus::Committed; }
};

#ifdef _WIN32

/// Writes `candidate` to `path` if the file there still holds `expected`, or is still
/// absent when `expected` is empty (std::nullopt; an empty string is an empty file).
///
/// The target is read, bytes and file identity (volume serial and file index), and
/// compared with `expected`. The candidate goes into a new temporary beside it, named
/// `<file name>.<32 hex digits>.tmp` and opened with CREATE_NEW, then flushed with
/// FlushFileBuffers and closed, each result checked. The target is read again, and the
/// write goes ahead only if its bytes still match and it is still the same file. An
/// existing target is swapped for the temporary with ReplaceFileW, which keeps the
/// target's attributes, and no backup is made. An absent one is created by
/// MoveFileExW(MOVEFILE_WRITE_THROUGH) without MOVEFILE_REPLACE_EXISTING, which fails
/// rather than overwrite a file that appeared after the check.
///
/// The target is never opened for writing, truncated or deleted. A read-only target fails
/// with the error Windows gives; its attribute is left alone.
///
/// On a conflict or a failure the temporary is deleted by the exact name this call
/// created, never by pattern, so an unrelated .tmp or .bak beside the target is not
/// touched. The one exception is outcome_uncertain, where the temporary may be the only
/// copy left and stays.
///
/// The final check and the replacement are two operations, so a program that writes the
/// target in between is overwritten. This is not compare-and-swap. The caller serializes
/// its own writes to one file; this function holds no lock.
///
/// Throws std::invalid_argument when `path` is empty or does not name a file.
CheckedWriteResult WriteFileChecked(const std::wstring& path,
                                    const std::optional<std::string>& expected,
                                    const std::string& candidate);

#endif  // _WIN32

}  // namespace cameraunlock
