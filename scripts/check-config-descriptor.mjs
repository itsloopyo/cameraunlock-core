#!/usr/bin/env node

// The `config` descriptor in launcher-manifest.json: where a converted mod's config file lives
// and which launcher preference rows it binds, with their committed values (docs/canonical-config.md,
// "The config descriptor"). This file holds every rule the block is held to. validate-manifest.mjs
// runs them on a built ZIP's manifest, conformance runs them on the committed one, and
// encode-seed.mjs writes `rows` from expectedRows().
//
//   node scripts/check-config-descriptor.mjs                  # the repo vendoring this core
//   node scripts/check-config-descriptor.mjs <repo> [...]     # repo paths or sibling names
//   node scripts/check-config-descriptor.mjs --json --roots-file <file>
//
// The default run prints each repo's problems, a converted repo delivered by manifest that
// carries no block among them, and exits 1 when there is one. --json prints what
// scripts/conformance.ps1 decides its config-descriptor check from, and always exits 0.
//
// The committed manifest carries a placeholder mod_info.version that packaging stamps, so the
// rule holding canonical_since to that version runs on a built ZIP only. The rule holding it
// above every v* tag whose committed config lacks the stamp needs the tags, so it runs on a full
// clone only.

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { isDeepStrictEqual } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";

import { repoState } from "./check-canonical-config.mjs";
import { findSection, findValue, hasCanonicalStamp, parseCanonicalIni } from "./lib/canonical-ini.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPOS_ROOT = path.dirname(CORE_ROOT);
const readJson = (rel) => JSON.parse(fs.readFileSync(path.join(CORE_ROOT, rel), "utf8"));
const SCHEMA = readJson("data/config-schema.json");
const FORMAT = readJson("data/config-format.json");
const GAMES = readJson("data/games.json").games;
const TRACKING_MODE = readJson("data/pipeline-conformance.json").preference_modes.tracking_mode;

function boolConcept(id) {
  const concept = SCHEMA.concepts.find((c) => c.id === id);
  if (!concept || !concept.canonical || concept.type !== "bool") {
    throw new Error(`data/config-schema.json has no canonical bool concept ${id}, which the config descriptor reads`);
  }
  return { id, section: concept.section, key: concept.key, default: concept.default };
}

// In the order the generator writes them.
export const LAUNCHER_ROWS = ["EnableOnStartup", "WorldSpaceYaw", "RotationEnabled", "PositionEnabled", "TrueFreeLook"].map(boolConcept);
const POSITION_ALLOWED = boolConcept("PositionAllowed");
// The one row data/config-format.json descriptor_omits can list.
const WORLD_SPACE_YAW = LAUNCHER_ROWS.find((r) => r.id === "WorldSpaceYaw");
const TRACKING_ROWS = ["RotationEnabled", "PositionEnabled"];
if (!isDeepStrictEqual(TRACKING_MODE.channels, TRACKING_ROWS)) {
  throw new Error(`data/pipeline-conformance.json tracking_mode channels are ${TRACKING_MODE.channels.join(", ")}, and the descriptor reads ${TRACKING_ROWS.join(", ")}`);
}

const FIELDS = ["path", "anchor", "legacy_source", "canonical_since", "rows"];
// The fleet's one config name. No v* release before the canonical format reads a file of that
// name, so a launcher that manages it leaves alone the file an older version reads after a rollback.
const CONFIG_NAME = "CameraUnlock.ini";
const ANCHORS = ["game_root", "exe_dir", "mod_home"];
const MANIFEST_MODES = ["manifest", "manifest_variants"];
const RELEASE_VERSION = /^\d+\.\d+\.\d+$/;
const TAG_VERSION = /^v(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$/;

const isObject = (v) => typeof v === "object" && v !== null && !Array.isArray(v);
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

// The paths, relative to the game folder, a seed's or a files[] row's target can land on. The
// launcher resolves exe_dir from the executable it detected, which is executable_relpath or, for
// a Game Pass install, xbox_executable_relpath. A mod_home target is never in the game folder.
export function gameRelativeTargets(item, man) {
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
    if (!FIELDS.includes(key)) problems.push(`config has an unknown field "${key}"; it holds ${FIELDS.join(", ")}`);
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
      if (typeof version !== "string" || !/^\d+\.\d+\.\d+(-.+)?$/.test(version)) {
        problems.push(`config.canonical_since needs mod_info.version to compare with, and it is ${JSON.stringify(version)}`);
      } else if (compareVersions(since, version) > 0) {
        problems.push(`config.canonical_since ${since} is above mod_info.version ${version}; it names a version that shipped the canonical file`);
      }
    }
  }

  if (!("rows" in config)) {
    problems.push("config has no rows");
    return problems;
  }
  const rows = config.rows;
  if (!isObject(rows)) return [...problems, `config.rows must be an object, not ${JSON.stringify(rows)}`];
  const ids = LAUNCHER_ROWS.map((r) => r.id);
  for (const [id, value] of Object.entries(rows)) {
    if (!ids.includes(id)) problems.push(`config.rows names ${id}, which is not one of ${ids.join(", ")}`);
    else if (typeof value !== "boolean") problems.push(`config.rows.${id} is ${JSON.stringify(value)}, not true or false`);
  }
  if ("RotationEnabled" in rows && !("PositionEnabled" in rows)) {
    problems.push("config.rows has RotationEnabled without PositionEnabled; a tracking mode is written through PositionEnabled");
  }
  if (typeof rows.RotationEnabled === "boolean" && typeof rows.PositionEnabled === "boolean") {
    const listed = TRACKING_MODE.modes.some((m) => m.RotationEnabled === rows.RotationEnabled && m.PositionEnabled === rows.PositionEnabled);
    if (!listed) {
      problems.push(
        `config.rows RotationEnabled=${rows.RotationEnabled}, PositionEnabled=${rows.PositionEnabled} is no mode data/pipeline-conformance.json preference_modes lists`,
      );
    }
  }
  return problems;
}

// The rows a repo's descriptor holds: every launcher concept its committed file has as an active
// line, with the committed value. Two kinds are left out. A concept data/config-format.json
// descriptor_omits lists for the repo, which the launcher then never manages. And the tracking
// pair, when the file has PositionAllowed=false, since that mod runs rotation only whatever the
// pair says. A committed WorldSpaceYaw away from the fleet default is refused unless it is
// omitted, so a game that differs on purpose cannot be handed to a launcher global by default.
export function expectedRows(root, state) {
  if (state.files.length !== 1) {
    return { rows: null, problems: [`data/config-format.json records ${state.files.length} config files for ${state.repo}, and a descriptor names one`] };
  }
  const [file] = state.files;
  if (file.state !== "stamped") {
    const which = file.committed === null ? "records no committed file" : `records ${file.committed}, which is ${file.state}`;
    return { rows: null, problems: [`descriptor rows come from the committed config, and data/config-format.json ${which} for ${state.repo}`] };
  }
  const doc = parseCanonicalIni(fs.readFileSync(path.join(root, ...file.committed.split("/"))));
  const omitted = (FORMAT.descriptor_omits[state.repo] ?? []).map((o) => o.row);
  const problems = [];
  const read = (concept) => {
    const section = findSection(doc, concept.section);
    const line = section === null ? null : findValue(section, concept.key);
    if (line === null) return undefined;
    if (line.value === "true") return true;
    if (line.value === "false") return false;
    problems.push(`${file.committed} line ${line.line}: [${concept.section}] ${concept.key}=${line.value} is not true or false, the values the renderer writes`);
    return undefined;
  };
  const positionAllowed = read(POSITION_ALLOWED);
  const rows = {};
  for (const concept of LAUNCHER_ROWS) {
    const value = read(concept);
    if (omitted.includes(concept.id)) {
      if (value === undefined) {
        problems.push(`data/config-format.json descriptor_omits lists ${concept.id} for ${state.repo}, and ${file.committed} has no ${concept.key} line`);
      }
      continue;
    }
    if (value === undefined) continue;
    if (concept === WORLD_SPACE_YAW && value !== concept.default) {
      problems.push(
        `${file.committed} has [${concept.section}] ${concept.key}=${value}, away from the fleet default ${concept.default}; a game whose default differs on purpose is listed in data/config-format.json descriptor_omits, so a launcher global never reaches it, and any other game commits the default`,
      );
      continue;
    }
    if (positionAllowed === false && TRACKING_ROWS.includes(concept.id)) continue;
    rows[concept.id] = value;
  }
  return { rows: problems.length === 0 ? rows : null, problems };
}

function payloads(man) {
  const variants = Array.isArray(man.variants) ? man.variants : [];
  return variants.length === 0 ? [man] : variants;
}

function seedsOf(man) {
  const seeds = [];
  for (const list of [man.seed, ...payloads(man).map((p) => p?.loader?.seed)]) {
    if (Array.isArray(list)) seeds.push(...list.filter(isObject));
  }
  return seeds;
}

function listingText(state) {
  if (state.listing === "unlisted") return "it is not in data/config-format.json";
  if (state.listing === "exempt") return `data/config-format.json exempts it: ${state.reason}`;
  if (state.listing === "predecessor") return `it is an earlier repo of ${state.repo}`;
  return "no committed config file carries the [CameraUnlock] stamp";
}

// The rules that hold the block to the repo it was built from: data/config-format.json's entry,
// the committed file, and the manifest's own seeds and files[].
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
  const seeds = seedsOf(man).filter((s) => typeof s.target === "string");
  const files = payloads(man)
    .flatMap((payload) => (Array.isArray(payload?.files) ? payload.files : []))
    .filter((f) => isObject(f) && typeof f.target === "string");
  const atExeDir = [config, ...seeds, ...files].filter((i) => anchorOf(i) === "exe_dir").map((i) => i.target ?? i.path);
  if (!gameKnown && atExeDir.length > 0) {
    problems.push(
      `${atExeDir.join(", ")} anchored at exe_dir, and mod_info.game_id ${JSON.stringify(gameId)} is not in data/games.json, so which file each lands on cannot be checked`,
    );
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

  // An item hits a file when it names the file from the same anchor, or resolves onto one of the
  // file's paths in the game folder.
  const hits = (name, gameFiles) => (item) =>
    (anchorOf(item) === anchor && lower(item.target) === name.toLowerCase()) || targets(item).some((t) => gameFiles.has(t));
  const onConfig = hits(p, new Set([...installedLower, ...targets({ target: p, anchor })]));
  const onLegacy = legacy === null
    ? () => false
    : hits(legacy, new Set([...installed.map((i) => besideOf(i, legacyName).toLowerCase()), ...targets({ target: legacy, anchor })]));
  const hitFile = (item) => (onConfig(item) ? "the config" : onLegacy(item) ? `the legacy file ${legacy}` : null);

  for (const f of files) {
    const hit = hitFile(f);
    if (hit) problems.push(`files[] ${f.target} (${anchorOf(f)}) lands on ${hit}; a files[] row is copied over whatever is there at every deploy, the player's settings included`);
  }
  for (const seed of seeds) {
    const hit = hitFile(seed);
    if (hit) {
      problems.push(
        `seed ${seed.target} (${anchorOf(seed)}) writes ${hit}; a package with a config block seeds neither its config nor its legacy file. The mod creates the config at first launch and imports the legacy file only while the config is absent, and a launcher that hash-checks seeded files (Lopari v0.9.0) downloads a drifted one again, which a file the launcher edits always is`,
      );
    }
  }

  const expected = expectedRows(root, state);
  problems.push(...expected.problems);
  if (expected.rows !== null && !isDeepStrictEqual(config.rows, expected.rows)) {
    problems.push(`config.rows is ${JSON.stringify(config.rows)}, and ${file.committed} gives ${JSON.stringify(expected.rows)}; ${fixHint(state)}`);
  }
  return problems;
}

function fixHint(state) {
  const omits = (FORMAT.descriptor_omits[state.repo] ?? []).map((o) => o.row);
  const omitted = omits.length > 0 ? ` (data/config-format.json descriptor_omits leaves out ${omits.join(", ")})` : "";
  return `node cameraunlock-core/scripts/encode-seed.mjs, which render-config runs, rewrites rows${omitted}`;
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

const NO_BLOCK =
  "converted, delivered by manifest, and launcher-manifest.json has no config block; write path and anchor by hand, legacy_source and canonical_since where the rules ask for them, and \"rows\": {}, then run render-config";

// What conformance's config-descriptor check reads for one repo's committed manifest.
export function repoReport(root, state = repoState(root)) {
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
  // Rows are written from the committed file, so a repo data/config-format.json records none for
  // cannot carry a block yet; config-format reports that.
  report.applies =
    state.converted && MANIFEST_MODES.includes(man.delivery_mode) && state.files.length === 1 && state.files[0].state === "stamped";
  report.problems = descriptorProblems(man, { root, state, checkVersion: false });
  if (report.applies && !report.has_block) report.problems.push(NO_BLOCK);
  if (report.has_block && report.problems.length === 0) {
    const tags = tagProblems(root, state, man.config);
    report.shallow = tags.shallow;
    report.problems.push(...tags.problems);
  }
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
  const json = tokens.includes("--json");
  tokens = tokens.filter((t) => t !== "--json");
  const unknown = tokens.find((t) => t.startsWith("--"));
  if (unknown) throw new Error(`unknown option ${unknown}`);
  const roots = tokens.length > 0 ? tokens.map(resolveRoot) : [REPOS_ROOT];
  const reports = roots.map((root) => repoReport(root));
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

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = main(process.argv.slice(2));
}
