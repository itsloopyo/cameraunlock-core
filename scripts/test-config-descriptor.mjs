#!/usr/bin/env node

// Holds scripts/check-config-descriptor.mjs to its rules: a descriptor that meets every rule is
// clean, and one mutation per rule fails with that rule's message. Repos are throwaway git
// checkouts named after data/config-format.json entries. Real entries that record a committed
// file are read through repoState: abzu-headtracking (one installed path),
// fallout-new-vegas-headtracking and prey-headtracking (two, beside two executables), each with
// its legacy file beside CameraUnlock.ini, and sleeping-dogs-headtracking (never published a
// pre-canonical build, so no legacy file). The rest are synthetic: subnautica-headtracking's
// BepInEx entry with a committed file, whose real entry records none yet, and shapes no repo has
// (a mod_home file, two config files). It also holds the rule that a converted repo's manifest
// seeds and ships no config, block or not, and runs the per_game generator in encode-seed.mjs, the
// rules in validate-manifest.mjs on built ZIPs, the Nexus ZIP config rule, the report conformance
// reads, conformance's config-descriptor and config-preserve checks, and the packager's ConvertFrom-Json /
// ConvertTo-Json -Depth 10 round trip.
//
//   node scripts/test-config-descriptor.mjs      (pixi run test-config-descriptor, part of pixi run check)

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { isDeepStrictEqual } from "node:util";
import { fileURLToPath } from "node:url";

import { manualZipConfigEntries, repoState } from "./check-canonical-config.mjs";
import { configWriteProblems, descriptorProblems, expectedPerGame, repoReport } from "./check-config-descriptor.mjs";
import { encodePerGame } from "./encode-seed.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SCRIPTS = path.join(CORE_ROOT, "scripts");
// What render-config commits: default on every concept row a table does not mark PerGame().
const ALL = fs.readFileSync(path.join(CORE_ROOT, "data", "fixtures", "canonical-ini", "head-tracking", "all-concepts-fresh.ini"), "latin1");
// The same file for subnautica-headtracking, whose per_game lists WorldSpaceYaw.
const YAW_KEPT = ALL.replace("WorldSpaceYaw=default", "WorldSpaceYaw=false");
const LEGACY_INI = "[General]\r\nEnabled=1\r\n";
const FORMAT = JSON.parse(fs.readFileSync(path.join(CORE_ROOT, "data", "config-format.json"), "utf8"));

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

// A throwaway checkout of a real entry, with text at the committed path it records, and its state.
function real(label, name, text = ALL) {
  const [{ committed }] = FORMAT.configs[name];
  const root = repo(label, name, { [committed]: text });
  return { root, state: repoState(root) };
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
const abzuWith = (label, text) => real(label, "abzu-headtracking", text);
const abzu = abzuWith("abzu", ALL);
const abzuConfig = {
  path: "AbzuGame/Binaries/Win64/CameraUnlock.ini",
  anchor: "game_root",
  legacy_source: "AbzuGame/Binaries/Win64/HeadTracking.ini",
  canonical_since: "1.1.0",
  per_game: {},
};
const abzuMan = (change = (c) => c, extra = {}) => manifest("abzu", change(structuredClone(abzuConfig)), extra);
clean("abzu", abzuMan(), abzu);
clean("abzu without anchor, which is game_root", abzuMan(({ anchor, ...c }) => c), abzu);

// fallout-new-vegas-headtracking: two installed paths, so exe_dir with the tail of both.
const fnv = real("fnv", "fallout-new-vegas-headtracking");
const fnvConfig = { path: "CameraUnlock.ini", anchor: "exe_dir", legacy_source: "HeadTracking.ini", canonical_since: "1.1.0", per_game: {} };
const fnvMan = (change = (c) => c, extra = {}) => manifest("fallout-new-vegas", change(structuredClone(fnvConfig)), extra);
clean("fallout-new-vegas", fnvMan(), fnv);

// prey-headtracking: two installed paths beside two executables.
const prey = real("prey", "prey-headtracking");
const preyMan = (change = (c) => c) =>
  manifest("prey", change({ path: "CameraUnlock.ini", anchor: "exe_dir", legacy_source: "HeadTracking.ini", canonical_since: "1.1.0", per_game: {} }));
clean("prey", preyMan(), prey);

// sleeping-dogs-headtracking: never published a pre-canonical build, so no canonical_since.
const sd = real("sleeping-dogs", "sleeping-dogs-headtracking");
const sdConfig = { path: "CameraUnlock.ini", per_game: {} };
clean("sleeping-dogs", manifest("sleeping-dogs", structuredClone(sdConfig)), sd);

// subnautica-headtracking's BepInEx entry, legacy, with per_game WorldSpaceYaw, given a
// committed file.
const BEP_LEGACY = FORMAT.configs["subnautica-headtracking"][0].legacy_source;
const BEP_ENTRY = { ...FORMAT.configs["subnautica-headtracking"][0], committed: "Config.ini" };
const bepWith = (label, text) => synthetic(label, "subnautica-headtracking", "legacy", [{ ...BEP_ENTRY, text }]);
const bep = bepWith("bepinex", YAW_KEPT);
const bepConfig = { path: "BepInEx/config/CameraUnlock.ini", legacy_source: `BepInEx/config/${BEP_LEGACY}`, canonical_since: "1.1.0", per_game: { WorldSpaceYaw: "false" } };
const bepMan = (change = (c) => c, extra = {}) => manifest("subnautica", change(structuredClone(bepConfig)), extra);
clean("bepinex with legacy_source and a per_game row", bepMan(), bep);

// A file outside the game folder.
const home = synthetic("mod-home", "mod-home-headtracking", "mover", [
  { committed: "HeadTracking.ini", installed: [], no_installed_reason: "beside the DLL" },
]);
const homeMan = (change = (c) => c) => manifest("abzu", change({ path: "CameraUnlock.ini", anchor: "mod_home", per_game: {} }));
clean("mod_home", homeMan(), home);


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
fails(
  "path the legacy file",
  abzuMan((c) => ({ ...c, path: "AbzuGame/Binaries/Win64/HeadTracking.ini" })),
  abzu,
  "config.path AbzuGame/Binaries/Win64/HeadTracking.ini is not named CameraUnlock.ini",
);
fails("unknown anchor", abzuMan((c) => ({ ...c, anchor: "install_dir" })), abzu, 'config.anchor "install_dir" is not one of');
const ROWS_REFUSED = "config has rows, which the descriptor no longer carries";
fails("rows beside per_game", abzuMan((c) => ({ ...c, rows: {} })), abzu, ROWS_REFUSED);
fails("rows in place of per_game", abzuMan(({ per_game: _, ...c }) => ({ ...c, rows: { EnableOnStartup: true } })), abzu, ROWS_REFUSED);
fails("no per_game", abzuMan(({ per_game: _, ...c }) => c), abzu, "config has no per_game");
fails("per_game not an object", abzuMan((c) => ({ ...c, per_game: [] })), abzu, "config.per_game must be an object");
fails("per_game value not text", bepMan((c) => ({ ...c, per_game: { WorldSpaceYaw: false } })), bep, "config.per_game.WorldSpaceYaw is false, not the text");
for (const token of ["default", "Default", "DEFAULT", " default\t"]) {
  fails(`per_game value ${JSON.stringify(token)}`, bepMan((c) => ({ ...c, per_game: { WorldSpaceYaw: token } })), bep, "never the default token");
}
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
  fails("two config files", manifest("abzu", { path: "CameraUnlock.ini", per_game: {} }), two, "records 2 config files for two-files-headtracking; a package with more than one declares no descriptor");
}
fails("game_root path not the installed one", abzuMan((c) => ({ ...c, path: "CameraUnlock.ini" })), abzu, "config.path CameraUnlock.ini is not AbzuGame/Binaries/Win64/CameraUnlock.ini");
fails("game_root with two installed paths", fnvMan((c) => ({ ...c, anchor: "game_root" })), fnv, "records 2 installed paths");
fails("game_root with no installed path", homeMan((c) => ({ ...c, anchor: "game_root" })), home, "anchor is game_root, and data/config-format.json records no installed path");
fails("exe_dir path not a tail", fnvMan((c) => ({ ...c, path: "Other/CameraUnlock.ini" })), fnv, "is not the tail of every installed path");
fails("exe_dir path a tail that lands off the installed paths", preyMan((c) => ({ ...c, path: "Release/CameraUnlock.ini" })), prey, "lands on binaries/danielle/x64/release/release/cameraunlock.ini, binaries/danielle/gaming.desktop.x64/release/release/cameraunlock.ini beside the executable");
fails("exe_dir bare name of a file below the executable's folder", bepMan((c) => ({ ...c, anchor: "exe_dir", path: "CameraUnlock.ini", legacy_source: BEP_LEGACY })), bep, "config.path CameraUnlock.ini at exe_dir lands on cameraunlock.ini beside the executable");
fails("exe_dir with no installed path", homeMan((c) => ({ ...c, anchor: "exe_dir" })), home, "anchor is exe_dir, and data/config-format.json records no installed path");
fails("mod_home with installed paths", abzuMan((c) => ({ ...c, anchor: "mod_home" })), abzu, "anchor is mod_home");
fails("legacy_source where the entry has none", manifest("sleeping-dogs", { ...structuredClone(sdConfig), legacy_source: "headtrack.ini" }), sd, "records no legacy_source");
fails("no legacy_source where the entry has one", bepMan(({ legacy_source: _, ...c }) => c), bep, "config has no legacy_source");
fails("legacy_source not the entry's", bepMan((c) => ({ ...c, legacy_source: "BepInEx/config/other.cfg" })), bep, `config.legacy_source BepInEx/config/other.cfg is not BepInEx/config/${BEP_LEGACY}`);
fails("legacy_source in another folder", abzuMan((c) => ({ ...c, legacy_source: "HeadTracking.ini" })), abzu, "config.legacy_source HeadTracking.ini is not AbzuGame/Binaries/Win64/HeadTracking.ini");
fails("exe_dir legacy_source spelled game-relative", fnvMan((c) => ({ ...c, legacy_source: "Fallout New Vegas English/HeadTracking.ini" })), fnv, "config.legacy_source Fallout New Vegas English/HeadTracking.ini is not HeadTracking.ini");
fails("legacy repo without canonical_since", abzuMan(({ canonical_since: _, ...c }) => c), abzu, "config has no canonical_since");
fails("canonical_since in a repo with no pre-canonical build", manifest("sleeping-dogs", { ...structuredClone(sdConfig), canonical_since: "1.0.0" }), sd, "never published a pre-canonical build");
fails("a per_game id data/config-format.json lists, missing", bepMan((c) => ({ ...c, per_game: {} })), bep, "config.per_game has no WorldSpaceYaw, which data/config-format.json per_game lists for subnautica-headtracking");
fails("a per_game id data/config-format.json does not list", abzuMan((c) => ({ ...c, per_game: { WorldSpaceYaw: "false" } })), abzu, "config.per_game names WorldSpaceYaw, which data/config-format.json per_game does not list for abzu-headtracking");
fails("an extra per_game id beside a listed one", bepMan((c) => ({ ...c, per_game: { ...c.per_game, UdpPort: "4243" } })), bep, "config.per_game names UdpPort");
fails("a stale per_game value", bepMan((c) => ({ ...c, per_game: { WorldSpaceYaw: "true" } })), bep, 'config.per_game.WorldSpaceYaw is "true", and Config.ini holds "false" there');
fails("a per_game value spelled another way", bepMan((c) => ({ ...c, per_game: { WorldSpaceYaw: "False" } })), bep, 'config.per_game.WorldSpaceYaw is "False", and Config.ini holds "false" there');
{
  const noYaw = bepWith("per-game-absent", edit(ALL, "WorldSpaceYaw=default\r\n", ""));
  fails("per_game names a row the file lacks", bepMan(), noYaw, "per_game lists WorldSpaceYaw for subnautica-headtracking, and Config.ini has no WorldSpaceYaw line");
  const tokened = bepWith("per-game-token", ALL);
  fails("a per_game row the file holds default on", bepMan(), tokened, "Config.ini line 23: [General] WorldSpaceYaw=default, and data/config-format.json per_game lists WorldSpaceYaw");
  const commented = bepWith("per-game-commented", edit(ALL, "WorldSpaceYaw=default", "; WorldSpaceYaw=false"));
  clean("a per_game row commented out at the game's own default", bepMan(), commented);
  const commentedElsewhere = bepWith("per-game-commented-elsewhere", edit(edit(ALL, "WorldSpaceYaw=default\r\n", ""), "[Network]\r\n", "[Network]\r\n; WorldSpaceYaw=false\r\n"));
  fails("a per_game row commented out under another section", bepMan(), commentedElsewhere, "Config.ini has no WorldSpaceYaw line");
}
// The rows a launcher once read no longer draw anything from the descriptor: a value on a row
// per_game does not list is the lint's finding, and PositionAllowed no longer changes the block.
clean("a value on a row per_game does not list", abzuMan(), abzuWith("yaw-value", edit(ALL, "WorldSpaceYaw=default", "WorldSpaceYaw=false")));
clean("PositionAllowed=false", abzuMan(), abzuWith("position-not-allowed", edit(ALL, "PositionAllowed=default", "PositionAllowed=false")));
fails("exe_dir for a game data/games.json does not list", manifest("no-such-game", structuredClone(fnvConfig)), fnv, "is not in data/games.json");

// The write rule, which runs on a converted repo's manifest with a config block or without one:
// no seed or files[] row writes CameraUnlock.ini, the legacy file or a file named like the
// committed config, in any folder and at any anchor.
function writes(label, man, base, expected) {
  const problems = configWriteProblems(man, base.state);
  const ok = expected === null ? problems.length === 0 : problems.some((p) => p.includes(expected));
  check(ok, `${label}: should ${expected === null ? "be clean" : `fail with "${expected}"`}, got ${JSON.stringify(problems)}`);
}
const seed = (target, anchor) => ({ target, content_b64: "", ...(anchor ? { anchor } : {}) });
const SEEDS_NOTHING = "a converted release seeds nothing";
writes("files[] over the config", abzuMan(undefined, { files: [{ source: "CameraUnlock.ini", target: "AbzuGame/Binaries/Win64/CameraUnlock.ini" }] }), abzu, "lands on CameraUnlock.ini");
writes("files[] in a variant over the config", abzuMan(undefined, { delivery_mode: "manifest_variants", files: undefined, variants: [{ id: "steam", files: [{ source: "a", target: "AbzuGame\\Binaries\\Win64\\CameraUnlock.ini" }] }] }), abzu, "lands on CameraUnlock.ini");
writes("files[] over the legacy file", bepMan(undefined, { files: [{ source: "a", target: `BepInEx/config/${BEP_LEGACY}` }] }), bep, `lands on the legacy file ${BEP_LEGACY}`);
writes("a seed of the config", fnvMan(undefined, { loader: { seed: [seed("CameraUnlock.ini", "exe_dir")] } }), fnv, `seed CameraUnlock.ini (exe_dir) writes CameraUnlock.ini, and ${SEEDS_NOTHING}`);
writes("a config seed in a variant", abzuMan(undefined, { delivery_mode: "manifest_variants", variants: [{ id: "steam", loader: { seed: [seed("AbzuGame/Binaries/Win64/CameraUnlock.ini")] } }] }), abzu, SEEDS_NOTHING);
writes("a top-level loader seed beside variants", abzuMan(undefined, { delivery_mode: "manifest_variants", loader: { seed: [seed("AbzuGame/Binaries/Win64/CameraUnlock.ini")] }, variants: [{ id: "steam" }] }), abzu, SEEDS_NOTHING);
writes("a config seed spelled another way", abzuMan(undefined, { seed: [seed("abzugame\\binaries\\win64\\cameraunlock.ini")] }), abzu, SEEDS_NOTHING);
writes("a seed of the legacy file", bepMan(undefined, { seed: [seed(`BepInEx\\config\\${BEP_LEGACY}`)] }), bep, `writes the legacy file ${BEP_LEGACY}`);
writes("a seed of one layout's legacy file", fnvMan(undefined, { seed: [seed("Fallout New Vegas English\\HeadTracking.ini")] }), fnv, "writes the legacy file HeadTracking.ini");
writes("a legacy seed with no config block", manifest("fallout-new-vegas", undefined, { loader: { seed: [seed("HeadTracking.ini", "exe_dir")] } }), fnv, `writes the legacy file HeadTracking.ini, and ${SEEDS_NOTHING}`);
writes("a seed at mod_home", manifest("abzu", undefined, { seed: [seed("CameraUnlock.ini", "mod_home")] }), home, SEEDS_NOTHING);
writes("a seed named like the committed file", manifest("sleeping-dogs", undefined, { seed: [seed("headtrack.ini")] }), sd, "writes headtrack.ini, the committed config's name");
{
  const two = synthetic("two-files-writes", "two-files-headtracking", "mover", [
    { committed: "A.ini", installed: ["A.ini"] },
    { committed: "B.ini", installed: ["B.ini"] },
  ]);
  writes("a seed in a repo with two config files", manifest("abzu", undefined, { seed: [seed("B.ini")] }), two, "writes B.ini, the committed config's name");
}
{
  const root = repo("writes-unconverted", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI });
  writes("a legacy seed in an unconverted repo", manifest("abzu", undefined, { seed: [seed("AbzuGame/Binaries/Win64/HeadTracking.ini")] }), { root, state: repoState(root) }, null);
}
writes("a seed that writes another file", abzuMan(undefined, { loader: { seed: [seed("BepInEx/config/BepInEx.cfg")] } }), abzu, null);
check(
  problemsOf(fnvMan(undefined, { loader: { seed: [seed("CameraUnlock.ini", "exe_dir")] } }), fnv).length === 0,
  "the block rules should leave a seed to the write rule, so validate-manifest and conformance report it once",
);

// The Nexus ZIP rule validate-manifest runs: an entry on the config or on the legacy file beside
// any installed path, at its path or a tail of it, and nothing else.
{
  const zipHits = (base, entries) => manualZipConfigEntries(base.state, entries);
  const same = (got, want, label) => check(isDeepStrictEqual(got, want), `nexus zip: ${label}, got ${JSON.stringify(got)}`);
  same(zipHits(abzu, ["AbzuGame/Binaries/Win64/CameraUnlock.ini"]), ["AbzuGame/Binaries/Win64/CameraUnlock.ini"], "the config at its installed path");
  same(zipHits(abzu, ["AbzuGame\\Binaries\\Win64\\HeadTracking.ini"]),["AbzuGame/Binaries/Win64/HeadTracking.ini"], "the legacy file beside it");
  same(zipHits(abzu, ["headtracking.ini"]), ["headtracking.ini"], "the legacy file in a flat ZIP");
  same(zipHits(fnv, ["Fallout New Vegas English/HeadTracking.ini"]), ["Fallout New Vegas English/HeadTracking.ini"], "the legacy file beside one layout's config");
  same(zipHits(bep, [`BepInEx/config/${BEP_LEGACY}`, "BepInEx/config/", "BepInEx/plugins/Mod.dll"]), [`BepInEx/config/${BEP_LEGACY}`], "the BepInEx .cfg, and no folder or plugin");
  same(zipHits(sd, ["headtrack.ini", "CameraUnlock.ini"]), ["CameraUnlock.ini"], "no legacy file where the entry records none");
  same(zipHits(abzu, ["AbzuGame/Binaries/Win64/Other.ini", "Win64/HeadTracking.ini.bak"]), [], "another file");
}

// Every rule's mutation above changes one thing; so does each of these, which must stay clean.
// expectedPerGame is what the generator writes.
check(isDeepStrictEqual(expectedPerGame(bep.root, bep.state).perGame, { WorldSpaceYaw: "false" }), "expectedPerGame should read the per_game row subnautica-headtracking keeps");
check(isDeepStrictEqual(expectedPerGame(abzu.root, abzu.state).perGame, {}), "expectedPerGame should be empty for a repo per_game lists nothing for");

// The generator: encode-seed rewrites config.per_game and nothing else, the fixture's seed of the
// legacy file included.
function runScript(script, ...args) {
  const r = spawnSync(process.execPath, [path.join(SCRIPTS, script), ...args], { encoding: "utf8" });
  return { status: r.status, out: r.stdout + r.stderr };
}
{
  const preyText = fs.readFileSync(path.join(CORE_ROOT, "data", "fixtures", "encode-seed", "prey-headtracking.launcher-manifest.json"), "utf8");
  const blockWith = (perGame) =>
    `  "config": {\n    "path": "CameraUnlock.ini",\n    "anchor": "exe_dir",\n    "legacy_source": "HeadTracking.ini",\n    "canonical_since": "1.1.0",\n    ${perGame}\n  },\n`;
  const withBlock = (perGame) => edit(preyText, '  "delivery_mode": "manifest",\n', `  "delivery_mode": "manifest",\n${blockWith(perGame)}`);
  const staleText = withBlock('"per_game": { "WorldSpaceYaw": "true" }');
  const root = repo("generator", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": staleText });

  const before = runScript("encode-seed.mjs", "--check", root);
  check(before.status === 1 && before.out.includes("STALE config.per_game"), `generator: --check on a stale per_game should exit 1, got ${before.status}\n${before.out}`);
  check(fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8") === staleText, "generator: --check should not write");

  const wrote = runScript("encode-seed.mjs", root);
  check(wrote.status === 0 && wrote.out.includes("wrote   config.per_game"), `generator: encoding should exit 0, got ${wrote.status}\n${wrote.out}`);
  const text = fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8");
  const expectedText = withBlock('"per_game": {}');
  check(text === expectedText, "generator: only config.per_game should change, the fixture's seed included");

  const after = runScript("encode-seed.mjs", "--check", root);
  check(after.status === 0 && after.out.includes("ok    config.per_game matches HeadTracking.ini"), `generator: --check after encoding should exit 0, got ${after.status}\n${after.out}`);
  runScript("encode-seed.mjs", root);
  check(fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8") === text, "generator: encoding a current per_game should change nothing");
  const ruled = problemsOf(JSON.parse(text), { root, state: repoState(root) }, false);
  check(ruled.length === 0, `generator: the per_game it writes should pass the rules, got ${JSON.stringify(ruled)}`);

  const crlf = repo("generator-crlf", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": staleText.replace(/\n/g, "\r\n") });
  runScript("encode-seed.mjs", crlf);
  check(fs.readFileSync(path.join(crlf, "launcher-manifest.json"), "utf8") === expectedText.replace(/\n/g, "\r\n"), "generator: a CRLF manifest should keep its CRLF");

  // A repo per_game lists a row for, through the synthetic subnautica-headtracking entry: one row
  // per line at the file's indent, a stale value rewritten, every other byte kept.
  for (const [label, from] of [
    ["empty", '"per_game": {}'],
    ["stale", '"per_game": {\n      "WorldSpaceYaw": "true"\n    }'],
  ]) {
    const kept = bepWith(`generator-per-game-${label}`, YAW_KEPT);
    fs.writeFileSync(path.join(kept.root, "launcher-manifest.json"), withBlock(from));
    const result = encodePerGame(kept.root, kept.state);
    check(
      result.perGame.stale && result.updated === withBlock('"per_game": {\n      "WorldSpaceYaw": "false"\n    }'),
      `generator: a ${label} per_game should get the committed WorldSpaceYaw, got\n${result.updated}`,
    );
  }

  const noPerGame = repo("generator-no-per-game", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": edit(staleText, ',\n    "per_game": { "WorldSpaceYaw": "true" }', "") });
  const noPerGameRun = runScript("encode-seed.mjs", noPerGame);
  check(noPerGameRun.status !== 0 && noPerGameRun.out.includes('add "per_game": {}'), `generator: a block without per_game should be refused, got ${noPerGameRun.status}\n${noPerGameRun.out}`);

  const rowsText = withBlock('"rows": {}');
  const rows = repo("generator-rows", "prey-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": rowsText });
  const rowsRun = runScript("encode-seed.mjs", rows);
  check(
    rowsRun.status !== 0 && rowsRun.out.includes('replace it with "per_game": {}') && fs.readFileSync(path.join(rows, "launcher-manifest.json"), "utf8") === rowsText,
    `generator: a block with rows should be refused and left as it was, got ${rowsRun.status}\n${rowsRun.out}`,
  );

  const unstamped = repo("generator-unstamped", "prey-headtracking", { "HeadTracking.ini": LEGACY_INI, "launcher-manifest.json": staleText });
  const unstampedRun = runScript("encode-seed.mjs", unstamped);
  check(unstampedRun.status !== 0 && unstampedRun.out.includes("which is unstamped"), `generator: per_game from an unstamped file should be refused, got ${unstampedRun.status}\n${unstampedRun.out}`);
}

// validate-manifest runs the rules on a built ZIP, against the repo whose release/ folder holds it,
// which it reads through the real data/config-format.json entry, abzu-headtracking's. The repo is
// converted unless its committed HeadTracking.ini is given as a legacy file.
const ABZU_BIN = "AbzuGame/Binaries/Win64";
const shipping = (target) => ({ files: [{ source: "plugins/Mod.dll", target: "Mod.dll" }, { source: "plugins/Mod.dll", target }] });
// [case, manifest fields, message] for a converted abzu-headtracking manifest with no block.
const BLOCKLESS_WRITES = [
  ["a legacy seed", { loader: { seed: [seed(`${ABZU_BIN}/HeadTracking.ini`)] } }, `seed ${ABZU_BIN}/HeadTracking.ini (game_root) writes the legacy file HeadTracking.ini, and ${SEEDS_NOTHING}`],
  ["a config seed", { seed: [seed(`${ABZU_BIN}/CameraUnlock.ini`)] }, `seed ${ABZU_BIN}/CameraUnlock.ini (game_root) writes CameraUnlock.ini, and ${SEEDS_NOTHING}`],
  ["files[] on the config", shipping(`${ABZU_BIN}/CameraUnlock.ini`), `files[] ${ABZU_BIN}/CameraUnlock.ini (game_root) lands on CameraUnlock.ini`],
  ["files[] on the legacy file", shipping(`${ABZU_BIN}/HeadTracking.ini`), `files[] ${ABZU_BIN}/HeadTracking.ini (game_root) lands on the legacy file HeadTracking.ini`],
];
function zipRepo(label, config, extra = {}, committedText = ALL) {
  const man = abzuMan(() => config, extra);
  const root = repo(label, "abzu-headtracking", { "HeadTracking.ini": committedText });
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
  const good = runScript("validate-manifest.mjs", zipRepo("zip-good", structuredClone(abzuConfig)));
  check(good.status === 0 && good.out.includes("config descriptor"), `validate-manifest: a block that meets every rule should pass, got ${good.status}\n${good.out}`);
  const legacyPath = runScript("validate-manifest.mjs", zipRepo("zip-legacy-path", { ...structuredClone(abzuConfig), path: "AbzuGame/Binaries/Win64/HeadTracking.ini" }));
  check(legacyPath.status === 1 && legacyPath.out.includes("is not named CameraUnlock.ini"), `validate-manifest: a block naming the legacy file should fail, got ${legacyPath.status}\n${legacyPath.out}`);
  const rows = runScript("validate-manifest.mjs", zipRepo("zip-rows", { ...structuredClone(abzuConfig), rows: {} }));
  check(rows.status === 1 && rows.out.includes(ROWS_REFUSED), `validate-manifest: a block with rows should fail, got ${rows.status}\n${rows.out}`);
  const extra = runScript("validate-manifest.mjs", zipRepo("zip-extra-per-game", { ...structuredClone(abzuConfig), per_game: { WorldSpaceYaw: "false" } }));
  check(extra.status === 1 && extra.out.includes("config.per_game names WorldSpaceYaw"), `validate-manifest: a per_game id the repo does not keep should fail, got ${extra.status}\n${extra.out}`);
  const version = runScript("validate-manifest.mjs", zipRepo("zip-version", { ...structuredClone(abzuConfig), canonical_since: "2.0.0" }));
  check(version.status === 1 && version.out.includes("is above mod_info.version 1.2.0"), `validate-manifest: canonical_since above the ZIP's version should fail, got ${version.status}\n${version.out}`);
  for (const [i, [what, extra, expected]] of BLOCKLESS_WRITES.entries()) {
    const run = runScript("validate-manifest.mjs", zipRepo(`zip-blockless-${i}`, undefined, extra));
    check(run.status === 1 && run.out.includes(expected), `validate-manifest: a converted repo with no block and ${what} should fail with "${expected}", got ${run.status}\n${run.out}`);
  }
  const unconvertedSeed = runScript("validate-manifest.mjs", zipRepo("zip-unconverted-seed", undefined, BLOCKLESS_WRITES[0][1], LEGACY_INI));
  check(unconvertedSeed.status === 0, `validate-manifest: an unconverted repo seeding its config should pass, got ${unconvertedSeed.status}\n${unconvertedSeed.out}`);
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
  const r = repoReport(reworked.root);
  check(r.applies && !r.has_block && r.problems.length === 1 && r.problems[0].includes("has no config block"), `report: a converted manifest repo without a block should fail for it, got ${JSON.stringify(r)}`);
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
  const good = repoReport(tagged);
  check(good.problems.length === 0 && !good.shallow, `tags: canonical_since 1.1.0 is above v1.0.0 and v1.1.0-beta, got ${JSON.stringify(good.problems)}`);
  fs.writeFileSync(path.join(tagged, "launcher-manifest.json"), JSON.stringify(abzuMan((c) => ({ ...c, canonical_since: "1.0.0" }))));
  const low = repoReport(tagged);
  check(low.problems.some((p) => p.includes("is not above v1.0.0")), `tags: canonical_since 1.0.0 should fail against v1.0.0, got ${JSON.stringify(low.problems)}`);

  const shallowRoot = path.join(scratch, "shallow", "abzu-headtracking");
  const clone = spawnSync("git", ["clone", "-q", "--depth", "1", `file://${tagged.replace(/\\/g, "/")}`, shallowRoot], { encoding: "utf8" });
  if (clone.status !== 0) throw new Error(`git clone failed: ${clone.stderr}`);
  fs.writeFileSync(path.join(shallowRoot, "launcher-manifest.json"), JSON.stringify(abzuMan((c) => ({ ...c, canonical_since: "1.0.0" }))));
  const shallow = repoReport(shallowRoot);
  check(shallow.shallow && shallow.problems.length === 0, `tags: a shallow clone should be reported as such, got ${JSON.stringify(shallow)}`);

  // conformance's config-descriptor check over real entries: for each converted repo with no
  // block that seeds or ships either file, a FAIL for the missing block and one for the write;
  // one for a block that names the legacy file and one for a block that carries rows; and nothing
  // for a block that meets every rule or for an unconverted repo that seeds its config.
  const noBlock = BLOCKLESS_WRITES.map(([, extra], i) =>
    repo(`conformance-no-block-${i}`, "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined, extra)) }),
  );
  const unconvertedSeed = repo("conformance-unconverted-seed", "abzu-headtracking", {
    "HeadTracking.ini": LEGACY_INI,
    "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined, BLOCKLESS_WRITES[0][1])),
  });
  const legacyBlock = repo("conformance-legacy-path", "abzu-headtracking", {
    "HeadTracking.ini": ALL,
    "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, path: "AbzuGame/Binaries/Win64/HeadTracking.ini" }))),
  });
  const rowsBlock = repo("conformance-rows", "abzu-headtracking", {
    "HeadTracking.ini": ALL,
    "launcher-manifest.json": JSON.stringify(abzuMan(({ per_game: _, ...c }) => ({ ...c, rows: {} }))),
  });
  const goodBlock = repo("conformance-good", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan()) });
  const conformance = spawnSync(
    "powershell",
    ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `& '${path.join(SCRIPTS, "conformance.ps1")}' -Repo ${[...noBlock, unconvertedSeed, legacyBlock, rowsBlock, goodBlock].map((r) => `'${r}'`).join(",")} -Check config-descriptor -Json; exit $LASTEXITCODE`],
    { encoding: "utf8" },
  );
  const findings = JSON.parse(conformance.stdout.replace(/^\uFEFF/, "") || "[]");
  const list = (Array.isArray(findings) ? findings : [findings]).filter((x) => x.check === "config-descriptor" && x.severity === "FAIL");
  const count = (needle) => list.filter((x) => x.message.includes(needle)).length;
  check(
    conformance.status === 1 && list.length === 2 * BLOCKLESS_WRITES.length + 3 && count("has no config block") === BLOCKLESS_WRITES.length &&
      BLOCKLESS_WRITES.every(([, , expected]) => count(expected) === 1) && count("is not named CameraUnlock.ini") === 1 &&
      count(ROWS_REFUSED) === 1 && count("config has no per_game") === 1,
    `conformance: config-descriptor should fail each converted repo with no block on the block and on its write, the block that names the legacy file, and the block with rows for rows and for its missing per_game, and nothing else, got ${conformance.status}\n${conformance.stdout}${conformance.stderr}`,
  );

  // conformance's config-preserve check: a converted repo's install.cmd lists neither file in
  // MOD_DLLS or MOD_SEED_FILES, and its uninstall.cmd neither in MOD_SEED_FILES. A repo converted
  // only by a stamped file data/config-format.json does not record lists that file's name in
  // neither. The same scripts in an unconverted repo, and the lists without them in a converted
  // one, draw nothing.
  const PRESERVE = `"AbzuGame\\Binaries\\Win64\\CameraUnlock.ini" "AbzuGame\\Binaries\\Win64\\HeadTracking.ini"`;
  const scripts = (dlls, seeds, uninstallSeeds = seeds, preserve = PRESERVE) => ({
    "scripts/install.cmd": `@echo off\r\n:: --- CONFIG BLOCK ---\r\nset "MOD_DLLS=${dlls}"\r\nset "MOD_SEED_FILES=${seeds}"\r\n:: --- END CONFIG BLOCK ---\r\n`,
    "scripts/uninstall.cmd": `@echo off\r\n:: --- CONFIG BLOCK ---\r\nset "MOD_DLLS=${dlls}"\r\nset "MOD_SEED_FILES=${uninstallSeeds}"\r\nset "PRESERVE_FILES=${preserve}"\r\n:: --- END CONFIG BLOCK ---\r\nset "_BODY=%WRAPPER_DIR%shared\\uninstall-body.cmd"\r\n`,
  });
  const listed = repo("preserve-listed", "abzu-headtracking", { "HeadTracking.ini": ALL, ...scripts("Mod.asi HeadTracking.ini", "CameraUnlock.ini", "CameraUnlock.ini HeadTracking.ini") });
  const swapped = repo("preserve-swapped", "abzu-headtracking", { "HeadTracking.ini": ALL, ...scripts("Mod.asi CameraUnlock.ini", "HeadTracking.ini", "") });
  const stampOnly = repo("preserve-stamp-only", "deus-ex-mankind-divided-headtracking", { "config/HeadTracking.ini": ALL, ...scripts("Mod.asi", "HeadTracking.ini", "HeadTracking.ini", "retail\\CameraUnlock.ini") });
  git(stampOnly, "add", "config/HeadTracking.ini");
  check(
    isDeepStrictEqual(repoState(stampOnly).unrecorded_stamped, ["config/HeadTracking.ini"]),
    `preserve-stamp-only: the fixture should be converted by its unrecorded stamped file alone, got ${JSON.stringify(repoState(stampOnly))}`,
  );
  const unlisted = repo("preserve-unlisted", "abzu-headtracking", { "HeadTracking.ini": ALL, ...scripts("Mod.asi", "") });
  const legacyListed = repo("preserve-unconverted", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI, ...scripts("Mod.asi HeadTracking.ini", "CameraUnlock.ini") });
  const preserve = spawnSync(
    "powershell",
    ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `& '${path.join(SCRIPTS, "conformance.ps1")}' -Repo '${listed}','${swapped}','${stampOnly}','${unlisted}','${legacyListed}' -Check config-preserve -Json; exit $LASTEXITCODE`],
    { encoding: "utf8" },
  );
  const preserveFindings = JSON.parse(preserve.stdout.replace(/^\uFEFF/, "") || "[]");
  const messages = (Array.isArray(preserveFindings) ? preserveFindings : [preserveFindings]).map((x) => `${x.severity} ${x.message}`);
  const expected = [
    "FAIL install.cmd's MOD_DLLS lists HeadTracking.ini, the legacy file,",
    "FAIL install.cmd's MOD_SEED_FILES lists CameraUnlock.ini, which an update from a legacy build writes",
    "FAIL uninstall.cmd's MOD_SEED_FILES lists CameraUnlock.ini, the player's settings,",
    "FAIL uninstall.cmd's MOD_SEED_FILES lists HeadTracking.ini, the legacy file an older build reads after a rollback,",
    "FAIL install.cmd's MOD_DLLS lists CameraUnlock.ini, so every script install copies the default over the player's settings.",
    "FAIL install.cmd's MOD_SEED_FILES lists HeadTracking.ini, the legacy file, so a fresh install gets a legacy file no older build wrote",
    "FAIL install.cmd's MOD_SEED_FILES lists HeadTracking.ini, a copy of the committed config,",
    "FAIL uninstall.cmd's MOD_SEED_FILES lists HeadTracking.ini, a file named like the committed config,",
  ];
  check(
    preserve.status === 1 && messages.length === expected.length &&
      expected.every((e) => messages.filter((m) => m.startsWith(e) && m.endsWith("The mod creates CameraUnlock.ini at first launch; list neither file.")).length === 1),
    `conformance: config-preserve should fail each converted repo's listings and nothing else, got ${preserve.status}\n${preserve.stdout}${preserve.stderr}`,
  );

  // conformance's config-defaults check: in a converted repo, DefaultsFile.At outside a test
  // folder, PerUser inside one, and a test source that builds an owner (emplacing an optional one
  // declared in another file included) or initialises PluginMod through a reference without naming
  // At each fail; vendored code, an unconverted repo, reads of PluginMod and the right uses do not.
  const tracked = (label, name, files) => {
    const root = repo(label, name, files);
    git(root, "add", "-A");
    return root;
  };
  const wrong = tracked("defaults-wrong", "abzu-headtracking", {
    "HeadTracking.ini": ALL,
    "src/mod.cpp": "void Load() {\r\n  options.defaults = config::DefaultsFile::At(L\"C:\\\\Defaults.ini\");\r\n}\r\n",
    "src/Mod/Plugin.cs": "class P {\r\n  void Load() { options.Defaults = DefaultsFile.At(path); }\r\n}\r\n",
    "tests/config_tests.cpp": "auto d = DefaultsFile::PerUser();\r\nConfigOwner<Config> owner(Options(dir));\r\n",
    "src/Mod.Tests/OwnerTests.cs": "class T {\r\n  void Run() { var owner = new ConfigOwner<Cfg>(options); }\r\n}\r\n",
    "tests/config_differential/differential_tests.cpp": "auto owner = std::make_unique<cfg::ConfigOwner<Config>>(Options(p));\r\n",
    "Test/plugin_test.cpp": "void Run() { ref::PluginMod::Instance().Initialize(descriptor); }\r\n",
    "tests/fixture.h": "struct Fixture {\r\n  std::optional<cameraunlock::config::ConfigOwner<Config>> m_owner;\r\n  void Build(std::optional<ConfigOwner<Config>>& slot);\r\n};\r\n",
    "tests/fixture.cpp": "void Fixture::Load() {\r\n  m_owner.emplace(ConfigOwnerOptionsFor(path));\r\n}\r\nvoid Fixture::Build(std::optional<ConfigOwner<Config>>& slot) { slot->emplace(Options(p)); }\r\n",
    "tests/in_place_test.cpp": "std::optional<ConfigOwner<Config>> owner{std::in_place, Options(p)};\r\n",
    "tests/plugin_ref_test.cpp": "void Run() {\r\n  auto& mod = ref::PluginMod::Instance();\r\n  mod.Initialize(descriptor);\r\n}\r\n",
    "vendor/lib/x.cpp": "auto d = DefaultsFile::At(L\"C:\\\\x.ini\");\r\n",
  });
  const right = tracked("defaults-right", "abzu-headtracking", {
    "HeadTracking.ini": ALL,
    "src/dllmain.cpp": "options.defaults = cameraunlock::config::DefaultsFile::PerUser();\r\ng_owner = std::make_unique<cfg::ConfigOwner<Config>>(std::move(options));\r\n",
    "tests/config_tests.cpp": "ConfigOwner<Config> owner(Options(dir, DefaultsFile::At(dir / L\"Defaults.ini\")));\r\n",
    "src/Game.Tests/T.cs": "var owner = new ConfigOwner<C>(new ConfigOwnerOptions<C> { Defaults = DefaultsFile.At(p) });\r\n",
    "tests/helpers.h": "std::unique_ptr<ConfigOwner<Config>> NewOwner(const fs::path& path);\r\nvoid Use(ConfigOwner<Config>& owner);\r\n",
    "src/latests/foo.cpp": "auto d = DefaultsFile::PerUser();\r\n",
    "src/mod.h": "class Mod {\r\n  std::optional<cameraunlock::config::ConfigOwner<Config>> m_owner;\r\n};\r\n",
    "src/mod.cpp": "void Mod::Load() {\r\n  m_owner.emplace(ConfigOwnerOptionsFor(path, DefaultsFile::PerUser()));\r\n}\r\n",
    "tests/emplace_test.cpp": "std::optional<ConfigOwner<Config>> owner;\r\nowner.emplace(Options(dir, DefaultsFile::At(dir / L\"Defaults.ini\")));\r\n",
    "tests/plugin_read_test.cpp": "void Check() {\r\n  auto& config = ref::PluginMod::Instance().GetConfig();\r\n  bool on = ref::PluginMod::Instance().IsEnabled();\r\n  std::vector<int> v;\r\n  v.emplace(v.end(), 1);\r\n}\r\n",
  });
  const unconvertedAt = tracked("defaults-unconverted", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI, "src/mod.cpp": "auto d = DefaultsFile::At(L\"C:\\\\x.ini\");\r\n" });
  const defaults = spawnSync(
    "powershell",
    ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `& '${path.join(SCRIPTS, "conformance.ps1")}' -Repo '${wrong}','${right}','${unconvertedAt}' -Check config-defaults -Json; exit $LASTEXITCODE`],
    { encoding: "utf8" },
  );
  const defaultsFindings = JSON.parse(defaults.stdout.replace(/^\uFEFF/, "") || "[]");
  const defaultsMessages = (Array.isArray(defaultsFindings) ? defaultsFindings : [defaultsFindings]).map((x) => `${x.severity} ${x.message}`);
  const atRule = "a mod must never point at a fixed path, so it passes DefaultsFile.PerUser()";
  const testRule = "a test must never read or create the player's real Defaults.ini, so it passes DefaultsFile.At with a scratch path";
  const expectedDefaults = [
    `FAIL src/mod.cpp:2 names DefaultsFile.At outside a test folder; ${atRule}`,
    `FAIL src/Mod/Plugin.cs:2 names DefaultsFile.At outside a test folder; ${atRule}`,
    `FAIL tests/config_tests.cpp:1 names DefaultsFile.PerUser in a test folder; ${testRule}`,
    `FAIL tests/config_tests.cpp:2 builds a ConfigOwner and never names DefaultsFile.At; ${testRule}`,
    `FAIL src/Mod.Tests/OwnerTests.cs:2 builds a ConfigOwner and never names DefaultsFile.At; ${testRule}`,
    `FAIL tests/config_differential/differential_tests.cpp:1 builds a ConfigOwner and never names DefaultsFile.At; ${testRule}`,
    `FAIL Test/plugin_test.cpp:1 initialises PluginMod and never names DefaultsFile.At; ${testRule}`,
    `FAIL tests/fixture.cpp:2,4 builds a ConfigOwner and never names DefaultsFile.At; ${testRule}`,
    `FAIL tests/in_place_test.cpp:1 builds a ConfigOwner and never names DefaultsFile.At; ${testRule}`,
    `FAIL tests/plugin_ref_test.cpp:3 initialises PluginMod and never names DefaultsFile.At; ${testRule}`,
  ];
  check(
    defaults.status === 1 && isDeepStrictEqual([...defaultsMessages].sort(), [...expectedDefaults].sort()),
    `conformance: config-defaults should fail each wrong use and nothing else, got ${defaults.status}\n${defaults.stdout}${defaults.stderr}`,
  );
}

// Copy-SharedBundle runs Assert-LauncherManifestConfig on the committed manifest, so a package
// script that never calls validate-manifest still refuses a broken block, and a seed or files[]
// row of the config in a converted repo, block or not.
{
  const module = path.join(CORE_ROOT, "powershell", "ReleaseWorkflow.psm1");
  const assertConfig = (root) =>
    spawnSync(
      "powershell",
      ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `Import-Module '${module}' -Force; try { Assert-LauncherManifestConfig -RepoRoot '${root}' -CoreRoot '${CORE_ROOT}'; exit 0 } catch { Write-Output $_.Exception.Message; exit 1 }`],
      { encoding: "utf8" },
    );
  const legacyPath = assertConfig(repo("assert-legacy-path", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, path: "AbzuGame/Binaries/Win64/HeadTracking.ini" }))) }));
  check(legacyPath.status === 1 && legacyPath.stdout.includes("is not named CameraUnlock.ini"), `packaging: a block naming the legacy file should fail, got ${legacyPath.status}\n${legacyPath.stdout}${legacyPath.stderr}`);
  const good = assertConfig(repo("assert-good", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan()) }));
  check(good.status === 0, `packaging: a block with an empty per_game should pass, got ${good.status}\n${good.stdout}${good.stderr}`);
  const rows = assertConfig(repo("assert-rows", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, rows: {} }))) }));
  check(rows.status === 1 && rows.stdout.includes(ROWS_REFUSED), `packaging: a block with rows should fail, got ${rows.status}\n${rows.stdout}${rows.stderr}`);
  const extra = assertConfig(repo("assert-extra-per-game", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan((c) => ({ ...c, per_game: { WorldSpaceYaw: "false" } }))) }));
  check(extra.status === 1 && extra.stdout.includes("config.per_game names WorldSpaceYaw"), `packaging: a per_game id the repo does not keep should fail, got ${extra.status}\n${extra.stdout}${extra.stderr}`);
  const none = assertConfig(repo("assert-none", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined)) }));
  check(none.status === 0, `packaging: a manifest with no block should pass, got ${none.status}\n${none.stdout}${none.stderr}`);
  // The no-seed rule holds at packaging with no block, and the missing block alone, which
  // conformance reports, does not fail a release.
  for (const [label, extra, message] of BLOCKLESS_WRITES) {
    const r = assertConfig(repo(`assert-blockless-${label.replace(/\W+/g, "-")}`, "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined, extra)) }));
    check(r.status === 1 && r.stdout.includes(message), `packaging: ${label} with no block in a converted repo should fail with "${message}", got ${r.status}\n${r.stdout}${r.stderr}`);
  }
  const blockless = assertConfig(repo("assert-blockless-clean", "abzu-headtracking", { "HeadTracking.ini": ALL, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined)) }));
  check(blockless.status === 0, `packaging: a converted repo with no block and no seed should pass, got ${blockless.status}\n${blockless.stdout}${blockless.stderr}`);
  const unconvertedSeed = assertConfig(repo("assert-unconverted-seed", "abzu-headtracking", { "HeadTracking.ini": LEGACY_INI, "launcher-manifest.json": JSON.stringify(abzuMan(() => undefined, BLOCKLESS_WRITES[0][1])) }));
  check(unconvertedSeed.status === 0, `packaging: an unconverted repo seeding its config should pass, got ${unconvertedSeed.status}\n${unconvertedSeed.stdout}${unconvertedSeed.stderr}`);
}

// Packaging stamps the version through ConvertFrom-Json and ConvertTo-Json -Depth 10 in Windows
// PowerShell (scripts/package-bepinex-mod.ps1), which must carry the block through unchanged, an
// empty per_game included.
for (const [label, man] of [["a per_game row", bepMan()], ["an empty per_game", abzuMan()]]) {
  const file = path.join(scratch, "roundtrip.json");
  fs.writeFileSync(file, JSON.stringify(man, null, 2));
  const ps = spawnSync(
    "powershell",
    ["-NoProfile", "-Command", `(Get-Content -LiteralPath '${file}' -Raw | ConvertFrom-Json) | ConvertTo-Json -Depth 10`],
    { encoding: "utf8" },
  );
  check(ps.status === 0 && isDeepStrictEqual(JSON.parse(ps.stdout), man), `packaging round trip: the config block with ${label} should survive, got ${ps.status}\n${ps.stdout}${ps.stderr}`);
}

fs.rmSync(scratch, { recursive: true, force: true });

if (failures.length > 0) {
  console.error(`${failures.length} of ${checks} checks failed:`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`config-descriptor: ${checks} checks passed`);
