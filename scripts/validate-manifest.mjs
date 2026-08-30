#!/usr/bin/env node
//
// Validate that a built release ZIP is a coherent package for the delivery
// mode its launcher-manifest.json declares. There are three, and each is
// checked on its own terms:
//
//   manifest     The launcher's deploy engine reads the package directly, so
//                every path it will read (loader.archives[].source,
//                files[].source) must exist inside the ZIP. This mirrors the
//                engine's hard requirement ("manifest lists file X but it is
//                not in the package") so a broken manifest is caught at build
//                time, before a user ever downloads it.
//
//   install_cmd  The launcher shells out to install.cmd instead. Some mods
//                cannot be expressed as a manifest at all - rv-there-yet
//                deploys to a Steam and an Xbox/Game Pass install in the same
//                run, and control-ultimate-edition provisions its loader by
//                copying one raw DLL under a new name, neither of which the
//                engine's extract-an-archive model covers. What must hold is
//                that the scripts are in the ZIP and everything they call is
//                too: a thin wrapper whose shared body was never staged still
//                leaves a ZIP that looks complete, and fails on the user's
//                machine with "install-body-bepinex.cmd not found".
//
//   external     A third-party mod manager owns deployment (Outer Wilds via
//                OWML). Nothing in the package provisions anything, so the
//                check is that it says where to send the user and declares no
//                loader it has no way to install.
//
// In manifest mode a declared source that is absent is fatal - the engine
// deploys from that list. In the other two the lists are descriptive, ingested
// for display and audit, so a missing source is reported as a warning rather
// than failing a package that installs correctly.
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
// release/*-installer.zip, for validating across a full checkout. A repo that
// publishes no installer at all - external delivery hands a manager-consumable
// ZIP straight to the user - falls back to its newest non-Nexus release ZIP.

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

  const mode = man.delivery_mode;
  if (mode === "install_cmd") return validateInstallCmd(label, zip, man, entries, entryByLower);
  if (mode === "external") return validateExternal(label, zip, man, entryByLower);
  if (mode !== "manifest") {
    throw new Error(
      `delivery_mode is "${mode}", expected one of "manifest", "install_cmd", "external"`,
    );
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

  const { missing, miscased } = resolveSources(sources, entryByLower);
  if (missing.length > 0) {
    throw new Error(`manifest sources missing from zip: ${missing.join(", ")}`);
  }

  const seeds = (man.loader?.seed ?? []).length;
  const rt = (man.runtime_requirements ?? []).length;
  console.log(
    `OK   ${label}: ${path.basename(zip)} — manifest, ${sources.length} file(s), ${seeds} seed(s), ${rt} runtime req(s)`,
  );
  warnMiscased(label, miscased);
}

// install_cmd: the scripts ARE the delivery mechanism, so the gate is that
// they and everything they reach for are in the package.
function validateInstallCmd(label, zip, man, entries, entryByLower) {
  const scripts = [man.install_script ?? "install.cmd", man.uninstall_script ?? "uninstall.cmd"];
  const problems = [];
  let checked = 0;

  for (const script of scripts) {
    const declared = script.replace(/\\/g, "/");
    const entry = entryByLower.get(declared.toLowerCase());
    if (entry === undefined) {
      problems.push(`${declared} is not in the zip`);
      continue;
    }
    const body = readEntry(zip, entry);
    if (body === null) {
      problems.push(`${declared} is listed in the zip but could not be read`);
      continue;
    }
    checked += 1;
    for (const dep of scriptDependencies(body)) {
      if (!entryByLower.has(dep.toLowerCase())) {
        problems.push(`${declared} calls ${dep}, which is not in the zip`);
      }
    }
  }

  if (problems.length > 0) throw new Error(problems.join("; "));

  const sources = (man.files ?? []).map((f) => f.source).filter(Boolean);
  const { missing, miscased } = resolveSources(sources, entryByLower);
  console.log(
    `OK   ${label}: ${path.basename(zip)} — install_cmd, ${checked} script(s), ${entries.length} entries`,
  );
  warnDescriptive(label, missing);
  warnMiscased(label, miscased);
}

// external: a third-party manager deploys this, so there is nothing here to
// provision and nothing to check against the engine. What the package still
// owes the user is a route to that manager.
function validateExternal(label, zip, man, entryByLower) {
  const ext = man.external;
  if (!ext) throw new Error('delivery_mode is "external" but there is no "external" block');
  for (const key of ["manager_name", "manager_url"]) {
    if (!ext[key]) {
      throw new Error(`external block has no "${key}" - nothing tells the user where to go`);
    }
  }
  if ((man.loader?.archives ?? []).length > 0) {
    throw new Error(
      'delivery_mode is "external" but loader.archives is non-empty - nothing in the package provisions it',
    );
  }

  const sources = (man.files ?? []).map((f) => f.source).filter(Boolean);
  if (sources.length === 0) {
    throw new Error("manifest declares no files - nothing describes what ships");
  }
  const { missing, miscased } = resolveSources(sources, entryByLower);
  console.log(
    `OK   ${label}: ${path.basename(zip)} — external via ${ext.manager_name}, ${sources.length} file(s)`,
  );
  warnDescriptive(label, missing);
  warnMiscased(label, miscased);
}

// What a .cmd in the package hands off to. Both forms are real: the thin
// wrappers `call` a shared body staged by Copy-SharedBundle, and
// rv-there-yet's install.cmd dispatches to a sibling install.ps1 because it
// deploys to two game installs in one run.
function scriptDependencies(text) {
  const deps = new Set();
  for (const m of text.matchAll(/shared[\\/]((?:un)?install-body[a-z-]*\.cmd|find-game\.ps1)/gi)) {
    deps.add(`shared/${m[1]}`);
  }
  for (const m of text.matchAll(/%(?:SCRIPT_DIR%|~dp0)([A-Za-z0-9_.-]+\.ps1)/g)) deps.add(m[1]);
  return deps;
}

function resolveSources(sources, entryByLower) {
  const missing = [];
  const miscased = [];
  for (const source of sources) {
    const declared = source.replace(/\\/g, "/");
    const entry = entryByLower.get(declared.toLowerCase());
    if (entry === undefined) missing.push(declared);
    else if (entry !== declared) miscased.push(`${declared} (zip has "${entry}")`);
  }
  return { missing, miscased };
}

function warnMiscased(label, miscased) {
  if (miscased.length === 0) return;
  console.log(
    `WARN ${label}: manifest source(s) matched the zip only after ignoring case, fix the manifest casing: ${miscased.join(", ")}`,
  );
}

// Outside manifest mode the file list does not drive deployment, so a stale
// entry cannot break an install - but the launcher ingests it to describe the
// mod, and a path that names nothing describes nothing.
function warnDescriptive(label, missing) {
  if (missing.length === 0) return;
  console.log(
    `WARN ${label}: manifest describes file(s) the zip does not contain: ${missing.join(", ")}`,
  );
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

// Newest installer ZIP, or - for a mod that publishes none because a third
// party manager consumes the package directly - the newest release ZIP that is
// not the Nexus payload-only variant. Still returns null on an empty release
// directory, so a repo that never packaged is reported rather than skipped.
function newestInstaller(dir) {
  if (!fs.existsSync(dir)) return null;
  const byNewest = (a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs;
  const names = fs.readdirSync(dir).filter((n) => n.toLowerCase().endsWith(".zip"));
  const installers = names.filter((n) => n.endsWith("-installer.zip"));
  const pool = installers.length ? installers : names.filter((n) => !n.endsWith("-nexus.zip"));
  return pool.map((n) => path.join(dir, n)).sort(byNewest)[0] ?? null;
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
