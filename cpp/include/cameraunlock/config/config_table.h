#pragma once

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/hotkey_codec.h>
#include <cameraunlock/config/value_codecs.h>

#include <charconv>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Config tables: which rows a canonical config file holds and which Config field each one
// reads into, then ApplyCanonical to read a parsed file into a Config, RenderCanonical to
// write a Config as a canonical file and RenderCanonicalFresh to write the file a game starts
// with. Pure, no <windows.h>. CameraUnlock.Core.Config.ConfigTable
// is the C# twin, and data/fixtures/canonical-ini/table holds both to the same bytes.

namespace cameraunlock::config {

/// What RenderCanonical writes above the settings.
struct RenderHeader {
    /// The game's name as data/games.json spells it, in printable ASCII, since everything the
    /// renderer writes is ASCII.
    std::string display_name;
};

/// What ApplyCanonical found beyond the reader's own diagnostics (CanonicalIni::diagnostics),
/// ordered by first line, then kind.
struct ApplyReport {
    std::vector<CanonicalDiagnostic> diagnostics;
};

namespace detail {

template <class Int>
constexpr bool IntegralHolds(long long min, long long max) {
    if constexpr (std::is_signed_v<Int>) {
        return static_cast<long long>(std::numeric_limits<Int>::min()) <= min &&
               static_cast<long long>(std::numeric_limits<Int>::max()) >= max;
    } else {
        return min >= 0 &&
               static_cast<unsigned long long>(max) <= static_cast<unsigned long long>(std::numeric_limits<Int>::max());
    }
}

}  // namespace detail

/// True when a field of type Field can hold concept Id: bool for the kBool family; for kInteger
/// an integral type other than bool whose limits hold the concept's range, so std::uint16_t
/// holds UdpPort (1 to 65535) and not DataFreshnessMs (1 to INT_MAX); float or double for
/// kFloating; std::string for kHotkey. A concept row whose field fails this does not compile.
template <schema::Concept Id, class Field>
constexpr bool FieldHoldsConcept() {
    using Traits = schema::ConceptTraits<Id>;
    if constexpr (Traits::kFamily == schema::ValueFamily::kBool) {
        return std::is_same_v<Field, bool>;
    } else if constexpr (Traits::kFamily == schema::ValueFamily::kInteger) {
        if constexpr (std::is_integral_v<Field> && !std::is_same_v<Field, bool>) {
            return detail::IntegralHolds<Field>(Traits::kMin, Traits::kMax);
        } else {
            return false;
        }
    } else if constexpr (Traits::kFamily == schema::ValueFamily::kFloating) {
        return std::is_same_v<Field, float> || std::is_same_v<Field, double>;
    } else {
        return std::is_same_v<Field, std::string>;
    }
}

namespace detail {

// A row as the non-template parts see it.
struct TableRow {
    std::string section;
    std::string key;
    std::vector<std::string> comment;
    std::optional<schema::Concept> concept_id;
    bool engine = false;
    bool writable = false;
    bool hotkey = false;
    bool per_game = false;
};

// A global concept row that is not PerGame, whose default is the effective default.
inline bool FollowsDefaultsIni(const TableRow& row) {
    return row.concept_id.has_value() && schema::kConcepts[static_cast<std::size_t>(*row.concept_id)].global &&
           !row.per_game;
}

// Where a row's value came from when ApplyCanonicalEffective read a file: the file's own value,
// or the row's start, the effective default, which Defaults.ini or the table gave.
enum class ValueSource { kFile, kDefaultsIni, kBuiltIn };

// What ApplyCanonicalEffective found, and each row's source, in table order.
struct EffectiveApplyResult {
    ApplyReport report;
    std::vector<ValueSource> sources;
};

// How a render writes a row: as RenderCanonical does (an Engine row at its default commented),
// as its value, or as the default token.
enum class RowForm { kAsRender, kValue, kDefault };

// One Config's row values, for ApplyRows.
class RowTarget {
public:
    // Reads text into the row's field. Returns the codec's error, empty when it was read.
    virtual std::string Apply(std::size_t row, std::string_view text) = 0;
    virtual bool IsFalse(std::size_t row) const = 0;
    virtual void ResetToStart(std::size_t row) = 0;

protected:
    ~RowTarget() = default;
};

// One Config's row values, for RenderRows.
class RowSource {
public:
    virtual std::string Render(std::size_t row) const = 0;
    virtual bool EqualsDefault(std::size_t row) const = 0;

protected:
    ~RowSource() = default;
};

// "[Section] Key", for messages.
std::string RowName(const TableRow& row);

// Splits a comment at '\n' into lines of printable ASCII with no leading or trailing space.
// An empty text is no lines. Throws std::invalid_argument naming the row.
std::vector<std::string> CommentLines(const char* text, const std::string& row);

// Throw std::invalid_argument when the row cannot join the rows before it.
void CheckConceptRow(const std::vector<TableRow>& rows, const TableRow& row);
void CheckLocalRow(const std::vector<TableRow>& rows, const TableRow& row);


// Throw std::invalid_argument when the table cannot render a fresh file: a row that follows
// Defaults.ini whose default is not the schema's (`holds` false; the two texts as the row's codec
// writes them), or RotationEnabled without PositionEnabled.
void CheckFreshRow(const TableRow& row, bool holds, const std::string& table_default, const std::string& schema_default);
void CheckFreshPair(const std::vector<TableRow>& rows);

// Throw std::invalid_argument when the table marks one of RotationEnabled and PositionEnabled
// PerGame and not the other. One PerGame half would start the pair from two sources, which can
// name no tracking mode from a Defaults.ini pair that is valid on its own.
void CheckPairPerGame(const std::vector<TableRow>& rows);

// Every row starts at its start and reports start_sources[row] until the file sets it.
EffectiveApplyResult ApplyRows(const CanonicalIni& doc, const std::vector<TableRow>& rows, RowTarget& target,
                               const std::vector<ValueSource>& start_sources);
// The header lines a game's file starts with. Throws std::invalid_argument for a display name
// that is empty, has a leading or trailing space, or holds a byte outside printable ASCII.
std::vector<std::string> GameFileHeader(const std::vector<TableRow>& rows, const RenderHeader& header);
std::string RenderRows(const std::vector<TableRow>& rows, const std::vector<std::string>& header, const RowSource& source,
                       const std::vector<RowForm>& forms);

template <class T>
struct IsIntCodec : std::false_type {};
template <class Int>
struct IsIntCodec<IntCodec<Int>> : std::true_type {};

template <class T>
struct IsFloatingCodec : std::false_type {};
template <class F>
struct IsFloatingCodec<FloatingCodec<F>> : std::true_type {};

// A value as a message shows it when its codec cannot write it: numbers in their shortest
// round-trip form, an enum as its number, a list or color as its items joined by ", ".
template <class T>
std::string DisplayValue(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(value));
    } else if constexpr (std::is_arithmetic_v<T>) {
        char text[64];
        const std::to_chars_result written = std::to_chars(text, text + sizeof(text), value);
        return std::string(text, written.ptr);
    } else {
        std::string text;
        bool first = true;
        for (const auto& item : value) {
            if (!first) text += ", ";
            first = false;
            text += DisplayValue(item);
        }
        return text;
    }
}

// Range(lo, hi) of an int row takes whole numbers no larger in size than 2^53, which a
// double holds exactly, within the field's type.
constexpr double kLargestExactWhole = 9007199254740992.0;

template <class Config>
class RowOps {
public:
    virtual ~RowOps() = default;
    virtual std::string Apply(std::string_view text, Config& config) const = 0;
    virtual std::string Render(const Config& config) const = 0;
    virtual std::string Display(const Config& config) const = 0;
    virtual bool Equal(const Config& a, const Config& b) const = 0;
    // Whether the field holds the text as the row's codec reads it; the text as the codec writes
    // it comes back in `canonical`.
    virtual bool Holds(const Config& config, std::string_view text, std::string& canonical) const = 0;
    virtual void Assign(Config& to, const Config& from) const = 0;
    virtual bool IsFalse(const Config& config) const = 0;
    virtual std::shared_ptr<const RowOps> WithRange(double lo, double hi) const = 0;
};

template <class Config, class Codec, class Get, class Set>
class CodecRow final : public RowOps<Config> {
public:
    using Value = typename Codec::Value;

    CodecRow(Codec codec, Get get, Set set) : codec_(std::move(codec)), get_(std::move(get)), set_(std::move(set)) {}

    std::string Apply(std::string_view text, Config& config) const override {
        CodecParseResult<Value> read = codec_.Parse(text);
        if (!read.ok()) return read.error;
        set_(config, std::move(read.value));
        return {};
    }

    std::string Render(const Config& config) const override { return codec_.Render(get_(config)); }

    std::string Display(const Config& config) const override { return DisplayValue(Value(get_(config))); }

    bool Equal(const Config& a, const Config& b) const override { return codec_.Equal(get_(a), get_(b)); }

    bool Holds(const Config& config, std::string_view text, std::string& canonical) const override {
        const CodecParseResult<Value> read = codec_.Parse(text);
        if (!read.ok()) throw std::logic_error("'" + std::string(text) + "' does not read: " + read.error);
        canonical = codec_.Render(read.value);
        return codec_.Equal(get_(config), read.value);
    }

    void Assign(Config& to, const Config& from) const override { set_(to, Value(get_(from))); }

    bool IsFalse(const Config& config) const override {
        if constexpr (std::is_same_v<Value, bool>) {
            return !get_(config);
        } else {
            throw std::logic_error("IsFalse on a row that does not hold a bool");
        }
    }

    std::shared_ptr<const RowOps<Config>> WithRange(double lo, double hi) const override {
        if constexpr (IsIntCodec<Codec>::value) {
            const bool representable = lo >= -kLargestExactWhole && lo <= kLargestExactWhole &&
                                       hi >= -kLargestExactWhole && hi <= kLargestExactWhole;
            if (!representable || lo != static_cast<double>(static_cast<long long>(lo)) ||
                hi != static_cast<double>(static_cast<long long>(hi)) ||
                !IntegralHolds<Value>(static_cast<long long>(lo), static_cast<long long>(hi))) {
                throw std::invalid_argument("Range(" + std::to_string(lo) + ", " + std::to_string(hi) +
                                            ") is not two whole numbers the field's type holds");
            }
            return std::make_shared<CodecRow>(Codec(static_cast<Value>(lo), static_cast<Value>(hi)), get_, set_);
        } else if constexpr (IsFloatingCodec<Codec>::value) {
            return std::make_shared<CodecRow>(Codec(static_cast<Value>(lo), static_cast<Value>(hi)), get_, set_);
        } else {
            throw std::invalid_argument("Range applies to an int, float or double row");
        }
    }

private:
    Codec codec_;
    Get get_;
    Set set_;
};

}  // namespace detail

template <class Config>
class ConfigTable;

template <class Config>
class ConfigOwner;

template <class Config>
ApplyReport ApplyCanonical(const CanonicalIni& doc, const ConfigTable<Config>& table, Config& inout);

template <class Config>
std::string RenderCanonical(const ConfigTable<Config>& table, const Config& values, const RenderHeader& header);

template <class Config>
std::string RenderCanonicalFresh(const ConfigTable<Config>& table, const RenderHeader& header);

namespace detail {

template <class Config>
EffectiveApplyResult ApplyCanonicalEffective(const CanonicalIni& doc, const ConfigTable<Config>& table, Config& inout,
                                             const Config& effective,
                                             const std::vector<schema::Concept>& from_defaults_ini);

template <class Config>
std::string RenderCanonicalMigration(const ConfigTable<Config>& table, const Config& values, const Config& effective,
                                     const RenderHeader& header);

template <class Config>
std::string RenderCanonicalValues(const ConfigTable<Config>& table, const Config& values,
                                  const std::vector<std::string>& header);

}  // namespace detail

/// The rows of one game's canonical config file, each bound to a field of Config.
///
/// Concept rows take their section, key, codec, range and comment from the schema
/// (schema::ConceptTraits); local rows name their own. A row's default is its
/// field's value in the defaults instance the table is built with. Modifiers apply to the last
/// row added, or to the concept row Select names.
///
/// Every check throws std::invalid_argument from the call that breaks it: two rows with one key
/// name anywhere in the file (ASCII case-insensitive), counting the ConfigFormat key core
/// writes in [CameraUnlock]; a local section or key that is not PascalCase ASCII letters and
/// digits; a local row in [CameraUnlock], or in a schema section that holds no canonical
/// concept ([Sensitivity], [Inversion], [Reticle]) or that a non_canonical_keys group lists
/// ([Deadzone], schema::kNonCanonicalSections), or in a section spelled like a schema
/// section or an earlier local section with other letter case; a local key that is a
/// concept's key or alias, canonical, non-canonical or retired, under the
/// schema's normalisation (ResolveConfigKey), or a spelling the schema's non_canonical_keys
/// lists (schema::kNonCanonicalKeys); a local row with no comment that follows no
/// local row of its section; a default its row cannot write; RotationEnabled and
/// PositionEnabled both defaulting to false. EnumCodec already refuses a token that is not
/// PascalCase.
template <class Config>
class ConfigTable {
public:
    ConfigTable() : ConfigTable(Config{}) {}

    explicit ConfigTable(Config defaults) : defaults_(std::move(defaults)) {}

    /// A concept row on a direct member. The member's type must hold the concept
    /// (FieldHoldsConcept) or this does not compile.
    template <schema::Concept Id, class Field>
    ConfigTable& Concept(Field Config::*field) {
        static_assert(!std::is_function_v<Field>, "a row binds a data member, not a member function");
        return AddConcept<Id>(MemberGetter(field), MemberSetter(field));
    }

    /// A concept row read by get(const Config&) and written by set(Config&, Field), for a
    /// nested field or one the game keeps in several places. Field is get's result type,
    /// which must hold the concept.
    template <schema::Concept Id, class Get, class Set>
    ConfigTable& Concept(Get get, Set set) {
        return AddConcept<Id>(std::move(get), std::move(set));
    }

    /// A game-local row on a direct member, whose type must be the codec's Value. The comment
    /// is written above the row; '\n' separates its lines. An empty comment is allowed when an
    /// earlier local row of the section is written above this one, whose comment covers both.
    template <class Field, class Codec>
    ConfigTable& Local(const char* section, const char* key, Field Config::*field, Codec codec,
                       const char* comment) {
        static_assert(!std::is_function_v<Field>, "a row binds a data member, not a member function");
        return Local(section, key, MemberGetter(field), MemberSetter(field), std::move(codec), comment);
    }

    /// A game-local row read by get and written by set, as the concept overload.
    template <class Get, class Set, class Codec>
    ConfigTable& Local(const char* section, const char* key, Get get, Set set, Codec codec, const char* comment) {
        using Field = std::decay_t<std::invoke_result_t<Get&, const Config&>>;
        static_assert(std::is_same_v<Field, typename Codec::Value>, "a local row's field type is its codec's Value");
        static_assert(std::is_invocable_v<Set&, Config&, Field>, "the setter takes (Config&, the field's type)");
        if (section == nullptr || key == nullptr || comment == nullptr) {
            throw std::invalid_argument("a local row needs a section, a key and a comment");
        }
        detail::TableRow row;
        row.section = section;
        row.key = key;
        row.hotkey = std::is_same_v<Codec, HotkeyCodec>;
        row.comment = detail::CommentLines(comment, detail::RowName(row));
        detail::CheckLocalRow(rows_, row);
        Add(std::move(row),
            std::make_shared<detail::CodecRow<Config, Codec, Get, Set>>(std::move(codec), std::move(get), std::move(set)));
        return *this;
    }

    /// Replaces the schema's file_comment on a concept row, where the game's unit or behaviour
    /// differs. '\n' separates lines; the text may not be empty.
    ConfigTable& Comment(const char* text) {
        const std::size_t row = Last("Comment");
        if (!rows_[row].concept_id) {
            throw std::invalid_argument(detail::RowName(rows_[row]) +
                                        " is a local row, which carries its comment in Local");
        }
        if (text == nullptr) throw std::invalid_argument("Comment needs a text");
        std::vector<std::string> lines = detail::CommentLines(text, detail::RowName(rows_[row]));
        if (lines.empty()) throw std::invalid_argument(detail::RowName(rows_[row]) + " needs a comment");
        rows_[row].comment = std::move(lines);
        return *this;
    }

    /// An inclusive range for an int, float or double local row: a value outside it is
    /// invalid, never clamped. A concept row has the schema's range.
    ConfigTable& Range(double lo, double hi) {
        const std::size_t row = Last("Range");
        if (rows_[row].concept_id) {
            throw std::invalid_argument(detail::RowName(rows_[row]) + " is a concept row, which has the schema's range");
        }
        std::shared_ptr<const detail::RowOps<Config>> ranged;
        try {
            ranged = ops_[row]->WithRange(lo, hi);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(detail::RowName(rows_[row]) + ": " + e.what());
        }
        CheckDefault(rows_[row], *ranged);
        ops_[row] = std::move(ranged);
        return *this;
    }

    /// Marks the row as data about the game rather than a taste: at its default
    /// it is written as a comment, `; Key=value`, so a later build's corrected default reaches
    /// everyone who never set it.
    ConfigTable& Engine() {
        rows_[Last("Engine")].engine = true;
        return *this;
    }

    /// Marks the concept row as one this game keeps: its default is the table's own and never
    /// Defaults.ini's, so `default`, a missing key and an invalid value all read the table's
    /// default, and RenderCanonicalFresh writes the row's value. Each use needs an entry, approved
    /// by the owner, in the repo's `per_game` list in data/config-format.json. RotationEnabled and
    /// PositionEnabled are one setting, the tracking mode, so a table that binds both marks both or
    /// neither; ApplyCanonical and RenderCanonicalFresh throw on one without the other. Throws
    /// std::invalid_argument on a local row, and on a concept that is not global, which every game
    /// keeps already.
    ConfigTable& PerGame() {
        const std::size_t row = Last("PerGame");
        if (!rows_[row].concept_id) {
            throw std::invalid_argument(detail::RowName(rows_[row]) +
                                        " is a local row, which never takes a value from Defaults.ini");
        }
        if (!schema::kConcepts[static_cast<std::size_t>(*rows_[row].concept_id)].global) {
            throw std::invalid_argument(detail::RowName(rows_[row]) +
                                        " is not global in data/config-schema.json, so every game keeps its own "
                                        "value and Defaults.ini never reaches it; PerGame() is for a global concept");
        }
        rows_[row].per_game = true;
        return *this;
    }

    /// Marks the row as one the config owner's Save may change.
    ConfigTable& Writable() {
        rows_[Last("Writable")].writable = true;
        return *this;
    }

    /// Makes the concept's row the one the next modifier applies to, for a table another
    /// function built.
    ConfigTable& Select(schema::Concept id) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].concept_id == id) {
                last_ = i;
                return *this;
            }
        }
        throw std::invalid_argument(std::string("the table has no row for ") +
                                    schema::kConcepts[static_cast<std::size_t>(id)].name);
    }

    const Config& defaults() const { return defaults_; }

private:
    friend class ConfigOwner<Config>;
    friend ApplyReport ApplyCanonical<Config>(const CanonicalIni&, const ConfigTable&, Config&);
    friend std::string RenderCanonical<Config>(const ConfigTable&, const Config&, const RenderHeader&);
    friend std::string RenderCanonicalFresh<Config>(const ConfigTable&, const RenderHeader&);
    friend detail::EffectiveApplyResult detail::ApplyCanonicalEffective<Config>(const CanonicalIni&, const ConfigTable&,
                                                                                Config&, const Config&,
                                                                                const std::vector<schema::Concept>&);
    friend std::string detail::RenderCanonicalMigration<Config>(const ConfigTable&, const Config&, const Config&,
                                                                const RenderHeader&);
    friend std::string detail::RenderCanonicalValues<Config>(const ConfigTable&, const Config&,
                                                             const std::vector<std::string>&);

    template <class Field>
    static auto MemberGetter(Field Config::*field) {
        return [field](const Config& config) -> const Field& { return config.*field; };
    }

    template <class Field>
    static auto MemberSetter(Field Config::*field) {
        return [field](Config& config, Field value) { config.*field = std::move(value); };
    }

    template <schema::Concept Id, class Field>
    static auto ConceptCodec() {
        using Traits = schema::ConceptTraits<Id>;
        if constexpr (Traits::kFamily == schema::ValueFamily::kBool) {
            return BoolCodec{};
        } else if constexpr (Traits::kFamily == schema::ValueFamily::kInteger) {
            return IntCodec<Field>(static_cast<Field>(Traits::kMin), static_cast<Field>(Traits::kMax));
        } else if constexpr (Traits::kFamily == schema::ValueFamily::kFloating) {
            return FloatingCodec<Field>(static_cast<Field>(Traits::kMin), static_cast<Field>(Traits::kMax));
        } else {
            return HotkeyCodec{};
        }
    }

    template <schema::Concept Id, class Get, class Set>
    ConfigTable& AddConcept(Get get, Set set) {
        using Field = std::decay_t<std::invoke_result_t<Get&, const Config&>>;
        static_assert(FieldHoldsConcept<Id, Field>(),
                      "the field's type does not hold this concept: a bool concept needs bool, an integer concept "
                      "an integral type other than bool whose limits hold the concept's range, a floating concept "
                      "float or double, a hotkey concept std::string");
        static_assert(std::is_invocable_v<Set&, Config&, Field>, "the setter takes (Config&, the field's type)");
        using Codec = decltype(ConceptCodec<Id, Field>());

        const schema::ConceptInfo& info = schema::kConcepts[static_cast<std::size_t>(Id)];
        detail::TableRow row;
        row.section = info.section;
        row.key = info.key;
        row.comment.assign(info.file_comment, info.file_comment + info.file_comment_lines);
        row.concept_id = Id;
        row.hotkey = info.family == schema::ValueFamily::kHotkey;
        detail::CheckConceptRow(rows_, row);
        auto ops = std::make_shared<detail::CodecRow<Config, Codec, Get, Set>>(ConceptCodec<Id, Field>(),
                                                                               std::move(get), std::move(set));
        if constexpr (Id == schema::Concept::RotationEnabled || Id == schema::Concept::PositionEnabled) {
            constexpr schema::Concept kOther = Id == schema::Concept::RotationEnabled
                                                   ? schema::Concept::PositionEnabled
                                                   : schema::Concept::RotationEnabled;
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].concept_id == kOther && ops_[i]->IsFalse(defaults_) && ops->IsFalse(defaults_)) {
                    throw std::invalid_argument(
                        "RotationEnabled and PositionEnabled both default to false, which is not a tracking mode");
                }
            }
        }
        Add(std::move(row), std::move(ops));
        return *this;
    }

    // A row joins the table only once every check has passed, so a table a check threw from
    // is left as it was.
    void Add(detail::TableRow row, std::shared_ptr<const detail::RowOps<Config>> ops) {
        CheckDefault(row, *ops);
        rows_.push_back(std::move(row));
        ops_.push_back(std::move(ops));
        last_ = rows_.size() - 1;
    }

    void CheckDefault(const detail::TableRow& row, const detail::RowOps<Config>& ops) const {
        try {
            ops.Render(defaults_);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(detail::RowName(row) + " has a default it cannot write: " + e.what());
        }
    }

    std::size_t Last(const char* modifier) const {
        if (!last_) throw std::invalid_argument(std::string(modifier) + " needs a row: add or Select one first");
        return *last_;
    }

    Config defaults_;
    std::vector<detail::TableRow> rows_;
    std::vector<std::shared_ptr<const detail::RowOps<Config>>> ops_;
    std::optional<std::size_t> last_;
};

namespace detail {

template <class Config>
using RowOpsList = std::vector<std::shared_ptr<const RowOps<Config>>>;

template <class Config>
class ConfigRowTarget final : public RowTarget {
public:
    ConfigRowTarget(const RowOpsList<Config>& ops, const std::vector<const Config*>& starts, Config& config)
        : ops_(ops), starts_(starts), config_(config) {}
    std::string Apply(std::size_t row, std::string_view text) override { return ops_[row]->Apply(text, config_); }
    bool IsFalse(std::size_t row) const override { return ops_[row]->IsFalse(config_); }
    void ResetToStart(std::size_t row) override { ops_[row]->Assign(config_, *starts_[row]); }

private:
    const RowOpsList<Config>& ops_;
    const std::vector<const Config*>& starts_;
    Config& config_;
};

template <class Config>
class ConfigRowSource final : public RowSource {
public:
    ConfigRowSource(const std::vector<TableRow>& rows, const RowOpsList<Config>& ops, const Config& defaults,
                    const Config& values)
        : rows_(rows), ops_(ops), defaults_(defaults), values_(values) {}
    std::string Render(std::size_t row) const override {
        try {
            return ops_[row]->Render(values_);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(RowName(rows_[row]) + ": " + e.what());
        }
    }
    bool EqualsDefault(std::size_t row) const override { return ops_[row]->Equal(values_, defaults_); }

private:
    const std::vector<TableRow>& rows_;
    const RowOpsList<Config>& ops_;
    const Config& defaults_;
    const Config& values_;
};

/// ApplyCanonical over effective defaults. A global concept row that is not PerGame starts from its
/// value in `effective`, every other row from the table's defaults; `default`, a missing key and an
/// invalid value leave a row at its start, and a pair naming no tracking mode takes both starts.
/// `from_defaults_ini` names the concepts whose effective default Defaults.ini gave, which the
/// result reports as the source of such a row left at its start.
///
/// Throws std::invalid_argument for a document that is not readable, a table that marks one of
/// RotationEnabled and PositionEnabled PerGame and not the other, a concept in `from_defaults_ini`
/// that is not a row of the table following Defaults.ini, and starts of RotationEnabled and
/// PositionEnabled that are both false.
template <class Config>
EffectiveApplyResult ApplyCanonicalEffective(const CanonicalIni& doc, const ConfigTable<Config>& table, Config& inout,
                                             const Config& effective,
                                             const std::vector<schema::Concept>& from_defaults_ini) {
    const std::vector<TableRow>& rows = table.rows_;
    const RowOpsList<Config>& ops = table.ops_;
    CheckPairPerGame(rows);
    std::vector<const Config*> starts;
    std::vector<ValueSource> start_sources(rows.size(), ValueSource::kBuiltIn);
    for (const TableRow& row : rows) starts.push_back(FollowsDefaultsIni(row) ? &effective : &table.defaults_);
    for (const schema::Concept id : from_defaults_ini) {
        std::size_t found = rows.size();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].concept_id == id && FollowsDefaultsIni(rows[i])) found = i;
        }
        if (found == rows.size()) {
            throw std::invalid_argument(std::string(schema::kConcepts[static_cast<std::size_t>(id)].name) +
                                        " is not a row of this table that follows Defaults.ini");
        }
        start_sources[found] = ValueSource::kDefaultsIni;
    }
    std::size_t rotation = rows.size();
    std::size_t position = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].concept_id == schema::Concept::RotationEnabled) rotation = i;
        if (rows[i].concept_id == schema::Concept::PositionEnabled) position = i;
    }
    if (rotation < rows.size() && position < rows.size() && ops[rotation]->IsFalse(*starts[rotation]) &&
        ops[position]->IsFalse(*starts[position])) {
        throw std::invalid_argument("RotationEnabled and PositionEnabled both start false, which is not a tracking mode");
    }
    ConfigRowTarget<Config> target(ops, starts, inout);
    return ApplyRows(doc, rows, target, start_sources);
}

/// Writes a migrated file: RenderCanonical of `values`, except that a global concept row that is not
/// PerGame is written `Key=default` when its value equals its value in `effective`, and otherwise
/// as its value, never in the commented form of an Engine row, which would read back as the
/// effective default. RotationEnabled and PositionEnabled are written default only when both equal
/// their effective values. Throws as RenderCanonical, and std::invalid_argument for a table that
/// marks one of RotationEnabled and PositionEnabled PerGame and not the other.
template <class Config>
std::string RenderCanonicalMigration(const ConfigTable<Config>& table, const Config& values, const Config& effective,
                                     const RenderHeader& header) {
    const std::vector<TableRow>& rows = table.rows_;
    CheckPairPerGame(rows);
    std::vector<RowForm> forms(rows.size(), RowForm::kAsRender);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (FollowsDefaultsIni(rows[i])) {
            forms[i] = table.ops_[i]->Equal(values, effective) ? RowForm::kDefault : RowForm::kValue;
        }
    }
    std::size_t rotation = rows.size();
    std::size_t position = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].concept_id == schema::Concept::RotationEnabled) rotation = i;
        if (rows[i].concept_id == schema::Concept::PositionEnabled) position = i;
    }
    if (rotation < rows.size() && position < rows.size() &&
        (forms[rotation] == RowForm::kValue || forms[position] == RowForm::kValue)) {
        if (forms[rotation] == RowForm::kDefault) forms[rotation] = RowForm::kValue;
        if (forms[position] == RowForm::kDefault) forms[position] = RowForm::kValue;
    }
    const ConfigRowSource<Config> source(rows, table.ops_, table.defaults_, values);
    return RenderRows(rows, GameFileHeader(rows, header), source, forms);
}

/// Writes `values` as RenderCanonical does, except that every row is written as its value, an
/// Engine row at its default included, and the header is `header`, each line written as it is.
/// Throws std::invalid_argument, naming the row, for a value its codec cannot write.
template <class Config>
std::string RenderCanonicalValues(const ConfigTable<Config>& table, const Config& values,
                                  const std::vector<std::string>& header) {
    const ConfigRowSource<Config> source(table.rows_, table.ops_, table.defaults_, values);
    return RenderRows(table.rows_, header, source, std::vector<RowForm>(table.rows_.size(), RowForm::kValue));
}

}  // namespace detail

/// Reads a parsed canonical file into the table's rows of `inout`; fields no row binds are left
/// as they are. Every row starts from its default, so a key the file leaves out reads as the
/// default with no diagnostic, and so does `default` on a concept row: the value, after the
/// reader's trimming, equal to `default` in any ASCII letter case. On a local row the word is a
/// value like any other. A value its codec does not read keeps the default and draws
/// InvalidValue. A section the table has no row in draws one UnknownSection, a key no row of a
/// read section names one UnknownKey, and none is drawn in [CameraUnlock]. A key that names a
/// row of the table in another section, or a concept row by an alias, draws MisplacedKey naming
/// the row; one that names a retired concept draws RetiredKey, and one that names a concept the
/// canonical format does not write, or is a spelling the schema's non_canonical_keys lists (a
/// deadzone, a response curve), draws NonCanonicalConcept with the schema's reason; all three in
/// any section. Any other key in a section a non_canonical_keys group lists ([Sensitivity],
/// [Inversion], [Deadzone]) draws NonCanonicalConcept with that group's reason, beside the
/// section's UnknownSection. No key takes its value from another. When the table binds RotationEnabled and
/// PositionEnabled and both read false, both take their defaults and one NoTrackingMode names the
/// lines that set them.
///
/// Throws std::invalid_argument for a document that is not readable, and for a table that marks
/// one of RotationEnabled and PositionEnabled PerGame and not the other.
template <class Config>
ApplyReport ApplyCanonical(const CanonicalIni& doc, const ConfigTable<Config>& table, Config& inout) {
    return detail::ApplyCanonicalEffective(doc, table, inout, table.defaults_, {}).report;
}

/// Writes `values` as a canonical file: the header comments, [CameraUnlock]
/// with ConfigFormat, the schema sections the table has rows in, in the schema's order, each
/// with its concept rows in the schema's concepts order and then its local rows in table order,
/// then the local sections in table order. Each row is its comment lines as `; text`, then
/// `Key=value`, or `; Key=value` for an Engine row holding its default. A blank line separates
/// sections. CRLF line endings with a final CRLF, no byte order mark. When the table has a global
/// concept row that is not PerGame, six header lines say what `default` means and where
/// Defaults.ini is.
///
/// Throws std::invalid_argument, naming the row, for a value its codec cannot write, and for a
/// display name that is empty, has a leading or trailing space, or holds a byte outside
/// printable ASCII.
template <class Config>
std::string RenderCanonical(const ConfigTable<Config>& table, const Config& values, const RenderHeader& header) {
    const detail::ConfigRowSource<Config> source(table.rows_, table.ops_, table.defaults_, values);
    return detail::RenderRows(table.rows_, detail::GameFileHeader(table.rows_, header), source,
                              std::vector<detail::RowForm>(table.rows_.size(), detail::RowForm::kAsRender));
}

/// Writes the file a game starts with: RenderCanonical of the defaults, except that every global
/// concept row that is not PerGame is written `Key=default`, an Engine row included. Every other
/// row is written as RenderCanonical writes it, so the row of a concept that is not global holds the
/// game's own default, commented only when it is an Engine row.
///
/// Throws std::invalid_argument when a global concept row that is not PerGame defaults to a value other
/// than the schema's, naming the row; when the table binds RotationEnabled without
/// PositionEnabled, or marks one of them PerGame and not the other; and for a display name
/// RenderCanonical refuses.
template <class Config>
std::string RenderCanonicalFresh(const ConfigTable<Config>& table, const RenderHeader& header) {
    const std::vector<detail::TableRow>& rows = table.rows_;
    std::vector<detail::RowForm> forms(rows.size(), detail::RowForm::kAsRender);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!detail::FollowsDefaultsIni(rows[i])) continue;
        std::string schema_default;
        const bool holds = table.ops_[i]->Holds(
            table.defaults_, schema::kConcepts[static_cast<std::size_t>(*rows[i].concept_id)].default_text, schema_default);
        detail::CheckFreshRow(rows[i], holds, table.ops_[i]->Render(table.defaults_), schema_default);
        forms[i] = detail::RowForm::kDefault;
    }
    detail::CheckFreshPair(rows);
    detail::CheckPairPerGame(rows);
    const detail::ConfigRowSource<Config> source(rows, table.ops_, table.defaults_, table.defaults_);
    return detail::RenderRows(rows, detail::GameFileHeader(rows, header), source, forms);
}

}  // namespace cameraunlock::config
