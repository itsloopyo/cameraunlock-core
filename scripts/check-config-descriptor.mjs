#!/usr/bin/env node

// The `config` descriptor in launcher-manifest.json: where a converted mod's config file lives
// and which rows the game keeps for itself, with the values its committed file holds there
// (docs/canonical-config.md, "The config descriptor"). This file holds every rule the block is
// held to. validate-manifest.mjs runs them on a built ZIP's manifest, conformance runs them on the
// committed one, and encode-seed.mjs writes `per_game` from expectedPerGame(). It also holds the
// rule that a converted repo's manifest seeds and ships no config, block or not
// (configWriteProblems).
//
//   node scripts/check-config-descriptor.mjs                  # the repo vendoring this core
//   node scripts/check-config-descriptor.mjs <repo> [...]     # repo paths or sibling names
//   node scripts/check-config-descriptor.mjs --json --roots-file <file>
//   node scripts/check-config-descriptor.mjs --package <repo>
//   node scripts/check-config-descriptor.mjs --release <x.y.z> <repo>
//
// The default run prints each repo's problems, a converted repo delivered by manifest that
// carries no block among them, and exits 1 when there is one. --json prints what
// scripts/conformance.ps1 decides its config-descriptor check from, and always exits 0.
// --package is what Copy-SharedBundle runs on a mod's committed manifest: every problem but the
// missing block, which conformance reports, since the block lands in its own change and a
// converted repo has to be able to release before it does. In a GitHub Actions build for a
// v<x.y.z> tag it also holds canonical_since to x.y.z (releaseProblems). --release runs that one
// rule for a release of <x.y.z>, which is what New-ReleaseTag runs before it tags.
//
// A converted repo writes canonical_since as the version it converts in and keeps the last
// release's version until the release bumps it, so a package whose mod_info.version is below
// canonical_since is a pre-release of it: validate-manifest warns there, and only a release
// below canonical_since fails. The committed manifest carries a placeholder mod_info.version
// that packaging stamps, so that warning comes from a built ZIP only. The rule holding
// canonical_since above every v* tag whose committed config lacks the stamp needs the tags, so
// it runs on a full clone only.

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { repoState } from "./check-canonical-config.mjs";
import {
  equalsAsciiIgnoreCase,
  findSection,
  findValue,
  hasCanonicalStamp,
  isDefaultToken,
  parseCanonicalIni,
  splitLines,
  trimSpaceTab,
} from "./lib/canonical-ini.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPOS_ROOT = path.dirname(CORE_ROOT);
const readJson = (rel) => JSON.parse(fs.readFileSync(path.join(CORE_ROOT, rel), "utf8"));
const SCHEMA = readJson("data/config-schema.json");
const FORMAT = readJson("data/config-format.json");
const GAMES = readJson("data/games.json").games;

const FIELDS = ["path", "anchor", "legacy_source", "canonical_since", "per_game"];
// The renderer writes an Engine row marked PerGame() at its default as "; Key=value".
const COMMENTED_ROW = /^;[ \t]*([A-Za-z0-9]+)[ \t]*=(.*)$/;
// The fleet's one config name. No v* release before the canonical format reads a file of that
// name, so a launcher that manages it leaves alone the file an older version reads after a rollback.
const CONFIG_NAME = "CameraUnlock.ini";
const ANCHORS = ["game_root", "exe_dir", "mod_home"];
const MANIFEST_MODES = ["manifest", "manifest_variants"];
const RELEASE_VERSION = /^\d+\.\d+\.\d+$/;
const VERSION = /^\d+\.\d+\.\d+(-.+)?$/;
const TAG_VERSION = /^v(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$/;
const RELEASE_TAG = /^v(\d+\.\d+\.\d+)$/;

const isObject = (v) => typeof v === "object" && v !== null && !Array.isArray(v);
const perGameRows = (repo) => (FORMAT.per_game[repo] ?? []).map((e) => e.row);
const NOT_GLOBAL_IDS = new Set(SCHEMA.concepts.filter((c) => c.canonical && !c.global).map((c) => c.id));
const slashes = (p) => p.replace(/\\/g, "/");
const lower = (p) => slashes(p).toLowerCase();
const anchorOf = (item) => item.anchor ?? "game_root";

// Negative, zero or positive as a is below, equal to or above b. A pre-release sorts below its
// release, as in SemVer; two pre-releases of one version compare equal, which no rule here needs
// to tell apart.
export function compareVersions(a, b) {
  const parse = (v) => {
    const m = /^(\d+)\.(\d+)\.(\d+)(-.+)?$/.exec(v);
    if (!m) throw new Error(`${JSON.stringify(v)} is not a version`);
    return [Number(m[1]), Number(m[2]), Number(m[3]), m[4] === undefined ? 1 : 0];
  };
  const [x, y] = [parse(a), parse(b)];
  for (let i = 0; i < 4; i++) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
}

// The paths, relative to the game folder, an anchored target can land on. The launcher resolves
// exe_dir from the executable it detected, which is executable_relpath or, for a Game Pass
// install, xbox_executable_relpath. A mod_home target is never in the game folder.
function gameRelativeTargets(item, man) {
  const anchor = anchorOf(item);
  if (anchor === "game_root") return [lower(item.target)];
  if (anchor === "mod_home") return [];
  if (anchor !== "exe_dir") throw new Error(`${item.target} has an unknown anchor '${anchor}'`);
  const gameId = man.mod_info?.game_id;
  const game = GAMES[gameId];
  if (game === undefined) {
    throw new Error(`${item.target} is anchored at exe_dir, and mod_info.game_id '${gameId}' is not in data/games.json`);
  }
  return [game.executable_relpath, game.xbox_executable_relpath]
    .filter((exe) => exe !== undefined)
    .map((exe) => lower(path.win32.join(path.win32.dirname(exe), item.target)));
}

function pathProblem(field, p) {
  if (typeof p !== "string" || p === "") return `config.${field} must be a non-empty string`;
  if (p.includes("\\")) return `config.${field} ${p} uses "\\"; descriptor paths are separated by "/"`;
  if (p.startsWith("/") || p.includes(":")) return `config.${field} ${p} names a root or a drive; it is relative to its anchor`;
  if (p.split("/").some((seg) => seg === "" || seg === "." || seg === "..")) {
    return `config.${field} ${p} has an empty, "." or ".." segment`;
  }
  return null;
}

// The rules that need the manifest alone.
export function shapeProblems(man, { checkVersion }) {
  const problems = [];
  for (const variant of Array.isArray(man.variants) ? man.variants : []) {
    if (isObject(variant) && "config" in variant) {
      problems.push(`variant "${variant.id}" carries a config block; the descriptor sits at the top level only`);
    }
  }
  if (!("config" in man)) return problems;
  const config = man.config;
  if (!isObject(config)) return [...problems, `config must be an object, not ${JSON.stringify(config)}`];

  if (!MANIFEST_MODES.includes(man.delivery_mode)) {
    problems.push(
      `config is read from a package the launcher deploys itself, delivery_mode "manifest" or "manifest_variants", and this one is ${JSON.stringify(man.delivery_mode)}`,
    );
  }
  for (const key of Object.keys(config)) {
    if (key === "rows") {
      problems.push(
        'config has rows, which the descriptor no longer carries: a launcher writes no game file, so the block lists only the rows the game keeps for itself, in per_game ("per_game": {} where there are none, which render-config fills)',
      );
    } else if (!FIELDS.includes(key)) {
      problems.push(`config has an unknown field "${key}"; it holds ${FIELDS.join(", ")}`);
    }
  }
  if (!("path" in config)) problems.push("config has no path");
  else {
    const p = pathProblem("path", config.path);
    if (p) problems.push(p);
    else if (config.path.split("/").pop() !== CONFIG_NAME) {
      problems.push(
        `config.path ${config.path} is not named ${CONFIG_NAME}; a converted mod keeps its settings in ${CONFIG_NAME}, which no pre-canonical release reads, so a launcher managing it never touches the file an older version of the mod reads after a rollback`,
      );
    }
  }
  if ("anchor" in config && !ANCHORS.includes(config.anchor)) {
    problems.push(`config.anchor ${JSON.stringify(config.anchor)} is not one of ${ANCHORS.join(", ")}`);
  }
  if ("legacy_source" in config) {
    const p = pathProblem("legacy_source", config.legacy_source);
    if (p) problems.push(p);
  }
  if ("canonical_since" in config) {
    const since = config.canonical_since;
    if (typeof since !== "string" || !RELEASE_VERSION.test(since)) {
      problems.push(`config.canonical_since ${JSON.stringify(since)} is not a version written x.y.z`);
    } else if (checkVersion) {
      const version = man.mod_info?.version;
      if (typeof version !== "string" || !VERSION.test(version)) {
        problems.push(`config.canonical_since needs mod_info.version to compare with, and it is ${JSON.stringify(version)}`);
      }
    }
  }

  if (!("per_game" in config)) {
    problems.push('config has no per_game; write "per_game": {} and run render-config, which fills it');
    return problems;
  }
  const perGame = config.per_game;
  if (!isObject(perGame)) return [...problems, `config.per_game must be an object, not ${JSON.stringify(perGame)}`];
  for (const [id, value] of Object.entries(perGame)) {
    if (typeof value !== "string") {
      problems.push(`config.per_game.${id} is ${JSON.stringify(value)}, not the text the committed file holds on the row`);
    } else if (isDefaultToken(trimSpaceTab(value))) {
      problems.push(`config.per_game.${id} is ${JSON.stringify(value)}; per_game holds the value the game keeps for itself, never the default token`);
    }
  }
  return problems;
}

const sinceOf = (man) => {
  const since = isObject(man.config) ? man.config.canonical_since : undefined;
  return typeof since === "string" && RELEASE_VERSION.test(since) ? since : null;
};

// What validate-manifest warns about a package whose mod_info.version is below canonical_since:
// the build is a pre-release of canonical_since. null when it is not, and when shapeProblems
// reports either field.
export function preReleaseWarning(man) {
  const since = sinceOf(man);
  const version = man.mod_info?.version;
  if (since === null || typeof version !== "string" || !VERSION.test(version) || compareVersions(since, version) <= 0) return null;
  return `config.canonical_since ${since} is above mod_info.version ${version}, so this package is a pre-release of ${since}; releasing any version below ${since} fails`;
}

// A release of `version` ships the canonical file its manifest declares, so it is at or above
// canonical_since, the first version that shipped it.
export function releaseProblems(man, version) {
  if (!RELEASE_VERSION.test(version)) throw new Error(`${JSON.stringify(version)} is not a release version written x.y.z`);
  const since = sinceOf(man);
  if (since === null || compareVersions(since, version) <= 0) return [];
  return [
    `releasing ${version}, and config.canonical_since is ${since}, the first version that ships CameraUnlock.ini; release ${since} or later, or, if ${version} is the release that first ships it, set canonical_since to ${version}`,
  ];
}

// The version a build cuts when GitHub Actions runs it for a v<x.y.z> tag, the trigger of every
// release workflow in the fleet; null for any other build.
export function releaseVersionFromEnv(env = process.env) {
  if (env.GITHUB_ACTIONS !== "true" || env.GITHUB_REF_TYPE !== "tag") return null;
  return RELEASE_TAG.exec(env.GITHUB_REF_NAME ?? "")?.[1] ?? null;
}

// The value text a committed file holds on a concept row: the active line's value, or, with no
// active line, the value of the row commented out under its own section, the form the renderer
// gives an Engine row marked PerGame() at its default. null when the file has neither.
function committedText(bytes, doc, concept) {
  const section = findSection(doc, concept.section);
  const line = section === null ? null : findValue(section, concept.key);
  if (line !== null) return { value: line.value, line: line.line };
  let current = null;
  let found = null;
  for (const { text, number } of splitLines(bytes)) {
    const trimmed = trimSpaceTab(text);
    if (trimmed.startsWith("[")) {
      const close = trimmed.indexOf("]");
      current = close < 0 ? null : trimSpaceTab(trimmed.slice(1, close));
      continue;
    }
    const commented = COMMENTED_ROW.exec(trimmed);
    if (commented !== null && commented[1] === concept.key && current !== null && equalsAsciiIgnoreCase(current, concept.section)) {
      found = { value: trimSpaceTab(commented[2]), line: number };
    }
  }
  return found;
}

// The per_game map a repo's descriptor holds: each concept data/config-format.json per_game lists
// for the repo, in that order, with the value text its committed file holds on the row. The file
// holds a value there, never the token, since the table marks the row PerGame().
export function expectedPerGame(root, state) {
  if (state.files.length !== 1) {
    return { perGame: null, problems: [`data/config-format.json records ${state.files.length} config files for ${state.repo}, and a descriptor names one`] };
  }
  const [file] = state.files;
  if (file.state !== "stamped") {
    const which = file.committed === null ? "records no committed file" : `records ${file.committed}, which is ${file.state}`;
    return { perGame: null, problems: [`descriptor per_game values come from the committed config, and data/config-format.json ${which} for ${state.repo}`] };
  }
  const bytes = fs.readFileSync(path.join(root, ...file.committed.split("/")));
  const doc = parseCanonicalIni(bytes);
  const problems = [];
  const perGame = {};
  for (const id of perGameRows(state.repo)) {
    const concept = SCHEMA.concepts.find((c) => c.id === id);
    const held = committedText(bytes, doc, concept);
    if (held === null) {
      problems.push(`data/config-format.json per_game lists ${id} for ${state.repo}, and ${file.committed} has no ${concept.key} line`);
    } else if (isDefaultToken(held.value)) {
      problems.push(
        `${file.committed} line ${held.line}: [${concept.section}] ${concept.key}=${held.value}, and data/config-format.json per_game lists ${id} for ${state.repo}, so the file holds the game's own value there`,
      );
    } else {
      perGame[id] = held.value;
    }
  }
  return { perGame: problems.length === 0 ? perGame : null, problems };
}

// Every seed and files[] row, at the top level and in each variant, with a string target.
function writersOf(man) {
  const holders = [man, ...(Array.isArray(man.variants) ? man.variants : [])].filter(isObject);
  const listed = (lists) => lists.filter(Array.isArray).flat().filter((i) => isObject(i) && typeof i.target === "string");
  return {
    seeds: listed(holders.flatMap((h) => [h.seed, isObject(h.loader) ? h.loader.seed : undefined])),
    files: listed(holders.map((h) => h.files)),
  };
}

const leafOf = (p) => p.split(/[\\/]/).pop();

// A converted release seeds nothing and ships no config through files[]: no seed or row writes
// CameraUnlock.ini, the legacy file or a file named like the committed config, with a config
// block or without one. Matched by file name in any folder and at any anchor, so a mod_home
// target, or an exe_dir one for a game data/games.json does not list, is caught too, and no
// converted release writes a file of one of those names for another reason.
export function configWriteProblems(man, state) {
  if (!state.converted) return [];
  const names = new Map([[CONFIG_NAME.toLowerCase(), CONFIG_NAME]]);
  const add = (name, what) => {
    if (!names.has(name.toLowerCase())) names.set(name.toLowerCase(), what);
  };
  for (const f of state.files) if (f.legacy_source !== null) add(f.legacy_source, `the legacy file ${f.legacy_source}`);
  for (const committed of [...state.files.map((f) => f.committed).filter((c) => c !== null), ...state.unrecorded_stamped]) {
    add(leafOf(committed), `${leafOf(committed)}, the committed config's name`);
  }
  const hit = (item) => names.get(leafOf(item.target).toLowerCase());
  const { seeds, files } = writersOf(man);
  const problems = [];
  for (const f of files) {
    const what = hit(f);
    if (what) {
      problems.push(`files[] ${f.target} (${anchorOf(f)}) lands on ${what}; a files[] row is copied over whatever is there at every deploy, the player's settings included, and a converted release ships no config`);
    }
  }
  for (const seed of seeds) {
    const what = hit(seed);
    if (what) {
      problems.push(
        `seed ${seed.target} (${anchorOf(seed)}) writes ${what}, and a converted release seeds nothing: the mod creates CameraUnlock.ini at first launch and imports the legacy file only while CameraUnlock.ini is absent, so a seeded CameraUnlock.ini stops the import on an update, a seeded legacy file is imported on a fresh install, and Lopari v0.9.0 reinstalls the mod before launching once a seeded file has changed or is gone`,
      );
    }
  }
  return problems;
}

function listingText(state) {
  if (state.listing === "unlisted") return "it is not in data/config-format.json";
  if (state.listing === "exempt") return `data/config-format.json exempts it: ${state.reason}`;
  if (state.listing === "predecessor") return `it is an earlier repo of ${state.repo}`;
  return "no committed config file carries the [CameraUnlock] stamp";
}

// The rules that hold the block to the repo it was built from: data/config-format.json's entry
// and the committed file.
function repoProblems(man, root, state) {
  const config = man.config;
  if (!state.converted) return [`config is declared, and ${state.folder} is not converted to the canonical config format (${listingText(state)})`];
  if (state.files.length !== 1) {
    return [`config names one file, and data/config-format.json records ${state.files.length} config files for ${state.repo}; a package with more than one declares no descriptor`];
  }
  const [file] = state.files;
  const problems = [];
  const anchor = anchorOf(config);
  const p = config.path;
  const installed = file.installed.map(slashes);
  const installedLower = installed.map((i) => i.toLowerCase());
  const listed = installed.join(", ");

  const gameId = man.mod_info?.game_id;
  const gameKnown = GAMES[gameId] !== undefined;
  if (!gameKnown && anchor === "exe_dir") {
    problems.push(`config.path ${p} is anchored at exe_dir, and mod_info.game_id ${JSON.stringify(gameId)} is not in data/games.json, so which file it lands on cannot be checked`);
  }
  const targets = (item) =>
    ANCHORS.includes(anchorOf(item)) && (anchorOf(item) !== "exe_dir" || gameKnown) ? gameRelativeTargets(item, man) : [];

  if (anchor === "game_root") {
    if (installed.length !== 1) {
      problems.push(
        installed.length === 0
          ? "config.anchor is game_root, and data/config-format.json records no installed path; a file outside the game folder is anchored at mod_home"
          : `config.anchor is game_root, and data/config-format.json records ${installed.length} installed paths (${listed}); a config with one path per layout is anchored at exe_dir`,
      );
    } else if (p !== installed[0]) {
      problems.push(`config.path ${p} is not ${installed[0]}, the installed path data/config-format.json records`);
    }
  } else if (anchor === "exe_dir") {
    if (installed.length === 0) {
      problems.push("config.anchor is exe_dir, and data/config-format.json records no installed path; a file outside the game folder is anchored at mod_home");
    } else {
      const not = installed.filter((i) => i !== p && !i.endsWith(`/${p}`));
      if (not.length > 0) problems.push(`config.path ${p} is not the tail of every installed path data/config-format.json records: not of ${not.join(", ")}`);
      const off = targets({ target: p, anchor }).filter((t) => !installedLower.includes(t));
      if (off.length > 0) {
        problems.push(
          `config.path ${p} at exe_dir lands on ${off.join(", ")} beside the executable data/games.json records for ${gameId}, which is not an installed path data/config-format.json records (${listed}); a launcher would manage a file the mod never reads`,
        );
      }
    }
  } else if (installed.length > 0) {
    problems.push(`config.anchor is mod_home, and data/config-format.json records installed path(s) in the game folder: ${listed}`);
  }

  // The legacy file sits beside the config, so from the config's anchor it is the folder of
  // config.path and the name data/config-format.json records.
  const folderOf = (s) => s.slice(0, Math.max(s.lastIndexOf("/"), 0));
  const besideOf = (s, name) => (folderOf(s) === "" ? name : `${folderOf(s)}/${name}`);
  const legacyName = file.legacy_source;
  const legacy = legacyName === null ? null : besideOf(p, legacyName);
  if (legacy === null && "legacy_source" in config) {
    problems.push(`config.legacy_source is ${config.legacy_source}, and data/config-format.json records no legacy_source for ${state.repo}`);
  } else if (legacy !== null && !("legacy_source" in config)) {
    problems.push(`config has no legacy_source, and data/config-format.json records ${legacyName}, which is ${legacy} beside config.path`);
  } else if (legacy !== null && config.legacy_source !== legacy) {
    problems.push(`config.legacy_source ${config.legacy_source} is not ${legacy}, the legacy file data/config-format.json records (${legacyName}) in the folder of config.path`);
  }

  if (state.listing === "legacy" && !("canonical_since" in config)) {
    problems.push(`config has no canonical_since, and ${state.repo} published pre-canonical builds (data/config-format.json legacy); it names the first version that shipped the canonical file`);
  } else if (state.listing !== "legacy" && "canonical_since" in config) {
    problems.push(`config.canonical_since is set, and ${state.repo} never published a pre-canonical build (it is not in data/config-format.json legacy)`);
  }

  const kept = perGameRows(state.repo);
  const hint = "node cameraunlock-core/scripts/encode-seed.mjs, which render-config runs, rewrites per_game";
  for (const id of kept) {
    if (!(id in config.per_game)) problems.push(`config.per_game has no ${id}, which data/config-format.json per_game lists for ${state.repo}; ${hint}`);
  }
  for (const id of Object.keys(config.per_game)) {
    if (NOT_GLOBAL_IDS.has(id)) {
      problems.push(
        `config.per_game names ${id}, which is not global in data/config-schema.json: every game keeps its own value there already, so it is never a per_game row`,
      );
    } else if (!kept.includes(id)) {
      problems.push(
        `config.per_game names ${id}, which data/config-format.json per_game does not list for ${state.repo}; a game keeps a row for itself only with the owner's approval recorded there`,
      );
    }
  }
  const expected = expectedPerGame(root, state);
  problems.push(...expected.problems);
  if (expected.perGame !== null) {
    for (const [id, value] of Object.entries(expected.perGame)) {
      if (id in config.per_game && config.per_game[id] !== value) {
        problems.push(`config.per_game.${id} is ${JSON.stringify(config.per_game[id])}, and ${file.committed} holds ${JSON.stringify(value)} there; ${hint}`);
      }
    }
  }
  return problems;
}

// Every rule for a manifest's descriptor. `root` is the repo the package was built from and
// `state` its repoState, read here when not given. The repo rules run once the shape holds.
export function descriptorProblems(man, { root, state, checkVersion }) {
  const shape = shapeProblems(man, { checkVersion });
  if (shape.length > 0 || !("config" in man)) return shape;
  return repoProblems(man, root, state ?? repoState(root));
}

function git(root, args, encoding = "utf8") {
  const result = spawnSync("git", ["-C", root, ...args], { encoding });
  if (result.error) throw result.error;
  return result;
}

// canonical_since has to be above every v* tag whose committed config carries no stamp, since
// it marks where the canonical file starts and each of those versions reads the old format.
export function tagProblems(root, state, config) {
  if (!isObject(config) || typeof config.canonical_since !== "string" || !RELEASE_VERSION.test(config.canonical_since)) {
    return { shallow: false, problems: [] };
  }
  const shallow = git(root, ["rev-parse", "--is-shallow-repository"]);
  if (shallow.status !== 0) throw new Error(`git rev-parse failed in ${root}: ${shallow.stderr.trim()}`);
  if (shallow.stdout.trim() === "true") return { shallow: true, problems: [] };
  const committed = state.files[0]?.committed;
  if (state.files.length !== 1 || committed === null) return { shallow: false, problems: [] };
  const tags = git(root, ["tag", "-l", "v*"]);
  if (tags.status !== 0) throw new Error(`git tag failed in ${root}: ${tags.stderr.trim()}`);
  const problems = [];
  for (const tag of tags.stdout.split(/\r?\n/).filter(Boolean)) {
    const version = TAG_VERSION.exec(tag)?.[1];
    if (version === undefined) continue;
    const blob = git(root, ["cat-file", "blob", `${tag}:${committed}`], "buffer");
    const stamped = blob.status === 0 && hasCanonicalStamp(blob.stdout);
    if (!stamped && compareVersions(config.canonical_since, version) <= 0) {
      problems.push(`config.canonical_since ${config.canonical_since} is not above ${tag}, whose ${committed} carries no [CameraUnlock] stamp`);
    }
  }
  return { shallow: false, problems };
}

// The rules compare the block with data/config-format.json, never with the mod's code, so the
// hint puts the code first: a block written for a build that still reads the legacy file passes.
const NO_BLOCK =
  "converted, delivered by manifest, and launcher-manifest.json has no config block. The block names CameraUnlock.ini, so it lands with or after the change that makes the mod read CameraUnlock.ini, with the legacy file as the owner's legacy path; then write path and anchor by hand, legacy_source and canonical_since where the rules ask for them, and \"per_game\": {}, and run render-config";

// What conformance's config-descriptor check reads for one repo's committed manifest. With a
// release version, the manifest is also held to releaseProblems for it.
export function repoReport(root, state = repoState(root), release = null) {
  const report = {
    root,
    folder: state.folder,
    manifest: "absent",
    delivery_mode: null,
    converted: state.converted,
    config_files: state.files.length,
    has_block: false,
    applies: false,
    shallow: false,
    problems: [],
  };
  const manifestPath = path.join(root, "launcher-manifest.json");
  if (!fs.existsSync(manifestPath)) return report;
  let man;
  try {
    man = JSON.parse(fs.readFileSync(manifestPath, "utf8").replace(/^\uFEFF/, ""));
  } catch {
    man = null;
  }
  if (!isObject(man)) {
    // conformance's manifest check reports a manifest that is not a JSON object.
    report.manifest = "unparseable";
    return report;
  }
  report.manifest = "present";
  report.delivery_mode = man.delivery_mode ?? null;
  report.has_block = "config" in man;
  // per_game is written from the committed file, so a repo data/config-format.json records none for
  // cannot carry a block yet; config-format reports that.
  report.applies =
    state.converted && MANIFEST_MODES.includes(man.delivery_mode) && state.files.length === 1 && state.files[0].state === "stamped";
  report.problems = [...descriptorProblems(man, { root, state, checkVersion: false }), ...configWriteProblems(man, state)];
  if (report.applies && !report.has_block) report.problems.push(NO_BLOCK);
  if (report.has_block && report.problems.length === 0) {
    const tags = tagProblems(root, state, man.config);
    report.shallow = tags.shallow;
    report.problems.push(...tags.problems);
  }
  if (release !== null) report.problems.push(...releaseProblems(man, release));
  return report;
}

function resolveRoot(token) {
  for (const candidate of [token, path.join(REPOS_ROOT, token), path.join(REPOS_ROOT, `${token}-headtracking`)]) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) return path.resolve(candidate);
  }
  throw new Error(`no repo found for '${token}', as a path or under ${REPOS_ROOT}`);
}

function main(argv) {
  const rootsFileAt = argv.indexOf("--roots-file");
  let tokens = argv;
  if (rootsFileAt >= 0) {
    const file = argv[rootsFileAt + 1];
    if (file === undefined) throw new Error("--roots-file takes a file of repo paths, one per line");
    tokens = [...argv.filter((_, i) => i !== rootsFileAt && i !== rootsFileAt + 1), ...fs.readFileSync(file, "utf8").split(/\r?\n/).filter((l) => l !== "")];
  }
  const releaseAt = tokens.indexOf("--release");
  let release = null;
  if (releaseAt >= 0) {
    release = tokens[releaseAt + 1];
    if (release === undefined || !RELEASE_VERSION.test(release)) throw new Error("--release takes the version being released, written x.y.z");
    tokens = tokens.filter((_, i) => i !== releaseAt && i !== releaseAt + 1);
  }
  const json = tokens.includes("--json");
  const packaging = tokens.includes("--package");
  if ([json, packaging, release !== null].filter(Boolean).length > 1) throw new Error("--json, --package and --release are separate runs");
  tokens = tokens.filter((t) => t !== "--json" && t !== "--package");
  const unknown = tokens.find((t) => t.startsWith("--"));
  if (unknown) throw new Error(`unknown option ${unknown}`);
  const roots = tokens.length > 0 ? tokens.map(resolveRoot) : [REPOS_ROOT];
  if (release !== null) {
    let refused = false;
    for (const root of roots) {
      const manifestPath = path.join(root, "launcher-manifest.json");
      if (!fs.existsSync(manifestPath)) continue;
      const problems = releaseProblems(JSON.parse(fs.readFileSync(manifestPath, "utf8").replace(/^\uFEFF/, "")), release);
      for (const p of problems) console.log(`FAIL ${path.basename(root)}: ${p}`);
      refused ||= problems.length > 0;
    }
    return refused ? 1 : 0;
  }
  const reports = roots.map((root) => repoReport(root, repoState(root), packaging ? releaseVersionFromEnv() : null));
  if (packaging) for (const r of reports) r.problems = r.problems.filter((p) => p !== NO_BLOCK);
  if (json) {
    console.log(JSON.stringify(reports, null, 1));
    return 0;
  }
  let failed = false;
  for (const r of reports) {
    for (const p of r.problems) console.log(`FAIL ${r.folder}: ${p}`);
    if (r.shallow) console.log(`WARN ${r.folder}: a shallow clone has no tags, so canonical_since was not held to them`);
    if (r.problems.length === 0 && r.has_block) console.log(`ok   ${r.folder}`);
    failed ||= r.problems.length > 0;
  }
  return failed ? 1 : 0;
}

if (fs.realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exitCode = main(process.argv.slice(2));
}
