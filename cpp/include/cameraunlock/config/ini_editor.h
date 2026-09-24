#pragma once

#include <string>
#include <vector>

namespace cameraunlock {

/// One change to a canonical INI document: give `key` in `[section]` the value `value`.
///
/// Section and key match ASCII case-insensitively, and a replaced line keeps the file's
/// own spelling. When the key is absent and `insert_if_absent` is set, the line is added
/// as `key=value`, and a missing section as `[section]`, spelled exactly as given here.
/// Section, key and value are printable ASCII (see EditIni).
///
/// By default an edit follows the canonical reader, which keeps the last occurrence of a
/// repeated key: that occurrence is replaced, and an absent key goes into the last block
/// of a section whose header repeats. Set `first_occurrence_wins` for a reader that
/// takes the first occurrence instead, GetPrivateProfileStringA for one: the first
/// occurrence in the document is replaced, and an absent key goes into the first block.
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
    /// The document starts with a UTF-16 byte order mark, either byte order. The
    /// canonical reader cannot read it.
    Utf16 = 1,
    /// The document holds a 0x00 byte. The canonical reader cannot read it.
    NulByte = 2,
    /// The key is absent and the edit did not ask for it to be inserted.
    KeyNotFound = 3,
};

/// The spelling the shared fixtures under data/fixtures/canonical-ini/editor use, e.g.
/// "KeyNotFound".
const char* IniEditRefusalName(IniEditRefusal refusal);

struct IniEditResult {
    IniEditRefusal refusal = IniEditRefusal::None;
    /// The edited document. Empty when refused.
    std::string bytes;
    /// The refused edit's section and key, as the caller spelled them. Empty for a
    /// refusal of the whole document.
    std::string section;
    std::string key;
    /// For NulByte, the 1-based line holding the first NUL, counted as the canonical
    /// reader counts lines. Empty otherwise.
    std::vector<int> lines;

    bool Succeeded() const { return refusal == IniEditRefusal::None; }
};

/// Applies a batch of edits to the bytes of a canonical INI document, touching nothing
/// else. Pure: no file I/O. An absent file is an empty `original`.
///
/// The editor never decodes. It reads lines as ParseCanonicalIni does: CRLF, LF and a
/// lone CR each end a line; a UTF-8 byte order mark at offset 0 is skipped, and kept in
/// the output; each line is trimmed of spaces and tabs only; a line starting ';' or '#'
/// is a comment; one starting '[' is a section header named by the text up to its first
/// ']', and one with no ']' ends the section above it and names none; any other line
/// with text before its first '=' is a key line. Keys above the first header belong to
/// no section and are never matched. Headers that repeat a section's name make one
/// section. An edit's key is ASCII, so it never matches a key holding any other byte.
/// Every byte the edits do not change is copied through, whatever its encoding.
///
/// The document is refused whole, with no bytes produced, when it starts with a UTF-16
/// byte order mark or holds a NUL, which the canonical reader cannot read either, or
/// when an edit's key is absent and not to be inserted.
///
/// A replacement rewrites everything after the '=' and the spaces and tabs after it, up
/// to the line terminator: there are no inline comments. The key's spelling, the white
/// space around '=' and the terminator are kept.
///
/// An insertion goes after the last key line of the section's last block, or of its
/// first block when the edit says the first occurrence wins, and straight after the
/// header when that block has no key line. A missing section is appended at the end of
/// the file after a blank line. New lines end with the file's most common line ending,
/// CRLF on a tie and then LF. A file whose last line has no terminator still ends
/// without one: the new text is joined on with a line ending in front instead of behind.
///
/// Throws std::invalid_argument for an edit that cannot be written so that it reads back
/// as given: a section, key or value holding a byte outside printable ASCII (0x20 to
/// 0x7E), an empty section or key, a section, key or value with a leading or trailing
/// space, a section holding ']', a key holding '=' or starting '[', ';' or '#', and two
/// edits of the same key. A value may be empty.
IniEditResult EditIni(const std::string& original, const std::vector<IniEdit>& edits);

}  // namespace cameraunlock
