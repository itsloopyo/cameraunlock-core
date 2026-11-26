// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#ifndef _WIN32
#error "config_watcher.h is Windows only: it waits on Windows change notifications, which the WebAssembly build has no import for"
#endif

#include <cameraunlock/config/config_owner.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cameraunlock::config {

/// A file as its folder's listing shows it.
///
/// ConfigOwner::FileChanged compares write times only, and Windows stamps a write with the
/// clock's current tick: two writes inside one tick (about 0.5 to 15.6 ms, whatever timer
/// resolution the machine runs at) carry the same time, so the last of a slider's quick writes
/// can look unchanged and never be read. An editor that writes a temporary and renames it over
/// the file leaves a new file ID with each write, so the ID, the change time and the size are
/// compared too. They come from the folder's listing, never from opening the file: an editor's
/// MoveFileExW fails while any handle is open on the file it replaces.
struct ConfigFileStamp {
    /// The Win32 error listing the folder, 0 when it was listed or is not there. A folder that
    /// stays unlistable compares equal to itself, so a watcher settles instead of reading for ever.
    std::uint32_t error = 0;
    bool present = false;
    std::int64_t id = 0;
    std::int64_t write_time = 0;
    std::int64_t change_time = 0;
    std::int64_t size = 0;

    bool operator==(const ConfigFileStamp& o) const {
        return error == o.error && present == o.present && id == o.id && write_time == o.write_time &&
               change_time == o.change_time && size == o.size;
    }
    bool operator!=(const ConfigFileStamp& o) const { return !(*this == o); }
};

/// The stamp of the file at `path`, read from its folder's listing. A missing folder holds no
/// file: its stamp is not present, with error 0. Throws std::invalid_argument for a path with no
/// folder in it.
ConfigFileStamp ReadConfigFileStamp(const std::wstring& path);

/// The folder part of `path`, before its last '\' or '/'. Throws std::invalid_argument for a
/// path with neither.
std::wstring ConfigFolderOf(const std::wstring& path);

namespace detail {

// Not API: ConfigWatcher's thread, in config_watcher.cpp. Opens a change notification on each
// folder and runs `step` on a thread of its own: once at once, then each time a folder signals,
// and again after `retry_ms` while `step` returns true. Stops when Stop is called, a wait or a
// re-arm fails, or `step` throws, logging why for the last three. `log` must not throw: it runs on
// the thread, where nothing catches it.
class ConfigWatchThread {
public:
    ConfigWatchThread();
    ConfigWatchThread(const ConfigWatchThread&) = delete;
    ConfigWatchThread& operator=(const ConfigWatchThread&) = delete;
    // Stops a running thread and waits for it. The wait ends when the thread says it is done or
    // when it has ended, so a DLL's static destructor under ExitProcess, where Windows has already
    // ended the thread, does not hang. A DLL that is unloaded while the process runs stops it first:
    // the thread's last instructions are in that DLL.
    ~ConfigWatchThread();

    // Throws std::logic_error while running, and std::runtime_error naming the call and the
    // Windows error when a folder cannot be watched or the thread does not start.
    void Start(const std::vector<std::wstring>& folders, std::uint32_t retry_ms, std::function<bool()> step,
               std::function<void(const std::string&)> log);
    // Throws std::logic_error when not started.
    void Stop();
    // Start was called and Stop has not been, whether or not the thread has stopped by itself.
    bool Running() const { return state_ != nullptr; }

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace detail

/// A ConfigOwner that applies edits to its file and to Defaults.ini as they land, woken by a
/// Windows change notification on their folders, so nothing is read while nothing changes.
///
/// A change counts when FileChanged says so or either file's ConfigFileStamp differs from the one
/// last read. Each read reads again while the files change under it (at most 8 times), so the
/// stamps it keeps are those of the files the result came from; a file that could not be read
/// yet, or never settled, is read again 50 ms later, as no further notification may come for it.
/// A notification for another file in the same folder finds nothing changed and reads nothing.
///
/// Every line goes to `log`, UTF-8, without a prefix, as the owner's own log lines. `log` runs
/// while the watcher holds its lock, on the caller's thread or the watcher's: it must not throw,
/// call back into the watcher or wait on a thread that does. `read` runs on the watcher's thread
/// with no lock held, and must not call Stop or destroy the watcher.
template <class Config>
class ConfigWatcher {
public:
    using LogFn = std::function<void(const std::string&)>;
    /// What Start's thread hands each read: the last Applied result of ReloadSettled, or its last
    /// result when none applied, so `read` checks the status and logs the result's lines itself.
    using ReadFn = std::function<void(ConfigReloadResult<Config>)>;

    static constexpr std::uint32_t kRetryUnreadMs = 50;
    static constexpr int kMaxRereads = 8;

    /// Throws std::invalid_argument for an empty `log`, and whatever ConfigOwner's constructor
    /// throws for `options`.
    ConfigWatcher(ConfigOwnerOptions<Config> options, LogFn log)
        : path_(options.path), log_(RequireLog(std::move(log))) {
        owner_ = std::make_unique<ConfigOwner<Config>>(std::move(options));
    }

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    /// For Save and Watched. Load, Reload and FileChanged go through the watcher, which keeps the
    /// stamps they were read at.
    ConfigOwner<Config>& Owner() { return *owner_; }

    /// ConfigOwner::Load, with the config file's stamp taken before it, so an edit landing during
    /// Load counts at the first Changed. Where Load creates or rewrites the file, that read finds
    /// the owner's own bytes and applies nothing.
    ConfigLoadResult<Config> Load() {
        std::lock_guard<std::mutex> lock(mutex_);
        const ConfigFileStamp before = Stamp(path_, game_logged_);
        ConfigLoadResult<Config> result = owner_->Load();
        seen_.game = before;
        // Load chose, and maybe created, Defaults.ini, so its path is known only now.
        seen_.defaults = ReadSeen().defaults;
        return result;
    }

    /// Whether either file differs from the last read. Throws std::logic_error before Load.
    bool Changed() {
        std::lock_guard<std::mutex> lock(mutex_);
        return ChangedLocked();
    }

    /// ConfigOwner::Reload, again while the files change under it. Throws std::logic_error before Load.
    ConfigReloadResult<Config> ReloadSettled() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<ConfigReloadResult<Config>> applied;
        ConfigReloadResult<Config> last;
        for (int i = 0; i < kMaxRereads; ++i) {
            const Seen before = ReadSeen();
            last = owner_->Reload();
            if (last.status == ConfigReloadStatus::Applied) applied = last;
            // Where the files never settle, the stamps from before the last Reload stay, which
            // differ from the files', so the next Changed reads again.
            seen_ = before;
            if (ReadSeen() == before) break;
        }
        return applied ? std::move(*applied) : std::move(last);
    }

    /// The folders Start waits on: the config file's, then Defaults.ini's where Load found one in
    /// another folder. Throws std::logic_error before Load.
    std::vector<std::wstring> Folders() {
        const ConfigWatched watched = owner_->Watched();
        std::vector<std::wstring> folders{ConfigFolderOf(watched.path)};
        if (watched.defaults_path) {
            const std::wstring defaults = ConfigFolderOf(*watched.defaults_path);
            if (!detail::OwnerSamePath(defaults, folders[0])) folders.push_back(defaults);
        }
        return folders;
    }

    /// Starts the thread that reads each change and hands the result to `read`. Its first pass
    /// does not wait, as an edit made after Load and before the notifications existed raised none.
    /// A `read` that throws stops the thread, logged. Throws std::logic_error before Load or while
    /// started, and std::runtime_error, nothing logged, when the folders cannot be watched or the
    /// thread does not start. The lines saying the watch started are logged once it has, so a line
    /// the thread logs at its first pass can come before them.
    void Start(ReadFn read) {
        if (!read) throw std::invalid_argument("ConfigWatcher::Start needs a read");
        if (thread_.Running()) throw std::logic_error("ConfigWatcher::Start is called once until Stop");
        const std::vector<std::wstring> folders = Folders();
        thread_.Start(
            folders, kRetryUnreadMs,
            [this, read = std::move(read)] {
                if (!Changed()) return false;
                read(ReloadSettled());
                return Changed();
            },
            log_);
        if (!owner_->Watched().defaults_path) log_("no Defaults.ini was found, so only the config file is watched");
        log_("watching for edits, applied as they land");
    }

    /// Ends Start's thread and waits for it. Throws std::logic_error when not started.
    void Stop() { thread_.Stop(); }

    /// Start was called and Stop has not been, whether or not the thread has stopped by itself.
    bool Running() const { return thread_.Running(); }

private:
    struct Seen {
        ConfigFileStamp game;
        ConfigFileStamp defaults;
        bool operator==(const Seen& o) const { return game == o.game && defaults == o.defaults; }
        bool operator!=(const Seen& o) const { return !(*this == o); }
    };

    static LogFn RequireLog(LogFn log) {
        if (!log) throw std::invalid_argument("ConfigWatcher needs a log");
        return log;
    }

    // A listing error is logged once, until another error or a listing that works.
    ConfigFileStamp Stamp(const std::wstring& path, std::uint32_t& logged) {
        const ConfigFileStamp stamp = ReadConfigFileStamp(path);
        if (stamp.error != 0 && stamp.error != logged) {
            log_("listing the folder of " + detail::OwnerUtf8(path) + " failed with Windows error " +
                 std::to_string(stamp.error) +
                 ", so an edit made in the same clock tick as the one before it may wait for the next edit");
        }
        logged = stamp.error;
        return stamp;
    }

    Seen ReadSeen() {
        const ConfigWatched watched = owner_->Watched();
        Seen now;
        now.game = Stamp(watched.path, game_logged_);
        if (watched.defaults_path) now.defaults = Stamp(*watched.defaults_path, defaults_logged_);
        return now;
    }

    bool ChangedLocked() { return owner_->FileChanged() || ReadSeen() != seen_; }

    const std::wstring path_;
    const LogFn log_;
    std::unique_ptr<ConfigOwner<Config>> owner_;
    std::mutex mutex_;
    Seen seen_;
    std::uint32_t game_logged_ = 0;
    std::uint32_t defaults_logged_ = 0;
    // Last, so it is destroyed first: its thread calls into the members above.
    detail::ConfigWatchThread thread_;
};

}  // namespace cameraunlock::config
