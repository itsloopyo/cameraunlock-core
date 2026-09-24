#pragma once

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/checked_file_writer.h>
#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/ini_editor.h>
#include <cameraunlock/config/legacy_import.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// The one reader and writer of a game's canonical config file: ConfigOwner<Config>. The C#
// twin is CameraUnlock.Core.Config.ConfigOwner<TConfig>; the statuses carry the same numbers,
// the player messages the same words, and the two make the same decisions. The statuses,
// options and results compile everywhere; the owner itself is Windows only, as the checked
// writer it writes through is.

namespace cameraunlock::config {

/// What ConfigOwner::Load found and did. The numbers match CameraUnlock.Core.Config's
/// ConfigLoadStatus.
enum class ConfigLoadStatus {
    /// The file was read as a canonical file: stamped, or unstamped in a game with no legacy
    /// import, which the first save stamps.
    Canonical = 0,
    /// A legacy file was converted to the canonical format this launch.
    Migrated = 1,
    /// There was no file, and one holding the defaults was created.
    Created = 2,
    /// The file could not be converted, read or created this launch. It is left as it was, the
    /// session runs on the settings the result holds, nothing is saved this session, and the
    /// next launch tries again.
    Deferred = 3,
    /// The legacy import refused the file, as the game's last pre-canonical build did. The game
    /// does what that build did on the refusal; the file is left as it was and nothing is saved
    /// this session.
    LegacyRefused = 4,
    /// A file the canonical reader cannot read (saved as UTF-16, or holding a NUL byte) that is
    /// stamped, or that no legacy import reads. The session runs on the defaults and nothing is
    /// saved until the file is fixed.
    Unreadable = 5,
};

/// What ConfigOwner::Save did. The numbers match CameraUnlock.Core.Config's ConfigSaveStatus.
enum class ConfigSaveStatus {
    /// The file holds the new values, or already held them and nothing was written.
    Saved = 0,
    /// Nothing was written and the file is as it was.
    NotSaved = 1,
    /// Windows started replacing the file and did not finish, so the file may be missing or
    /// renamed. The new contents are in ConfigSaveResult::temporary_path.
    Uncertain = 2,
};

/// What ConfigOwner::Reload found. Reload never writes. The numbers match
/// CameraUnlock.Core.Config's ConfigReloadStatus.
enum class ConfigReloadStatus {
    /// The file holds the bytes the owner last wrote, so there is nothing to apply.
    Unchanged = 0,
    /// The file was read and ConfigReloadResult::config holds its settings.
    Applied = 1,
    /// The file has no [CameraUnlock] stamp and the game's legacy import reads it: an old file
    /// put back during the session. It was read through the import, is not written, and is
    /// converted at the next launch.
    LegacyReadOnly = 2,
    /// The file could not be read. The game keeps the settings it has.
    Unreadable = 3,
};

/// The C# enum member's spelling, e.g. "Migrated". Throw std::invalid_argument for a value
/// outside the enum.
const char* ConfigLoadStatusName(ConfigLoadStatus status);
const char* ConfigSaveStatusName(ConfigSaveStatus status);
const char* ConfigReloadStatusName(ConfigReloadStatus status);

/// What a ConfigOwner is built from. Fill it by member assignment, never positionally, so a
/// member added later changes no game's code. The owner copies it when it is built.
template <class Config>
struct ConfigOwnerOptions {
    /// The config file, as a fully qualified path (`C:\...` or `\\server\...`). Required.
    std::wstring path;
    /// The game's table. Required: a table with no rows is refused.
    ConfigTable<Config> table;
    /// The game's frozen legacy import. An empty `run` means the game never published a build
    /// reading a pre-canonical file. With an import, a file with no [CameraUnlock] stamp is a
    /// legacy file: it is converted once, never edited. The import is handed the path in the
    /// ANSI code page too, and whether that form lost a character (LegacyInput).
    LegacyImport<Config> import;
    /// What the renderer writes above the settings. Required.
    RenderHeader header;
    /// Shows the player a one-line message, e.g. through the game's overlay, when settings are
    /// not converted, cannot be read or are not saved. Optional. It runs after the owner has
    /// released its lock.
    std::function<void(const std::string&)> status_sink;
};

/// What ConfigOwner::Load returns. Nothing is logged by the owner.
template <class Config>
struct ConfigLoadResult {
    ConfigLoadStatus status = ConfigLoadStatus::Canonical;
    /// The settings the session runs on.
    Config config;
    /// What the canonical reader and the table found in the file read, for a Canonical or a
    /// Migrated load; empty otherwise.
    std::vector<CanonicalDiagnostic> diagnostics;
    /// Every line for the game's log, UTF-8, each naming the file, in order: the diagnostics'
    /// sentences, the conversion's lines (values the import dropped, lines the new file does not
    /// carry, where the original is kept) and the error behind a Deferred or Unreadable load.
    /// Returned rather than logged, so a game can load before its logger is up.
    std::vector<std::string> log;
    /// For Deferred, LegacyRefused and Unreadable, the message for the player, which the owner
    /// also hands its status sink once; empty otherwise.
    std::string reason;
};

/// What ConfigOwner::Save returns. Nothing is logged by the owner.
struct ConfigSaveResult {
    ConfigSaveStatus status = ConfigSaveStatus::Saved;
    /// For NotSaved and Uncertain, the message for the player, which the owner also hands its
    /// status sink; empty for Saved. Uncertain names the file and the kept temporary.
    std::string reason;
    /// The Win32 error that stopped the save: the writer's, or the one reading the file. 0 when
    /// there was none, e.g. for a conflict.
    std::uint32_t error = 0;
    /// For Uncertain, the temporary holding the new contents, left in place; empty otherwise.
    std::wstring temporary_path;
    /// The lines for the game's log, UTF-8, naming the file and the operation. Empty for Saved.
    std::vector<std::string> log;
};

/// What ConfigOwner::Reload returns. Nothing is logged by the owner.
template <class Config>
struct ConfigReloadResult {
    ConfigReloadStatus status = ConfigReloadStatus::Unchanged;
    /// For Applied and LegacyReadOnly, the settings read; empty for Unchanged and Unreadable,
    /// where the game keeps the settings it has.
    std::optional<Config> config;
    /// What the canonical reader and the table found, for Applied; empty otherwise.
    std::vector<CanonicalDiagnostic> diagnostics;
    /// Every line for the game's log, UTF-8, each naming the file, in order.
    std::vector<std::string> log;
    /// For Unreadable, the message for the player, which the owner also hands its status sink;
    /// for LegacyReadOnly, the message saying the file is read but not saved; empty otherwise.
    std::string reason;
};

#ifdef _WIN32

namespace detail {

// Not API: ConfigOwner's Windows half, in config_owner.cpp.

// The test seam. Run before each step with a label and the path the step acts on: `Open`,
// `Import`, `Recheck`, `ReadBack` (of the copy) and `Remember` for the conversion's own steps,
// and `Copy.`, `Commit.`, `Create.` or `Save.` followed by a CheckedWriteStepName for the
// writer's. At a writer step a nonzero return fails that step with that Win32 error, as the
// writer's own fault seam does; at the owner's steps it must return 0. A test ends the process
// from it to interrupt a step, or changes the files to race one.
using ConfigOwnerHook = std::function<std::uint32_t(const std::string& label, const std::wstring& path)>;

struct OwnerFileRead {
    /// False when there is no file: ERROR_FILE_NOT_FOUND or ERROR_PATH_NOT_FOUND.
    bool present = false;
    /// The Win32 error opening or reading the file, 0 when it was read.
    std::uint32_t error = 0;
    std::string bytes;
};

// Design 4.5 step 1: the file held open for reading, sharing read and write but not delete,
// so no program can newly open it denying read sharing, rename it or delete it while it is
// held. ReplaceFileW fails while it is open, so it is closed before any commit.
class OwnerHeldFile {
public:
    OwnerHeldFile() = default;
    OwnerHeldFile(const OwnerHeldFile&) = delete;
    OwnerHeldFile& operator=(const OwnerHeldFile&) = delete;
    ~OwnerHeldFile();

    /// Opens and reads the file. Held only when the result is present with no error.
    OwnerFileRead Open(const std::wstring& path);
    /// Reads the held file again from its start. Returns the Win32 error, 0 when read.
    std::uint32_t Reread(std::string& bytes);
    /// Throws std::system_error when CloseHandle fails.
    void Close();

private:
    void* handle_ = nullptr;
};

// Reads a file shared for read, write and delete.
OwnerFileRead OwnerReadFile(const std::wstring& path);

// The last write time as a FILETIME count, as .NET's File.GetLastWriteTimeUtc reads it:
// GetFileAttributesExW, then FindFirstFileW when that fails for a file that is not missing (one
// pending deletion). 0 for a missing file or folder or a drive with no media. Throws
// std::system_error when both fail otherwise.
std::uint64_t OwnerLastWriteTime(const std::wstring& path);

// GetFullPathNameW of a fully qualified path. Throws std::invalid_argument naming the option
// for an empty or relative path.
std::wstring OwnerFullPath(const std::wstring& path, const char* option);

// Design 4.5 step 2: the path in the ANSI code page, converted with WC_NO_BEST_FIT_CHARS, and
// whether a character had no representation there.
LegacyInput OwnerLegacyInput(const std::wstring& path);

std::string OwnerUtf8(const std::wstring& text);
std::string OwnerFileName(const std::wstring& path);
// An ANSI path as UTF-8, for the log.
std::string OwnerAnsiForLog(const std::string& ansi);

CheckedWriteResult OwnerWrite(const std::wstring& path, const std::optional<std::string>& expected,
                              const std::string& candidate, const ConfigOwnerHook& hook, const char* prefix);

// Calls the hook at one of the owner's own steps.
void OwnerStep(const ConfigOwnerHook& hook, const char* label, const std::wstring& path);

// True when the result is a failure, or a conflict whose temporary could not be deleted.
bool OwnerWriteFailed(const CheckedWriteResult& result);
// The log line for a failed write, e.g. "Writing <path> failed at Commit: Windows error 5: ...".
std::string OwnerWriteLog(const std::wstring& path, const CheckedWriteResult& result);
// What the player is told about a failed write, in the words of design 4.7.
std::string OwnerWriteWhy(const std::wstring& path, const CheckedWriteResult& result);
// What the player is told about a read that failed with a Win32 error.
std::string OwnerReadWhy(const std::wstring& path, std::uint32_t error);
// What the player is told about a write that found the file changed, appeared or gone.
std::string OwnerConflict(CheckedWriteStatus status);
// "Windows error N: <the system's message>".
std::string OwnerErrorText(std::uint32_t error);

// Design 4.6: keeps the snapshot in `<path>.pre-canonical`, or in `<path>.pre-canonical.last`
// when the first copy holds other bytes, each written through the checked writer and read back.
// Returns the copy's path, or an empty path with `why` set for the player.
struct OwnerKept {
    std::wstring path;
    std::string why;
};
OwnerKept OwnerKeepOriginal(const std::wstring& path, const std::string& snapshot, const ConfigOwnerHook& hook,
                            std::vector<std::string>& log);

// Design 4.3 step 5: a line for every key line of the legacy file that the import does not read.
void OwnerLogNotCarried(const std::string& snapshot, const std::vector<LegacyKey>& keys, const std::string& input,
                        std::vector<std::string>& log);

// Declared only: the test suite defines it to build an owner with a ConfigOwnerHook.
struct ConfigOwnerTestAccess;

}  // namespace detail

/// The one reader and writer of a game's canonical config file. It converts a legacy file once
/// through the game's frozen import, creates a missing file from the table's defaults, saves the
/// rows the table marks Writable, and reloads. It writes only through WriteFileChecked, so every
/// write replaces the whole file or nothing. Windows only.
///
/// Build it before anything reads the file, and read the file only through it. One mutex
/// serializes Load, Reload, Save and FileChanged; the status sink runs after it is released.
/// Save is synchronous: call it from the HotkeyPoller thread or another thread that is not
/// drawing a frame, never from a per-frame path. The mutex does not coordinate separate
/// processes.
///
/// Load converts files, so it must not run under the loader lock: call it from the game's init
/// thread, never from DllMain.
///
/// Nothing here writes a row the table does not mark Writable, so the End toggle, which changes
/// only the session, never reaches EnableOnStartup unless the game marks that row Writable and
/// calls Save for it. A table with both RotationEnabled and PositionEnabled must mark both
/// Writable or neither, since a mode change writes the pair.
template <class Config>
class ConfigOwner {
public:
    /// Throws std::invalid_argument when the path is empty or not fully qualified, the table has
    /// no rows, the import names keys but has no run, the table has both RotationEnabled and
    /// PositionEnabled and marks only one of them Writable, or the table cannot render its
    /// defaults under the header.
    explicit ConfigOwner(ConfigOwnerOptions<Config> options) : ConfigOwner(std::move(options), nullptr) {}

    ConfigOwner(const ConfigOwner&) = delete;
    ConfigOwner& operator=(const ConfigOwner&) = delete;

    /// Reads the file, and converts, creates or refuses it first where it has to:
    /// - No file: the table's defaults are rendered and the file created, never over a file that
    ///   appears meanwhile (Created). If one appears, or the folder cannot be written, the session
    ///   runs on the defaults and nothing retries (Deferred).
    /// - A file that cannot be opened (held by a program denying read sharing, or denied by its
    ///   permissions): Deferred on the table's defaults, and the import does not run, since the
    ///   stamp that tells a legacy file from a canonical one is inside the file.
    /// - A [CameraUnlock] stamp, looked for in a UTF-16 file's text too: read as canonical
    ///   (Canonical), or Unreadable when saved as UTF-16 or holding a NUL. The import never runs.
    /// - No stamp, and the game has an import: converted (Migrated).
    /// - No stamp and no import: read as canonical with a line in the log saying the next save
    ///   stamps it (Canonical), or Unreadable.
    ///
    /// A conversion holds the legacy file open, readable and writable by others but not
    /// deletable, while it reads the bytes, runs the import and reads the bytes again; bytes that
    /// changed meanwhile defer it. The import is handed the path and its ANSI form. When the
    /// import reports the file Absent although the owner holds it, the conversion goes ahead only
    /// if the ANSI form lost a character: the published build, handed that form, never saw the
    /// file and ran on the defaults the import gave, so those are written, the file's content is
    /// kept in the copy, and the log says so. Otherwise it defers with both paths logged. Every
    /// conversion of a path whose ANSI form lost a character logs that, so a per-key import that
    /// read nothing there is named too.
    ///
    /// The conversion then renders the imported settings, reads the render back through the table
    /// and requires every row to equal the import's (floats bitwise), keeps the original bytes in
    /// `.pre-canonical` (or `.pre-canonical.last`) beside the file, reads that copy back, and
    /// replaces the file only if it still holds the bytes the import read. Any failure defers: the
    /// file is left as it was, the session runs on what the import gave, the player is told once
    /// through the status sink, and the next launch tries again. A process killed at any point
    /// leaves the legacy file whole, or the new file whole.
    ///
    /// Must not run under the loader lock. Ordinary I/O failures are reported through the result.
    /// An import that throws, or a table hook that throws, is a bug and the exception is not caught.
    ConfigLoadResult<Config> Load() {
        ConfigLoadResult<Config> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = LoadLocked();
        }
        const bool report = result.status == ConfigLoadStatus::Deferred ||
                            result.status == ConfigLoadStatus::LegacyRefused ||
                            result.status == ConfigLoadStatus::Unreadable;
        if (report && status_sink_) status_sink_(result.reason);
        return result;
    }

    /// Changes rows of the file. `change` is given the settings read from the file as it is now
    /// and sets the new values on it; the game has already applied them to its running state. A
    /// row that holds a new value afterwards must be marked Writable in the table. When
    /// RotationEnabled or PositionEnabled changes, both are written, so a tracking mode is always
    /// one edit.
    ///
    /// Saves only a file the canonical reader can read, whose ConfigFormat is not newer than this
    /// build's, and that is stamped or belongs to a game with no legacy import; an unstamped file,
    /// or a stamp with no ConfigFormat or one that is not a number, gets its [CameraUnlock]
    /// ConfigFormat line in the same write. The edited bytes are read back through the table
    /// before anything is written: only the changed rows may differ, and they must hold the new
    /// values. Rows already holding the values write nothing and report Saved. A missing file is
    /// not created here: Load creates it at the next launch. After a Deferred, LegacyRefused or
    /// Unreadable load nothing is saved that session, until a Reload applies a readable file.
    ///
    /// Never rolls back and never retries. NotSaved and Uncertain are handed to the status sink
    /// once. An editor writing the file between the last check and the replacement is
    /// overwritten: the replacement is not compare-and-swap.
    ///
    /// Throws std::invalid_argument for an empty `change`, or a Writable row holding a value its
    /// codec cannot write or text outside printable ASCII, which the editor refuses; and
    /// std::logic_error when Load has not run, or when a row that is not Writable changed, naming
    /// the row.
    ConfigSaveResult Save(const std::function<void(Config&)>& change) {
        if (!change) throw std::invalid_argument("Save needs a change");
        ConfigSaveResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            RequireLoaded("Save");
            result = SaveLocked(change);
        }
        if (result.status != ConfigSaveStatus::Saved && status_sink_) status_sink_(result.reason);
        return result;
    }

    /// Reads the file again, for a watcher that saw FileChanged or a reload the player asked
    /// for. Unchanged when the file holds exactly the bytes the owner last wrote, the
    /// conversion's and creation's included, and no Reload has applied other bytes since. A file
    /// with no stamp that the legacy import reads is read through it, held open as a conversion
    /// holds it, and never written (LegacyReadOnly); the next launch converts it. An import that
    /// refuses the file, cannot decode it or finds none, or a file that changes while the import
    /// reads it, is Unreadable. Unreadable leaves the game's settings as they are and is handed
    /// to the status sink. Never writes and never converts.
    ///
    /// Throws std::logic_error when Load has not run.
    ConfigReloadResult<Config> Reload() {
        ConfigReloadResult<Config> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            RequireLoaded("Reload");
            result = ReloadLocked();
        }
        if (result.status == ConfigReloadStatus::Unreadable && status_sink_) status_sink_(result.reason);
        return result;
    }

    /// True when the file's last write time differs from the one the owner recorded at its last
    /// Load, Reload or committed Save. The time is read with GetFileAttributesExW, or from the
    /// folder's listing with FindFirstFileW when that refuses a file that is there (one pending
    /// deletion), as .NET reads it. A missing file counts as write time 0, so a file that appears
    /// or goes away counts.
    ///
    /// Throws std::logic_error when Load has not run, and std::system_error when Windows can read
    /// the time neither way for a reason other than the file's absence.
    bool FileChanged() {
        std::lock_guard<std::mutex> lock(mutex_);
        RequireLoaded("FileChanged");
        return detail::OwnerLastWriteTime(path_) != recorded_write_time_;
    }

private:
    friend struct detail::ConfigOwnerTestAccess;

    ConfigOwner(ConfigOwnerOptions<Config> options, detail::ConfigOwnerHook hook)
        : path_(detail::OwnerFullPath(options.path, "path")),
          path_text_(detail::OwnerUtf8(path_)),
          name_(detail::OwnerFileName(path_)),
          table_(std::move(options.table)),
          import_(std::move(options.import)),
          header_(std::move(options.header)),
          status_sink_(std::move(options.status_sink)),
          hook_(std::move(hook)) {
        if (table_.rows_.empty()) throw std::invalid_argument("the options' table has no rows");
        if (!import_.run && !import_.keys.empty()) {
            throw std::invalid_argument("the options' import names keys but has no run");
        }
        const std::optional<std::size_t> rotation = RowOf(schema::Concept::RotationEnabled);
        const std::optional<std::size_t> position = RowOf(schema::Concept::PositionEnabled);
        if (rotation && position && table_.rows_[*rotation].writable != table_.rows_[*position].writable) {
            const std::size_t writable = table_.rows_[*rotation].writable ? *rotation : *position;
            const std::size_t other = writable == *rotation ? *position : *rotation;
            throw std::invalid_argument("the table marks " + RowName(writable) + " Writable but not " +
                                        RowName(other) + ", and a tracking mode change writes both");
        }
        default_bytes_ = RenderCanonical(table_, table_.defaults(), header_);
    }

    static constexpr const char* kChangedWhileRead = "the file was changed by another program while it was read";

    ConfigLoadResult<Config> LoadLocked() {
        loaded_ = true;
        saves_allowed_ = false;
        committed_.reset();
        std::vector<std::string> log;
        recorded_write_time_ = detail::OwnerLastWriteTime(path_);

        detail::OwnerStep(hook_, "Open", path_);
        detail::OwnerHeldFile held;
        const detail::OwnerFileRead opened = held.Open(path_);
        if (opened.error != 0) {
            log.push_back(path_text_ + ": could not be opened: " + detail::OwnerErrorText(opened.error));
            return LoadResult(ConfigLoadStatus::Deferred, table_.defaults(), {}, std::move(log),
                              name_ + " cannot be read: " + detail::OwnerReadWhy(path_, opened.error) +
                                  ". The mod runs on its default settings this session.");
        }
        if (!opened.present) return Create(std::move(log));

        const bool stamped = HasCanonicalStamp(opened.bytes);
        if (!stamped && import_.run) return Migrate(held, opened.bytes, std::move(log));
        held.Close();
        return ReadCanonical(opened.bytes, stamped, std::move(log));
    }

    ConfigLoadResult<Config> ReadCanonical(const std::string& bytes, bool stamped, std::vector<std::string> log) {
        const CanonicalIni doc = ParseCanonicalIni(bytes);
        if (!doc.IsReadable()) {
            const std::string why = Unreadable(doc);
            log.push_back(path_text_ + ": cannot be read: " + why);
            return LoadResult(ConfigLoadStatus::Unreadable, table_.defaults(), {}, std::move(log),
                              name_ + " cannot be read: " + why +
                                  ". The mod runs on its default settings and saves nothing until the file is fixed.");
        }
        Config config = table_.defaults();
        std::vector<CanonicalDiagnostic> diagnostics = Apply(doc, config, log);
        if (!stamped) {
            log.push_back(path_text_ +
                          ": has no [CameraUnlock] section. It is read as the canonical format, and the next save "
                          "adds the section.");
        }
        saves_allowed_ = true;
        return LoadResult(ConfigLoadStatus::Canonical, std::move(config), std::move(diagnostics), std::move(log), "");
    }

    ConfigLoadResult<Config> Create(std::vector<std::string> log) {
        const CheckedWriteResult written = detail::OwnerWrite(path_, std::nullopt, default_bytes_, hook_, "Create.");
        std::string why;
        if (written.Committed()) {
            committed_ = default_bytes_;
            recorded_write_time_ = detail::OwnerLastWriteTime(path_);
            saves_allowed_ = true;
            log.push_back(path_text_ + ": created with the default settings.");
            return LoadResult(ConfigLoadStatus::Created, table_.defaults(), {}, std::move(log), "");
        }
        if (detail::OwnerWriteFailed(written)) {
            why = detail::OwnerWriteWhy(path_, written);
            log.push_back(path_text_ + ": not created: " + detail::OwnerWriteLog(path_, written));
        } else {
            why = detail::OwnerConflict(written.status);
            log.push_back(path_text_ + ": not created: " + CheckedWriteStatusName(written.status));
        }
        return LoadResult(ConfigLoadStatus::Deferred, table_.defaults(), {}, std::move(log),
                          name_ + " was not created: " + why + ". The mod runs on its default settings this session.");
    }

    ConfigLoadResult<Config> Migrate(detail::OwnerHeldFile& held, const std::string& snapshot,
                                     std::vector<std::string> log) {
        Config imported = table_.defaults();
        const LegacyInput input = detail::OwnerLegacyInput(path_);
        detail::OwnerStep(hook_, "Import", path_);
        const ImportResult legacy = import_.run(input, imported);
        detail::OwnerStep(hook_, "Recheck", path_);
        std::string changed_why;
        std::string reread;
        const std::uint32_t reread_error = held.Reread(reread);
        if (reread_error != 0) {
            changed_why = detail::OwnerReadWhy(path_, reread_error);
            log.push_back(path_text_ + ": could not be read again after the import: " +
                          detail::OwnerErrorText(reread_error));
        } else if (reread != snapshot) {
            changed_why = kChangedWhileRead;
        }
        held.Close();
        if (!changed_why.empty()) return Defer(std::move(imported), std::move(log), changed_why);

        if (input.ansi_lossy) {
            log.push_back(path_text_ + ": has a character the ANSI code page cannot hold, so a reader given its ANSI path, " +
                          detail::OwnerAnsiForLog(input.ansi_path) + ", finds no file there.");
        }
        switch (legacy.status) {
            case ImportStatus::Refused:
                log.push_back(path_text_ + ": the old settings reader refused the file: " + legacy.reason);
                return LoadResult(ConfigLoadStatus::LegacyRefused, std::move(imported), {}, std::move(log),
                                  NotConverted(legacy.reason));
            case ImportStatus::Undecodable:
                log.push_back(path_text_ + ": the old settings reader could not decode the file: " + legacy.reason);
                return Defer(std::move(imported), std::move(log), legacy.reason);
            case ImportStatus::Absent:
                if (!input.ansi_lossy) {
                    log.push_back(path_text_ + ": the old settings reader found no file, while the owner holds it open (" +
                                  std::to_string(snapshot.size()) + " bytes); the ANSI path it was given is " +
                                  detail::OwnerAnsiForLog(input.ansi_path));
                    LogDropped(legacy, log);
                    return Defer(std::move(imported), std::move(log), "the old settings reader could not find the file");
                }
                log.push_back(path_text_ +
                              ": the old settings reader found no file, as the old build found none there and ran on "
                              "its defaults. Those defaults are written, and the file's content is kept in the copy.");
                break;
            case ImportStatus::Imported:
                break;
        }
        LogDropped(legacy, log);

        std::string rendered;
        try {
            rendered = RenderCanonical(table_, imported, header_);
        } catch (const std::invalid_argument& e) {
            const std::optional<std::size_t> row = FirstUnwritable(imported);
            if (!row) throw;
            log.push_back(path_text_ + ": " + RowName(*row) + " cannot be written in the new format: " + e.what());
            std::string why = Unconvertible(*row, imported);
            return Defer(std::move(imported), std::move(log), why);
        }

        Config reread_config = table_.defaults();
        std::vector<std::string> read_back;
        std::vector<CanonicalDiagnostic> diagnostics = Apply(ParseCanonicalIni(rendered), reread_config, read_back);
        const std::optional<std::size_t> different = FirstDifference(imported, reread_config);
        if (different) {
            log.push_back(path_text_ + ": " + RowName(*different) + " reads back from the new format as " +
                          RowValueText(*different, reread_config) + ", not " + RowValueText(*different, imported));
            std::string why = Unconvertible(*different, imported);
            return Defer(std::move(imported), std::move(log), why);
        }

        const detail::OwnerKept kept = detail::OwnerKeepOriginal(path_, snapshot, hook_, log);
        if (kept.path.empty()) return Defer(std::move(imported), std::move(log), kept.why);

        const CheckedWriteResult written = detail::OwnerWrite(path_, snapshot, rendered, hook_, "Commit.");
        if (detail::OwnerWriteFailed(written)) {
            log.push_back(detail::OwnerWriteLog(path_, written));
            return Defer(std::move(imported), std::move(log), detail::OwnerWriteWhy(path_, written));
        }
        if (!written.Committed()) {
            log.push_back(path_text_ + ": not replaced: " + CheckedWriteStatusName(written.status));
            return Defer(std::move(imported), std::move(log), detail::OwnerConflict(written.status));
        }

        detail::OwnerStep(hook_, "Remember", path_);
        committed_ = rendered;
        recorded_write_time_ = detail::OwnerLastWriteTime(path_);
        saves_allowed_ = true;
        log.push_back(path_text_ + ": converted to the canonical format. The original is kept in " +
                      detail::OwnerUtf8(kept.path) + ".");
        detail::OwnerLogNotCarried(snapshot, import_.keys, path_text_, log);
        log.insert(log.end(), read_back.begin(), read_back.end());
        return LoadResult(ConfigLoadStatus::Migrated, std::move(reread_config), std::move(diagnostics), std::move(log),
                          "");
    }

    ConfigLoadResult<Config> Defer(Config config, std::vector<std::string> log, const std::string& why) {
        return LoadResult(ConfigLoadStatus::Deferred, std::move(config), {}, std::move(log), NotConverted(why));
    }

    std::string NotConverted(const std::string& why) const {
        return name_ + " was not converted to the new settings format: " + why +
               ". The mod tries again at the next launch and saves nothing this session.";
    }

    std::string Unconvertible(std::size_t row, const Config& config) const {
        return RowName(row) + "=" + RowValueText(row, config) + " cannot be converted";
    }

    ConfigSaveResult SaveLocked(const std::function<void(Config&)>& change) {
        std::vector<std::string> log;
        if (!saves_allowed_) {
            log.push_back(path_text_ + ": not saved: the file was not loaded this session");
            return NotSaved("the settings file could not be used this session", 0, std::move(log));
        }

        const detail::OwnerFileRead read = detail::OwnerReadFile(path_);
        if (read.error != 0) {
            log.push_back(path_text_ + ": not saved: could not be read: " + detail::OwnerErrorText(read.error));
            return NotSaved(detail::OwnerReadWhy(path_, read.error), read.error, std::move(log));
        }
        if (!read.present) {
            log.push_back(path_text_ + ": not saved: the file is missing");
            return NotSaved("the settings file is missing; it is created again at the next launch", 0, std::move(log));
        }
        const std::string& snapshot = read.bytes;

        const CanonicalIni doc = ParseCanonicalIni(snapshot);
        if (!doc.IsReadable()) {
            const std::string why = Unreadable(doc);
            log.push_back(path_text_ + ": not saved: " + why);
            return NotSaved(name_ + " cannot be read: " + why, 0, std::move(log));
        }
        if (doc.format_version > kConfigFormat) {
            log.push_back(path_text_ + ": not saved: ConfigFormat " + std::to_string(doc.format_version) +
                          " is newer than this build's " + std::to_string(kConfigFormat));
            return NotSaved(name_ + " was written by a newer version of the mod", 0, std::move(log));
        }
        const bool stamped = HasCanonicalStamp(snapshot);
        if (!stamped && import_.run) {
            log.push_back(path_text_ + ": not saved: it has no [CameraUnlock] section, so it is an old file");
            return NotSaved(name_ + " is in the old settings format; it is converted at the next launch", 0,
                            std::move(log));
        }

        Config baseline = table_.defaults();
        ApplyCanonical(doc, table_, baseline);
        Config changed = baseline;
        change(changed);

        const std::size_t count = table_.rows_.size();
        std::vector<bool> edited(count, false);
        bool any = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (table_.ops_[i]->Equal(baseline, changed)) continue;
            if (!table_.rows_[i].writable) {
                throw std::logic_error(RowName(i) +
                                       " changed, but the table does not mark it Writable, so Save may not write it");
            }
            edited[i] = true;
            any = true;
        }
        if (!any) return ConfigSaveResult{};

        const std::optional<std::size_t> rotation = RowOf(schema::Concept::RotationEnabled);
        const std::optional<std::size_t> position = RowOf(schema::Concept::PositionEnabled);
        if (rotation && position && (edited[*rotation] || edited[*position])) {
            edited[*rotation] = true;
            edited[*position] = true;
        }

        std::vector<IniEdit> edits;
        for (std::size_t i = 0; i < count; ++i) {
            if (!edited[i]) continue;
            IniEdit edit;
            edit.section = table_.rows_[i].section;
            edit.key = table_.rows_[i].key;
            edit.value = table_.ops_[i]->Render(changed);
            edit.insert_if_absent = true;
            edits.push_back(std::move(edit));
        }
        if (!stamped || FormatUnread(doc)) {
            IniEdit stamp;
            stamp.section = "CameraUnlock";
            stamp.key = "ConfigFormat";
            stamp.value = std::to_string(kConfigFormat);
            stamp.insert_if_absent = true;
            edits.push_back(std::move(stamp));
        }
        const IniEditResult edit = EditIni(snapshot, edits);
        if (!edit.Succeeded()) {
            throw std::logic_error(std::string("the editor refused a file the canonical reader reads: ") +
                                   IniEditRefusalName(edit.refusal));
        }
        const std::string& candidate = edit.bytes;

        Config written_config = table_.defaults();
        ApplyCanonical(ParseCanonicalIni(candidate), table_, written_config);
        for (std::size_t i = 0; i < count; ++i) {
            const Config& expected = edited[i] ? changed : baseline;
            if (table_.ops_[i]->Equal(written_config, expected)) continue;
            log.push_back(path_text_ + ": not saved: " + RowName(i) + " would read back as " +
                          RowValueText(i, written_config) + ", not " + RowValueText(i, expected));
            return NotSaved(RowName(i) + "=" + RowValueText(i, changed) + " does not read back from " + name_, 0,
                            std::move(log));
        }

        const CheckedWriteResult written = detail::OwnerWrite(path_, snapshot, candidate, hook_, "Save.");
        if (detail::OwnerWriteFailed(written)) {
            log.push_back(detail::OwnerWriteLog(path_, written));
            ConfigSaveResult result;
            result.error = written.error != 0 ? written.error : written.cleanup_error;
            result.log = std::move(log);
            if (written.outcome_uncertain) {
                result.status = ConfigSaveStatus::Uncertain;
                result.reason = "Settings may not be saved: " + detail::OwnerWriteWhy(path_, written) + ".";
                result.temporary_path = written.temporary_path;
                return result;
            }
            result.status = ConfigSaveStatus::NotSaved;
            result.reason = "Settings not saved: " + detail::OwnerWriteWhy(path_, written) + ".";
            return result;
        }
        if (!written.Committed()) {
            log.push_back(path_text_ + ": not saved: " + CheckedWriteStatusName(written.status));
            return NotSaved(detail::OwnerConflict(written.status), 0, std::move(log));
        }
        committed_ = candidate;
        recorded_write_time_ = detail::OwnerLastWriteTime(path_);
        return ConfigSaveResult{};
    }

    static ConfigSaveResult NotSaved(const std::string& why, std::uint32_t error, std::vector<std::string> log) {
        ConfigSaveResult result;
        result.status = ConfigSaveStatus::NotSaved;
        result.reason = "Settings not saved: " + why + ".";
        result.error = error;
        result.log = std::move(log);
        return result;
    }

    ConfigReloadResult<Config> ReloadLocked() {
        std::vector<std::string> log;
        const std::uint64_t write_time = detail::OwnerLastWriteTime(path_);
        const detail::OwnerFileRead read = detail::OwnerReadFile(path_);
        if (read.error != 0) {
            log.push_back(path_text_ + ": not reloaded: " + detail::OwnerErrorText(read.error));
            return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                            name_ + " cannot be read: " + detail::OwnerReadWhy(path_, read.error) +
                                ". The current settings stay.");
        }
        recorded_write_time_ = write_time;
        if (!read.present) {
            log.push_back(path_text_ + ": not reloaded: the file is missing");
            return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                            name_ + " is missing, so the current settings stay. It is created again at the next launch.");
        }
        const std::string& bytes = read.bytes;

        if (committed_ && bytes == *committed_) {
            return Reloaded(ConfigReloadStatus::Unchanged, std::nullopt, {}, std::move(log), "");
        }
        if (!HasCanonicalStamp(bytes) && import_.run) return ReloadLegacy(bytes, std::move(log));

        const CanonicalIni doc = ParseCanonicalIni(bytes);
        if (!doc.IsReadable()) {
            const std::string why = Unreadable(doc);
            log.push_back(path_text_ + ": not reloaded: " + why);
            return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                            name_ + " cannot be read: " + why + ". The current settings stay.");
        }
        Config config = table_.defaults();
        std::vector<CanonicalDiagnostic> diagnostics = Apply(doc, config, log);
        committed_.reset();
        saves_allowed_ = true;
        return Reloaded(ConfigReloadStatus::Applied, std::move(config), std::move(diagnostics), std::move(log), "");
    }

    // The import reads the file while it is held as a conversion holds it, so the settings it
    // gives come from the bytes checked for a stamp.
    ConfigReloadResult<Config> ReloadLegacy(const std::string& bytes, std::vector<std::string> log) {
        detail::OwnerHeldFile held;
        const detail::OwnerFileRead opened = held.Open(path_);
        if (opened.error != 0) {
            return NotReloaded(detail::OwnerReadWhy(path_, opened.error), detail::OwnerErrorText(opened.error),
                               std::move(log));
        }
        if (!opened.present) {
            return NotReloaded("the file was deleted while it was read", "the file is missing", std::move(log));
        }
        if (opened.bytes != bytes) return NotReloaded(kChangedWhileRead, kChangedWhileRead, std::move(log));

        Config imported = table_.defaults();
        const ImportResult legacy = import_.run(detail::OwnerLegacyInput(path_), imported);
        std::string changed_why;
        std::string reread;
        const std::uint32_t reread_error = held.Reread(reread);
        if (reread_error != 0) {
            changed_why = detail::OwnerReadWhy(path_, reread_error);
            log.push_back(path_text_ + ": could not be read again after the import: " +
                          detail::OwnerErrorText(reread_error));
        } else if (reread != bytes) {
            changed_why = kChangedWhileRead;
        }
        held.Close();
        if (!changed_why.empty()) return NotReloaded(changed_why, changed_why, std::move(log));

        switch (legacy.status) {
            case ImportStatus::Refused:
            case ImportStatus::Undecodable:
                return NotReloaded(legacy.reason, "the old settings reader refused the file: " + legacy.reason,
                                   std::move(log));
            case ImportStatus::Absent:
                return NotReloaded("the old settings reader could not find the file",
                                   "the old settings reader found no file, while the owner holds it open (" +
                                       std::to_string(bytes.size()) + " bytes)",
                                   std::move(log));
            case ImportStatus::Imported:
                break;
        }
        LogDropped(legacy, log);
        log.push_back(path_text_ +
                      ": has no [CameraUnlock] section, so it is an old file. It is read, not saved, and converted at "
                      "the next launch.");
        committed_.reset();
        return Reloaded(ConfigReloadStatus::LegacyReadOnly, std::move(imported), {}, std::move(log),
                        name_ +
                            " is in the old settings format. It is read, changes are not saved, and it is converted at "
                            "the next launch.");
    }

    ConfigReloadResult<Config> NotReloaded(const std::string& why, const std::string& detail_text,
                                           std::vector<std::string> log) {
        log.push_back(path_text_ + ": not reloaded: " + detail_text);
        return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                        name_ + " cannot be read: " + why + ". The current settings stay.");
    }

    std::vector<CanonicalDiagnostic> Apply(const CanonicalIni& doc, Config& config, std::vector<std::string>& log) {
        std::vector<CanonicalDiagnostic> diagnostics = doc.diagnostics;
        ApplyReport report = ApplyCanonical(doc, table_, config);
        diagnostics.insert(diagnostics.end(), report.diagnostics.begin(), report.diagnostics.end());
        for (const CanonicalDiagnostic& diagnostic : diagnostics) {
            log.push_back(path_text_ + ": " + DescribeCanonicalDiagnostic(diagnostic));
        }
        return diagnostics;
    }

    void LogDropped(const ImportResult& import, std::vector<std::string>& log) const {
        for (const DroppedValue& dropped : import.dropped) log.push_back(path_text_ + ": " + DescribeDroppedValue(dropped));
    }

    std::optional<std::size_t> FirstUnwritable(const Config& config) const {
        for (std::size_t i = 0; i < table_.rows_.size(); ++i) {
            try {
                table_.ops_[i]->Render(config);
            } catch (const std::invalid_argument&) {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> FirstDifference(const Config& a, const Config& b) const {
        for (std::size_t i = 0; i < table_.rows_.size(); ++i) {
            if (!table_.ops_[i]->Equal(a, b)) return i;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> RowOf(schema::Concept id) const {
        for (std::size_t i = 0; i < table_.rows_.size(); ++i) {
            if (table_.rows_[i].concept_id == id) return i;
        }
        return std::nullopt;
    }

    std::string RowName(std::size_t row) const { return detail::RowName(table_.rows_[row]); }

    // The row's value as a message shows it: its canonical text, or the value itself when the
    // codec cannot write it.
    std::string RowValueText(std::size_t row, const Config& config) const {
        try {
            return table_.ops_[row]->Render(config);
        } catch (const std::invalid_argument&) {
            return table_.ops_[row]->Display(config);
        }
    }

    void RequireLoaded(const char* operation) const {
        if (!loaded_) throw std::logic_error(std::string(operation) + " needs Load to have run first");
    }

    static std::string Unreadable(const CanonicalIni& doc) {
        if (doc.status == CanonicalReadStatus::Utf16) return "it is saved as UTF-16; save it as ANSI or UTF-8";
        return "line " + std::to_string(doc.unreadable_line) + " holds a NUL byte";
    }

    // Design 1.8: a stamp with no ConfigFormat, or one that is not a number, is read as the
    // current format, and the next save writes the number.
    static bool FormatUnread(const CanonicalIni& doc) {
        for (const CanonicalDiagnostic& diagnostic : doc.diagnostics) {
            if (diagnostic.kind == CanonicalDiagnosticKind::ConfigFormatMissing ||
                diagnostic.kind == CanonicalDiagnosticKind::ConfigFormatInvalid) {
                return true;
            }
        }
        return false;
    }

    static ConfigLoadResult<Config> LoadResult(ConfigLoadStatus status, Config config,
                                               std::vector<CanonicalDiagnostic> diagnostics,
                                               std::vector<std::string> log, std::string reason) {
        return ConfigLoadResult<Config>{status, std::move(config), std::move(diagnostics), std::move(log),
                                        std::move(reason)};
    }

    static ConfigReloadResult<Config> Reloaded(ConfigReloadStatus status, std::optional<Config> config,
                                               std::vector<CanonicalDiagnostic> diagnostics,
                                               std::vector<std::string> log, std::string reason) {
        return ConfigReloadResult<Config>{status, std::move(config), std::move(diagnostics), std::move(log),
                                          std::move(reason)};
    }

    const std::wstring path_;
    const std::string path_text_;
    const std::string name_;
    const ConfigTable<Config> table_;
    const LegacyImport<Config> import_;
    const RenderHeader header_;
    const std::function<void(const std::string&)> status_sink_;
    const detail::ConfigOwnerHook hook_;
    std::string default_bytes_;

    std::mutex mutex_;
    bool loaded_ = false;
    bool saves_allowed_ = false;
    std::optional<std::string> committed_;
    std::uint64_t recorded_write_time_ = 0;
};

#endif  // _WIN32

}  // namespace cameraunlock::config
