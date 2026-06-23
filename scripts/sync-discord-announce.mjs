#!/usr/bin/env node
// Sync the canonical Discord release-announce step (scripts/templates/
// discord-announce-step.yml) into every head-tracking mod's release workflow.
//
// Inserts the step immediately after the "Create GitHub Release" step (anchored
// on the `gh release create` command), re-indented to match the target file's
// step indentation. Idempotent: a workflow that already references
// DISCORD_RELEASE_WEBHOOK is left untouched. Reusable-workflow callers are
// skipped (they inherit the step from release-bepinex-mod.yml).
//
//   node sync-discord-announce.mjs            # dry run, reports per repo
//   node sync-discord-announce.mjs --apply    # write changes
//
// Scans sibling repos of the cameraunlock-core checkout (../*-headtracking and
// ../*-head-tracking). Pass explicit repo paths as args to limit the scan.

import { readFileSync, writeFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve, basename } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const coreRoot = resolve(__dirname, '..');
const siblingsRoot = resolve(coreRoot, '..');
const apply = process.argv.includes('--apply');
const explicitRepos = process.argv.slice(2).filter((a) => !a.startsWith('--'));

const templatePath = join(coreRoot, 'scripts', 'templates', 'discord-announce-step.yml');

function leadingSpaces(line) {
  const m = line.match(/^( *)/);
  return m[1].length;
}

// The template step, dedented to a zero baseline. Drops the leading comment
// header and surrounding blank lines so only the `- name:` step remains.
function loadTemplateStep() {
  const raw = readFileSync(templatePath, 'utf8').replace(/\r\n/g, '\n').split('\n');
  const start = raw.findIndex((l) => l.trim().startsWith('- name:'));
  if (start === -1) throw new Error('template has no `- name:` step');
  let lines = raw.slice(start);
  while (lines.length && lines[lines.length - 1].trim() === '') lines.pop();
  const base = leadingSpaces(lines[0]);
  return lines.map((l) => (l.trim() === '' ? '' : l.slice(base)));
}

const templateStep = loadTemplateStep();

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

function validateYaml(path) {
  execFileSync('python', ['-c', 'import sys,yaml; yaml.safe_load(open(sys.argv[1],encoding="utf-8"))', path], {
    stdio: 'pipe',
  });
}

function processRepo(repoPath) {
  const name = basename(repoPath);
  const wfs = findReleaseWorkflows(repoPath);
  if (!wfs.length) return [{ name, status: 'NO_RELEASE_WF' }];
  return wfs.map((wf) => processWorkflowFile(name, wf));
}

function processWorkflowFile(repoName, wf) {
  const name = `${repoName}/${basename(wf.path)}`;
  if (/DISCORD_RELEASE_WEBHOOK/.test(wf.text)) return { name, status: 'ALREADY' };
  if (/release-bepinex-mod\.yml/.test(wf.text)) return { name, status: 'REUSABLE_CALLER' };

  const eol = wf.text.includes('\r\n') ? '\r\n' : '\n';
  const lines = wf.text.replace(/\r\n/g, '\n').split('\n');

  // Locate every `- name:` step header, then the one whose body runs
  // `gh release create` — that is the anchor we insert after.
  const headers = [];
  for (let i = 0; i < lines.length; i++) {
    if (/^ *- name:/.test(lines[i])) headers.push({ i, indent: leadingSpaces(lines[i]) });
  }
  if (!headers.length) return { name, status: 'NO_STEPS' };

  let anchor = null;
  for (let h = 0; h < headers.length; h++) {
    const start = headers[h].i;
    const end = h + 1 < headers.length ? headers[h + 1].i : lines.length;
    if (lines.slice(start, end).some((l) => ANCHOR_RE.test(l))) {
      anchor = { indent: headers[h].indent, blockEnd: end };
    }
  }
  if (!anchor) return { name, status: 'NO_RELEASE_STEP', wf: wf.path };

  // blockEnd may include trailing blank lines that belong before the next
  // sibling; back up over them so the new step sits flush after the anchor.
  let insertAt = anchor.blockEnd;
  while (insertAt > 0 && lines[insertAt - 1].trim() === '') insertAt--;

  const step = reindent(templateStep, anchor.indent);
  const newLines = [...lines.slice(0, insertAt), '', ...step, ...lines.slice(insertAt)];
  const out = newLines.join(eol);

  if (apply) {
    writeFileSync(wf.path, out, 'utf8');
    try {
      validateYaml(wf.path);
    } catch (e) {
      writeFileSync(wf.path, wf.text, 'utf8'); // revert
      return { name, status: 'YAML_INVALID_REVERTED', error: String(e.stderr || e), wf: wf.path };
    }
    return { name, status: 'PATCHED', indent: anchor.indent, wf: wf.path };
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
  }
  execFileSync('node', ['-e', `require('fs').unlinkSync(${JSON.stringify(tmp)})`]);
  return { name, status: yamlOk ? 'WOULD_PATCH' : 'WOULD_FAIL_YAML', indent: anchor.indent, error: yamlErr, wf: wf.path };
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
    if (r.indent != null) extra += ` [indent ${r.indent}]`;
    if (r.error) extra += ` ${r.error.split('\n')[0]}`;
    console.log(`   ${r.name}${extra}`);
  }
  console.log('');
}
