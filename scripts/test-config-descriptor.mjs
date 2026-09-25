#!/usr/bin/env node

// Holds scripts/check-config-descriptor.mjs to its rules: a descriptor that meets every rule is
// clean, and one mutation per rule fails with that rule's message. Repos are throwaway git
// checkouts named after data/config-format.json entries. A descriptor names CameraUnlock.ini, and
// no entry records that name yet, so the clean cases run against synthetic entries: real repos'
// shapes (abzu-headtracking, one installed path; fallout-new-vegas-headtracking and
// prey-headtracking, two; sleeping-dogs-headtracking, never published a pre-canonical build) with
// the file recorded under the fleet name, and shapes no repo has (a legacy_source beside the
// executable, a mod_home file, two config files). The real entries stand for a repo converted in
// place, whose file a pre-canonical release reads. It also runs the rows generator in
// encode-seed.mjs, the rules in validate-manifest.mjs on built ZIPs, the report conformance reads,
// conformance's config-descriptor check, and the packager's ConvertFrom-Json /
// ConvertTo-Json -Depth 10 round trip.
//
//   node scripts/test-config-descriptor.mjs      (pixi run test-config-descriptor, part of pixi run check)

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { isDeepStrictEqual } from "node:util";
import { fileURLToPath } from "node:url";

import { repoState } from "./check-canonical-config.mjs";
import { descriptorProblems, expectedRows, repoReport } from "./check-config-descriptor.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SCRIPTS = path.join(CORE_ROOT, "scripts");
const ALL = fs.readFileSync(path.join(CORE_ROOT, "data", "fixtures", "canonical-ini", "head-tracking", "all-concepts.ini"), "latin1");
const ALL_ROWS = { EnableOnStartup: true, WorldSpaceYaw: true, RotationEnabled: true, PositionEnabled: true, TrueFreeLook: false };
const LEGACY_INI = "[General]\r\nEnabled=1\r\n";

const failures = [];
let checks = 0;
const check = (ok, message) => {
  checks++;
  if (!ok) failures.push(message);
};

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "config-descriptor-"));
const edit = (text, from, to) => {
  if (!text.includes(from)) throw new Error(`the fixture has no ${JSON.stringify(from)}`);
  return text.replace(from, to);
};

function git(root, ...args) {
  const r = spawnSync("git", ["-C", root, "-c", "user.name=test", "-c", "user.email=test@example.invalid", ...args], { encoding: "utf8" });
  if (r.status !== 0) throw new Error(`git ${args.join(" ")} failed: ${r.stderr}`);
  return r.stdout;
}

// A throwaway checkout whose folder name is the data/config-format.json entry it stands for.
function repo(label, name, files) {
  const root = path.join(scratch, label, name);
  fs.mkdirSync(root, { recursive: true });
  git(root, "init", "-q");
  for (const [rel, text] of Object.entries(files)) {
    fs.mkdirSync(path.dirname(path.join(root, rel)), { recursive: true });
    fs.writeFileSync(path.join(root, rel), typeof text === "string" ? Buffer.from(text, "latin1") : text);
  }
  return root;
}

// The state repoState would give an entry no real repo has yet, for a checkout at root.
function syntheticState(root, name, listing, entries) {
  const files = entries.map(({ text, ...e }) => ({ legacy_source: null, dialect: "native", state: "stamped", problems: [], ...e }));
  return { root, folder: name, repo: name, listing, converted: true, files, unrecorded_stamped: [], allow_legacy_symbols: [] };
}

// A throwaway checkout of such an entry, with its committed file.
function synthetic(label, name, listing, entries) {
  const root = repo(label, name, Object.fromEntries(entries.map((e) => [e.committed, e.text ?? ALL])));
  return { root, state: syntheticState(root, name, listing, entries) };
}

const manifest = (gameId, config, extra = {}) => ({
  schema_version: 2,
  mod_info: { name: "Mod", version: "1.2.0", game_id: gameId },
  delivery_mode: "manifest",
  files: [{ source: "plugins/Mod.dll", target: "Mod.dll" }],
  ...extra,
  config,
});

function problemsOf(man, base, checkVersion = true) {
  return descriptorProblems(man, { root: base.root, state: base.state, checkVersion });
}

function clean(label, man, base, checkVersion) {
  const problems = problemsOf(man, base, checkVersion);
  check(problems.length === 0, `${label}: should be clean, got ${JSON.stringify(problems)}`);
}

function fails(label, man, base, expected, checkVersion) {
  const problems = problemsOf(man, base, checkVersion);
  check(problems.some((p) => p.includes(expected)), `${label}: should fail with "${expected}", got ${JSON.stringify(problems)}`);
}

// abzu-headtracking: legacy, one installed path, so game_root.
const ABZU_ENTRY = { committed: "HeadTracking.ini", installed: ["AbzuGame\\Binaries\\Win64\\CameraUnlock.ini"] };
const abzuWith = (label, text) => synthetic(label, "abzu-headtracking", "legacy", [{ ...ABZU_ENTRY, text }]);
const abzu = abzuWith("abzu", ALL);
const abzuConfig = { path: "AbzuGame/Binaries/Win64/CameraUnlock.ini", anchor: "game_root", canonical_since: "1.1.0", rows: { ...ALL_ROWS } };
const abzuMan = (change = (c) => c, extra = {}) => manifest("abzu", change(structuredClone(abzuConfig)), extra);
clean("abzu", abzuMan(), abzu);
clean("abzu without anchor, which is game_root", abzuMan(({ anchor, ...c }) => c), abzu);

// fallout-new-vegas-headtracking: two installed paths, so exe_dir with the tail of both.
const fnv = synthetic("fnv", "fallout-new-vegas-headtracking", "legacy", [
  { committed: "config/HeadTracking.ini", installed: ["CameraUnlock.ini", "Fallout New Vegas English\\CameraUnlock.ini"] },
]);
const fnvConfig = { path: "CameraUnlock.ini", anchor: "exe_dir", canonical_since: "1.1.0", rows: { ...ALL_ROWS } };
const fnvMan = (change = (c) => c, extra = {}) => manifest("fallout-new-vegas", change(structuredClone(fnvConfig)), extra);
clean("fallout-new-vegas", fnvMan(), fnv);

// prey-headtracking: two installed paths beside two executables.
const prey = synthetic("prey", "prey-headtracking", "legacy", [
  { committed: "HeadTracking.ini", installed: ["Binaries\\Danielle\\x64\\Release\\CameraUnlock.ini", "Binaries\\Danielle\\Gaming.Desktop.x64\\Release\\CameraUnlock.ini"] },
]);
const preyMan = (change = (c) => c) => manifest("prey", change({ path: "CameraUnlock.ini", anchor: "exe_dir", canonical_since: "1.1.0", rows: { ...ALL_ROWS } }));
clean("prey", preyMan(), prey);

// sleeping-dogs-headtracking: never published a pre-canonical build, so no canonical_since.
const sd = synthetic("sleeping-dogs", "sleeping-dogs-headtracking", "mover", [{ committed: "config/headtrack.ini", installed: ["CameraUnlock.ini"] }]);
const sdConfig = { path: "CameraUnlock.ini", rows: { ...ALL_ROWS } };
clean("sleeping-dogs", manifest("sleeping-dogs", structuredClone(sdConfig)), sd);

// A BepInEx-shaped entry with a legacy_source, legacy, and descriptor_omits WorldSpaceYaw for it.
const bep = synthetic("bepinex", "subnautica-headtracking", "legacy", [
  { committed: "Config.ini", installed: ["BepInEx\\config\\CameraUnlock.ini"], legacy_source: "BepInEx\\config\\com.cameraunlock.x.cfg", dialect: "unity" },
]);
const { WorldSpaceYaw: _omitted, ...bepRows } = ALL_ROWS;
const bepConfig = { path: "BepInEx/config/CameraUnlock.ini", legacy_source: "BepInEx/config/com.cameraunlock.x.cfg", canonical_since: "1.1.0", rows: bepRows };
const bepMan = (change = (c) => c, extra = {}) => manifest("subnautica", change(structuredClone(bepConfig)), extra);
clean("bepinex with legacy_source and an omitted row", bepMan(), bep);

// A file outside the game folder.
const home = synthetic("mod-home", "mod-home-headtracking", "mover", [
  { committed: "HeadTracking.ini", installed: [], no_installed_reason: "beside the DLL" },
]);
const homeMan = (change = (c) => c) => manifest("abzu", change({ path: "CameraUnlock.ini", anchor: "mod_home", rows: { ...ALL_ROWS } }));
clean("mod_home", homeMan(), home);

// An exe_dir entry with a legacy_source, which is relative to the same folder as the path. The
// installed paths are beside fallout-new-vegas's two executables.
const exeLegacy = synthetic("exe-legacy", "exe-legacy-headtracking", "mover", [
  { committed: "Config.ini", installed: ["Fallout New Vegas English\\CameraUnlock.ini", "CameraUnlock.ini"], legacy_source: "Fallout New Vegas English\\Old.ini" },
]);
const exeLegacyMan = (legacy, extra) => manifest("fallout-new-vegas", { path: "CameraUnlock.ini", anchor: "exe_dir", legacy_source: legacy, rows: { ...ALL_ROWS } }, extra);
clean("exe_dir legacy_source", exeLegacyMan("Old.ini"), exeLegacy);
fails("exe_dir legacy_source spelled game-relative", exeLegacyMan("Fallout New Vegas English/Old.ini"), exeLegacy, "config.legacy_source Fallout New Vegas English/Old.ini does not name Fallout New Vegas English/Old.ini");

// One mutation per shape rule.
fails("config not an object", abzuMan(() => 5), abzu, "config must be an object");
fails("unknown field", abzuMan((c) => ({ ...c, format: 1 })), abzu, 'unknown field "format"');
fails("no path", abzuMan(({ path: _, ...c }) => c), abzu, "config has no path");
fails("path not a string", abzuMan((c) => ({ ...c, path: 3 })), abzu, "config.path must be a non-empty string");
fails("backslash path", abzuMan((c) => ({ ...c, path: "AbzuGame\\Binaries\\Win64\\CameraUnlock.ini" })), abzu, 'uses "\\"');
fails("rooted path", abzuMan((c) => ({ ...c, path: "/HeadTracking.ini" })), abzu, "names a root or a drive");
fails("drive path", abzuMan((c) => ({ ...c, path: "C:/HeadTracking.ini" })), abzu, "names a root or a drive");
fails("traversing path", abzuMan((c) => ({ ...c, path: "../HeadTracking.ini" })), abzu, 'has an empty, "." or ".." segment');
fails("traversing legacy_source", bepMan((c) => ({ ...c, legacy_source: "BepInEx/../x.cfg" })), bep, "config.legacy_source BepInEx/../x.cfg has an empty");
{
  const root = repo("in-place", "abzu-headtracking", { "HeadTracking.ini": ALL });
  fails(
    "path the file a pre-canonical release reads",
    abzuMan((c) => ({ ...c, path: "AbzuGame/Binaries/Win64/HeadTracking.ini" })),
    { root, state: repoState(root) },
    "config.path AbzuGame/Binaries/Win64/HeadTracking.ini is not named CameraUnlock.ini",
  );
}
fails("unknown anchor", abzuMan((c) => ({ ...c, anchor: "install_dir" })), abzu, 'config.anchor "install_dir" is not one of');
fails("no rows", abzuMan(({ rows: _, ...c }) => c), abzu, "config has no rows");
fails("rows not an object", abzuMan((c) => ({ ...c, rows: [] })), abzu, "config.rows must be an object");
fails("unknown row", abzuMan((c) => ({ ...c, rows: { ...c.rows, ShowReticle: true } })), abzu, "config.rows names ShowReticle");
fails("row not a bool", abzuMan((c) => ({ ...c, rows: { ...c.rows, TrueFreeLook: "false" } })), abzu, 'config.rows.TrueFreeLook is "false"');
fails("RotationEnabled alone", abzuMan((c) => ({ ...c, rows: { RotationEnabled: true } })), abzu, "RotationEnabled without PositionEnabled");
fails("unlisted pair", abzuMan((c) => ({ ...c, rows: { ...c.rows, RotationEnabled: false, PositionEnabled: false } })), abzu, "is no mode data/pipeline-conformance.json");
fails("canonical_since not x.y.z", abzuMan((c) => ({ ...c, canonical_since: "1.1" })), abzu, 'config.canonical_since "1.1" is not a version');
fails("canonical_since above the version", abzuMan((c) => ({ ...c, canonical_since: "1.3.0" })), abzu, "is above mod_info.version 1.2.0");
clean("canonical_since above a committed placeholder version, which conformance does not compare", abzuMan((c) => ({ ...c, canonical_since: "1.3.0" }), { mod_info: { name: "Mod", version: "0.0.0", game_id: "abzu" } }), abzu, false);
fails("canonical_since above a pre-release of it", abzuMan(undefined, { mod_info: { name: "Mod", version: "1.1.0-dev.3", game_id: "abzu" } }), abzu, "is above mod_info.version 1.1.0-dev.3");
fails("no mod_info.version", abzuMan(undefined, { mod_info: { name: "Mod", game_id: "abzu" } }), abzu, "needs mod_info.version");
fails("config in a variant", abzuMan(undefined, { delivery_mode: "manifest_variants", variants: [{ id: "steam", config: {} }] }), abzu, 'variant "steam" carries a config block');
{
  const { config: _c, ...withoutTop } = abzuMan();
  fails("config in a variant alone", { ...withoutTop, variants: [{ id: "steam", config: {} }] }, abzu, 'variant "steam" carries a config block');
}
fails("install_cmd delivery", abzuMan(undefined, { delivery_mode: "install_cmd" }), abzu, 'this one is "install_cmd"');

// One mutation per repo rule.
{
  const root = repo("unconverted", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI });
  fails("unconverted repo", abzuMan(), { root, state: repoState(root) }, "abzu-headtracking is not converted");
}
{
  const two = synthetic("two-files", "two-files-headtracking", "mover", [
    { committed: "A.ini", installed: ["A.ini"] },
    { committed: "B.ini", installed: ["B.ini"] },
  ]);
  fails("two config files", manifest("abzu", { path: "CameraUnlock.ini", rows: { ...ALL_ROWS } }), two, "records 2 config files for two-files-headtracking; a package with more than one declares no descriptor");
}
fails("game_root path not the installed one", abzuMan((c) => ({ ...c, path: "CameraUnlock.ini" })), abzu, "config.path CameraUnlock.ini is not AbzuGame/Binaries/Win64/CameraUnlock.ini");
fails("game_root with two installed paths", fnvMan((c) => ({ ...c, anchor: "game_root" })), fnv, "records 2 installed paths");
fails("game_root with no installed path", homeMan((c) => ({ ...c, anchor: "game_root" })), home, "anchor is game_root, and data/config-format.json records no installed path");
fails("exe_dir path not a tail", fnvMan((c) => ({ ...c, path: "Other/CameraUnlock.ini" })), fnv, "is not the tail of every installed path");
fails("exe_dir path a tail that lands off the installed paths", preyMan((c) => ({ ...c, path: "Release/CameraUnlock.ini" })), prey, "lands on binaries/danielle/x64/release/release/cameraunlock.ini, binaries/danielle/gaming.desktop.x64/release/release/cameraunlock.ini beside the executable");
fails("exe_dir bare name of a file below the executable's folder", bepMan((c) => ({ ...c, anchor: "exe_dir", path: "CameraUnlock.ini", legacy_source: "com.cameraunlock.x.cfg" })), bep, "config.path CameraUnlock.ini at exe_dir lands on cameraunlock.ini beside the executable");
fails("exe_dir with no installed path", homeMan((c) => ({ ...c, anchor: "exe_dir" })), home, "anchor is exe_dir, and data/config-format.json records no installed path");
fails("mod_home with installed paths", abzuMan((c) => ({ ...c, anchor: "mod_home" })), abzu, "anchor is mod_home");
fails("legacy_source where the entry has none", abzuMan((c) => ({ ...c, legacy_source: "Old.cfg" })), abzu, "records no legacy_source");
fails("no legacy_source where the entry has one", bepMan(({ legacy_source: _, ...c }) => c), bep, "config has no legacy_source");
fails("legacy_source not the entry's", bepMan((c) => ({ ...c, legacy_source: "BepInEx/config/other.cfg" })), bep, "config.legacy_source BepInEx/config/other.cfg does not name");
fails("legacy repo without canonical_since", abzuMan(({ canonical_since: _, ...c }) => c), abzu, "config has no canonical_since");
fails("canonical_since in a repo with no pre-canonical build", manifest("sleeping-dogs", { ...structuredClone(sdConfig), canonical_since: "1.0.0" }), sd, "never published a pre-canonical build");
fails("stale row value", abzuMan((c) => ({ ...c, rows: { ...c.rows, EnableOnStartup: false } })), abzu, "config.rows is");
fails("committed row left out", abzuMan(({ rows: { TrueFreeLook: _, ...rows }, ...c }) => ({ ...c, rows })), abzu, "config.rows is");
{
  const base = abzuWith("no-free-look", edit(ALL, "TrueFreeLook=false\r\n", ""));
  const { TrueFreeLook: _, ...rows } = ALL_ROWS;
  clean("a file without TrueFreeLook", abzuMan((c) => ({ ...c, rows })), base);
  fails("a row the committed file does not have", abzuMan(), base, "config.rows is");
}
{
  const base = abzuWith("yaw-off-default", edit(ALL, "WorldSpaceYaw=true", "WorldSpaceYaw=false"));
  fails("WorldSpaceYaw away from the default, not omitted", abzuMan((c) => ({ ...c, rows: { ...c.rows, WorldSpaceYaw: false } })), base, "WorldSpaceYaw=false, away from the fleet default true");
  const omitted = synthetic("yaw-off-default-omitted", "subnautica-headtracking", "legacy", [
    { ...bep.state.files[0], text: edit(ALL, "WorldSpaceYaw=true", "WorldSpaceYaw=false") },
  ]);
  clean("WorldSpaceYaw away from the default, omitted", bepMan(), omitted);
}
fails("an omitted row declared", bepMan((c) => ({ ...c, rows: { ...ALL_ROWS } })), bep, "descriptor_omits leaves out WorldSpaceYaw");
{
  const noYaw = synthetic("omit-absent", "subnautica-headtracking", "legacy", [
    { ...bep.state.files[0], text: edit(ALL, "WorldSpaceYaw=true\r\n", "") },
  ]);
  fails("descriptor_omits names a row the file lacks", bepMan(), noYaw, "descriptor_omits lists WorldSpaceYaw for subnautica-headtracking, and Config.ini has no WorldSpaceYaw line");
}
{
  const base = abzuWith("position-not-allowed", edit(ALL, "PositionAllowed=true", "PositionAllowed=false"));
  const { RotationEnabled: _r, PositionEnabled: _p, ...rows } = ALL_ROWS;
  clean("PositionAllowed=false without tracking rows", abzuMan((c) => ({ ...c, rows })), base);
  fails("PositionAllowed=false with tracking rows", abzuMan(), base, "config.rows is");
}
{
  fails("a committed bool not written true or false", abzuMan(), abzuWith("yes-value", edit(ALL, "EnableOnStartup=true", "EnableOnStartup=yes")), "EnableOnStartup=yes is not true or false");
}
fails("files[] over the config", abzuMan(undefined, { files: [{ source: "CameraUnlock.ini", target: "AbzuGame/Binaries/Win64/CameraUnlock.ini" }] }), abzu, "lands on the config");
fails("files[] over one layout of the config", fnvMan(undefined, { files: [{ source: "CameraUnlock.ini", target: "Fallout New Vegas English/CameraUnlock.ini" }] }), fnv, "lands on the config");
fails("files[] in a variant over the config", abzuMan(undefined, { delivery_mode: "manifest_variants", files: undefined, variants: [{ id: "steam", files: [{ source: "a", target: "AbzuGame\\Binaries\\Win64\\CameraUnlock.ini" }] }] }), abzu, "lands on the config");
fails("files[] over the legacy file", bepMan(undefined, { files: [{ source: "a", target: "BepInEx/config/com.cameraunlock.x.cfg" }] }), bep, "lands on the legacy file");
fails("a seed of the config", fnvMan(undefined, { loader: { seed: [{ target: "CameraUnlock.ini", anchor: "exe_dir", content_b64: "" }] } }), fnv, "seed CameraUnlock.ini (exe_dir) writes the config; a package with a config block seeds neither");
fails("a config seed at another anchor", fnvMan(undefined, { loader: { seed: [{ target: "CameraUnlock.ini", content_b64: "" }] } }), fnv, "seed CameraUnlock.ini (game_root) writes the config");
fails("a config seed in a variant", abzuMan(undefined, { delivery_mode: "manifest_variants", variants: [{ id: "steam", loader: { seed: [{ target: "AbzuGame/Binaries/Win64/CameraUnlock.ini", content_b64: "" }] } }] }), abzu, "writes the config");
fails("a seed of the legacy file", bepMan(undefined, { seed: [{ target: "BepInEx\\config\\com.cameraunlock.x.cfg", content_b64: "" }] }), bep, "writes the legacy file BepInEx/config/com.cameraunlock.x.cfg");
fails("an exe_dir seed of the legacy file", exeLegacyMan("Old.ini", { loader: { seed: [{ target: "Old.ini", anchor: "exe_dir", content_b64: "" }] } }), exeLegacy, "writes the legacy file");
fails("a config seed spelled another way", abzuMan(undefined, { seed: [{ target: "abzugame\\binaries\\win64\\cameraunlock.ini", content_b64: "" }] }), abzu, "writes the config");
clean("a seed that writes another file", abzuMan(undefined, { loader: { seed: [{ target: "BepInEx/config/BepInEx.cfg", content_b64: "" }] } }), abzu);
fails("exe_dir for a game data/games.json does not list", manifest("no-such-game", structuredClone(fnvConfig)), fnv, "is not in data/games.json");

// Every rule's mutation above changes one thing; so does each of these, which must stay clean.
clean("rows in another order", abzuMan((c) => ({ ...c, rows: Object.fromEntries(Object.entries(c.rows).reverse()) })), abzu);

// expectedRows is what the generator writes.
check(isDeepStrictEqual(expectedRows(abzu.root, abzu.state).rows, ALL_ROWS), "expectedRows should read the five rows of all-concepts.ini");

// The generator: encode-seed rewrites config.rows and nothing else.
function runScript(script, ...args) {
  const r = spawnSync(process.execPath, [path.join(SCRIPTS, script), ...args], { encoding: "utf8" });
  return { status: r.status, out: r.stdout + r.stderr };
}
{
  const preyText = fs.readFileSync(path.join(CORE_ROOT, "data", "fixtures", "encode-seed", "prey-headtracking.launcher-manifest.json"), "utf8");
  const block = '  "config": {\n    "path": "CameraUnlock.ini",\n    "anchor": "exe_dir",\n    "canonical_since": "1.1.0",\n    "rows": {}\n  },\n';
  const staleText = edit(preyText, '  "delivery_mode": "manifest",\n', `  "delivery_mode": "manifest",\n${block}`);
  const root = repo("generator", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": staleText });
  const seedsBefore = JSON.parse(staleText).loader.seed;

  const before = runScript("encode-seed.mjs", "--check", root);
  check(before.status === 1 && before.out.includes("STALE config.rows"), `generator: --check on stale rows should exit 1, got ${before.status}\n${before.out}`);
  check(fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8") === staleText, "generator: --check should not write");

  const wrote = runScript("encode-seed.mjs", root);
  check(wrote.status === 0 && wrote.out.includes("wrote   config.rows"), `generator: encoding should exit 0, got ${wrote.status}\n${wrote.out}`);
  const text = fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8");
  const rowsText = '"rows": {\n      "EnableOnStartup": true,\n      "WorldSpaceYaw": true,\n      "RotationEnabled": true,\n      "PositionEnabled": true,\n      "TrueFreeLook": false\n    }';
  const seedB64 = Buffer.from(ALL, "latin1").toString("base64");
  const expectedText = edit(staleText, '"rows": {}', rowsText).split(`"${seedsBefore[0].content_b64}"`).join(`"${seedB64}"`);
  check(text === expectedText, "generator: only config.rows and the stale seed should change, rows one per line at the file's indent");

  const after = runScript("encode-seed.mjs", "--check", root);
  check(after.status === 0 && after.out.includes("ok    config.rows match HeadTracking.ini"), `generator: --check after encoding should exit 0, got ${after.status}\n${after.out}`);
  runScript("encode-seed.mjs", root);
  check(fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8") === text, "generator: encoding current rows should change nothing");
  // The fixture keeps its seed of HeadTracking.ini to show both rewritten. Against prey's entry
  // recorded under CameraUnlock.ini, that seed writes another file and the rows pass.
  const ruled = problemsOf(JSON.parse(text), { root, state: syntheticState(root, "prey-headtracking", "legacy", prey.state.files) }, false);
  check(ruled.length === 0, `generator: the rows it writes should pass the rules, got ${JSON.stringify(ruled)}`);

  const crlf = repo("generator-crlf", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": staleText.replace(/\n/g, "\r\n") });
  runScript("encode-seed.mjs", crlf);
  check(fs.readFileSync(path.join(crlf, "launcher-manifest.json"), "utf8") === expectedText.replace(/\n/g, "\r\n"), "generator: a CRLF manifest should get CRLF rows");

  const noRows = repo("generator-no-rows", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": edit(staleText, ',\n    "rows": {}', "") });
  const noRowsRun = runScript("encode-seed.mjs", noRows);
  check(noRowsRun.status !== 0 && noRowsRun.out.includes('add "rows": {}'), `generator: a block without rows should be refused, got ${noRowsRun.status}\n${noRowsRun.out}`);

  const unstamped = repo("generator-unstamped", "prey-headtracking", { "HeadTracking.ini": LEGACY_INI, "launcher-manifest.json": edit(staleText, '"loader": {', '"loader_": {') });
  const unstampedRun = runScript("encode-seed.mjs", unstamped);
  check(unstampedRun.status !== 0 && unstampedRun.out.includes("which is unstamped"), `generator: rows from an unstamped file should be refused, got ${unstampedRun.status}\n${unstampedRun.out}`);
}

// validate-manifest runs the rules on a built ZIP, against the repo whose release/ folder holds it.
// It reads the repo's real data/config-format.json entry, abzu-headtracking's, which is recorded
// under HeadTracking.ini, so a block naming CameraUnlock.ini there also fails for its path; each
// case checks that the rule it is about is among the problems.
function zipRepo(label, config, name = "abzu-headtracking") {
  const man = abzuMan(() => config);
  const root = repo(label, name, { "HeadTracking.ini": ALL });
  const staging = path.join(scratch, label, "staging");
  fs.mkdirSync(path.join(staging, "plugins"), { recursive: true });
  fs.writeFileSync(path.join(staging, "launcher-manifest.json"), JSON.stringify(man, null, 2));
  fs.writeFileSync(path.join(staging, "plugins", "Mod.dll"), "dll");
  fs.mkdirSync(path.join(root, "release"));
  const zip = path.join(root, "release", "Mod-v1.2.0-installer.zip");
  const tar = spawnSync(process.platform === "win32" ? path.join(process.env.SystemRoot || "C:\\Windows", "System32", "tar.exe") : "tar", ["-a", "-cf", zip, "-C", staging, "launcher-manifest.json", "plugins"], { encoding: "utf8" });
  if (tar.status !== 0) throw new Error(`tar failed: ${tar.stderr}`);
  return zip;
}
{
  const inPlace = runScript("validate-manifest.mjs", zipRepo("zip-in-place", { ...structuredClone(abzuConfig), path: "AbzuGame/Binaries/Win64/HeadTracking.ini" }));
  check(inPlace.status === 1 && inPlace.out.includes("is not named CameraUnlock.ini"), `validate-manifest: a block naming the file a pre-canonical release reads should fail, got ${inPlace.status}\n${inPlace.out}`);
  const stale = runScript("validate-manifest.mjs", zipRepo("zip-stale", { ...structuredClone(abzuConfig), rows: { ...ALL_ROWS, EnableOnStartup: false } }));
  check(stale.status === 1 && stale.out.includes("config.rows is"), `validate-manifest: stale rows should fail, got ${stale.status}\n${stale.out}`);
  const version = runScript("validate-manifest.mjs", zipRepo("zip-version", { ...structuredClone(abzuConfig), canonical_since: "2.0.0" }));
  check(version.status === 1 && version.out.includes("is above mod_info.version 1.2.0"), `validate-manifest: canonical_since above the ZIP's version should fail, got ${version.status}\n${version.out}`);
  const loose = zipRepo("zip-loose", structuredClone(abzuConfig));
  const moved = path.join(scratch, "zip-loose", "Mod-v1.2.0-installer.zip");
  fs.renameSync(loose, moved);
  const looseRun = runScript("validate-manifest.mjs", moved);
  check(looseRun.status === 1 && looseRun.out.includes("name the repo"), `validate-manifest: a descriptor ZIP outside a release/ folder should fail, got ${looseRun.status}\n${looseRun.out}`);
}

// The report conformance reads, and the canonical_since rule over tags.
{
  const reworked = abzuWith("report-no-block", ALL);
  fs.writeFileSync(path.join(reworked.root, "launcher-manifest.json"), JSON.stringify(abzuMan(() => undefined)));
  const r = repoReport(reworked.root, reworked.state);
  check(r.applies && !r.has_block && r.problems.length === 1 && r.problems[0].includes("has no config block"), `report: a converted manifest repo without a block should fail for it, got ${JSON.stringify(r)}`);
  const inPlaceRoot = repo("report-in-place", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined)) });
  const inPlace = repoReport(inPlaceRoot);
  check(inPlace.converted && !inPlace.applies && inPlace.problems.length === 0, `report: a repo converted in place, its file not yet CameraUnlock.ini, should not be asked for a block, got ${JSON.stringify(inPlace)}`);
  const unconverted = repoReport(repo("report-unconverted", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined)) }));
  check(!unconverted.applies && unconverted.problems.length === 0, `report: an unconverted repo without a block should not apply, got ${JSON.stringify(unconverted)}`);
  const unrecordedRoot = repo("report-unrecorded", "far-cry-6-headtracking", { "config/FarCry6HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(manifest("far-cry-6", undefined)) });
  git(unrecordedRoot, "add", "-A");
  const unrecorded = repoReport(unrecordedRoot);
  check(unrecorded.converted && !unrecorded.applies && unrecorded.problems.length === 0, `report: a stamped file data/config-format.json does not record should leave the block to config-format, got ${JSON.stringify(unrecorded)}`);
  const installCmd = repoReport(repo("report-install-cmd", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined, { delivery_mode: "install_cmd" })) }));
  check(!installCmd.applies, "report: an install_cmd repo should not apply");

  const tagged = repo("tags", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI });
  git(tagged, "add", "-A");
  git(tagged, "commit", "-q", "-m", "legacy");
  git(tagged, "tag", "v1.0.0");
  git(tagged, "tag", "v1.1.0-beta");
  fs.writeFileSync(path.join(tagged, "HeadTracking.ini"), Buffer.from(ALL, "latin1"));
  git(tagged, "add", "-A");
  git(tagged, "commit", "-q", "-m", "canonical");
  git(tagged, "tag", "v1.1.0");
  fs.writeFileSync(path.join(tagged, "launcher-manifest.json"), JSON.stringify(abzuMan()));
  const taggedState = syntheticState(tagged, "abzu-headtracking", "legacy", [ABZU_ENTRY]);
  const good = repoReport(tagged, taggedState);
  check(good.problems.length === 0 && !good.shallow, `tags: canonical_since 1.1.0 is above v1.0.0 and v1.1.0-beta, got ${JSON.stringify(good.problems)}`);
  fs.writeFileSync(path.join(tagged, "launcher-manifest.json"), JSON.stringify(abzuMan((c) => ({ ...c, canonical_since: "1.0.0" }))));
  const low = repoReport(tagged, taggedState);
  check(low.problems.some((p) => p.includes("is not above v1.0.0")), `tags: canonical_since 1.0.0 should fail against v1.0.0, got ${JSON.stringify(low.problems)}`);

  const shallowRoot = path.join(scratch, "shallow", "abzu-headtracking");
  const clone = spawnSync("git", ["clone", "-q", "--depth", "1", `file://${tagged.replace(/\\/g, "/")}`, shallowRoot], { encoding: "utf8" });
  if (clone.status !== 0) throw new Error(`git clone failed: ${clone.stderr}`);
  fs.writeFileSync(path.join(shallowRoot, "launcher-manifest.json"), JSON.stringify(abzuMan((c) => ({ ...c, canonical_since: "1.0.0" }))));
  const shallow = repoReport(shallowRoot, syntheticState(shallowRoot, "abzu-headtracking", "legacy", [ABZU_ENTRY]));
  check(shallow.shallow && shallow.problems.length === 0, `tags: a shallow clone should be reported as such, got ${JSON.stringify(shallow)}`);

  // conformance's config-descriptor check over real entries: nothing for a repo converted in
  // place without a block, and a FAIL for one whose block names the file its old releases read.
  const inPlaceBlock = repo("conformance-in-place", "abzu-headtracking", {
    "HeadTracking.ini": ALL,
    "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, path: "AbzuGame/Binaries/Win64/HeadTracking.ini" }))),
  });
  const conformance = spawnSync(
    "powershell",
    ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `& '${path.join(SCRIPTS, "conformance.ps1")}' -Repo '${inPlaceRoot}','${inPlaceBlock}' -Check config-descriptor -Json; exit $LASTEXITCODE`],
    { encoding: "utf8" },
  );
  const findings = JSON.parse(conformance.stdout.replace(/^\uFEFF/, "") || "[]");
  const list = Array.isArray(findings) ? findings : [findings];
  check(
    conformance.status === 1 && list.length === 1 && list[0].check === "config-descriptor" && list[0].severity === "FAIL" &&
      list[0].message.includes("is not named CameraUnlock.ini"),
    `conformance: config-descriptor should fail only the block that names the file old releases read, got ${conformance.status}\n${conformance.stdout}${conformance.stderr}`,
  );
}

// Copy-SharedBundle runs Assert-LauncherManifestConfig on the committed manifest, so a package
// script that never calls validate-manifest still refuses a broken block.
{
  const module = path.join(CORE_ROOT, "powershell", "ReleaseWorkflow.psm1");
  const assertConfig = (root) =>
    spawnSync(
      "powershell",
      ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `Import-Module '${module}' -Force; try { Assert-LauncherManifestConfig -RepoRoot '${root}' -CoreRoot '${CORE_ROOT}'; exit 0 } catch { Write-Output $_.Exception.Message; exit 1 }`],
      { encoding: "utf8" },
    );
  const inPlace = assertConfig(repo("assert-in-place", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, path: "AbzuGame/Binaries/Win64/HeadTracking.ini" }))) }));
  check(inPlace.status === 1 && inPlace.stdout.includes("is not named CameraUnlock.ini"), `packaging: a block naming the file a pre-canonical release reads should fail, got ${inPlace.status}\n${inPlace.stdout}${inPlace.stderr}`);
  const stale = assertConfig(repo("assert-stale", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, rows: { ...c.rows, EnableOnStartup: false } }))) }));
  check(stale.status === 1 && stale.stdout.includes("config.rows is"), `packaging: stale rows should fail, got ${stale.status}\n${stale.stdout}${stale.stderr}`);
  const none = assertConfig(repo("assert-none", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined)) }));
  check(none.status === 0, `packaging: a manifest with no block should pass, got ${none.status}\n${none.stdout}${none.stderr}`);
}

// Packaging stamps the version through ConvertFrom-Json and ConvertTo-Json -Depth 10 in Windows
// PowerShell (scripts/package-bepinex-mod.ps1), which must carry the block through unchanged.
{
  const man = bepMan();
  const file = path.join(scratch, "roundtrip.json");
  fs.writeFileSync(file, JSON.stringify(man, null, 2));
  const ps = spawnSync(
    "powershell",
    ["-NoProfile", "-Command", `(Get-Content -LiteralPath '${file}' -Raw | ConvertFrom-Json) | ConvertTo-Json -Depth 10`],
    { encoding: "utf8" },
  );
  check(ps.status === 0 && isDeepStrictEqual(JSON.parse(ps.stdout), man), `packaging round trip: the config block should survive, got ${ps.status}\n${ps.stdout}${ps.stderr}`);
}

fs.rmSync(scratch, { recursive: true, force: true });

if (failures.length > 0) {
  console.error(`${failures.length} of ${checks} checks failed:`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`config-descriptor: ${checks} checks passed`);
