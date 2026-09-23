#pragma once

#include <string>
#include <vector>

namespace cameraunlock {

/// One change to an INI document: give `key` in `[section]` the value `value`.
///
/// Section and key match ASCII case-insensitively, and a replaced line keeps the
/// file's own spelling. When the key is absent and `insert_if_absent` is set, the line
/// is added as `key=value`, and a missing section as `[section]`, spelled exactly as
/// given here. The value is written verbatim.
struct IniEdit {
    std::string section;
    std::string key;
    std::string value;
    bool insert_if_absent = false;
};

/// Why EditIni produced no document. The numbers match CameraUnlock.Core.Config's
/// IniEditRefusal.
enum class IniEditRefusal {
    None = 0,
    /// The document starts with a UTF-16 byte order mark, either byte order.
    Utf16 = 1,
    InvalidUtf8 = 2,
    NulByte = 3,
    /// A CR not followed by LF. The C# and C++ flat readers split such a line
    /// differently, so no edit of it can be checked against both.
    LoneCarriageReturn = 4,
    /// The edit's section header appears more than once.
    DuplicateSection = 5,
    /// The edit's key appears more than once in its section. Which occurrence
    /// counts depends on the reader, so the caller decides with its own.
    DuplicateKey = 6,
    /// The key is absent and the edit did not ask for it to be inserted.
    KeyNotFound = 7,
};

/// The spelling the shared fixtures under data/fixtures/ini-editor use, e.g. "DuplicateKey".
const char* IniEditRefusalName(IniEditRefusal refusal);

struct IniEditResult {
    IniEditRefusal refusal = IniEditRefusal::None;
    /// The edited document. Empty when refused.
    std::string bytes;
    /// The refused edit's section and key, as the caller spelled them. Empty for a
    /// refusal of the whole document.
    std::string section;
    std::string key;
    /// 1-based line numbers the refusal is about: every occurrence for a duplicate,
    /// the line holding the first offending byte for an encoding refusal.
    std::vector<int> lines;

    bool Succeeded() const { return refusal == IniEditRefusal::None; }
};

/// Applies a batch of edits to the bytes of an INI document, touching nothing else.
/// Pure: no file I/O. An absent file is an empty `original`.
///
/// The document is refused whole, with no bytes produced, when it is UTF-16, invalid
/// UTF-8, holds a NUL or a lone CR, or when any edit is ambiguous or missing. A UTF-8
/// byte order mark is kept.
///
/// Lines are read the way the flat readers read them. Leading and trailing spaces and
/// tabs are ignored; a line starting ';' or '#' is a comment; one starting '[' is a
/// section header named by the text up to the first ']'; otherwise the first '=' with
/// text before it makes a key line. Keys before the first header belong to no section
/// and are never matched.
///
/// A replacement rewrites only the value: the text after '=' and its whitespace, up to
/// an inline ';' or '#' outside quotes, less the whitespace before that comment.
/// Everything else on the line, its terminator included, is kept byte for byte.
///
/// An insertion goes after the section's last line that is neither blank nor a comment,
/// so a comment block introducing the next section stays with that section. A missing
/// section is appended at the end of the file after a blank line. New lines end with
/// the file's dominant line ending, CRLF when the counts tie. A file whose last line
/// has no terminator still ends without one: the new text is joined on with a single
/// line ending in front instead of behind.
///
/// Throws std::invalid_argument for an edit that cannot be written as one line: a
/// section or key that is empty, has surrounding whitespace or holds CR, LF, NUL or
/// invalid UTF-8, a section holding ']', a key holding '=' or starting '[', ';' or '#',
/// a value holding CR, LF, NUL or invalid UTF-8, and two edits of the same key.
IniEditResult EditIni(const std::string& original, const std::vector<IniEdit>& edits);

}  // namespace cameraunlock
