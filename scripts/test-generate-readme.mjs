#!/usr/bin/env node

// Holds the README config block of scripts/generate-readme.mjs to what it says about
// Defaults.ini, over synthetic repo states whose committed file is a core fixture.
//
// data/fixtures/readme/values-*.md are the blocks the generator rendered from
// data/fixtures/readme/values.ini (a copy of table/render-defaults/expected.ini, a file that
// holds values) at 8c83941, before it knew Defaults.ini. A repo whose committed file still holds
// values must keep that block byte for byte, or every converted README drifts at the next pin
// bump.
//
//   node scripts/test-generate-readme.mjs

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { configBlock } from "./generate-readme.mjs";
import { findSection, findValue, parseCanonicalIni } from "./lib/canonical-ini.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const VALUES = "data/fixtures/readme/values.ini";
const FRESH = "data/fixtures/canonical-ini/example/CameraUnlock.ini";
const ALL_FRESH = "data/fixtures/canonical-ini/head-tracking/all-concepts-fresh.ini";
const DEFAULTS_INI = parseCanonicalIni(fs.readFileSync(path.join(CORE_ROOT, "data/fixtures/canonical-ini/global/Defaults.ini")));
const SCHEMA = JSON.parse(fs.readFileSync(path.join(CORE_ROOT, "data/config-schema.json"), "utf8"));

const entry = (committed, installed, legacy_source, dialect, extra = {}) =>
  ({ committed, installed, legacy_source, dialect, no_installed_reason: null, state: "stamped", ...extra });
const CASES = {
  "legacy-bepinex": ["legacy", (c) => [entry(c, ["BepInEx\\config\\CameraUnlock.ini"], "com.example.headtracking.cfg", "unity")]],
  "legacy-stores": ["legacy", (c) => [entry(c, ["CameraUnlock.ini", "Game\\Binaries\\Win64\\CameraUnlock.ini"], "HeadTracking.ini", "native")]],
  "new-bepinex": ["mover", (c) => [entry(c, ["BepInEx\\config\\CameraUnlock.ini"], null, "unity")]],
  "new-one-path": ["mover", (c) => [entry(c, ["CameraUnlock.ini"], null, "native")]],
  "new-no-installed": ["mover", (c) => [entry(c, [], null, "native", { no_installed_reason: "It sits in the folder the plugin loader names." })]],
  "new-several": ["mover", (c) => [entry(c, ["A\\CameraUnlock.ini"], null, "native"), entry(c, ["B\\CameraUnlock.ini", "C\\CameraUnlock.ini"], null, "native")]],
};
const block = (name, committed, root = CORE_ROOT) => {
  const [listing, files] = CASES[name];
  return configBlock({ root, listing, converted: true, unrecorded_stamped: [], files: files(committed) });
};

let checks = 0;
const failures = [];
function check(ok, what) {
  checks++;
  if (!ok) failures.push(what);
}

const WHO = "A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads.";
const WHERE = "`Defaults.ini` is `%AppData%\\CameraUnlock\\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.";
const NEVER = "The mod never changes `Defaults.ini` after that.";
const THIS_GAME = "Writing a value in place of `default` changes that setting for this game only.";
const EARLIER = ", and neither do earlier versions of this mod.";
const HOTKEY = "When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.";
const NATIVE = "On Linux and macOS without Wine or Proton, this version reads its settings";
const nativeSentence = (legacy) => legacy === null
  ? "On Linux and macOS without Wine or Proton, this version reads its settings and saves none: it creates no `CameraUnlock.ini` and a change made in game lasts until the game closes."
  : `On Linux and macOS without Wine or Proton, this version reads its settings and saves none: it creates no \`CameraUnlock.ini\`, reads your settings from \`${legacy}\` again at every start while there is no \`CameraUnlock.ini\`, and a change made in game lasts until the game closes.`;
const NATIVE_CREATE = ", or the game runs on Linux or macOS without Wine or Proton.";
const MIGRATED = "is written as `default` when the value imported for it equals its default at that start";
const PAIR = "`RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.";
const RESET = "replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.";

for (const name of Object.keys(CASES)) {
  const expected = fs.readFileSync(path.join(CORE_ROOT, "data/fixtures/readme", `values-${name}.md`), "utf8");
  const values = block(name, VALUES);
  check(values + "\n" === expected, `${name}: the block for a file of values differs from data/fixtures/readme/values-${name}.md`);

  const fresh = block(name, FRESH);
  const legacy = name.startsWith("legacy");
  const [{ dialect, legacy_source: legacySource }] = CASES[name][1](FRESH);
  const csharp = dialect === "unity";
  for (const [text, want, what] of [
    [WHO, true, "who reads Defaults.ini"],
    [HOTKEY, true, "that a saved hotkey change stops the row following Defaults.ini"],
    [WHERE, true, "where Defaults.ini is"],
    [NEVER, true, "that the mod never changes Defaults.ini"],
    [THIS_GAME, true, "what a value in place of default does"],
    [EARLIER, legacy, "that earlier versions do not read Defaults.ini"],
    [NATIVE, csharp, "the native Linux and macOS read-only sentence"],
    [nativeSentence(legacySource), csharp, "the native read-only sentence for this repo's legacy file or its absence"],
    [NATIVE_CREATE, csharp, "the native exception to creating Defaults.ini"],
    [MIGRATED, legacy, "what the import writes as default"],
    [PAIR, legacy, "the tracking-mode pair of the import"],
    [RESET, legacy, "that the reset rows follow Defaults.ini"],
  ]) {
    check(fresh.includes(text) === want, `${name}: the fresh block ${want ? "lacks" : "has"} ${what}`);
  }
  check(fresh.split("`Defaults.ini` is `%AppData%").length === 2, `${name}: the fresh block says where Defaults.ini is other than once`);
  if (csharp && legacy) {
    check(fresh.indexOf(NATIVE) > fresh.indexOf("Earlier versions of the mod kept these settings"), `${name}: the native sentence names the legacy file before the block introduces it`);
  }
}

// Every built-in value the block lists is the one Defaults.ini holds when the mod creates it.
function builtIns(text) {
  const lines = text.split("\n");
  const at = lines.indexOf("The built-in value of each setting set to `default` below:");
  if (at < 0) return null;
  const rows = [];
  for (const line of lines.slice(at + 2)) {
    const m = /^- `([A-Za-z0-9]+)=(.*)`$/.exec(line);
    if (m === null) break;
    rows.push([m[1], m[2]]);
  }
  return rows;
}
const inDefaultsIni = (key) => {
  const concept = SCHEMA.concepts.find((c) => c.canonical && c.key === key);
  return findValue(findSection(DEFAULTS_INI, concept.section), key).value;
};
const defaultKeys = (committed) => parseCanonicalIni(fs.readFileSync(path.join(CORE_ROOT, committed)))
  .sections.flatMap((s) => s.values.filter((v) => v.value === "default").map((v) => v.key));
for (const committed of [FRESH, ALL_FRESH]) {
  const rows = builtIns(block("new-one-path", committed));
  const keys = defaultKeys(committed);
  check(rows !== null && rows.map(([k]) => k).join(",") === keys.join(","), `${committed}: the built-in list names ${rows?.map(([k]) => k).join(", ")}, not the file's default rows ${keys.join(", ")}`);
  for (const [key, text] of rows ?? []) {
    check(text === inDefaultsIni(key), `${committed}: ${key}=${text} in the block, ${inDefaultsIni(key)} in global/Defaults.ini`);
  }
}
check(defaultKeys(ALL_FRESH).length === SCHEMA.concepts.filter((c) => c.canonical).length, `${ALL_FRESH} no longer sets every canonical concept to default`);

// On a game's local row the word is data: the block lists only concept rows and still renders.
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "readme-"));
let local;
try {
  fs.writeFileSync(path.join(scratch, "local.ini"), "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n[Network]\r\nUdpPort=default\r\n\r\n[Camera]\r\nMode=Default\r\n");
  local = builtIns(block("new-one-path", "local.ini", scratch));
} finally {
  fs.rmSync(scratch, { recursive: true, force: true });
}
check(JSON.stringify(local) === JSON.stringify([["UdpPort", "4242"]]), `a local row holding Default: the built-in list is ${JSON.stringify(local)}`);

// A C# repo and a C++ one never share a README block.
let mixed = null;
try {
  configBlock({ root: CORE_ROOT, listing: "mover", converted: true, unrecorded_stamped: [], files: [entry(FRESH, ["A\\CameraUnlock.ini"], null, "unity"), entry(FRESH, ["B\\CameraUnlock.ini"], null, "native")] });
} catch (e) {
  mixed = e.message;
}
check(mixed === "data/config-format.json gives one repo's config files the dialects unity and native; the config block needs one owner language", `mixed dialects: ${mixed ?? "no error"}`);

for (const f of failures) console.error(`FAIL ${f}`);
console.log(`${checks - failures.length} of ${checks} README block checks passed.`);
process.exitCode = failures.length > 0 ? 1 : 0;
