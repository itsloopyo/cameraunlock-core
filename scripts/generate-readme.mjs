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
//   node scripts/generate-readme.mjs --all --print opentrack
//   node scripts/generate-readme.mjs --all --write --force
//
// --write leaves alone any section that has grown a subsection the generator
// does not render, because that subsection is something a repo verified and
// nothing else records. --force overwrites it anyway.
//
// Exit 0 when every checked section matches, 1 when any differs. --write
// rewrites the sections in place and always exits 0 on success.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

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

// License is deliberately absent. Several repos extend the MIT line with a
// scope note - that BepInEx and the libraries inside it keep their own
// licences, that the demo clip at the top of the page is the publisher's
// footage - and rendering a one-line replacement would delete a true statement
// about what the licence does not cover.

// The two sections --write applies unless --sections narrows it. Neither
// carries a per-repo fact, so rendering them can only replace a paraphrase with
// the canonical wording.
//
// Controls, Requirements, Installation, Configuration, Updating, Uninstalling,
// Building from Source, License and Disclaimer are all absent on purpose.
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
const DEFAULT_WRITE = ['opentrack', 'community'];

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

const argv = process.argv.slice(2);
const flags = new Set(argv.filter((a) => a.startsWith('--')));
const valueOf = (name) => {
  const i = argv.indexOf(`--${name}`);
  return i >= 0 ? argv[i + 1] : null;
};
const tokens = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && ['--sections', '--print'].includes(argv[i - 1])));

const write = flags.has('--write');
const printOnly = valueOf('print');
const sectionFilter = valueOf('sections');
const selected = sectionFilter
  ? sectionFilter.split(',').map((s) => s.trim()).filter(Boolean)
  : (write ? DEFAULT_WRITE : Object.keys(SECTIONS));
for (const id of selected) {
  if (!SECTIONS[id]) {
    console.error(`Unknown section '${id}'. Known: ${Object.keys(SECTIONS).join(', ')}`);
    process.exit(2);
  }
}


if (printOnly) {
  if (!SECTIONS[printOnly]) {
    console.error(`Unknown section '${printOnly}'. Known: ${Object.keys(SECTIONS).join(', ')}`);
    process.exit(2);
  }
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
if (flags.has('--all')) {
  roots = fs.readdirSync(REPOS_ROOT, { withFileTypes: true })
    .filter((d) => d.isDirectory() && /-(headtracking|head-tracking)$/.test(d.name))
    .map((d) => path.join(REPOS_ROOT, d.name))
    .filter((p) => fs.existsSync(path.join(p, '.git')) && fs.existsSync(path.join(p, 'cameraunlock-core')) && fs.existsSync(path.join(p, 'README.md')));
} else if (tokens.length) {
  roots = tokens.map(resolveRepo);
} else {
  roots = [path.dirname(CORE_ROOT)];
}

let drift = 0;
for (const root of roots) {
  const ctx = repoContext(root);
  const readme = path.join(root, 'README.md');
  const original = fs.readFileSync(readme, 'utf8');
  const doc = splitSections(original);
  const notes = [];

  for (const id of selected) {
    const rendered = SECTIONS[id].render(ctx);
    if (rendered === null) {
      if (!write) notes.push(`${id}: no data to render from, left alone`);
      continue;
    }
    const result = applySection(doc, id, rendered, flags.has('--force'));
    if (result !== 'unchanged') notes.push(`${id}: ${result}`);
  }

  const updated = joinSections(doc);
  const changed = updated !== original.replace(/\r\n/g, '\n');
  if (write && changed) fs.writeFileSync(readme, updated, 'utf8');
  if (changed && !write) drift++;

  const verb = write ? (changed ? 'wrote' : 'ok   ') : (changed ? 'drift' : 'ok   ');
  if (notes.length || changed) console.log(`${verb} ${ctx.name}${notes.length ? `  (${notes.join('; ')})` : ''}`);
}

console.log(`\n${roots.length} repos, ${drift} with drift.`);
process.exit(write ? 0 : (drift > 0 ? 1 : 0));
