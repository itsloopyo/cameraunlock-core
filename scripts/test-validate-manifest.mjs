#!/usr/bin/env node

// Holds scripts/validate-manifest.mjs to two rules on built ZIPs.
//
// patches[].tool: the launcher runs a Cecil mod's patch tool in place from the package root, with
// the tool's folder as its working directory. The tool has to be in the ZIP, and it and the files
// beside it in its folder are declared payload, while an undeclared binary anywhere else still
// fails. ZIPs here are named by a path outside any release/ folder, so no repo rule applies.
//
// The Nexus ZIP: with no arguments the validator checks the Nexus ZIP of the installer ZIP it just
// validated, the same name with -nexus.zip, and warns about and skips any other one as stale. That
// runs from a throwaway abzu-headtracking checkout (a converted data/config-format.json entry) with
// a copy of core's scripts and data in its cameraunlock-core folder, the layout a mod has.
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

// delivery_mode is matched case-sensitively and a null is an unknown mode, as Lopari's serde enum
// reads them. ReleaseWorkflow.psm1's Assert-LauncherManifestDelivery is held to the same cases.
for (const [label, mode] of [["capitalised", "Manifest"], ["upper-install-cmd", "INSTALL_CMD"], ["null", null], ["empty", ""]]) {
  const r = patchCase(`mode-${label}`, patched({ delivery_mode: mode, install_cmd_reason: "writes a registry key" }), payload());
  check(r.status === 1 && r.out.includes(`delivery_mode is "${mode}"`), `delivery_mode: ${JSON.stringify(mode)} should fail as unknown, got ${r.status}
${r.out}`);
}

// The Nexus ZIP. The mod is abzu-headtracking, converted: its committed HeadTracking.ini is the
// rendered canonical file, and its config installs at AbzuGame/Binaries/Win64/CameraUnlock.ini.
const ALL = fs.readFileSync(path.join(CORE_ROOT, "data", "fixtures", "canonical-ini", "head-tracking", "all-concepts-fresh.ini"));
const INSTALLED = "AbzuGame/Binaries/Win64/CameraUnlock.ini";
const abzuMan = (version) => ({
  schema_version: 2,
  mod_info: { name: "Mod", version, game_id: "abzu" },
  delivery_mode: "manifest",
  files: [{ source: "plugins/Mod.dll", target: "Mod.dll" }],
});

// A mod checkout holding release/<name> for each ZIP given, with core's scripts and data copied
// into its cameraunlock-core folder. Returns the result of validating it with no arguments.
function selfRun(label, zips) {
  const root = path.join(scratch, "nexus", label, "abzu-headtracking");
  fs.mkdirSync(root, { recursive: true });
  const git = spawnSync("git", ["-C", root, "init", "-q"], { encoding: "utf8" });
  if (git.status !== 0) throw new Error(`git init failed: ${git.stderr}`);
  fs.writeFileSync(path.join(root, "HeadTracking.ini"), ALL);
  const core = path.join(root, "cameraunlock-core");
  fs.cpSync(path.join(CORE_ROOT, "data"), path.join(core, "data"), { recursive: true });
  fs.cpSync(path.join(CORE_ROOT, "scripts", "lib"), path.join(core, "scripts", "lib"), { recursive: true });
  for (const name of fs.readdirSync(path.join(CORE_ROOT, "scripts")).filter((n) => n.endsWith(".mjs"))) {
    fs.copyFileSync(path.join(CORE_ROOT, "scripts", name), path.join(core, "scripts", name));
  }
  for (const [name, [man, files]] of Object.entries(zips)) zip(path.join(root, "release", name), man, files);
  return run(path.join(core, "scripts", "validate-manifest.mjs"));
}
const installer = (version) => [abzuMan(version), { "plugins/Mod.dll": "dll" }];
const nexus = (files) => [null, files];

{
  const carries = selfRun("carries", {
    "Mod-v1.2.0-installer.zip": installer("1.2.0"),
    "Mod-v1.2.0-nexus.zip": nexus({ "Mod.dll": "dll", [INSTALLED]: "[CameraUnlock]\r\n" }),
  });
  check(
    carries.status === 1 && carries.out.includes(`Mod-v1.2.0-nexus.zip carries ${INSTALLED}`),
    `nexus: the Nexus ZIP of the same build carrying the config should fail, got ${carries.status}\n${carries.out}`,
  );

  const clean = selfRun("clean", {
    "Mod-v1.2.0-installer.zip": installer("1.2.0"),
    "Mod-v1.2.0-nexus.zip": nexus({ "Mod.dll": "dll" }),
  });
  check(clean.status === 0 && clean.out.includes("Mod-v1.2.0-nexus.zip - carries no config"), `nexus: a clean Nexus ZIP of the same build should pass, got ${clean.status}\n${clean.out}`);

  const stale = selfRun("stale", {
    "Mod-v1.3.0-installer.zip": installer("1.3.0"),
    "Mod-v1.2.0-nexus.zip": nexus({ "Mod.dll": "dll", [INSTALLED]: "[CameraUnlock]\r\n" }),
  });
  check(
    stale.status === 0 && stale.out.includes("WARN abzu-headtracking: Mod-v1.2.0-nexus.zip is not the Nexus ZIP of Mod-v1.3.0-installer.zip, so it is stale and its config is not checked"),
    `nexus: a Nexus ZIP of another build should be skipped with a warning, got ${stale.status}\n${stale.out}`,
  );

  const none = selfRun("none", { "Mod-v1.2.0-installer.zip": installer("1.2.0") });
  check(none.status === 0 && !none.out.includes("nexus"), `nexus: a repo with no Nexus ZIP should say nothing about one, got ${none.status}\n${none.out}`);
}

{
  const names = ["OuterWildsHeadTracking.dll", "CameraUnlock.Core.dll", "manifest.json", "default-config.json"];
  const owml = {
    schema_version: 2,
    mod_info: { name: "Head Tracking", version: "1.3.0", game_id: "outer-wilds" },
    strategy: "OWML",
    delivery_mode: "external",
    external: { manager_name: "Outer Wilds Mod Manager", manager_url: "https://outerwildsmods.com/", owml_mod_id: "itsloopyo.OuterWildsHeadTracking" },
    files: names.map((name) => ({ source: name, target: name, anchor: "mod_home" })),
  };
  for (const scenario of ["valid", "legacy", "wrong-id", "config-overwrite", "wrong-anchor", "missing-dll", "loader"]) {
    const man = structuredClone(owml);
    const files = Object.fromEntries(names.map((name) => [name, "fixture"]));
    if (scenario === "legacy") delete man.external.owml_mod_id;
    if (scenario === "wrong-id") man.external.owml_mod_id = "../OtherMod";
    if (scenario === "config-overwrite") man.files[0].target = "config.json";
    if (scenario === "wrong-anchor") man.files[0].anchor = "game_root";
    if (scenario === "missing-dll") delete files[names[0]];
    if (scenario === "loader") man.loader = { archives: [] };
    const result = run(VALIDATOR, zip(path.join(scratch, `owml-${scenario}.zip`), man, files));
    const expected = ["valid", "legacy"].includes(scenario) ? 0 : 1;
    check(result.status === expected, `OWML ${scenario}: expected ${expected}, got ${result.status}\n${result.out}`);
  }
}

fs.rmSync(scratch, { recursive: true, force: true });

if (failures.length > 0) {
  console.error(`${failures.length} of ${checks} checks failed:`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`validate-manifest: ${checks} checks passed`);
