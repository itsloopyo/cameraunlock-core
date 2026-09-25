#!/usr/bin/env node

// Runs scripts/encode-seed.mjs against copies of real manifests in throwaway git repos:
// resident-evil-2-headtracking's (loader.seed, game_root) and prey-headtracking's (exe_dir,
// two store layouts), both copied unchanged into data/fixtures/encode-seed/, plus the
// top-level and variant seed shapes built from the first. The fixtures seed HeadTracking.ini,
// which data/config-format.json records as each repo's legacy file, so the cases retarget
// the seed to CameraUnlock.ini, the config the entries record, and one case keeps the
// fixture's own seed to show a seed of the legacy file is not a config seed.
//
//   node scripts/test-encode-seed.mjs      (pixi run test-encode-seed, part of pixi run check)

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SCRIPT = path.join(CORE_ROOT, "scripts", "encode-seed.mjs");
const FIXTURES = path.join(CORE_ROOT, "data", "fixtures", "encode-seed");
const CANONICAL = fs.readFileSync(path.join(CORE_ROOT, "data", "fixtures", "canonical-ini", "head-tracking", "all-concepts.ini"));
const OTHER_SEED = Buffer.from("[Loader]\r\nEnabled=true\r\n").toString("base64");

const failures = [];
let checks = 0;
const check = (ok, message) => {
  checks++;
  if (!ok) failures.push(message);
};

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "encode-seed-"));

// A throwaway checkout: the folder name is what data/config-format.json lists it under.
function repo(label, name, manifest, committed) {
  const root = path.join(scratch, label, name);
  fs.mkdirSync(root, { recursive: true });
  const init = spawnSync("git", ["init", "-q", root], { encoding: "utf8" });
  if (init.status !== 0) throw new Error(`git init failed: ${init.stderr}`);
  fs.writeFileSync(path.join(root, "launcher-manifest.json"), manifest);
  if (committed !== undefined) fs.writeFileSync(path.join(root, "HeadTracking.ini"), committed);
  return root;
}

function run(root, ...args) {
  const r = spawnSync(process.execPath, [SCRIPT, ...args, root], { encoding: "utf8" });
  return { status: r.status, out: r.stdout + r.stderr };
}

const readManifest = (root) => fs.readFileSync(path.join(root, "launcher-manifest.json"), "utf8");
const seedsOf = (man) => [...(man.loader?.seed ?? []), ...(man.seed ?? []), ...(man.variants ?? []).flatMap((v) => v.loader?.seed ?? [])];

// A stale manifest: --check fails, encoding rewrites the blob and nothing else, and then the
// manifest is current and a second run changes nothing.
function roundTrip(label, name, manifestText, configSeeds) {
  const root = repo(label, name, manifestText, CANONICAL);
  const before = run(root, "--check");
  check(before.status === 1 && before.out.includes("STALE"), `${label}: --check on a stale seed should exit 1, got ${before.status}\n${before.out}`);
  check(readManifest(root) === manifestText, `${label}: --check should not write`);

  const encoded = run(root);
  check(encoded.status === 0, `${label}: encoding should exit 0, got ${encoded.status}\n${encoded.out}`);
  const text = readManifest(root);
  const want = CANONICAL.toString("base64");
  const oldBlobs = new Set(configSeeds.map((s) => s.content_b64));
  let expectedText = manifestText;
  for (const old of oldBlobs) expectedText = expectedText.split(`"${old}"`).join(`"${want}"`);
  check(text === expectedText, `${label}: only the config seeds' content_b64 should change`);
  const seeds = seedsOf(JSON.parse(text));
  check(
    seeds.filter((s) => s.content_b64 === want).length === configSeeds.length,
    `${label}: each config seed should carry the committed bytes`,
  );
  check(
    seeds.filter((s) => s.content_b64 === OTHER_SEED).length === seedsOf(JSON.parse(manifestText)).filter((s) => s.content_b64 === OTHER_SEED).length,
    `${label}: a seed that writes no config should keep its content`,
  );

  const after = run(root, "--check");
  check(after.status === 0, `${label}: --check after encoding should exit 0, got ${after.status}\n${after.out}`);
  run(root);
  check(readManifest(root) === text, `${label}: encoding a current manifest should change nothing`);
}

// The fixture's seed of the legacy file, retargeted to the config beside it.
const retarget = (text, from, to) => {
  if (text.split(from).length !== 2) throw new Error(`the fixture does not name ${from} once`);
  return text.replace(from, to);
};
const re2Fixture = fs.readFileSync(path.join(FIXTURES, "resident-evil-2-headtracking.launcher-manifest.json"), "utf8");
const re2Text = retarget(re2Fixture, '"target": "reframework/plugins/HeadTracking.ini"', '"target": "reframework/plugins/CameraUnlock.ini"');
const re2 = JSON.parse(re2Text);
roundTrip("loader-seed", "resident-evil-2-headtracking", re2Text, re2.loader.seed);

const preyText = retarget(fs.readFileSync(path.join(FIXTURES, "prey-headtracking.launcher-manifest.json"), "utf8"), '"target": "HeadTracking.ini"', '"target": "CameraUnlock.ini"');
roundTrip("exe-dir", "prey-headtracking", preyText, JSON.parse(preyText).loader.seed);

const configSeed = re2.loader.seed[0];
const otherSeed = { target: "BepInEx/config/BepInEx.cfg", content_b64: OTHER_SEED };
const { loader, ...rest } = re2;
const { seed: _seed, ...loaderWithoutSeed } = loader;
const topLevel = { ...rest, loader: loaderWithoutSeed, seed: [configSeed, otherSeed] };
roundTrip("top-level-seed", "resident-evil-2-headtracking", JSON.stringify(topLevel, null, 2), [configSeed]);

const variants = {
  ...rest,
  delivery_mode: "manifest_variants",
  variants: ["a", "b"].map((id) => ({ id, loader: { ...loader, seed: [configSeed, otherSeed] }, files: re2.files })),
};
roundTrip("variants", "resident-evil-2-headtracking", JSON.stringify(variants, null, 2), [configSeed, configSeed]);

// A seed of the legacy file writes no config, so encoding leaves it as it was.
const legacySeed = repo("legacy-seed", "resident-evil-2-headtracking", re2Fixture, CANONICAL);
const legacySeedRun = run(legacySeed);
check(
  legacySeedRun.status === 0 && legacySeedRun.out.includes("no seed in launcher-manifest.json writes a config") && readManifest(legacySeed) === re2Fixture,
  `legacy seed: should be left alone, got ${legacySeedRun.status}
${legacySeedRun.out}`,
);

// The committed file must be the converted one: an unstamped file is refused and not encoded.
const legacyBytes = Buffer.from(configSeed.content_b64, "base64");
const unstamped = repo("unstamped", "resident-evil-2-headtracking", re2Text, legacyBytes);
const refused = run(unstamped);
check(refused.status !== 0 && refused.out.includes("which is unstamped"), `unstamped: should refuse, got ${refused.status}\n${refused.out}`);
check(readManifest(unstamped) === re2Text, "unstamped: the manifest should be left as it was");

// A seed that writes no config and happens to carry the config seed's blob cannot be told
// apart from it by value, so encoding refuses instead of rewriting both.
const shared = { ...re2, loader: { ...loader, seed: [configSeed, { target: "Other.ini", content_b64: configSeed.content_b64 }] } };
const sharedRoot = repo("shared-blob", "resident-evil-2-headtracking", JSON.stringify(shared, null, 2), CANONICAL);
const sharedRun = run(sharedRoot);
check(sharedRun.status !== 0 && sharedRun.out.includes("carry the same content_b64"), `shared-blob: should refuse, got ${sharedRun.status}\n${sharedRun.out}`);

// JSON can spell the same blob with an escape, which a rewrite by value cannot reach.
const escapedText = re2Text.replace(`"${configSeed.content_b64}"`, `"\\u${configSeed.content_b64.charCodeAt(0).toString(16).padStart(4, "0")}${configSeed.content_b64.slice(1)}"`);
check(JSON.parse(escapedText).loader.seed[0].content_b64 === configSeed.content_b64, "escaped: the fixture should spell the same blob");
const escaped = repo("escaped", "resident-evil-2-headtracking", escapedText, CANONICAL);
const escapedRun = run(escaped);
check(escapedRun.status !== 0 && escapedRun.out.includes("cannot rewrite in place"), `escaped: should refuse, got ${escapedRun.status}\n${escapedRun.out}`);
check(readManifest(escaped) === escapedText, "escaped: the manifest should be left as it was");

const unknownAnchor ={ ...re2, loader: { ...loader, seed: [{ ...configSeed, anchor: "install_dir" }] } };
const anchorRun = run(repo("anchor", "resident-evil-2-headtracking", JSON.stringify(unknownAnchor), CANONICAL));
check(anchorRun.status !== 0 && anchorRun.out.includes("unknown anchor 'install_dir'"), `unknown anchor: should refuse, got ${anchorRun.status}\n${anchorRun.out}`);

const noManifest = path.join(scratch, "no-manifest", "resident-evil-2-headtracking");
fs.mkdirSync(noManifest, { recursive: true });
const noManifestRun = run(noManifest, "--check");
check(noManifestRun.status === 0 && noManifestRun.out.includes("no launcher-manifest.json"), `no manifest: should exit 0, got ${noManifestRun.status}\n${noManifestRun.out}`);

fs.rmSync(scratch, { recursive: true, force: true });

if (failures.length > 0) {
  console.error(`${failures.length} of ${checks} checks failed:`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`encode-seed: ${checks} checks passed`);
