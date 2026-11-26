import { execFileSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';

// Java 22 is the oldest runtime the binding's java.lang.foreign calls run on.
export const JAVAC_FLAGS = ['--release', '22', '-Xlint:all', '-Werror'];

export function run(tool, args, options = {}) {
    execFileSync(tool, args, { stdio: 'inherit', ...options });
}

export function javaSources(directory) {
    return readdirSync(directory, { recursive: true, withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith('.java'))
        .map((entry) => join(entry.parentPath, entry.name));
}
