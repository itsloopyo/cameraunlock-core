#!/usr/bin/env node

// Re-encodes the launcher-manifest.json seeds that write a mod's canonical config, from the
// committed file's bytes (design 5.1). A seed is one of loader.seed[], a top-level seed[] or
// variants[].loader.seed[], the shapes Assert-ManifestSeedsMatchShipped checks at packaging.
// It writes a config when its target, resolved against its anchor, is an `installed` path
// data/config-format.json records for the repo; every other seed (BepInEx.cfg, a marks file)
// is left alone. Only the content_b64 strings change: the rest of the manifest keeps its bytes.
//
//   node cameraunlock-core/scripts/encode-seed.mjs           # the repo vendoring this core
//   node cameraunlock-core/scripts/encode-seed.mjs --check   # exit 1 when a seed is stale
//   node scripts/encode-seed.mjs [--check] <repo root>
//
// The seeded file has to be the repo's converted (stamped) committed file. A repo with no
// launcher-manifest.json, or none of whose seeds writes a config, has nothing to encode.

import fs from "node:fs";
import path from "node:path";
import { isDeepStrictEqual } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";

import { repoState } from "./check-canonical-config.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const GAMES = JSON.parse(fs.readFileSync(path.join(CORE_ROOT, "data", "games.json"), "utf8")).games;

const gamePath = (p) => p.replace(/\\/g, "/").toLowerCase();

function seedLists(man) {
  const lists = [
    ["loader.seed", man.loader?.seed],
    ["seed", man.seed],
    ...(man.variants ?? []).map((v, i) => [`variants[${i}].loader.seed`, v?.loader?.seed]),
  ];
  const seeds = [];
  for (const [where, list] of lists) {
    if (list === undefined) continue;
    if (!Array.isArray(list)) throw new Error(`launcher-manifest.json ${where} is not an array`);
    list.forEach((seed, i) => seeds.push({ where: `${where}[${i}]`, seed }));
  }
  return seeds;
}

// The paths, relative to the game folder, a seed's target can land on. Lopari resolves
// exe_dir from the executable it detected, which is executable_relpath or, for a Game Pass
// install, xbox_executable_relpath.
function gameRelativeTargets(seed, man) {
  const anchor = seed.anchor ?? "game_root";
  if (anchor === "game_root") return [gamePath(seed.target)];
  if (anchor === "mod_home") return [];
  if (anchor !== "exe_dir") throw new Error(`seed ${seed.target} has an unknown anchor '${anchor}'`);
  const gameId = man.mod_info?.game_id;
  const game = GAMES[gameId];
  if (game === undefined) {
    throw new Error(`seed ${seed.target} is anchored at exe_dir, and mod_info.game_id '${gameId}' is not in data/games.json`);
  }
  return [game.executable_relpath, game.xbox_executable_relpath]
    .filter((exe) => exe !== undefined)
    .map((exe) => gamePath(path.win32.join(path.win32.dirname(exe), seed.target)));
}

// For each seed, the committed file whose bytes it must carry, or null when it seeds no config.
function seededFiles(root, man, seeds) {
  const state = repoState(root);
  if (state.listing !== "legacy" && state.listing !== "mover") {
    throw new Error(`${state.folder} has no configs entry in data/config-format.json (it is ${state.listing})`);
  }
  return seeds.map(({ where, seed }) => {
    if (typeof seed?.target !== "string" || typeof seed.content_b64 !== "string") {
      throw new Error(`launcher-manifest.json ${where} needs a string target and content_b64`);
    }
    const targets = gameRelativeTargets(seed, man);
    const files = state.files.filter((f) => f.installed.some((p) => targets.includes(gamePath(p))));
    const committed = [...new Set(files.map((f) => f.committed))];
    if (committed.length === 0) return null;
    if (committed.length > 1) {
      throw new Error(`seed ${seed.target} lands on installed paths of ${committed.length} config files in data/config-format.json`);
    }
    const [file] = files;
    if (file.committed === null) {
      throw new Error(`seed ${seed.target} writes ${file.installed.join(" or ")}, and data/config-format.json records no committed file for it`);
    }
    if (file.state !== "stamped") {
      throw new Error(`seed ${seed.target} writes ${file.committed}, which is ${file.state}; encode-seed encodes the rendered file of a converted repo`);
    }
    return file.committed;
  });
}

export function encodeSeeds(root) {
  const manifestPath = path.join(root, "launcher-manifest.json");
  if (!fs.existsSync(manifestPath)) return { manifestPath, text: null, seeds: [] };
  const text = fs.readFileSync(manifestPath, "utf8");
  const man = JSON.parse(text.replace(/^\uFEFF/, ""));
  const seeds = seedLists(man);
  const committed = seededFiles(root, man, seeds);

  const results = seeds.map(({ seed }, i) => {
    if (committed[i] === null) return null;
    const bytes = fs.readFileSync(path.join(root, ...committed[i].split("/")));
    const b64 = bytes.toString("base64");
    return { target: seed.target, committed: committed[i], old: seed.content_b64, b64, stale: b64 !== seed.content_b64 };
  });

  // The text is rewritten by value, so every seed carrying the same old blob must end up
  // carrying the same new one.
  const replacement = new Map();
  seeds.forEach(({ seed }, i) => {
    const next = results[i]?.b64 ?? seed.content_b64;
    const seen = replacement.get(seed.content_b64);
    if (seen !== undefined && seen !== next) {
      throw new Error(`two seeds carry the same content_b64 and need different contents (${seed.target} among them); give each its own blob by hand first`);
    }
    replacement.set(seed.content_b64, next);
  });
  const updated = text.replace(/("content_b64"\s*:\s*")([^"\\]*)(")/g, (m, open, value, close) =>
    replacement.has(value) ? open + replacement.get(value) + close : m,
  );

  const expected = structuredClone(man);
  seedLists(expected).forEach(({ seed }, i) => {
    if (results[i] !== null) seed.content_b64 = results[i].b64;
  });
  if (!isDeepStrictEqual(JSON.parse(updated.replace(/^\uFEFF/, "")), expected)) {
    throw new Error(`${manifestPath} spells a content_b64 in a way encode-seed cannot rewrite in place (an escape in the string?)`);
  }
  return { manifestPath, text, updated, seeds: results.filter((r) => r !== null) };
}

function main(argv) {
  const check = argv.includes("--check");
  const rest = argv.filter((a) => a !== "--check");
  const unknown = rest.find((a) => a.startsWith("--"));
  if (unknown) throw new Error(`unknown option ${unknown}`);
  if (rest.length > 1) throw new Error("encode-seed takes one repo root");
  const root = path.resolve(rest[0] ?? path.dirname(CORE_ROOT));

  const result = encodeSeeds(root);
  if (result.text === null) {
    console.log(`${root} has no launcher-manifest.json, so there is no seed to encode`);
    return 0;
  }
  if (result.seeds.length === 0) {
    console.log("no seed in launcher-manifest.json writes a config data/config-format.json records; nothing to encode");
    return 0;
  }
  const stale = result.seeds.filter((s) => s.stale);
  if (check) {
    for (const s of result.seeds) {
      console.log(s.stale ? `STALE ${s.target}: content_b64 is not ${s.committed}; run encode-seed` : `ok    ${s.target} carries ${s.committed}`);
    }
    return stale.length > 0 ? 1 : 0;
  }
  if (result.updated !== result.text) fs.writeFileSync(result.manifestPath, result.updated);
  for (const s of result.seeds) {
    console.log(s.stale ? `encoded ${s.target} from ${s.committed}` : `ok      ${s.target} already carries ${s.committed}`);
  }
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = main(process.argv.slice(2));
}
