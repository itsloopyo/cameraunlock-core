#!/usr/bin/env node
// Holds the code in docs/*.md to the compiled tests it is taken from, so an example cannot rot.
//
// A fenced block whose opening fence line is directly preceded by one of these lines is checked:
//
//   <!-- excerpt: <repo path> -->  the block's lines are a run of consecutive lines of that file,
//                                  compared after taking the run's common indentation off
//   <!-- file: <repo path> -->     the block is that whole file, CRLF read as LF, without its
//                                  final newline
//
// Every ```cpp and ```csharp block must carry one: code in those languages is only shown in docs
// when a test compiles and runs it. Every other fence must be tagged ini or text, so C++ or C#
// cannot get past this check under another tag (cs, c++) or none. Fences may be indented, as in
// a list item.
//
//   node scripts/check-doc-examples.mjs

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const DOCS = path.join(ROOT, 'docs');
const MARKER = /^<!-- (excerpt|file): (\S+) -->$/;
const CHECKED_LANGUAGES = new Set(['cpp', 'csharp']);
const UNCHECKED_LANGUAGES = new Set(['ini', 'text']);

function readLf(file) {
  return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
}

function indentOf(line) {
  return line.length - line.trimStart().length;
}

function dedent(lines) {
  const indents = lines.filter((l) => l.trim() !== '').map(indentOf);
  const common = indents.length === 0 ? 0 : Math.min(...indents);
  return lines.map((l) => (l.trim() === '' ? '' : l.slice(common)));
}

function isExcerpt(block, source) {
  for (let start = 0; start + block.length <= source.length; start++) {
    const run = dedent(source.slice(start, start + block.length));
    if (run.every((line, i) => line === block[i])) return true;
  }
  return false;
}

function fences(lines) {
  const found = [];
  for (let i = 0; i < lines.length; i++) {
    const open = /^( *)(`{3,}|~{3,})(.*)$/.exec(lines[i]);
    if (!open) continue;
    const [, indent, fence, info] = open;
    const closing = new RegExp(`^ *${fence[0] === '`' ? '`' : '~'}{${fence.length},} *$`);
    const close = lines.findIndex((l, j) => j > i && closing.test(l));
    if (close < 0) throw new Error(`line ${i + 1}: a code fence that is never closed`);
    const body = lines.slice(i + 1, close).map((l) => l.slice(Math.min(indent.length, indentOf(l))));
    const language = info.trim().split(/\s+/)[0].toLowerCase();
    const marker = i > 0 ? MARKER.exec(lines[i - 1].trim()) : null;
    found.push({ line: i + 1, language, marker, body });
    i = close;
  }
  return found;
}

const problems = [];
let checked = 0;
for (const name of fs.readdirSync(DOCS).filter((n) => n.endsWith('.md')).sort()) {
  const doc = `docs/${name}`;
  for (const fence of fences(readLf(path.join(DOCS, name)).split('\n'))) {
    const where = `${doc}:${fence.line}`;
    if (!CHECKED_LANGUAGES.has(fence.language) && !UNCHECKED_LANGUAGES.has(fence.language)) {
      problems.push(`${where}: ${fence.language === '' ? 'an untagged block' : `a block tagged ${fence.language}`}; tag code cpp or csharp and name the test it comes from, and anything else ini or text`);
      continue;
    }
    if (!fence.marker) {
      if (CHECKED_LANGUAGES.has(fence.language)) {
        problems.push(`${where}: a ${fence.language} block with no <!-- excerpt: ... --> line above it; show code a test compiles, and name the test`);
      }
      continue;
    }
    const [, kind, source] = fence.marker;
    const file = path.join(ROOT, ...source.split('/'));
    if (!fs.existsSync(file)) {
      problems.push(`${where}: names ${source}, which does not exist`);
      continue;
    }
    const text = readLf(file);
    if (kind === 'file') {
      const whole = text.endsWith('\n') ? text.slice(0, -1) : text;
      if (fence.body.join('\n') !== whole) problems.push(`${where}: the block is not ${source} as it is now`);
    } else if (fence.body.length === 0 || !isExcerpt(fence.body, text.split('\n'))) {
      problems.push(`${where}: the block is not a run of lines of ${source} as it is now`);
    }
    checked++;
  }
}

for (const problem of problems) console.error(problem);
if (problems.length > 0) process.exit(1);
console.log(checked === 1 ? '1 doc example matches its source.' : `${checked} doc examples match their sources.`);
