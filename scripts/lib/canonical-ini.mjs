// The canonical INI grammar (design 2.2 items 1-7), for core's scripts. The same rules as
// C++ ParseCanonicalIni / HasCanonicalStamp and C# CanonicalIni.Parse / HasStamp, held to
// the same fixtures: scripts/test-canonical-config.mjs runs data/fixtures/canonical-ini/reader/
// through this file in `pixi run check`.
//
// Documents are bytes (a Buffer or Uint8Array). Names and values come back as latin1 strings,
// one character per byte, so nothing is decoded and every byte survives as it was.

export const CONFIG_FORMAT = 1;
export const STAMP_SECTION = "CameraUnlock";
export const FORMAT_KEY = "ConfigFormat";
// A token on concept rows only, where it leaves the row at its default: Defaults.ini's value when
// the row is not PerGame() and Defaults.ini supplies one, the table's default otherwise.
export const DEFAULT_TOKEN = "default";

export const DIAGNOSTIC_KINDS = Object.freeze({
  TextAfterSectionHeader: 1,
  UnclosedSectionHeader: 2,
  EmptySectionName: 3,
  EmptyKey: 4,
  MissingEquals: 5,
  KeyOutsideSection: 6,
  DuplicateKey: 7,
  ConfigFormatMissing: 8,
  ConfigFormatInvalid: 9,
  ConfigFormatNewer: 10,
});

const INT_MAX = 2147483647;

export function trimSpaceTab(s) {
  let begin = 0;
  let end = s.length;
  while (begin < end && (s[begin] === " " || s[begin] === "\t")) begin++;
  while (end > begin && (s[end - 1] === " " || s[end - 1] === "\t")) end--;
  return s.slice(begin, end);
}

function foldAscii(s) {
  return s.replace(/[A-Z]/g, (c) => String.fromCharCode(c.charCodeAt(0) + 32));
}

export function equalsAsciiIgnoreCase(a, b) {
  return a.length === b.length && foldAscii(a) === foldAscii(b);
}

// `value` as the reader returns it, already trimmed of spaces and tabs.
export function isDefaultToken(value) {
  return equalsAsciiIgnoreCase(value, DEFAULT_TOKEN);
}

function toLatin1(bytes) {
  return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength).toString("latin1");
}

function startsWithUtf16Mark(text) {
  return text.length >= 2 && ((text[0] === "\xFF" && text[1] === "\xFE") || (text[0] === "\xFE" && text[1] === "\xFF"));
}

// Every line of the document after a leading UTF-8 byte order mark: `text` without its
// terminator, `ending` the terminator ("\r\n", "\n", "\r" or "" for an unterminated last
// line), `number` 1-based.
export function splitLines(bytes) {
  const text = toLatin1(bytes);
  const lines = [];
  let pos = text.startsWith("\xEF\xBB\xBF") ? 3 : 0;
  let number = 0;
  while (pos < text.length) {
    let end = pos;
    while (end < text.length && text[end] !== "\r" && text[end] !== "\n") end++;
    let next;
    let ending;
    if (end < text.length && text[end] === "\r" && end + 1 < text.length && text[end + 1] === "\n") {
      next = end + 2;
      ending = "\r\n";
    } else {
      next = end + 1;
      ending = end < text.length ? text[end] : "";
    }
    lines.push({ text: text.slice(pos, end), ending, number: ++number });
    pos = next;
  }
  return lines;
}

function lineOfOffset(text, offset) {
  let line = 1;
  for (let i = 0; i < offset; i++) {
    if (text[i] === "\n" || (text[i] === "\r" && (i + 1 >= text.length || text[i + 1] !== "\n"))) line++;
  }
  return line;
}

// `line` is trimmed and starts with '['.
function parseHeader(line) {
  const close = line.indexOf("]", 1);
  if (close < 0) return { closed: false, name: "", trailing: "" };
  return { closed: true, name: trimSpaceTab(line.slice(1, close)), trailing: trimSpaceTab(line.slice(close + 1)) };
}

function hasStampInLines(lines) {
  for (const { text } of lines) {
    const line = trimSpaceTab(text);
    if (line === "" || line[0] !== "[") continue;
    const header = parseHeader(line);
    if (header.closed && equalsAsciiIgnoreCase(header.name, STAMP_SECTION)) return true;
  }
  return false;
}

// Every UTF-16 code unit above 0x7F becomes 0x80, which neither the header rule nor the
// name CameraUnlock treats specially. An odd last byte is not a unit and is dropped.
function narrowUtf16(text) {
  const littleEndian = text[0] === "\xFF";
  let narrow = "";
  for (let i = 2; i + 1 < text.length; i += 2) {
    const lo = text.charCodeAt(littleEndian ? i : i + 1);
    const hi = text.charCodeAt(littleEndian ? i + 1 : i);
    const unit = (hi << 8) | lo;
    narrow += unit < 0x80 ? String.fromCharCode(unit) : "\x80";
  }
  return narrow;
}

export function hasCanonicalStamp(bytes) {
  const text = toLatin1(bytes);
  if (startsWithUtf16Mark(text)) return hasStampInLines(splitLines(Buffer.from(narrowUtf16(text), "latin1")));
  return hasStampInLines(splitLines(bytes));
}

function diagnostic(kind, line, section, key, value) {
  return { kind, lines: [line], section, key, value };
}

function parseFormatNumber(value) {
  if (!/^[0-9]+$/.test(value)) return -1;
  let number = 0;
  for (const c of value) {
    if (number < INT_MAX) number = Math.min(number * 10 + (c.charCodeAt(0) - 48), INT_MAX);
  }
  return number < 1 ? -1 : number;
}

// { status: "Readable" | "Utf16" | "NulByte", unreadableLine, formatVersion,
//   sections: [{ name, line, values: [{ key, value, line, earlierLines }] }],
//   diagnostics: [{ kind, lines, section, key, value }] }
// A section is spelled as its first header, a key as its kept (last) occurrence.
export function parseCanonicalIni(bytes) {
  const doc = { status: "Readable", unreadableLine: 0, formatVersion: 0, sections: [], diagnostics: [] };
  const text = toLatin1(bytes);
  if (startsWithUtf16Mark(text)) {
    doc.status = "Utf16";
    return doc;
  }
  const nul = text.indexOf("\0");
  if (nul >= 0) {
    doc.status = "NulByte";
    doc.unreadableLine = lineOfOffset(text, nul);
    return doc;
  }

  let current = null;
  let stampHeaderLine = 0;
  for (const { text: raw, number } of splitLines(bytes)) {
    const line = trimSpaceTab(raw);
    if (line === "" || line[0] === ";" || line[0] === "#") continue;

    if (line[0] === "[") {
      const header = parseHeader(line);
      current = null;
      if (!header.closed) {
        doc.diagnostics.push(diagnostic("UnclosedSectionHeader", number, "", "", line));
        continue;
      }
      if (header.trailing !== "") {
        doc.diagnostics.push(diagnostic("TextAfterSectionHeader", number, header.name, "", header.trailing));
      }
      if (header.name === "") {
        doc.diagnostics.push(diagnostic("EmptySectionName", number, "", "", line));
        continue;
      }
      current = doc.sections.find((s) => equalsAsciiIgnoreCase(s.name, header.name)) ?? null;
      if (current === null) {
        current = { name: header.name, line: number, values: [] };
        doc.sections.push(current);
        if (stampHeaderLine === 0 && equalsAsciiIgnoreCase(header.name, STAMP_SECTION)) stampHeaderLine = number;
      }
      continue;
    }

    const equals = line.indexOf("=");
    if (equals < 0) {
      doc.diagnostics.push(diagnostic("MissingEquals", number, "", "", line));
      continue;
    }
    const key = trimSpaceTab(line.slice(0, equals));
    const value = trimSpaceTab(line.slice(equals + 1));
    if (key === "") {
      doc.diagnostics.push(diagnostic("EmptyKey", number, "", "", line));
      continue;
    }
    if (current === null) {
      doc.diagnostics.push(diagnostic("KeyOutsideSection", number, "", key, value));
      continue;
    }
    const existing = current.values.find((v) => equalsAsciiIgnoreCase(v.key, key));
    if (existing) {
      existing.earlierLines.push(existing.line);
      existing.key = key;
      existing.value = value;
      existing.line = number;
    } else {
      current.values.push({ key, value, line: number, earlierLines: [] });
    }
  }

  for (const section of doc.sections) {
    for (const v of section.values) {
      if (v.earlierLines.length === 0) continue;
      const d = diagnostic("DuplicateKey", 0, section.name, v.key, v.value);
      d.lines = [...v.earlierLines, v.line];
      doc.diagnostics.push(d);
    }
  }
  readFormat(doc, stampHeaderLine);

  doc.diagnostics.sort((a, b) => a.lines[0] - b.lines[0] || DIAGNOSTIC_KINDS[a.kind] - DIAGNOSTIC_KINDS[b.kind]);
  return doc;
}

function readFormat(doc, stampHeaderLine) {
  doc.formatVersion = CONFIG_FORMAT;
  const stamp = findSection(doc, STAMP_SECTION);
  if (stamp === null) return;
  const format = findValue(stamp, FORMAT_KEY);
  if (format === null) {
    doc.diagnostics.push(diagnostic("ConfigFormatMissing", stampHeaderLine, stamp.name, "", ""));
    return;
  }
  const number = parseFormatNumber(format.value);
  if (number < 0) {
    doc.diagnostics.push(diagnostic("ConfigFormatInvalid", format.line, stamp.name, format.key, format.value));
    return;
  }
  doc.formatVersion = number;
  if (number > CONFIG_FORMAT) {
    doc.diagnostics.push(diagnostic("ConfigFormatNewer", format.line, stamp.name, format.key, format.value));
  }
}

export function findSection(doc, name) {
  return doc.sections.find((s) => equalsAsciiIgnoreCase(s.name, name)) ?? null;
}

export function findValue(section, key) {
  return section.values.find((v) => equalsAsciiIgnoreCase(v.key, key)) ?? null;
}
