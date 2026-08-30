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
//                And the converse, which is the half that used to be silent: a
//                payload file that is IN the ZIP but on neither list is never
//                deployed. Add a DLL to packaging staging, forget the manifest
//                row, and the package installs an incomplete mod while passing
//                every gate, because everything declared is still present. See
//                undeclaredPayload() for what does not count as payload.
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

  assertVersionMatchesZipName(man, zip);

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

  // The converse, which is the half that used to be silent. Everything the
  // engine deploys comes off loader.archives[] and files[]; a payload file that
  // is in the ZIP but on neither list is simply never written to the game. That
  // ships an installer which succeeds and leaves the mod incomplete, and the
  // forward check above passes it, because everything declared is still there.
  const undeclared = undeclaredPayload(entries, sources, man);
  if (undeclared.fatal.length > 0) {
    throw new Error(
      `zip carries binaries no manifest row deploys, so a launcher install would leave them out and the mod would not run: ${undeclared.fatal.join(", ")}. Add a files[] row for each, or drop it from packaging staging.`,
    );
  }

  const seeds = (man.loader?.seed ?? []).length;
  const rt = (man.runtime_requirements ?? []).length;
  console.log(
    `OK   ${label}: ${path.basename(zip)} — manifest, ${sources.length} file(s), ${seeds} seed(s), ${rt} runtime req(s)`,
  );
  warnMiscased(label, miscased);
  warnUndeployed(label, undeclared.cosmetic);
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
  // Comments first. A .cmd that explains why it does NOT use the shared bundle names
  // the path it is not using, and scanning the raw text read that as a dependency and
  // failed a package that was correct - minecraft-java-edition, whose install.cmd says
  // it resolves %APPDATA%\.minecraft directly rather than through find-game.ps1.
  text = text
    .split(/\r?\n/)
    .filter((line) => !/^\s*(::|@?rem\b)/i.test(line))
    .join("\n");
  for (const m of text.matchAll(/shared[\\/]((?:un)?install-body[a-z-]*\.cmd|find-game\.ps1)/gi)) {
    deps.add(`shared/${m[1]}`);
  }
  for (const m of text.matchAll(/%(?:SCRIPT_DIR%|~dp0)([A-Za-z0-9_.-]+\.ps1)/g)) deps.add(m[1]);
  return deps;
}

// Which ZIP entries the engine would never deploy from files[], and so are not
// evidence of a forgotten manifest row. Each of these earns its place:
//
//   the manifest itself         read by the engine, not written to the game
//   docs and licence texts      README/CHANGELOG/LICENSE/notices, and the whole
//                               licenses/ tree, ship to discharge the licences
//                               of what is compiled in; none is deployed
//   install/uninstall scripts   the non-manifest delivery path, plus everything
//                               they reach for - the shared/ bundle
//                               (find-game.ps1, games.json, the
//                               install-body-*.cmd files) and any scripts/ tree
//                               a repo stages beside them
//   vendored loader provenance  the LICENSE/README/nupkg that accompany
//                               vendor/<loader>/. The loader payload itself is
//                               NOT exempt: it is deployed, via
//                               loader.archives[] or a files[] row, so an
//                               undeclared binary or archive under vendor/ is
//                               the same bug as anywhere else
//   a seed target               loader.seed writes the file from content_b64,
//                               so the copy in the ZIP is a reference the
//                               engine does not need a files[] row for
//
// Declared inside the function, not at module scope: this file runs its job
// loop at the top and declares its helpers below, so a top-level `const` here
// is still in the temporal dead zone when the first package is validated.
function undeclaredPayload(entries, sources, man) {
  // profile/ is the launcher's own staging convention rather than a files[] target:
  // its AsiLoader strategy reads `<profile>/asi/*` out of the package, so a DLL mirrored
  // there is deployed by the launcher without a manifest row naming it. See
  // bioshock-remastered-headtracking/scripts/package.ps1, which stages it deliberately.
  const exemptDirs = /^(shared|scripts|licenses|profile)\//i;
  const exemptDocs =
    /^(readme|changelog|licence|license|notice|third[-_]party[-_a-z]*)\.(md|txt)$|^(licence|license|notice)$/i;
  const installScripts = /^(un)?install[a-z0-9._-]*\.(cmd|ps1|bat|sh)$/i;
  const vendorProvenance = /^vendor\/[^/]+\/(licence|license|notice|readme)[^/]*$|\.nupkg$/i;

  const declared = new Set(sources.map((s) => s.replace(/\\/g, "/").toLowerCase()));
  // Seed targets name the path in the GAME directory, not in the ZIP, so the
  // two only ever agree on the leaf.
  const seeded = new Set(
    (man.loader?.seed ?? [])
      .map((s) => s?.target)
      .filter(Boolean)
      .map((t) => t.split(/[\\/]/).pop().toLowerCase()),
  );

  // The split is by what an undeclared entry costs. A binary the mod needs in
  // order to run is inert to the engine, so a manifest install produces a mod
  // that does not work - fatal. Anything else undeclared is a file the launcher
  // path silently does without: usually the commented default config that
  // install.cmd copies and the mod regenerates for itself when absent
  // (Config::LoadOrCreateDefault and friends), so a launcher user gets the
  // generated one instead of the documented one. Worth saying, not worth
  // refusing a package that installs and runs.
  const payloadExt = /\.(dll|asi|exe|so|dylib|jar|pyd|node|zip|7z|rar|tar|gz)$/i;

  const fatal = [];
  const cosmetic = [];
  for (const entry of entries) {
    if (entry.endsWith("/")) continue;
    if (declared.has(entry.toLowerCase())) continue;
    const base = entry.split("/").pop();
    if (entry.toLowerCase() === "launcher-manifest.json") continue;
    if (exemptDirs.test(entry)) continue;
    if (exemptDocs.test(base)) continue;
    if (installScripts.test(base)) continue;
    if (vendorProvenance.test(entry)) continue;
    if (seeded.has(base.toLowerCase())) continue;
    (payloadExt.test(base) ? fatal : cosmetic).push(entry);
  }
  return { fatal, cosmetic };
}

// mod_info.version lands in the launcher's install receipt and drives version
// badging, and the ZIP filename is what a user downloads and what the release
// is tagged from. Nothing else compares the two, so a packager that stamps one
// and not the other ships a package the launcher reports under the wrong
// version. Only asserted when the name actually carries a version: the nightly
// ZIPs are named `-dev-` on purpose.
function assertVersionMatchesZipName(man, zip) {
  const named = /-v(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)-/.exec(path.basename(zip));
  if (!named) return;
  const declared = man.mod_info?.version;
  if (!declared) {
    throw new Error(
      `zip is named for version ${named[1]} but the manifest has no mod_info.version, so the launcher receipt would record none`,
    );
  }
  if (declared !== named[1]) {
    throw new Error(
      `mod_info.version is ${declared} but the zip is named for ${named[1]}; the launcher records the manifest value, so the receipt and the download would disagree`,
    );
  }
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

// A non-binary file in the ZIP that no manifest row deploys. install.cmd copies
// it and the launcher does not, so the two delivery paths leave the game in
// different states. For a config the mod regenerates that is cosmetic; the fix
// where it matters is a loader.seed entry, which writes the file once and does
// not clobber a config the user has since edited.
function warnUndeployed(label, entries) {
  if (entries.length === 0) return;
  console.log(
    `WARN ${label}: zip carries file(s) no manifest row deploys, so a launcher install does without them where install.cmd copies them: ${entries.join(", ")}`,
  );
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
