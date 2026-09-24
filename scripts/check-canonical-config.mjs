#!/usr/bin/env node

// The canonical config lint (design 6.3), over the committed config files that
// data/config-format.json records for a repo. A file is linted once it carries the
// [CameraUnlock] stamp; before that it is a legacy file and there is nothing to hold it to.
//
//   node scripts/check-canonical-config.mjs                  # the repo vendoring this core
//   node scripts/check-canonical-config.mjs <repo> [...]     # repo paths or sibling names
//   node scripts/check-canonical-config.mjs --json <repo> [...]
//   node scripts/check-canonical-config.mjs --report         # pixi run config-report
//
// The default run prints each stamped file's problems and exits 1 when there is one.
// --json prints what scripts/conformance.ps1 decides its config checks from: where the repo
// stands in data/config-format.json, and each config file's state and problems. It always
// exits 0; the FAIL and WARN policy is conformance's.
// --report prints the fleet report: game-local (section, key) pairs shared by three or more
// canonical repos, the game-local section names in use, and concept values in committed
// files that differ from the schema default.

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
  CONFIG_FORMAT,
  FORMAT_KEY,
  STAMP_SECTION,
  equalsAsciiIgnoreCase,
  hasCanonicalStamp,
  parseCanonicalIni,
  splitLines,
  trimSpaceTab,
} from "./lib/canonical-ini.mjs";
import { formatKeyBindings, parseKeyBindings } from "./lib/key-bindings.mjs";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const REPOS_ROOT = path.dirname(CORE_ROOT);
const readJson = (rel) => JSON.parse(fs.readFileSync(path.join(CORE_ROOT, rel), "utf8"));
const SCHEMA = readJson("data/config-schema.json");
const FORMAT = readJson("data/config-format.json");

// Design 1.6 rule 3: the bare nouns of the schema's deliberately_unaliased list.
const BARE_NOUNS = ["Enabled", "Enable", "Amount", "Factor", "Scale", "Limit", "Multiplier", "Yaw", "Pitch", "Roll", "Position"];
const PASCAL_CASE = /^[A-Z][A-Za-z0-9]*$/;
const HOTKEYS_SECTION = "Hotkeys";

const normalise = (name) => name.toLowerCase().replace(/[_-]/g, "");

const unaliased = new Set(SCHEMA.deliberately_unaliased.flatMap((group) => group.spellings));
for (const noun of BARE_NOUNS) {
  if (!unaliased.has(noun)) throw new Error(`data/config-schema.json deliberately_unaliased no longer lists ${noun}`);
}
const bareNouns = new Set(BARE_NOUNS.map(normalise));

const conceptByName = new Map();
for (const concept of SCHEMA.concepts) {
  for (const name of [concept.key, ...concept.aliases]) conceptByName.set(normalise(name), concept);
}
const retiredByName = new Map();
for (const retired of SCHEMA.retired) {
  for (const name of retired.aliases) retiredByName.set(normalise(name), retired);
}
const deadSections = new Set(
  SCHEMA.sections.filter((s) => !SCHEMA.concepts.some((c) => c.canonical && c.section === s)).map((s) => s.toLowerCase()),
);
const schemaSection = (name) => SCHEMA.sections.find((s) => equalsAsciiIgnoreCase(s, name)) ?? null;

// The binding list a canonical hotkey concept must hold in this repo: its canonical_default,
// with any binding hotkey_exceptions replaces for the repo swapped for its replacement.
export function expectedHotkey(concept, exceptions) {
  const exception = exceptions?.[concept.key];
  const bindings = concept.canonical_default.split(", ");
  if (!exception) return bindings.join(", ");
  return bindings.map((b) => (b === exception.replaces ? exception.with : b)).join(", ");
}

const lineList = (lines) => lines.join(", ");
// "line 4", "lines 4, 7", or the first three and a count, so a file saved with the wrong line
// endings is one readable problem rather than a list of every line.
const someLines = (lines) =>
  lines.length === 1
    ? `line ${lines[0]}`
    : lines.length <= 5
      ? `lines ${lineList(lines)}`
      : `lines ${lineList(lines.slice(0, 3))} and ${lines.length - 3} more`;

function describeDiagnostic(d) {
  const at = `line ${lineList(d.lines)}`;
  switch (d.kind) {
    case "TextAfterSectionHeader": return `${at}: "${d.value}" follows [${d.section}]; a header line holds only the header`;
    case "UnclosedSectionHeader": return `${at}: "${d.value}" has no closing ]`;
    case "EmptySectionName": return `${at}: "${d.value}" names no section`;
    case "EmptyKey": return `${at}: "${d.value}" has no key before the =`;
    case "MissingEquals": return `${at}: "${d.value}" is neither a setting, a section header nor a comment`;
    case "KeyOutsideSection": return `${at}: ${d.key} is above every section header`;
    case "DuplicateKey": return `[${d.section}] ${d.key} is set on lines ${lineList(d.lines)}`;
    case "ConfigFormatMissing": return `${at}: [${d.section}] has no ${FORMAT_KEY}`;
    case "ConfigFormatInvalid": return `${at}: ${d.key}=${d.value} is not a format number`;
    case "ConfigFormatNewer": return `${at}: ${d.key}=${d.value} is newer than format ${CONFIG_FORMAT}, the one this core writes`;
    default: throw new Error(`no description for reader diagnostic ${d.kind}`);
  }
}

// The problems of one canonical file, as sentences. `dialect` is the repo's hotkey dialect,
// `exceptions` its hotkey_exceptions entry.
export function lintCanonicalConfig(bytes, { dialect, exceptions }) {
  const doc = parseCanonicalIni(bytes);
  if (doc.status === "Utf16") return ["is unreadable: it starts with a UTF-16 byte order mark"];
  if (doc.status === "NulByte") return [`is unreadable: line ${doc.unreadableLine} holds a NUL byte`];

  const problems = [];
  const lines = splitLines(bytes);
  if (bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
    problems.push("starts with a UTF-8 byte order mark; a canonical file is ASCII and the renderer writes none");
  }
  const nonAscii = lines.filter((l) => /[\x80-\xFF]/.test(l.text)).map((l) => l.number);
  if (nonAscii.length > 0) {
    problems.push(`${someLines(nonAscii)} ${nonAscii.length === 1 ? "holds" : "hold"} a byte above 0x7F; a canonical file is ASCII only`);
  }

  const notCrlf = lines.filter((l) => l.ending !== "\r\n").map((l) => l.number);
  if (notCrlf.length > 0) problems.push(`${someLines(notCrlf)} ${notCrlf.length === 1 ? "does" : "do"} not end in CRLF`);

  for (const d of doc.diagnostics) problems.push(describeDiagnostic(d));

  for (const { text, number } of lines) {
    const line = trimSpaceTab(text);
    if (line === "" || line[0] === ";" || line[0] === "#" || line[0] === "[") continue;
    const equals = line.indexOf("=");
    if (equals < 0) continue;
    if (text !== `${trimSpaceTab(line.slice(0, equals))}=${trimSpaceTab(line.slice(equals + 1))}`) {
      problems.push(`line ${number} is not written Key=value, with nothing around the key or the =`);
    }
  }

  const stamp = doc.sections.find((s) => equalsAsciiIgnoreCase(s.name, STAMP_SECTION)) ?? null;
  if (stamp === null) {
    problems.push(`has no [${STAMP_SECTION}] section`);
  } else {
    if (stamp.name !== STAMP_SECTION) problems.push(`line ${stamp.line}: [${stamp.name}] is spelled [${STAMP_SECTION}]`);
    for (const v of stamp.values) {
      if (!equalsAsciiIgnoreCase(v.key, FORMAT_KEY)) {
        problems.push(`line ${v.line}: [${STAMP_SECTION}] ${v.key} is not written by a mod; the section is core's and holds ${FORMAT_KEY} only`);
      } else if (v.key !== FORMAT_KEY) {
        problems.push(`line ${v.line}: ${v.key} is spelled ${FORMAT_KEY}`);
      } else if (v.value !== String(CONFIG_FORMAT) && doc.diagnostics.every((d) => !d.kind.startsWith("ConfigFormat"))) {
        problems.push(`line ${v.line}: ${FORMAT_KEY}=${v.value} is written ${FORMAT_KEY}=${CONFIG_FORMAT}`);
      }
    }
  }

  const keySections = new Map();
  for (const section of doc.sections) {
    if (section === stamp) {
      for (const v of section.values) keySections.set(v.key.toLowerCase(), section.name);
      continue;
    }
    const inSchema = schemaSection(section.name);
    if (inSchema !== null && inSchema !== section.name) {
      problems.push(`line ${section.line}: [${section.name}] is spelled [${inSchema}]`);
    } else if (inSchema === null && !PASCAL_CASE.test(section.name)) {
      problems.push(`line ${section.line}: [${section.name}] is not a PascalCase name of ASCII letters and digits`);
    }
    if (deadSections.has(section.name.toLowerCase())) {
      problems.push(`line ${section.line}: [${section.name}] holds no canonical setting, so a canonical file has no such section`);
    }

    for (const v of section.values) {
      const where = `line ${v.line}: [${section.name}] ${v.key}`;
      const norm = normalise(v.key);
      const concept = conceptByName.get(norm);
      if (concept) {
        if (!concept.canonical) {
          problems.push(`${where}: ${concept.canonical_reason}`);
          continue;
        }
        const target = `[${concept.section}] ${concept.key}`;
        if (norm !== normalise(concept.key)) {
          problems.push(`${where} is an alias of ${target}; a canonical file writes the concept's own key`);
          continue;
        }
        if (v.key !== concept.key) {
          problems.push(`${where} is spelled ${concept.key}`);
          continue;
        }
        if (!equalsAsciiIgnoreCase(section.name, concept.section)) {
          problems.push(`${where} belongs in [${concept.section}]`);
          continue;
        }
        if (concept.codec === "hotkey") {
          const parsed = parseKeyBindings(v.value, dialect);
          const expected = expectedHotkey(concept, exceptions);
          if (parsed.error) {
            problems.push(`${where}=${v.value} is not a ${dialect} key list: ${parsed.error}`);
          } else if (v.value !== expected) {
            problems.push(
              `${where}=${v.value} differs from the fleet's ${expected}; a game that binds one of those chords itself records the replacement in data/config-format.json hotkey_exceptions`,
            );
          }
        }
        continue;
      }

      const retired = retiredByName.get(norm);
      if (retired) {
        problems.push(`${where} is retired (data/config-schema.json retired ${retired.id}), so a canonical file has no row for it`);
        continue;
      }
      if (bareNouns.has(norm)) {
        problems.push(`${where}: a bare ${v.key} takes its meaning from the section; name what it applies to`);
      } else if (!PASCAL_CASE.test(v.key)) {
        problems.push(`${where} is not a PascalCase name of ASCII letters and digits`);
      }
      const other = keySections.get(v.key.toLowerCase());
      if (other !== undefined) {
        problems.push(`${where}: ${v.key} is also a key under [${other}], and a key name is used once in a file`);
      } else {
        keySections.set(v.key.toLowerCase(), section.name);
      }
      if (equalsAsciiIgnoreCase(section.name, HOTKEYS_SECTION)) {
        const parsed = parseKeyBindings(v.value, dialect);
        if (parsed.error) problems.push(`${where}=${v.value} is not a ${dialect} key list: ${parsed.error}`);
      }
    }
  }
  return problems;
}

function git(root, args) {
  const result = spawnSync("git", ["-C", root, ...args], { encoding: "utf8" });
  if (result.error) throw result.error;
  return result;
}

// Where a checkout stands in data/config-format.json: `repo` is its name there.
function listingOf(folder) {
  if (folder in FORMAT.exempt) return { repo: folder, listing: "exempt" };
  for (const [name, entry] of Object.entries(FORMAT.legacy)) {
    if (name === folder || entry.renamed_from === folder) return { repo: name, listing: "legacy" };
    if ((entry.predecessors ?? []).includes(folder)) return { repo: name, listing: "predecessor" };
  }
  if (folder in FORMAT.configs) return { repo: folder, listing: "mover" };
  return { repo: null, listing: "unlisted" };
}

function fileState(root, repo, file) {
  const state = {
    committed: file.committed,
    installed: file.installed,
    legacy_source: file.legacy_source,
    dialect: file.dialect,
    state: "unrecorded",
    problems: [],
  };
  if (file.committed === null) return state;
  const full = path.join(root, ...file.committed.split("/"));
  if (!fs.existsSync(full)) {
    state.state = "missing";
    return state;
  }
  const bytes = fs.readFileSync(full);
  if (!hasCanonicalStamp(bytes)) {
    state.state = "unstamped";
    return state;
  }
  state.state = "stamped";
  state.problems = lintCanonicalConfig(bytes, { dialect: file.dialect, exceptions: FORMAT.hotkey_exceptions[repo] });
  if (git(root, ["ls-files", "--error-unmatch", "--", file.committed]).status !== 0) {
    state.problems.push("is not tracked by git");
  }
  const attr = git(root, ["check-attr", "text", "--", file.committed]);
  if (attr.status !== 0) throw new Error(`git check-attr failed in ${root}: ${attr.stderr.trim()}`);
  const text = attr.stdout.trim().split(": ").pop();
  if (text !== "unset") {
    state.problems.push(`git check-attr text reports ${text}; .gitattributes marks the file -text so git never rewrites its bytes`);
  }
  return state;
}

export function repoState(root) {
  const folder = path.basename(root);
  const { repo, listing } = listingOf(folder);
  const result = { root, folder, repo, listing, converted: false, files: [], allow_legacy_symbols: [] };
  if (listing === "exempt") result.reason = FORMAT.exempt[repo];
  if (listing !== "legacy" && listing !== "mover") return result;
  result.files = FORMAT.configs[repo].map((file) => fileState(root, repo, file));
  result.converted = result.files.some((f) => f.state === "stamped");
  result.allow_legacy_symbols = FORMAT.allow_legacy_symbols[repo] ?? [];
  return result;
}

function resolveRoot(token) {
  for (const candidate of [token, path.join(REPOS_ROOT, token), path.join(REPOS_ROOT, `${token}-headtracking`)]) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) return path.resolve(candidate);
  }
  throw new Error(`no repo found for '${token}', as a path or under ${REPOS_ROOT}`);
}

function lintMain(roots) {
  let failed = false;
  for (const root of roots) {
    const s = repoState(root);
    if (s.listing === "exempt") {
      console.log(`${s.folder}: exempt, ${s.reason}`);
      continue;
    }
    if (s.listing === "predecessor") {
      console.log(`${s.folder}: an earlier repo of ${s.repo}, whose config it does not hold`);
      continue;
    }
    if (s.listing === "unlisted") {
      console.log(`FAIL ${s.folder}: not in data/config-format.json, so nothing records its config files`);
      failed = true;
      continue;
    }
    for (const f of s.files) {
      const name = `${s.folder}: ${f.committed ?? f.installed[0] ?? "(no path)"}`;
      if (f.state === "unrecorded") console.log(`${name}: data/config-format.json records no committed file yet`);
      else if (f.state === "unstamped") console.log(`${name}: no [${STAMP_SECTION}] stamp, so not a canonical file yet`);
      else if (f.state === "missing") {
        console.log(`FAIL ${name}: recorded in data/config-format.json, and not in the repo`);
        failed = true;
      } else if (f.problems.length === 0) {
        console.log(`ok   ${name}`);
      } else {
        failed = true;
        console.log(`FAIL ${name}`);
        for (const p of f.problems) console.log(`       ${p}`);
      }
    }
  }
  return failed ? 1 : 0;
}

function conceptDiffersFromDefault(concept, value) {
  if (concept.codec === "hotkey") return value !== concept.canonical_default;
  switch (concept.type) {
    case "bool": return value.toLowerCase() !== String(concept.default);
    case "int":
    case "float": return Number(value) !== concept.default;
    case "string": return value !== concept.default;
    default: throw new Error(`no default comparison for concept type ${concept.type}`);
  }
}

function reportMain() {
  const canonical = [];
  let withCheckout = 0;
  for (const [repo, files] of Object.entries(FORMAT.configs)) {
    const folder = [repo, FORMAT.legacy[repo]?.renamed_from].find((f) => f && fs.existsSync(path.join(REPOS_ROOT, f)));
    if (!folder) continue;
    withCheckout++;
    for (const file of files) {
      if (file.committed === null) continue;
      const full = path.join(REPOS_ROOT, folder, ...file.committed.split("/"));
      if (!fs.existsSync(full)) continue;
      const bytes = fs.readFileSync(full);
      if (hasCanonicalStamp(bytes)) canonical.push({ repo, committed: file.committed, doc: parseCanonicalIni(bytes) });
    }
  }

  const pairs = new Map();
  const sections = new Map();
  const differing = [];
  const note = (map, key, spelled, repo) => {
    if (!map.has(key)) map.set(key, { spelled, repos: new Set() });
    map.get(key).repos.add(repo);
  };
  for (const { repo, committed, doc } of canonical) {
    for (const section of doc.sections) {
      if (equalsAsciiIgnoreCase(section.name, STAMP_SECTION)) continue;
      if (schemaSection(section.name) === null) note(sections, section.name.toLowerCase(), section.name, repo);
      for (const v of section.values) {
        const concept = conceptByName.get(normalise(v.key));
        if (!concept) {
          note(pairs, `${section.name}\n${v.key}`.toLowerCase(), `[${section.name}] ${v.key}`, repo);
        } else if (concept.canonical && conceptDiffersFromDefault(concept, v.value)) {
          const def = concept.codec === "hotkey" ? concept.canonical_default : String(concept.default);
          differing.push(`${repo} ${committed}: [${section.name}] ${v.key}=${v.value} (schema default ${def})`);
        }
      }
    }
  }

  const byCount = (map) => [...map.values()].sort((a, b) => b.repos.size - a.repos.size || a.spelled.localeCompare(b.spelled));
  const list = (entries) => (entries.length === 0 ? ["  none"] : entries);
  const repoCount = new Set(canonical.map((c) => c.repo)).size;
  const out = [
    `${repoCount} canonical repos, of ${withCheckout} repos in data/config-format.json with a checkout under ${REPOS_ROOT}.`,
    "",
    "Game-local (section, key) pairs in three or more canonical repos, candidates for a concept (design 1.6):",
    ...list(byCount(pairs).filter((e) => e.repos.size >= 3).map((e) => `  ${e.spelled}  ${e.repos.size}: ${[...e.repos].sort().join(", ")}`)),
    "",
    "Game-local section names in use (design 1.6 rule 5):",
    ...list(byCount(sections).map((e) => `  [${e.spelled}]  ${e.repos.size}: ${[...e.repos].sort().join(", ")}`)),
    "",
    "Concept values in committed files that differ from the schema default (input to the end-of-project audit):",
    ...list(differing.map((d) => `  ${d}`)),
  ];
  console.log(out.join("\n"));
}

function main(argv) {
  const json = argv.includes("--json");
  const report = argv.includes("--report");
  const tokens = argv.filter((a) => a !== "--json" && a !== "--report");
  const unknown = tokens.find((t) => t.startsWith("--"));
  if (unknown) throw new Error(`unknown option ${unknown}`);
  if (report) {
    if (json || tokens.length > 0) throw new Error("--report takes no repos and no --json");
    reportMain();
    return 0;
  }
  const roots = tokens.length > 0 ? tokens.map(resolveRoot) : [REPOS_ROOT];
  if (json) {
    console.log(JSON.stringify(roots.map(repoState), null, 1));
    return 0;
  }
  return lintMain(roots);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = main(process.argv.slice(2));
}
