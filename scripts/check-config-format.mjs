#!/usr/bin/env node

// Shape gate for data/config-format.json, and the pin that freezes its `legacy` set.
//
// `legacy` names the repos that published a pre-canonical build. Only those carry a
// frozen legacy import, and conformance fails a repo outside the set that has one, so a
// name added later would let a new repo ship a second config dialect. LEGACY_PIN is the
// SHA-256 of the sorted names joined by "\n": adding, removing or renaming one fails here.
//
//   node scripts/check-config-format.mjs
//
// Reads data/config-format.json, and data/config-schema.json and data/keys.json to check
// hotkey_exceptions against the hotkey concepts, so it is safe in a clean CI checkout.

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { formatKeyBindings, parseKeyBindings } from "./lib/key-bindings.mjs";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const FORMAT_PATH = path.join(REPO_ROOT, "data", "config-format.json");
const SCHEMA = JSON.parse(fs.readFileSync(path.join(REPO_ROOT, "data", "config-schema.json"), "utf8"));

const SCHEMA_VERSION = 1;
const LEGACY_COUNT = 87;
const LEGACY_PIN = "ba0932608d431edffb0f721d5d35b0309de0d28624dcabe864db593897d37c2b";

const TOP_LEVEL_KEYS = [
  "schema_version",
  "_comment",
  "legacy",
  "exempt",
  "configs",
  "normalisations",
  "approved_changes",
  "hotkey_exceptions",
  "allow_legacy_symbols",
];
const DIALECTS = new Set(["native", "unity"]);
const REPO_NAME = /^[a-z0-9]+(-[a-z0-9]+)*$/;
const DATE = /^\d{4}-\d{2}-\d{2}$/;
const BAD_PATH_CHARS = /[<>:"|?*\x00-\x1f]/;

const problems = [];
const fail = (msg) => problems.push(msg);

const raw = fs.readFileSync(FORMAT_PATH, "utf8");
let doc;
try {
  doc = JSON.parse(raw);
} catch (err) {
  console.error(`${FORMAT_PATH} is not valid JSON: ${err.message}`);
  process.exit(1);
}

// JSON.parse keeps the last of a repeated key and says nothing, so a repo listed twice
// under one object would silently lose its first entry. Only a scan of the text sees it.
for (const dup of duplicateKeys(raw)) fail(dup);

const isObject = (v) => typeof v === "object" && v !== null && !Array.isArray(v);
const isText = (v) => typeof v === "string" && v.trim() !== "";

checkKeys("the top level", doc, TOP_LEVEL_KEYS, []);
if (doc.schema_version !== SCHEMA_VERSION) {
  fail(`schema_version is ${JSON.stringify(doc.schema_version)} but this validator implements v${SCHEMA_VERSION}`);
}
if (!Array.isArray(doc._comment) || !doc._comment.every((l) => typeof l === "string")) {
  fail("_comment must be an array of strings");
}
for (const key of TOP_LEVEL_KEYS.slice(2)) {
  if (!isObject(doc[key])) {
    console.error(`data/config-format.json: \`${key}\` must be an object`);
    process.exit(1);
  }
}

const { legacy, exempt, configs } = doc;
const claimed = new Map();
const claim = (name, where) => {
  if (typeof name !== "string" || !REPO_NAME.test(name)) {
    fail(`${where}: ${JSON.stringify(name)} is not a lowercase hyphenated repo name`);
    return;
  }
  const first = claimed.get(name);
  if (first) fail(`${where}: ${name} is already named by ${first}`);
  else claimed.set(name, where);
};

for (const [name, entry] of Object.entries(legacy)) {
  claim(name, `legacy.${name}`);
  if (!isObject(entry)) {
    fail(`legacy.${name} must be an object`);
    continue;
  }
  checkKeys(`legacy.${name}`, entry, [], ["predecessors", "renamed_from"]);
  if ("predecessors" in entry) {
    if (!Array.isArray(entry.predecessors) || entry.predecessors.length === 0) {
      fail(`legacy.${name}.predecessors must be a non-empty array`);
    } else {
      for (const p of entry.predecessors) claim(p, `legacy.${name}.predecessors`);
    }
  }
  if ("renamed_from" in entry) claim(entry.renamed_from, `legacy.${name}.renamed_from`);
}

for (const [name, reason] of Object.entries(exempt)) {
  claim(name, `exempt.${name}`);
  if (!isText(reason)) fail(`exempt.${name} must be the reason, a non-empty string`);
}

const legacyNames = Object.keys(legacy).sort();
const pin = crypto.createHash("sha256").update(legacyNames.join("\n")).digest("hex");
if (legacyNames.length !== LEGACY_COUNT || pin !== LEGACY_PIN) {
  fail(
    `legacy holds ${legacyNames.length} repos hashing to ${pin}, but the frozen set is ${LEGACY_COUNT} ` +
      `hashing to ${LEGACY_PIN}. The set is frozen when it lands (design 6.3): no repo publishes a ` +
      "pre-canonical build after it, so a name cannot be added, and removing one drops a published mod's import",
  );
}

for (const name of legacyNames) {
  if (!(name in configs)) fail(`legacy.${name} has no configs entry`);
}
for (const [name, files] of Object.entries(configs)) {
  if (!REPO_NAME.test(name)) fail(`configs: ${JSON.stringify(name)} is not a lowercase hyphenated repo name`);
  const owner = claimed.get(name);
  if (name in exempt) fail(`configs.${name}: an exempt repo does not migrate, so it has no configs entry`);
  else if (owner && owner !== `legacy.${name}`) fail(`configs.${name}: the name is already used by ${owner}`);
  if (!Array.isArray(files) || files.length === 0) {
    fail(`configs.${name} must be a non-empty array of config files`);
    continue;
  }
  const seen = new Map();
  files.forEach((file, i) => {
    const where = `configs.${name}[${i}]`;
    if (!isObject(file)) {
      fail(`${where} must be an object`);
      return;
    }
    checkKeys(where, file, ["committed", "installed", "legacy_source", "dialect"], ["no_installed_reason"]);
    if (file.committed !== null) checkPath(`${where}.committed`, file.committed, "/");
    if (file.legacy_source !== null) checkPath(`${where}.legacy_source`, file.legacy_source, "\\");
    if (!DIALECTS.has(file.dialect)) {
      fail(`${where}.dialect ${JSON.stringify(file.dialect)} is not one of ${[...DIALECTS].join(", ")}`);
    }
    if (!Array.isArray(file.installed)) {
      fail(`${where}.installed must be an array`);
      return;
    }
    if (file.installed.length === 0 && !isText(file.no_installed_reason)) {
      fail(`${where}.installed is empty, which needs a no_installed_reason saying where the file lives`);
    }
    if (file.installed.length > 0 && "no_installed_reason" in file) {
      fail(`${where} has installed paths and a no_installed_reason; drop the reason`);
    }
    for (const p of file.installed) {
      checkPath(`${where}.installed`, p, "\\");
      const key = typeof p === "string" ? p.toLowerCase() : p;
      if (seen.has(key)) fail(`${where}.installed: ${p} is also installed by ${seen.get(key)}`);
      else seen.set(key, where);
    }
  });
}

const dropRules = new Map();
const noteDropRule = (rule, where) => {
  if (!isText(rule)) {
    fail(`${where}.drop_rule must be a non-empty string`);
    return;
  }
  if (dropRules.has(rule)) fail(`${where}.drop_rule ${rule} is also used by ${dropRules.get(rule)}`);
  else dropRules.set(rule, where);
};

for (const [id, n] of Object.entries(doc.normalisations)) {
  const where = `normalisations.${id}`;
  if (!isObject(n)) {
    fail(`${where} must be an object`);
    continue;
  }
  checkKeys(where, n, ["rule", "approved"], ["pending", "probe", "drop_rule"]);
  if (!isText(n.rule)) fail(`${where}.rule must be a non-empty string`);
  if ("probe" in n && !isText(n.probe)) fail(`${where}.probe must be a non-empty string`);
  if (n.approved === null) {
    if (!isText(n.pending)) fail(`${where} is not approved, so it needs a pending string saying what the owner decides`);
    if ("drop_rule" in n) fail(`${where} is not approved, so no DropRule may record it`);
  } else {
    if (!DATE.test(n.approved)) fail(`${where}.approved must be null or a YYYY-MM-DD date`);
    if ("pending" in n) fail(`${where} is approved and still has a pending string`);
    noteDropRule(n.drop_rule, where);
  }
}

for (const [id, c] of Object.entries(doc.approved_changes)) {
  const where = `approved_changes.${id}`;
  if (!isObject(c)) {
    fail(`${where} must be an object`);
    continue;
  }
  checkKeys(where, c, ["decision", "text", "approved", "drop_rule"], []);
  if (!Number.isInteger(c.decision) || c.decision < 1) fail(`${where}.decision must be a positive integer`);
  if (!isText(c.text)) fail(`${where}.text must be a non-empty string`);
  if (typeof c.approved !== "string" || !DATE.test(c.approved)) fail(`${where}.approved must be a YYYY-MM-DD date`);
  noteDropRule(c.drop_rule, where);
}

for (const [name, byKey] of Object.entries(doc.hotkey_exceptions)) {
  const where = `hotkey_exceptions.${name}`;
  if (!(name in configs)) fail(`${where}: ${name} is not a repo in configs`);
  if (!isObject(byKey) || Object.keys(byKey).length === 0) {
    fail(`${where} must be a non-empty object keyed by hotkey key`);
    continue;
  }
  for (const [key, ex] of Object.entries(byKey)) {
    if (!isObject(ex)) {
      fail(`${where}.${key} must be an object`);
      continue;
    }
    checkKeys(`${where}.${key}`, ex, ["replaces", "with", "reason"], []);
    for (const field of ["replaces", "with", "reason"]) {
      if (!isText(ex[field])) fail(`${where}.${key}.${field} must be a non-empty string`);
    }
    if (!isText(ex.replaces) || !isText(ex.with)) continue;
    const concept = SCHEMA.concepts.find((c) => c.key === key && c.canonical_default !== undefined);
    if (!concept) {
      fail(`${where}.${key}: not a hotkey concept with a canonical_default in data/config-schema.json`);
      continue;
    }
    const defaults = concept.canonical_default.split(", ");
    if (!defaults.includes(ex.replaces)) {
      fail(`${where}.${key}.replaces ${JSON.stringify(ex.replaces)} is not a binding of ${key}'s canonical_default ${concept.canonical_default}`);
    }
    if (defaults.includes(ex.with)) fail(`${where}.${key}.with ${JSON.stringify(ex.with)} is already in ${key}'s canonical_default`);
    for (const dialect of new Set(Array.isArray(configs[name]) ? configs[name].map((f) => f?.dialect) : [])) {
      if (!DIALECTS.has(dialect)) continue;
      const parsed = parseKeyBindings(ex.with, dialect);
      if (parsed.error) fail(`${where}.${key}.with: ${parsed.error}`);
      else if (parsed.bindings.length !== 1 || formatKeyBindings(parsed.bindings, dialect) !== ex.with) {
        fail(`${where}.${key}.with must be one binding written as the codec writes it: ${formatKeyBindings(parsed.bindings, dialect)}`);
      }
    }
  }
}

for (const [name, uses] of Object.entries(doc.allow_legacy_symbols)) {
  const where = `allow_legacy_symbols.${name}`;
  if (!(name in configs)) fail(`${where}: ${name} is not a repo in configs`);
  if (!Array.isArray(uses) || uses.length === 0) {
    fail(`${where} must be a non-empty array`);
    continue;
  }
  uses.forEach((use, i) => {
    if (!isObject(use)) {
      fail(`${where}[${i}] must be an object`);
      return;
    }
    checkKeys(`${where}[${i}]`, use, ["symbol", "file", "reason"], []);
    if (!isText(use.symbol)) fail(`${where}[${i}].symbol must be a non-empty string`);
    if (!isText(use.reason)) fail(`${where}[${i}].reason must be a non-empty string`);
    checkPath(`${where}[${i}].file`, use.file, "/");
  });
}

if (problems.length > 0) {
  console.error(`data/config-format.json has ${problems.length} problem(s):`);
  for (const p of problems) console.error(`  ${p}`);
  process.exit(1);
}
console.log(
  `data/config-format.json: ${legacyNames.length} legacy (pinned), ${Object.keys(exempt).length} exempt, ` +
    `${Object.keys(configs).length} repos with configs`,
);

function checkKeys(where, obj, required, optional) {
  if (!isObject(obj)) return;
  for (const key of required) {
    if (!(key in obj)) fail(`${where} is missing "${key}"`);
  }
  const known = new Set([...required, ...optional]);
  for (const key of Object.keys(obj)) {
    if (!known.has(key)) fail(`${where} has unknown key "${key}"`);
  }
}

// Game paths are backslash-separated, as install scripts and PRESERVE_FILES write them;
// repo paths are forward-slash-separated, as git writes them. Either way relative, with no
// empty, "." or ".." segment and nothing Windows refuses in a file name.
function checkPath(where, p, sep) {
  if (!isText(p)) {
    fail(`${where}: ${JSON.stringify(p)} must be a non-empty string`);
    return;
  }
  const other = sep === "\\" ? "/" : "\\";
  if (p.includes(other)) {
    fail(`${where}: ${p} uses "${other}"; ${sep === "\\" ? "game" : "repo"} paths are separated by "${sep}"`);
    return;
  }
  if (BAD_PATH_CHARS.test(p)) {
    fail(`${where}: ${p} is not relative or holds a character Windows refuses in a path`);
    return;
  }
  for (const seg of p.split(sep)) {
    if (seg === "" || seg === "." || seg === ".." || seg !== seg.trim() || seg.endsWith(".")) {
      fail(`${where}: ${p} has an empty, ".", ".." or space-padded segment, or one ending in "."`);
      return;
    }
  }
}

function duplicateKeys(text) {
  const found = [];
  const stack = [];
  let lastKey = "";
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === '"') {
      let j = i + 1;
      while (text[j] !== '"') j += text[j] === "\\" ? 2 : 1;
      const top = stack[stack.length - 1];
      if (top && top.object && top.expectKey) {
        const key = JSON.parse(text.slice(i, j + 1));
        if (top.keys.has(key)) found.push(`"${key}" appears twice in ${top.name || "the top level"}`);
        top.keys.add(key);
        top.expectKey = false;
        lastKey = key;
      }
      i = j;
    } else if (c === "{" || c === "[") {
      const parent = stack[stack.length - 1];
      let name = "";
      if (parent && parent.object) name = parent.name ? `${parent.name}.${lastKey}` : lastKey;
      else if (parent) name = `${parent.name}[]`;
      stack.push({ object: c === "{", keys: new Set(), expectKey: c === "{", name });
    } else if (c === "}" || c === "]") {
      stack.pop();
    } else if (c === ",") {
      const top = stack[stack.length - 1];
      if (top && top.object) top.expectKey = true;
    }
  }
  return found;
}
