#!/usr/bin/env node
// Compiles java/ and stages what its tests and its vectors harness load, under java/build/:
//
//   classes/        java/src, the binding and the boot class a mod compiles into its own jar,
//                   with the C interface as JVM classes (cpp/build-wasm/core)
//   test-classes/   java/harness and java/tests, with the testing and other-ABI modules
//   lib/            the Endive jars the module classes run on (docs/core-jar.md)
//   *.dll           CameraUnlockCore.dll, CameraUnlockCoreTesting.dll and
//                   CameraUnlockCoreOtherAbi.dll, beside the class folders, which is where the
//                   binding looks for its library
//   boot/Fixture.jar  an agent jar for BootTests: the boot class and java/tests/fixture
//
// java/src is compiled for Java 22, the first release where java.lang.foreign is final, so a
// construct from a later release fails here and not in a mod on an older runtime.
//
// First, check-win32-imports.mjs holds the modules' imports of "cameraunlock_win32" to the
// functions Win32Host registers.
//
// The DLLs and the modules are built by the tasks this depends on (pixi.toml, build-java). javac,
// jar and node come from the pixi environment.

import { execFileSync } from 'node:child_process';
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { basename, delimiter, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { checkWin32Imports } from './check-win32-imports.mjs';
import { toolchain } from './wasm-toolchain.mjs';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const javaRoot = join(repoRoot, 'java');
const out = join(javaRoot, 'build');
const dllRoot = join(repoRoot, 'scripts', 'pipeline-vectors', 'harness', 'c', 'build', 'cameraunlock', 'Release');
const dlls = ['CameraUnlockCore.dll', 'CameraUnlockCoreTesting.dll', 'CameraUnlockCoreOtherAbi.dll'];
const modules = join(repoRoot, 'cpp', 'build-wasm');

function sources(directory) {
    return readdirSync(directory, { recursive: true, withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith('.java'))
        .map((entry) => join(entry.parentPath, entry.name));
}

function run(tool, args) {
    execFileSync(tool, args, { stdio: 'inherit' });
}

/** A module's generated source and classes, which build-wasm.mjs writes. */
function module(variant) {
    const folder = join(modules, variant);
    if (!existsSync(join(folder, 'build.stamp'))) throw new Error(`${folder} is not built: run this through \`pixi run build-java\``);
    return { src: join(folder, 'src'), classes: join(folder, 'classes') };
}

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

const lib = join(out, 'lib');
mkdirSync(lib);
for (const jar of (await toolchain()).runtime) copyFileSync(jar, join(lib, basename(jar)));
const runtime = readdirSync(lib).map((jar) => join(lib, jar)).join(delimiter);

const core = module('core'), testing = module('testing'), otherAbi = module('other-abi');
checkWin32Imports(['core', 'testing', 'other-abi'].map((variant) => join(modules, variant, `${variant}.wasm`)));
const javac = ['--release', '22', '-Xlint:all', '-Werror'];
run('javac', [...javac, '-cp', [runtime, core.classes].join(delimiter), '-d', join(out, 'classes'),
    ...sources(join(javaRoot, 'src')), ...sources(core.src)]);
cpSync(core.classes, join(out, 'classes'), { recursive: true });
run('javac', [...javac, '-cp', [runtime, join(out, 'classes'), testing.classes, otherAbi.classes].join(delimiter),
    '-d', join(out, 'test-classes'), ...sources(join(javaRoot, 'harness')), ...sources(join(javaRoot, 'tests')),
    ...sources(testing.src), ...sources(otherAbi.src)]);
cpSync(testing.classes, join(out, 'test-classes'), { recursive: true });
cpSync(otherAbi.classes, join(out, 'test-classes'), { recursive: true });

for (const dll of dlls) {
    const built = join(dllRoot, dll);
    if (!existsSync(built)) throw new Error(`${built} is not built: run this through \`pixi run build-java\``);
    copyFileSync(built, join(out, dll));
}

// The agent jar a Java agent mod ships, in small: core's boot class and an Agent-Class.
const boot = join(out, 'boot');
mkdirSync(boot);
const manifest = join(boot, 'MANIFEST.MF');
writeFileSync(manifest, 'Agent-Class: fixture.Agent\nPremain-Class: fixture.Agent\n', 'ascii');
run('jar', ['--create', '--file', join(boot, 'Fixture.jar'), '--manifest', manifest,
    '-C', join(out, 'classes'), 'com/cameraunlock/core/agent',
    '-C', join(out, 'test-classes'), 'fixture']);

console.log(`built ${out}`);
