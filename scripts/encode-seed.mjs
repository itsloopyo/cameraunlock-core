#!/usr/bin/env node

// Rewrites what launcher-manifest.json restates from a mod's canonical config, from the committed
// file: the seeds that write the config, and the config descriptor's rows.
//
// A seed is one of loader.seed[], a top-level seed[] or variants[].loader.seed[], the shapes
// Assert-ManifestSeedsMatchShipped checks at packaging (design 5.1). It writes a config when its
// target, resolved against its anchor, is an `installed` path data/config-format.json records
// for the repo; every other seed (BepInEx.cfg, a marks file) is left alone. Its content_b64
// becomes the committed file's bytes.
//
// config.rows becomes the launcher rows the committed file holds, as
// scripts/check-config-descriptor.mjs expectedRows() reads them. The rest of the block (path,
// anchor, legacy_source, canonical_since) is written by hand at the conversion, with "rows": {}
// for this to fill.
//
// Only the content_b64 strings and the rows object change: the rest of the manifest keeps its
// bytes.
//
//   node cameraunlock-core/scripts/encode-seed.mjs           # the repo vendoring this core
//   node cameraunlock-core/scripts/encode-seed.mjs --check   # exit 1 when a seed or rows is stale
//   node scripts/encode-seed.mjs [--check] <repo root>
//
// The seeded file has to be the repo's converted (stamped) committed file. A repo with no
// launcher-manifest.json, or with no seed that writes a config and no config block, has nothing
// to encode.

import fs from "node:fs";
import path from "node:path";
import { isDeepStrictEqual } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";

import { repoState } from "./check-canonical-config.mjs";
import { LAUNCHER_ROWS, expectedRows, gameRelativeTargets } from "./check-config-descriptor.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

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

// For each seed, the committed file whose bytes it must carry, or null when it seeds no config.
function seededFiles(state, man, seeds) {
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
  if (!fs.existsSync(manifestPath)) return { manifestPath, text: null, seeds: [], rows: null };
  const text = fs.readFileSync(manifestPath, "utf8");
  const man = JSON.parse(text.replace(/^\uFEFF/, ""));
  const state = repoState(root);
  if (state.listing !== "legacy" && state.listing !== "mover") {
    throw new Error(`${state.folder} has no configs entry in data/config-format.json (it is ${state.listing})`);
  }
  const seeds = seedLists(man);
  const committed = seededFiles(state, man, seeds);

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
  let updated = text.replace(/("content_b64"\s*:\s*")([^"\\]*)(")/g, (m, open, value, close) =>
    replacement.has(value) ? open + replacement.get(value) + close : m,
  );

  const expected = structuredClone(man);
  seedLists(expected).forEach(({ seed }, i) => {
    if (results[i] !== null) seed.content_b64 = results[i].b64;
  });

  let rows = null;
  if ("config" in man) {
    if (typeof man.config !== "object" || man.config === null || !("rows" in man.config)) {
      throw new Error(`${manifestPath} has a config block with no rows to write; add "rows": {} to it by hand`);
    }
    const read = expectedRows(root, state);
    if (read.rows === null) throw new Error(read.problems.join("; "));
    rows = { committed: state.files[0].committed, stale: !isDeepStrictEqual(man.config.rows, read.rows) };
    if (rows.stale) {
      updated = replaceRows(updated, read.rows);
      expected.config.rows = read.rows;
    }
  }

  if (!isDeepStrictEqual(JSON.parse(updated.replace(/^\uFEFF/, "")), expected)) {
    throw new Error(`${manifestPath} spells a content_b64 or config.rows in a way encode-seed cannot rewrite in place (an escape in a string, or a repeated key?)`);
  }
  return { manifestPath, text, updated, seeds: results.filter((r) => r !== null), rows };
}

const skipSpace = (t, i) => {
  while (i < t.length && " \t\r\n".includes(t[i])) i++;
  return i;
};

function stringEnd(t, i) {
  let j = i + 1;
  while (t[j] !== '"') j += t[j] === "\\" ? 2 : 1;
  return j + 1;
}

function valueEnd(t, i) {
  if (t[i] === '"') return stringEnd(t, i);
  if (t[i] !== "{" && t[i] !== "[") {
    let j = i;
    while (j < t.length && !",}] \t\r\n".includes(t[j])) j++;
    return j;
  }
  let depth = 0;
  for (let j = i; ; ) {
    if (t[j] === '"') {
      j = stringEnd(t, j);
      continue;
    }
    if (t[j] === "{" || t[j] === "[") depth++;
    else if ((t[j] === "}" || t[j] === "]") && --depth === 0) return j + 1;
    j++;
  }
}

// Where the value of `name` sits in the object that opens at t[open], and where its key starts.
function member(t, open, name) {
  let i = skipSpace(t, open + 1);
  while (t[i] === '"') {
    const keyEnd = stringEnd(t, i);
    const start = skipSpace(t, skipSpace(t, keyEnd) + 1);
    const end = valueEnd(t, start);
    if (JSON.parse(t.slice(i, keyEnd)) === name) return { key: i, start, end };
    i = skipSpace(t, end);
    if (t[i] === ",") i = skipSpace(t, i + 1);
  }
  throw new Error(`launcher-manifest.json has no "${name}" member where encode-seed looks for it`);
}

// The manifest text with config.rows replaced and every other byte kept. The object is laid out
// one row per line, indented one step deeper than the "rows" key, the step being the one between
// "config" and "rows", in the file's own line ending.
function replaceRows(text, rows) {
  const indentAt = (i) => /^[ \t]*/.exec(text.slice(text.lastIndexOf("\n", i) + 1))[0];
  const config = member(text, skipSpace(text, text.charCodeAt(0) === 0xfeff ? 1 : 0), "config");
  const target = member(text, config.start, "rows");
  const outer = indentAt(target.key);
  const step = outer.slice(indentAt(config.key).length) || "  ";
  const eol = text.includes("\r\n") ? "\r\n" : "\n";
  const entries = LAUNCHER_ROWS.filter((r) => r.id in rows).map((r) => `${outer}${step}"${r.id}": ${rows[r.id]}`);
  const body = entries.length === 0 ? "{}" : `{${eol}${entries.join(`,${eol}`)}${eol}${outer}}`;
  return text.slice(0, target.start) + body + text.slice(target.end);
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
    console.log("no seed in launcher-manifest.json writes a config data/config-format.json records; no seed to encode");
  }
  if (result.rows === null) {
    console.log("launcher-manifest.json has no config block, so there are no rows to write");
  }
  if (check) {
    for (const s of result.seeds) {
      console.log(s.stale ? `STALE ${s.target}: content_b64 is not ${s.committed}; run encode-seed` : `ok    ${s.target} carries ${s.committed}`);
    }
    if (result.rows !== null) {
      console.log(result.rows.stale ? `STALE config.rows: not the rows ${result.rows.committed} holds; run encode-seed` : `ok    config.rows match ${result.rows.committed}`);
    }
    return result.seeds.some((s) => s.stale) || result.rows?.stale ? 1 : 0;
  }
  if (result.updated !== result.text) fs.writeFileSync(result.manifestPath, result.updated);
  for (const s of result.seeds) {
    console.log(s.stale ? `encoded ${s.target} from ${s.committed}` : `ok      ${s.target} already carries ${s.committed}`);
  }
  if (result.rows !== null) {
    console.log(result.rows.stale ? `wrote   config.rows from ${result.rows.committed}` : `ok      config.rows already match ${result.rows.committed}`);
  }
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = main(process.argv.slice(2));
}
