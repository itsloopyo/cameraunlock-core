#pragma once

#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// What a game's legacy import hands the migration driver. The import is the game's own reader
// as its last pre-canonical build ran it, frozen in the game's repo with a copy of that build's
// Config struct and defaults, plus a map from that struct into the runtime Config. It runs once,
// on a file that has no [CameraUnlock] stamp, and writes nothing. Pure, no <windows.h>. The C#
// twins are in CameraUnlock.Core.Config.

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
    /// A sensitivity, deadzone, response curve or axis inversion the player set away from the
    /// shipped default. The tracker shapes the pose; the mod no longer does. A shipped default
    /// that is not identity moves into the mod's axis conversion instead and is not dropped.
    PoseShaping = 2,
    /// A reticle setting: mods no longer draw or toggle a reticle.
    Reticle = 3,
    /// A feature that shipped disabled pending verification now follows the mod's default. The
    /// map records one only where the legacy value differs from that default.
    FollowsDefault = 4,
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

/// What an import returns. Build it with the factories, which hold each status to its fields.
struct ImportResult {
    ImportStatus status = ImportStatus::Imported;
    /// For Refused and Undecodable, what the player is told; empty otherwise.
    std::string reason;
    /// For Imported and Absent, the values the map dropped, in the order it met them; empty
    /// otherwise.
    std::vector<DroppedValue> dropped;

    static ImportResult Imported(std::vector<DroppedValue> dropped) {
        return ImportResult{ImportStatus::Imported, {}, std::move(dropped)};
    }
    static ImportResult Absent(std::vector<DroppedValue> dropped) {
        return ImportResult{ImportStatus::Absent, {}, std::move(dropped)};
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
        return ImportResult{status, std::move(reason), {}};
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

}  // namespace cameraunlock::config
