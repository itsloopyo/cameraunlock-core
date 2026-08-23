#!/usr/bin/env node
//
// Validate that a mod's THIRD-PARTY-NOTICES.md actually carries the licence
// text of every dependency compiled INTO its binary.
//
// This is the content half of the licence gate. Copy-LicenceNotices
// (ReleaseWorkflow.psm1) is the other half: it throws when a published ZIP
// ships without LICENSE / THIRD-PARTY-NOTICES.md at all. A ZIP can pass that
// and still carry a notices file that names a licence it never reproduces, or
// that has drifted from what upstream now says, which is what this catches.
//
// The check is per-LINE, not a whole-file diff: every substantive line of the
// reference licence must appear somewhere in the notices file. That enforces
// the legal substance while leaving layout alone, so a mod that splits the
// text into sections with its own prose between them still passes.
//
// One comparison covers both ways this goes wrong:
//
//   - Licence named but never reproduced. Every line is missing. MIT and
//     BSD-2-Clause require the notice, conditions and disclaimer to accompany
//     the binary; naming the licence satisfies none of them.
//   - A transitive holder dropped. legal/minhook.txt carries a second,
//     separate copyright for Hacker Disassembler Engine 32/64 (Vyacheslav
//     Patkov) below Tsuda Kageyu's, and hde32.c/hde64.c are compiled in. A
//     verbatim reproduction necessarily contains it; an attribution written
//     from memory of the headline project does not.
//
//   node scripts/validate-notices.mjs                 # this repo
//   node scripts/validate-notices.mjs <repo> [...]    # sibling repos
//
// The reference is the licence shipped with the version the mod actually
// compiles, i.e. the file in the dependency's own directory, falling back to
// legal/<slug>.txt when the source is not on disk. It has to be that way
// round: the obligation attaches to the version in the binary, and two mods
// can legitimately vendor different snapshots of the same component whose
// copyright years differ.
//
// Where both exist they are compared and any difference is reported as a
// warning, never a failure. A difference means one of two things and only a
// human can say which: our canonical copy has gone stale against a newer
// upstream, or a vendored tree has been altered and no longer matches what
// upstream ships. Failing on it would make the gate permanently red for any
// mod that legitimately vendors a newer snapshot.
//
// Dependencies come from the FetchContent_Declare blocks in CMakeLists.txt,
// plus any vendored source tree under extern/ | third_party/ | external/.
// A dependency with no reference text anywhere FAILS rather than being
// skipped: a check that passes when it cannot see the licence is worse than
// no check, because it reads as proof.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const CORE = path.resolve(SCRIPT_DIR, "..");
const LEGAL = path.join(CORE, "legal");
const ROOT = path.resolve(CORE, "..");

const SELF = Symbol("self");
const VENDORED_SOURCE_DIRS = ["extern", "third_party", "external"];
const LICENCE_NAMES = /^(LICENSE|LICENCE|COPYING|NOTICE)(\.(txt|md))?$/i;

// A dependency directory is named for the build, not for the component. Map
// what CMake and the vendored trees call it onto the legal/ slug.
const SLUG_ALIASES = {
  imgui: "dear-imgui",
  "imgui-docking": "dear-imgui",
  asiloader: "ultimate-asi-loader",
  "ultimate-asi-loader": "ultimate-asi-loader",
  doorstop: "unity-doorstop",
  cet: "cyber-engine-tweaks",
};

const tokens = process.argv.slice(2);
const jobs = tokens.length ? tokens : [SELF];

let failures = 0;
for (const token of jobs) {
  const repo = token === SELF ? ROOT : resolveRepo(token);
  const label = path.basename(repo);

  if (!fs.existsSync(repo)) {
    console.error(`FAIL ${label}: no such repo`);
    failures += 1;
    continue;
  }

  const { found, problems } = collect(repo);
  if (problems.length) {
    for (const p of problems) console.error(`FAIL ${label}: ${p}`);
    failures += 1;
    continue;
  }
  if (!found.length) {
    console.log(`ok   ${label}: nothing compiled in that carries a licence`);
    continue;
  }

  const noticesPath = path.join(repo, "THIRD-PARTY-NOTICES.md");
  if (!fs.existsSync(noticesPath)) {
    console.error(
      `FAIL ${label}: no THIRD-PARTY-NOTICES.md, but ${found.length} licensed ` +
        `dependency(ies) are compiled into the binary`,
    );
    failures += 1;
    continue;
  }
  const notices = normaliseLines(read(noticesPath));

  let repoFailed = false;
  for (const dep of found) {
    if (dep.canonical && dep.onDisk) {
      const canonicalLines = normaliseLines(read(dep.canonical));
      const drift = [...normaliseLines(read(dep.onDisk))].filter((l) => !canonicalLines.has(l));
      if (drift.length) {
        console.warn(
          `warn ${label}: this mod's ${dep.name} differs from legal/${dep.slug}.txt ` +
            `in ${drift.length} line(s). Either the canonical copy is stale against a ` +
            `newer upstream, or this tree was altered. Checking against the shipped copy.`,
        );
        for (const line of drift.slice(0, 3)) console.warn(`       here: ${line}`);
      }
    }

    // The version in the binary is the one whose licence has to travel.
    const reference = dep.onDisk ?? dep.canonical;
    const missing = [...normaliseLines(read(reference))].filter((l) => !notices.has(l));
    if (!missing.length) continue;

    repoFailed = true;
    const via = dep.onDisk
      ? path.relative(repo, dep.onDisk).replace(/\\/g, "/")
      : `legal/${dep.slug}.txt`;
    console.error(
      `FAIL ${label}: ${dep.name} is compiled in but ${missing.length} line(s) ` +
        `of ${via} are absent from THIRD-PARTY-NOTICES.md`,
    );
    for (const line of missing.slice(0, 4)) console.error(`       missing: ${line}`);
    if (missing.length > 4) console.error(`       ... and ${missing.length - 4} more`);
  }

  if (repoFailed) failures += 1;
  else console.log(`ok   ${label}: ${found.map((d) => d.name).join(", ")}`);
}

if (failures) {
  console.error(`\n${failures} repo(s) failed notices validation`);
  process.exit(1);
}

// A repo token may be bare (witcher-3) or full (witcher-3-headtracking).
function resolveRepo(token) {
  const direct = path.resolve(ROOT, token);
  return fs.existsSync(direct) ? direct : path.resolve(ROOT, `${token}-headtracking`);
}

// What the linker pulls in, and the two places its licence text can be read
// from: our canonical copy, and the dependency's own directory.
function collect(repo) {
  const found = [];
  const problems = [];

  const add = (name, onDisk) => {
    const slug = toSlug(name);
    const canonical = path.join(LEGAL, `${slug}.txt`);
    const hasCanonical = fs.existsSync(canonical);
    if (!hasCanonical && !onDisk) {
      problems.push(
        `"${name}" is compiled in but has no licence text to check against: ` +
          `no legal/${slug}.txt, and no licence file in its own directory`,
      );
      return;
    }
    found.push({ name, slug, canonical: hasCanonical ? canonical : null, onDisk });
  };

  const cml = path.join(repo, "CMakeLists.txt");
  if (fs.existsSync(cml)) {
    for (const m of read(cml).matchAll(/FetchContent_Declare\s*\(\s*([A-Za-z0-9_.-]+)([\s\S]*?)\)/g)) {
      if (!/GIT_REPOSITORY|URL\s/i.test(m[2])) continue;
      add(m[1], findLicence(path.join(repo, "build", "_deps", `${m[1]}-src`)));
    }
  }

  for (const parent of VENDORED_SOURCE_DIRS) {
    const base = path.join(repo, parent);
    if (!fs.existsSync(base)) continue;
    for (const entry of fs.readdirSync(base, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      const onDisk = findLicence(path.join(base, entry.name));
      if (onDisk || fs.existsSync(path.join(LEGAL, `${toSlug(entry.name)}.txt`))) {
        add(entry.name, onDisk);
      }
    }
  }

  return { found, problems };
}

function toSlug(name) {
  const key = name.replace(/[_-]?src$/i, "").toLowerCase();
  return SLUG_ALIASES[key] ?? key;
}

function findLicence(dir) {
  if (!fs.existsSync(dir)) return null;
  const hit = fs.readdirSync(dir).find((n) => LICENCE_NAMES.test(n));
  return hit ? path.join(dir, hit) : null;
}

function read(file) {
  return fs.readFileSync(file, "utf8").replace(/^﻿/, "").replace(/\r\n/g, "\n");
}

// Compare on substance, not formatting: collapse whitespace runs, drop blank
// lines and rule-off separators, so re-wrapping or re-sectioning a notice does
// not read as a missing notice.
function normaliseLines(text) {
  const out = new Set();
  for (const raw of text.split("\n")) {
    const line = raw.replace(/\s+/g, " ").trim();
    if (line && !/^[-=_*#`]{3,}$/.test(line)) out.add(line);
  }
  return out;
}
