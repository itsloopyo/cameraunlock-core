using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The one reader and writer of a game's canonical config file. While that file is absent it
    /// imports the game's legacy file, a separate file it never writes, through the game's frozen
    /// import into a new config file, or creates the config file from the table's fresh render. It
    /// saves the rows the table marks Writable, and reloads. It writes only the config file and a
    /// Defaults.ini that is absent, and only through <see cref="CheckedFileWriter"/>, so every write
    /// replaces the whole file or nothing. The C++ twin is cameraunlock::config::ConfigOwner.
    /// <para>
    /// Every concept row the table does not mark PerGame takes its default from Defaults.ini
    /// (<see cref="ConfigOwnerOptions{TConfig}.Defaults"/>): <c>default</c>, a missing key and an
    /// invalid value on such a row read Defaults.ini's value, or the row's own default where
    /// Defaults.ini gives none. Load finds the file, creates it with the built-in values where none
    /// exists and it may, and reads it once; Reload reads it again. No failure to find, create or read
    /// it stops the mod: the rows then use the built-in values, with one line in the log.
    /// </para>
    /// <para>
    /// On Windows, under Wine included, it reads and writes. Anywhere else (Linux or macOS with a
    /// native runtime) it reads the config file, imports the legacy file in memory, reads the first
    /// Defaults.ini it finds and writes nothing: Load returns <see cref="ConfigLoadStatus.ReadOnly"/>
    /// and every save is NotSaved.
    /// </para>
    /// <para>
    /// Build it before anything reads the file, and read the file only through it. One lock
    /// serializes <see cref="Load"/>, <see cref="Reload"/> and <see cref="Save"/>; the status sink
    /// runs after the lock is released. A Unity mod calls them on the main thread, where its
    /// hotkeys fire: a save is one synchronous write per key press, never a per-frame call. The
    /// lock does not coordinate separate processes.
    /// </para>
    /// <para>
    /// Nothing here writes a row the table does not mark Writable, so the End toggle, which
    /// changes only the session, never reaches EnableOnStartup unless the game marks that row
    /// Writable and calls Save for it. A table with both RotationEnabled and PositionEnabled
    /// must mark both Writable or neither, since a mode change writes the pair.
    /// </para>
    /// </summary>
    public sealed class ConfigOwner<TConfig> where TConfig : class
    {
        private const string StampSection = "CameraUnlock";
        private const string ChangedWhileRead = "the file was changed by another program while it was read";
        private const string ReadOnlyText = "Settings are read but not saved on this system: this version saves settings "
            + "only on Windows, including under Wine and Proton. Changes made in game last until the game closes.";
        private const string SavesOnlyOnWindows = "this version saves settings only on Windows";
        private const string KeptUntilRestart = " Settings that use it keep the values they had until the game restarts.";
        private const int HResultAccessDenied = unchecked((int)0x80070005);
        private const int HResultSharingViolation = unchecked((int)0x80070020);
        private const int HResultLockViolation = unchecked((int)0x80070021);
        private const int ErrorAccessDenied = 5;
        private const int ErrorSharingViolation = 32;
        private const int ErrorLockViolation = 33;
        private const uint InvalidFileAttributes = 0xFFFFFFFF;
        private const uint FileAttributeReadOnly = 0x1;

        private readonly object _lock = new object();
        private readonly string _path;
        private readonly string _name;
        private readonly ConfigTable<TConfig> _table;
        private readonly RenderHeader _header;
        private readonly byte[] _freshBytes;
        private readonly byte[][] _importSections;
        private readonly byte[][] _importKeys;
        private readonly DefaultsFile _defaultsFile;
        private readonly bool _writes;
#if NULLABLE_ENABLED
        private readonly LegacyImport<TConfig>? _import;
        private readonly string? _legacySourcePath;
        private readonly Action<string>? _statusSink;
        private readonly Action<string, string>? _beforeStep;
        private byte[]? _committed;
        private DefaultsCandidate? _defaultsAt;
        private byte[]? _defaultsSeen;
        private ConfigValueSource[]? _sources;
#else
        private readonly LegacyImport<TConfig> _import;
        private readonly string _legacySourcePath;
        private readonly Action<string> _statusSink;
        private readonly Action<string, string> _beforeStep;
        private byte[] _committed;
        private DefaultsCandidate _defaultsAt;
        private byte[] _defaultsSeen;
        private ConfigValueSource[] _sources;
#endif
        private bool _loaded;
        private bool _savesAllowed;
        private DateTime _recordedWriteTime;
        private DateTime _defaultsWriteTime;
        private DefaultsIniSnapshot _snapshot;
        private TConfig _effective;
        private ConceptDescriptor[] _fromDefaultsIni;

        /// <exception cref="ArgumentNullException"><paramref name="options"/> is null.</exception>
        /// <exception cref="ArgumentException">Path, Table, Header or Defaults is missing; a path is
        /// not absolute; Import is set without a LegacySourcePath; LegacySourcePath is set without an
        /// Import, or names Path; the table has both RotationEnabled and PositionEnabled and marks
        /// only one of them Writable; a legacy key's name holds an unpaired surrogate; or the table
        /// cannot render its fresh file under the header (<see cref="ConfigTable{TConfig}.RenderFresh"/>:
        /// a concept row not marked PerGame whose default is not the schema's, RotationEnabled
        /// without PositionEnabled, or a header the renderer refuses).</exception>
        public ConfigOwner(ConfigOwnerOptions<TConfig> options)
            : this(options, null, Environment.OSVersion.Platform)
        {
        }

#if NULLABLE_ENABLED
        internal ConfigOwner(ConfigOwnerOptions<TConfig> options, Action<string, string>? beforeStep)
#else
        internal ConfigOwner(ConfigOwnerOptions<TConfig> options, Action<string, string> beforeStep)
#endif
            : this(options, beforeStep, Environment.OSVersion.Platform)
        {
        }

        /// <summary>
        /// <see cref="ConfigOwner{TConfig}(ConfigOwnerOptions{TConfig})"/> with a hook run before each
        /// step, given a label and the path the step acts on: <c>Open</c> (the config file, then the
        /// legacy file), <c>Import</c>, <c>Recheck</c> and <c>Remember</c> for the import's own steps,
        /// and <c>Defaults.</c>, <c>Commit.</c>, <c>Create.</c> or <c>Save.</c> followed by a
        /// <see cref="CheckedWriteStep"/> name for the writer's. A test throws from it to fail a step,
        /// or ends the process to interrupt one. <paramref name="platform"/> stands for the system the
        /// owner runs on: anything but Win32NT gives the read-only owner.
        /// </summary>
#if NULLABLE_ENABLED
        internal ConfigOwner(ConfigOwnerOptions<TConfig> options, Action<string, string>? beforeStep, PlatformID platform)
#else
        internal ConfigOwner(ConfigOwnerOptions<TConfig> options, Action<string, string> beforeStep, PlatformID platform)
#endif
        {
            if (options == null) throw new ArgumentNullException("options");
            if (options.Table == null) throw new ArgumentException("the options name no Table", "options");
            if (options.Header == null) throw new ArgumentException("the options name no Header", "options");
            if (options.Defaults == null)
            {
                throw new ArgumentException("the options name no Defaults: a mod sets DefaultsFile.PerUser(), and a test "
                    + "DefaultsFile.At(path) with a scratch path", "options");
            }
            _path = AbsolutePath(options.Path, "Path");
            if (options.Import != null && options.LegacySourcePath == null)
            {
                throw new ArgumentException("Import is set, but no LegacySourcePath names the file it reads", "options");
            }
            if (options.LegacySourcePath != null)
            {
                if (options.Import == null)
                {
                    throw new ArgumentException("LegacySourcePath is set, but no Import reads it", "options");
                }
                _legacySourcePath = AbsolutePath(options.LegacySourcePath, "LegacySourcePath");
                if (string.Equals(_legacySourcePath, _path, StringComparison.OrdinalIgnoreCase))
                {
                    throw new ArgumentException("LegacySourcePath names the config file itself", "options");
                }
            }

            _name = System.IO.Path.GetFileName(_path);
            _table = options.Table;
            _header = options.Header;
            _import = options.Import;
            _statusSink = options.StatusSink;
            _beforeStep = beforeStep;
            _defaultsFile = options.Defaults;
            _writes = platform == PlatformID.Win32NT;
            int rotation = _table.RowOf(ConfigConcepts.RotationEnabled);
            int position = _table.RowOf(ConfigConcepts.PositionEnabled);
            if (rotation >= 0 && position >= 0 && _table.RowWritable(rotation) != _table.RowWritable(position))
            {
                int writable = _table.RowWritable(rotation) ? rotation : position;
                int other = writable == rotation ? position : rotation;
                throw new ArgumentException("the table marks " + _table.RowName(writable) + " Writable but not "
                    + _table.RowName(other) + ", and a tracking mode change writes both", "options");
            }
            _freshBytes = _table.RenderFresh(_header);
            IList<LegacyKey> keys = _import == null ? (IList<LegacyKey>)new LegacyKey[0] : _import.Keys;
            _importSections = new byte[keys.Count][];
            _importKeys = new byte[keys.Count][];
            for (int i = 0; i < keys.Count; i++)
            {
                _importSections[i] = CanonicalIni.NameBytes(keys[i].Section, "options");
                _importKeys[i] = CanonicalIni.NameBytes(keys[i].Key, "options");
            }
            _snapshot = DefaultsIni.Read(new byte[0]);
            _effective = _table.CreateDefaults();
            _fromDefaultsIni = new ConceptDescriptor[0];
        }

        /// <summary>
        /// Reads Defaults.ini, then the config file, first importing the legacy file or creating the
        /// config file where there is none:
        /// <list type="bullet">
        /// <item>Defaults.ini first: where no file exists it is created with the built-in values, on
        /// Windows and under Wine outside a packaged app, never over a file that appears meanwhile,
        /// whatever becomes of the config file. It is read once, and the rows that follow it take its
        /// values for the session. A Defaults.ini that cannot be found, created or read gives the
        /// built-in values, one line in the log and, when it exists and cannot be read, one message.</item>
        /// <item>A file at Path: read as canonical (Canonical), stamped or not, or Unreadable when
        /// saved as UTF-16 or holding a NUL. An unstamped file gets a line in the log saying the
        /// next save adds the section; the first save that changes a row stamps it. The import
        /// never runs and the legacy file is never opened; when one exists, a line in the log says
        /// the settings are read from Path and the legacy file is not read.</item>
        /// <item>No file at Path and a file at LegacySourcePath: the legacy file is imported into a
        /// new file at Path (Migrated). A row that follows Defaults.ini is written <c>default</c>
        /// where the imported value equals what <c>default</c> gives it at this Load, and the
        /// tracking mode pair only when both rows do.</item>
        /// <item>Neither: the table's fresh render is written, never over a file that appears
        /// meanwhile (Created). If one appears, or the folder cannot be written, the session runs on
        /// the defaults and nothing retries (Deferred).</item>
        /// </list>
        /// <para>
        /// An import holds the legacy file open, readable and writable by others but not
        /// deletable, while it reads the bytes, runs the import and reads the bytes again; bytes
        /// that changed meanwhile defer it. It then renders the imported settings, reads the render
        /// back through the table and requires every row to equal the import's (floats bitwise),
        /// and creates the file at Path only if no file has appeared there. Any failure defers: Path
        /// is not created by the owner, the session runs on what the import gave, nothing is saved,
        /// and the player is told once through the status sink. The next launch imports again,
        /// unless another program created a file at Path meanwhile: that file is read at the next
        /// launch and the import does not run again, which the player message says. A legacy file
        /// that cannot be opened defers the same way, on the defaults. The legacy file is never
        /// written, renamed, deleted or copied, whatever happens. A process killed at any point
        /// leaves Path and Defaults.ini each absent or whole.
        /// </para>
        /// <para>
        /// Off Windows nothing is written: the config file is read, or the legacy file imported and
        /// read back in memory, or the session runs on the defaults, and the status is ReadOnly.
        /// </para>
        /// <para>
        /// The status sink gets the config file's message, then at most one about Defaults.ini:
        /// that it cannot be read, else that a value this game would take from it is refused, else
        /// that two Defaults.ini files exist and one is ignored. Ordinary I/O failures are reported
        /// through the result. An import that throws, or a hook in the table that throws, is a bug
        /// and the exception is not caught.
        /// </para>
        /// </summary>
        public ConfigLoadResult<TConfig> Load()
        {
            ConfigLoadResult<TConfig> result;
            string defaultsMessage;
            lock (_lock)
            {
                result = LoadLocked(out defaultsMessage);
            }
            if (_statusSink != null)
            {
                bool report = result.Status == ConfigLoadStatus.Deferred || result.Status == ConfigLoadStatus.LegacyRefused
                    || result.Status == ConfigLoadStatus.Unreadable || result.Status == ConfigLoadStatus.ReadOnly;
                if (report) _statusSink(result.Reason);
                if (defaultsMessage.Length > 0) _statusSink(defaultsMessage);
            }
            return result;
        }

        /// <summary>
        /// Changes rows of the file. <paramref name="change"/> is given the settings read from the
        /// file as it is now and sets the new values on it; the game has already applied them to
        /// its running state. A row that holds a new value afterwards must be marked Writable in the
        /// table, or this throws. When RotationEnabled or PositionEnabled changes, both are written,
        /// so a tracking mode is always one edit. A row that held <c>default</c> or was missing is
        /// written as its value, so from then on this game keeps it, and the log says so.
        /// <para>
        /// Saves only a file the canonical reader can read and whose ConfigFormat is not newer than
        /// this build's; an unstamped file, or a stamp with no ConfigFormat or one that is not a
        /// number, gets its [CameraUnlock] ConfigFormat line in the same write. The file is read over
        /// the values Defaults.ini gave this session. The edited bytes are read back through the
        /// table before anything is written: only the changed rows may differ, they must hold the
        /// new values, and every other row must take its value from where it did. Rows already
        /// holding the values write nothing and report Saved. A missing file is not created here:
        /// <see cref="Load"/> creates it at the next launch, importing the legacy file if there is
        /// one. After a Deferred, LegacyRefused or Unreadable load nothing is saved that session,
        /// until a Reload applies a readable file. After a ReadOnly load nothing is saved at all.
        /// Neither the legacy file nor Defaults.ini is ever written.
        /// </para>
        /// <para>
        /// Never rolls back and never retries. NotSaved and Uncertain are handed to the status sink
        /// once. An editor writing the file between the last check and the replacement is
        /// overwritten: the replacement is not compare-and-swap.
        /// </para>
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="change"/> is null.</exception>
        /// <exception cref="InvalidOperationException">Load has not run, or a row that is not
        /// Writable changed; the message names the row.</exception>
        /// <exception cref="ArgumentException">A Writable row holds a value its codec cannot write,
        /// or text outside printable ASCII, which the editor refuses.</exception>
        public ConfigSaveResult Save(Action<TConfig> change)
        {
            if (change == null) throw new ArgumentNullException("change");
            ConfigSaveResult result;
            lock (_lock)
            {
                RequireLoaded("Save");
                result = SaveLocked(change);
            }
            if (result.Status != ConfigSaveStatus.Saved && _statusSink != null) _statusSink(result.Reason);
            return result;
        }

        /// <summary>
        /// Reads Defaults.ini and the file again, for a watcher that saw <see cref="FileChanged"/> or
        /// a reload the player asked for. Defaults.ini is read where Load found it: new readable bytes
        /// replace the values it gives; a file that went missing or cannot be read keeps them, with
        /// one line in the log and one message. Unchanged when the file holds exactly the bytes the
        /// owner last wrote, the import's and creation's included, no Reload has applied other bytes
        /// since, and Defaults.ini gave nothing new. Any other file is read as canonical, stamped or
        /// not, over Defaults.ini's current values (Applied). A missing file, or one the canonical
        /// reader cannot read, is Unreadable, which leaves the game's settings as they are and is
        /// handed to the status sink. Never writes, never imports and never opens the legacy file.
        /// </summary>
        /// <exception cref="InvalidOperationException">Load has not run.</exception>
        public ConfigReloadResult<TConfig> Reload()
        {
            ConfigReloadResult<TConfig> result;
            string defaultsMessage;
            lock (_lock)
            {
                RequireLoaded("Reload");
                result = ReloadLocked(out defaultsMessage);
            }
            if (_statusSink != null)
            {
                if (result.Status == ConfigReloadStatus.Unreadable) _statusSink(result.Reason);
                if (defaultsMessage.Length > 0) _statusSink(defaultsMessage);
            }
            return result;
        }

        /// <summary>
        /// True when the last write time of the file, or of Defaults.ini where Load found it, differs
        /// from the one the owner recorded at its last Load, Reload or committed Save. A missing file
        /// has a write time too, so a file that appears or goes away counts.
        /// </summary>
        /// <exception cref="InvalidOperationException">Load has not run.</exception>
        public bool FileChanged()
        {
            lock (_lock)
            {
                RequireLoaded("FileChanged");
                return File.GetLastWriteTimeUtc(_path) != _recordedWriteTime || DefaultsWriteTime() != _defaultsWriteTime;
            }
        }

        private ConfigLoadResult<TConfig> LoadLocked(out string defaultsMessage)
        {
            _loaded = true;
            _savesAllowed = false;
            _committed = null;
            _sources = null;
            var log = new List<string>();
            string twoFiles;
            string unreadable = LoadDefaults(log, out twoFiles);

            ConfigLoadResult<TConfig> loaded = LoadConfigFile(log);
            var lines = new List<string>(loaded.Log);
            string refused = DefaultsLines(lines);
            defaultsMessage = unreadable.Length > 0 ? unreadable : refused.Length > 0 ? refused : twoFiles;

            ConfigLoadStatus status = loaded.Status;
            string reason = loaded.Reason;
            if (!_writes && (status == ConfigLoadStatus.Canonical || status == ConfigLoadStatus.Migrated
                || status == ConfigLoadStatus.Created))
            {
                status = ConfigLoadStatus.ReadOnly;
                reason = ReadOnlyText;
                lines.Add(ReadOnlyText);
            }
            return Result(status, loaded.Config, new List<CanonicalDiagnostic>(loaded.Diagnostics), lines, reason);
        }

        private ConfigLoadResult<TConfig> LoadConfigFile(List<string> log)
        {
            _recordedWriteTime = File.GetLastWriteTimeUtc(_path);

#if NULLABLE_ENABLED
            byte[]? bytes;
            Held? held = null;
#else
            byte[] bytes;
            Held held = null;
#endif
            string opening = _path;
            try
            {
                Step("Open", opening);
                bytes = ReadIfPresent(opening);
                if (bytes == null && _legacySourcePath != null)
                {
                    opening = _legacySourcePath;
                    Step("Open", opening);
                    held = Held.Open(opening);
                }
            }
            catch (IOException e)
            {
                return CannotOpen(opening, e, log);
            }
            catch (UnauthorizedAccessException e)
            {
                return CannotOpen(opening, e, log);
            }

            if (bytes != null)
            {
                if (_legacySourcePath != null && File.Exists(_legacySourcePath))
                {
                    log.Add(_path + ": settings are read from this file. " + _legacySourcePath
                        + " is left as it was and is not read.");
                }
                return ReadCanonical(bytes, CanonicalIni.HasStamp(bytes), log);
            }
            if (held != null) return Migrate(held, opening, log);
            return Create(log);
        }

        // Where Defaults.ini is, created where the choice allows it and read once. Adds its one
        // location line, or the one failure line, and returns the message for a file that exists and
        // cannot be read, or empty; twoFiles gets the message for two files, or empty.
        private string LoadDefaults(List<string> log, out string twoFiles)
        {
            twoFiles = string.Empty;
            DefaultsResolution resolution = _defaultsFile.Resolve(_writes);
            IList<DefaultsCandidate> candidates = resolution.Candidates;
            var exists = new bool[candidates.Count];
            for (int i = 0; i < exists.Length; i++) exists[i] = Exists(candidates[i].Path);
            var outcomes = new DefaultsCreationOutcome[candidates.Count];
#if NULLABLE_ENABLED
            byte[]? created = null;
#else
            byte[] created = null;
#endif
            DefaultsChoice choice = DefaultsLocation.Choose(resolution, exists, outcomes);
            while (choice.Create >= 0)
            {
                outcomes[choice.Create] = CreateDefaults(candidates[choice.Create], out created);
                choice = DefaultsLocation.Choose(resolution, exists, outcomes);
            }

            _defaultsAt = choice.Read >= 0 ? candidates[choice.Read] : candidates.Count > 0 ? candidates[0] : null;
            _defaultsWriteTime = DefaultsWriteTime();
            _defaultsSeen = null;
            DefaultsIniSnapshot none = DefaultsIni.Read(new byte[0]);
            if (choice.Read < 0)
            {
                log.Add(choice.Line);
                TakeSnapshot(none);
                return string.Empty;
            }

            DefaultsCandidate at = candidates[choice.Read];
#if NULLABLE_ENABLED
            byte[]? bytes = outcomes[choice.Read].Kind == DefaultsCreation.Created ? created : null;
#else
            byte[] bytes = outcomes[choice.Read].Kind == DefaultsCreation.Created ? created : null;
#endif
            if (bytes == null)
            {
                try
                {
                    bytes = ReadIfPresent(at.Path);
                }
                catch (IOException e)
                {
                    return NotRead(log, at, DefaultsWhy(e, at));
                }
                catch (UnauthorizedAccessException e)
                {
                    return NotRead(log, at, DefaultsWhy(e, at));
                }
                if (bytes == null)
                {
                    NotRead(log, at, "the file was deleted at the same time");
                    return string.Empty;
                }
            }

            _defaultsSeen = bytes;
            DefaultsIniSnapshot snapshot = DefaultsIni.Read(bytes);
            if (snapshot.Unreadable != null) return NotRead(log, at, snapshot.Unreadable);
            log.Add(choice.Line);
            if (snapshot.FormatLine != null) log.Add(snapshot.FormatLine);
            TakeSnapshot(snapshot);
            twoFiles = choice.Message;
            return string.Empty;
        }

        // A Defaults.ini found and not read: its one line, the built-in values, and the message.
        private string NotRead(List<string> log, DefaultsCandidate at, string why)
        {
            log.Add("Defaults.ini: " + DefaultsLocation.Named(at) + " cannot be read: " + why + "." + DefaultsLocation.BuiltIn);
            TakeSnapshot(DefaultsIni.Read(new byte[0]));
            return "Defaults.ini cannot be read: " + why + ". Settings that use it take the built-in values.";
        }

        // The folder by the one-level rule, then the file through the checked writer with no
        // expected bytes, so it is never written over a file that appeared meanwhile.
#if NULLABLE_ENABLED
        private DefaultsCreationOutcome CreateDefaults(DefaultsCandidate candidate, out byte[]? created)
#else
        private DefaultsCreationOutcome CreateDefaults(DefaultsCandidate candidate, out byte[] created)
#endif
        {
            created = null;
            int folder = DefaultsLocation.CreateFolder(candidate.Folder);
            if (folder == DefaultsLocation.ErrorPathNotFound) return new DefaultsCreationOutcome(DefaultsCreation.ParentMissing, string.Empty);
            if (folder != 0)
            {
                string why = folder == ErrorAccessDenied
                    ? "the folder cannot be written"
                    : "it could not be written (" + new Win32Exception(folder).Message + ")";
                return new DefaultsCreationOutcome(DefaultsCreation.FolderFailed, why);
            }

            byte[] bytes = DefaultsIni.Render();
            CheckedWriteOutcome outcome;
            try
            {
                outcome = CheckedFileWriter.Write(candidate.Path, null, bytes, WriterHook("Defaults."));
            }
            catch (CheckedWriteException e)
            {
                return new DefaultsCreationOutcome(DefaultsCreation.FileFailed, DefaultsWhy(e, candidate));
            }
            switch (outcome)
            {
                case CheckedWriteOutcome.Committed:
                    created = bytes;
                    return new DefaultsCreationOutcome(DefaultsCreation.Created, string.Empty);
                case CheckedWriteOutcome.TargetAppeared:
                    return new DefaultsCreationOutcome(DefaultsCreation.Appeared, string.Empty);
                default:
                    throw new InvalidOperationException("a write that expects no file gave " + outcome);
            }
        }

        // The values Defaults.ini gives: each accepted value of a row that follows Defaults.ini,
        // through the row's own codec and setter, over a fresh defaults instance.
        private void TakeSnapshot(DefaultsIniSnapshot snapshot)
        {
            TConfig effective = _table.CreateDefaults();
            var from = new List<ConceptDescriptor>();
            for (int i = 0; i < _table.RowCount; i++)
            {
#if NULLABLE_ENABLED
                ConceptDescriptor? concept = _table.RowConcept(i);
#else
                ConceptDescriptor concept = _table.RowConcept(i);
#endif
                if (concept == null || !_table.RowFollowsDefaultsIni(i)) continue;
                DefaultsIniValue value = snapshot.Value(concept);
                if (value.State != DefaultsIniValueState.Accepted) continue;
#if NULLABLE_ENABLED
                string? error = _table.RowApply(i, value.Value, effective);
#else
                string error = _table.RowApply(i, value.Value, effective);
#endif
                if (error != null)
                {
                    throw new InvalidOperationException(_table.RowName(i) + ": Defaults.ini's value "
                        + CodecText.Utf8Text(value.Value) + " passed the Defaults.ini reader and not the row's codec: " + error);
                }
                from.Add(concept);
            }
            _snapshot = snapshot;
            _effective = effective;
            _fromDefaultsIni = from.ToArray();
        }

        // Design 3.3: which rows came from Defaults.ini, which the file sets itself, which took the
        // built-in, and a line per refused value this game would take. Returns the in-game message
        // for those refused values, or empty.
        private string DefaultsLines(List<string> log)
        {
            if (_sources == null) return string.Empty;
            TConfig builtIn = _table.CreateDefaults();
            var taken = new List<string>();
            var own = new List<string>();
            var fallen = new List<string>();
            var refusedLines = new List<string>();
            var refused = new List<string>();
            bool pairLogged = false;
            for (int i = 0; i < _table.RowCount; i++)
            {
#if NULLABLE_ENABLED
                ConceptDescriptor? concept = _table.RowConcept(i);
#else
                ConceptDescriptor concept = _table.RowConcept(i);
#endif
                if (concept == null || !_table.RowFollowsDefaultsIni(i)) continue;
                string key = _table.RowKey(i);
                if (_sources[i] == ConfigValueSource.File)
                {
                    own.Add(key);
                    continue;
                }
                if (_sources[i] == ConfigValueSource.DefaultsIni)
                {
                    taken.Add(key + "=" + _table.RowValueText(i, _effective));
                    continue;
                }
                fallen.Add(key + "=" + _table.RowValueText(i, builtIn));
                DefaultsIniValue value = _snapshot.Value(concept);
                if (value.State != DefaultsIniValueState.Refused) continue;
                bool pair = concept == ConfigConcepts.RotationEnabled || concept == ConfigConcepts.PositionEnabled;
                if (!pair || !_snapshot.PairRefused)
                {
                    refusedLines.Add(DefaultsIni.RefusedLine(value, _table.RowValueText(i, builtIn)));
                    refused.Add(key + "=" + CodecText.Utf8Text(value.Value));
                    continue;
                }
                if (pairLogged) continue;
                pairLogged = true;
                refusedLines.Add(DefaultsIni.PairLine(_snapshot, BuiltInText(ConfigConcepts.RotationEnabled, builtIn),
                    BuiltInText(ConfigConcepts.PositionEnabled, builtIn)));
                var entries = new List<string>();
                foreach (ConceptDescriptor member in new ConceptDescriptor[] { ConfigConcepts.RotationEnabled, ConfigConcepts.PositionEnabled })
                {
                    DefaultsIniValue held = _snapshot.Value(member);
                    if (held.State == DefaultsIniValueState.Refused) entries.Add(member.Key + "=" + CodecText.Utf8Text(held.Value));
                }
                refused.Add(string.Join(" and ", entries.ToArray()));
            }

            if (taken.Count > 0) log.Add(_path + ": from Defaults.ini: " + string.Join("; ", taken.ToArray()));
            if (own.Count > 0)
            {
                log.Add(_path + ": set in this file, so Defaults.ini does not change them: " + string.Join(", ", own.ToArray()) + ".");
            }
            if (fallen.Count > 0) log.Add(_path + ": built-in, not set in Defaults.ini: " + string.Join("; ", fallen.ToArray()));
            log.AddRange(refusedLines);
            if (refused.Count == 0) return string.Empty;
            return "Defaults.ini: " + refused.Count.ToString(CultureInfo.InvariantCulture)
                + (refused.Count == 1 ? " setting cannot" : " settings cannot") + " be used (" + string.Join("; ", refused.ToArray())
                + "), so this game uses its built-in values for them. The log has the details.";
        }

        // The game's built-in value of a tracking mode row, or the schema's for one the table does
        // not bind; the two are equal for every row that follows Defaults.ini.
        private string BuiltInText(ConceptDescriptor concept, TConfig builtIn)
        {
            int row = _table.RowOf(concept);
            return row >= 0 ? _table.RowValueText(row, builtIn) : concept.DefaultText;
        }

        private ConfigLoadResult<TConfig> CannotOpen(string path, Exception e, List<string> log)
        {
            log.Add(path + ": could not be opened: " + e.Message);
            if (path == _legacySourcePath) return Defer(OnDefaults(), path, log, Why(e), true);
            return Result(ConfigLoadStatus.Deferred, OnDefaults(), NoDiagnostics(), log,
                System.IO.Path.GetFileName(path) + " cannot be read: " + Why(e)
                    + ". The mod runs on its default settings this session.");
        }

        private ConfigLoadResult<TConfig> ReadCanonical(byte[] bytes, bool stamped, List<string> log)
        {
            CanonicalIni doc = CanonicalIni.Parse(bytes);
            if (!doc.IsReadable)
            {
                string why = Unreadable(doc);
                log.Add(_path + ": cannot be read: " + why);
                return Result(ConfigLoadStatus.Unreadable, OnDefaults(), NoDiagnostics(), log,
                    _name + " cannot be read: " + why + ". The mod runs on its default settings and saves nothing until "
                        + "the file is fixed.");
            }
            TConfig config = _table.CreateDefaults();
            List<CanonicalDiagnostic> diagnostics = Apply(doc, config, log);
            if (!stamped && _writes)
            {
                log.Add(_path + ": has no [CameraUnlock] section. It is read as the canonical format, and the next save "
                    + "adds the section.");
            }
            _savesAllowed = true;
            return Result(ConfigLoadStatus.Canonical, config, diagnostics, log, string.Empty);
        }

        private ConfigLoadResult<TConfig> Create(List<string> log)
        {
            TConfig defaults = OnDefaults();
            if (!_writes) return Result(ConfigLoadStatus.Created, defaults, NoDiagnostics(), log, string.Empty);
            string why;
            try
            {
                CheckedWriteOutcome outcome = CheckedFileWriter.Write(_path, null, _freshBytes, WriterHook("Create."));
                if (outcome == CheckedWriteOutcome.Committed)
                {
                    _committed = _freshBytes;
                    _recordedWriteTime = File.GetLastWriteTimeUtc(_path);
                    _savesAllowed = true;
                    log.Add(_path + ": created with the default settings.");
                    return Result(ConfigLoadStatus.Created, defaults, NoDiagnostics(), log, string.Empty);
                }
                why = Conflict(outcome);
                log.Add(_path + ": not created: " + outcome);
            }
            catch (CheckedWriteException e)
            {
                why = Why(e);
                log.Add(_path + ": not created: " + e.Message);
            }
            return Result(ConfigLoadStatus.Deferred, defaults, NoDiagnostics(), log,
                _name + " was not created: " + why + ". The mod runs on its default settings this session.");
        }

        private ConfigLoadResult<TConfig> Migrate(Held held, string input, List<string> log)
        {
            byte[] snapshot = held.Snapshot;
            TConfig imported = _table.CreateDefaults();
            ImportResult import;
#if NULLABLE_ENABLED
            string? changedWhy = null;
#else
            string changedWhy = null;
#endif
            try
            {
                Step("Import", input);
                import = RequireImport().Run(new LegacyImportInput(input), imported);
                if (import == null) throw new InvalidOperationException("the legacy import returned no result");
                Step("Recheck", input);
                try
                {
                    if (!Same(held.Reread(), snapshot)) changedWhy = ChangedWhileRead;
                }
                catch (IOException e)
                {
                    changedWhy = Why(e);
                    log.Add(input + ": could not be read again after the import: " + e.Message);
                }
            }
            finally
            {
                held.Dispose();
            }

            if (changedWhy != null) return Defer(imported, input, log, changedWhy, true);
            switch (import.Status)
            {
                case ImportStatus.Refused:
                    log.Add(input + ": the old settings reader refused the file: " + import.Reason);
                    return Result(ConfigLoadStatus.LegacyRefused, imported, NoDiagnostics(), log,
                        NotImported(input, import.Reason, true));
                case ImportStatus.Undecodable:
                    log.Add(input + ": the old settings reader could not decode the file: " + import.Reason);
                    return Defer(imported, input, log, import.Reason, true);
                case ImportStatus.Absent:
                    log.Add(input + ": the old settings reader found no file, while the owner holds it open ("
                        + snapshot.Length.ToString(CultureInfo.InvariantCulture) + " bytes)");
                    LogDropped(import, input, log);
                    return Defer(imported, input, log, "the old settings reader could not find the file", true);
            }
            LogDropped(import, input, log);

            byte[] rendered;
            try
            {
                rendered = _table.RenderMigration(imported, _effective, _header);
            }
            catch (ArgumentException e)
            {
                int row = FirstUnwritable(imported);
                if (row < 0) throw;
                log.Add(input + ": " + _table.RowName(row) + " cannot be written in the new format: " + e.Message);
                return Defer(imported, input, log, Unconvertible(row, imported), true);
            }

            CanonicalIni doc = CanonicalIni.Parse(rendered);
            TConfig reread = _table.CreateDefaults();
            var readBack = new List<string>();
            List<CanonicalDiagnostic> diagnostics = Apply(doc, reread, readBack);
            int different = FirstDifference(imported, reread);
            if (different >= 0)
            {
                _sources = null;
                log.Add(input + ": " + _table.RowName(different) + " reads back from the new format as "
                    + _table.RowValueText(different, reread) + ", not " + _table.RowValueText(different, imported));
                return Defer(imported, input, log, Unconvertible(different, imported), true);
            }
            if (!_writes)
            {
                LogNotCarried(snapshot, input, log);
                log.AddRange(readBack);
                return Result(ConfigLoadStatus.Migrated, reread, diagnostics, log, string.Empty);
            }

            try
            {
                CheckedWriteOutcome outcome = CheckedFileWriter.Write(_path, null, rendered, WriterHook("Commit."));
                if (outcome != CheckedWriteOutcome.Committed)
                {
                    log.Add(_path + ": not created: " + outcome);
                    return Defer(imported, input, log, Conflict(outcome), false);
                }
            }
            catch (CheckedWriteException e)
            {
                log.Add(e.Message);
                // A failed RemoveTemporary means a file appeared at Path first, and the next launch
                // reads that file instead of importing.
                return Defer(imported, input, log, Why(e), e.Step != CheckedWriteStep.RemoveTemporary);
            }

            Step("Remember", _path);
            _committed = rendered;
            _recordedWriteTime = File.GetLastWriteTimeUtc(_path);
            _savesAllowed = true;
            log.Add(_path + ": created from " + input + ", which is left as it was.");
            LogNotCarried(snapshot, input, log);
            log.AddRange(readBack);
            return Result(ConfigLoadStatus.Migrated, reread, diagnostics, log, string.Empty);
        }

        private ConfigLoadResult<TConfig> Defer(TConfig config, string input, List<string> log, string why, bool retried)
        {
            return Result(ConfigLoadStatus.Deferred, config, NoDiagnostics(), log, NotImported(input, why, retried));
        }

        private string NotImported(string input, string why, bool retried)
        {
            string legacy = System.IO.Path.GetFileName(input);
            string head = legacy + " was not imported into " + _name + ": " + why;
            if (retried) return head + ". The mod tries again at the next launch and saves nothing this session.";
            return head + ". The mod saves nothing this session and reads " + _name + ", not " + legacy
                + ", at the next launch.";
        }
        private string Unconvertible(int row, TConfig config)
        {
            return _table.RowName(row) + "=" + _table.RowValueText(row, config) + " cannot be converted";
        }

        private ConfigSaveResult SaveLocked(Action<TConfig> change)
        {
            var log = new List<string>();
            if (!_writes)
            {
                log.Add(_path + ": not saved: " + SavesOnlyOnWindows);
                return NotSaved(SavesOnlyOnWindows, null, log);
            }
            if (!_savesAllowed)
            {
                log.Add(_path + ": not saved: the file was not loaded this session");
                return NotSaved("the settings file could not be used this session", null, log);
            }

            byte[] snapshot;
            try
            {
#if NULLABLE_ENABLED
                byte[]? read = ReadIfPresent(_path);
#else
                byte[] read = ReadIfPresent(_path);
#endif
                if (read == null)
                {
                    log.Add(_path + ": not saved: the file is missing");
                    return NotSaved("the settings file is missing; it is created again at the next launch", null, log);
                }
                snapshot = read;
            }
            catch (IOException e)
            {
                log.Add(_path + ": not saved: could not be read: " + e.Message);
                return NotSaved(Why(e), e, log);
            }
            catch (UnauthorizedAccessException e)
            {
                log.Add(_path + ": not saved: could not be read: " + e.Message);
                return NotSaved(Why(e), e, log);
            }

            CanonicalIni doc = CanonicalIni.Parse(snapshot);
            if (!doc.IsReadable)
            {
                string why = Unreadable(doc);
                log.Add(_path + ": not saved: " + why);
                return NotSaved(_name + " cannot be read: " + why, null, log);
            }
            if (doc.FormatVersion > CanonicalIni.ConfigFormat)
            {
                log.Add(_path + ": not saved: ConfigFormat " + doc.FormatVersion.ToString(CultureInfo.InvariantCulture)
                    + " is newer than this build's " + CanonicalIni.ConfigFormat.ToString(CultureInfo.InvariantCulture));
                return NotSaved(_name + " was written by a newer version of the mod", null, log);
            }
            bool stamped = CanonicalIni.HasStamp(snapshot);

            TConfig baseline = _table.CreateDefaults();
            ConfigValueSource[] baselineSources = _table.Apply(doc, baseline, _effective, _fromDefaultsIni).Sources;
            TConfig changed = _table.CreateDefaults();
            _table.Apply(doc, changed, _effective, _fromDefaultsIni);
            change(changed);

            int count = _table.RowCount;
            var edited = new bool[count];
            bool any = false;
            for (int i = 0; i < count; i++)
            {
                if (_table.RowEqual(i, baseline, changed)) continue;
                if (!_table.RowWritable(i))
                {
                    throw new InvalidOperationException(_table.RowName(i)
                        + " changed, but the table does not mark it Writable, so Save may not write it");
                }
                edited[i] = true;
                any = true;
            }
            if (!any) return new ConfigSaveResult(ConfigSaveStatus.Saved, string.Empty, null, null, log);

            int rotation = _table.RowOf(ConfigConcepts.RotationEnabled);
            int position = _table.RowOf(ConfigConcepts.PositionEnabled);
            if (rotation >= 0 && position >= 0 && (edited[rotation] || edited[position]))
            {
                edited[rotation] = true;
                edited[position] = true;
            }

            var edits = new List<IniEdit>();
            for (int i = 0; i < count; i++)
            {
                if (!edited[i]) continue;
                edits.Add(new IniEdit(_table.RowSection(i), _table.RowKey(i),
                    Encoding.UTF8.GetString(_table.RowRender(i, changed)), true));
            }
            if (!stamped || FormatUnread(doc))
            {
                edits.Add(new IniEdit(StampSection, CanonicalIni.FormatKeyText,
                    CanonicalIni.ConfigFormat.ToString(CultureInfo.InvariantCulture), true));
            }
            byte[] candidate = IniEditor.Edit(snapshot, edits).Bytes;

            TConfig written = _table.CreateDefaults();
            ConfigValueSource[] writtenSources = _table.Apply(CanonicalIni.Parse(candidate), written, _effective, _fromDefaultsIni).Sources;
            for (int i = 0; i < count; i++)
            {
                if (_table.RowEqual(i, written, edited[i] ? changed : baseline)
                    && (edited[i] || writtenSources[i] == baselineSources[i]))
                {
                    continue;
                }
                log.Add(_path + ": not saved: " + _table.RowName(i) + " would read back as "
                    + _table.RowValueText(i, written) + " from " + SourceName(writtenSources[i]) + ", not "
                    + _table.RowValueText(i, edited[i] ? changed : baseline) + " from " + SourceName(edited[i] ? ConfigValueSource.File : baselineSources[i]));
                return NotSaved(_table.RowName(i) + "=" + _table.RowValueText(i, changed) + " does not read back from "
                    + _name, null, log);
            }

            try
            {
                CheckedWriteOutcome outcome = CheckedFileWriter.Write(_path, snapshot, candidate, WriterHook("Save."));
                if (outcome != CheckedWriteOutcome.Committed)
                {
                    log.Add(_path + ": not saved: " + outcome);
                    return NotSaved(Conflict(outcome), null, log);
                }
            }
            catch (CheckedWriteException e)
            {
                log.Add(e.Message);
                if (e.OutcomeUncertain)
                {
                    return new ConfigSaveResult(ConfigSaveStatus.Uncertain, "Settings may not be saved: " + Why(e) + ".", e,
                        e.TemporaryPath, log);
                }
                return NotSaved(Why(e), e, log);
            }
            _committed = candidate;
            _recordedWriteTime = File.GetLastWriteTimeUtc(_path);
            for (int i = 0; i < count; i++)
            {
                if (!edited[i] || !_table.RowFollowsDefaultsIni(i) || baselineSources[i] == ConfigValueSource.File) continue;
                log.Add(_path + ": " + _table.RowKey(i) + "=" + _table.RowValueText(i, changed)
                    + " is now set for this game, and no longer follows Defaults.ini.");
            }
            return new ConfigSaveResult(ConfigSaveStatus.Saved, string.Empty, null, null, log);
        }

        private static string SourceName(ConfigValueSource source)
        {
            switch (source)
            {
                case ConfigValueSource.File:
                    return "the file";
                case ConfigValueSource.DefaultsIni:
                    return "Defaults.ini";
                default:
                    return "the built-in value";
            }
        }

#if NULLABLE_ENABLED
        private static ConfigSaveResult NotSaved(string why, Exception? error, List<string> log)
#else
        private static ConfigSaveResult NotSaved(string why, Exception error, List<string> log)
#endif
        {
            return new ConfigSaveResult(ConfigSaveStatus.NotSaved, "Settings not saved: " + why + ".", error, null, log);
        }

        private ConfigReloadResult<TConfig> ReloadLocked(out string defaultsMessage)
        {
            var log = new List<string>();
            _sources = null;
            DateTime writeTime = File.GetLastWriteTimeUtc(_path);
            _defaultsWriteTime = DefaultsWriteTime();
            bool snapshotChanged;
            string unreadable = ReloadDefaults(log, out snapshotChanged);
            ConfigReloadResult<TConfig> reloaded = ReloadConfigFile(log, writeTime, snapshotChanged);
            var lines = new List<string>(reloaded.Log);
            string refused = DefaultsLines(lines);
            defaultsMessage = unreadable.Length > 0 ? unreadable : snapshotChanged ? refused : string.Empty;
            return Reloaded(reloaded.Status, reloaded.Config, new List<CanonicalDiagnostic>(reloaded.Diagnostics), lines,
                reloaded.Reason);
        }

        private ConfigReloadResult<TConfig> ReloadConfigFile(List<string> log, DateTime writeTime, bool snapshotChanged)
        {
            byte[] bytes;
            try
            {
#if NULLABLE_ENABLED
                byte[]? read = ReadIfPresent(_path);
#else
                byte[] read = ReadIfPresent(_path);
#endif
                if (read == null)
                {
                    _recordedWriteTime = writeTime;
                    log.Add(_path + ": not reloaded: the file is missing");
                    return Reloaded(ConfigReloadStatus.Unreadable, null, NoDiagnostics(), log,
                        _name + " is missing, so the current settings stay. It is created again at the next launch.");
                }
                bytes = read;
            }
            catch (IOException e)
            {
                log.Add(_path + ": not reloaded: " + e.Message);
                return Reloaded(ConfigReloadStatus.Unreadable, null, NoDiagnostics(), log,
                    _name + " cannot be read: " + Why(e) + ". The current settings stay.");
            }
            catch (UnauthorizedAccessException e)
            {
                log.Add(_path + ": not reloaded: " + e.Message);
                return Reloaded(ConfigReloadStatus.Unreadable, null, NoDiagnostics(), log,
                    _name + " cannot be read: " + Why(e) + ". The current settings stay.");
            }
            _recordedWriteTime = writeTime;

            if (_committed != null && Same(bytes, _committed) && !snapshotChanged)
            {
                return Reloaded(ConfigReloadStatus.Unchanged, null, NoDiagnostics(), log, string.Empty);
            }

            CanonicalIni doc = CanonicalIni.Parse(bytes);
            if (!doc.IsReadable)
            {
                string why = Unreadable(doc);
                log.Add(_path + ": not reloaded: " + why);
                return Reloaded(ConfigReloadStatus.Unreadable, null, NoDiagnostics(), log,
                    _name + " cannot be read: " + why + ". The current settings stay.");
            }
            TConfig config = _table.CreateDefaults();
            List<CanonicalDiagnostic> diagnostics = Apply(doc, config, log);
            if (_committed != null && !Same(bytes, _committed)) _committed = null;
            _savesAllowed = true;
            return Reloaded(ConfigReloadStatus.Applied, config, diagnostics, log, string.Empty);
        }

        // Defaults.ini again, where Load found it. Returns the message for one that went missing or
        // cannot be read, which keeps the values it gave, or empty.
        private string ReloadDefaults(List<string> log, out bool changed)
        {
            changed = false;
            if (_defaultsAt == null) return string.Empty;
            string named = DefaultsLocation.Named(_defaultsAt);
#if NULLABLE_ENABLED
            byte[]? bytes;
#else
            byte[] bytes;
#endif
            try
            {
                bytes = ReadIfPresent(_defaultsAt.Path);
            }
            catch (IOException e)
            {
                return KeptValues(log, named, DefaultsWhy(e, _defaultsAt));
            }
            catch (UnauthorizedAccessException e)
            {
                return KeptValues(log, named, DefaultsWhy(e, _defaultsAt));
            }
            if (bytes == null)
            {
                if (_defaultsSeen == null) return string.Empty;
                _defaultsSeen = null;
                log.Add("Defaults.ini: " + named + " is missing." + KeptUntilRestart);
                return "Defaults.ini is missing." + KeptUntilRestart;
            }
            if (_defaultsSeen != null && Same(bytes, _defaultsSeen)) return string.Empty;
            _defaultsSeen = bytes;
            DefaultsIniSnapshot snapshot = DefaultsIni.Read(bytes);
            if (snapshot.Unreadable != null) return KeptValues(log, named, snapshot.Unreadable);
            log.Add("Defaults.ini: " + named + " (read)");
            if (snapshot.FormatLine != null) log.Add(snapshot.FormatLine);
            TakeSnapshot(snapshot);
            changed = true;
            return string.Empty;
        }

        private static string KeptValues(List<string> log, string named, string why)
        {
            log.Add("Defaults.ini: " + named + " cannot be read: " + why + "." + KeptUntilRestart);
            return "Defaults.ini cannot be read: " + why + "." + KeptUntilRestart;
        }

        private List<CanonicalDiagnostic> Apply(CanonicalIni doc, TConfig config, List<string> log)
        {
            var diagnostics = new List<CanonicalDiagnostic>(doc.Diagnostics);
            TableApplyResult applied = _table.Apply(doc, config, _effective, _fromDefaultsIni);
            diagnostics.AddRange(applied.Report.Diagnostics);
            _sources = applied.Sources;
            foreach (CanonicalDiagnostic diagnostic in diagnostics) log.Add(_path + ": " + diagnostic.Describe());
            return diagnostics;
        }

        // The defaults the session runs on where no file is read: Defaults.ini's values on the rows
        // that follow it, the table's on the rest.
        private TConfig OnDefaults()
        {
            TConfig config = _table.CreateDefaults();
            _sources = _table.Apply(CanonicalIni.Parse(new byte[0]), config, _effective, _fromDefaultsIni).Sources;
            return config;
        }

        private DateTime DefaultsWriteTime()
        {
            if (_defaultsAt == null) return DateTime.MinValue;
            try
            {
                return File.GetLastWriteTimeUtc(_defaultsAt.Path);
            }
            catch (IOException)
            {
                return DateTime.MaxValue;
            }
            catch (UnauthorizedAccessException)
            {
                return DateTime.MaxValue;
            }
        }

        private void LogDropped(ImportResult import, string input, List<string> log)
        {
            foreach (DroppedValue dropped in import.Dropped) log.Add(input + ": " + dropped.Describe());
        }

        // 4.3 step 5: every key line of the legacy file that the frozen reader does not read.
        private void LogNotCarried(byte[] snapshot, string input, List<string> log)
        {
            if (CanonicalIni.StartsWithUtf16Mark(snapshot))
            {
                log.Add(input + ": is saved as UTF-16, so its lines this build does not read are not listed; the original "
                    + "keeps them.");
                return;
            }
            foreach (CanonicalIni.KeyLine line in CanonicalIni.KeyLines(snapshot))
            {
                if (IsImported(line)) continue;
                string section = line.Section == null ? string.Empty : "[" + Encoding.UTF8.GetString(line.Section) + "] ";
                log.Add(input + ": not carried: " + section + Encoding.UTF8.GetString(line.Key) + "="
                    + Encoding.UTF8.GetString(line.Value) + " on line " + line.Line.ToString(CultureInfo.InvariantCulture)
                    + ", this build does not read it");
            }
        }

        private bool IsImported(CanonicalIni.KeyLine line)
        {
            for (int i = 0; i < _importKeys.Length; i++)
            {
                if (!CanonicalIni.EqualsAsciiIgnoreCase(line.Key, _importKeys[i])) continue;
                byte[] section = _importSections[i];
                if (section.Length == 0) return true;
                if (line.Section != null && CanonicalIni.EqualsAsciiIgnoreCase(line.Section, section)) return true;
            }
            return false;
        }

        private int FirstUnwritable(TConfig config)
        {
            for (int i = 0; i < _table.RowCount; i++)
            {
                try
                {
                    _table.RowRender(i, config);
                }
                catch (ArgumentException)
                {
                    return i;
                }
            }
            return -1;
        }

        private int FirstDifference(TConfig a, TConfig b)
        {
            for (int i = 0; i < _table.RowCount; i++)
            {
                if (!_table.RowEqual(i, a, b)) return i;
            }
            return -1;
        }

        private LegacyImport<TConfig> RequireImport()
        {
            if (_import == null) throw new InvalidOperationException("the owner has no legacy import");
            return _import;
        }

        private void RequireLoaded(string operation)
        {
            if (!_loaded) throw new InvalidOperationException(operation + " needs Load to have run first");
        }

        private void Step(string label, string path)
        {
            if (_beforeStep != null) _beforeStep(label, path);
        }

#if NULLABLE_ENABLED
        private Action<CheckedWriteStep, string>? WriterHook(string prefix)
#else
        private Action<CheckedWriteStep, string> WriterHook(string prefix)
#endif
        {
            if (_beforeStep == null) return null;
            Action<string, string> hook = _beforeStep;
            return (step, path) => hook(prefix + step, path);
        }

        private static string Unreadable(CanonicalIni doc)
        {
            if (doc.Status == CanonicalReadStatus.Utf16) return "it is saved as UTF-16; save it as ANSI or UTF-8";
            return "line " + doc.UnreadableLine.ToString(CultureInfo.InvariantCulture) + " holds a NUL byte";
        }

        // Design 1.8: a stamp with no ConfigFormat, or one that is not a number, is read as the
        // current format, and the next save writes the number.
        private static bool FormatUnread(CanonicalIni doc)
        {
            foreach (CanonicalDiagnostic diagnostic in doc.Diagnostics)
            {
                if (diagnostic.Kind == CanonicalDiagnosticKind.ConfigFormatMissing
                    || diagnostic.Kind == CanonicalDiagnosticKind.ConfigFormatInvalid)
                {
                    return true;
                }
            }
            return false;
        }

        private static string Conflict(CheckedWriteOutcome outcome)
        {
            switch (outcome)
            {
                case CheckedWriteOutcome.TargetAppeared:
                    return "another program created the file at the same time";
                case CheckedWriteOutcome.TargetMissing:
                    return "the file was deleted at the same time";
                default:
                    return "the file was changed by another program at the same time";
            }
        }

        // What the player is told about an I/O error, in the words of design 4.7. Off Windows the
        // read-only attribute is not asked about, since that asks kernel32.
        private string Why(Exception error)
        {
            var failed = error as CheckedWriteException;
            if (failed != null && failed.OutcomeUncertain)
            {
                return "Windows did not finish replacing " + failed.TargetPath + ", so it may be missing; the new settings are in "
                    + failed.TemporaryPath;
            }
            Exception cause = failed != null && failed.InnerException != null ? failed.InnerException : error;
            int native = cause is Win32Exception ? ((Win32Exception)cause).NativeErrorCode : 0;
            int hresult = Marshal.GetHRForException(cause);
            if (native == ErrorSharingViolation || native == ErrorLockViolation || hresult == HResultSharingViolation
                || hresult == HResultLockViolation)
            {
                return "the file is in use by another program";
            }
            bool denied = cause is UnauthorizedAccessException || native == ErrorAccessDenied || hresult == HResultAccessDenied;
            if (denied && failed != null && failed.Step == CheckedWriteStep.CreateTemporary) return "the folder cannot be written";
            if (denied && failed != null && _writes && IsReadOnly(failed.TargetPath)) return "the file is read-only";
            return (failed != null ? "it could not be written (" : "it could not be read (") + cause.Message + ")";
        }

        // Why, with the Defaults.ini candidate's paths in the form its lines show them, so a system
        // message that names a path carries no account name.
        private string DefaultsWhy(Exception error, DefaultsCandidate candidate)
        {
            string why = Why(error);
            foreach (KeyValuePair<string, string> path in new[]
            {
                new KeyValuePair<string, string>(candidate.Path, candidate.Shown),
                new KeyValuePair<string, string>(candidate.Folder, candidate.ShownFolder),
                new KeyValuePair<string, string>(candidate.Parent, candidate.ShownParent),
            })
            {
                if (path.Key.Length > 0) why = why.Replace(path.Key, path.Value);
            }
            return why;
        }

        private static bool IsReadOnly(string path)
        {
            uint attributes = CheckedFileWriter.GetFileAttributesW(path);
            return attributes != InvalidFileAttributes && (attributes & FileAttributeReadOnly) != 0;
        }

        // A file or a folder of that name: either one is there for the choice, and a folder then
        // fails to read with its reason.
        private static bool Exists(string path)
        {
            return File.Exists(path) || Directory.Exists(path);
        }

        private static string AbsolutePath(
#if NULLABLE_ENABLED
            string? path,
#else
            string path,
#endif
            string option)
        {
            if (path == null || path.Length == 0) throw new ArgumentException("the options name no " + option, "options");
            if (!System.IO.Path.IsPathRooted(path))
            {
                throw new ArgumentException(option + " '" + path + "' is not an absolute path", "options");
            }
            return System.IO.Path.GetFullPath(path);
        }

#if NULLABLE_ENABLED
        private static byte[]? ReadIfPresent(string path)
#else
        private static byte[] ReadIfPresent(string path)
#endif
        {
            FileStream stream;
            try
            {
                stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            }
            catch (FileNotFoundException)
            {
                return null;
            }
            catch (DirectoryNotFoundException)
            {
                return null;
            }
            using (stream)
            {
                return ReadAll(stream);
            }
        }

        private static byte[] ReadAll(Stream stream)
        {
            using (var bytes = new MemoryStream())
            {
                var buffer = new byte[4096];
                int read;
                while ((read = stream.Read(buffer, 0, buffer.Length)) > 0) bytes.Write(buffer, 0, read);
                return bytes.ToArray();
            }
        }

        private static bool Same(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (a[i] != b[i]) return false;
            }
            return true;
        }

        private static List<CanonicalDiagnostic> NoDiagnostics()
        {
            return new List<CanonicalDiagnostic>();
        }

        private static ConfigLoadResult<TConfig> Result(ConfigLoadStatus status, TConfig config,
            List<CanonicalDiagnostic> diagnostics, List<string> log, string reason)
        {
            return new ConfigLoadResult<TConfig>(status, config, diagnostics, log, reason);
        }

        private static ConfigReloadResult<TConfig> Reloaded(ConfigReloadStatus status,
#if NULLABLE_ENABLED
            TConfig? config,
#else
            TConfig config,
#endif
            List<CanonicalDiagnostic> diagnostics, List<string> log, string reason)
        {
            return new ConfigReloadResult<TConfig>(status, config, diagnostics, log, reason);
        }

        // Design 4.5 step 1: the file held open for reading, sharing read and write but not delete,
        // from the snapshot to the re-read, so no program can newly open it denying read sharing,
        // rename it or delete it while the import reads it.
        private sealed class Held : IDisposable
        {
            private readonly FileStream _stream;

            private Held(FileStream stream)
            {
                _stream = stream;
                Snapshot = ReadAll(stream);
            }

            public byte[] Snapshot { get; }

#if NULLABLE_ENABLED
            public static Held? Open(string path)
#else
            public static Held Open(string path)
#endif
            {
                FileStream stream;
                try
                {
                    stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                }
                catch (FileNotFoundException)
                {
                    return null;
                }
                catch (DirectoryNotFoundException)
                {
                    return null;
                }
                try
                {
                    return new Held(stream);
                }
                catch
                {
                    stream.Dispose();
                    throw;
                }
            }

            public byte[] Reread()
            {
                _stream.Seek(0, SeekOrigin.Begin);
                return ReadAll(_stream);
            }

            public void Dispose()
            {
                _stream.Dispose();
            }
        }
    }
}
