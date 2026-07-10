#!/usr/bin/env node
// Set (or audit) the DISCORD_RELEASE_WEBHOOK secret on every mod repo that
// needs it. Discovers repos the same way sync-discord-announce.mjs does:
// sibling checkouts of cameraunlock-core whose release workflow references
// DISCORD_RELEASE_WEBHOOK, plus reusable-workflow callers (they inherit the
// announce step from release-bepinex-mod.yml but still need the secret via
// `secrets: inherit`).
//
// By default only repos missing the secret are set, so re-running is cheap.
//
//   node set-discord-webhook-secret.mjs --check         # audit, change nothing
//   node set-discord-webhook-secret.mjs --url <webhook> # set where missing
//   node set-discord-webhook-secret.mjs                 # same, URL from $DISCORD_RELEASE_WEBHOOK
//   node set-discord-webhook-secret.mjs --force ...     # overwrite everywhere (rotation)
//
// --name switches which secret is managed across the same repo set (the
// release pipeline's other secrets ride the same workflows), e.g.:
//
//   node set-discord-webhook-secret.mjs --name LOPARI_APP_ACTIONS_TOKEN --check
//   node set-discord-webhook-secret.mjs --name LOPARI_APP_ACTIONS_TOKEN --value <pat>
//
// Without --value the value comes from the env var named by --name.
// The value is passed to `gh secret set` via stdin, never argv.

import { readFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve, basename } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const siblingsRoot = resolve(__dirname, '..', '..');
const OWNER = 'itsloopyo';

const argv = process.argv.slice(2);
const checkOnly = argv.includes('--check');
const force = argv.includes('--force');
const flagValue = (flag) => {
  const i = argv.indexOf(flag);
  return i !== -1 ? argv[i + 1] : undefined;
};
const secretName = flagValue('--name') ?? 'DISCORD_RELEASE_WEBHOOK';
if (!/^[A-Z][A-Z0-9_]*$/.test(secretName)) {
  console.error(`Invalid secret name: ${secretName}`);
  process.exit(1);
}
const secretValue = flagValue('--value') ?? flagValue('--url') ?? process.env[secretName];

if (!checkOnly && !secretValue) {
  console.error(`No value for ${secretName}: pass --value <v>, set $${secretName}, or use --check to audit.`);
  process.exit(1);
}

function needsSecret(repoPath) {
  const wfDir = join(repoPath, '.github', 'workflows');
  if (!existsSync(wfDir)) return false;
  return readdirSync(wfDir)
    .filter((f) => /\.ya?ml$/i.test(f))
    .some((f) => new RegExp(`${secretName}|release-bepinex-mod\\.yml`).test(readFileSync(join(wfDir, f), 'utf8')));
}

const repos = readdirSync(siblingsRoot)
  .filter((d) => /-head-?tracking$/.test(d))
  .map((d) => join(siblingsRoot, d))
  .filter((p) => {
    try {
      return statSync(p).isDirectory() && needsSecret(p);
    } catch {
      return false;
    }
  })
  .map((p) => basename(p));

console.log(`Repos needing the secret: ${repos.length}`);

const missing = [];
for (const repo of repos) {
  const out = execFileSync('gh', ['secret', 'list', '--repo', `${OWNER}/${repo}`], { encoding: 'utf8' });
  if (new RegExp(`^${secretName}\\b`, 'm').test(out)) {
    console.log(`  set     ${repo}`);
  } else {
    console.log(`  MISSING ${repo}`);
    missing.push(repo);
  }
}

console.log(`\n${missing.length} of ${repos.length} missing ${secretName}`);

if (checkOnly) process.exit(0);

const targets = force ? repos : missing;
if (!targets.length) {
  console.log('Nothing to do.');
  process.exit(0);
}

console.log('');
for (const repo of targets) {
  execFileSync('gh', ['secret', 'set', secretName, '--repo', `${OWNER}/${repo}`], {
    input: secretValue,
    stdio: ['pipe', 'inherit', 'inherit'],
  });
  console.log(`  set ${repo}`);
}
console.log(`\nDone: secret set on ${targets.length} repo(s).`);
