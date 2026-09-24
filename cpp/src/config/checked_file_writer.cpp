#include <cameraunlock/config/checked_file_writer.h>

#include "checked_file_writer_internal.h"

namespace cameraunlock {

const char* CheckedWriteStatusName(CheckedWriteStatus status) {
    switch (status) {
        case CheckedWriteStatus::Committed: return "Committed";
        case CheckedWriteStatus::TargetChanged: return "TargetChanged";
        case CheckedWriteStatus::TargetAppeared: return "TargetAppeared";
        case CheckedWriteStatus::TargetMissing: return "TargetMissing";
        case CheckedWriteStatus::TargetReplaced: return "TargetReplaced";
        case CheckedWriteStatus::Failed: return "Failed";
    }
    return "?";
}

const char* CheckedWriteStepName(CheckedWriteStep step) {
    switch (step) {
        case CheckedWriteStep::None: return "None";
        case CheckedWriteStep::ReadTarget: return "ReadTarget";
        case CheckedWriteStep::CreateTemporary: return "CreateTemporary";
        case CheckedWriteStep::WriteTemporary: return "WriteTemporary";
        case CheckedWriteStep::FlushTemporary: return "FlushTemporary";
        case CheckedWriteStep::CloseTemporary: return "CloseTemporary";
        case CheckedWriteStep::RecheckTarget: return "RecheckTarget";
        case CheckedWriteStep::Commit: return "Commit";
        case CheckedWriteStep::RemoveTemporary: return "RemoveTemporary";
    }
    return "?";
}

}  // namespace cameraunlock

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cameraunlock {
namespace {

struct Snapshot {
    bool exists = false;
    std::string bytes;
    DWORD volume = 0;
    DWORD index_high = 0;
    DWORD index_low = 0;
};

std::wstring RandomHex32() {
    static const wchar_t kDigits[] = L"0123456789abcdef";
    std::random_device random;
    std::wstring hex;
    hex.reserve(32);
    for (int word = 0; word < 4; ++word) {
        std::uint32_t bits = random();
        for (int nibble = 0; nibble < 8; ++nibble) {
            hex.push_back(kDigits[bits & 0xF]);
            bits >>= 4;
        }
    }
    return hex;
}

std::wstring FullPath(const std::wstring& path) {
    DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (size == 0) {
        throw std::invalid_argument("WriteFileChecked: the path cannot be made absolute");
    }
    std::vector<wchar_t> buffer(size);
    DWORD length = GetFullPathNameW(path.c_str(), size, buffer.data(), nullptr);
    if (length == 0 || length >= size) {
        throw std::invalid_argument("WriteFileChecked: the path cannot be made absolute");
    }
    return std::wstring(buffer.data(), length);
}

class Attempt {
public:
    Attempt(std::wstring target, std::wstring temporary, const detail::CheckedWriteFault& fault)
        : target_(std::move(target)), temporary_(std::move(temporary)), fault_(fault) {}

    CheckedWriteResult Run(const std::optional<std::string>& expected, const std::string& candidate) {
        Snapshot first;
        if (DWORD error = Read(CheckedWriteStep::ReadTarget, first)) {
            return Fail(CheckedWriteStep::ReadTarget, error, false);
        }
        CheckedWriteStatus status = Compare(expected, first, first);
        if (status != CheckedWriteStatus::Committed) return Conflict(status);

        if (DWORD error = Before(CheckedWriteStep::CreateTemporary, temporary_)) {
            return Fail(CheckedWriteStep::CreateTemporary, error, false);
        }
        handle_ = CreateFileW(temporary_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            return Fail(CheckedWriteStep::CreateTemporary, GetLastError(), false);
        }
        created_ = true;

        if (DWORD error = Before(CheckedWriteStep::WriteTemporary, temporary_)) {
            return Fail(CheckedWriteStep::WriteTemporary, error, false);
        }
        std::size_t offset = 0;
        while (offset < candidate.size()) {
            const DWORD chunk = static_cast<DWORD>(
                (std::min)(candidate.size() - offset, static_cast<std::size_t>(1) << 20));
            DWORD written = 0;
            if (!WriteFile(handle_, candidate.data() + offset, chunk, &written, nullptr)) {
                return Fail(CheckedWriteStep::WriteTemporary, GetLastError(), false);
            }
            // A synchronous WriteFile to a file either writes everything or fails; a zero
            // count here would otherwise loop forever.
            if (written == 0) return Fail(CheckedWriteStep::WriteTemporary, ERROR_WRITE_FAULT, false);
            offset += written;
        }

        if (DWORD error = Before(CheckedWriteStep::FlushTemporary, temporary_)) {
            return Fail(CheckedWriteStep::FlushTemporary, error, false);
        }
        if (!FlushFileBuffers(handle_)) {
            return Fail(CheckedWriteStep::FlushTemporary, GetLastError(), false);
        }

        if (DWORD error = Before(CheckedWriteStep::CloseTemporary, temporary_)) {
            return Fail(CheckedWriteStep::CloseTemporary, error, false);
        }
        const HANDLE closing = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        if (!CloseHandle(closing)) {
            return Fail(CheckedWriteStep::CloseTemporary, GetLastError(), false);
        }

        Snapshot second;
        if (DWORD error = Read(CheckedWriteStep::RecheckTarget, second)) {
            return Fail(CheckedWriteStep::RecheckTarget, error, false);
        }
        status = Compare(expected, first, second);
        if (status != CheckedWriteStatus::Committed) return Conflict(status);

        const bool creating = !second.exists;
        DWORD error = Before(CheckedWriteStep::Commit, target_);
        if (error == 0) {
            const BOOL done =
                creating ? MoveFileExW(temporary_.c_str(), target_.c_str(), MOVEFILE_WRITE_THROUGH)
                         : ReplaceFileW(target_.c_str(), temporary_.c_str(), nullptr, 0, nullptr, nullptr);
            if (!done) error = GetLastError();
        }
        if (error == 0) return CommittedResult();
        if (creating && (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)) {
            return Conflict(CheckedWriteStatus::TargetAppeared);
        }
        const bool uncertain = !creating && (error == ERROR_UNABLE_TO_MOVE_REPLACEMENT ||
                                             error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2);
        if (!uncertain) return Fail(CheckedWriteStep::Commit, error, false);

        // ReplaceFileW can stop with the target already deleted or renamed. Left like that,
        // the next launch finds no file and writes defaults over the user's values.
        DWORD completion = 0;
        if (GetFileAttributesW(target_.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (MoveFileExW(temporary_.c_str(), target_.c_str(), MOVEFILE_WRITE_THROUGH)) return CommittedResult();
            completion = GetLastError();
        }
        CheckedWriteResult result = Fail(CheckedWriteStep::Commit, error, true);
        result.completion_error = completion;
        return result;
    }

private:
    static CheckedWriteResult CommittedResult() {
        CheckedWriteResult result;
        result.status = CheckedWriteStatus::Committed;
        return result;
    }

    DWORD Before(CheckedWriteStep step, const std::wstring& path) const {
        return fault_ ? static_cast<DWORD>(fault_(step, path)) : 0;
    }

    static CheckedWriteStatus Compare(const std::optional<std::string>& expected, const Snapshot& first,
                                      const Snapshot& current) {
        if (!expected) {
            return current.exists ? CheckedWriteStatus::TargetAppeared : CheckedWriteStatus::Committed;
        }
        if (!current.exists) return CheckedWriteStatus::TargetMissing;
        if (current.bytes != *expected) return CheckedWriteStatus::TargetChanged;
        if (current.volume != first.volume || current.index_high != first.index_high ||
            current.index_low != first.index_low) {
            return CheckedWriteStatus::TargetReplaced;
        }
        return CheckedWriteStatus::Committed;
    }

    DWORD Read(CheckedWriteStep step, Snapshot& out) const {
        if (DWORD error = Before(step, target_)) return error;
        const HANDLE file = CreateFileW(target_.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                out = Snapshot{};
                return 0;
            }
            return error;
        }

        Snapshot snapshot;
        snapshot.exists = true;
        DWORD error = 0;
        BY_HANDLE_FILE_INFORMATION information;
        if (GetFileInformationByHandle(file, &information)) {
            snapshot.volume = information.dwVolumeSerialNumber;
            snapshot.index_high = information.nFileIndexHigh;
            snapshot.index_low = information.nFileIndexLow;
            char buffer[4096];
            for (;;) {
                DWORD read = 0;
                if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
                    error = GetLastError();
                    break;
                }
                if (read == 0) break;
                snapshot.bytes.append(buffer, read);
            }
        } else {
            error = GetLastError();
        }
        if (!CloseHandle(file) && error == 0) error = GetLastError();
        if (error == 0) out = std::move(snapshot);
        return error;
    }

    void RemoveTemporary(CheckedWriteResult& result) const {
        result.temporary_path = temporary_;
        if (DWORD error = Before(CheckedWriteStep::RemoveTemporary, temporary_)) {
            result.cleanup_error = error;
        } else if (!DeleteFileW(temporary_.c_str())) {
            result.cleanup_error = GetLastError();
        } else {
            result.temporary_removed = true;
        }
    }

    CheckedWriteResult Conflict(CheckedWriteStatus status) const {
        CheckedWriteResult result;
        result.status = status;
        if (created_) RemoveTemporary(result);
        return result;
    }

    CheckedWriteResult Fail(CheckedWriteStep step, DWORD error, bool uncertain) {
        CheckedWriteResult result;
        result.status = CheckedWriteStatus::Failed;
        result.failed_step = step;
        result.error = error;
        result.outcome_uncertain = uncertain;
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(handle_)) result.cleanup_error = GetLastError();
            handle_ = INVALID_HANDLE_VALUE;
        }
        if (created_) {
            result.temporary_path = temporary_;
            if (!uncertain) RemoveTemporary(result);
        }
        return result;
    }

    const std::wstring target_;
    const std::wstring temporary_;
    const detail::CheckedWriteFault& fault_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool created_ = false;
};

}  // namespace

namespace detail {

CheckedWriteResult WriteFileCheckedWithFault(const std::wstring& path,
                                             const std::optional<std::string>& expected,
                                             const std::string& candidate,
                                             const CheckedWriteFault& fault) {
    if (path.empty()) throw std::invalid_argument("WriteFileChecked: the path is empty");
    const std::wstring target = FullPath(path);
    const std::size_t separator = target.find_last_of(L"\\/");
    if (separator == std::wstring::npos || separator + 1 == target.size()) {
        throw std::invalid_argument("WriteFileChecked: the path does not name a file");
    }
    const std::wstring temporary = target + L"." + RandomHex32() + L".tmp";
    return Attempt(target, temporary, fault).Run(expected, candidate);
}

}  // namespace detail

CheckedWriteResult WriteFileChecked(const std::wstring& path, const std::optional<std::string>& expected,
                                    const std::string& candidate) {
    return detail::WriteFileCheckedWithFault(path, expected, candidate, nullptr);
}

}  // namespace cameraunlock

#endif  // _WIN32
