#!/usr/bin/env node

// The canonical config lint (design 6.3), over the committed config files that
// data/config-format.json records for a repo. A file is linted once it carries the
// [CameraUnlock] stamp; before that it is a legacy file and there is nothing to hold it to.
//
//   node scripts/check-canonical-config.mjs                  # the repo vendoring this core
//   node scripts/check-canonical-config.mjs <repo> [...]     # repo paths or sibling names
//   node scripts/check-canonical-config.mjs --json <repo> [...]
//   node scripts/check-canonical-config.mjs --json --roots-file <file>   # repo paths, one per line
//   node scripts/check-canonical-config.mjs --report         # pixi run config-report
//
// The default run prints each stamped file's problems and exits 1 when there is one.
// --json prints what scripts/conformance.ps1 decides its config checks from: where the repo
// stands in data/config-format.json, and each config file's state and problems. It always
// exits 0; the FAIL and WARN policy is conformance's.
// --report prints the fleet report: game-local (section, key) pairs shared by three or more
// canonical repos, the game-local section names in use, and each repo's per_game rows.

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  CONFIG_FORMAT,
  DEFAULT_TOKEN,
  FORMAT_KEY,
  STAMP_SECTION,
  equalsAsciiIgnoreCase,
  findSection,
  findValue,
  hasCanonicalStamp,
  isDefaultToken,
  parseCanonicalIni,
  splitLines,
  trimSpaceTab,
} from "./lib/canonical-ini.mjs";
import { parseKeyBindings } from "./lib/key-bindings.mjs";

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
const canonicalByKey = new Map(SCHEMA.concepts.filter((c) => c.canonical).map((c) => [c.key, c]));
// The renderer comments out an Engine row that holds its default as "; Key=value".
const COMMENTED_ROW = /^;[ \t]*([A-Za-z0-9]+)[ \t]*=/;
const nonCanonicalKeyByName = new Map();
const nonCanonicalSectionByName = new Map();
for (const group of SCHEMA.non_canonical_keys) {
  for (const name of group.spellings) nonCanonicalKeyByName.set(normalise(name), group);
  for (const section of group.sections) nonCanonicalSectionByName.set(section.toLowerCase(), group);
}
const retiredByName = new Map();
for (const retired of SCHEMA.retired) {
  for (const name of retired.aliases) retiredByName.set(normalise(name), retired);
}
const deadSections = new Set([
  ...SCHEMA.sections.filter((s) => !SCHEMA.concepts.some((c) => c.canonical && c.section === s)).map((s) => s.toLowerCase()),
  ...nonCanonicalSectionByName.keys(),
]);
const schemaSection = (name) => SCHEMA.sections.find((s) => equalsAsciiIgnoreCase(s, name)) ?? null;

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

// The problems of one committed canonical file, as sentences. `dialect` is the repo's hotkey
// dialect, `perGame` the concept ids data/config-format.json per_game lists for the repo: those
// rows hold the game's own value, and every other concept row holds default.
export function lintCanonicalConfig(bytes, { dialect, perGame }) {
  if (!Array.isArray(perGame)) throw new Error("lintCanonicalConfig needs perGame, the repo's per_game concept ids");
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

  const headerLines = new Map();
  let current = null;
  for (const { text, number } of lines) {
    const line = trimSpaceTab(text);
    if (line[0] === ";") {
      const commented = COMMENTED_ROW.exec(line);
      const concept = commented === null ? undefined : canonicalByKey.get(commented[1]);
      if (concept !== undefined && current !== null && equalsAsciiIgnoreCase(current, concept.section) && !perGame.includes(concept.id)) {
        problems.push(
          `line ${number}: [${current}] ${concept.key} is commented out, the form render-config gives an Engine row marked PerGame() at its default, and data/config-format.json per_game does not list ${concept.id} for this repo; a committed file writes ${concept.key}=${DEFAULT_TOKEN} there`,
        );
      }
      continue;
    }
    if (line === "" || line[0] === "#") continue;
    if (line[0] === "[") {
      const close = line.indexOf("]");
      const name = close < 0 ? "" : trimSpaceTab(line.slice(1, close));
      if (name === "" || close !== line.length - 1) continue;
      current = name;
      if (text !== `[${name}]`) {
        problems.push(`line ${number} is not written [${name}], with nothing around the name or the brackets`);
      }
      const first = headerLines.get(name.toLowerCase());
      if (first !== undefined) {
        problems.push(`line ${number}: [${name}] repeats the section begun on line ${first}; a canonical file writes each section once`);
      } else {
        headerLines.set(name.toLowerCase(), number);
      }
      continue;
    }
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
  const valued = [];
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
        const kept = perGame.includes(concept.id);
        if (isDefaultToken(v.value)) {
          if (kept) {
            problems.push(
              `${where}=${v.value}: data/config-format.json per_game lists ${concept.id} for this repo, so its table marks the row PerGame() and the file holds the game's own value there`,
            );
          }
          continue;
        }
        if (!kept) {
          valued.push(v);
          continue;
        }
        if (concept.codec === "hotkey") {
          const parsed = parseKeyBindings(v.value, dialect);
          if (parsed.error) problems.push(`${where}=${v.value} is not a ${dialect} key list: ${parsed.error}`);
        }
        continue;
      }

      const retired = retiredByName.get(norm);
      if (retired) {
        problems.push(`${where} is retired (data/config-schema.json retired ${retired.id}), so a canonical file has no row for it`);
        continue;
      }
      const nonCanonical = nonCanonicalKeyByName.get(norm) ?? nonCanonicalSectionByName.get(section.name.toLowerCase());
      if (nonCanonical) {
        problems.push(`${where}: ${nonCanonical.canonical_reason}`);
        continue;
      }
      if (norm.startsWith("chord")) {
        problems.push(`${where}: a chord is an item of its action's key list (ToggleKey=End, Ctrl+Shift+Y), not a key of its own`);
      } else if (bareNouns.has(norm)) {
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
  if (valued.length > 0) {
    valued.sort((a, b) => a.line - b.line);
    const keys = valued.map((v) => v.key).join(", ");
    const lines = someLines(valued.map((v) => v.line));
    problems.push(
      valued.length === 1
        ? `${lines}: ${keys} holds a value, and data/config-format.json per_game does not list it for this repo; a committed file holds ${DEFAULT_TOKEN} on every concept row but the ones per_game lists, as render-config writes it`
        : `${lines}: ${keys} hold values, and data/config-format.json per_game lists none of them for this repo; a committed file holds ${DEFAULT_TOKEN} on every concept row but the ones per_game lists, as render-config writes it`,
    );
  }
  return problems;
}

function git(root, args) {
  const result = spawnSync("git", ["-C", root, ...args], { encoding: "utf8" });
  if (result.error) throw result.error;
  return result;
}

const lowerKeys = (object) => new Map(Object.keys(object).map((name) => [name.toLowerCase(), name]));
const EXEMPT = lowerKeys(FORMAT.exempt);
const CONFIGS = lowerKeys(FORMAT.configs);

// Where a repo name stands in data/config-format.json: `repo` is its name there. GitHub names
// are case-insensitive, so the match is too.
function listingOf(name) {
  const lower = name.toLowerCase();
  if (EXEMPT.has(lower)) return { repo: EXEMPT.get(lower), listing: "exempt" };
  for (const [repo, entry] of Object.entries(FORMAT.legacy)) {
    if (repo.toLowerCase() === lower || entry.renamed_from?.toLowerCase() === lower) return { repo, listing: "legacy" };
    if ((entry.predecessors ?? []).some((p) => p.toLowerCase() === lower)) return { repo, listing: "predecessor" };
  }
  if (CONFIGS.has(lower)) return { repo: CONFIGS.get(lower), listing: "mover" };
  return { repo: null, listing: "unlisted" };
}

// A worktree or a clone under another folder name is still the repo its origin names.
function listingOfCheckout(root, folder) {
  const byFolder = listingOf(folder);
  if (byFolder.listing !== "unlisted") return byFolder;
  const origin = git(root, ["remote", "get-url", "origin"]);
  if (origin.status !== 0) return byFolder;
  const name = origin.stdout.trim().split(/[\\/:]/).filter(Boolean).pop();
  return name === undefined ? byFolder : listingOf(name.replace(/\.git$/i, ""));
}

// Tracked config files carrying the stamp, in a repo whose data/config-format.json entry
// records no committed path for one of its files: the conversion committed a canonical file
// and did not record it in core.
function unrecordedStampedFiles(root) {
  const listed = git(root, ["-c", "core.quotepath=off", "ls-files", "-z"]);
  if (listed.status !== 0) throw new Error(`git ls-files failed in ${root}: ${listed.stderr.trim()}`);
  return listed.stdout
    .split("\0")
    .filter((rel) => /\.(ini|cfg)$/i.test(rel))
    .filter((rel) => {
      const full = path.join(root, ...rel.split("/"));
      return fs.existsSync(full) && hasCanonicalStamp(fs.readFileSync(full));
    });
}

function fileState(root, repo, file) {
  const state = {
    committed: file.committed,
    installed: file.installed,
    legacy_source: file.legacy_source,
    dialect: file.dialect,
    no_installed_reason: file.no_installed_reason ?? null,
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
  state.problems = lintCanonicalConfig(bytes, { dialect: file.dialect, perGame: (FORMAT.per_game[repo] ?? []).map((e) => e.row) });
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
  const { repo, listing } = listingOfCheckout(root, folder);
  const result = { root, folder, repo, listing, converted: false, files: [], unrecorded_stamped: [], allow_legacy_symbols: [] };
  if (listing === "exempt") result.reason = FORMAT.exempt[repo];
  if (listing !== "legacy" && listing !== "mover") return result;
  result.files = FORMAT.configs[repo].map((file) => fileState(root, repo, file));
  if (result.files.some((f) => f.state === "unrecorded")) result.unrecorded_stamped = unrecordedStampedFiles(root);
  result.converted = result.files.some((f) => f.state === "stamped") || result.unrecorded_stamped.length > 0;
  result.allow_legacy_symbols = FORMAT.allow_legacy_symbols[repo] ?? [];
  return result;
}

// The entries of a manual (Nexus) ZIP that land on one of the repo's config files when the ZIP is
// extracted over the game folder: an installed CameraUnlock.ini or, where the entry records one,
// the legacy file in the same folder, at its path or at a tail of it (a flat ZIP meant for the
// exe folder). Compared with / separators and without case.
export function manualZipConfigEntries(state, entries) {
  const files = state.files.flatMap((f) =>
    f.installed.flatMap((at) => {
      const p = at.replace(/\\/g, "/");
      const folder = p.slice(0, p.lastIndexOf("/") + 1);
      return f.legacy_source === null ? [p] : [p, folder + f.legacy_source];
    }),
  ).map((p) => p.toLowerCase());
  return entries
    .map((e) => e.replace(/\\/g, "/"))
    .filter((e) => !e.endsWith("/"))
    .filter((e) => files.some((p) => p === e.toLowerCase() || p.endsWith(`/${e.toLowerCase()}`)));
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
    for (const rel of s.unrecorded_stamped) {
      console.log(`FAIL ${s.folder}: ${rel} carries the [${STAMP_SECTION}] stamp, and data/config-format.json records no committed file for it`);
      failed = true;
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
  const note = (map, key, spelled, repo) => {
    if (!map.has(key)) map.set(key, { spelled, repos: new Set() });
    map.get(key).repos.add(repo);
  };
  for (const { repo, doc } of canonical) {
    for (const section of doc.sections) {
      if (equalsAsciiIgnoreCase(section.name, STAMP_SECTION)) continue;
      if (schemaSection(section.name) === null) note(sections, section.name.toLowerCase(), section.name, repo);
      for (const v of section.values) {
        if (!conceptByName.has(normalise(v.key))) {
          note(pairs, `${section.name}\n${v.key}`.toLowerCase(), `[${section.name}] ${v.key}`, repo);
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
    "Rows data/config-format.json per_game lets a game keep for itself, with the value each canonical committed file holds:",
    ...list(perGameRows(canonical)),
  ];
  console.log(out.join("\n"));
}

function perGameRows(canonical) {
  return Object.entries(FORMAT.per_game).flatMap(([repo, entries]) =>
    entries.map((entry) => {
      const concept = SCHEMA.concepts.find((c) => c.id === entry.row);
      const values = canonical
        .filter((c) => c.repo === repo)
        .map(({ committed, doc }) => {
          const section = findSection(doc, concept.section);
          const line = section === null ? null : findValue(section, concept.key);
          return `${committed} ${line === null ? "has no line" : `${concept.key}=${line.value}`}`;
        });
      const held = values.length === 0 ? "no canonical committed file" : values.join("; ");
      return `  ${repo}: ${entry.row}, approved ${entry.approved} (${held}): ${entry.reason}`;
    }),
  );
}

function main(argv) {
  const rootsFileAt = argv.indexOf("--roots-file");
  let rootsFile = null;
  if (rootsFileAt >= 0) {
    rootsFile = argv[rootsFileAt + 1];
    if (rootsFile === undefined) throw new Error("--roots-file takes a file of repo paths, one per line");
    argv = argv.filter((_, i) => i !== rootsFileAt && i !== rootsFileAt + 1);
  }
  const json = argv.includes("--json");
  const report = argv.includes("--report");
  let tokens = argv.filter((a) => a !== "--json" && a !== "--report");
  const unknown = tokens.find((t) => t.startsWith("--"));
  if (unknown) throw new Error(`unknown option ${unknown}`);
  if (report) {
    if (json || tokens.length > 0 || rootsFile !== null) throw new Error("--report takes no repos and no --json");
    reportMain();
    return 0;
  }
  if (rootsFile !== null) {
    tokens = [...tokens, ...fs.readFileSync(rootsFile, "utf8").split(/\r?\n/).filter((line) => line !== "")];
  }
  const roots = tokens.length > 0 ? tokens.map(resolveRoot) : [REPOS_ROOT];
  if (json) {
    console.log(JSON.stringify(roots.map(repoState), null, 1));
    return 0;
  }
  return lintMain(roots);
}

if (fs.realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exitCode = main(process.argv.slice(2));
}
