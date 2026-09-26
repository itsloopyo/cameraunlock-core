#!/usr/bin/env node
//
// Render the README sections core owns, and report where a repo's copy has
// drifted from them.
//
// Why this exists: 88 repos hand-wrote the same OpenTrack setup paragraph 88
// times. Of 4,433 substantive README lines across the fleet, 14 appeared
// verbatim in 25 or more repos and none in 40, while the H2 skeleton was
// identical in 34. The instruction "use OpenTrack's neuralnet tracker with a
// webcam" existed in 25 distinct phrasings. Every claim about kit we do not
// own and have not tested entered the fleet through one of those rewrites:
// "any OpenTrack-compatible tracker", "phone trackers all speak the same
// protocol", "keep the headset on your head, not on the desk". The paragraph
// that says what the mod accepts has one correct wording, so it is written
// once, here, and rendered.
//
// What is NOT generated is as deliberate as what is. Everything else stays
// hand-written, because each of those sections is a claim about one game, one
// engine or one publisher that somebody had to verify. Rendering them from a
// data file would mean inventing the claim from a game id, which is the exact
// failure this file exists to stop. The reasoning per section is at
// DEFAULT_WRITE below.
//
//   node scripts/generate-readme.mjs                     # check this repo
//   node scripts/generate-readme.mjs --all               # check the fleet
//   node scripts/generate-readme.mjs --all --write
//   node scripts/generate-readme.mjs valheim subnautica
//   node scripts/generate-readme.mjs --all --sections opentrack
//   node scripts/generate-readme.mjs --print opentrack
//   node scripts/generate-readme.mjs --print config valheim   # the block for NEXUS_MODS.md
//   node scripts/generate-readme.mjs --all --write --force
//   node scripts/generate-readme.mjs --json --sections config --roots-file <file>
//
// --write leaves alone any section that has grown a subsection the generator
// does not render, because that subsection is something a repo verified and
// nothing else records. --force overwrites it anyway.
//
// The config block is the one piece inside a hand-written section: it sits
// between CONFIG_START and CONFIG_END in Configuration and is rendered from
// data/config-format.json and the repo's committed canonical config, so a repo
// has one once it is converted and none before.
//
// Exit 0 when every checked section matches, 1 when any differs or a config
// block cannot be rendered. --write rewrites the sections in place and exits 0
// unless a config block could not be rendered. --json prints each repo's result
// per section for scripts/conformance.ps1 and always exits 0; --roots-file takes
// the repo paths one per line.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { repoState } from './check-canonical-config.mjs';
import { equalsAsciiIgnoreCase, isDefaultToken, parseCanonicalIni } from './lib/canonical-ini.mjs';

const CORE_ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const REPOS_ROOT = path.dirname(CORE_ROOT);

// ---------------------------------------------------------------------------
// Facts. Every number here is read out of core, not remembered.
//
//   port           udp_receiver.h, the default Start(port) argument
//   binds 0.0.0.0  udp_socket.cpp:86, INADDR_ANY
//   48 bytes       opentrack_packet.h kMinPacketSize, offsets 0/8/16/24/32/40
//   cm and degrees opentrack_packet.h, TryParsePosition multiplies kCmToMeters
//   loopback only  socket_types.h IsRemoteAddress: 127.0.0.0/8 is local, and
//                  everything else, including this machine's own LAN address,
//                  is remote and gets RemoteSmoothing instead of LocalSmoothing
//   no centring    AGENTS.md: the tracker owns the centre, the pipeline's
//                  centre is identity, no mod registers a recenter hotkey
// ---------------------------------------------------------------------------
const DEFAULT_PORT = '4242';

const OPENTRACK_HEADING = 'Setting Up OpenTrack';

// Headings whose whole section this file replaces with the OpenTrack block. The
// fleet spells it six ways and several repos split the phone half into its own
// H2, so both are absorbed into one section.
// A leading `3. ` is allowed because sleeping-dogs numbers its whole README as a
// sequence of steps; that repo's heading is kept rather than renamed, or the
// numbering it depends on would break.
const OPENTRACK_HEADING_RE =
  /^(\d+\.\s+)?(setting up opentrack|opentrack setup|open ?track setup|head tracking setup|tracking setup|configure tracking input|phone[- ]?app setup|phone setup|setting up your tracker)$/i;

function openTrackSection({ port }) {
  return `The mod listens for OpenTrack pose data on UDP port \`${port}\`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
\`x, y, z, yaw, pitch, roll\`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host \`127.0.0.1\`, port \`${port}\`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a \`neuralnet tracker\` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run \`ipconfig\`
to find it) on port \`${port}\`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
\`127.0.0.1:${port}\`.

Anything arriving from outside \`127.0.0.0/8\` counts as a remote connection and
is smoothed with \`RemoteSmoothing\` rather than \`LocalSmoothing\`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.`;
}

function communitySection() {
  return `- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker`;
}

const SECTIONS = {
  opentrack: { heading: OPENTRACK_HEADING, match: OPENTRACK_HEADING_RE, render: openTrackSection },
  community: { heading: 'Community & Support', match: /^community (&|and) support$/i, render: communitySection },
};

// ---------------------------------------------------------------------------
// The config block
//
// Every sentence is a fact data/config-format.json records or core's config
// owner does for every converted mod: the installed path, the one-time import
// of the legacy file in the same folder, which the mod never writes, what an
// older build reads, and for BepInEx that ConfigurationManager does not list the
// settings. A committed file with default rows adds what the owner does with
// Defaults.ini and the built-in value of each such row. Nothing here knows which
// game it is describing.
// ---------------------------------------------------------------------------

const CONFIG_ID = 'config';
const CONFIG_START = '<!-- cameraunlock:config -->';
const CONFIG_END = '<!-- /cameraunlock:config -->';
const CONFIG_HEADING = 'Configuration';
const CONFIG_HEADING_RE = /^(\d+\.\s+)?configuration$/i;
const CONFIG_INSERT_AFTER = ['Controls', OPENTRACK_HEADING, 'Installation'];

const FORMAT = JSON.parse(fs.readFileSync(path.join(CORE_ROOT, 'data', 'config-format.json'), 'utf8'));
const CANONICAL_CONCEPTS = JSON.parse(fs.readFileSync(path.join(CORE_ROOT, 'data', 'config-schema.json'), 'utf8'))
  .concepts.filter((c) => c.canonical);

const CONFIG_NAME = 'CameraUnlock.ini';
const DEFAULTS_NAME = 'Defaults.ini';

const code = (p) => `\`${p}\``;
const EDIT = 'Edit it with any text editor.';

// One committed file can stand for several config entries (mass-effect keeps one per game),
// and each entry lists one installed path per store layout.
function locationParagraph(entries) {
  if (entries.length === 1) {
    const [{ installed, no_installed_reason: reason }] = entries;
    if (installed.length === 0) {
      return `The mod reads its settings from ${code(CONFIG_NAME)}. ${reason} It creates the file when it starts and finds none. ${EDIT}`;
    }
    if (installed.length === 1) {
      return `The mod reads its settings from ${code(installed[0])} in the game folder, and creates the file when it starts and finds none. ${EDIT}`;
    }
    return [
      `The mod reads its settings from ${code(CONFIG_NAME)} in the game folder, at one of these paths depending on the store the game came from:`,
      '',
      ...installed.map((p) => `- ${code(p)}`),
      '',
      `It creates the file when it starts and finds none. ${EDIT}`,
    ].join('\n');
  }
  if (entries.some((e) => e.installed.length === 0)) {
    throw new Error(`data/config-format.json gives ${CONFIG_NAME} several entries, one of them with no installed path; the config block has no wording for that`);
  }
  const alternatives = entries.some((e) => e.installed.length > 1);
  return [
    `The mod keeps a separate ${code(CONFIG_NAME)} at each of these paths in the game folder:`,
    '',
    ...entries.map((e) => `- ${e.installed.map(code).join(' or ')}`),
    '',
    `${alternatives ? 'Where a line names two paths, which one is used depends on the store the game came from. ' : ''}The mod creates each file when it starts and finds none. Edit them with any text editor.`,
  ].join('\n');
}

// Settings the conversion drops although the mod read them, one line per approved_changes entry.
// An entry with no line here stops the block rendering, so a newly approved change cannot reach
// players' files without the README saying so. scripts/templates/canonical-config-changelog.md
// repeats these lines and the paragraphs below for each conversion's changelog; change both.
const APPROVED_CHANGE_LINES = {
  pose_shaping: 'A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.',
  reticle: 'Reticle settings, and a key that toggled the reticle.',
  follows_default: "The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.",
};

function droppedSettings() {
  const lines = Object.keys(FORMAT.approved_changes).map((id) => {
    if (!(id in APPROVED_CHANGE_LINES)) {
      throw new Error(`data/config-format.json approved_changes.${id} has no line in the config block; add one to APPROVED_CHANGE_LINES in scripts/generate-readme.mjs`);
    }
    return `- ${APPROVED_CHANGE_LINES[id]}`;
  });
  return ['Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:', '', ...lines].join('\n');
}

// legacy is the bare name of the file the repo's pre-canonical builds read, in the same folder.
// defaults is what defaultRows gives for the committed file.
function legacyParagraphs(legacy, defaults) {
  const old = code(legacy);
  const config = code(CONFIG_NAME);
  const followed = code(DEFAULTS_NAME);
  const imported = [
    `Earlier versions of the mod kept these settings in ${old}, in the same folder. The first time this version starts and finds no ${config}, it reads your settings from ${old} and writes them into ${config}. It never changes ${old}, and does not read it again while ${config} exists.`,
  ];
  let reset = `Deleting only ${config} makes the next start read ${old} again. To go back to the defaults, replace everything in ${config} with the defaults below.`;
  if (defaults.length > 0) {
    const keys = new Set(defaults.map((r) => r.key));
    const pair = keys.has('RotationEnabled') && keys.has('PositionEnabled')
      ? ' `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.'
      : '';
    imported.push(`A setting that the defaults below set to \`default\` is written as \`default\` when the value imported for it equals its default at that start, which is the value ${followed} gives it, or the built-in value where ${followed} gives none. It then follows ${followed}. Every other setting is written with the value imported for it.${pair}`);
    reset += ` Every setting they set to \`default\` then follows ${followed}.`;
  }
  return [
    ...imported,
    droppedSettings(),
    `An older version of the mod reads ${old} and never reads ${config}, so a setting you change after updating is not in ${old}.`,
    reset,
  ];
}

// What core's config owners do with Defaults.ini. csharp: the owner is core's C# one, the only
// owner that runs natively on Linux and macOS, where it reads and never writes.
function defaultsParagraphs(legacy, csharp) {
  const followed = code(DEFAULTS_NAME);
  const config = code(CONFIG_NAME);
  return [
    `A setting set to \`default\` takes its value from ${followed}, which every head tracking mod that keeps its settings in ${config} reads. Head tracking mods that keep their settings in another file do not read it${legacy ? ', and neither do earlier versions of this mod' : ''}. Writing a value in place of \`default\` changes that setting for this game only.`,
    `${followed} is ${code('%AppData%\\CameraUnlock\\Defaults.ini')} on Windows; ${code('$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini')} on Linux, or ${code('~/.config/CameraUnlock/Defaults.ini')} where \`XDG_CONFIG_HOME\` is not set, under Wine and Proton too; and ${code('~/Library/Application Support/CameraUnlock/Defaults.ini')} on macOS. The mod's log, where it writes one, names the file it read.`,
    `When the mod starts and finds no ${followed}, it creates one holding the built-in values, unless Windows runs the game as a packaged app${csharp ? ', or the game runs on Linux or macOS without Wine or Proton' : ''}. The mod never changes ${followed} after that. ${EDIT}`,
    ...(csharp ? ['On Linux and macOS without Wine or Proton, this version reads its settings and saves none, so a change made in game lasts until the game closes.'] : []),
  ];
}

// Floats as the canonical codec writes them, which data/fixtures/canonical-ini/global/Defaults.ini
// shows; scripts/test-generate-readme.mjs holds every concept to that file.
function builtInText(concept) {
  if (concept.codec === 'hotkey') return concept.canonical_default;
  if (concept.type === 'float' && Number.isInteger(concept.default)) return concept.default.toFixed(1);
  return String(concept.default);
}

// Each row the committed file sets to default, in file order, with its built-in value.
function defaultRows(root, committed) {
  const doc = parseCanonicalIni(fs.readFileSync(path.join(root, ...committed.split('/'))));
  return doc.sections.flatMap((section) => section.values.filter((v) => isDefaultToken(v.value)).map((v) => {
    const concept = CANONICAL_CONCEPTS.find((c) => equalsAsciiIgnoreCase(c.section, section.name) && equalsAsciiIgnoreCase(c.key, v.key));
    if (concept === undefined) {
      throw new Error(`${committed} line ${v.line}: [${section.name}] ${v.key} holds ${v.value}, which only a canonical concept's row takes`);
    }
    return { key: concept.key, text: builtInText(concept) };
  }));
}

function builtInList(defaults) {
  return ['The built-in value of each setting set to `default` below:', '', ...defaults.map((r) => `- ${code(`${r.key}=${r.text}`)}`)].join('\n');
}

// data/config-format.json records dialect unity for exactly the repos whose owner is core's C# one
// (checked against the sources of every checkout on 2026-09-26).
function csharpOwner(files) {
  const dialects = new Set(files.map((f) => f.dialect));
  if (dialects.size !== 1) {
    throw new Error(`data/config-format.json gives one repo's config files the dialects ${[...dialects].join(' and ')}; the config block needs one owner language`);
  }
  return dialects.has('unity');
}

const isBepInEx = (entry) => entry.installed.some((p) => p.toLowerCase().startsWith('bepinex\\config\\'));

function fencedIni(root, committed) {
  const text = fs.readFileSync(path.join(root, ...committed.split('/')), 'utf8').replace(/\r\n/g, '\n');
  if (/^```/m.test(text)) throw new Error(`${committed} has a line starting with \`\`\`, which would end the README's code fence`);
  return ['```ini', text.endsWith('\n') ? text.slice(0, -1) : text, '```'].join('\n');
}

// The block between the markers, or null for a repo that is not converted. A converted repo
// whose config cannot be rendered yet (a file unstamped or unrecorded) throws; config-format in
// conformance reports the same state.
export function configBlock(state) {
  if (!state.converted) return null;
  if (state.unrecorded_stamped.length > 0) {
    throw new Error(`${state.unrecorded_stamped.join(', ')} carries the [CameraUnlock] stamp, and data/config-format.json records no committed file for it`);
  }
  const unready = state.files.filter((f) => f.state !== 'stamped');
  if (unready.length > 0) {
    throw new Error(`converted, and ${unready.map((f) => f.committed ?? f.installed[0]).join(', ')} is ${unready[0].state}; one conversion switches every config file of a repo`);
  }

  const legacy = state.listing === 'legacy';
  const groups = new Map();
  for (const f of state.files) {
    if (!groups.has(f.committed)) groups.set(f.committed, []);
    groups.get(f.committed).push(f);
  }
  const parts = [];
  let explained = false;
  for (const [committed, entries] of groups) {
    const bepinex = entries.some(isBepInEx);
    if (entries.length > 1 && (legacy || bepinex)) {
      throw new Error(`data/config-format.json gives ${committed} several entries in a legacy or BepInEx repo; the config block has no wording for that`);
    }
    const defaults = defaultRows(state.root, committed);
    parts.push(locationParagraph(entries));
    if (defaults.length > 0 && !explained) {
      parts.push(...defaultsParagraphs(legacy, csharpOwner(state.files)));
      explained = true;
    }
    if (legacy) parts.push(...legacyParagraphs(entries[0].legacy_source, defaults));
    if (bepinex) parts.push(`BepInEx's ConfigurationManager ${legacy ? 'no longer lists' : 'does not list'} these settings.`);
    if (defaults.length > 0) parts.push(builtInList(defaults));
    parts.push('With every setting at its default, the file reads:');
    parts.push(fencedIni(state.root, committed));
  }
  return parts.join('\n\n');
}

function findConfigMarkers(doc) {
  if (doc.preamble.some((line) => line === CONFIG_START || line === CONFIG_END)) {
    throw new Error(`README.md has a config block marker above its first section; the block belongs in ## ${CONFIG_HEADING}`);
  }
  const found = [];
  doc.blocks.forEach((b, blockIndex) => {
    b.body.forEach((line, lineIndex) => {
      if (line === CONFIG_START || line === CONFIG_END) found.push({ blockIndex, lineIndex, line });
    });
  });
  return found;
}

// 'unchanged', 'rewritten', 'inserted' or 'removed'; throws on markers it cannot pair.
function applyConfigBlock(doc, rendered) {
  const markers = findConfigMarkers(doc);
  if (markers.length > 0) {
    const [start, end] = markers;
    if (markers.length !== 2 || start.line !== CONFIG_START || end.line !== CONFIG_END || start.blockIndex !== end.blockIndex) {
      throw new Error(`README.md needs exactly one ${CONFIG_START} line followed by one ${CONFIG_END} line in the same section`);
    }
    if (!CONFIG_HEADING_RE.test(doc.blocks[start.blockIndex].heading)) {
      throw new Error(`README.md has its config block under ## ${doc.blocks[start.blockIndex].heading}; the block belongs in ## ${CONFIG_HEADING}`);
    }
    const body = doc.blocks[start.blockIndex].body;
    if (rendered === null) {
      body.splice(start.lineIndex, end.lineIndex - start.lineIndex + 1);
      return 'removed';
    }
    const current = body.slice(start.lineIndex + 1, end.lineIndex).join('\n');
    if (current === rendered) return 'unchanged';
    body.splice(start.lineIndex + 1, end.lineIndex - start.lineIndex - 1, ...rendered.split('\n'));
    return 'rewritten';
  }
  if (rendered === null) return 'unchanged';

  const lines = [CONFIG_START, ...rendered.split('\n'), CONFIG_END];
  const at = doc.blocks.findIndex((b) => CONFIG_HEADING_RE.test(b.heading));
  if (at >= 0) {
    // Before the section's first ### subsection: a block appended at the end would read as part
    // of whatever subsection comes last.
    const body = doc.blocks[at].body;
    let fenced = false;
    let sub = body.findIndex((line) => {
      if (/^```/.test(line)) fenced = !fenced;
      return !fenced && /^###\s/.test(line);
    });
    if (sub < 0) sub = body.length;
    const lead = body.slice(0, sub);
    while (lead.length > 0 && lead[lead.length - 1].trim() === '') lead.pop();
    const rest = body.slice(sub);
    body.splice(0, body.length, ...lead, ...(lead.length > 0 ? [''] : []), ...lines, ...(rest.length > 0 ? ['', ...rest] : []));
    return 'inserted';
  }
  let insertAt = doc.blocks.length;
  for (const heading of CONFIG_INSERT_AFTER) {
    const i = doc.blocks.findIndex((b) => b.heading.toLowerCase() === heading.toLowerCase());
    if (i >= 0) { insertAt = i + 1; break; }
  }
  doc.blocks.splice(insertAt, 0, { heading: CONFIG_HEADING, body: lines });
  return 'inserted';
}

// License is deliberately absent. Several repos extend the MIT line with a
// scope note - that BepInEx and the libraries inside it keep their own
// licences, that the demo clip at the top of the page is the publisher's
// footage - and rendering a one-line replacement would delete a true statement
// about what the licence does not cover.

// What --write applies unless --sections narrows it. The two sections carry no
// per-repo fact, so rendering them can only replace a paraphrase with the
// canonical wording. The config block's per-repo facts are the ones
// data/config-format.json and the committed config record, and the section
// around it stays hand-written.
//
// Controls, Requirements, Installation, the rest of Configuration, Updating,
// Uninstalling, Building from Source, License and Disclaimer are all absent on
// purpose.
//
// Controls was tried from lopari's catalog and the catalog turned out to be the
// thing that was wrong: 46 of its 54 entries advertise a Recenter hotkey on
// Home / Ctrl+Shift+T, and no mod in the fleet binds one. Generating from it
// would have pushed that into 46 READMEs. The bindings live in five languages
// and are read by hand.
//
// The rest each turn on something no data file records: whether the installer
// bundles the loader, whether uninstall removes it, which build tool the repo
// uses, which publisher the disclaimer names, and what the licence does not
// cover. A renderer for them would be a guess with a straight face.
const DEFAULT_WRITE = ['opentrack', 'community', CONFIG_ID];

// Where a missing section is inserted: after the first of these that exists,
// else at the top for opentrack and at the end for the rest.
const INSERT_AFTER = {
  opentrack: ['Installation', 'Requirements', 'Features'],
  community: ['Building from Source', 'Uninstalling'],
};

// ---------------------------------------------------------------------------
// README surgery
// ---------------------------------------------------------------------------

function splitSections(text) {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  const blocks = [];
  let preamble = [];
  let current = null;
  let fenced = false;
  for (const line of lines) {
    if (/^```/.test(line)) fenced = !fenced;
    const m = !fenced && /^##\s+(.+?)\s*$/.exec(line);
    if (m) {
      if (current) blocks.push(current);
      current = { heading: m[1], body: [] };
      continue;
    }
    if (current) current.body.push(line);
    else preamble.push(line);
  }
  if (current) blocks.push(current);
  return { preamble, blocks };
}

function joinSections({ preamble, blocks }) {
  const out = [preamble.join('\n').replace(/\s+$/, '')];
  for (const b of blocks) {
    out.push(`## ${b.heading}`);
    out.push(b.body.join('\n').replace(/^\n+/, '').replace(/\s+$/, ''));
  }
  return out.filter((s) => s.length).join('\n\n') + '\n';
}

function normalise(s) {
  return s.replace(/\r\n/g, '\n').replace(/[ \t]+$/gm, '').trim();
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

// The port the mod actually listens on. A shipped INI that names one is the
// authority; otherwise it is core's default. Guessing is not an option here,
// because the number goes into an instruction a user follows literally.
function listenPort(repoRoot) {
  const candidates = ['HeadTracking.ini', 'config/HeadTracking.ini', 'assets/HeadTracking.ini'];
  for (const rel of candidates) {
    const file = path.join(repoRoot, rel);
    if (!fs.existsSync(file)) continue;
    const m = /^\s*(?:Udp)?Port\s*=\s*(\d{4,5})/im.exec(fs.readFileSync(file, 'utf8'));
    if (m) return m[1];
  }
  return DEFAULT_PORT;
}

function repoContext(repoRoot) {
  return { name: path.basename(repoRoot), root: repoRoot, port: listenPort(repoRoot) };
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

function applySection(doc, id, rendered, force) {
  const spec = SECTIONS[id];
  const matches = [];
  for (let i = 0; i < doc.blocks.length; i++) {
    if (spec.match.test(doc.blocks[i].heading)) matches.push(i);
  }

  if (matches.length === 0) {
    const after = INSERT_AFTER[id] || [];
    // A section with no anchor to follow goes first for opentrack, which reads
    // near the top, and last for the rest, which are tail matter.
    let at = id === 'opentrack' ? 0 : doc.blocks.length;
    for (const heading of after) {
      const i = doc.blocks.findIndex((b) => b.heading.toLowerCase() === heading.toLowerCase());
      if (i >= 0) { at = i + 1; break; }
    }
    doc.blocks.splice(at, 0, { heading: spec.heading, body: rendered.split('\n') });
    return 'inserted';
  }

  const first = matches[0];
  const before = normalise(doc.blocks[first].body.join('\n'));

  // A repo that has added a subsection of its own is not drift to be flattened.
  // sleeping-dogs documents a FreeTrack shared-memory source next to the UDP
  // one, which is true of that mod and of no other, so overwriting the section
  // would delete a verified paragraph nothing else records.
  const subheads = (text) => new Set((text.match(/^###\s+(.+?)\s*$/gm) || []).map((h) => h.toLowerCase()));
  const rendered_subs = subheads(rendered);
  const extra = [...subheads(before)].filter((h) => !rendered_subs.has(h));
  if (extra.length && !force) return `kept, it adds ${extra.length} subsection${extra.length > 1 ? 's' : ''} this does not render`;

  const numbered = /^\d+\.\s/.test(doc.blocks[first].heading);
  doc.blocks[first] = {
    heading: numbered ? doc.blocks[first].heading : spec.heading,
    body: rendered.split('\n'),
  };
  // A repo that split the section across several H2s (an OpenTrack setup plus a
  // separate phone-app setup) collapses into the one canonical section.
  for (let k = matches.length - 1; k >= 1; k--) doc.blocks.splice(matches[k], 1);
  if (matches.length > 1) return 'merged';
  return before === normalise(rendered) ? 'unchanged' : 'rewritten';
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

function main() {
  const argv = process.argv.slice(2);
  const VALUE_FLAGS = ['--sections', '--print', '--roots-file'];
  const flags = new Set(argv.filter((a) => a.startsWith('--')));
  const valueOf = (name) => {
    const i = argv.indexOf(`--${name}`);
    if (i < 0) return null;
    if (argv[i + 1] === undefined || argv[i + 1].startsWith('--')) {
      console.error(`--${name} takes a value`);
      process.exit(2);
    }
    return argv[i + 1];
  };
  const tokens = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && VALUE_FLAGS.includes(argv[i - 1])));

  const KNOWN = [...Object.keys(SECTIONS), CONFIG_ID];
  const write = flags.has('--write');
  const json = flags.has('--json');
  const printOnly = valueOf('print');
  const rootsFile = valueOf('roots-file');
  const sectionFilter = valueOf('sections');
  const selected = sectionFilter
    ? sectionFilter.split(',').map((s) => s.trim()).filter(Boolean)
    : (write ? DEFAULT_WRITE : KNOWN);
  for (const id of [...selected, ...(printOnly ? [printOnly] : [])]) {
    if (!KNOWN.includes(id)) {
      console.error(`Unknown section '${id}'. Known: ${KNOWN.join(', ')}`);
      process.exit(2);
    }
  }
  if (json && write) {
    console.error('--json reports and does not write');
    process.exit(2);
  }

  if (printOnly && printOnly !== CONFIG_ID) {
    const ctx = { port: DEFAULT_PORT };
    console.log(`## ${SECTIONS[printOnly].heading}\n\n${SECTIONS[printOnly].render(ctx)}`);
    process.exit(0);
  }

  function resolveRepo(token) {
    for (const candidate of [token, path.join(REPOS_ROOT, token), path.join(REPOS_ROOT, `${token}-headtracking`), path.join(REPOS_ROOT, `${token}-head-tracking`)]) {
      if (fs.existsSync(path.join(candidate, 'README.md'))) return path.resolve(candidate);
    }
    throw new Error(`No repo with a README.md found for '${token}'.`);
  }

  let roots;
  if (rootsFile !== null) {
    if (flags.has('--all') || tokens.length) {
      console.error('--roots-file takes the place of --all and repo names');
      process.exit(2);
    }
    roots = fs.readFileSync(rootsFile, 'utf8').split(/\r?\n/).filter((line) => line !== '');
  } else if (flags.has('--all')) {
    roots = fs.readdirSync(REPOS_ROOT, { withFileTypes: true })
      .filter((d) => d.isDirectory() && /-(headtracking|head-tracking)$/.test(d.name))
      .map((d) => path.join(REPOS_ROOT, d.name))
      .filter((p) => fs.existsSync(path.join(p, '.git')) && fs.existsSync(path.join(p, 'cameraunlock-core')) && fs.existsSync(path.join(p, 'README.md')));
  } else if (tokens.length) {
    roots = tokens.map(resolveRepo);
  } else {
    roots = [path.dirname(CORE_ROOT)];
  }

  // NEXUS_MODS.md is untracked, so no check reaches it: its config block is pasted from here.
  if (printOnly === CONFIG_ID) {
    if (roots.length !== 1) {
      console.error('--print config prints one repo\'s block; name one repo, or run it from the repo');
      process.exit(2);
    }
    const block = configBlock(repoState(roots[0]));
    if (block === null) {
      console.error(`${path.basename(roots[0])} is not converted to the canonical config format, so it has no config block`);
      process.exit(1);
    }
    console.log(block);
    process.exit(0);
  }

  let drift = 0;
  let failed = 0;
  const report = [];
  for (const root of roots) {
    const readme = path.join(root, 'README.md');
    if (!fs.existsSync(readme)) {
      if (!json) throw new Error(`${root} has no README.md`);
      report.push({ root, readme: false, sections: {}, error: null });
      continue;
    }
    const ctx = repoContext(root);
    const original = fs.readFileSync(readme, 'utf8');
    const doc = splitSections(original);
    const notes = [];
    const results = {};
    let error = null;

    for (const id of selected) {
      if (id === CONFIG_ID) {
        try {
          results[id] = applyConfigBlock(doc, configBlock(repoState(root)));
        } catch (e) {
          error = e.message;
          failed++;
          notes.push(`${id}: cannot render, ${e.message}`);
          continue;
        }
        if (results[id] !== 'unchanged') notes.push(`${id}: ${results[id]}`);
        continue;
      }
      const rendered = SECTIONS[id].render(ctx);
      if (rendered === null) {
        if (!write) notes.push(`${id}: no data to render from, left alone`);
        continue;
      }
      results[id] = applySection(doc, id, rendered, flags.has('--force'));
      if (results[id] !== 'unchanged') notes.push(`${id}: ${results[id]}`);
    }

    if (json) {
      report.push({ root, readme: true, sections: results, error });
      continue;
    }

    const updated = joinSections(doc);
    const changed = updated !== original.replace(/\r\n/g, '\n');
    if (write && changed) fs.writeFileSync(readme, updated, 'utf8');
    if (changed && !write) drift++;

    const verb = error !== null ? 'fail ' : write ? (changed ? 'wrote' : 'ok   ') : (changed ? 'drift' : 'ok   ');
    if (notes.length || changed) console.log(`${verb} ${ctx.name}${notes.length ? `  (${notes.join('; ')})` : ''}`);
  }

  if (json) {
    console.log(JSON.stringify(report, null, 1));
    process.exit(0);
  }
  console.log(`\n${roots.length} repos, ${drift} with drift.`);
  if (failed > 0) console.log(`${failed} config block${failed > 1 ? 's' : ''} could not be rendered.`);
  process.exit(failed > 0 || (!write && drift > 0) ? 1 : 0);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) main();
