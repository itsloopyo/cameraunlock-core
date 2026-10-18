#!/usr/bin/env node
// cameraunlock-core as one jar for a Java mod, with no native file (docs/core-jar.md):
//
//   node cameraunlock-core/scripts/build-core-jar.mjs <out.jar>
//
// The jar holds java/src (the binding, CameraUnlock.loadBundled, and the boot class), the C
// interface built as WebAssembly and compiled to JVM classes (cpp/build-wasm/core), the classes
// of the Endive jars those run on, and under META-INF/licenses the notices that travel with them.
// A mod compiles against it and puts its whole content into the mod's own jar, which a loader
// that takes one jar per mod (ZombieBuddy) needs.
//
// javac and jar come from the caller's environment; the toolchain is fetched by build-wasm.mjs.
// Windows only, as the toolchain is (wasm-toolchain.mjs).

import { execFileSync } from 'node:child_process';
import { copyFileSync, cpSync, existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { delimiter, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { toolchain } from './wasm-toolchain.mjs';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const target = process.argv[2];
if (!target) throw new Error('usage: node build-core-jar.mjs <out.jar>');

function run(tool, args, options = {}) {
    execFileSync(tool, args, { stdio: 'inherit', ...options });
}

function sources(directory) {
    return readdirSync(directory, { recursive: true, withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith('.java'))
        .map((entry) => join(entry.parentPath, entry.name));
}

run(process.execPath, [join(repoRoot, 'scripts', 'build-wasm.mjs'), 'core']);
const tools = await toolchain();
const module = join(repoRoot, 'cpp', 'build-wasm', 'core');

const work = mkdtempSync(join(tmpdir(), 'cameraunlock-core-jar-'));
try {
    const classes = join(work, 'classes');
    mkdirSync(classes);
    run('javac', ['--release', '22', '-Xlint:all', '-Werror', '-cp', [...tools.runtime, join(module, 'classes')].join(delimiter),
        '-d', classes, ...sources(join(repoRoot, 'java', 'src')), ...sources(join(module, 'src'))]);
    cpSync(join(module, 'classes'), classes, { recursive: true });

    // Each Endive jar's classes. Its module-info, manifest and Maven metadata are its own, would
    // collide with the next jar's, and say nothing on a class path.
    for (const jar of tools.runtime) run('jar', ['--extract', '--file', jar], { cwd: classes });
    for (const own of ['module-info.class', 'THIRD-PARTY.txt', join('META-INF', 'MANIFEST.MF'), join('META-INF', 'maven')]) {
        rmSync(join(classes, own), { recursive: true, force: true });
    }

    const licences = join(classes, 'META-INF', 'licenses');
    mkdirSync(licences, { recursive: true });
    for (const notice of ['cameraunlock-core.txt', 'endive.txt', 'wasi-libc.txt', 'llvm-runtimes.txt']) {
        const from = join(repoRoot, 'legal', notice);
        if (!existsSync(from)) throw new Error(`${from} is missing: the notices travel with the classes they are for`);
        copyFileSync(from, join(licences, notice));
    }

    mkdirSync(dirname(resolve(target)), { recursive: true });
    rmSync(target, { force: true });
    // One date on every entry, and no manifest to carry the JDK's version, so the same sources and
    // toolchain give the same bytes. Nothing reads a manifest of this jar: a mod takes its classes.
    run('jar', ['--create', '--no-manifest', '--date', '1980-01-01T00:00:02Z', '--file', resolve(target), '-C', classes, '.']);
    console.log(`built ${resolve(target)}`);
} finally {
    rmSync(work, { recursive: true, force: true });
}
