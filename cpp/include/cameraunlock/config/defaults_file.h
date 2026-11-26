#pragma once

#include <cameraunlock/config/defaults_location.h>

#include <optional>
#include <string>

namespace cameraunlock::config {

class DefaultsFile;

namespace detail {

// Not API. The resolver's input given, not probed, so a test can stand for any machine.
DefaultsFile DefaultsFileFromProbe(DefaultsProbe probe);
// False for a default-constructed DefaultsFile.
bool DefaultsFileIsSet(const DefaultsFile& file);
// The candidates: an At() path as one candidate that may be created, a given probe resolved, or
// PerUser() probed on this machine (Windows only, in config_owner.cpp). Throws std::invalid_argument
// for a DefaultsFile that names no file.
DefaultsResolution ResolveDefaultsFile(const DefaultsFile& file);

}  // namespace detail

/// Where a ConfigOwner finds Defaults.ini, the file every global concept row not marked PerGame
/// takes its default from. A mod passes PerUser(); a test passes At() with a scratch path, so no test reads
/// or creates the player's own file. A default-constructed DefaultsFile names no file, and an owner
/// or a PluginMod given one throws. CameraUnlock.Core.Config.DefaultsFile is the C# twin.
class DefaultsFile {
public:
    DefaultsFile() = default;

    /// The player's own Defaults.ini: %AppData%\CameraUnlock\Defaults.ini, found through the
    /// roaming AppData known folder; under Wine or Proton the host's config folder where Wine maps
    /// it, else the prefix's AppData. Load creates it where none exists, except in a packaged app.
    static DefaultsFile PerUser();

    /// Defaults.ini at `path`, a fully qualified path (`C:\...` or `\\server\...`) used as it is, for
    /// a test. Load creates it when it is absent as it would the player's own: its folder one level
    /// only, so the folder's parent must exist, then the file with the built-in values, never over a
    /// file that appears meanwhile. The log shows the path as it is. Throws std::invalid_argument
    /// for an empty path or one that is not fully qualified.
    static DefaultsFile At(std::wstring path);

private:
    enum class Kind { kUnset, kPerUser, kAt, kProbed };

    friend DefaultsFile detail::DefaultsFileFromProbe(detail::DefaultsProbe probe);
    friend bool detail::DefaultsFileIsSet(const DefaultsFile& file);
    friend detail::DefaultsResolution detail::ResolveDefaultsFile(const DefaultsFile& file);

    Kind kind_ = Kind::kUnset;
    std::wstring path_;
    std::optional<detail::DefaultsProbe> probe_;
};

}  // namespace cameraunlock::config
