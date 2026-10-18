#!/usr/bin/env node
// Builds the C interface as WebAssembly and turns each module into JVM classes with Endive's
// build-time compiler (docs/core-jar.md):
//
//   node scripts/build-wasm.mjs [core|testing|other-abi ...]    (no names: all three)
//
// For each variant, under cpp/build-wasm/<variant>/:
//   <variant>.wasm   the module
//   src/             the one Java source Endive writes, com/cameraunlock/core/wasm/<Class>.java
//   classes/         the compiled functions and the module's .meta resource
//   build.stamp      the toolchain pins and command lines it was built with, written last
//
//   core        what a mod ships: CameraUnlock.loadBundled() (CoreModule)
//   testing     the same with cameraunlock/c/testing/cameraunlock_testing.h (CoreTestingModule)
//   other-abi   tests/c_other_abi.c, a module of the ABI before this one (CoreOtherAbiModule)
//
// A variant is left as it is when its stamp names the pins and command lines this run would use
// and is newer than every source, header and this script. The stamp is removed before a rebuild
// and written once Endive has finished, so a build that stopped part way is not taken for done.

import { execFile } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { cpus } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { TOOLCHAIN_PINS, toolchain } from './wasm-toolchain.mjs';

const run = promisify(execFile);
const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const cpp = join(repoRoot, 'cpp');
const out = join(cpp, 'build-wasm');

// What the C interface is built from, as CameraUnlockCore.dll is, less what only Windows' own
// threads and sockets need (CMakeLists.txt's CAMERAUNLOCK_SOURCES has the rest).
const SOURCES = [
    'src/c/cameraunlock_c.cpp',
    'src/data/tracking_pose.cpp',
    'src/math/angle_utils.cpp',
    'src/math/deadzone_utils.cpp',
    'src/math/smoothing_utils.cpp',
    'src/protocol/opentrack_packet.cpp',
    'src/protocol/udp_socket.cpp',
    'src/protocol/udp_receiver.cpp',
    'src/processing/center_offset_manager.cpp',
    'src/processing/tracking_processor.cpp',
    'src/config/ini_reader.cpp',
    'src/config/head_tracking_config.cpp',
    'src/config/ini_editor.cpp',
    'src/config/canonical_ini.cpp',
    'src/config/checked_file_writer.cpp',
    'src/config/retired_keys.cpp',
    'src/config/value_guards.cpp',
    'src/config/value_codecs.cpp',
    'src/config/hotkey_codec.cpp',
    'src/config/config_table.cpp',
    'src/config/legacy_import.cpp',
    'src/config/config_owner.cpp',
    'src/config/defaults_ini.cpp',
    'src/config/defaults_location.cpp',
    'src/os/game_window.cpp',
    'src/input/hotkey_poller.cpp',
    'src/input/key_bindings.cpp',
    'src/logging/file_log.cpp',
];

// Standard exception handling (exnref), the only kind Endive runs; clang emits the legacy kind
// without the second flag. A reactor: no main, constructors in _initialize.
const COMPILE = ['--target=wasm32-wasip1', '-O2', '-std=c++17', '-fwasm-exceptions', '-mllvm', '-wasm-use-legacy-eh=false',
    '-DCAMERAUNLOCK_WIN32_HOST', '-I', join(cpp, 'include'), '-Werror', '-Wall'];
const LINK = ['--target=wasm32-wasip1', '-mexec-model=reactor', '-fwasm-exceptions', '-lunwind', '-Wl,--gc-sections',
    '-Wl,--export=malloc', '-Wl,--export=free'];
const OTHER_ABI = ['--target=wasm32-wasip1', '-O2', '-mexec-model=reactor', '-I', join(cpp, 'include'), '-Wl,--export=cameraunlock_abi'];
// FAIL: a function too large for one JVM method stops the build rather than running in Endive's
// interpreter.
const ENDIVE_ARGS = ['--interpreter-fallback', 'FAIL'];

const VARIANTS = {
    core: { module: 'CoreModule', defines: [], headers: ['include/cameraunlock/c/cameraunlock.h', 'include/cameraunlock/c/host/cameraunlock_host.h'] },
    testing: { module: 'CoreTestingModule', defines: ['-DCAMERAUNLOCK_C_TESTING'],
        headers: ['include/cameraunlock/c/cameraunlock.h', 'include/cameraunlock/c/host/cameraunlock_host.h',
            'include/cameraunlock/c/testing/cameraunlock_testing.h'] },
    'other-abi': { module: 'CoreOtherAbiModule' },
};

function newest(directory) {
    let latest = 0;
    for (const entry of readdirSync(directory, { recursive: true, withFileTypes: true })) {
        if (entry.isFile()) latest = Math.max(latest, statSync(join(entry.parentPath, entry.name)).mtimeMs);
    }
    return latest;
}

/** The C functions a header declares, each one an export. */
function exportsOf(headers) {
    const names = new Set();
    for (const header of headers) {
        // A function named in a comment is not declared.
        // One pass, so whichever comment starts first is the one taken out.
        const code = readFileSync(join(cpp, header), 'utf8').replace(/\/\*[\s\S]*?\*\/|\/\/.*$/gm, '');
        for (const match of code.matchAll(/\b(cameraunlock_[a-z0-9_]+)\s*\(/g)) names.add(match[1]);
    }
    return [...names].sort().map((name) => `-Wl,--export=${name}`);
}

/** `jobs` run with at most `limit` at once; the first failure is thrown once all have ended. */
async function pooled(jobs, limit) {
    const failures = [];
    let next = 0;
    async function worker() {
        while (next < jobs.length) {
            const job = jobs[next++];
            try {
                await job();
            } catch (failure) {
                failures.push(failure);
            }
        }
    }
    await Promise.all(Array.from({ length: limit }, worker));
    if (failures.length > 0) throw failures[0];
}

async function compile(tools, variant, spec) {
    const objects = join(out, variant, 'objects');
    mkdirSync(objects, { recursive: true });
    const jobs = SOURCES.map((source) => async () => {
        const object = join(objects, source.replaceAll('/', '_') + '.o');
        try {
            await run(tools.clang, [...COMPILE, ...spec.defines, `--sysroot=${tools.sysroot}`, '-c', join(cpp, source), '-o', object],
                { maxBuffer: 64 << 20 });
        } catch (failure) {
            throw new Error(`${source} did not compile for WebAssembly (${variant}):\n${failure.stderr || failure.message}`);
        }
        return object;
    });
    await pooled(jobs, Math.max(1, Math.floor(cpus().length / 2)));
    return SOURCES.map((source) => join(objects, source.replaceAll('/', '_') + '.o'));
}

async function build(tools, variant) {
    const spec = VARIANTS[variant];
    const folder = join(out, variant);
    const wasm = join(folder, `${variant}.wasm`);
    const stampFile = join(folder, 'build.stamp');
    const stamp = `${JSON.stringify({ toolchain: TOOLCHAIN_PINS, sources: SOURCES, compile: COMPILE, link: LINK, otherAbi: OTHER_ABI,
        endive: ENDIVE_ARGS, variant: spec }, null, 2)}\n`;
    const inputs = Math.max(newest(join(cpp, 'include')), newest(join(cpp, 'src')), statSync(join(cpp, 'tests', 'c_other_abi.c')).mtimeMs,
        statSync(fileURLToPath(import.meta.url)).mtimeMs);
    // The newest input is read before the build and kept in the stamp, so a file saved while the
    // build runs is newer than what the stamp says was built.
    const built = existsSync(stampFile) ? readFileSync(stampFile, 'utf8') : '';
    if (built.startsWith(stamp) && Number(built.slice(stamp.length)) >= inputs) {
        process.stderr.write(`${variant}: up to date\n`);
        return;
    }
    rmSync(stampFile, { force: true });
    mkdirSync(folder, { recursive: true });
    if (variant === 'other-abi') {
        await run(tools.cc, [...OTHER_ABI, `--sysroot=${tools.sysroot}`, join(cpp, 'tests', 'c_other_abi.c'), '-o', wasm]);
    } else {
        const objects = await compile(tools, variant, spec);
        await run(tools.clang, [...LINK, `--sysroot=${tools.sysroot}`, ...objects, ...exportsOf(spec.headers), '-o', wasm],
            { maxBuffer: 64 << 20 });
    }
    for (const generated of ['src', 'classes']) rmSync(join(folder, generated), { recursive: true, force: true });
    await run('java', ['-jar', tools.compiler, wasm, '--prefix', `com.cameraunlock.core.wasm.${spec.module}`,
        '--source-dir', join(folder, 'src'), '--class-dir', join(folder, 'classes'), '--wasm-dir', join(folder, 'classes'),
        ...ENDIVE_ARGS], { maxBuffer: 64 << 20 });
    writeFileSync(stampFile, `${stamp}${inputs}\n`, 'utf8');
    process.stderr.write(`${variant}: ${wasm} (${(statSync(wasm).size / 1048576).toFixed(1)} MB)\n`);
}

const asked = process.argv.slice(2);
for (const name of asked) {
    if (!VARIANTS[name]) throw new Error(`no variant ${name}: one of ${Object.keys(VARIANTS).join(', ')}`);
}
const tools = await toolchain();
for (const variant of asked.length > 0 ? asked : Object.keys(VARIANTS)) await build(tools, variant);
