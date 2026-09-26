#pragma once

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/checked_file_writer.h>
#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/defaults_ini.h>
#include <cameraunlock/config/defaults_location.h>
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
    /// The file at `path` was read as a canonical file, stamped or not; the first save that
    /// changes a row stamps an unstamped one. The legacy file is not read.
    Canonical = 0,
    /// The legacy file was imported into a new file at `path` this launch. The legacy file is
    /// left as it was.
    Migrated = 1,
    /// There was no file at `path` and no legacy file, and a file holding the defaults was
    /// created at `path`.
    Created = 2,
    /// The file at `path` could not be read or created, or the legacy file could not be read or
    /// imported, this launch. The legacy file is left as it was and the owner creates no file at
    /// `path`; a file at `path` that could not be read is left as it was too. The session runs on
    /// the settings the result holds and nothing is saved this session. The next launch loads
    /// again: it reads a file another program created at `path` meanwhile, and otherwise imports
    /// or creates the file again.
    Deferred = 3,
    /// The legacy import refused the legacy file, as the game's last pre-canonical build did.
    /// The game does what that build did on the refusal. The legacy file is left as it was,
    /// `path` is not created, and nothing is saved this session.
    LegacyRefused = 4,
    /// The file at `path` is one the canonical reader cannot read (saved as UTF-16, or holding a
    /// NUL byte). The session runs on the defaults and nothing is saved until the file is fixed.
    /// The legacy file is not read.
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
/// CameraUnlock.Core.Config's ConfigReloadStatus, where 2 is not used.
enum class ConfigReloadStatus {
    /// The file holds the bytes the owner last wrote, so there is nothing to apply.
    Unchanged = 0,
    /// The file was read and ConfigReloadResult::config holds its settings.
    Applied = 1,
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
    /// The config file, as a fully qualified path (`C:\...` or `\\server\...`):
    /// `CameraUnlock.ini`, in the folder that holds the game's legacy file where it has one.
    /// Required.
    std::wstring path;
    /// The game's table. Required: a table with no rows is refused.
    ConfigTable<Config> table;
    /// The game's frozen legacy import. An empty `run` means the game never published a build
    /// before the canonical format. A `run` needs `legacy_path`, the one file it reads. The
    /// import is handed that path in the ANSI code page too, and whether that form lost a
    /// character (LegacyInput).
    LegacyImport<Config> import;
    /// The game's legacy file, as a fully qualified path, normally in the same folder as `path`:
    /// `HeadTracking.ini` beside `CameraUnlock.ini`. Required with an import `run`, refused
    /// without one, and refused when it names the file `path` names (compared without case).
    /// Only while no file exists at `path` does the import read this file, and the owner then
    /// creates `path` from what it gives. Once `path` exists this file is not read again. It is
    /// never written, renamed, deleted or copied.
    std::wstring legacy_path;
    /// What the renderer writes above the settings. Required.
    RenderHeader header;
    /// Shows the player a one-line message, e.g. through the game's overlay, when settings are
    /// not imported, cannot be read or are not saved. Optional. It runs after the owner has
    /// released its lock.
    std::function<void(const std::string&)> status_sink;
    /// Where Defaults.ini is: DefaultsFile::PerUser() in a mod, and DefaultsFile::At(path) with a
    /// scratch path in every test. A helper that builds the options for both takes it as a
    /// parameter, so a test never reaches the player's own file. Required.
    DefaultsFile defaults;
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
    /// Every line for the game's log, UTF-8, each naming the file. It can hold the line saying
    /// the settings are read from `path` while a legacy file is also present, the line saying an
    /// unstamped file gets its section at the next save, the diagnostics' sentences, the line
    /// saying `path` was created and from what, the import's lines (values it dropped, lines of
    /// the legacy file the new file does not carry) and the error behind a Deferred,
    /// LegacyRefused or Unreadable load. It starts with where Defaults.ini is and what happened
    /// to it, and ends with which rows took their value from Defaults.ini, which the file sets
    /// itself, which took the built-in value, and each value Defaults.ini holds that this game
    /// would take and cannot use. Returned rather than logged, so a game can load before its
    /// logger is up.
    std::vector<std::string> log;
    /// For Deferred, LegacyRefused and Unreadable, the message for the player, which the owner
    /// also hands its status sink once; empty otherwise. A message about Defaults.ini goes to the
    /// status sink after it and is not here.
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
    /// The lines for the game's log, UTF-8, naming the file and the operation. For Saved, a line
    /// for each row that held `default` or was missing and is now written as a value, so no longer
    /// follows Defaults.ini; otherwise empty for Saved.
    std::vector<std::string> log;
};

/// What ConfigOwner::Reload returns. Nothing is logged by the owner.
template <class Config>
struct ConfigReloadResult {
    ConfigReloadStatus status = ConfigReloadStatus::Unchanged;
    /// For Applied, the settings read; empty for Unchanged and Unreadable, where the game keeps
    /// the settings it has.
    std::optional<Config> config;
    /// What the canonical reader and the table found, for Applied; empty otherwise.
    std::vector<CanonicalDiagnostic> diagnostics;
    /// Every line for the game's log, UTF-8, each naming the file, in order.
    std::vector<std::string> log;
    /// For Unreadable, the message for the player, which the owner also hands its status sink;
    /// empty otherwise.
    std::string reason;
};

#ifdef _WIN32

namespace detail {

// Not API: ConfigOwner's Windows half, in config_owner.cpp.

// The test seam. Run before each step with a label and the path the step acts on: `Open` (the
// config file, then the legacy file), `Import`, `Recheck` and `Remember` for the import's own
// steps, and `Defaults.`, `Commit.`, `Create.` or `Save.` followed by a CheckedWriteStepName for
// the writer's. At a writer step a nonzero return fails that step with that Win32 error, as the
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

// Design 4.5 step 1: the legacy file held open for reading, sharing read and write but not
// delete, so no program can newly open it denying read sharing, rename it or delete it while the
// import reads it.
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

// True when two full paths are equal ignoring case, as CompareStringOrdinal compares them.
bool OwnerSamePath(const std::wstring& a, const std::wstring& b);

// True when GetFileAttributesW finds a file, not a folder, at the path. Nothing is opened.
bool OwnerFileExists(const std::wstring& path);

// True when GetFileAttributesW finds a file or a folder at the path, so a folder named
// Defaults.ini is found and then fails to read with its reason. Nothing is opened.
bool OwnerPathExists(const std::wstring& path);

// OwnerLastWriteTime, or the largest count when the time cannot be read, so a Defaults.ini whose
// time stays unreadable never counts as changed and never throws from FileChanged.
std::uint64_t DefaultsLastWriteTime(const std::wstring& path);

// What the player is told when the CameraUnlock folder could not be created.
std::string DefaultsFolderWhy(std::uint32_t error);

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
std::string OwnerReadWhy(std::uint32_t error);
// What the player is told about a write that found the file changed, appeared or gone.
std::string OwnerConflict(CheckedWriteStatus status);
// "Windows error N: <the system's message>".
std::string OwnerErrorText(std::uint32_t error);

// Design 4.3 step 5: a line for every key line of the legacy file that the import does not read.
void OwnerLogNotCarried(const std::string& snapshot, const std::vector<LegacyKey>& keys, const std::string& input,
                        std::vector<std::string>& log);

// Declared only: the test suite defines it to build an owner with a ConfigOwnerHook.
struct ConfigOwnerTestAccess;

}  // namespace detail

/// The one reader and writer of a game's canonical config file. While that file is absent it
/// imports the game's legacy file, a separate file it never writes, through the game's frozen
/// import into a new config file, or creates the config file from the table's fresh render. It
/// saves the rows the table marks Writable, and reloads. It writes only the config file and a
/// Defaults.ini that is absent, and only through WriteFileChecked, so every write replaces the
/// whole file or nothing. Windows only, Wine and Proton included.
///
/// Every global concept row the table does not mark PerGame takes its default from Defaults.ini
/// (`options.defaults`): `default`, a missing key and an invalid value on such a row read
/// Defaults.ini's value, or the row's own default where Defaults.ini gives none. Load finds the
/// file, creates it with the built-in values where none exists and it may, and reads it once;
/// Reload reads it again. No failure to find, create or read it stops the mod: the rows then use
/// the built-in values, with one line in the log.
///
/// Build it before anything reads the file, and read the file only through it. One mutex
/// serializes Load, Reload, Save and FileChanged; the status sink runs after it is released.
/// Save is synchronous: call it from the HotkeyPoller thread or another thread that is not
/// drawing a frame, never from a per-frame path. The mutex does not coordinate separate
/// processes.
///
/// Load reads and creates files, so it must not run under the loader lock: call it from the
/// game's init thread, never from DllMain.
///
/// Nothing here writes a row the table does not mark Writable, so the End toggle, which changes
/// only the session, never reaches EnableOnStartup unless the game marks that row Writable and
/// calls Save for it. A table with both RotationEnabled and PositionEnabled must mark both
/// Writable or neither, since a mode change writes the pair.
template <class Config>
class ConfigOwner {
public:
    /// Throws std::invalid_argument when the path is empty or not fully qualified, the table has
    /// no rows, the import names keys but has no run, the import has a run and legacy_path is
    /// empty, legacy_path is set and the import has no run, legacy_path is not fully qualified or
    /// names the file `path` names (compared without case), the table has both RotationEnabled
    /// and PositionEnabled and marks only one of them Writable, `defaults` names no file, or the
    /// table cannot render its fresh file under the header (RenderCanonicalFresh: a global concept
    /// row not marked PerGame whose default is not the schema's, RotationEnabled without
    /// PositionEnabled, or a header the renderer refuses).
    explicit ConfigOwner(ConfigOwnerOptions<Config> options) : ConfigOwner(std::move(options), nullptr) {}

    ConfigOwner(const ConfigOwner&) = delete;
    ConfigOwner& operator=(const ConfigOwner&) = delete;

    /// Reads Defaults.ini, then the config file, first importing the legacy file or creating the
    /// config file where there is none:
    /// - Defaults.ini first: where no file exists it is created with the built-in values, outside
    ///   a packaged app, never over a file that appears meanwhile, whatever becomes of the config
    ///   file. It is read once, and the rows that follow it take its values for the session. A
    ///   Defaults.ini that cannot be found, created or read gives the built-in values, one line in
    ///   the log and, when it exists and cannot be read, one message.
    /// - A file at `path`: read as canonical (Canonical), stamped or not, or Unreadable when saved
    ///   as UTF-16 or holding a NUL. An unstamped file gets a line in the log saying the next save
    ///   adds the section; the first save that changes a row stamps it. The import never runs and
    ///   the legacy file is never opened; when one exists (GetFileAttributesW), a line in the log
    ///   says the settings are read from `path` and the legacy file is not read. A file at `path`
    ///   that cannot be opened (held by a program denying read sharing, pending deletion, or
    ///   denied by its permissions) is Deferred on the table's defaults, and nothing is imported.
    /// - No file at `path` and a file at legacy_path: the legacy file is imported into a new file
    ///   at `path` (Migrated). A row that follows Defaults.ini is written `default` where the
    ///   imported value equals what `default` gives it at this Load, and the tracking mode pair
    ///   only when both rows do.
    /// - Neither: the table's fresh render is written, never over a file that appears meanwhile
    ///   (Created). If one appears, or the folder cannot be written, the session runs on the
    ///   defaults and nothing retries (Deferred).
    ///
    /// An import holds the legacy file open, readable and writable by others but not deletable,
    /// while it reads the bytes, runs the import and reads the bytes again; bytes that changed
    /// meanwhile defer it. The import is handed the legacy path and its ANSI form. When the import
    /// reports the file Absent although the owner holds it, the import goes ahead only if the ANSI
    /// form lost a character: the published build, handed that form, never saw the file and ran
    /// on the defaults the import gave, so those are written to `path`, the legacy file is left
    /// as it was, and the log says so. Otherwise it defers with both paths logged. Every import of
    /// a path whose ANSI form lost a character logs that, so a per-key import that read nothing
    /// there is named too.
    ///
    /// The import then renders the imported settings, reads the render back through the table and
    /// requires every row to equal the import's (floats bitwise), and creates the file at `path`
    /// only if no file has appeared there. Any failure defers: the owner creates no file at
    /// `path`, the session runs on what the import gave, nothing is saved, and the player is told
    /// once through the status sink. The next launch imports again, unless another program created
    /// a file at `path` meanwhile: that file is read at the next launch and the import does not
    /// run again, which the player message says. A legacy file that cannot be opened defers the
    /// same way, on the defaults. The legacy file is never written, renamed, deleted or copied,
    /// whatever happens. A process killed at any point leaves `path` and Defaults.ini each absent
    /// or whole.
    ///
    /// The status sink gets the config file's message, then at most one about Defaults.ini: that
    /// it cannot be read, else that a value this game would take from it is refused, else that two
    /// Defaults.ini files exist and one is ignored. Must not run under the loader lock. Ordinary
    /// I/O failures are reported through the result. An import that throws, or a table hook that
    /// throws, is a bug and the exception is not caught.
    ConfigLoadResult<Config> Load() {
        ConfigLoadResult<Config> result;
        std::string defaults_message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = LoadLocked(defaults_message);
        }
        const bool report = result.status == ConfigLoadStatus::Deferred ||
                            result.status == ConfigLoadStatus::LegacyRefused ||
                            result.status == ConfigLoadStatus::Unreadable;
        if (status_sink_) {
            if (report) status_sink_(result.reason);
            if (!defaults_message.empty()) status_sink_(defaults_message);
        }
        return result;
    }

    /// Changes rows of the file. `change` is given the settings read from the file as it is now
    /// and sets the new values on it; the game has already applied them to its running state. A
    /// row that holds a new value afterwards must be marked Writable in the table. When
    /// RotationEnabled or PositionEnabled changes, both are written, so a tracking mode is always
    /// one edit. A row that held `default` or was missing is written as its value, so from then on
    /// this game keeps it, and the log says so.
    ///
    /// Saves only a file the canonical reader can read and whose ConfigFormat is not newer than
    /// this build's; an unstamped file, or a stamp with no ConfigFormat or one that is not a
    /// number, gets its [CameraUnlock] ConfigFormat line in the same write. The file is read over
    /// the values Defaults.ini gave this session. The edited bytes are read back through the table
    /// before anything is written: only the changed rows may differ, they must hold the new
    /// values, and every other row must take its value from where it did. Rows already holding the
    /// values write nothing and report Saved. A missing file is not created here: Load creates it
    /// at the next launch, importing the legacy file if there is one. After a Deferred,
    /// LegacyRefused or Unreadable load nothing is saved that session, until a Reload applies a
    /// readable file. Neither the legacy file nor Defaults.ini is ever written.
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

    /// Reads Defaults.ini and the file again, for a watcher that saw FileChanged or a reload the
    /// player asked for. Defaults.ini is read where Load found it: new readable bytes replace the
    /// values it gives; a file that went missing or cannot be read keeps them, with one line in the
    /// log and one message. Unchanged when the file holds exactly the bytes the owner last wrote,
    /// the import's and creation's included, no Reload has applied other bytes since, and
    /// Defaults.ini gave nothing an Applied reload has not yet read over. Any other file is read as canonical, stamped or not, over
    /// Defaults.ini's current values (Applied). A missing file, or one the canonical reader cannot
    /// read, is Unreadable, which leaves the game's settings as they are and is handed to the
    /// status sink. Never writes, never imports and never opens the legacy file.
    ///
    /// Throws std::logic_error when Load has not run.
    ConfigReloadResult<Config> Reload() {
        ConfigReloadResult<Config> result;
        std::string defaults_message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            RequireLoaded("Reload");
            result = ReloadLocked(defaults_message);
        }
        if (status_sink_) {
            if (result.status == ConfigReloadStatus::Unreadable) status_sink_(result.reason);
            if (!defaults_message.empty()) status_sink_(defaults_message);
        }
        return result;
    }

    /// True when the last write time of the file, or of Defaults.ini where Load found it, differs
    /// from the one the owner recorded at its last Load, Reload or committed Save. The time is read
    /// with GetFileAttributesExW, or from the folder's listing with FindFirstFileW when that
    /// refuses a file that is there (one pending deletion), as .NET reads it. A missing file counts
    /// as write time 0, so a file that appears or goes away counts.
    ///
    /// Throws std::logic_error when Load has not run, and std::system_error when Windows can read
    /// the config file's time neither way for a reason other than the file's absence.
    bool FileChanged() {
        std::lock_guard<std::mutex> lock(mutex_);
        RequireLoaded("FileChanged");
        return detail::OwnerLastWriteTime(path_) != recorded_write_time_ || DefaultsWriteTime() != defaults_write_time_;
    }

private:
    friend struct detail::ConfigOwnerTestAccess;

    ConfigOwner(ConfigOwnerOptions<Config> options, detail::ConfigOwnerHook hook)
        : path_(detail::OwnerFullPath(options.path, "path")),
          path_text_(detail::OwnerUtf8(path_)),
          name_(detail::OwnerFileName(path_)),
          legacy_path_(LegacyPathOf(options, path_)),
          legacy_text_(detail::OwnerUtf8(legacy_path_)),
          legacy_name_(detail::OwnerFileName(legacy_path_)),
          table_(std::move(options.table)),
          import_(std::move(options.import)),
          header_(std::move(options.header)),
          status_sink_(std::move(options.status_sink)),
          hook_(std::move(hook)),
          defaults_(std::move(options.defaults)),
          snapshot_(detail::ReadDefaultsIni("")),
          effective_(table_.defaults()) {
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
        if (!detail::DefaultsFileIsSet(defaults_)) {
            throw std::invalid_argument(
                "the options name no defaults: a mod sets DefaultsFile::PerUser(), and a test DefaultsFile::At(path) "
                "with a scratch path");
        }
        fresh_bytes_ = RenderCanonicalFresh(table_, header_);
    }

    static constexpr const char* kChangedWhileRead = "the file was changed by another program while it was read";
    static constexpr const char* kKeptUntilRestart =
        " Settings that use it keep the values they had until the game restarts.";

    // The legacy file's full path, empty for a game with no import. Runs in the member
    // initializers, before `options.import` is moved.
    static std::wstring LegacyPathOf(const ConfigOwnerOptions<Config>& options, const std::wstring& path) {
        if (options.import.run && options.legacy_path.empty()) {
            throw std::invalid_argument("import is set, but no legacy_path names the file it reads");
        }
        if (options.legacy_path.empty()) return std::wstring();
        if (!options.import.run) throw std::invalid_argument("legacy_path is set, but no import reads it");
        std::wstring legacy = detail::OwnerFullPath(options.legacy_path, "legacy_path");
        if (detail::OwnerSamePath(legacy, path)) throw std::invalid_argument("legacy_path names the config file itself");
        return legacy;
    }

    ConfigLoadResult<Config> LoadLocked(std::string& defaults_message) {
        loaded_ = true;
        saves_allowed_ = false;
        committed_.reset();
        sources_.reset();
        snapshot_unapplied_ = false;
        std::vector<std::string> log;
        std::string two_files;
        const std::string unreadable = LoadDefaults(log, two_files);
        ConfigLoadResult<Config> result = LoadConfigFile(std::move(log));
        const std::string refused = DefaultsLines(result.log);
        defaults_message = !unreadable.empty() ? unreadable : !refused.empty() ? refused : two_files;
        return result;
    }

    ConfigLoadResult<Config> LoadConfigFile(std::vector<std::string> log) {
        recorded_write_time_ = detail::OwnerLastWriteTime(path_);

        detail::OwnerStep(hook_, "Open", path_);
        const detail::OwnerFileRead read = detail::OwnerReadFile(path_);
        if (read.error != 0) {
            log.push_back(path_text_ + ": could not be opened: " + detail::OwnerErrorText(read.error));
            return LoadResult(ConfigLoadStatus::Deferred, OnDefaults(), {}, std::move(log),
                              name_ + " cannot be read: " + detail::OwnerReadWhy(read.error) +
                                  ". The mod runs on its default settings this session.");
        }
        if (read.present) {
            if (!legacy_path_.empty() && detail::OwnerFileExists(legacy_path_)) {
                log.push_back(path_text_ + ": settings are read from this file. " + legacy_text_ +
                              " is left as it was and is not read.");
            }
            return ReadCanonical(read.bytes, HasCanonicalStamp(read.bytes), std::move(log));
        }
        if (legacy_path_.empty()) return Create(std::move(log));

        detail::OwnerStep(hook_, "Open", legacy_path_);
        detail::OwnerHeldFile held;
        const detail::OwnerFileRead opened = held.Open(legacy_path_);
        if (opened.error != 0) {
            log.push_back(legacy_text_ + ": could not be opened: " + detail::OwnerErrorText(opened.error));
            return Defer(OnDefaults(), std::move(log), detail::OwnerReadWhy(opened.error), true);
        }
        if (!opened.present) return Create(std::move(log));
        return Migrate(held, opened.bytes, std::move(log));
    }

    ConfigLoadResult<Config> ReadCanonical(const std::string& bytes, bool stamped, std::vector<std::string> log) {
        const CanonicalIni doc = ParseCanonicalIni(bytes);
        if (!doc.IsReadable()) {
            const std::string why = Unreadable(doc);
            log.push_back(path_text_ + ": cannot be read: " + why);
            return LoadResult(ConfigLoadStatus::Unreadable, OnDefaults(), {}, std::move(log),
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
        Config defaults = OnDefaults();
        const CheckedWriteResult written = detail::OwnerWrite(path_, std::nullopt, fresh_bytes_, hook_, "Create.");
        std::string why;
        if (written.Committed()) {
            committed_ = fresh_bytes_;
            recorded_write_time_ = detail::OwnerLastWriteTime(path_);
            saves_allowed_ = true;
            log.push_back(path_text_ + ": created with the default settings.");
            return LoadResult(ConfigLoadStatus::Created, std::move(defaults), {}, std::move(log), "");
        }
        if (detail::OwnerWriteFailed(written)) {
            why = detail::OwnerWriteWhy(path_, written);
            log.push_back(path_text_ + ": not created: " + detail::OwnerWriteLog(path_, written));
        } else {
            why = detail::OwnerConflict(written.status);
            log.push_back(path_text_ + ": not created: " + CheckedWriteStatusName(written.status));
        }
        return LoadResult(ConfigLoadStatus::Deferred, std::move(defaults), {}, std::move(log),
                          name_ + " was not created: " + why + ". The mod runs on its default settings this session.");
    }

    ConfigLoadResult<Config> Migrate(detail::OwnerHeldFile& held, const std::string& snapshot,
                                     std::vector<std::string> log) {
        Config imported = table_.defaults();
        const LegacyInput input = detail::OwnerLegacyInput(legacy_path_);
        detail::OwnerStep(hook_, "Import", legacy_path_);
        const ImportResult legacy = import_.run(input, imported);
        detail::OwnerStep(hook_, "Recheck", legacy_path_);
        std::string changed_why;
        std::string reread;
        const std::uint32_t reread_error = held.Reread(reread);
        if (reread_error != 0) {
            changed_why = detail::OwnerReadWhy(reread_error);
            log.push_back(legacy_text_ + ": could not be read again after the import: " +
                          detail::OwnerErrorText(reread_error));
        } else if (reread != snapshot) {
            changed_why = kChangedWhileRead;
        }
        held.Close();
        if (!changed_why.empty()) return Defer(std::move(imported), std::move(log), changed_why, true);

        if (input.ansi_lossy) {
            log.push_back(legacy_text_ +
                          ": has a character the ANSI code page cannot hold, so a reader given its ANSI path, " +
                          detail::OwnerAnsiForLog(input.ansi_path) + ", finds no file there.");
        }
        switch (legacy.status) {
            case ImportStatus::Refused:
                log.push_back(legacy_text_ + ": the old settings reader refused the file: " + legacy.reason);
                return LoadResult(ConfigLoadStatus::LegacyRefused, std::move(imported), {}, std::move(log),
                                  NotImported(legacy.reason, true));
            case ImportStatus::Undecodable:
                log.push_back(legacy_text_ + ": the old settings reader could not decode the file: " + legacy.reason);
                return Defer(std::move(imported), std::move(log), legacy.reason, true);
            case ImportStatus::Absent:
                if (!input.ansi_lossy) {
                    log.push_back(legacy_text_ + ": the old settings reader found no file, while the owner holds it open (" +
                                  std::to_string(snapshot.size()) + " bytes); the ANSI path it was given is " +
                                  detail::OwnerAnsiForLog(input.ansi_path));
                    LogDropped(legacy, log);
                    return Defer(std::move(imported), std::move(log), "the old settings reader could not find the file",
                                 true);
                }
                log.push_back(legacy_text_ +
                              ": the old settings reader found no file, as the old build found none there and ran on "
                              "its defaults. Those defaults are written to " +
                              path_text_ + ", and this file is left as it was.");
                break;
            case ImportStatus::Imported:
                break;
        }
        LogDropped(legacy, log);

        std::string rendered;
        try {
            rendered = detail::RenderCanonicalMigration(table_, imported, effective_, header_);
        } catch (const std::invalid_argument& e) {
            const std::optional<std::size_t> row = FirstUnwritable(imported);
            if (!row) throw;
            log.push_back(legacy_text_ + ": " + RowName(*row) + " cannot be written in the new format: " + e.what());
            std::string why = Unconvertible(*row, imported);
            return Defer(std::move(imported), std::move(log), why, true);
        }

        Config reread_config = table_.defaults();
        std::vector<std::string> read_back;
        std::vector<CanonicalDiagnostic> diagnostics = Apply(ParseCanonicalIni(rendered), reread_config, read_back);
        const std::optional<std::size_t> different = FirstDifference(imported, reread_config);
        if (different) {
            sources_.reset();
            log.push_back(legacy_text_ + ": " + RowName(*different) + " reads back from the new format as " +
                          RowValueText(*different, reread_config) + ", not " + RowValueText(*different, imported));
            std::string why = Unconvertible(*different, imported);
            return Defer(std::move(imported), std::move(log), why, true);
        }

        const CheckedWriteResult written = detail::OwnerWrite(path_, std::nullopt, rendered, hook_, "Commit.");
        if (detail::OwnerWriteFailed(written)) {
            log.push_back(detail::OwnerWriteLog(path_, written));
            // A conflict whose temporary could not be deleted means a file appeared at `path`
            // first, and the next launch reads that file instead of importing.
            return Defer(std::move(imported), std::move(log), detail::OwnerWriteWhy(path_, written),
                         written.status == CheckedWriteStatus::Failed);
        }
        if (!written.Committed()) {
            log.push_back(path_text_ + ": not created: " + CheckedWriteStatusName(written.status));
            return Defer(std::move(imported), std::move(log), detail::OwnerConflict(written.status), false);
        }

        detail::OwnerStep(hook_, "Remember", path_);
        committed_ = rendered;
        recorded_write_time_ = detail::OwnerLastWriteTime(path_);
        saves_allowed_ = true;
        log.push_back(path_text_ + ": created from " + legacy_text_ + ", which is left as it was.");
        detail::OwnerLogNotCarried(snapshot, import_.keys, legacy_text_, log);
        log.insert(log.end(), read_back.begin(), read_back.end());
        return LoadResult(ConfigLoadStatus::Migrated, std::move(reread_config), std::move(diagnostics), std::move(log),
                          "");
    }

    // `retried` is false where the next launch reads a file another program created at `path`
    // and does not import.
    ConfigLoadResult<Config> Defer(Config config, std::vector<std::string> log, const std::string& why, bool retried) {
        return LoadResult(ConfigLoadStatus::Deferred, std::move(config), {}, std::move(log), NotImported(why, retried));
    }

    std::string NotImported(const std::string& why, bool retried) const {
        const std::string head = legacy_name_ + " was not imported into " + name_ + ": " + why;
        if (retried) return head + ". The mod tries again at the next launch and saves nothing this session.";
        return head + ". The mod saves nothing this session and reads " + name_ + ", not " + legacy_name_ +
               ", at the next launch.";
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
            return NotSaved(detail::OwnerReadWhy(read.error), read.error, std::move(log));
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

        Config baseline = table_.defaults();
        const std::vector<detail::ValueSource> baseline_sources =
            detail::ApplyCanonicalEffective(doc, table_, baseline, effective_, from_defaults_ini_).sources;
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
        const std::vector<detail::ValueSource> written_sources =
            detail::ApplyCanonicalEffective(ParseCanonicalIni(candidate), table_, written_config, effective_,
                                            from_defaults_ini_)
                .sources;
        for (std::size_t i = 0; i < count; ++i) {
            const Config& expected = edited[i] ? changed : baseline;
            if (table_.ops_[i]->Equal(written_config, expected) &&
                (edited[i] || written_sources[i] == baseline_sources[i])) {
                continue;
            }
            log.push_back(path_text_ + ": not saved: " + RowName(i) + " would read back as " +
                          RowValueText(i, written_config) + " from " + SourceName(written_sources[i]) + ", not " +
                          RowValueText(i, expected) + " from " +
                          SourceName(edited[i] ? detail::ValueSource::kFile : baseline_sources[i]));
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
        ConfigSaveResult saved;
        for (std::size_t i = 0; i < count; ++i) {
            if (!edited[i] || !detail::FollowsDefaultsIni(table_.rows_[i]) ||
                baseline_sources[i] == detail::ValueSource::kFile) {
                continue;
            }
            saved.log.push_back(path_text_ + ": " + table_.rows_[i].key + "=" + RowValueText(i, changed) +
                                " is now set for this game, and no longer follows Defaults.ini.");
        }
        return saved;
    }

    static const char* SourceName(detail::ValueSource source) {
        switch (source) {
            case detail::ValueSource::kFile: return "the file";
            case detail::ValueSource::kDefaultsIni: return "Defaults.ini";
            case detail::ValueSource::kBuiltIn: return "the built-in value";
        }
        throw std::invalid_argument("a value source outside the enum");
    }

    static ConfigSaveResult NotSaved(const std::string& why, std::uint32_t error, std::vector<std::string> log) {
        ConfigSaveResult result;
        result.status = ConfigSaveStatus::NotSaved;
        result.reason = "Settings not saved: " + why + ".";
        result.error = error;
        result.log = std::move(log);
        return result;
    }

    ConfigReloadResult<Config> ReloadLocked(std::string& defaults_message) {
        sources_.reset();
        const std::uint64_t write_time = detail::OwnerLastWriteTime(path_);
        defaults_write_time_ = DefaultsWriteTime();
        std::vector<std::string> log;
        bool changed = false;
        const std::string unreadable = ReloadDefaults(log, changed);
        const bool unapplied = changed || snapshot_unapplied_;
        ConfigReloadResult<Config> result = ReloadConfigFile(std::move(log), write_time, unapplied);
        snapshot_unapplied_ = unapplied && result.status != ConfigReloadStatus::Applied;
        const std::string refused = DefaultsLines(result.log);
        defaults_message = !unreadable.empty() ? unreadable : unapplied ? refused : std::string();
        return result;
    }

    ConfigReloadResult<Config> ReloadConfigFile(std::vector<std::string> log, std::uint64_t write_time,
                                                bool snapshot_unapplied) {
        const detail::OwnerFileRead read = detail::OwnerReadFile(path_);
        if (read.error != 0) {
            log.push_back(path_text_ + ": not reloaded: " + detail::OwnerErrorText(read.error));
            return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                            name_ + " cannot be read: " + detail::OwnerReadWhy(read.error) +
                                ". The current settings stay.");
        }
        recorded_write_time_ = write_time;
        if (!read.present) {
            log.push_back(path_text_ + ": not reloaded: the file is missing");
            return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                            name_ + " is missing, so the current settings stay. It is created again at the next launch.");
        }
        const std::string& bytes = read.bytes;

        if (committed_ && bytes == *committed_ && !snapshot_unapplied) {
            return Reloaded(ConfigReloadStatus::Unchanged, std::nullopt, {}, std::move(log), "");
        }

        const CanonicalIni doc = ParseCanonicalIni(bytes);
        if (!doc.IsReadable()) {
            const std::string why = Unreadable(doc);
            log.push_back(path_text_ + ": not reloaded: " + why);
            return Reloaded(ConfigReloadStatus::Unreadable, std::nullopt, {}, std::move(log),
                            name_ + " cannot be read: " + why + ". The current settings stay.");
        }
        Config config = table_.defaults();
        std::vector<CanonicalDiagnostic> diagnostics = Apply(doc, config, log);
        if (committed_ && bytes != *committed_) committed_.reset();
        saves_allowed_ = true;
        return Reloaded(ConfigReloadStatus::Applied, std::move(config), std::move(diagnostics), std::move(log), "");
    }

    // Where Defaults.ini is, created where the choice allows it and read once. Adds its one
    // location line, or the one failure line, and returns the message for a file that exists and
    // cannot be read, or empty; `two_files` gets the message for two files, or empty.
    std::string LoadDefaults(std::vector<std::string>& log, std::string& two_files) {
        two_files.clear();
        const detail::DefaultsResolution resolution = detail::ResolveDefaultsFile(defaults_);
        const std::vector<detail::DefaultsCandidate>& candidates = resolution.candidates;
        std::vector<bool> exists;
        for (const detail::DefaultsCandidate& candidate : candidates) exists.push_back(detail::OwnerPathExists(candidate.path));
        std::vector<detail::DefaultsCreationOutcome> outcomes(candidates.size());
        std::optional<std::string> created;
        detail::DefaultsChoice choice = detail::ChooseDefaults(resolution, exists, outcomes);
        while (choice.create >= 0) {
            const std::size_t index = static_cast<std::size_t>(choice.create);
            outcomes[index] = CreateDefaults(candidates[index], created);
            choice = detail::ChooseDefaults(resolution, exists, outcomes);
        }

        if (choice.read >= 0) {
            defaults_at_ = candidates[static_cast<std::size_t>(choice.read)];
        } else if (!candidates.empty()) {
            defaults_at_ = candidates[0];
        } else {
            defaults_at_.reset();
        }
        defaults_write_time_ = DefaultsWriteTime();
        defaults_seen_.reset();
        if (choice.read < 0) {
            log.push_back(choice.line);
            TakeSnapshot(detail::ReadDefaultsIni(""));
            return {};
        }

        const detail::DefaultsCandidate& at = candidates[static_cast<std::size_t>(choice.read)];
        std::string bytes;
        if (outcomes[static_cast<std::size_t>(choice.read)].kind == detail::DefaultsCreation::kCreated) {
            bytes = *created;
        } else {
            const detail::OwnerFileRead read = detail::OwnerReadFile(at.path);
            if (read.error != 0) return NotRead(log, at, detail::OwnerReadWhy(read.error));
            if (!read.present) {
                NotRead(log, at, "the file was deleted at the same time");
                return {};
            }
            bytes = read.bytes;
        }

        defaults_seen_ = bytes;
        detail::DefaultsIniSnapshot snapshot = detail::ReadDefaultsIni(bytes);
        if (snapshot.unreadable) return NotRead(log, at, *snapshot.unreadable);
        log.push_back(choice.line);
        if (snapshot.format_line) log.push_back(*snapshot.format_line);
        TakeSnapshot(std::move(snapshot));
        two_files = choice.message;
        return {};
    }

    // A Defaults.ini found and not read: its one line, the built-in values, and the message.
    std::string NotRead(std::vector<std::string>& log, const detail::DefaultsCandidate& at, const std::string& why) {
        log.push_back("Defaults.ini: " + detail::DefaultsNamed(at) + " cannot be read: " + why + "." +
                      detail::kDefaultsBuiltIn);
        TakeSnapshot(detail::ReadDefaultsIni(""));
        return "Defaults.ini cannot be read: " + why + ". Settings that use it take the built-in values.";
    }

    // The folder by the one-level rule, then the file through the checked writer with no expected
    // bytes, so it is never written over a file that appeared meanwhile.
    detail::DefaultsCreationOutcome CreateDefaults(const detail::DefaultsCandidate& candidate,
                                                   std::optional<std::string>& created) {
        const std::uint32_t folder = detail::CreateDefaultsFolder(candidate.folder);
        if (folder == detail::kDefaultsPathNotFound) return {detail::DefaultsCreation::kParentMissing, ""};
        if (folder != 0) return {detail::DefaultsCreation::kFolderFailed, detail::DefaultsFolderWhy(folder)};
        const std::string bytes = detail::RenderDefaultsIni();
        const CheckedWriteResult written = detail::OwnerWrite(candidate.path, std::nullopt, bytes, hook_, "Defaults.");
        if (detail::OwnerWriteFailed(written)) {
            return {detail::DefaultsCreation::kFileFailed, detail::OwnerWriteWhy(candidate.path, written)};
        }
        if (written.Committed()) {
            created = bytes;
            return {detail::DefaultsCreation::kCreated, ""};
        }
        if (written.status == CheckedWriteStatus::TargetAppeared) return {detail::DefaultsCreation::kAppeared, ""};
        throw std::logic_error(std::string("a write that expects no file gave ") + CheckedWriteStatusName(written.status));
    }

    // Defaults.ini again, where Load found it. Returns the message for one that went missing or
    // cannot be read, which keeps the values it gave, or empty.
    std::string ReloadDefaults(std::vector<std::string>& log, bool& changed) {
        changed = false;
        if (!defaults_at_) return {};
        const std::string named = detail::DefaultsNamed(*defaults_at_);
        const detail::OwnerFileRead read = detail::OwnerReadFile(defaults_at_->path);
        if (read.error != 0) return KeptValues(log, named, detail::OwnerReadWhy(read.error));
        if (!read.present) {
            if (!defaults_seen_) return {};
            defaults_seen_.reset();
            log.push_back("Defaults.ini: " + named + " is missing." + kKeptUntilRestart);
            return std::string("Defaults.ini is missing.") + kKeptUntilRestart;
        }
        if (defaults_seen_ && read.bytes == *defaults_seen_) return {};
        defaults_seen_ = read.bytes;
        detail::DefaultsIniSnapshot snapshot = detail::ReadDefaultsIni(read.bytes);
        if (snapshot.unreadable) return KeptValues(log, named, *snapshot.unreadable);
        log.push_back("Defaults.ini: " + named + " (read)");
        if (snapshot.format_line) log.push_back(*snapshot.format_line);
        TakeSnapshot(std::move(snapshot));
        changed = true;
        return {};
    }

    static std::string KeptValues(std::vector<std::string>& log, const std::string& named, const std::string& why) {
        log.push_back("Defaults.ini: " + named + " cannot be read: " + why + "." + kKeptUntilRestart);
        return "Defaults.ini cannot be read: " + why + "." + kKeptUntilRestart;
    }

    // The values Defaults.ini gives: each accepted value of a row that follows Defaults.ini,
    // through the row's own codec and setter, over a fresh defaults instance.
    void TakeSnapshot(detail::DefaultsIniSnapshot snapshot) {
        Config effective = table_.defaults();
        std::vector<schema::Concept> from;
        for (std::size_t i = 0; i < table_.rows_.size(); ++i) {
            if (!detail::FollowsDefaultsIni(table_.rows_[i])) continue;
            const schema::Concept id = *table_.rows_[i].concept_id;
            const detail::DefaultsIniValue& value = snapshot.Value(id);
            if (value.state != detail::DefaultsIniValueState::kAccepted) continue;
            const std::string error = table_.ops_[i]->Apply(value.value, effective);
            if (!error.empty()) {
                throw std::logic_error(RowName(i) + ": Defaults.ini's value " + detail::DefaultsIniText(value.value) +
                                       " passed the Defaults.ini reader and not the row's codec: " + error);
            }
            from.push_back(id);
        }
        snapshot_ = std::move(snapshot);
        effective_ = std::move(effective);
        from_defaults_ini_ = std::move(from);
    }

    // Design 3.3: which rows came from Defaults.ini, which the file sets itself, which took the
    // built-in, and a line per refused value this game would take. Returns the in-game message for
    // those refused values, or empty.
    std::string DefaultsLines(std::vector<std::string>& log) const {
        if (!sources_) return {};
        const Config& built_in = table_.defaults();
        std::vector<std::string> taken;
        std::vector<std::string> own;
        std::vector<std::string> fallen;
        std::vector<std::string> refused_lines;
        std::vector<std::string> refused;
        bool pair_logged = false;
        for (std::size_t i = 0; i < table_.rows_.size(); ++i) {
            if (!detail::FollowsDefaultsIni(table_.rows_[i])) continue;
            const schema::Concept id = *table_.rows_[i].concept_id;
            const std::string& key = table_.rows_[i].key;
            if ((*sources_)[i] == detail::ValueSource::kFile) {
                own.push_back(key);
                continue;
            }
            if ((*sources_)[i] == detail::ValueSource::kDefaultsIni) {
                taken.push_back(key + "=" + RowValueText(i, effective_));
                continue;
            }
            fallen.push_back(key + "=" + RowValueText(i, built_in));
            const detail::DefaultsIniValue& value = snapshot_.Value(id);
            if (value.state != detail::DefaultsIniValueState::kRefused) continue;
            const bool pair = id == schema::Concept::RotationEnabled || id == schema::Concept::PositionEnabled;
            if (!pair || !snapshot_.pair_refused) {
                refused_lines.push_back(detail::DefaultsIniRefusedLine(value, RowValueText(i, built_in)));
                refused.push_back(key + "=" + detail::DefaultsIniText(value.value));
                continue;
            }
            if (pair_logged) continue;
            pair_logged = true;
            refused_lines.push_back(detail::DefaultsIniPairLine(
                snapshot_, BuiltInText(schema::Concept::RotationEnabled), BuiltInText(schema::Concept::PositionEnabled)));
            std::string entry;
            for (const schema::Concept member : {schema::Concept::RotationEnabled, schema::Concept::PositionEnabled}) {
                const detail::DefaultsIniValue& held = snapshot_.Value(member);
                if (held.state != detail::DefaultsIniValueState::kRefused) continue;
                entry += (entry.empty() ? "" : " and ") + std::string(schema::kConcepts[static_cast<std::size_t>(member)].key) +
                         "=" + detail::DefaultsIniText(held.value);
            }
            refused.push_back(entry);
        }

        if (!taken.empty()) log.push_back(path_text_ + ": from Defaults.ini: " + Joined(taken, "; "));
        if (!own.empty()) {
            log.push_back(path_text_ + ": set in this file, so Defaults.ini does not change them: " + Joined(own, ", ") + ".");
        }
        if (!fallen.empty()) log.push_back(path_text_ + ": built-in, not set in Defaults.ini: " + Joined(fallen, "; "));
        log.insert(log.end(), refused_lines.begin(), refused_lines.end());
        if (refused.empty()) return {};
        return "Defaults.ini: " + std::to_string(refused.size()) +
               (refused.size() == 1 ? " setting cannot" : " settings cannot") + " be used (" + Joined(refused, "; ") +
               "), so this game uses its built-in values for them. The log has the details.";
    }

    // The game's built-in value of a tracking mode row, or the schema's for one the table does not
    // bind; the two are equal for every row that follows Defaults.ini.
    std::string BuiltInText(schema::Concept id) const {
        const std::optional<std::size_t> row = RowOf(id);
        return row ? RowValueText(*row, table_.defaults()) : schema::kConcepts[static_cast<std::size_t>(id)].default_text;
    }

    static std::string Joined(const std::vector<std::string>& items, const char* separator) {
        std::string text;
        for (const std::string& item : items) text += (text.empty() ? "" : separator) + item;
        return text;
    }

    std::vector<CanonicalDiagnostic> Apply(const CanonicalIni& doc, Config& config, std::vector<std::string>& log) {
        std::vector<CanonicalDiagnostic> diagnostics = doc.diagnostics;
        detail::EffectiveApplyResult applied =
            detail::ApplyCanonicalEffective(doc, table_, config, effective_, from_defaults_ini_);
        diagnostics.insert(diagnostics.end(), applied.report.diagnostics.begin(), applied.report.diagnostics.end());
        sources_ = std::move(applied.sources);
        for (const CanonicalDiagnostic& diagnostic : diagnostics) {
            log.push_back(path_text_ + ": " + DescribeCanonicalDiagnostic(diagnostic));
        }
        return diagnostics;
    }

    // The defaults the session runs on where no file is read: Defaults.ini's values on the rows
    // that follow it, the table's on the rest.
    Config OnDefaults() {
        Config config = table_.defaults();
        sources_ = detail::ApplyCanonicalEffective(ParseCanonicalIni(""), table_, config, effective_, from_defaults_ini_)
                       .sources;
        return config;
    }

    std::uint64_t DefaultsWriteTime() const {
        return defaults_at_ ? detail::DefaultsLastWriteTime(defaults_at_->path) : 0;
    }

    void LogDropped(const ImportResult& import, std::vector<std::string>& log) const {
        for (const DroppedValue& dropped : import.dropped) {
            log.push_back(legacy_text_ + ": " + DescribeDroppedValue(dropped));
        }
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
    // Empty for a game with no import.
    const std::wstring legacy_path_;
    const std::string legacy_text_;
    const std::string legacy_name_;
    const ConfigTable<Config> table_;
    const LegacyImport<Config> import_;
    const RenderHeader header_;
    const std::function<void(const std::string&)> status_sink_;
    const detail::ConfigOwnerHook hook_;
    const DefaultsFile defaults_;
    std::string fresh_bytes_;

    // Defaults.ini for the session: where it is, the bytes last read there (none when there was
    // no file), their write time, and what it gives.
    std::optional<detail::DefaultsCandidate> defaults_at_;
    std::optional<std::string> defaults_seen_;
    std::uint64_t defaults_write_time_ = 0;
    detail::DefaultsIniSnapshot snapshot_;
    Config effective_;
    std::vector<schema::Concept> from_defaults_ini_;
    // Where each row of the config the last Load or Reload returned took its value, when it
    // was read over the effective defaults; none for a config the import gave.
    std::optional<std::vector<detail::ValueSource>> sources_;
    // A Defaults.ini a Reload read while the file could not be read: the next Reload that reads
    // the file applies it, and tells its refused values then.
    bool snapshot_unapplied_ = false;

    std::mutex mutex_;
    bool loaded_ = false;
    bool saves_allowed_ = false;
    std::optional<std::string> committed_;
    std::uint64_t recorded_write_time_ = 0;
};

#endif  // _WIN32

}  // namespace cameraunlock::config
