// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include "cameraunlock/config/config_watcher.h"

#include "cameraunlock/os/win32.h"

#include <exception>
#include <system_error>
#include <thread>

namespace cameraunlock::config {

namespace {

// Lists the folder open as `dir` up to the file `name` and fills `stamp` from its entry, or sets
// stamp.error where the listing fails.
void FindInListing(HANDLE dir, const std::wstring& name, ConfigFileStamp& stamp) {
    alignas(8) unsigned char buffer[16 * 1024];
    FILE_INFO_BY_HANDLE_CLASS info = FileIdBothDirectoryRestartInfo;
    for (;;) {
        if (!GetFileInformationByHandleEx(dir, info, buffer, sizeof buffer)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) stamp.error = error;
            return;
        }
        info = FileIdBothDirectoryInfo;
        const unsigned char* at = buffer;
        for (;;) {
            const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(at);
            const int length = static_cast<int>(entry->FileNameLength / sizeof(wchar_t));
            if (CompareStringOrdinal(entry->FileName, length, name.c_str(), static_cast<int>(name.size()), TRUE) ==
                CSTR_EQUAL) {
                stamp.present = true;
                stamp.id = entry->FileId.QuadPart;
                stamp.write_time = entry->LastWriteTime.QuadPart;
                stamp.change_time = entry->ChangeTime.QuadPart;
                stamp.size = entry->EndOfFile.QuadPart;
            }
            if (entry->NextEntryOffset == 0) break;
            at += entry->NextEntryOffset;
        }
        if (stamp.present) return;
    }
}

std::runtime_error NotWatched(const std::string& what, DWORD error) {
    return std::runtime_error("edits to the config are not watched, so an edit waits for the next start: " + what +
                              " failed with Windows error " + std::to_string(error));
}

}  // namespace

std::wstring ConfigFolderOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        throw std::invalid_argument("the config path " + detail::OwnerUtf8(path) + " has no folder in it");
    }
    return path.substr(0, slash);
}

ConfigFileStamp ReadConfigFileStamp(const std::wstring& path) {
    const std::wstring folder = ConfigFolderOf(path);
    const std::wstring name = path.substr(folder.size() + 1);
    ConfigFileStamp stamp;
    const HANDLE dir = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
        stamp.error = GetLastError();
        // A folder that is not there holds no file: the owner counts that as a missing file too.
        if (stamp.error == ERROR_FILE_NOT_FOUND || stamp.error == ERROR_PATH_NOT_FOUND) stamp.error = 0;
        return stamp;
    }
    FindInListing(dir, name, stamp);
    CloseHandle(dir);
    return stamp;
}

namespace detail {

struct ConfigWatchThread::State {
    HANDLE stop = nullptr;
    HANDLE stopped = nullptr;
    // The thread's own handle, signalled when it ends however it ends.
    HANDLE thread = nullptr;
};

namespace {

// The thread: runs `step` each time one of `handles` signals, until `stop` is set, a wait or a
// re-arm fails or `step` throws, then closes the handles.
void RunWatch(const std::vector<HANDLE>& handles, HANDLE stop, std::uint32_t retry_ms, const std::function<bool()>& step,
              const std::function<void(const std::string&)>& log) {
    std::vector<HANDLE> waits = handles;
    waits.push_back(stop);
    DWORD wait = 0;
    for (;;) {
        const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, wait);
        if (woke == WAIT_OBJECT_0 + handles.size()) break;
        if (woke != WAIT_TIMEOUT) {
            if (woke >= WAIT_OBJECT_0 + handles.size()) {
                log("watching for edits stopped: the wait failed with Windows error " + std::to_string(GetLastError()));
                break;
            }
            // Asked again before reading, so an edit landing during the read signals again.
            if (!FindNextChangeNotification(handles[woke - WAIT_OBJECT_0])) {
                log("watching for edits stopped: re-arming failed with Windows error " + std::to_string(GetLastError()));
                break;
            }
        }
        try {
            wait = step() ? retry_ms : INFINITE;
        } catch (const std::exception& e) {
            log(std::string("watching for edits stopped: applying an edit threw: ") + e.what());
            break;
        } catch (...) {
            log("watching for edits stopped: applying an edit threw something that is not a std::exception");
            break;
        }
    }
    for (const HANDLE h : handles) FindCloseChangeNotification(h);
}

void CloseAll(const std::vector<HANDLE>& handles, HANDLE stop, HANDLE stopped) {
    for (const HANDLE h : handles) FindCloseChangeNotification(h);
    if (stop) CloseHandle(stop);
    if (stopped) CloseHandle(stopped);
}

}  // namespace

ConfigWatchThread::ConfigWatchThread() = default;

ConfigWatchThread::~ConfigWatchThread() {
    if (state_) Stop();
}

void ConfigWatchThread::Start(const std::vector<std::wstring>& folders, std::uint32_t retry_ms, std::function<bool()> step,
                              std::function<void(const std::string&)> log) {
    if (state_) throw std::logic_error("ConfigWatchThread::Start while it runs");
    std::vector<HANDLE> handles;
    HANDLE stop = nullptr;
    HANDLE stopped = nullptr;
    // Each failure reads its Windows error before anything else can change it.
    const auto fail = [&](const std::string& what, DWORD error) {
        CloseAll(handles, stop, stopped);
        return NotWatched(what, error);
    };
    // A file written, created, or renamed over (an editor writes a temporary and renames it over
    // the file) signals its folder.
    for (const std::wstring& folder : folders) {
        const HANDLE h = FindFirstChangeNotificationW(
            folder.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            throw fail("watching a config folder", error);
        }
        handles.push_back(h);
    }
    stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop) {
        const DWORD error = GetLastError();
        throw fail("making the watcher's stop event", error);
    }
    stopped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopped) {
        const DWORD error = GetLastError();
        throw fail("making the watcher's stopped event", error);
    }
    auto state = std::make_unique<State>();
    state->stop = stop;
    state->stopped = stopped;
    // Detached, so no std::thread is left joinable at exit, and Stop waits for `stopped` or the
    // thread's end: a join from a DLL's static destructor under FreeLibrary waits for a thread
    // that needs the loader lock to end, while `stopped` is set before the thread returns.
    std::thread thread;
    try {
        thread = std::thread([handles, stop, stopped, retry_ms, step = std::move(step), log = std::move(log)]() mutable {
            RunWatch(handles, stop, retry_ms, step, log);
            step = nullptr;
            log = nullptr;
            SetEvent(stopped);
        });
    } catch (const std::exception& e) {
        CloseAll(handles, stop, stopped);
        throw std::runtime_error(std::string("edits to the config are not watched, so an edit waits for the next start: "
                                             "the watcher's thread did not start (") +
                                 e.what() + ")");
    }
    if (!DuplicateHandle(GetCurrentProcess(), thread.native_handle(), GetCurrentProcess(), &state->thread, SYNCHRONIZE,
                         FALSE, 0)) {
        const DWORD error = GetLastError();
        SetEvent(stop);
        thread.join();
        CloseHandle(stop);
        CloseHandle(stopped);
        throw NotWatched("taking the watcher thread's handle", error);
    }
    thread.detach();
    state_ = std::move(state);
}

void ConfigWatchThread::Stop() {
    if (!state_) throw std::logic_error("ConfigWatchThread::Stop with no Start");
    SetEvent(state_->stop);
    const HANDLE ends[] = {state_->stopped, state_->thread};
    WaitForMultipleObjects(2, ends, FALSE, INFINITE);
    CloseHandle(state_->stop);
    CloseHandle(state_->stopped);
    CloseHandle(state_->thread);
    state_.reset();
}

}  // namespace detail

}  // namespace cameraunlock::config
