#!/usr/bin/env node

// Holds core's script-side config code to the shared fixtures and to its own rules:
//
// - scripts/lib/canonical-ini.mjs against every case in data/fixtures/canonical-ini/reader/,
//   the files the C++ and C# readers run;
// - scripts/lib/key-bindings.mjs against data/fixtures/canonical-ini/keys/cases.tsv, both
//   dialects;
// - scripts/check-canonical-config.mjs's lint: the rendered fixture files pass it, and each
//   rule fails a copy of one of them edited to break that rule alone.
//
//   node scripts/test-canonical-config.mjs      (pixi run check-canonical-ini-js)

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { hasCanonicalStamp, parseCanonicalIni } from "./lib/canonical-ini.mjs";
import { formatKeyBindings, parseKeyBindings } from "./lib/key-bindings.mjs";
import { lintCanonicalConfig } from "./check-canonical-config.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const FIXTURES = path.join(CORE_ROOT, "data", "fixtures", "canonical-ini");

const failures = [];
let checks = 0;
const check = (ok, message) => {
  checks++;
  if (!ok) failures.push(message);
};

// The fixtures' byte escape: 0x20-0x7E other than '\' as themselves, '\' as '\\', every other
// byte as \xHH.
function escapeBytes(latin1) {
  let out = "";
  for (const c of latin1) {
    const b = c.charCodeAt(0);
    if (c === "\\") out += "\\\\";
    else if (b >= 0x20 && b <= 0x7e) out += c;
    else out += `\\x${b.toString(16).toUpperCase().padStart(2, "0")}`;
  }
  return out;
}

function unescapeBytes(text) {
  return text.replace(/\\\\|\\x([0-9A-F]{2})/g, (m, hex) => (hex ? String.fromCharCode(parseInt(hex, 16)) : "\\"));
}

function tsvRows(file) {
  return fs
    .readFileSync(file, "latin1")
    .split("\n")
    .filter((line) => line !== "" && !line.startsWith("#"))
    .map((line) => line.split("\t"));
}

function readerRows(bytes) {
  const doc = parseCanonicalIni(bytes);
  const rows = [
    doc.status === "NulByte" ? `status\tNulByte\t${doc.unreadableLine}` : `status\t${doc.status}`,
    `format\t${doc.status === "Readable" ? doc.formatVersion : 0}`,
    `stamp\t${hasCanonicalStamp(bytes)}`,
  ];
  if (doc.status !== "Readable") return rows;
  const kept = doc.sections.flatMap((s) => s.values.map((v) => ({ section: s.name, ...v })));
  kept.sort((a, b) => a.line - b.line);
  for (const v of kept) {
    const earlier = v.earlierLines.length === 0 ? "-" : v.earlierLines.join(",");
    rows.push(["key", escapeBytes(v.section), escapeBytes(v.key), escapeBytes(v.value), v.line, earlier].join("\t"));
  }
  for (const d of doc.diagnostics) rows.push(`diagnostic\t${d.kind}\t${d.lines.join(",")}`);
  return rows;
}

const readerDir = path.join(FIXTURES, "reader");
const readerCases = fs.readdirSync(readerDir).filter((n) => fs.statSync(path.join(readerDir, n)).isDirectory());
for (const name of readerCases) {
  const bytes = fs.readFileSync(path.join(readerDir, name, "input.ini"));
  const expected = tsvRows(path.join(readerDir, name, "expected.tsv")).map((r) => r.join("\t"));
  const actual = readerRows(bytes);
  check(
    JSON.stringify(actual) === JSON.stringify(expected),
    `reader/${name}: expected\n    ${expected.join("\n    ")}\n  read\n    ${actual.join("\n    ")}`,
  );
}

const keyRows = tsvRows(path.join(FIXTURES, "keys", "cases.tsv"));
for (const [dialect, input, result, canonical = ""] of keyRows) {
  const text = unescapeBytes(input);
  const parsed = parseKeyBindings(text, dialect);
  const label = `keys ${dialect} '${input}'`;
  if (result === "invalid") {
    check(parsed.error !== undefined && parsed.bindings === undefined, `${label}: read, expected invalid`);
    continue;
  }
  if (parsed.error) {
    check(false, `${label}: ${parsed.error}`);
    continue;
  }
  const written = formatKeyBindings(parsed.bindings, dialect);
  check(written === unescapeBytes(canonical), `${label}: wrote '${written}', expected '${canonical}'`);
  const reread = parseKeyBindings(written, dialect);
  check(JSON.stringify(reread.bindings) === JSON.stringify(parsed.bindings), `${label}: '${written}' reads back differently`);
}

// The lint. Every file below is a renderer's output, so it has to pass; each mutation breaks
// one rule and must draw exactly the problem named.
const rendered = {
  "head-tracking/all-concepts.ini": "native",
  "table/render-defaults/expected.ini": "native",
};
for (const [rel, dialect] of Object.entries(rendered)) {
  for (const d of [dialect, "unity"]) {
    const problems = lintCanonicalConfig(fs.readFileSync(path.join(FIXTURES, rel)), { dialect: d, exceptions: undefined });
    check(problems.length === 0, `lint ${rel} (${d}) should pass, and says:\n    ${problems.join("\n    ")}`);
  }
}

const base = fs.readFileSync(path.join(FIXTURES, "head-tracking", "all-concepts.ini"), "latin1");
const replace = (from, to) => {
  if (!base.includes(from)) throw new Error(`the mutation base has no ${JSON.stringify(from)}`);
  return base.replace(from, to);
};
const append = (lines) => `${base}\r\n${lines.join("\r\n")}\r\n`;

const mutations = [
  ["an LF line", replace("UdpPort=4242\r\n", "UdpPort=4242\n"), "line 17 does not end in CRLF"],
  ["no final CRLF", base.slice(0, -2), "line 90 does not end in CRLF"],
  ["LF throughout", base.replace(/\r\n/g, "\n"), "lines 1, 2, 3 and 87 more do not end in CRLF"],
  ["two non-ASCII lines", replace("; UDP port", "; UDP p\xF6rt").replace("; true: head", "; tr\xFCe: head"), "lines 16, 20 hold a byte above 0x7F"],
  ["a UTF-8 mark", `\xEF\xBB\xBF${base}`, "starts with a UTF-8 byte order mark"],
  ["a non-ASCII byte", replace("; UDP port", "; UDP p\xF6rt"), "line 16 holds a byte above 0x7F"],
  ["a NUL byte", replace("UdpPort=4242", "UdpPort=42\x0042"), "is unreadable: line 17 holds a NUL byte"],
  ["a UTF-16 mark", `\xFF\xFE${base}`, "is unreadable: it starts with a UTF-16 byte order mark"],
  ["spaces around =", replace("UdpPort=4242", "UdpPort = 4242"), "line 17 is not written Key=value"],
  ["an indented key", replace("UdpPort=4242", "  UdpPort=4242"), "line 17 is not written Key=value"],
  ["an indented header", replace("[Network]", "  [Network]"), "line 15 is not written [Network]"],
  ["spaces inside a header", replace("[Network]", "[ Network ]"), "line 15 is not written [Network]"],
  ["a repeated section", append(["[hotkeys]", "ZoomKey=F5"]), "line 92: [hotkeys] repeats the section begun on line 75"],
  ["a note after a header", replace("[Network]", "[Network] ; port"), "line 15: \"; port\" follows [Network]"],
  ["an unclosed header", replace("[Network]", "[Network"), ["line 15: \"[Network\" has no closing ]", "line 17: UdpPort is above every section header"]],
  ["a line with no =", replace("UdpPort=4242", "UdpPort"), "line 17: \"UdpPort\" is neither a setting"],
  ["a repeated key", replace("UdpPort=4242", "UdpPort=4242\r\nUdpPort=4243"), "[Network] UdpPort is set on lines 17, 18"],
  ["no ConfigFormat", replace("ConfigFormat=1\r\n", ""), "line 11: [CameraUnlock] has no ConfigFormat"],
  ["ConfigFormat newer", replace("ConfigFormat=1", "ConfigFormat=2"), "line 13: ConfigFormat=2 is newer than format 1"],
  ["ConfigFormat padded", replace("ConfigFormat=1", "ConfigFormat=01"), "line 13: ConfigFormat=01 is written ConfigFormat=1"],
  ["ConfigFormat miscased", replace("ConfigFormat=1", "configformat=1"), "line 13: configformat is spelled ConfigFormat"],
  ["a mod key in the stamp", replace("ConfigFormat=1", "ConfigFormat=1\r\nContract=2"), "line 14: [CameraUnlock] Contract is not written by a mod"],
  ["the stamp miscased", replace("[CameraUnlock]", "[cameraunlock]"), "line 11: [cameraunlock] is spelled [CameraUnlock]"],
  ["a schema section miscased", replace("[Network]", "[network]"), "line 15: [network] is spelled [Network]"],
  ["an alias", replace("UdpPort=4242", "Port=4242"), "line 17: [Network] Port is an alias of [Network] UdpPort"],
  ["a concept miscased", replace("UdpPort=4242", "udpport=4242"), "line 17: [Network] udpport is spelled UdpPort"],
  ["a concept with an underscore", replace("UdpPort=4242", "Udp_Port=4242"), "line 17: [Network] Udp_Port is spelled UdpPort"],
  ["a concept in another section", replace("UdpPort=4242", "").replace("[Light]\r\n", "[Light]\r\nUdpPort=4242\r\n"), "[Light] UdpPort belongs in [Network]"],
  ["a non-canonical concept", replace("[Light]\r\n", "[Light]\r\nRecenterKey=Home\r\n"), "[Light] RecenterKey: The mod keeps no centre of its own"],
  ["a non-canonical alias", replace("[Light]\r\n", "[Light]\r\nInvertX=true\r\n"), "[Light] InvertX: The mod applies the head pose as the tracker sends it, with no axis inversion"],
  ["a position sensitivity", replace("[Light]\r\n", "[Light]\r\nPositionSensitivityX=1.0\r\n"), "[Light] PositionSensitivityX: The mod applies the head pose as the tracker sends it, with no sensitivity"],
  ["a deadzone", append(["[Camera]", "YawDeadzone=0.5"]), "line 93: [Camera] YawDeadzone: The mod applies the head pose as the tracker sends it, with no deadzone of its own."],
  ["a deadzone in snake case", append(["[Camera]", "deadzone_deg=0.5"]), "line 93: [Camera] deadzone_deg: The mod applies the head pose as the tracker sends it, with no deadzone"],
  ["a response curve", replace("[Light]\r\n", "[Light]\r\nResponseCurve=2\r\n"), "[Light] ResponseCurve: The mod applies the head pose as the tracker sends it, with no response curve of its own."],
  ["a retired key", replace("LocalSmoothing=0.0", "LocalSmoothing=0.0\r\nSmoothing=0.1"), "[Smoothing] Smoothing is retired"],
  ["a deadzone minimum", append(["[Rotation]", "DeadzoneMin=0.1"]), "line 93: [Rotation] DeadzoneMin: The mod applies the head pose as the tracker sends it, with no deadzone of its own."],
  ["a curve strength", append(["[Rotation]", "CurveStrength=0.5"]), "line 93: [Rotation] CurveStrength: The mod applies the head pose as the tracker sends it, with no response curve of its own."],
  ["a rotation scale", append(["[Tuning]", "rot_scale=1.5"]), "line 93: [Tuning] rot_scale: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["a yaw gain", append(["[ExtendedView]", "YawGain=1.5"]), "line 93: [ExtendedView] YawGain: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["a rotation sensitivity", append(["[Tracking]", "RotationSensitivity=1.0"]), "line 93: [Tracking] RotationSensitivity: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["one position sensitivity", replace("EnableOnStartup=true", "EnableOnStartup=true\r\nPositionSensitivity=1.0"), "[General] PositionSensitivity: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["a bare sensitivity", replace("TrueFreeLook=false", "TrueFreeLook=false\r\nSensitivity=1.0"), "[Position] Sensitivity: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["a position multiplier", replace("TrueFreeLook=false", "TrueFreeLook=false\r\nPositionMultiplier=1.0"), "[Position] PositionMultiplier: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["a lean scale", replace("TrueFreeLook=false", "TrueFreeLook=false\r\nLeanScale=1.0"), "[Position] LeanScale: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."],
  ["an axis sign", append(["[Tuning]", "SignYaw=-1"]), "line 93: [Tuning] SignYaw: The mod applies the head pose as the tracker sends it, with no axis inversion of its own."],
  ["a position scale", append(["[Camera]", "PositionScale=5000"]), "line 93: [Camera] PositionScale: The mod converts your head movement to the game's units itself"],
  ["a world scale", replace("TrueFreeLook=false", "TrueFreeLook=false\r\nWorldScale=39.37"), "[Position] WorldScale: The mod converts your head movement to the game's units itself"],
  ["[Sensitivity]", append(["[Sensitivity]", "Deadband=1"]), ["line 92: [Sensitivity] holds no canonical setting", "line 93: [Sensitivity] Deadband: The mod applies the head pose as the tracker sends it, with no deadzone of its own."]],
  ["[Sensitivity] with a bare Yaw", append(["[Sensitivity]", "Yaw=1.0"]), ["line 92: [Sensitivity] holds no canonical setting", "line 93: [Sensitivity] Yaw: The mod applies the head pose as the tracker sends it, with no sensitivity of its own."]],
  ["[Inversion] with a bare Pitch", append(["[Inversion]", "Pitch=true"]), ["line 92: [Inversion] holds no canonical setting", "line 93: [Inversion] Pitch: The mod applies the head pose as the tracker sends it, with no axis inversion of its own."]],
  ["[Deadzone] with a bare Yaw", append(["[Deadzone]", "Yaw=0.5"]), ["line 92: [Deadzone] holds no canonical setting", "line 93: [Deadzone] Yaw: The mod applies the head pose as the tracker sends it, with no deadzone of its own."]],
  ["[Deadzone] with any key", append(["[Deadzone]", "; Degrees.", "Threshold=0.5"]), ["line 92: [Deadzone] holds no canonical setting", "line 94: [Deadzone] Threshold: The mod applies the head pose as the tracker sends it, with no deadzone of its own."]],
  ["[deadzone] miscased", append(["[deadzone]", "YawDegrees=0.5"]), ["line 92: [deadzone] is not a PascalCase name", "line 92: [deadzone] holds no canonical setting", "line 93: [deadzone] YawDegrees: The mod applies the head pose as the tracker sends it, with no deadzone of its own."]],
  ["[Reticle]", append(["[Reticle]"]), "line 92: [Reticle] holds no canonical setting"],
  ["a bare noun", append(["[Flashlight]", "Enabled=true"]), "line 93: [Flashlight] Enabled: a bare Enabled"],
  ["a chord switch", replace("EnableOnStartup=true", "EnableOnStartup=true\r\nChordToggle=true"), "line 22: [General] ChordToggle: a chord is an item of its action's key list"],
  ["a chord letter row", replace("TrueFreeLookKey=Insert, Ctrl+Shift+U", "TrueFreeLookKey=Insert, Ctrl+Shift+U\r\nChord_Toggle_Key=Y"), "[Hotkeys] Chord_Toggle_Key: a chord is an item of its action's key list"],
  ["true free look in snake case", replace("TrueFreeLook=false", "true_free_look=false"), "[Position] true_free_look is spelled TrueFreeLook"],
  ["true free look off the default keys", replace("TrueFreeLookKey=Insert, Ctrl+Shift+U", "TrueFreeLookKey=Insert"), "[Hotkeys] TrueFreeLookKey=Insert differs from the fleet's Insert, Ctrl+Shift+U"],
  ["a snake_case key", append(["[Camera]", "yaw_limit=90"]), "line 93: [Camera] yaw_limit is not a PascalCase name"],
  ["a lower-case section", append(["[target]", "CbSize=4"]), "line 92: [target] is not a PascalCase name"],
  ["a key in two sections", append(["[Camera]", "Offset=1", "[Debug]", "Offset=2"]), "line 95: [Debug] Offset: Offset is also a key under [Camera]"],
  ["a key in two sections, cased apart", append(["[Camera]", "Offset=1", "[Debug]", "OFFSET=2"]), "[Debug] OFFSET: OFFSET is also a key under [Camera]"],
  ["a hex hotkey in a Unity mod", replace("YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=0x22, Ctrl+Shift+H"), "YawModeKey=0x22, Ctrl+Shift+H is not a unity key list", "unity"],
  ["a hotkey that is no key", replace("ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=Endd, Ctrl+Shift+Y"), "ToggleKey=Endd, Ctrl+Shift+Y is not a native key list: 'Endd' is not a key name"],
  ["a hotkey off the default", replace("ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=End"), "[Hotkeys] ToggleKey=End differs from the fleet's End, Ctrl+Shift+Y"],
  ["a hotkey in another spelling", replace("ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=end, ctrl+shift+y"), "ToggleKey=end, ctrl+shift+y differs from the fleet's End, Ctrl+Shift+Y"],
  ["a local hotkey that is no key", replace("YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=PageDown, Ctrl+Shift+H\r\nZoomKey=Ctrl+"), "[Hotkeys] ZoomKey=Ctrl+ is not a native key list"],
];
for (const [label, text, expected, dialect = "native"] of mutations) {
  const want = [expected].flat();
  const problems = lintCanonicalConfig(Buffer.from(text, "latin1"), { dialect, exceptions: undefined });
  check(
    problems.length === want.length && want.every((w, i) => problems[i].includes(w)),
    `lint mutation "${label}": expected ${want.map((w) => `"${w}"`).join(", ")}, got\n    ${problems.join("\n    ") || "(none)"}`,
  );
}

const exception = { ToggleKey: { replaces: "Ctrl+Shift+Y", with: "Ctrl+Shift+T", reason: "the game binds Ctrl+Shift+Y" } };
const excepted = Buffer.from(replace("ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=End, Ctrl+Shift+T"), "latin1");
check(lintCanonicalConfig(excepted, { dialect: "native", exceptions: exception }).length === 0, "lint: a hotkey_exceptions replacement should pass");
check(
  lintCanonicalConfig(Buffer.from(base, "latin1"), { dialect: "native", exceptions: exception }).length === 1,
  "lint: with a hotkey_exceptions entry, the fleet default no longer passes",
);

if (failures.length > 0) {
  console.error(`${failures.length} of ${checks} checks failed:`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`canonical INI scripts: ${readerCases.length} reader cases, ${keyRows.length} key cases, ${mutations.length} lint mutations; ${checks} checks passed`);
