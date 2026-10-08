#include "cameraunlock/config/config_table.h"

#include "cameraunlock/ads/aim_mode.h"
#include "cameraunlock/config/config_key_schema.g.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::config::detail {

namespace {

constexpr std::string_view kStampSection = "CameraUnlock";
constexpr std::string_view kFormatKey = "ConfigFormat";
constexpr const char* kCrlf = "\r\n";
constexpr std::string_view kDefaultToken = "default";

constexpr const char* kDefaultsIniHeader[] = {
    "; A setting set to default takes its value from Defaults.ini, which every head tracking mod",
    "; that keeps its settings in CameraUnlock.ini reads: %AppData%\\CameraUnlock\\Defaults.ini on",
    "; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on",
    "; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini",
    "; on macOS. The log names the file it read. Change a setting in Defaults.ini to change it in",
    "; every game that has it set to default, or write a value here instead of default to change it",
    "; for this game only.",
};

bool IsPrintableAscii(std::string_view text) {
    for (char c : text) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

bool IsSchemaSectionWithCanonicalConcept(std::string_view section) {
    for (const schema::ConceptInfo& info : schema::kConcepts) {
        if (section == info.section) return true;
    }
    return false;
}

void CheckUniqueKey(const std::vector<TableRow>& rows, const TableRow& row) {
    for (const TableRow& earlier : rows) {
        if (EqualsAsciiIgnoreCase(earlier.key, row.key)) {
            throw std::invalid_argument(RowName(row) + ": " + RowName(earlier) +
                                        " already has that key, and a key name is used once in the file");
        }
    }
}

CanonicalDiagnostic MakeDiagnostic(CanonicalDiagnosticKind kind, int line, std::string_view section,
                                   std::string_view key, std::string_view value, std::string_view detail) {
    CanonicalDiagnostic d;
    d.kind = kind;
    d.lines.push_back(line);
    d.section = std::string(section);
    d.key = std::string(key);
    d.value = std::string(value);
    d.detail = std::string(detail);
    return d;
}

// The schema's reason for a key naming a concept the canonical format does not write, or
// nullptr.
const char* NonCanonicalReason(const char* canonical) {
    for (const schema::NonCanonicalConcept& concept_info : schema::kNonCanonicalConcepts) {
        if (std::string_view(canonical) == concept_info.normalized) return concept_info.reason;
    }
    return nullptr;
}

// The schema's reason for a spelling of a setting the canonical format does not write that is
// no concept (a deadzone, a response curve), or nullptr.
const char* NonCanonicalKeyReason(const std::string& key) {
    const std::string normalized = NormalizeConfigKey(key);
    for (const schema::NonCanonicalKey& other : schema::kNonCanonicalKeys) {
        if (normalized == other.normalized) return other.reason;
    }
    return nullptr;
}

// The schema's reason for a section that holds only settings the canonical format does not
// write ([Sensitivity], [Inversion], [Deadzone]), or nullptr.
const char* NonCanonicalSectionReason(std::string_view section) {
    for (const schema::NonCanonicalSection& other : schema::kNonCanonicalSections) {
        if (EqualsAsciiIgnoreCase(section, other.section)) return other.reason;
    }
    return nullptr;
}

// The row a key names outside its own section or spelling: a row's key in any section, or a
// concept row's key or alias. Every key name is used once in a file, so there is at most one.
const TableRow* MisplacedRow(const std::vector<TableRow>& rows, const std::string& key, const char* canonical) {
    for (const TableRow& row : rows) {
        if (EqualsAsciiIgnoreCase(row.key, key)) return &row;
        if (canonical != nullptr && row.concept_id && NormalizeConfigKey(row.key) == canonical) return &row;
    }
    return nullptr;
}

// A key no row read: MisplacedKey, RetiredKey or NonCanonicalConcept when it names such a
// setting, else UnknownKey when `report_unknown`.
void ReportUnread(const std::vector<TableRow>& rows, const CanonicalSection& section, const CanonicalValue& value,
                  bool report_unknown, std::vector<CanonicalDiagnostic>& out) {
    const char* canonical = ResolveConfigKey(value.key);
    if (const TableRow* row = MisplacedRow(rows, value.key, canonical)) {
        out.push_back(MakeDiagnostic(CanonicalDiagnosticKind::MisplacedKey, value.line, section.name, value.key,
                                     value.value, RowName(*row)));
        return;
    }
    if (canonical != nullptr && IsRetiredConfigKey(canonical)) {
        out.push_back(
            MakeDiagnostic(CanonicalDiagnosticKind::RetiredKey, value.line, section.name, value.key, value.value, ""));
        return;
    }
    const char* reason = canonical == nullptr ? NonCanonicalKeyReason(value.key) : NonCanonicalReason(canonical);
    if (reason == nullptr) reason = NonCanonicalSectionReason(section.name);
    if (reason != nullptr) {
        out.push_back(MakeDiagnostic(CanonicalDiagnosticKind::NonCanonicalConcept, value.line, section.name,
                                     value.key, value.value, reason));
        return;
    }
    if (report_unknown) {
        out.push_back(
            MakeDiagnostic(CanonicalDiagnosticKind::UnknownKey, value.line, section.name, value.key, value.value, ""));
    }
}

void AppendRow(std::string& out, const TableRow& row, std::size_t index, const RowSource& source, RowForm form) {
    for (const std::string& line : row.comment) out.append("; ").append(line).append(kCrlf);
    const std::string value = form == RowForm::kDefault ? std::string(kDefaultToken) : source.Render(index);
    if (form == RowForm::kAsRender && row.engine && source.EqualsDefault(index)) out.append("; ");
    out.append(row.key).append("=").append(value).append(kCrlf);
}

}  // namespace

std::string RowName(const TableRow& row) { return "[" + row.section + "] " + row.key; }

void CheckFreshRow(const TableRow& row, bool holds, const std::string& table_default, const std::string& schema_default) {
    if (holds) return;
    throw std::invalid_argument(RowName(row) + " defaults to " + table_default + ", and the schema to " + schema_default +
                                ". A fresh file writes default on this row, which takes Defaults.ini's value, so the "
                                "row's own default must be the schema's, or the row must be marked PerGame().");
}

void CheckFreshPair(const std::vector<TableRow>& rows) {
    const auto binds = [&](schema::Concept id) {
        return std::any_of(rows.begin(), rows.end(), [&](const TableRow& row) { return row.concept_id == id; });
    };
    if (binds(schema::Concept::RotationEnabled) && !binds(schema::Concept::PositionEnabled)) {
        throw std::invalid_argument("the table binds [General] RotationEnabled without [Position] PositionEnabled, and "
                                    "the tracking mode is the two of them together");
    }
}

void CheckPairPerGame(const std::vector<TableRow>& rows) {
    const auto find = [&](schema::Concept id) {
        return std::find_if(rows.begin(), rows.end(), [&](const TableRow& row) { return row.concept_id == id; });
    };
    const auto rotation = find(schema::Concept::RotationEnabled);
    const auto position = find(schema::Concept::PositionEnabled);
    if (rotation == rows.end() || position == rows.end() || rotation->per_game == position->per_game) return;
    const TableRow& marked = rotation->per_game ? *rotation : *position;
    const TableRow& other = rotation->per_game ? *position : *rotation;
    throw std::invalid_argument(RowName(marked) + " is marked PerGame() and " + RowName(other) +
                                " is not. The two are one setting, the tracking mode, so PerGame() marks both or "
                                "neither.");
}

std::vector<std::string> CommentLines(const char* text, const std::string& row) {
    std::vector<std::string> lines;
    const std::string_view all(text);
    if (all.empty()) return lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = all.find('\n', start);
        const std::string_view line = all.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const std::string number = std::to_string(lines.size() + 1);
        if (line.empty()) throw std::invalid_argument(row + ": comment line " + number + " is empty");
        if (!IsPrintableAscii(line)) {
            throw std::invalid_argument(row + ": comment line " + number +
                                        " holds a byte outside printable ASCII, and a canonical file is ASCII");
        }
        if (line.front() == ' ' || line.back() == ' ') {
            throw std::invalid_argument(row + ": comment line " + number + " starts or ends with a space");
        }
        lines.emplace_back(line);
        if (end == std::string_view::npos) return lines;
        start = end + 1;
    }
}

void CheckConceptRow(const std::vector<TableRow>& rows, const TableRow& row) { CheckUniqueKey(rows, row); }

void CheckLocalRow(const std::vector<TableRow>& rows, const TableRow& row) {
    const std::string name = RowName(row);
    if (!IsPascalCase(row.section)) {
        throw std::invalid_argument(name + ": the section name is not PascalCase ASCII letters and digits");
    }
    if (!IsPascalCase(row.key)) {
        throw std::invalid_argument(name + ": the key is not PascalCase ASCII letters and digits");
    }
    if (EqualsAsciiIgnoreCase(row.section, kStampSection)) {
        throw std::invalid_argument(name + ": [CameraUnlock] belongs to core, so a game-local row goes elsewhere");
    }
    for (const char* section : schema::kSections) {
        if (!EqualsAsciiIgnoreCase(row.section, section)) continue;
        if (row.section != section) {
            throw std::invalid_argument(name + ": the schema spells this section [" + std::string(section) + "]");
        }
        if (!IsSchemaSectionWithCanonicalConcept(section)) {
            throw std::invalid_argument(name + ": [" + std::string(section) +
                                        "] holds none of the settings a canonical file writes, so it has no rows");
        }
    }
    if (const char* reason = NonCanonicalSectionReason(row.section)) {
        throw std::invalid_argument(name + ": [" + row.section + "] holds only settings a canonical file does not "
                                    "carry, so it has no rows: " + reason);
    }
    for (const TableRow& earlier : rows) {
        if (EqualsAsciiIgnoreCase(earlier.section, row.section) && earlier.section != row.section) {
            throw std::invalid_argument(name + ": " + RowName(earlier) + " spells this section [" + earlier.section +
                                        "]");
        }
    }
    const char* canonical = ResolveConfigKey(row.key);
    if (canonical != nullptr) {
        throw std::invalid_argument(name + ": " + row.key + " is the key or an alias of the schema concept '" +
                                    canonical + "', so a game-local row cannot use it");
    }
    if (const char* reason = NonCanonicalKeyReason(row.key)) {
        throw std::invalid_argument(name + ": " + row.key + " names a setting a canonical file does not carry, so a "
                                    "game-local row cannot use it: " + reason);
    }
    if (EqualsAsciiIgnoreCase(row.key, kFormatKey)) {
        throw std::invalid_argument(name + ": [CameraUnlock] ConfigFormat already has that key, and a key name is used "
                                           "once in the file");
    }
    CheckUniqueKey(rows, row);
    if (row.comment.empty()) {
        const bool covered = std::any_of(rows.begin(), rows.end(), [&](const TableRow& earlier) {
            return !earlier.concept_id && earlier.section == row.section;
        });
        if (!covered) {
            throw std::invalid_argument(name + " needs a comment: only a row written below a game-local row of its "
                                               "section may share that row's comment");
        }
    }
}

EffectiveApplyResult ApplyRows(const CanonicalIni& doc, const std::vector<TableRow>& rows, RowTarget& target,
                               const std::vector<ValueSource>& start_sources) {
    if (!doc.IsReadable()) {
        throw std::invalid_argument(std::string("ApplyCanonical needs a readable document, and this one is ") +
                                    CanonicalReadStatusName(doc.status));
    }

    for (std::size_t i = 0; i < rows.size(); ++i) target.ResetToStart(i);

    EffectiveApplyResult result;
    result.sources = start_sources;
    ApplyReport& report = result.report;
    std::vector<int> read_from(rows.size(), 0);
    for (const CanonicalSection& section : doc.sections) {
        if (EqualsAsciiIgnoreCase(section.name, kStampSection)) continue;
        const bool known = std::any_of(rows.begin(), rows.end(), [&](const TableRow& row) {
            return EqualsAsciiIgnoreCase(row.section, section.name);
        });
        if (!known) {
            report.diagnostics.push_back(
                MakeDiagnostic(CanonicalDiagnosticKind::UnknownSection, section.line, section.name, "", "", ""));
        }
        for (const CanonicalValue& value : section.values) {
            std::size_t row = rows.size();
            for (std::size_t i = 0; known && i < rows.size(); ++i) {
                if (EqualsAsciiIgnoreCase(rows[i].section, section.name) && EqualsAsciiIgnoreCase(rows[i].key, value.key)) {
                    row = i;
                }
            }
            if (row == rows.size()) {
                ReportUnread(rows, section, value, known, report.diagnostics);
                continue;
            }
            if (rows[row].concept_id && EqualsAsciiIgnoreCase(value.value, kDefaultToken)) continue;
            const std::string error = target.Apply(row, value.value);
            if (error.empty()) {
                read_from[row] = value.line;
                result.sources[row] = ValueSource::kFile;
            } else {
                report.diagnostics.push_back(MakeDiagnostic(CanonicalDiagnosticKind::InvalidValue, value.line,
                                                            section.name, value.key, value.value, error));
            }
        }
    }

    std::size_t rotation = rows.size();
    std::size_t position = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].concept_id == schema::Concept::RotationEnabled) rotation = i;
        if (rows[i].concept_id == schema::Concept::PositionEnabled) position = i;
    }
    if (rotation < rows.size() && position < rows.size() && target.IsFalse(rotation) && target.IsFalse(position)) {
        CanonicalDiagnostic d;
        d.kind = CanonicalDiagnosticKind::NoTrackingMode;
        for (const std::size_t row : {rotation, position}) {
            if (read_from[row] != 0) d.lines.push_back(read_from[row]);
        }
        std::sort(d.lines.begin(), d.lines.end());
        for (const std::size_t row : {rotation, position}) {
            target.ResetToStart(row);
            result.sources[row] = start_sources[row];
        }
        report.diagnostics.push_back(std::move(d));
    }

    std::stable_sort(report.diagnostics.begin(), report.diagnostics.end(),
                     [](const CanonicalDiagnostic& a, const CanonicalDiagnostic& b) {
                         if (a.lines.front() != b.lines.front()) return a.lines.front() < b.lines.front();
                         return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                     });
    return result;
}

std::vector<std::string> GameFileHeader(const std::vector<TableRow>& rows, const RenderHeader& header) {
    const std::string& name = header.display_name;
    if (name.empty() || !IsPrintableAscii(name) || name.front() == ' ' || name.back() == ' ') {
        throw std::invalid_argument("display name '" + name +
                                    "' is not printable ASCII without a leading or trailing space");
    }

    std::vector<std::string> lines{
        "; " + name + " head tracking settings.",
        "; Comments start with ; and go on their own line. Text after a value is part of the value.",
    };
    if (std::any_of(rows.begin(), rows.end(), [](const TableRow& row) { return row.hotkey; })) {
        lines.emplace_back("; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; "
                           "leave empty for none.");
    }
    if (std::any_of(rows.begin(), rows.end(), FollowsDefaultsIni)) {
        lines.insert(lines.end(), std::begin(kDefaultsIniHeader), std::end(kDefaultsIniHeader));
    }
    return lines;
}

std::string RenderRows(const std::vector<TableRow>& rows, const std::vector<std::string>& header, const RowSource& source,
                       const std::vector<RowForm>& forms) {
    std::string out;
    for (const std::string& line : header) out.append(line).append(kCrlf);
    out.append(kCrlf).append("[CameraUnlock]").append(kCrlf);
    out.append("; Written by the mod. Leave this section in place.").append(kCrlf);
    out.append(kFormatKey).append("=").append(std::to_string(kConfigFormat)).append(kCrlf);

    for (const char* section : schema::kSections) {
        std::vector<std::size_t> order;
        for (const schema::ConceptInfo& info : schema::kConcepts) {
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].concept_id == info.id && rows[i].section == section) order.push_back(i);
            }
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].concept_id && rows[i].section == section) order.push_back(i);
        }
        if (order.empty()) continue;
        out.append(kCrlf).append("[").append(section).append("]").append(kCrlf);
        for (const std::size_t i : order) AppendRow(out, rows[i], i, source, forms[i]);
    }

    std::vector<std::string> local_sections;
    for (const TableRow& row : rows) {
        const bool schema_section = std::any_of(std::begin(schema::kSections), std::end(schema::kSections),
                                                [&](const char* section) { return row.section == section; });
        if (!schema_section && std::find(local_sections.begin(), local_sections.end(), row.section) == local_sections.end()) {
            local_sections.push_back(row.section);
        }
    }
    for (const std::string& section : local_sections) {
        out.append(kCrlf).append("[").append(section).append("]").append(kCrlf);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].section == section) AppendRow(out, rows[i], i, source, forms[i]);
        }
    }
    return out;
}

namespace {

constexpr schema::Concept kTrackingModeRows[] = {schema::Concept::RotationEnabled, schema::Concept::PositionEnabled};
constexpr schema::Concept kAimModeRows[] = {schema::Concept::TrueFreeLook, schema::Concept::FreeLookMarker,
                                            schema::Concept::StockSights};

constexpr const char* kTrackingModeChoices[] = {"Rotation and position", "Rotation only", "Position only"};
constexpr const char* kAimModeChoices[] = {"Sights locked", "Free look with marker", "True free look", "Stock sights"};

constexpr const char* kTrackingModeComment[] = {
    "What of your head moves the view: its turning and its leaning, its turning alone, or its",
    "leaning alone.",
};
constexpr const char* kAimModeComment[] = {
    "What head tracking does while you aim down the sights. Sights locked: leaning keeps your",
    "eye on the sights. Free look with marker: the weapon stays put, your head moves freely",
    "around it, and a marker shows where the shot will land. True free look: the same with no",
    "marker. Stock sights: your head's turning and leaning ease out, so the sight picture is",
    "the game's own.",
};

bool IsLower(char c) { return c >= 'a' && c <= 'z'; }
bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// A PascalCase name as words: a new word starts at a capital after a small letter or a digit, and
// at the last capital of a run of them that a small letter follows. Every word but the first
// starts small, and a run of capitals is kept as it is: "Field of view", "Position limit Z back".
std::string AsWords(std::string_view name) {
    std::string words;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool word_follows = i + 1 < name.size() && IsLower(name[i + 1]);
        const bool starts = i > 0 && IsUpper(c) &&
                            (IsLower(name[i - 1]) || IsDigit(name[i - 1]) || (IsUpper(name[i - 1]) && word_follows));
        if (starts) words += ' ';
        words += starts && word_follows ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return words;
}

// The rows of a mode, in the order of `ids`, when the table has every one and all are Writable.
bool ModeRowsOf(const std::vector<TableRow>& rows, const schema::Concept* ids, std::size_t count, std::size_t* found) {
    for (std::size_t i = 0; i < count; ++i) {
        found[i] = rows.size();
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (rows[r].concept_id == ids[i] && rows[r].writable) found[i] = r;
        }
        if (found[i] == rows.size()) return false;
    }
    return true;
}

bool IsModeRow(const TableRow& row) {
    if (!row.concept_id) return false;
    return std::find(std::begin(kTrackingModeRows), std::end(kTrackingModeRows), *row.concept_id) !=
               std::end(kTrackingModeRows) ||
           std::find(std::begin(kAimModeRows), std::end(kAimModeRows), *row.concept_id) != std::end(kAimModeRows);
}

}  // namespace

std::size_t ModeRowCount(OptionSlot::Kind mode) {
    return mode == OptionSlot::Kind::kTrackingMode ? std::size(kTrackingModeRows) : std::size(kAimModeRows);
}

std::vector<OptionSlot> PlanOptions(const std::vector<TableRow>& rows, const std::vector<bool>& shaped) {
    OptionSlot tracking{OptionSlot::Kind::kTrackingMode};
    OptionSlot aim{OptionSlot::Kind::kAimMode};
    const bool has_tracking = ModeRowsOf(rows, kTrackingModeRows, std::size(kTrackingModeRows), tracking.rows);
    const bool has_aim = ModeRowsOf(rows, kAimModeRows, std::size(kAimModeRows), aim.rows);
    const std::size_t tracking_at = *std::min_element(tracking.rows, tracking.rows + std::size(kTrackingModeRows));
    const std::size_t aim_at = *std::min_element(aim.rows, aim.rows + std::size(kAimModeRows));

    std::vector<OptionSlot> slots;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (has_tracking && r == tracking_at) {
            slots.push_back(tracking);
        } else if (has_aim && r == aim_at) {
            slots.push_back(aim);
        } else if (rows[r].writable && !rows[r].engine && shaped[r] && !IsModeRow(rows[r])) {
            OptionSlot slot;
            slot.rows[0] = r;
            slots.push_back(slot);
        }
    }
    return slots;
}

ConfigOption DescribeOption(const TableRow& row, const OptionShape& shape) {
    ConfigOption option;
    option.id = row.key;
    option.section = row.section;
    option.label = AsWords(row.key);
    option.comment = row.comment;
    option.kind = shape.kind;
    option.min = shape.min;
    option.max = shape.max;
    option.step = 1.0;
    if (shape.kind == OptionKind::kFloat) {
        const double range = shape.max - shape.min;
        option.step = shape.ranged && range > 0.0 ? std::pow(10.0, std::round(std::log10(range / 50.0))) : 0.0;
    }
    for (const std::string& token : shape.tokens) option.choices.push_back(AsWords(token));
    return option;
}

ConfigOption DescribeMode(OptionSlot::Kind mode, const TableRow& first) {
    const bool tracking = mode == OptionSlot::Kind::kTrackingMode;
    ConfigOption option;
    option.source = mode;
    option.id = tracking ? "TrackingMode" : "AimMode";
    option.section = first.section;
    option.label = AsWords(option.id);
    if (tracking) {
        option.comment.assign(std::begin(kTrackingModeComment), std::end(kTrackingModeComment));
        option.choices.assign(std::begin(kTrackingModeChoices), std::end(kTrackingModeChoices));
    } else {
        option.comment.assign(std::begin(kAimModeComment), std::end(kAimModeComment));
        option.choices.assign(std::begin(kAimModeChoices), std::end(kAimModeChoices));
    }
    option.kind = OptionKind::kEnum;
    option.max = static_cast<double>(option.choices.size() - 1);
    option.step = 1.0;
    return option;
}

double ModeNumber(OptionSlot::Kind mode, const bool* on) {
    if (mode == OptionSlot::Kind::kTrackingMode) {
        const std::optional<TrackingMode> decoded = DecodeTrackingMode(on[0], on[1]);
        if (!decoded) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which is not a tracking mode");
        return static_cast<double>(static_cast<int>(*decoded));
    }
    return static_cast<double>(static_cast<int>(ads::DecodeAimMode(on[0], on[1], on[2])));
}

void ModeRows(OptionSlot::Kind mode, double number, bool* on) {
    const bool tracking = mode == OptionSlot::Kind::kTrackingMode;
    const double count = static_cast<double>(tracking ? std::size(kTrackingModeChoices) : std::size(kAimModeChoices));
    if (!(number >= 0.0 && number < count) || number != std::floor(number)) {
        RefuseOptionValue(number, tracking ? "it is not a tracking mode" : "it is not an aim mode");
    }
    if (tracking) {
        const TrackingModeChannels channels = EncodeTrackingMode(static_cast<TrackingMode>(static_cast<int>(number)));
        on[0] = channels.rotation_enabled;
        on[1] = channels.position_enabled;
    } else {
        const ads::AimModeSettings settings = ads::EncodeAimMode(static_cast<ads::AimMode>(static_cast<int>(number)));
        on[0] = settings.trueFreeLook;
        on[1] = settings.freeLookMarker;
        on[2] = settings.stockSights;
    }
}

void RefuseOptionValue(double value, const char* why) {
    throw std::invalid_argument(std::to_string(value) + " is not a value of the option: " + why);
}

}  // namespace cameraunlock::config::detail
