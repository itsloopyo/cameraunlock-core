// The toolchain that builds the C interface as WebAssembly and turns it into JVM classes
// (docs/core-jar.md): wasi-sdk's clang and sysroot, and Endive's build-time compiler and the
// runtime its classes need. Each file is pinned by version and SHA-256, fetched once into a cache
// every checkout and worktree of the user shares, and checked before it is used.
//
//   import { toolchain } from './wasm-toolchain.mjs';
//   const t = await toolchain();   // { clang, sysroot, compiler, runtime: [jars] }

import { createHash, randomUUID } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { createReadStream, createWriteStream, existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';

// Both releases were more than two weeks old when pinned (2026-10-18): wasi-sdk 34 is of
// 2026-08-25 and its sysroot carries a libc++ built with exceptions; Endive 1.1.0 is of 2026-09-03.
const WASI_SDK = {
    name: 'wasi-sdk-34.0-x86_64-windows',
    url: 'https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/wasi-sdk-34.0-x86_64-windows.tar.gz',
    sha256: 'cccb5c323a9b34f0349a9b09e8804a0a7632c68c3310f4b5f437ed57d7e71d8f',
};

const ENDIVE_VERSION = '1.1.0';
const ENDIVE = {
    'build-time-compiler-cli-experimental': 'ea4c13a1eb4d1d750100951ab5462c12c02cef2e33fb3ad522ddf70f35d5ec71',
    runtime: 'ff9cf089768392ca3db32478d92bb494b2dabed9f572336e4700fffd24f61123',
    wasm: 'd1ef86bc8682fdf31bdbeec61b1c37a3bfadc462c79e70188627993e8eb7e087',
    wasi: '31b3cc3069143c4b8fcdd498832714f2e08ac44941340e6c0d3967abeb324bd4',
    log: '63e591120b4349f2a428f524f880ca26c94f0fd3ac223d1452f6b7d5364d7180',
    annotations: '45573e0fd0e3eed33f8851cfece89cc5e3868df9e94a7b9598b75913d54a3d7e',
};
/** The jars the generated classes need at run time: a mod's jar carries their classes. */
export const ENDIVE_RUNTIME = ['runtime', 'wasm', 'wasi', 'log', 'annotations'];

function cacheRoot() {
    const base = process.env.LOCALAPPDATA;
    if (!base) throw new Error('LOCALAPPDATA is not set: the WebAssembly toolchain is cached under it, and this build is Windows only');
    return join(base, 'cameraunlock-core', 'toolchain');
}

async function sha256Of(file) {
    const hash = createHash('sha256');
    await pipeline(createReadStream(file), hash);
    return hash.digest('hex');
}

/** `url` saved as `file`, which has to hash to `sha256`; a file already there is checked, not fetched again. */
async function fetchChecked(url, file, sha256) {
    if (existsSync(file)) {
        const found = await sha256Of(file);
        if (found !== sha256) throw new Error(`${file} hashes to ${found}, not the pinned ${sha256}: delete it and build again`);
        return;
    }
    const partial = `${file}.${randomUUID()}.partial`;
    const response = await fetch(url, { redirect: 'follow' });
    if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
    await pipeline(Readable.fromWeb(response.body), createWriteStream(partial));
    const found = await sha256Of(partial);
    if (found !== sha256) {
        rmSync(partial, { force: true });
        throw new Error(`${url} hashes to ${found}, not the pinned ${sha256}`);
    }
    try {
        renameSync(partial, file);
    } catch (failure) {
        // Another build fetched the same file at the same time and renamed first.
        rmSync(partial, { force: true });
        if (!existsSync(file)) throw failure;
    }
}

export async function toolchain() {
    const root = cacheRoot();
    mkdirSync(root, { recursive: true });

    // The folder says which archive it came from, so an unpacking from another pin is never used.
    const sdk = join(root, WASI_SDK.name);
    const marker = join(sdk, 'cameraunlock-archive.sha256');
    if (existsSync(sdk) && (!existsSync(marker) || readFileSync(marker, 'ascii').trim() !== WASI_SDK.sha256)) {
        throw new Error(`${sdk} is not the unpacking of the pinned archive ${WASI_SDK.sha256}: delete the folder and build again`);
    }
    // Kept once unpacked: a build that fetched it can still be about to unpack it.
    const archive = join(root, `${WASI_SDK.name}.tar.gz`);
    if (!existsSync(sdk)) {
        process.stderr.write(`fetching ${WASI_SDK.url} (about 620 MB, once per user)\n`);
        await fetchChecked(WASI_SDK.url, archive, WASI_SDK.sha256);
    }
    // Checked again: another build may have unpacked it while this one fetched.
    if (!existsSync(sdk)) {
        const unpacking = join(root, `${WASI_SDK.name}.${randomUUID()}.unpacking`);
        mkdirSync(unpacking);
        // Windows' own tar: a GNU tar earlier on the PATH, as Git Bash has, takes C: for a host to connect to.
        const tar = join(process.env.SystemRoot ?? 'C:\\Windows', 'System32', 'tar.exe');
        try {
            execFileSync(tar, ['-xzf', archive, '-C', unpacking, '--strip-components=1'], { stdio: 'inherit' });
        } catch (failure) {
            rmSync(unpacking, { recursive: true, force: true });
            throw failure;
        }
        writeFileSync(join(unpacking, 'cameraunlock-archive.sha256'), `${WASI_SDK.sha256}\n`, 'ascii');
        try {
            renameSync(unpacking, sdk);
        } catch (failure) {
            rmSync(unpacking, { recursive: true, force: true });
            if (!existsSync(marker)) throw failure;
        }
    }

    const jars = {};
    for (const [artifact, sha256] of Object.entries(ENDIVE)) {
        const file = join(root, `endive-${artifact}-${ENDIVE_VERSION}.jar`);
        await fetchChecked(`https://repo1.maven.org/maven2/run/endive/${artifact}/${ENDIVE_VERSION}/${artifact}-${ENDIVE_VERSION}.jar`,
            file, sha256);
        jars[artifact] = file;
    }

    return {
        clang: join(sdk, 'bin', 'clang++.exe'),
        cc: join(sdk, 'bin', 'clang.exe'),
        sysroot: join(sdk, 'share', 'wasi-sysroot'),
        compiler: jars['build-time-compiler-cli-experimental'],
        runtime: ENDIVE_RUNTIME.map((artifact) => jars[artifact]),
    };
}
