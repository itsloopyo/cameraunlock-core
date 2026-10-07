#!/usr/bin/env node
// Compiles java/ and stages what its tests and its vectors harness load, under java/build/:
//
//   classes/        java/src, the binding and the boot class a mod compiles into its own jar
//   test-classes/   java/harness and java/tests
//   *.dll           CameraUnlockCore.dll, CameraUnlockCoreTesting.dll and
//                   CameraUnlockCoreOtherAbi.dll, beside the class folders, which is where the
//                   binding looks for its library
//   boot/Fixture.jar  an agent jar for BootTests: the boot class and java/tests/fixture
//
// java/src is compiled for Java 22, the first release where java.lang.foreign is final, so a
// construct from a later release fails here and not in a mod on an older runtime.
//
// The DLLs are built by the task that calls this (pixi.toml, build-java). javac, jar and node come
// from the pixi environment.

import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const javaRoot = join(repoRoot, 'java');
const out = join(javaRoot, 'build');
const dllRoot = join(repoRoot, 'scripts', 'pipeline-vectors', 'harness', 'c', 'build', 'cameraunlock', 'Release');
const dlls = ['CameraUnlockCore.dll', 'CameraUnlockCoreTesting.dll', 'CameraUnlockCoreOtherAbi.dll'];

function sources(directory) {
    return readdirSync(directory, { recursive: true, withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith('.java'))
        .map((entry) => join(entry.parentPath, entry.name));
}

function run(tool, args) {
    execFileSync(tool, args, { stdio: 'inherit' });
}

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

const javac = ['--release', '22', '-Xlint:all', '-Werror'];
run('javac', [...javac, '-d', join(out, 'classes'), ...sources(join(javaRoot, 'src'))]);
run('javac', [...javac, '-cp', join(out, 'classes'), '-d', join(out, 'test-classes'),
    ...sources(join(javaRoot, 'harness')), ...sources(join(javaRoot, 'tests'))]);

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
