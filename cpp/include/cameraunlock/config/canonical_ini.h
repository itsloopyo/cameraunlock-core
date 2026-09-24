#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::config {

/// The canonical INI dialect this build reads and writes, as `[CameraUnlock] ConfigFormat`.
inline constexpr int kConfigFormat = 1;

/// Whether ParseCanonicalIni interpreted the document. The numbers match
/// CameraUnlock.Core.Config.CanonicalReadStatus.
enum class CanonicalReadStatus {
    Readable = 0,
    /// The bytes start with a UTF-16 byte order mark, FF FE or FE FF.
    Utf16 = 1,
    /// The bytes hold a 0x00 byte, which also catches UTF-16 without a mark.
    NulByte = 2,
};

/// What a CanonicalDiagnostic reports. The numbers match
/// CameraUnlock.Core.Config.CanonicalDiagnosticKind. New kinds are appended; a number is
/// never reused or changed.
enum class CanonicalDiagnosticKind {
    /// Text after a section header's ']' is ignored. `section` is the header's name,
    /// `value` the ignored text.
    TextAfterSectionHeader = 1,
    /// A '[' line with no ']'. It ends the section above and names none, so every key
    /// up to the next header is reported as KeyOutsideSection. `value` is the line.
    UnclosedSectionHeader = 2,
    /// A header whose name is empty. Nothing matches it, so every key up to the next
    /// header is reported as KeyOutsideSection. `value` is the line.
    EmptySectionName = 3,
    /// A key line with nothing before its '='. `value` is the line.
    EmptyKey = 4,
    /// A line that is not blank, a comment or a header, and has no '='. `value` is the line.
    MissingEquals = 5,
    /// A key line above the first header, or below one that names no section. `key` and
    /// `value` are the line's.
    KeyOutsideSection = 6,
    /// A key given more than once in one section, counting every header of that name.
    /// The last occurrence is kept. `lines` names every occurrence in order; `section`,
    /// `key` and `value` are the kept ones.
    DuplicateKey = 7,
    /// `[CameraUnlock]` has no ConfigFormat, so the file is read as kConfigFormat. The
    /// line is the section's first header.
    ConfigFormatMissing = 8,
    /// ConfigFormat is not a format number: digits only, 1 or more. The file is read as
    /// kConfigFormat. `key` and `value` are the line's.
    ConfigFormatInvalid = 9,
    /// ConfigFormat is above kConfigFormat. It is kept as read (saturated at INT_MAX),
    /// and `key` and `value` are the line's.
    ConfigFormatNewer = 10,
    /// ApplyCanonical: a value its row's codec does not read. The row keeps its default.
    /// `section`, `key` and `value` are the line's; `detail` is what the codec expected.
    InvalidValue = 11,
    /// ApplyCanonical: a section the table has no row in, so nothing in it is read.
    /// `section` is its name; the line is its first header's.
    UnknownSection = 12,
    /// ApplyCanonical: a key in a section the table reads that no row names and that is
    /// no retired or non-canonical concept, so it is not read. `section`, `key` and
    /// `value` are the line's.
    UnknownKey = 13,
    /// ApplyCanonical: a key naming a retired concept (the schema's `retired` list), in
    /// any section but [CameraUnlock]. It is not read. `section`, `key` and `value` are
    /// the line's.
    RetiredKey = 14,
    /// ApplyCanonical: a key naming a concept the canonical format does not write, in any
    /// section but [CameraUnlock]. It is not read. `section`, `key` and `value` are the
    /// line's; `detail` is the schema's canonical_reason, the line saying why.
    NonCanonicalConcept = 15,
    /// ApplyCanonical: RotationEnabled and PositionEnabled are both false, which is no
    /// tracking mode, so both take the table's defaults. `lines` are the lines that set
    /// them.
    NoTrackingMode = 16,
};

/// One finding about the document. Returned, never logged, so a caller can read the file
/// before its logger exists and report afterwards. Every diagnostic names at least one
/// line. `section`, `key` and `value` hold the file's own bytes, and every text field is
/// empty where the kind above does not use it.
struct CanonicalDiagnostic {
    CanonicalDiagnosticKind kind = CanonicalDiagnosticKind::TextAfterSectionHeader;
    /// 1-based, ascending.
    std::vector<int> lines;
    std::string section;
    std::string key;
    std::string value;
    /// The kind's explanation, where the kind above names one.
    std::string detail;
};

/// One key of a section: its last occurrence.
struct CanonicalValue {
    /// Spelled as on `line`.
    std::string key;
    /// The raw bytes after '=', trimmed of spaces and tabs. May be empty.
    std::string value;
    /// 1-based.
    int line = 0;
    /// Every earlier occurrence of the key in the section, ascending.
    std::vector<int> earlier_lines;
};

struct CanonicalSection {
    /// Spelled as its first header spells it.
    std::string name;
    /// In the order each key first occurs.
    std::vector<CanonicalValue> values;
    /// 1-based line of its first header.
    int line = 0;

    /// The key compared ASCII case-insensitively, or nullptr.
    const CanonicalValue* Find(std::string_view key) const;
};

/// A canonical INI document at the level of sections and keys. Values are raw bytes;
/// codecs decide what they mean.
struct CanonicalIni {
    CanonicalReadStatus status = CanonicalReadStatus::Readable;
    /// For NulByte, the 1-based line holding the first NUL. 0 otherwise.
    int unreadable_line = 0;
    /// `[CameraUnlock] ConfigFormat` as read, kConfigFormat when the section, the key or
    /// a valid number is missing, and 0 when the document is unreadable.
    int format_version = 0;
    /// Repeated headers of one name are one section. In the order each first occurs.
    /// Empty when the document is unreadable.
    std::vector<CanonicalSection> sections;
    /// Ordered by first line, then kind. Empty when the document is unreadable.
    std::vector<CanonicalDiagnostic> diagnostics;

    bool IsReadable() const { return status == CanonicalReadStatus::Readable; }

    /// The section compared ASCII case-insensitively, or nullptr.
    const CanonicalSection* FindSection(std::string_view name) const;
    /// The key in the section, both compared ASCII case-insensitively, or nullptr.
    const CanonicalValue* Find(std::string_view section, std::string_view key) const;
};

/// Reads a canonical INI document from its bytes. Pure: no file I/O, never throws for
/// any input.
///
/// A document starting with a UTF-16 byte order mark or holding a NUL is unreadable and
/// nothing in it is interpreted. Otherwise: CRLF, LF and a lone CR each end a line; a
/// UTF-8 byte order mark at offset 0 is skipped; each line is trimmed of spaces and tabs
/// only. Blank lines and lines starting ';' or '#' are skipped. A line starting '[' is a
/// section header named by the text up to its first ']', trimmed. Any other line is a
/// key line split at its first '=', key and value trimmed. There are no inline comments,
/// quotes or escapes: `B=true ; c` gives the value `true ; c`. Every other byte,
/// 0x1A and bytes above 0x7F included, is an ordinary byte. Names compare ASCII
/// case-insensitively and nothing else is folded.
///
/// `[CameraUnlock]` belongs to core. Its ConfigFormat sets format_version; its other
/// keys are reserved for core.
CanonicalIni ParseCanonicalIni(std::string_view bytes);

/// True when some line opens a section named CameraUnlock under ParseCanonicalIni's
/// header rule, so `[CameraUnlock] ; note` counts and `; [CameraUnlock]` does not. When
/// the bytes start with a UTF-16 byte order mark they are searched in their UTF-16
/// decoding by that mark, so a canonical file re-saved as UTF-16 is still recognised.
/// A NUL does not hide the stamp: a stamped file can be unreadable.
bool HasCanonicalStamp(std::string_view bytes);

/// The enumerator's name, e.g. "NulByte", as data/fixtures/canonical-ini spells it.
const char* CanonicalReadStatusName(CanonicalReadStatus status);
/// The enumerator's name, e.g. "DuplicateKey", as data/fixtures/canonical-ini spells it.
const char* CanonicalDiagnosticKindName(CanonicalDiagnosticKind kind);

/// One sentence for the player, naming the line or lines, e.g.
/// `[General] ToggleKey is set on lines 3 and 9. Line 9 is used.`
std::string DescribeCanonicalDiagnostic(const CanonicalDiagnostic& diagnostic);

}  // namespace cameraunlock::config
