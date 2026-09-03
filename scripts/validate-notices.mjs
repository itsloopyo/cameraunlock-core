#!/usr/bin/env node
//
// Validate that a mod's THIRD-PARTY-NOTICES.md actually carries the licence
// text of every dependency the mod ships: compiled into its binary, or
// redistributed alongside it inside the release ZIP.
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
// Dependencies come from four places:
//
//   - FetchContent_Declare blocks in CMakeLists.txt (native mods).
//   - Vendored source trees under extern/ | third_party/ | external/.
//   - vendor/<slug>/ directories holding a redistributed binary payload.
//   - cameraunlock-core/vendor/minhook, when CMakeLists.txt turns on
//     CAMERAUNLOCK_BUILD_HOOKS and supplies no minhook target of its own. The
//     core submodule is not a dependency directory of the mod, so nothing
//     above sees it, and the MinHook statically linked into every such .asi
//     was invisible to this check until it was looked for by name.
//
// The third is what covers the managed (C#) mods. They compile nothing from
// source but ship the loader they vendor - BepInEx, MelonLoader, REFramework,
// Ultimate ASI Loader, Mono.Cecil - inside the release ZIP, which is a binary
// distribution and carries the same obligation as linking. A vendor directory
// holding only a README (a pointer to an upstream the user fetches for
// themselves) redistributes nothing and is skipped.
//
// A loader archive is not one component. The BepInEx zips carry UnityDoorstop,
// HarmonyX, Mono.Cecil and MonoMod alongside BepInEx itself, so vendoring one
// pulls all five into scope - see legal/README.md. Ultimate ASI Loader's
// dinput8.dll statically links MinHook, injector and miniz, and the 32-bit
// build also MemoryModule and d3d8to9. Its two release assets unpack to the
// same filename, so those rules key on the `- Asset:` line update-deps writes
// into the vendor README rather than on the payload.
//
// One obligation a reproduced text cannot discharge: MPL-2.0 section 3.2
// requires whoever distributes Covered Software in Executable Form to tell
// recipients how to obtain its Source Code Form. For those components the
// notices must also name the upstream source, and that is checked too.
//
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
const VENDORED_BINARY_DIR = "vendor";
const LICENCE_NAMES = /^(LICENSE|LICENCE|COPYING|NOTICE)(\.(txt|md))?$/i;

// Extra rights holders a vendored loader distribution drags in beyond the one
// its directory is named for. `archive` matches a payload filename, because
// one vendor/<slug>/ can hold several distributions. `asset` matches the
// `- Asset:` line of the vendor README instead, for loaders whose assets all
// unpack to the same filename: Ultimate-ASI-Loader.zip (x86) and
// Ultimate-ASI-Loader_x64.zip both yield dinput8.dll, and only the README
// records which one was fetched. Component sets per premake5.lua at v9.7.2
// through v9.7.4: the x64 target compiles MinHook, injector and miniz; the
// Win32 target compiles those plus MemoryModule and d3d8to9.
const ARCHIVE_COMPONENTS = [
  { archive: /^BepInEx.*\.zip$/i, components: ["unity-doorstop", "harmonyx", "mono-cecil", "monomod"] },
  { asset: /^Ultimate-ASI-Loader_x64\.zip$/i, components: ["minhook", "injector", "miniz"] },
  { asset: /^Ultimate-ASI-Loader\.zip$/i, components: ["minhook", "injector", "miniz", "memorymodule", "d3d8to9"] },
];

// Vendor directories whose README must resolve to an `asset` rule above. An
// unknown asset here means the component set inside the payload is unknown,
// and passing on the loader's own licence alone would be the false proof the
// header warns about.
const ASSET_KEYED_VENDORS = new Set(["ultimate-asi-loader"]);

// Components whose licence obliges a source offer on top of the reproduced
// text (MPL-2.0 section 3.2). The notices must name a copy of the source WE
// hold, not only the upstream: the duty to make it available is the
// distributor's, and an upstream repository can be deleted or made private
// by someone else. The fork carries every upstream commit, so the pinned one
// is in it; never delete it while a release ZIP with the component is live.
const SOURCE_OFFERS = {
  memorymodule: "https://github.com/itsloopyo/MemoryModule",
};

// A dependency directory is named for the build, not for the component. Map
// what CMake and the vendored trees call it onto the legal/ slug.
const SLUG_ALIASES = {
  imgui: "dear-imgui",
  fabric: "fabric-loader",
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
    console.log(`ok   ${label}: nothing shipped that carries a licence`);
    continue;
  }

  const noticesPath = path.join(repo, "THIRD-PARTY-NOTICES.md");
  if (!fs.existsSync(noticesPath)) {
    console.error(
      `FAIL ${label}: no THIRD-PARTY-NOTICES.md, but ${found.length} licensed ` +
        `dependency(ies) ship in this mod's release ZIP`,
    );
    failures += 1;
    continue;
  }
  const notices = normaliseLines(read(noticesPath));

  let repoFailed = false;
  for (const dep of found) {
    if (dep.canonical && dep.onDisk) {
      const canonicalLines = normaliseLines(stripGnuHowToApply(read(dep.canonical)));
      const drift = [...normaliseLines(stripGnuHowToApply(read(dep.onDisk)))].filter(
        (l) => !canonicalLines.has(l),
      );
      if (drift.length) {
        console.warn(
          `warn ${label}: this mod's ${dep.name} differs from legal/${dep.slug}.txt ` +
            `in ${drift.length} line(s). Either the canonical copy is stale against a ` +
            `newer upstream, or this tree was altered. Checking against the shipped copy.`,
        );
        for (const line of drift.slice(0, 3)) console.warn(`       here: ${line}`);
      }
    }

    const how = dep.carriedBy ? `is redistributed inside ${dep.carriedBy}, but` : "is shipped, but";

    const sourceOffer = SOURCE_OFFERS[dep.slug];
    if (sourceOffer && !read(noticesPath).includes(sourceOffer)) {
      repoFailed = true;
      console.error(
        `FAIL ${label}: ${dep.name} ${how} THIRD-PARTY-NOTICES.md never says where its ` +
          `Source Code Form can be obtained. MPL-2.0 section 3.2 requires that of every ` +
          `binary distribution; name our copy ${sourceOffer} at the commit inside the binary.`,
      );
    }

    // The version in the binary is the one whose licence has to travel.
    const reference = dep.onDisk ?? dep.canonical;
    const missing = [...normaliseLines(stripGnuHowToApply(read(reference)))].filter(
      (l) => !notices.has(l),
    );
    if (!missing.length) continue;

    repoFailed = true;
    const via = dep.onDisk
      ? path.relative(repo, dep.onDisk).replace(/\\/g, "/")
      : `legal/${dep.slug}.txt`;
    console.error(
      `FAIL ${label}: ${dep.name} ${how} ${missing.length} line(s) ` +
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

  const seen = new Set();
  // `carriedBy` names the archive a component rides in, so a failure says which
  // vendored zip pulled in a rights holder the notices never mention.
  const add = (name, onDisk, carriedBy) => {
    const slug = toSlug(name);
    if (seen.has(slug)) return;
    const canonical = path.join(LEGAL, `${slug}.txt`);
    const hasCanonical = fs.existsSync(canonical);
    if (!hasCanonical && !onDisk) {
      problems.push(
        `"${name}" is shipped but has no licence text to check against: ` +
          `no legal/${slug}.txt, and no licence file in its own directory`,
      );
      return;
    }
    seen.add(slug);
    found.push({ name, slug, canonical: hasCanonical ? canonical : null, onDisk, carriedBy });
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

  // cameraunlock-core/cpp builds its vendored MinHook into cameraunlock_hooks
  // whenever the consumer turns the option on without supplying a `minhook`
  // target. A mod that does supply one was found above and add() keeps that.
  const coreMinhook = path.join(repo, "cameraunlock-core", "vendor", "minhook");
  if (fs.existsSync(cml) && fs.existsSync(coreMinhook)) {
    const text = read(cml);
    if (/CAMERAUNLOCK_BUILD_HOOKS\s+ON\b/.test(text) || /cameraunlock-core[\/\\]vendor[\/\\]minhook/.test(text)) {
      add("minhook", findLicence(coreMinhook));
    }
  }

  const vendorBase = path.join(repo, VENDORED_BINARY_DIR);
  if (fs.existsSync(vendorBase)) {
    for (const entry of fs.readdirSync(vendorBase, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      const dir = path.join(vendorBase, entry.name);
      const payload = fs.readdirSync(dir).filter((n) => !LICENCE_NAMES.test(n) && !/\.md$/i.test(n));
      if (!payload.length) continue;
      add(entry.name, findLicence(dir));
      for (const file of payload) {
        for (const rule of ARCHIVE_COMPONENTS) {
          if (!rule.archive?.test(file)) continue;
          const carriedBy = `${VENDORED_BINARY_DIR}/${entry.name}/${file}`;
          for (const component of rule.components) add(component, null, carriedBy);
        }
      }

      const asset = vendoredAsset(dir);
      const rule = asset && ARCHIVE_COMPONENTS.find((r) => r.asset?.test(asset));
      if (rule) {
        const carriedBy = `${VENDORED_BINARY_DIR}/${entry.name}/ (asset ${asset})`;
        for (const component of rule.components) add(component, null, carriedBy);
      } else if (ASSET_KEYED_VENDORS.has(toSlug(entry.name))) {
        problems.push(
          asset
            ? `${VENDORED_BINARY_DIR}/${entry.name}/README.md records asset "${asset}", which no ` +
                `ARCHIVE_COMPONENTS rule covers, so the components inside the payload are unknown`
            : `${VENDORED_BINARY_DIR}/${entry.name}/README.md has no "- Asset:" line, so which ` +
                `upstream archive the payload came from, and what is compiled into it, is unknown`,
        );
      }
    }
  }

  return { found, problems };
}

// The `- Asset: \`<name>\`` line Update-VendoredLoader writes into the vendor
// README, or null when there is no README or it has no such line.
function vendoredAsset(dir) {
  const readme = path.join(dir, "README.md");
  if (!fs.existsSync(readme)) return null;
  const m = read(readme).match(/^- Asset:\s*`?([^`\r\n]+?)`?\s*$/m);
  return m ? m[1] : null;
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

// The GNU licences close with a "How to Apply These Terms" appendix - worked
// examples for someone licensing their OWN new work. It sits after "END OF
// TERMS AND CONDITIONS", so it binds nobody who merely redistributes, and
// legal/README.md already records that a notices file may carry the LGPL body
// once and name both BepInEx and UnityDoorstop against it. Requiring it would
// fail every correct notices file on boilerplate. Anything else a project
// appends after the terms (BepInEx's own NOTICE) is attribution and stays in
// scope.
function stripGnuHowToApply(text) {
  const cut = text.search(/^\s*How to Apply These Terms to Your New (Libraries|Programs)\s*$/m);
  return cut === -1 ? text : text.slice(0, cut);
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
