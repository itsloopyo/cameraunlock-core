#!/usr/bin/env node
//
// Validate that a built installer ZIP is a coherent manifest-mode package:
// delivery_mode is "manifest", and every path the launcher's deploy engine
// will read (loader.archives[].source, files[].source) actually exists
// inside the ZIP. This mirrors the engine's hard requirement ("manifest
// lists file X but it is not in the package") so a broken manifest is caught
// at build time, before a user ever downloads it.
//
// cameraunlock-core is vendored (git submodule) into every mod repo, so this
// is the single home for the check that every mod's release pipeline can run.
// The launcher (lopari) owns the manifest SCHEMA; this validator is the
// mod-side gate for it, the same split as the install.cmd contract.
//
// launcher-manifest.json is the ONLY manifest. An earlier parallel format,
// mod.json (manifestVersion: 1, camelCase runtimeRequirements, loader.type, no
// delivery_mode), was never read by lopari or lopari.app - lopari's own
// audit-loaders.py classes a repo carrying only mod.json as LEGACY - and has
// been removed fleet-wide. If you are adding manifest support to a mod, this
// file's schema is the one; do not reintroduce the other. The install/uninstall
// .cmd scripts are a separate, non-manifest delivery path and stay.
//
// A manifest is only warranted where lopari actually deploys the mod, i.e. it
// is in the catalog. It has to name the real shipped paths, so an invented one
// for a pre-release repo fails on a user's machine rather than at build time.
//
//   node scripts/validate-manifest.mjs                 # this repo's own zip
//   node scripts/validate-manifest.mjs <repo-or-zip> [...]
//
// With no args it validates the host repo's newest release/*-installer.zip
// (run it right after packaging). A bare repo token (e.g. dying-light-2, or
// dying-light-2-headtracking) resolves to a sibling repo's newest
// release/*-installer.zip, for validating across a full checkout.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
// core lives at <root>/cameraunlock-core/scripts. Two levels up is the repo
// that vendors core (a mod repo -> self-validate target), or the repos root
// when core is a standalone sibling checkout (-> sibling repo tokens resolve).
const ROOT = path.resolve(SCRIPT_DIR, "..", "..");

// Sentinel for the no-args case: validate this repo's own built zip.
const SELF = Symbol("self");

const tokens = process.argv.slice(2);
const jobs = tokens.length ? tokens : [SELF];

let failures = 0;
for (const token of jobs) {
  const isSelf = token === SELF;
  const zip = isSelf ? newestInstaller(path.join(ROOT, "release")) : resolveZip(token);
  const label = isSelf ? path.basename(ROOT) : token;
  if (!zip) {
    console.error(`FAIL ${label}: no installer zip found`);
    failures += 1;
    continue;
  }
  try {
    validate(label, zip);
  } catch (e) {
    console.error(`FAIL ${label}: ${e.message}`);
    failures += 1;
  }
}

if (failures > 0) {
  console.error(`\n${failures} package(s) failed validation.`);
  process.exit(1);
}
console.log("\nall packages valid.");

function validate(label, zip) {
  // PowerShell 5.1's Compress-Archive writes backslash separators, and the
  // launcher deploys on Windows where casing does not decide a match, so both
  // are normalised away before comparing. Casing differences are still worth
  // reporting: they break any consumer that reads the zip case-sensitively.
  const entries = listZip(zip).map((e) => e.replace(/\\/g, "/"));
  const entryByLower = new Map(entries.map((e) => [e.toLowerCase(), e]));
  const manifestRaw = readEntry(zip, "launcher-manifest.json");
  if (manifestRaw === null) throw new Error("no launcher-manifest.json in zip");
  const man = JSON.parse(manifestRaw.replace(/^﻿/, ""));

  if (man.delivery_mode !== "manifest") {
    throw new Error(`delivery_mode is "${man.delivery_mode}", expected "manifest"`);
  }

  const sources = [];
  // A fetched loader archive carries no in-zip source - its bytes are
  // downloaded + hash-verified at install time - so only check bundled ones.
  for (const a of man.loader?.archives ?? []) if (a.source) sources.push(a.source);
  // Guarded like the archives line above. An entry missing `source` otherwise pushed
  // undefined, which threw a bare TypeError from the replace() below - reported as the
  // failure reason with neither the file nor the entry named - and counted toward the
  // length check, defeating it.
  for (const f of man.files ?? []) {
    if (!f.source) throw new Error(`manifest files[] entry has no "source": ${JSON.stringify(f)}`);
    sources.push(f.source);
  }

  // A manifest that declares nothing passed the "everything declared is present"
  // check vacuously and printed OK. That is the worst outcome for a gate whose whole
  // job is catching a package that would deploy nothing - the manifest is round-tripped
  // through ConvertFrom-Json/ConvertTo-Json during stamping, so a depth overflow or a
  // renamed key silently empties it and CI publishes happily.
  if (sources.length === 0) {
    throw new Error("manifest declares no bundled sources - nothing would be deployed");
  }

  const missing = [];
  const miscased = [];
  for (const source of sources) {
    const declared = source.replace(/\\/g, "/");
    const entry = entryByLower.get(declared.toLowerCase());
    if (entry === undefined) missing.push(declared);
    else if (entry !== declared) miscased.push(`${declared} (zip has "${entry}")`);
  }
  if (missing.length > 0) {
    throw new Error(`manifest sources missing from zip: ${missing.join(", ")}`);
  }

  const seeds = (man.loader?.seed ?? []).length;
  const rt = (man.runtime_requirements ?? []).length;
  console.log(
    `OK   ${label}: ${path.basename(zip)} — ${sources.length} file(s), ${seeds} seed(s), ${rt} runtime req(s)`,
  );
  if (miscased.length > 0) {
    console.log(
      `WARN ${label}: manifest source(s) matched the zip only after ignoring case, fix the manifest casing: ${miscased.join(", ")}`,
    );
  }
}

function resolveZip(token) {
  if (token.endsWith(".zip")) return fs.existsSync(token) ? path.resolve(token) : null;
  // token is a repo dir name or a catalog id; find its release dir under the
  // repos root (only meaningful from a standalone core checkout).
  for (const name of [token, `${token}-headtracking`]) {
    const z = newestInstaller(path.join(ROOT, name, "release"));
    if (z) return z;
  }
  return null;
}

function newestInstaller(dir) {
  if (!fs.existsSync(dir)) return null;
  const zips = fs
    .readdirSync(dir)
    .filter((n) => n.endsWith("-installer.zip"))
    .map((n) => path.join(dir, n))
    .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
  return zips[0] ?? null;
}

function systemTar() {
  if (process.platform === "win32") {
    const sys = path.join(process.env.SystemRoot || "C:\\Windows", "System32", "tar.exe");
    if (fs.existsSync(sys)) return sys;
  }
  return "tar";
}

function listZip(zip) {
  return execFileSync(systemTar(), ["-tf", zip], { encoding: "utf8" })
    .split(/\r?\n/)
    .filter(Boolean);
}

function readEntry(zip, name) {
  try {
    return execFileSync(systemTar(), ["-xf", zip, "-O", name], {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    });
  } catch {
    return null;
  }
}
