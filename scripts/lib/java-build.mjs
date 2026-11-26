import { execFileSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';

// Java 25 is the oldest runtime core's Java runs on: java.lang.foreign is final from 22 and
// java.lang.classfile from 24.
export const JAVAC_FLAGS = ['--release', '25', '-Xlint:all', '-Werror'];

export function run(tool, args, options = {}) {
    execFileSync(tool, args, { stdio: 'inherit', ...options });
}

export function javaSources(directory) {
    return readdirSync(directory, { recursive: true, withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith('.java'))
        .map((entry) => join(entry.parentPath, entry.name));
}
