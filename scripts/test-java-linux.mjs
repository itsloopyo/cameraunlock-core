#!/usr/bin/env node
// Runs java/tests' LinuxTests in a Linux container: the jar's module with PosixHost answering its
// Win32 calls (docs/core-jar.md). The classes are the ones build-java compiled on this machine,
// mounted read-only; the container has no network, and runs as a user that is not root so a
// file's permissions hold. Needs Docker with Linux containers.

import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { CORE_ROOT } from './lib/repos.mjs';

// eclipse-temurin 25.0.4.1_1-jdk-noble, published 2026-10-27, pinned by digest.
const IMAGE = 'eclipse-temurin@sha256:a9c5ddd386f629518bd500ecfabf7d1e5d32d7ffed735d5c3b1604ae92c830f4';
const build = join(CORE_ROOT, 'java', 'build');

// Once in the image's UTF-8 locale, and once under LANG=C, where Java encodes file names as ASCII.
let failed = false;
for (const locale of [[], ['-e', 'LANG=C', '-e', 'LC_ALL=C']]) {
    console.log(locale.length === 0 ? "in the image's locale:" : 'under LANG=C:');
    const result = spawnSync('docker', [
        'run', '--rm', '--network', 'none', '--user', '1000:1000', '--cpus', '2', '--memory', '2g', '-e', 'HOME=/tmp', ...locale,
        '-v', `${build}:/build:ro`, IMAGE,
        'java', '--enable-native-access=ALL-UNNAMED', '-cp', '/build/classes:/build/test-classes:/build/lib/*',
        'com.cameraunlock.core.LinuxTests',
    ], { stdio: 'inherit', timeout: 600000 });
    if (result.error) throw result.error;
    if (result.status !== 0) failed = true;
}
process.exit(failed ? 1 : 0);
