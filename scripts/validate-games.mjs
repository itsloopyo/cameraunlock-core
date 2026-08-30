#!/usr/bin/env node

// Schema gate for data/games.json.
//
// games.json is schema v1 and ~90 repos read it, but nothing checked it: a
// required key could go missing and every test in this repo stayed green,
// because the only PowerShell suite here covers the vendoring soak and
// conformance.ps1 lints mod repos rather than this file. That is how
// `env_var` disappeared from an entry unnoticed.
//
//   node scripts/validate-games.mjs           schema only (clean checkout)
//   node scripts/validate-games.mjs --fleet   also cross-check sibling repos
//
// Without --fleet it touches nothing but data/games.json, so it is safe in
// CI, where cameraunlock-core is checked out alone. --fleet adds the checks
// that need the sibling mod repos beside this one.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const GAMES_PATH = path.join(REPO_ROOT, "data", "games.json");

const TOP_LEVEL_KEYS = new Set(["$comment", "schema_version", "games"]);
const SCHEMA_VERSION = 1;

// Required by schema v1. Renaming or removing one silently breaks detection
// in every mod that has not re-synced, so this is the check that matters.
const REQUIRED = ["display_name", "env_var", "executable_relpath"];

// The whole vocabulary, with the JSON type each key must carry. A key that
// is not here is either a typo or a schema addition nobody classified; both
// deserve a failure rather than a silent no-op in the consumers that only
// read the keys they know.
const OPTIONAL = {
  steam_app_id: "number",
  steam_folder: "string",
  gog_ids: "array",
  ubisoft_app_ids: "array",
  epic_search_paths: "array",
  ea_search_paths: "array",
  xbox_paths: "array",
  search_paths: "array",
  registry_paths: "array",
  msix_identity_name: "string",
  data_folder: "string",
  uses_owml: "boolean",
  xbox_executable_relpath: "string",
  modded_executable_relpath: "string",
  modded_launcher_hint: "string",
  engine: "string",
  loader: "string",
  delivery_mode: "string",
  catalog_status: "string",
  repo: "string",
};
const KNOWN = new Map([...REQUIRED.map((k) => [k, "string"]), ...Object.entries(OPTIONAL)]);

const CATALOG_STATUS = new Set(["absent", "prerelease", "private", "released"]);
const DELIVERY_MODE = new Set(["external", "install_cmd", "manifest"]);

const problems = [];
const fail = (msg) => problems.push(msg);

const raw = fs.readFileSync(GAMES_PATH, "utf8");

let doc;
try {
  doc = JSON.parse(raw);
} catch (err) {
  console.error(`${GAMES_PATH} is not valid JSON: ${err.message}`);
  process.exit(1);
}

// JSON.parse keeps the last of a repeated key and says nothing, so a
// duplicated game id would quietly shadow the first entry. Only a scan of
// the source text can see it.
for (const dup of duplicateGameIds(raw)) {
  fail(`duplicate game id "${dup}" - JSON.parse keeps the last one and drops the first silently`);
}

for (const key of Object.keys(doc)) {
  if (!TOP_LEVEL_KEYS.has(key)) {
    fail(`unknown top-level key "${key}" - expected one of ${[...TOP_LEVEL_KEYS].join(", ")}`);
  }
}
if (doc.schema_version === undefined) {
  fail("schema_version is missing");
} else if (doc.schema_version !== SCHEMA_VERSION) {
  fail(
    `schema_version is ${JSON.stringify(doc.schema_version)} but this validator implements v${SCHEMA_VERSION} - ` +
      "migrate the consumers in the same change and update this gate",
  );
}
if (!doc.games || typeof doc.games !== "object" || Array.isArray(doc.games)) {
  console.error("games.json has no `games` object");
  process.exit(1);
}

const games = doc.games;
const byEnvVar = new Map();
const byRepo = new Map();

for (const [id, game] of Object.entries(games)) {
  if (!/^[a-z0-9]+(-[a-z0-9]+)*$/.test(id)) {
    fail(`${id}: game id is not hyphen-lowercase`);
  }
  if (typeof game !== "object" || game === null || Array.isArray(game)) {
    fail(`${id}: entry is not an object`);
    continue;
  }

  for (const key of REQUIRED) {
    const value = game[key];
    if (value === undefined) {
      fail(`${id}: required key "${key}" is missing (schema v${SCHEMA_VERSION})`);
    } else if (typeof value !== "string" || value.trim() === "") {
      fail(`${id}.${key} must be a non-empty string, got ${JSON.stringify(value)}`);
    }
  }

  for (const [key, value] of Object.entries(game)) {
    const expected = KNOWN.get(key);
    if (expected === undefined) {
      fail(`${id}: unknown key "${key}" - add it to validate-games.mjs and to every consumer, or fix the typo`);
      continue;
    }
    if (!typeMatches(value, expected)) {
      fail(`${id}.${key} must be a ${expected}, got ${Array.isArray(value) ? "array" : typeof value}`);
    }
  }

  if (typeof game.env_var === "string") {
    if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(game.env_var)) {
      fail(`${id}.env_var ${JSON.stringify(game.env_var)} is not a usable environment variable name`);
    }
    // Two games on one variable means setting it points both detections at
    // one install directory, and the second game silently resolves wrong.
    const owner = byEnvVar.get(game.env_var);
    if (owner) fail(`${id}.env_var ${JSON.stringify(game.env_var)} is already used by ${owner}`);
    else byEnvVar.set(game.env_var, id);
  }

  if (game.catalog_status !== undefined && !CATALOG_STATUS.has(game.catalog_status)) {
    fail(
      `${id}.catalog_status ${JSON.stringify(game.catalog_status)} is not one of ${[...CATALOG_STATUS].join(", ")}`,
    );
  }
  if (game.delivery_mode !== undefined && !DELIVERY_MODE.has(game.delivery_mode)) {
    fail(`${id}.delivery_mode ${JSON.stringify(game.delivery_mode)} is not one of ${[...DELIVERY_MODE].join(", ")}`);
  }

  if (typeof game.repo === "string") {
    if (!/^[A-Za-z0-9][\w.-]*\/[A-Za-z0-9][\w.-]*$/.test(game.repo)) {
      fail(`${id}.repo ${JSON.stringify(game.repo)} is not an owner/name pair`);
    }
    const owner = byRepo.get(game.repo);
    if (owner) fail(`${id}.repo ${JSON.stringify(game.repo)} is already claimed by ${owner}`);
    else byRepo.set(game.repo, id);
  }
}

const fleet = process.argv.includes("--fleet");
if (fleet) {
  checkFleet(games, byRepo);
}

if (problems.length > 0) {
  console.error(`data/games.json has ${problems.length} problem(s):`);
  for (const p of problems) console.error(`  ${p}`);
  process.exit(1);
}
console.log(
  `data/games.json: ${Object.keys(games).length} games, schema v${doc.schema_version}` +
    `${fleet ? " (fleet cross-checks included)" : " (schema only; pass --fleet to cross-check sibling repos)"}`,
);

// Cross-repo checks. Sibling checkouts are matched by their git remote
// rather than by directory name, because a clone can be named anything -
// kingdom-come-deliverance-2-headtracking is checked out as
// kingdom-come-deliverance-2 here.
function checkFleet(games, byRepo) {
  const fleetRoot = path.dirname(REPO_ROOT);
  const checkouts = new Map();
  for (const entry of fs.readdirSync(fleetRoot, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const dir = path.join(fleetRoot, entry.name);
    const slug = originSlug(dir);
    if (slug) checkouts.set(slug, dir);
  }
  if (checkouts.size === 0) {
    fail(`--fleet was requested but no sibling git checkouts were found in ${fleetRoot}`);
    return;
  }

  const uncloned = [...byRepo.keys()].filter((repo) => !checkouts.has(repo)).sort();
  if (uncloned.length > 0) {
    console.log(`  ${uncloned.length} repo(s) in games.json have no checkout here: ${uncloned.join(", ")}`);
  }

  // A launcher-manifest.json is what lopari deploys from. Its game_id has to
  // exist here, and it has to be the id whose `repo` is the repo the manifest
  // lives in - a manifest naming another game's id deploys into the wrong
  // install directory on a user's machine, and nothing before this catches it.
  for (const [slug, dir] of checkouts) {
    const manifestPath = path.join(dir, "launcher-manifest.json");
    if (!fs.existsSync(manifestPath)) continue;
    let manifest;
    try {
      manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    } catch (err) {
      fail(`${slug}/launcher-manifest.json is not valid JSON: ${err.message}`);
      continue;
    }
    const gameId = manifest?.mod_info?.game_id;
    if (gameId === undefined) {
      fail(`${slug}/launcher-manifest.json has no mod_info.game_id`);
      continue;
    }
    if (!Object.hasOwn(games, gameId)) {
      fail(`${slug}/launcher-manifest.json names game_id "${gameId}", which has no entry in games.json`);
      continue;
    }
    const declared = games[gameId].repo;
    if (declared !== undefined && declared !== slug) {
      fail(`${slug}/launcher-manifest.json names game_id "${gameId}", whose games.json repo is "${declared}"`);
    }
  }
}

function originSlug(dir) {
  const config = path.join(dir, ".git", "config");
  if (!fs.existsSync(config) || !fs.statSync(config).isFile()) return null;
  const section = /\[remote "origin"\]([\s\S]*?)(?=\n\[|$)/.exec(fs.readFileSync(config, "utf8"));
  if (!section) return null;
  const url = /^\s*url\s*=\s*(.+)$/m.exec(section[1]);
  if (!url) return null;
  const match = /[/:]([^/:]+\/[^/]+?)(?:\.git)?\s*$/.exec(url[1].trim());
  return match ? match[1] : null;
}

function typeMatches(value, expected) {
  if (expected === "array") return Array.isArray(value);
  return typeof value === expected && !Array.isArray(value);
}

// Minimal scan for repeated keys directly inside the `games` object. Only
// strings, escapes and nesting depth matter, so this stays far shorter than
// a parser and cannot disagree with one about what a duplicate is.
function duplicateGameIds(text) {
  const seen = new Set();
  const dups = new Set();
  let depth = 0;
  let inGames = false;
  let gamesDepth = -1;
  let pendingKey = null;

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    if (ch === '"') {
      let j = i + 1;
      let value = "";
      while (j < text.length && text[j] !== '"') {
        if (text[j] === "\\") {
          value += text[j + 1];
          j += 2;
        } else {
          value += text[j];
          j += 1;
        }
      }
      let k = j + 1;
      while (k < text.length && /\s/.test(text[k])) k += 1;
      if (text[k] === ":") pendingKey = value;
      i = j;
      continue;
    }
    if (ch === "{" || ch === "[") {
      if (!inGames && pendingKey === "games") {
        inGames = true;
        gamesDepth = depth;
      } else if (inGames && depth === gamesDepth + 1 && pendingKey !== null) {
        if (seen.has(pendingKey)) dups.add(pendingKey);
        else seen.add(pendingKey);
      }
      depth += 1;
      pendingKey = null;
      continue;
    }
    if (ch === "}" || ch === "]") {
      depth -= 1;
      if (inGames && depth === gamesDepth) inGames = false;
      pendingKey = null;
    }
  }
  return [...dups];
}
