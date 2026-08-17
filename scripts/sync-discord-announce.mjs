#!/usr/bin/env node
// Sync the canonical Discord release steps into every head-tracking mod's
// release workflow:
//
//   - scripts/templates/discord-webhook-check-step.yml -> first step of the
//     release job, so a missing DISCORD_RELEASE_WEBHOOK secret fails the run
//     before anything is built or released.
//   - scripts/templates/catalog-pin-dispatch-step.yml then
//     scripts/templates/discord-announce-step.yml -> in that order,
//     immediately after the "Create GitHub Release" step (anchored on the
//     release-create command). The pin sync blocks until lopari.app pins
//     the new version, so the announce's "Run in Lopari" link is correct
//     the moment the Discord post exists.
//
// Reconciling, not insert-once: a step that exists but no longer matches its
// template is replaced in place, so template edits propagate on re-run.
// Reusable-workflow callers are skipped (they inherit both steps from
// release-bepinex-mod.yml).
//
//   node sync-discord-announce.mjs            # dry run, reports per repo
//   node sync-discord-announce.mjs --apply    # write changes
//
// Scans sibling repos of the cameraunlock-core checkout (../*-headtracking and
// ../*-head-tracking). Pass explicit repo paths as args to limit the scan.

import { readFileSync, writeFileSync, existsSync, readdirSync, statSync, unlinkSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve, basename } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const coreRoot = resolve(__dirname, '..');
const siblingsRoot = resolve(coreRoot, '..');
const apply = process.argv.includes('--apply');
const explicitRepos = process.argv.slice(2).filter((a) => !a.startsWith('--'));

function leadingSpaces(line) {
  const m = line.match(/^( *)/);
  return m[1].length;
}

// A template step, dedented to a zero baseline. Drops the leading comment
// header and surrounding blank lines so only the `- name:` step remains.
function loadTemplateStep(file) {
  const raw = readFileSync(join(coreRoot, 'scripts', 'templates', file), 'utf8')
    .replace(/\r\n/g, '\n')
    .split('\n');
  const start = raw.findIndex((l) => l.trim().startsWith('- name:'));
  if (start === -1) throw new Error(`${file} has no \`- name:\` step`);
  let lines = raw.slice(start);
  while (lines.length && lines[lines.length - 1].trim() === '') lines.pop();
  const base = leadingSpaces(lines[0]);
  return lines.map((l) => (l.trim() === '' ? '' : l.slice(base)));
}

const announceStep = loadTemplateStep('discord-announce-step.yml');
const checkStep = loadTemplateStep('discord-webhook-check-step.yml');
const pinSyncStep = loadTemplateStep('catalog-pin-dispatch-step.yml');
const ANNOUNCE_NAME = 'Announce release to Discord';
const CHECK_NAME = 'Require release announcement prerequisites';
// Prior names a step may still carry in a workflow synced before a rename;
// matched for replacement so the rename propagates instead of duplicating.
const CHECK_LEGACY_NAMES = ['Require Discord release webhook'];
const PIN_SYNC_NAME = 'Sync Lopari catalog pin';

function reindent(stepLines, indent) {
  const pad = ' '.repeat(indent);
  return stepLines.map((l) => (l === '' ? '' : pad + l));
}

// A release workflow is a release/publish-NAMED yaml that triggers on a tag.
// Returns every match (a repo can have several, e.g. release.yml + a variant).
// No fallback to build.yml etc. — an un-anchored build file is not a release.
const ANCHOR_RE = /gh release create|softprops\/action-gh-release|ncipollo\/release-action/;

function findReleaseWorkflows(repoPath) {
  const wfDir = join(repoPath, '.github', 'workflows');
  if (!existsSync(wfDir)) return [];
  return readdirSync(wfDir)
    .filter((f) => /\.ya?ml$/i.test(f) && /release|publish/i.test(f))
    .map((f) => join(wfDir, f))
    .map((p) => ({ path: p, text: readFileSync(p, 'utf8') }))
    .filter((wf) => /tags:/.test(wf.text));
}

// Resolved once, before any workflow is touched. A missing interpreter or a
// missing PyYAML used to surface from validateYaml as an ordinary exception,
// indistinguishable from a genuine parse error: --apply then reverted every
// file it had just written and reported YAML_INVALID_REVERTED for the whole
// fleet. Neither python nor pyyaml is in pixi.toml, so this is the common case
// on a clean environment.
function resolveYamlValidator() {
  for (const exe of ['python', 'python3']) {
    try {
      execFileSync(exe, ['-c', 'import yaml'], { stdio: 'pipe' });
      return exe;
    } catch (e) {
      if (e.code === 'ENOENT') continue;
      throw new Error(
        `\`${exe}\` is on PATH but cannot import PyYAML, which this script needs to validate every workflow it writes. Install it (\`pip install pyyaml\`) and re-run.\n${String(e.stderr || e)}`,
      );
    }
  }
  throw new Error(
    'No python interpreter found on PATH. This script validates every workflow it writes with python + PyYAML, neither of which is a declared dependency in pixi.toml. Install python and PyYAML, then re-run.',
  );
}

const pythonExe = resolveYamlValidator();

function validateYaml(path) {
  execFileSync(pythonExe, ['-c', 'import sys,yaml; yaml.safe_load(open(sys.argv[1],encoding="utf-8"))', path], {
    stdio: 'pipe',
  });
}

function findStepHeaders(lines) {
  const headers = [];
  for (let i = 0; i < lines.length; i++) {
    if (/^ *- name:/.test(lines[i])) headers.push({ i, indent: leadingSpaces(lines[i]), name: lines[i].replace(/^ *- name:\s*/, '').trim() });
  }
  return headers;
}

// End of the step block starting at header index `start`: the next step header
// at the same indent, or the first non-blank line indented less than the
// header (next job/key), whichever comes first. Trailing blank separator
// lines are excluded so they survive a replacement.
function stepBlockEnd(lines, start, indent) {
  let end = lines.length;
  for (let i = start + 1; i < lines.length; i++) {
    const line = lines[i];
    if (line.trim() === '') continue;
    const li = leadingSpaces(line);
    if (li < indent || (li === indent && /^ *- /.test(line))) {
      end = i;
      break;
    }
  }
  while (end > start + 1 && lines[end - 1].trim() === '') end--;
  return end;
}

function blocksEqual(a, b) {
  if (a.length !== b.length) return false;
  return a.every((l, i) => l.trimEnd() === b[i].trimEnd());
}

function processRepo(repoPath) {
  const name = basename(repoPath);
  const wfs = findReleaseWorkflows(repoPath);
  if (!wfs.length) return [{ name, status: 'NO_RELEASE_WF' }];
  return wfs.map((wf) => processWorkflowFile(name, wf));
}

function processWorkflowFile(repoName, wf) {
  const name = `${repoName}/${basename(wf.path)}`;
  if (/release-bepinex-mod\.yml/.test(wf.text)) return { name, status: 'REUSABLE_CALLER' };

  const eol = wf.text.includes('\r\n') ? '\r\n' : '\n';
  const lines = wf.text.replace(/\r\n/g, '\n').split('\n');
  const headers = findStepHeaders(lines);
  if (!headers.length) return { name, status: 'NO_STEPS' };

  // The release-create step anchors everything: the announce step goes after
  // it, and the check step goes at the top of the job that contains it.
  let anchor = null;
  for (let h = 0; h < headers.length; h++) {
    const start = headers[h].i;
    const end = h + 1 < headers.length ? headers[h + 1].i : lines.length;
    if (lines.slice(start, end).some((l) => ANCHOR_RE.test(l))) {
      anchor = { headerIndex: start, indent: headers[h].indent, blockEnd: end };
    }
  }
  if (!anchor) return { name, status: 'NO_RELEASE_STEP', wf: wf.path };

  // Edits collected as {at, remove, insert} and applied bottom-up so earlier
  // line indexes stay valid.
  const edits = [];
  const ops = [];

  // The pin-sync + announce pair is normalized as a unit: pin-sync MUST
  // precede announce (the announce's link is only correct once the pin is
  // live), so any deviation in content OR order removes both existing
  // blocks and re-inserts the canonical sequence after the release step.
  const announceHeader = headers.find((h) => h.name === ANNOUNCE_NAME);
  const pinSyncHeader = headers.find((h) => h.name === PIN_SYNC_NAME);
  const pairCurrent =
    announceHeader &&
    pinSyncHeader &&
    pinSyncHeader.i < announceHeader.i &&
    blocksEqual(
      lines.slice(announceHeader.i, stepBlockEnd(lines, announceHeader.i, announceHeader.indent)),
      reindent(announceStep, announceHeader.indent),
    ) &&
    blocksEqual(
      lines.slice(pinSyncHeader.i, stepBlockEnd(lines, pinSyncHeader.i, pinSyncHeader.indent)),
      reindent(pinSyncStep, pinSyncHeader.indent),
    );
  if (!pairCurrent) {
    for (const h of [announceHeader, pinSyncHeader].filter(Boolean)) {
      let start = h.i;
      const end = stepBlockEnd(lines, start, h.indent);
      // Absorb the blank separator above the block so removal+reinsert
      // doesn't accumulate blank lines run over run.
      if (start > 0 && lines[start - 1].trim() === '') start--;
      edits.push({ at: start, remove: end - start, insert: [] });
    }
    let insertAt = anchor.blockEnd;
    while (insertAt > 0 && lines[insertAt - 1].trim() === '') insertAt--;
    edits.push({
      at: insertAt,
      remove: 0,
      insert: ['', ...reindent(pinSyncStep, anchor.indent), '', ...reindent(announceStep, anchor.indent)],
    });
    ops.push(announceHeader || pinSyncHeader ? 'release-steps:normalized' : 'release-steps:inserted');
  }

  const checkHeader = headers.find((h) => h.name === CHECK_NAME || CHECK_LEGACY_NAMES.includes(h.name));
  if (checkHeader) {
    const end = stepBlockEnd(lines, checkHeader.i, checkHeader.indent);
    const rendered = reindent(checkStep, checkHeader.indent);
    if (!blocksEqual(lines.slice(checkHeader.i, end), rendered)) {
      edits.push({ at: checkHeader.i, remove: end - checkHeader.i, insert: rendered });
      ops.push('check:updated');
    }
  } else {
    // First step of the job containing the anchor: the first step header after
    // the last `steps:` line above the anchor.
    let stepsLine = -1;
    for (let i = anchor.headerIndex - 1; i >= 0; i--) {
      if (/^\s*steps:\s*$/.test(lines[i])) {
        stepsLine = i;
        break;
      }
    }
    if (stepsLine === -1) return { name, status: 'NO_STEPS_KEY', wf: wf.path };
    const firstStep = headers.find((h) => h.i > stepsLine);
    edits.push({ at: firstStep.i, remove: 0, insert: [...reindent(checkStep, firstStep.indent), ''] });
    ops.push('check:inserted');
  }

  if (!edits.length) return { name, status: 'CURRENT' };

  // Bottom-up, and at equal indexes removals before inserts: the canonical
  // pair is inserted exactly where a removed block started, and inserting
  // first would put the new lines inside the removal range.
  edits.sort((a, b) => b.at - a.at || b.remove - a.remove);
  let newLines = lines;
  for (const e of edits) {
    newLines = [...newLines.slice(0, e.at), ...e.insert, ...newLines.slice(e.at + e.remove)];
  }
  const out = newLines.join(eol);
  const opsLabel = ops.sort().join(',');

  if (apply) {
    writeFileSync(wf.path, out, 'utf8');
    try {
      validateYaml(wf.path);
    } catch (e) {
      writeFileSync(wf.path, wf.text, 'utf8'); // revert
      return { name, status: 'YAML_INVALID_REVERTED', error: String(e.stderr || e), wf: wf.path };
    }
    return { name, status: 'PATCHED', ops: opsLabel, wf: wf.path };
  }

  // Dry run: validate a temp render without touching the file.
  const tmp = wf.path + '.discord-dryrun';
  writeFileSync(tmp, out, 'utf8');
  let yamlOk = true;
  let yamlErr = '';
  try {
    validateYaml(tmp);
  } catch (e) {
    yamlOk = false;
    yamlErr = String(e.stderr || e);
  } finally {
    unlinkSync(tmp);
  }
  return { name, status: yamlOk ? 'WOULD_PATCH' : 'WOULD_FAIL_YAML', ops: opsLabel, error: yamlErr, wf: wf.path };
}

function discoverRepos() {
  if (explicitRepos.length) return explicitRepos.map((p) => resolve(p));
  return readdirSync(siblingsRoot)
    .filter((d) => /-head-?tracking$/.test(d))
    .map((d) => join(siblingsRoot, d))
    .filter((p) => {
      try {
        return statSync(p).isDirectory();
      } catch {
        return false;
      }
    });
}

const repos = discoverRepos();
const results = repos.flatMap(processRepo);

const byStatus = {};
for (const r of results) (byStatus[r.status] ||= []).push(r);

console.log(`\nMode: ${apply ? 'APPLY' : 'DRY RUN'}   Repos scanned: ${repos.length}\n`);
for (const status of Object.keys(byStatus).sort()) {
  const rs = byStatus[status];
  console.log(`== ${status} (${rs.length}) ==`);
  for (const r of rs) {
    let extra = '';
    if (r.ops) extra += ` [${r.ops}]`;
    if (r.error) extra += ` ${r.error.split('\n')[0]}`;
    console.log(`   ${r.name}${extra}`);
  }
  console.log('');
}

// The script previously always exited 0, so a run across the fleet that failed to patch
// several repos reported success to whatever invoked it - and those mods then shipped
// releases with no Discord announcement, the exact failure the webhook check exists to
// prevent. REUSABLE_CALLER is not a failure: those repos get the step from the shared
// workflow. NO_STEPS_KEY / NO_RELEASE_STEP / NO_RELEASE_WF mean the repo was not
// reconciled and needs a look.
const FAILING = new Set([
  'NO_RELEASE_WF',
  'NO_RELEASE_STEP',
  'NO_STEPS',
  'NO_STEPS_KEY',
  'YAML_INVALID_REVERTED',
  'WOULD_FAIL_YAML',
]);

const failed = results.filter((r) => FAILING.has(r.status));
if (failed.length > 0) {
  console.log(`${failed.length} repo(s) need attention: ${failed.map((r) => r.name).join(', ')}`);
  process.exit(1);
}

