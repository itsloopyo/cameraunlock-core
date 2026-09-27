#pragma once

#include <cameraunlock/config/config_concepts.g.h>

#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// What a game's legacy import hands the migration driver. The import is the game's own reader
// as its last pre-canonical build ran it, frozen in the game's repo with a copy of that build's
// Config struct and defaults, plus a map from that struct into the runtime Config. ConfigOwner
// runs it on the game's legacy file while no config file exists, to create one, and it writes
// nothing. Pure, no <windows.h>. The C# twins are in CameraUnlock.Core.Config.

namespace cameraunlock::config {

/// What an import established. The numbers match CameraUnlock.Core.Config.ImportStatus.
enum class ImportStatus {
    /// The frozen reader read the file and the map filled the Config.
    Imported = 0,
    /// The frozen reader refuses the file, as the published build did, e.g. a value outside
    /// the range that build accepted. The session runs as that build ran on a refusal, the
    /// file stays as it is, and the next launch tries again.
    Refused = 1,
    /// The frozen reader opened the file and could not decode it. The session runs on the
    /// defaults that build ran on, and the file stays as it is.
    Undecodable = 2,
    /// The frozen reader found no file, although the driver holds it open. The map filled the
    /// Config from what that build ran on without a file.
    Absent = 3,
};

/// The approved rule by which a map left a legacy value out of the migrated file. The numbers
/// match CameraUnlock.Core.Config.DropRule.
enum class DropRule {
    /// N2: a non-finite float or double imports as the row's default (LegacyFiniteOrDefault).
    NonFiniteNumber = 1,
    /// A sensitivity, unit scale, deadzone, response curve or axis inversion the player set away
    /// from the shipped default. The tracker shapes the pose; the mod no longer does. A shipped
    /// unit scale, and any other shipped default that is not identity, moves into the mod's axis
    /// conversion instead and is not dropped.
    PoseShaping = 2,
    /// A reticle setting: mods no longer draw or toggle a reticle.
    Reticle = 3,
    /// A feature that shipped disabled pending verification now follows the mod's default. The
    /// map records one only where the legacy value differs from that default.
    FollowsDefault = 4,
    /// N1: a hotkey code outside 0x01-0xFE imports as unbound (LegacyVirtualKeyToBindings).
    KeyCodeOutOfRange = 5,
    /// N3: a hotkey bound to a Ctrl, Shift or Alt key on its own imports as unbound
    /// (LegacyVirtualKeyToBindings).
    ModifierKey = 6,
    /// An aim decoupling switch set to false, which ran coupled aim. Aim is always decoupled now,
    /// so the setting has no row. The map records one only where the legacy value is false; a
    /// true value changes nothing.
    CoupledAim = 7,
    /// A position switch set to false, one that also kept the mode hotkey off the position modes
    /// (the setting PositionAllowed was added for). The tracking mode is the only way position is
    /// switched off now, so the map writes the rotation-only mode (RotationEnabled true,
    /// PositionEnabled false) and records the switch (LegacyPositionSwitch). A true value changes
    /// nothing and is not recorded.
    PositionSwitchOff = 8,
    /// A neck pivot distance (TrackerPivotForward, TrackerPivotUp or another spelling) the player
    /// changed from the value the game shipped. The tracker is authoritative, so the setting has
    /// no row (LegacyTrackerPivot). A value equal to the shipped one is not recorded.
    TrackerPivot = 9,
};

/// One legacy value the map did not carry, for the migration log.
struct DroppedValue {
    DropRule rule = DropRule::NonFiniteNumber;
    std::string section;
    std::string key;
    /// The value as the import read it, e.g. "nan".
    std::string value;
};

/// The line the migration logs for a dropped value, e.g.
/// `not carried: [Smoothing] RemoteSmoothing=nan, it is not a finite number, so the default is
/// used`. Throws std::invalid_argument for a rule outside DropRule.
std::string DescribeDroppedValue(const DroppedValue& dropped);

/// The effective legacy value of one pose-shaping setting the frozen reader read: a sensitivity,
/// unit scale, deadzone, response curve or axis inversion, which the canonical format has no row
/// for. A differential test reads these to check the conversion: where `folded` is true, the mod's
/// own axis code now does what `shipped` did.
struct PoseShapingValue {
    std::string section;
    std::string key;
    /// The value the import read, written as the canonical codecs write one ("true", "1.0",
    /// "0.5"), or "nan", "inf" or "-inf".
    std::string value;
    /// The value the game shipped, written the same way.
    std::string shipped;
    /// True when the value equals the shipped one, compared as numbers, so the conversion folds
    /// it into the mod's axis code; false when the player changed it, and it is also dropped.
    bool folded = false;
};

/// What an import returns. Build it with the factories, which hold each status to its fields.
struct ImportResult {
    ImportStatus status = ImportStatus::Imported;
    /// For Refused and Undecodable, what the player is told; empty otherwise.
    std::string reason;
    /// For Imported and Absent, the values the map dropped, in the order it met them; empty
    /// otherwise.
    std::vector<DroppedValue> dropped;
    /// For Imported and Absent, every pose-shaping setting the frozen reader read, in the order
    /// the map met them (LegacyPoseShaping); empty otherwise.
    std::vector<PoseShapingValue> pose_shaping;
    /// For Imported and Absent, the concepts the map leaves to Defaults.ini because the player
    /// never changed them from what the legacy build shipped, which LegacyFollowsDefaultsIni
    /// collects; empty otherwise. The migration gives each the value `default` gives it at that
    /// start and writes it `default`, so it follows Defaults.ini from then on. RotationEnabled or
    /// PositionEnabled names the tracking mode, and every row of the pair the table has takes it.
    /// Each must be a row of the table that follows Defaults.ini, or the migration throws. The map
    /// still sets the field, to the value it holds when the import runs alone.
    std::vector<schema::Concept> follows_defaults_ini;

    static ImportResult Imported(std::vector<DroppedValue> dropped, std::vector<PoseShapingValue> pose_shaping = {},
                                 std::vector<schema::Concept> follows_defaults_ini = {}) {
        return ImportResult{ImportStatus::Imported, {}, std::move(dropped), std::move(pose_shaping),
                            std::move(follows_defaults_ini)};
    }
    static ImportResult Absent(std::vector<DroppedValue> dropped, std::vector<PoseShapingValue> pose_shaping = {},
                               std::vector<schema::Concept> follows_defaults_ini = {}) {
        return ImportResult{ImportStatus::Absent, {}, std::move(dropped), std::move(pose_shaping),
                            std::move(follows_defaults_ini)};
    }
    /// Throws std::invalid_argument for an empty reason.
    static ImportResult Refused(std::string reason) { return WithReason(ImportStatus::Refused, std::move(reason)); }
    /// Throws std::invalid_argument for an empty reason.
    static ImportResult Undecodable(std::string reason) {
        return WithReason(ImportStatus::Undecodable, std::move(reason));
    }

private:
    static ImportResult WithReason(ImportStatus status, std::string reason) {
        if (reason.empty()) throw std::invalid_argument("a refused or undecodable import needs a reason");
        return ImportResult{status, std::move(reason), {}, {}, {}};
    }
};

/// The legacy file's path in the two forms an import may open it by, filled by the migration
/// driver.
struct LegacyInput {
    std::wstring path;
    /// `path` in the ANSI code page, for an import that calls an A function such as
    /// GetPrivateProfileStringA, as the published build did.
    std::string ansi_path;
    /// True when `path` has a character the ANSI code page cannot hold, so `ansi_path` names
    /// another file or none, and the published build never read the user's file.
    bool ansi_lossy = false;
};

/// A key the frozen reader reads. Section and key compare ASCII case-insensitively; an empty
/// section means every section, for a reader that ignores sections.
struct LegacyKey {
    std::string section;
    std::string key;
};

/// A game's legacy import, as the migration driver takes it.
template <class Config>
struct LegacyImport {
    /// Runs the frozen reader on the file through the file API the published build used, into
    /// the frozen Config struct from its own defaults, then maps that into `out`. Writes
    /// nothing. An empty function means the game has no import.
    std::function<ImportResult(const LegacyInput& input, Config& out)> run;
    /// Every key the frozen reader reads, reads outside the reader included. The driver logs
    /// each other key line of the legacy file as not carried, and the differential corpus
    /// (testing::GenerateIniMutations) takes the same list and mutates each one.
    std::vector<LegacyKey> keys;
};

/// Normalisations N1 and N3: a legacy hotkey code as a hotkey value. A code from 0x01 to 0xFE
/// gives its key name, or `0x` and hex for a code the key table does not name. Any other code
/// gives "", unbound (N1). `pixi run probe-n1` shows GetAsyncKeyState reporting none of them for a
/// key held in range, so such a hotkey never fired on one. The exception is 0xFF: kbd.h gives it
/// to the scan codes a layout leaves unmapped (VK__none_), and GetAsyncKeyState(0xFF) reports down
/// while that code is held, so a legacy 0xFF hotkey could fire, and N1 unbinds it (approved
/// 2026-09-25). A Ctrl, Shift or Alt key (0x10-0x12, 0xA0-0xA5) gives "" too (N3, approved
/// 2026-09-26): no hotkey value binds one, because it goes down before the key of any chord made
/// with it, so a binding on it fires on the way into every such chord. A map folding a
/// legacy chord switch into the same action appends the chord to what this gives, so the player
/// keeps the chord when the code is unbound.
std::string LegacyVirtualKeyToBindings(long long code);

/// LegacyVirtualKeyToBindings, recording the drop in `dropped` under `section` and `key` when
/// the code is outside 0x01-0xFE (DropRule::KeyCodeOutOfRange) or a Ctrl, Shift or Alt key
/// (DropRule::ModifierKey). Code 0 is recorded as nothing: it is how a legacy file says unbound,
/// and it stays unbound.
std::string LegacyVirtualKeyToBindings(long long code, const std::string& section, const std::string& key,
                                       std::vector<DroppedValue>& dropped);

/// Normalisation N2: a legacy float or double that is not finite imports as `row_default`,
/// the runtime row's default, and the drop is recorded in `dropped` under `section` and
/// `key`. A finite value is returned as it is. Throws std::invalid_argument when
/// `row_default` is not finite.
template <class F>
F LegacyFiniteOrDefault(F value, F row_default, const std::string& section, const std::string& key,
                        std::vector<DroppedValue>& dropped) {
    static_assert(std::is_same_v<F, float> || std::is_same_v<F, double>, "N2 applies to float and double");
    if (!std::isfinite(row_default)) {
        throw std::invalid_argument("[" + section + "] " + key + ": the row default is not finite");
    }
    if (std::isfinite(value)) return value;
    dropped.push_back({DropRule::NonFiniteNumber, section, key, std::isnan(value) ? "nan" : value > 0 ? "inf" : "-inf"});
    return row_default;
}

/// A pose-shaping setting the frozen reader read (approved change pose_shaping): records
/// `value`, its effective legacy value, beside `shipped`, the value the game shipped, in
/// `pose_shaping`. Equal, the conversion folds the shipped value into the mod's own axis code
/// and nothing is dropped. Different, the player set it, and it is also recorded in `dropped` as
/// DropRule::PoseShaping. The map sets no runtime field from it. Throws std::invalid_argument
/// when a float or double `shipped` is not finite.
void LegacyPoseShaping(bool value, bool shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped);
void LegacyPoseShaping(float value, float shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped);
void LegacyPoseShaping(double value, double shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped);

/// A legacy position switch that also kept the mode hotkey off the position modes (approved change
/// position_switch_off, the setting PositionAllowed stood for). Call it after the map has written
/// the tracking mode. For a `value` of false it writes the rotation-only mode (`rotation_enabled`
/// true, `position_enabled` false), whatever the map wrote there, and records the switch in
/// `dropped` as DropRule::PositionSwitchOff. True changes nothing and records nothing.
void LegacyPositionSwitch(bool value, const std::string& section, const std::string& key, bool& rotation_enabled,
                          bool& position_enabled, std::vector<DroppedValue>& dropped);

/// A legacy neck pivot distance (approved change tracker_pivot): TrackerPivotForward,
/// TrackerPivotUp or another spelling of either, which the canonical format has no row for.
/// `value` is the effective legacy pivot and `shipped` the effective pivot the game shipped, both
/// the distance the pipeline ran on, so 0 where the game's own switch had compensation off. Equal,
/// the player never changed it and nothing is recorded. Different, it is recorded in `dropped` as
/// DropRule::TrackerPivot. The map sets no runtime field from it. Throws std::invalid_argument
/// when `shipped` is not finite.
void LegacyTrackerPivot(float value, float shipped, const std::string& section, const std::string& key,
                        std::vector<DroppedValue>& dropped);

/// The rows a legacy import leaves to Defaults.ini (owner rule of 2026-09-26). A setting the player
/// never changed from what the legacy build shipped, because the legacy file does not hold it or
/// holds the shipped value, is no player's choice, so the migration writes it `default` and it
/// follows Defaults.ini. The legacy build is the published build that wrote the file, an older one
/// included, where the file shows which (owner ruling of 2026-09-27). A setting the player changed is carried as a value, written `default`
/// only where it equals what `default` gives at that start. Every legacy import gives each row of
/// its table that follows Defaults.ini (a global concept row not marked PerGame) one call, and
/// passes Concepts() to ImportResult::Imported or Absent as follows_defaults_ini. The map still
/// sets every field from the legacy value.
class LegacyFollowsDefaultsIni {
public:
    /// A setting the legacy build read: left to Defaults.ini when `value`, the effective legacy
    /// value, equals `shipped`, compared with ==. `shipped` is the effective value the published
    /// build that wrote the file shipped, which is not always the newest build's: where the default
    /// changed between published builds and the file shows which build wrote it, it is that build's
    /// (owner ruling of 2026-09-27). Throws std::invalid_argument for RotationEnabled or
    /// PositionEnabled (TrackingMode), a concept that is not global or was given before, or a float
    /// or double `shipped` that is not finite.
    template <class T>
    void Setting(schema::Concept id, const T& value, const T& shipped) {
        CheckShipped(id, shipped);
        Setting(id, value == shipped);
    }

    /// A setting whose default changed between published builds, where the file narrows the build
    /// that wrote it to several whose defaults differ: left to Defaults.ini when `value` equals any
    /// of `shipped`, the effective value each of those builds shipped, compared with ==. Throws as
    /// the overload above does, for any `shipped` value, and for an empty `shipped`.
    template <class T>
    void Setting(schema::Concept id, const T& value, const std::vector<T>& shipped) {
        if (shipped.empty()) {
            throw std::invalid_argument(std::string(schema::kConcepts[static_cast<std::size_t>(id)].name) +
                                        ": no shipped value is given");
        }
        bool unchanged = false;
        for (const T& one : shipped) {
            CheckShipped(id, one);
            if (value == one) unchanged = true;
        }
        Setting(id, unchanged);
    }

    /// A setting the import compares itself, such as a hotkey code together with its chord
    /// switch: left to Defaults.ini when `unchanged`. Throws as the comparing overload does.
    void Setting(schema::Concept id, bool unchanged);

    /// A concept the legacy build had no setting for, which no player can have changed: always
    /// left to Defaults.ini. Throws as Setting does.
    void NotInLegacy(schema::Concept id);

    /// The tracking mode, RotationEnabled and PositionEnabled as one unit: both are left to
    /// Defaults.ini when `value`, the legacy mode, equals `shipped`, the mode the legacy build
    /// shipped, compared with ==. Throws std::invalid_argument when the mode was given before.
    template <class T>
    void TrackingMode(const T& value, const T& shipped) {
        TrackingMode(value == shipped);
    }

    /// The tracking mode, compared by the import over every legacy setting it derives the mode
    /// from, a position switch included: both rows are left to Defaults.ini when `unchanged`.
    /// Throws std::invalid_argument when the mode was given before.
    void TrackingMode(bool unchanged);

    /// The concepts left to Defaults.ini, in the order given, for ImportResult's
    /// follows_defaults_ini.
    const std::vector<schema::Concept>& Concepts() const { return concepts_; }

private:
    template <class T>
    static void CheckShipped(schema::Concept id, const T& shipped) {
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(shipped)) {
                throw std::invalid_argument(std::string(schema::kConcepts[static_cast<std::size_t>(id)].name) +
                                            ": the shipped value is not finite");
            }
        }
    }

    void Given(schema::Concept id);

    std::vector<schema::Concept> given_;
    std::vector<schema::Concept> concepts_;
};

}  // namespace cameraunlock::config
