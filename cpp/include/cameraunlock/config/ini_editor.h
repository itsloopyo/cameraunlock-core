#pragma once

#include <string>
#include <vector>

namespace cameraunlock {

/// One change to an INI document: give `key` in `[section]` the value `value`.
///
/// Section and key match ASCII case-insensitively, and a replaced line keeps the
/// file's own spelling. When the key is absent and `insert_if_absent` is set, the line
/// is added as `key=value`, and a missing section as `[section]`, spelled exactly as
/// given here. The value is written verbatim, so it must be one the flat readers read
/// back unchanged (see EditIni).
///
/// Set `first_occurrence_wins` for a reader that takes the first occurrence of a key,
/// GetPrivateProfileStringA for one. A key that appears more than once in its section,
/// counting every header of that name, then has its first occurrence in the document
/// replaced and the others left as they are, and an absent key goes under the first of
/// several headers of its section. Unset, those edits are refused as DuplicateKey and
/// DuplicateSection.
struct IniEdit {
    std::string section;
    std::string key;
    std::string value;
    bool insert_if_absent = false;
    bool first_occurrence_wins = false;
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
    /// The edit asks for an absent key to be inserted, and its section header
    /// appears more than once, so there is no one place to put it unless the edit
    /// says the first occurrence wins.
    DuplicateSection = 5,
    /// The edit's key appears more than once in its section, counting every header
    /// of that name. Which occurrence counts depends on the reader, so the caller
    /// decides with its own: IniEdit::first_occurrence_wins, or no edit.
    DuplicateKey = 6,
    /// The key is absent and the edit did not ask for it to be inserted.
    KeyNotFound = 7,
    /// A line starts with white space other than a space or tab (a form feed,
    /// vertical tab, no-break space and the like), or a key ends in it. The C# flat
    /// reader trims it and the C++ one does not, so the line has no single reading
    /// to edit against.
    AmbiguousWhitespace = 8,
    /// A SUB byte (0x1A, Ctrl-Z). ParseIniConfig reads through a text-mode stream, which
    /// the Microsoft C runtime ends at that byte, and the C# flat reader reads past it, so
    /// nothing after it has a single reading.
    SubByte = 9,
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
    /// the line holding the first offending byte for an encoding refusal, every
    /// offending line for AmbiguousWhitespace.
    std::vector<int> lines;

    bool Succeeded() const { return refusal == IniEditRefusal::None; }
};

/// Applies a batch of edits to the bytes of an INI document, touching nothing else.
/// Pure: no file I/O. An absent file is an empty `original`.
///
/// The document is refused whole, with no bytes produced, when it is UTF-16, invalid
/// UTF-8, holds a NUL, a SUB (0x1A) or a lone CR, or when any edit is ambiguous or
/// missing. A UTF-8 byte order mark is kept.
///
/// Lines are read the way the flat readers read them. Leading and trailing spaces and
/// tabs are ignored; a line starting ';' or '#' is a comment; one starting '[' is a
/// section header named by the text up to the first ']'; otherwise the first '=' with
/// text before it makes a key line. Keys before the first header belong to no section
/// and are never matched. Headers that repeat a section's name make one section: a key
/// under any of them is that section's key.
///
/// The C# flat reader trims every white-space character off a line and its key, the C++
/// one only spaces and tabs. A line that starts with any other white space, or a key
/// that ends in it, reads differently in the two, so the document is refused as
/// AmbiguousWhitespace.
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
/// Throws std::invalid_argument for an edit that cannot be written so that it reads
/// back as given: a section or key that is empty, has surrounding white space or holds
/// CR, LF, NUL, SUB or invalid UTF-8, a section holding ']', a key holding '=' or
/// starting '[', ';' or '#', a value holding CR, LF, NUL, SUB or invalid UTF-8, a value
/// a flat reader would read as something else (surrounding white space, a ';' or '#'
/// outside quotes, a quote left open, or one pair of matching quotes around the whole
/// of it), and two edits of the same key.
IniEditResult EditIni(const std::string& original, const std::vector<IniEdit>& edits);

}  // namespace cameraunlock
