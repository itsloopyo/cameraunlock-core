#!/usr/bin/env node

// Writes the config descriptor's per_game in a mod's launcher-manifest.json from
// data/config-format.json and the committed canonical config: config.per_game becomes each row
// per_game lists for the repo with the value text the committed file holds there, as
// scripts/check-config-descriptor.mjs expectedPerGame() reads them. The rest of the block (path,
// anchor, legacy_source, canonical_since) is written by hand at the conversion, with
// "per_game": {} for this to fill. Only the per_game object changes: the rest of the manifest
// keeps its bytes.
//
// It writes no seed. A converted release seeds nothing, and check-config-descriptor.mjs
// configWriteProblems fails a seed of its config or legacy file.
//
//   node cameraunlock-core/scripts/encode-seed.mjs           # the repo vendoring this core
//   node cameraunlock-core/scripts/encode-seed.mjs --check   # exit 1 when per_game is stale
//   node scripts/encode-seed.mjs [--check] <repo root>
//
// The values come from the repo's converted (stamped) committed file. A repo with no
// launcher-manifest.json, or with no config block, has nothing to write.

import fs from "node:fs";
import path from "node:path";
import { isDeepStrictEqual } from "node:util";
import { fileURLToPath, pathToFileURL } from "node:url";

import { repoState } from "./check-canonical-config.mjs";
import { expectedPerGame } from "./check-config-descriptor.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

// `given` stands in for repoState(root), for a test of an entry no real repo has yet.
export function encodePerGame(root, given = null) {
  const manifestPath = path.join(root, "launcher-manifest.json");
  if (!fs.existsSync(manifestPath)) return { manifestPath, text: null, perGame: null };
  const text = fs.readFileSync(manifestPath, "utf8");
  const man = JSON.parse(text.replace(/^\uFEFF/, ""));
  const state = given ?? repoState(root);
  if (state.listing !== "legacy" && state.listing !== "mover") {
    throw new Error(`${state.folder} has no configs entry in data/config-format.json (it is ${state.listing})`);
  }
  if (!("config" in man)) return { manifestPath, text, updated: text, perGame: null };
  if (typeof man.config !== "object" || man.config === null) {
    throw new Error(`${manifestPath} has a config block that is not an object`);
  }
  if ("rows" in man.config) {
    throw new Error(`${manifestPath} has config.rows, which the descriptor no longer carries; replace it with "per_game": {} by hand`);
  }
  if (!("per_game" in man.config)) {
    throw new Error(`${manifestPath} has a config block with no per_game to write; add "per_game": {} to it by hand`);
  }
  const read = expectedPerGame(root, state);
  if (read.perGame === null) throw new Error(read.problems.join("; "));
  const perGame = { committed: state.files[0].committed, stale: !isDeepStrictEqual(man.config.per_game, read.perGame) };
  if (!perGame.stale) return { manifestPath, text, updated: text, perGame };

  const updated = replacePerGame(text, read.perGame);
  const expected = structuredClone(man);
  expected.config.per_game = read.perGame;
  if (!isDeepStrictEqual(JSON.parse(updated.replace(/^\uFEFF/, "")), expected)) {
    throw new Error(`${manifestPath} spells config.per_game in a way encode-seed cannot rewrite in place (an escape in a string, or a repeated key?)`);
  }
  return { manifestPath, text, updated, perGame };
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

// The manifest text with config.per_game replaced and every other byte kept. The object is laid
// out one row per line, indented one step deeper than the "per_game" key, the step being the one
// between "config" and "per_game", in the file's own line ending.
function replacePerGame(text, perGame) {
  const indentAt = (i) => /^[ \t]*/.exec(text.slice(text.lastIndexOf("\n", i) + 1))[0];
  const config = member(text, skipSpace(text, text.charCodeAt(0) === 0xfeff ? 1 : 0), "config");
  const target = member(text, config.start, "per_game");
  const outer = indentAt(target.key);
  const step = outer.slice(indentAt(config.key).length) || "  ";
  const eol = text.includes("\r\n") ? "\r\n" : "\n";
  const entries = Object.entries(perGame).map(([id, value]) => `${outer}${step}${JSON.stringify(id)}: ${JSON.stringify(value)}`);
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

  const result = encodePerGame(root);
  if (result.text === null) {
    console.log(`${root} has no launcher-manifest.json, so there is no per_game to write`);
    return 0;
  }
  if (result.perGame === null) {
    console.log("launcher-manifest.json has no config block, so there is no per_game to write");
    return 0;
  }
  const { committed, stale } = result.perGame;
  if (check) {
    console.log(stale ? `STALE config.per_game: not the per_game rows ${committed} holds; run encode-seed` : `ok    config.per_game matches ${committed}`);
    return stale ? 1 : 0;
  }
  if (result.updated !== result.text) fs.writeFileSync(result.manifestPath, result.updated);
  console.log(stale ? `wrote   config.per_game from ${committed}` : `ok      config.per_game already matches ${committed}`);
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = main(process.argv.slice(2));
}
