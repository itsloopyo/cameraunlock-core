#!/usr/bin/env node
// Holds the WebAssembly modules' imports of "cameraunlock_win32" to the host functions
// java/src/com/cameraunlock/core/Win32Host.java registers, both ways:
//
//   node scripts/check-win32-imports.mjs <module.wasm> ...
//
// An import the host does not register, or registers with another signature, compiles and links
// and fails only when the module is instantiated. A function the host registers that no module
// imports is dead code in the host. Either fails here, with the names. build-java.mjs runs it
// over every module the host serves.

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const MODULE = 'cameraunlock_win32';
const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const hostSource = join(repoRoot, 'java', 'src', 'com', 'cameraunlock', 'core', 'Win32Host.java');

const VALTYPES = { 0x7f: 'i32', 0x7e: 'i64', 0x7d: 'f32', 0x7c: 'f64' };

function reader(bytes) {
    let at = 0;
    const r = {
        get at() { return at; },
        set at(value) { at = value; },
        byte() {
            if (at >= bytes.length) throw new Error('the module ends inside a section');
            return bytes[at++];
        },
        u32() {
            let value = 0, shift = 0, b;
            do {
                b = r.byte();
                value += (b & 0x7f) * 2 ** shift;
                shift += 7;
            } while (b & 0x80);
            return value;
        },
        /** A value type: a reference type of more than one byte (ref null ht, ref ht) reads its heap type. */
        valtype() {
            const b = r.byte();
            if (b === 0x63 || b === 0x64) {
                r.u32();
                return 'ref';
            }
            return VALTYPES[b] ?? '?';
        },
        limits() {
            const flags = r.byte();
            r.u32();
            if (flags & 1) r.u32();
        },
        name() {
            const length = r.u32();
            const text = Buffer.from(bytes.subarray(at, at + length)).toString('utf8');
            at += length;
            return text;
        },
    };
    return r;
}

/** Each import of MODULE in a module: name -> "(i32,i32)->i32". */
export function importsOf(wasm) {
    const bytes = readFileSync(wasm);
    if (bytes.readUInt32LE(0) !== 0x6d736100 || bytes.readUInt32LE(4) !== 1) throw new Error(`${wasm} is not a WebAssembly module`);
    const r = reader(bytes);
    r.at = 8;
    const types = [];
    const imports = new Map();
    while (r.at < bytes.length) {
        const id = r.byte();
        const size = r.u32();
        const end = r.at + size;
        if (id === 1) {
            for (let count = r.u32(), i = 0; i < count; i++) {
                if (r.byte() !== 0x60) throw new Error(`${wasm}: type ${i} is not a function type`);
                const params = Array.from({ length: r.u32() }, () => r.valtype());
                const results = Array.from({ length: r.u32() }, () => r.valtype());
                types.push(`(${params.join(',')})->${results.join(',') || 'void'}`);
            }
        } else if (id === 2) {
            for (let count = r.u32(), i = 0; i < count; i++) {
                const module = r.name();
                const name = r.name();
                const kind = r.byte();
                if (kind === 0) {
                    const type = r.u32();
                    if (module === MODULE) imports.set(name, types[type]);
                    continue;
                }
                if (module === MODULE) throw new Error(`${wasm}: ${MODULE} has ${name} as an import of kind ${kind}, not a function`);
                if (kind === 1) {
                    r.valtype();
                    r.limits();
                } else if (kind === 2) {
                    r.limits();
                } else if (kind === 3) {
                    r.valtype();
                    r.byte();
                } else if (kind === 4) {
                    r.byte();
                    r.u32();
                } else {
                    throw new Error(`${wasm}: import ${module}.${name} is of kind ${kind}, which this check does not read`);
                }
            }
        }
        r.at = end;
    }
    return imports;
}

/** Each function Win32Host registers: name -> "(i32,...)->i32", or ->void for a procedure. */
export function hostFunctions(source = readFileSync(hostSource, 'utf8')) {
    const functions = new Map();
    // Comments out first, in one pass so whichever starts first is taken: a registration commented
    // out is not one.
    const code = source.replace(/\/\*[\s\S]*?\*\/|\/\/.*$/gm, '');
    for (const [, kind, name, parameters] of code.matchAll(/\b(function|procedure)\(\s*"([^"]+)",\s*(\d+),/g)) {
        if (functions.has(name)) throw new Error(`Win32Host registers ${name} twice`);
        const params = Array.from({ length: Number(parameters) }, () => 'i32').join(',');
        functions.set(name, `(${params})->${kind === 'function' ? 'i32' : 'void'}`);
    }
    if (functions.size === 0) throw new Error(`no host functions found in ${hostSource}`);
    return functions;
}

export function checkWin32Imports(wasms) {
    const host = hostFunctions();
    const imported = new Set();
    const problems = [];
    for (const wasm of wasms) {
        for (const [name, type] of importsOf(wasm)) {
            imported.add(name);
            const implemented = host.get(name);
            if (implemented === undefined) problems.push(`${wasm} imports ${MODULE}.${name} ${type}, which Win32Host does not register`);
            else if (implemented !== type) problems.push(`${wasm} imports ${MODULE}.${name} as ${type}; Win32Host registers it as ${implemented}`);
        }
    }
    for (const name of host.keys()) {
        if (!imported.has(name)) problems.push(`Win32Host registers ${MODULE}.${name}, which none of ${wasms.join(', ')} imports`);
    }
    if (problems.length > 0) throw new Error(`the modules' Win32 imports and Win32Host disagree:\n  ${problems.join('\n  ')}`);
    return imported.size;
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
    const wasms = process.argv.slice(2);
    if (wasms.length === 0) throw new Error('usage: node scripts/check-win32-imports.mjs <module.wasm> ...');
    console.log(`${checkWin32Imports(wasms)} imports of ${MODULE}, each registered by Win32Host with its signature`);
}
