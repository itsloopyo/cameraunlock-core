#!/usr/bin/env node

// Holds scripts/validate-manifest.mjs to its patches[].tool rule on built ZIPs.
//
// patches[].tool: the launcher runs a Cecil mod's patch tool in place from the package root, with
// the tool's folder as its working directory. The tool has to be in the ZIP, and it and the files
// beside it in its folder are declared payload, while an undeclared binary anywhere else still
// fails. ZIPs here are named by a path outside any release/ folder, so no repo rule applies.
//
//   node scripts/test-validate-manifest.mjs      (pixi run test-validate-manifest, part of pixi run check)

import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const VALIDATOR = path.join(CORE_ROOT, "scripts", "validate-manifest.mjs");
const TAR = process.platform === "win32" ? path.join(process.env.SystemRoot || "C:\\Windows", "System32", "tar.exe") : "tar";
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "validate-manifest-"));
const { GITHUB_ACTIONS: _actions, GITHUB_REF_TYPE: _refType, GITHUB_REF_NAME: _refName, ...BUILD_ENV } = process.env;

const failures = [];
let checks = 0;
const check = (ok, message) => {
  checks++;
  if (!ok) failures.push(message);
};

function run(script, ...args) {
  const r = spawnSync(process.execPath, [script, ...args], { encoding: "utf8", env: BUILD_ENV });
  return { status: r.status, out: r.stdout + r.stderr };
}

// A ZIP at zipPath holding a launcher-manifest.json of man (unless null) and each of files.
function zip(zipPath, man, files) {
  const staging = fs.mkdtempSync(path.join(scratch, "staging-"));
  const all = man === null ? files : { "launcher-manifest.json": JSON.stringify(man, null, 2), ...files };
  for (const [rel, text] of Object.entries(all)) {
    fs.mkdirSync(path.dirname(path.join(staging, rel)), { recursive: true });
    fs.writeFileSync(path.join(staging, rel), text);
  }
  fs.mkdirSync(path.dirname(zipPath), { recursive: true });
  const tops = [...new Set(Object.keys(all).map((rel) => rel.split("/")[0]))];
  const r = spawnSync(TAR, ["-a", "-cf", zipPath, "-C", staging, ...tops], { encoding: "utf8" });
  if (r.status !== 0) throw new Error(`tar failed: ${r.stderr}`);
  return zipPath;
}

const PATCH = {
  target: "Game_Data/Managed/Assembly-CSharp.dll",
  tool: "tools/BootstrapPatcher.exe",
  args: ["{input}", "{output}"],
  unpatch_args: ["unpatch", "{input}", "{output}"],
  marker: "HeadTracking_Patched_v1",
};
const patched = (extra = {}) => ({
  schema_version: 2,
  mod_info: { name: "Mod", version: "1.2.0", game_id: "mod" },
  delivery_mode: "manifest",
  files: [{ source: "mod/Mod.dll", target: "Game_Data/Managed/Mod.dll" }],
  patches: [structuredClone(PATCH)],
  ...extra,
});
const TOOLS = { "tools/BootstrapPatcher.exe": "exe", "tools/Mono.Cecil.dll": "cecil", "tools/BootstrapPatcher.exe.config": "config" };
const payload = (extra = {}) => ({ "mod/Mod.dll": "dll", ...TOOLS, ...extra });
const patchCase = (label, man, files) => run(VALIDATOR, zip(path.join(scratch, "patches", `${label}-v1.2.0-installer.zip`), man, files));

{
  const good = patchCase("good", patched(), payload());
  check(good.status === 0 && good.out.includes("1 patch(es)"), `patches: a tool in the ZIP should pass and be counted, got ${good.status}\n${good.out}`);
  check(!good.out.includes("tools/"), `patches: the tool and the files beside it are declared, so nothing should name them, got\n${good.out}`);

  const missing = patchCase("missing", patched(), { "mod/Mod.dll": "dll", "tools/Mono.Cecil.dll": "cecil" });
  check(
    missing.status === 1 && missing.out.includes("patches[].tool missing from zip") && missing.out.includes("tools/BootstrapPatcher.exe"),
    `patches: a tool missing from the ZIP should fail, got ${missing.status}\n${missing.out}`,
  );

  const miscased = patchCase("miscased", patched({ patches: [{ ...PATCH, tool: "Tools/bootstrappatcher.exe" }] }), payload());
  check(
    miscased.status === 0 && miscased.out.includes('Tools/bootstrappatcher.exe (zip has "tools/BootstrapPatcher.exe")'),
    `patches: a tool matched only without case should pass with a warning, got ${miscased.status}\n${miscased.out}`,
  );

  const unpatched = patchCase("unpatched", patched({ patches: undefined }), payload());
  check(
    unpatched.status === 1 && unpatched.out.includes("binaries no manifest row deploys") && unpatched.out.includes("tools/BootstrapPatcher.exe"),
    `patches: a tool no patches[] row names is undeclared, got ${unpatched.status}\n${unpatched.out}`,
  );

  const elsewhere = patchCase("elsewhere", patched(), payload({ "other/Extra.dll": "dll", "tools/sub/Nested.dll": "dll" }));
  check(
    elsewhere.status === 1 && elsewhere.out.includes("other/Extra.dll") && elsewhere.out.includes("tools/sub/Nested.dll") && !elsewhere.out.includes("tools/Mono.Cecil.dll"),
    `patches: only the tool's own folder is declared, not another folder or one below it, got ${elsewhere.status}\n${elsewhere.out}`,
  );

  const atRoot = patchCase("at-root", patched({ patches: [{ ...PATCH, tool: "BootstrapPatcher.exe" }] }), { "mod/Mod.dll": "dll", "BootstrapPatcher.exe": "exe", "Extra.dll": "dll" });
  check(
    atRoot.status === 1 && atRoot.out.includes("deploys, so a launcher install would leave them out and the mod would not run: Extra.dll."),
    `patches: a tool at the package root declares itself and nothing beside it, got ${atRoot.status}\n${atRoot.out}`,
  );

  for (const [label, change, expected] of [
    ["traversal", { tool: "../BootstrapPatcher.exe" }, "must stay relative to the package root"],
    ["absolute", { tool: "C:\\tools\\BootstrapPatcher.exe" }, "must stay relative to the package root"],
    ["no-tool", { tool: undefined }, 'no nonempty string "tool"'],
    ["no-target", { target: "" }, 'no nonempty string "target"'],
    ["no-marker", { marker: undefined }, 'no nonempty string "marker"'],
  ]) {
    const r = patchCase(label, patched({ patches: [{ ...PATCH, ...change }] }), payload());
    check(r.status === 1 && r.out.includes(expected), `patches: ${label} should fail with "${expected}", got ${r.status}\n${r.out}`);
  }

  const notArray = patchCase("not-array", patched({ patches: structuredClone(PATCH) }), payload());
  check(notArray.status === 1 && notArray.out.includes('"patches" must be an array'), `patches: an object should fail, got ${notArray.status}\n${notArray.out}`);

  const inVariant = patchCase(
    "in-variant",
    patched({ delivery_mode: "manifest_variants", files: undefined, patches: undefined, variants: [{ id: "only", files: [{ source: "mod/Mod.dll", target: "Mod.dll" }], patches: [structuredClone(PATCH)] }] }),
    payload({ "install.cmd": "@echo off", "uninstall.cmd": "@echo off" }),
  );
  check(
    inVariant.status === 1 && inVariant.out.includes('variant "only" has patches, which the launcher reads only at the top level'),
    `patches: a variant's patches should fail, got ${inVariant.status}\n${inVariant.out}`,
  );
}

fs.rmSync(scratch, { recursive: true, force: true });

if (failures.length > 0) {
  console.error(`${failures.length} of ${checks} checks failed:`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`validate-manifest: ${checks} checks passed`);
