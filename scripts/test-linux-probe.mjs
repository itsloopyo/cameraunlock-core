#!/usr/bin/env node
// `pixi run test-linux-probe`: the Defaults.ini resolver and the config owners under distro Wine and
// native Mono in Linux containers. Not part of `check`: it needs Docker with a Linux engine.
//
// It builds containers/linux-probe (the only step that reaches the network), then runs each case in
// a container with no network, a read-only root, every capability dropped, no-new-privileges, a
// non-root user, tmpfs for the homes and Wine prefixes, and core's build outputs mounted read-only.
// Each case is containers/linux-probe/run-case.sh, which runs the `--probe-defaults-ini` mode of the
// C++ test binary and of CameraUnlock.Core.FrameworkTests and prints what they report with snapshots
// of the files involved. This script checks every case against its expectation and exits 1 on any
// mismatch. Everything a run printed is kept under build-linux-probe/.
//
//   node scripts/test-linux-probe.mjs

import { spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const IMAGE = 'cameraunlock-core-linux-probe';
const CONTEXT = path.join(ROOT, 'containers', 'linux-probe');
const OUT = path.join(ROOT, 'build-linux-probe');
const FIXTURES = path.join(ROOT, 'data', 'fixtures', 'canonical-ini');
const FRAMEWORK_BIN = path.join(ROOT, 'csharp', 'src', 'CameraUnlock.Core.FrameworkTests', 'bin', 'Debug');

const HOME = '/home/jösé-日本';
const DOS_HOME = 'Z:\\home\\jösé-日本';
const PREFIX_ROAMING = 'drive_c/users/probe/AppData/Roaming';
const GAME = 'C:\\game\\CameraUnlock.ini';
const PREFIX_FILE = '%AppData%\\CameraUnlock\\Defaults.ini';
const PREFIX_NAMED = `${PREFIX_FILE} (this Wine prefix)`;
const HOST_FILE = '~\\.config\\CameraUnlock\\Defaults.ini';
const READ_ONLY = 'Settings are read but not saved on this system: this version saves settings only on Windows, including '
  + 'under Wine and Proton. Changes made in game last until the game closes.';
const NOT_SAVED = 'Settings not saved: this version saves settings only on Windows.';
const MONO_NET35_WARNING = ['WARNING: The runtime version supported by this application is unavailable.',
  'Using default runtime: v4.0.30319'];
const CLR4 = '4.0.30319.42000';
const WINE_SESSION_NOISE = 'error: XDG_RUNTIME_DIR is invalid or not set in the environment.';

const sha = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');
const example = fs.readFileSync(path.join(FIXTURES, 'example', 'CameraUnlock.ini'));
const EXAMPLE_SHA = sha(example);
const EXAMPLE_SAVED_SHA = sha(Buffer.from(
  example.toString('latin1').replace('WorldSpaceYaw=default', 'WorldSpaceYaw=false'), 'latin1'));
const DEFAULTS_SHA = sha(fs.readFileSync(path.join(FIXTURES, 'global', 'Defaults.ini')));
const HAND_WRITTEN_SHA = sha(Buffer.from('[Network]\r\nUdpPort=5151\r\n', 'latin1'));
const MIGRATE_DEFAULTS_SHA = sha(Buffer.from('[Network]\r\nUdpPort=5151\r\n[Hotkeys]\r\nToggleKey=F8\r\n', 'latin1'));
const MIGRATE_LEGACY_SHA = sha(Buffer.from(
  '; tuned by hand\r\n[General]\r\nPort = 5151\r\nYawWorld = false\r\nSmoothng = 0.3\r\n[Position]\r\nPosition = false\r\n', 'latin1'));

const MOUNTS = [
  [path.join(CONTEXT, 'run-case.sh'), '/probe/run-case.sh'],
  [path.join(ROOT, 'cpp', 'build-test', 'tests', 'Release', 'cameraunlock_tests.exe'), '/probe/cpp/cameraunlock_tests.exe'],
  [path.join(FRAMEWORK_BIN, 'net35'), '/probe/net35'],
  [path.join(FRAMEWORK_BIN, 'net472'), '/probe/net472'],
  [path.join(FIXTURES, 'example', 'CameraUnlock.ini'), '/probe/example/CameraUnlock.ini'],
];

function docker(args) {
  const result = spawnSync('docker', args, { maxBuffer: 256 * 1024 * 1024, timeout: 900000 });
  if (result.error) throw result.error;
  return { status: result.status, stdout: result.stdout.toString('utf8'), stderr: result.stderr.toString('utf8') };
}

function runCase(args) {
  return docker([
    'run', '--rm', '--network', 'none', '--read-only', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
    '--user', '1000:1000', '--pids-limit', '512', '--memory', '6g',
    '--tmpfs', '/tmp:rw,exec,nosuid,nodev,size=4g',
    '--tmpfs', '/home:rw,nosuid,nodev,size=64m,uid=1000,gid=1000,mode=0755',
    ...MOUNTS.flatMap(([host, inside]) => ['-v', `${host.replace(/\\/g, '/')}:${inside}:ro`]),
    IMAGE, 'bash', '/probe/run-case.sh', ...args,
  ]);
}

// The probe's lines for one step, the files a snapshot saw, and anything else the step printed.
function parse(stdout) {
  const steps = new Map();
  const snaps = new Map();
  let step = null;
  for (const line of stdout.split('\n')) {
    if (line === '') continue;
    const f = line.split('\t');
    if (f[0] === 'step') {
      step = {
        input: {}, resolution: {}, runtime: {}, candidates: [], choice: {}, load: {}, log: [], sink: [], save: {},
        saveLog: [], files: [], exit: null, end: false, error: [], stderr: [], other: [], gameLines: [],
      };
      steps.set(f[1], step);
      continue;
    }
    if (f[0] === 'snap') {
      if (!snaps.has(f[1])) snaps.set(f[1], new Map());
      if (f[2] === 'absent') snaps.get(f[1]).set(f[3], { absent: true });
      else snaps.get(f[1]).set(f[2], { sha: f[3], stat: f[4] });
      continue;
    }
    if (!step) throw new Error(`a line before any step: ${line}`);
    const candidate = () => (step.candidates[Number(f[1])] ??= {});
    switch (f[0]) {
      case 'input': step.input[f[1]] = f[2]; break;
      case 'resolution': step.resolution[f[1]] = f[2]; break;
      case 'runtime': step.runtime[f[1]] = f[2]; break;
      case 'candidate': candidate().kind = f[2]; break;
      case 'candidate-may-create': candidate().mayCreate = f[2]; break;
      case 'candidate-path': candidate().path = f[2]; break;
      case 'candidate-shown': candidate().shown = f[2]; break;
      case 'candidate-shown-folder': candidate().shownFolder = f[2]; break;
      case 'candidate-shown-parent': candidate().shownParent = f[2]; break;
      case 'candidate-exists': candidate().exists = f[2]; break;
      case 'choice': step.choice[f[1]] = f[2]; break;
      case 'load': step.load[f[1]] = f[2]; break;
      case 'log': step.log.push(f[1]); break;
      case 'sink': step.sink.push(f[1]); break;
      case 'save': step.save[f[1]] = f[2]; break;
      case 'save-log': step.saveLog.push(f[1]); break;
      case 'file': step.files.push({ path: f[1], sha: f[2] }); break;
      case 'end': step.end = true; break;
      case 'error': step.error.push(f[1]); break;
      case 'exit': step.exit = Number(f[1]); break;
      case 'stderr': step.stderr.push(f.slice(1).join('\t')); break;
      case 'game-line': step.gameLines.push(f.slice(1).join('\t')); break;
      default: step.other.push(line);
    }
  }
  return { steps, snaps };
}

class Check {
  constructor(name) {
    this.name = name;
    this.failures = [];
  }

  that(condition, what) {
    if (!condition) this.failures.push(what);
  }

  eq(actual, expected, what) {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    if (a !== e) this.failures.push(`${what}: expected ${e}, got ${a}`);
  }
}

function stepOf(check, run, name) {
  const step = run.steps.get(name);
  if (!step) {
    check.that(false, `no step ${name} ran`);
    return null;
  }
  check.eq(step.exit, 0, `${name}: exit code`);
  check.that(step.end, `${name}: the probe did not reach its end`);
  check.eq(step.error, [], `${name}: errors`);
  return step;
}

function snapOf(check, run, label) {
  const snap = run.snaps.get(label);
  check.that(snap, `no snapshot ${label}`);
  return snap ?? new Map();
}

function fileSha(snap, file) {
  const entry = snap.get(file);
  return entry && !entry.absent ? entry.sha : 'absent';
}

function under(snap, folder) {
  return [...snap.entries()].filter(([p, e]) => !e.absent && (p === folder || p.startsWith(`${folder}/`))).map(([p]) => p);
}

// Whether the log line that starts with `head` lists `entry`; entries are separated by `; `.
function listed(log, head, entry) {
  const line = log.find((l) => l.startsWith(head));
  return line !== undefined && line.slice(head.length).split('; ').includes(entry);
}

// ---- (a) Wine ----

function wineName(step) {
  return `Wine ${step.input.wine_version} on Linux`;
}

function created(step, where) {
  return `Defaults.ini: ${where} (${wineName(step)}, the host's config folder, created with the built-in values)`;
}

function prefixCreated(step, hostFolder, why) {
  return `Defaults.ini: ${PREFIX_FILE} (${wineName(step)}, this Wine prefix, created with the built-in values): `
    + `the host's config folder ${hostFolder} could not be used: ${why}.`;
}

// Every step of a Wine case prints only the probe's lines, and at most the one line Wine writes to
// stderr when a session starts; wine-mono runs both FrameworkTests builds on its 4.0 runtime.
function wineSteps(check, run, runtime) {
  for (const [name, step] of run.steps) {
    check.eq(step.other, [], `${name}: other output`);
    check.that(step.stderr.length <= 1 && step.stderr.every((l) => l === WINE_SESSION_NOISE),
      `${name}: stderr holds only Wine's session line, got ${JSON.stringify(step.stderr)}`);
    if (runtime === 'cpp') check.eq(step.runtime, {}, `${name}: runtime lines`);
    else check.eq([step.runtime.build, step.runtime.clr], [runtime, CLR4], `${name}: the build and CLR that ran`);
  }
}

function wineCommon(check, step, name, versions) {
  if (!step) return;
  check.eq(step.input.platform, 'Wine', `${name}: platform`);
  check.eq(`wine-${step.input.wine_version}`, versions.wine.split(' ')[0], `${name}: wine_get_version against wine --version`);
  check.eq(step.input.host_system, 'Linux', `${name}: host system`);
  check.eq(step.input.WINEHOMEDIR, `\\??\\${DOS_HOME}`, `${name}: WINEHOMEDIR`);
  check.eq(step.input.known_folder, 'C:\\users\\probe\\AppData\\Roaming', `${name}: the prefix's roaming folder`);
  check.eq(step.input.package_result, '', `${name}: GetCurrentPackageFullName is not asked under Wine`);
  const prefix = step.candidates[step.candidates.length - 1] ?? {};
  check.eq([prefix.kind, prefix.shown], ['WinePrefix', PREFIX_FILE], `${name}: the last candidate is the prefix's`);
}

function expectGameCreated(check, step, name) {
  if (!step) return;
  check.eq(step.load.status, 'Created', `${name}: load status`);
  check.that(step.log.includes(`${GAME}: created with the default settings.`), `${name}: the game file's created line`);
}

const WINE_CASES = {
  home(check, run, versions) {
    const create = stepOf(check, run, 'create');
    wineCommon(check, create, 'create', versions);
    if (create) {
      check.eq(create.input.XDG_CONFIG_HOME + create.input.WINE_HOST_XDG_CONFIG_HOME, '', 'create: no XDG variable');
      check.eq([create.candidates[0]?.kind, create.candidates[0]?.path, create.candidates[0]?.shown],
        ['WineHost', `${DOS_HOME}\\.config\\CameraUnlock\\Defaults.ini`, HOST_FILE], 'create: the host candidate');
      check.eq([create.choice.read, create.choice.create], ['-1', '0'], 'create: the choice creates the host file');
      expectGameCreated(check, create, 'create');
      check.eq(create.log[0], created(create, HOST_FILE), 'create: the location line');
      check.eq(create.sink, [], 'create: status-sink messages');
    }
    const a = snapOf(check, run, 'create');
    check.eq(fileSha(a, `${HOME}/.config/CameraUnlock/Defaults.ini`), DEFAULTS_SHA, 'create: the host file is global/Defaults.ini');
    check.eq(fileSha(a, `/tmp/prefix-a/drive_c/game/CameraUnlock.ini`), EXAMPLE_SHA, 'create: the game file is example/CameraUnlock.ini');
    check.eq(under(a, `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock`), [], 'create: nothing in the prefix\'s AppData');

    const save = stepOf(check, run, 'save');
    if (save) {
      check.eq(save.load.status, 'Canonical', 'save: load status');
      check.eq(save.log[0], `Defaults.ini: ${HOST_FILE} (${wineName(save)}, the host's config folder, read)`, 'save: the location line');
      check.eq([save.save.status, save.save.reason], ['Saved', ''], 'save: Save');
      check.eq(save.saveLog, [`${GAME}: WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini.`],
        'save: the save line');
    }
    const b = snapOf(check, run, 'save');
    check.eq(fileSha(b, `/tmp/prefix-a/drive_c/game/CameraUnlock.ini`), EXAMPLE_SAVED_SHA,
      'save: the game file changed only on its WorldSpaceYaw line');
    check.eq(under(b, '/tmp/prefix-a/drive_c/game'), ['/tmp/prefix-a/drive_c/game', '/tmp/prefix-a/drive_c/game/CameraUnlock.ini'],
      'save: nothing left beside the game file');
    check.eq(b.get(`${HOME}/.config/CameraUnlock/Defaults.ini`), a.get(`${HOME}/.config/CameraUnlock/Defaults.ini`),
      'save: the host file kept its bytes and write time');
  },

  xdg(check, run, versions) {
    const xdg = `${HOME}/.var/app/com.valvesoftware.Steam/config`;
    const dos = `${DOS_HOME}\\.var\\app\\com.valvesoftware.Steam\\config\\CameraUnlock`;
    const shown = '~\\.var\\app\\com.valvesoftware.Steam\\config\\CameraUnlock\\Defaults.ini';
    const step = stepOf(check, run, 'create');
    wineCommon(check, step, 'create', versions);
    if (step) {
      check.eq([step.input.XDG_CONFIG_HOME, step.input.WINE_HOST_XDG_CONFIG_HOME].filter((v) => v !== '').length, 1,
        'create: exactly one XDG variable reached the process');
      check.that([step.input.XDG_CONFIG_HOME, step.input.WINE_HOST_XDG_CONFIG_HOME].includes(xdg), 'create: its value is the host\'s');
      check.eq(step.input.dos_file_name, dos, 'create: wine_get_dos_file_name');
      check.eq([step.candidates[0]?.kind, step.candidates[0]?.path, step.candidates[0]?.shown],
        ['WineHost', `${dos}\\Defaults.ini`, shown], 'create: the host candidate');
      expectGameCreated(check, step, 'create');
      check.eq(step.log[0], created(step, shown), 'create: the location line');
    }
    const snap = snapOf(check, run, 'create');
    check.eq(fileSha(snap, `${xdg}/CameraUnlock/Defaults.ini`), DEFAULTS_SHA, 'create: the file in the XDG folder');
    check.eq(under(snap, `${HOME}/.config/CameraUnlock`), [], 'create: nothing in ~/.config');
    check.eq(under(snap, `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock`), [], 'create: nothing in the prefix\'s AppData');
  },

  'xdg-relative'(check, run, versions) {
    const step = stepOf(check, run, 'create');
    wineCommon(check, step, 'create', versions);
    if (step) {
      check.that([step.input.XDG_CONFIG_HOME, step.input.WINE_HOST_XDG_CONFIG_HOME].includes('relative/config'),
        'create: the relative XDG value reached the process');
      check.eq(step.resolution.unix_folder, '', 'create: the relative value is not used');
      check.eq([step.candidates[0]?.kind, step.candidates[0]?.shown], ['WineHost', HOST_FILE], 'create: the host candidate is ~/.config');
      expectGameCreated(check, step, 'create');
      check.eq(step.log[0], created(step, HOST_FILE), 'create: the location line');
    }
    const snap = snapOf(check, run, 'create');
    check.eq(fileSha(snap, `${HOME}/.config/CameraUnlock/Defaults.ini`), DEFAULTS_SHA, 'create: the host file');
    check.eq([...snap.keys()].filter((p) => p.includes('/relative')), [], 'create: no folder named after the relative value');
  },

  'xdg-missing-parent'(check, run, versions) {
    const step = stepOf(check, run, 'create');
    wineCommon(check, step, 'create', versions);
    if (step) {
      check.eq(step.input.dos_file_name, `${DOS_HOME}\\missing\\config\\CameraUnlock`, 'create: a missing folder still converts');
      check.eq([step.choice.read, step.choice.create], ['-1', '0'], 'create: the host is tried first');
      expectGameCreated(check, step, 'create');
      check.eq(step.log[0], prefixCreated(step, '~\\missing\\config\\CameraUnlock', '~\\missing\\config does not exist'),
        'create: the location line');
      check.eq(step.sink, [], 'create: status-sink messages');
    }
    const snap = snapOf(check, run, 'create');
    check.eq(under(snap, `${HOME}/missing`), [], 'create: the host folder and its parent are not created');
    check.eq(fileSha(snap, `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock/Defaults.ini`), DEFAULTS_SHA, 'create: the prefix file');
  },

  // What a player gets with Wine as it starts by default: its menu builder has created
  // $XDG_CONFIG_HOME/menus/applications-merged, so the folder's parent exists and the host file is made.
  'xdg-missing-parent-menu-builder'(check, run, versions) {
    const shown = '~\\missing\\config\\CameraUnlock\\Defaults.ini';
    const step = stepOf(check, run, 'create');
    wineCommon(check, step, 'create', versions);
    if (step) {
      expectGameCreated(check, step, 'create');
      check.eq(step.log[0], created(step, shown), 'create: the location line');
    }
    const snap = snapOf(check, run, 'create');
    check.eq(snap.get(`${HOME}/missing/config/menus/applications-merged`)?.sha, 'folder', 'create: Wine made the menus folder');
    check.eq(fileSha(snap, `${HOME}/missing/config/CameraUnlock/Defaults.ini`), DEFAULTS_SHA, 'create: the host file');
    check.eq(under(snap, `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock`), [], 'create: nothing in the prefix\'s AppData');
  },

  'host-unwritable'(check, run, versions) {
    const step = stepOf(check, run, 'create');
    wineCommon(check, step, 'create', versions);
    if (step) {
      expectGameCreated(check, step, 'create');
      check.eq(step.log[0], prefixCreated(step, '~\\.config\\CameraUnlock', 'it could not be created: the folder cannot be written'),
        'create: the location line');
      check.eq(step.sink, [], 'create: status-sink messages');
    }
    const snap = snapOf(check, run, 'create');
    check.eq(under(snap, `${HOME}/.config/CameraUnlock`), [], 'create: no host folder');
    check.eq(fileSha(snap, `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock/Defaults.ini`), DEFAULTS_SHA, 'create: the prefix file');
  },

  'two-prefixes'(check, run, versions) {
    const first = stepOf(check, run, 'first');
    wineCommon(check, first, 'first', versions);
    if (first) check.eq(first.log[0], created(first, HOST_FILE), 'first: the location line');
    const second = stepOf(check, run, 'second');
    wineCommon(check, second, 'second', versions);
    if (second) {
      expectGameCreated(check, second, 'second');
      check.eq([second.choice.read, second.choice.create], ['0', '-1'], 'second: the choice reads the host file');
      check.eq(second.log[0], `Defaults.ini: ${HOST_FILE} (${wineName(second)}, the host's config folder, read)`,
        'second: the location line');
      check.eq(second.sink, [], 'second: status-sink messages');
    }
    const a = snapOf(check, run, 'first');
    const b = snapOf(check, run, 'second');
    const host = `${HOME}/.config/CameraUnlock/Defaults.ini`;
    check.eq(fileSha(a, host), DEFAULTS_SHA, 'first: the host file');
    check.eq(b.get(host), a.get(host), 'second: the host file kept its bytes and write time');
    for (const prefix of ['/tmp/prefix-a', '/tmp/prefix-b']) {
      check.eq(under(b, `${prefix}/${PREFIX_ROAMING}/CameraUnlock`), [], `second: nothing in ${prefix}'s AppData`);
    }
    check.eq(fileSha(b, '/tmp/prefix-b/drive_c/game/CameraUnlock.ini'), EXAMPLE_SHA, 'second: the second game file');
  },

  'prefix-then-host'(check, run, versions) {
    const prefix = stepOf(check, run, 'prefix');
    wineCommon(check, prefix, 'prefix', versions);
    if (prefix) {
      expectGameCreated(check, prefix, 'prefix');
      check.eq(prefix.log[0],
        prefixCreated(prefix, '~\\.config\\CameraUnlock', 'it could not be created: the folder cannot be written'),
        'prefix: the location line');
    }
    const host = stepOf(check, run, 'host');
    wineCommon(check, host, 'host', versions);
    if (host) {
      check.eq(host.load.status, 'Canonical', 'host: load status');
      check.eq([host.choice.read, host.candidates.map((c) => c.exists)], ['0', ['true', 'true']], 'host: both exist, the host is read');
      check.eq(host.log[0], `Defaults.ini: ${HOST_FILE} is read, and ${PREFIX_NAMED} is not.`, 'host: the two-files line');
      check.eq(host.sink, [`Two Defaults.ini files: this game reads ${HOST_FILE} and ignores ${PREFIX_NAMED}.`],
        'host: the two-files message, once');
      check.that(listed(host.log, `${GAME}: from Defaults.ini: `, 'UdpPort=5151'), 'host: the host file\'s value is used');
    }
    const a = snapOf(check, run, 'prefix');
    const b = snapOf(check, run, 'host');
    const prefixFile = `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock/Defaults.ini`;
    check.eq(fileSha(a, prefixFile), DEFAULTS_SHA, 'prefix: the prefix file');
    check.eq(b.get(prefixFile), a.get(prefixFile), 'host: the prefix file kept its bytes and write time');
    check.eq(fileSha(b, `${HOME}/.config/CameraUnlock/Defaults.ini`), HAND_WRITTEN_SHA, 'host: the host file is untouched');
  },

  // A legacy file whose port equals the host Defaults.ini's, with a hand-written ToggleKey there that
  // the untouched legacy keys differ from.
  migrate(check, run, versions) {
    const step = stepOf(check, run, 'migrate');
    wineCommon(check, step, 'migrate', versions);
    const legacy = 'C:\\game\\HeadTracking.ini';
    if (step) {
      check.eq(step.load.status, 'Migrated', 'migrate: load status');
      check.eq(step.log[0], `Defaults.ini: ${HOST_FILE} (${wineName(step)}, the host's config folder, read)`,
        'migrate: the location line');
      check.that(step.log.includes(`${GAME}: created from ${legacy}, which is left as it was.`), 'migrate: the created-from line');
      check.that(step.log.includes(`${legacy}: not carried: [General] Smoothng=0.3 on line 5, this build does not read it`),
        'migrate: the not-carried line');
      check.that(listed(step.log, `${GAME}: from Defaults.ini: `, 'UdpPort=5151'), 'migrate: UdpPort follows Defaults.ini');
      check.eq(step.sink, [], 'migrate: status-sink messages');
      const rows = step.gameLines.filter((l) => /^[A-Za-z]+=/.test(l));
      check.eq(rows, ['ConfigFormat=1', 'UdpPort=default', 'EnableOnStartup=default', 'WorldSpaceYaw=false', 'RotationEnabled=true',
        'PositionEnabled=false', 'ToggleKey=End, Ctrl+Shift+Y', 'LightMultiplier=default'], 'migrate: the migrated rows');
    }
    const a = snapOf(check, run, 'before');
    const b = snapOf(check, run, 'migrate');
    const host = `${HOME}/.config/CameraUnlock/Defaults.ini`;
    check.eq(fileSha(a, host), MIGRATE_DEFAULTS_SHA, 'before: the hand-written host file');
    check.eq(b.get(host), a.get(host), 'migrate: the host file kept its bytes and write time');
    const legacyPath = '/tmp/prefix-a/drive_c/game/HeadTracking.ini';
    check.eq(fileSha(a, legacyPath), MIGRATE_LEGACY_SHA, 'before: the legacy file');
    check.eq(b.get(legacyPath), a.get(legacyPath), 'migrate: the legacy file kept its bytes and write time');
    check.eq(under(b, `/tmp/prefix-a/${PREFIX_ROAMING}/CameraUnlock`), [], 'migrate: nothing in the prefix\'s AppData');
    check.eq(under(b, '/tmp/prefix-a/drive_c/game'),
      ['/tmp/prefix-a/drive_c/game', '/tmp/prefix-a/drive_c/game/CameraUnlock.ini', legacyPath], 'migrate: the game folder');
  },
};

const WINE_RUNTIMES = { migrate: ['net35', 'net472'] };

// ---- (b) native Mono ----

const PLACEMENTS = {
  xdg: { line: 'Defaults.ini: ~/xdg/CameraUnlock/Defaults.ini (read)', reads: true },
  config: { line: 'Defaults.ini: ~/.config/CameraUnlock/Defaults.ini (read)', reads: true },
  library: { line: 'Defaults.ini: ~/Library/Application Support/CameraUnlock/Defaults.ini (read)', reads: true },
  'xdg-and-config': { line: 'Defaults.ini: ~/xdg/CameraUnlock/Defaults.ini (read)', reads: true },
  'config-and-library': {
    line: 'Defaults.ini: ~/.config/CameraUnlock/Defaults.ini is read, and ~/Library/Application Support/CameraUnlock/Defaults.ini is not.',
    message: 'Two Defaults.ini files: this game reads ~/.config/CameraUnlock/Defaults.ini and ignores '
      + '~/Library/Application Support/CameraUnlock/Defaults.ini.',
    reads: true,
  },
  none: {
    line: 'Defaults.ini: no file at ~/.config/CameraUnlock/Defaults.ini or ~/Library/Application Support/CameraUnlock/Defaults.ini; '
      + 'on this system the mod reads Defaults.ini but does not create it. Settings set to default use the built-in values.',
    reads: false,
  },
  'home-unset': {
    line: 'Defaults.ini: no location: HOME is not set to an absolute path. Settings set to default use the built-in values.',
    reads: false,
  },
  'home-unset-xdg': { line: 'Defaults.ini: $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (read)', reads: true },
};

function nativeCase(state, placement) {
  const expected = PLACEMENTS[placement];
  return (check, run) => {
    const before = snapOf(check, run, 'before');
    check.that(before.size > 0, 'the snapshot before holds the files');
    for (const build of ['net35', 'net472']) {
      const step = stepOf(check, run, build);
      if (step) {
        check.eq([step.runtime.build, step.runtime.clr], [build, CLR4], `${build}: the build and CLR that ran`);
        check.eq(step.stderr, [], `${build}: stderr`);
        check.eq(step.input.platform, 'Native', `${build}: platform`);
        check.eq(step.input.HOME, placement.startsWith('home-unset') ? '' : HOME, `${build}: HOME`);
        check.eq([step.load.status, step.load.reason], ['ReadOnly', READ_ONLY], `${build}: load`);
        check.eq(step.log[0], expected.line, `${build}: the location line`);
        check.that(step.log.includes(READ_ONLY), `${build}: the read-only line`);
        check.eq(step.sink, [READ_ONLY, ...(expected.message ? [expected.message] : []), NOT_SAVED], `${build}: status-sink messages`);
        check.eq([step.save.status, step.save.reason], ['NotSaved', NOT_SAVED], `${build}: Save`);
        check.eq(step.saveLog, ['/tmp/game/CameraUnlock.ini: not saved: this version saves settings only on Windows'],
          `${build}: the save line`);
        check.eq(step.other, build === 'net35' ? MONO_NET35_WARNING : [], `${build}: other output`);
        if (state === 'legacy') {
          check.that(step.log.includes('/tmp/game/HeadTracking.ini: not carried: [General] Smoothng=0.3 on line 5, this build does not read it'),
            `${build}: the import ran`);
        } else {
          const [head, entry] = expected.reads ? ['/tmp/game/CameraUnlock.ini: from Defaults.ini: ', 'UdpPort=5151']
            : ['/tmp/game/CameraUnlock.ini: built-in, not set in Defaults.ini: ', 'UdpPort=4242'];
          check.that(listed(step.log, head, entry), `${build}: ${entry} in the line starting ${head}`);
        }
      }
      const after = snapOf(check, run, build);
      check.eq([...after.entries()], [...before.entries()], `${build}: every file and folder as it was, bytes and write time`);
    }
  };
}

const NATIVE_STATES = ['legacy', 'canonical', 'nothing'];

// ---- run ----

function main() {
  for (const [host] of MOUNTS) {
    if (!fs.existsSync(host)) throw new Error(`${path.relative(ROOT, host)} is missing; the pixi task builds it first`);
  }
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });

  console.log(`Building ${IMAGE} from ${path.relative(ROOT, CONTEXT)}`);
  const build = docker(['build', '-t', IMAGE, CONTEXT]);
  fs.writeFileSync(path.join(OUT, 'build.log'), build.stdout + build.stderr);
  if (build.status !== 0) throw new Error(`docker build failed; see ${path.relative(ROOT, OUT)}/build.log`);
  const image = docker(['image', 'inspect', '--format', '{{.Id}}', IMAGE]).stdout.trim();

  const versionRun = runCase(['versions']);
  if (versionRun.status !== 0) throw new Error(`the versions case failed:\n${versionRun.stderr}`);
  const versions = {};
  const packages = [];
  for (const line of versionRun.stdout.split('\n').filter((l) => l !== '')) {
    const [, name, value] = line.split('\t');
    if (name === 'package') packages.push(value);
    else versions[name] = value;
  }
  const summary = [
    `image ${image}`,
    `base debian@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132 (trixie-20260824-slim)`,
    'apt snapshot.debian.org 20260911T000000Z',
    `wine ${versions.wine}`, `mono ${versions.mono}`, `wine-mono ${versions['wine-mono']}`, `debian ${versions.debian}`,
    ...packages.map((p) => `package ${p}`), '',
  ];

  const cases = [];
  for (const name of Object.keys(WINE_CASES)) {
    for (const runtime of WINE_RUNTIMES[name] ?? ['cpp', 'net35', 'net472']) {
      cases.push({
        id: `wine-${name}-${runtime}`,
        args: ['wine', name, runtime],
        check: (check, run, versions) => {
          WINE_CASES[name](check, run, versions);
          wineSteps(check, run, runtime);
        },
      });
    }
  }
  for (const state of NATIVE_STATES) {
    for (const placement of Object.keys(PLACEMENTS)) {
      cases.push({ id: `native-${state}-${placement}`, args: ['native', state, placement], check: nativeCase(state, placement) });
    }
  }

  let failed = 0;
  const xdgReached = new Set();
  for (const c of cases) {
    const result = runCase(c.args);
    fs.writeFileSync(path.join(OUT, `${c.id}.out`), result.stdout);
    fs.writeFileSync(path.join(OUT, `${c.id}.err`), result.stderr);
    const check = new Check(c.id);
    check.eq(result.status, 0, 'the case script\'s exit code');
    const run = parse(result.stdout);
    c.check(check, run, versions);
    if (c.args[1] === 'xdg') {
      const step = run.steps.get('create');
      if (step) xdgReached.add(step.input.XDG_CONFIG_HOME !== '' ? 'XDG_CONFIG_HOME' : 'WINE_HOST_XDG_CONFIG_HOME');
    }
    const line = `${check.failures.length === 0 ? 'PASS' : 'FAIL'} ${c.id}`;
    console.log(line);
    summary.push(line);
    for (const failure of check.failures) {
      console.log(`    ${failure}`);
      summary.push(`    ${failure}`);
    }
    if (check.failures.length > 0) failed += 1;
  }
  summary.push('', `under Wine the host's XDG folder reached the process as ${[...xdgReached].join(' and ')}`);
  summary.push(`${cases.length - failed} of ${cases.length} cases passed`);
  fs.writeFileSync(path.join(OUT, 'summary.txt'), `${summary.join('\n')}\n`);
  console.log(summary.slice(-2).join('\n'));
  console.log(`Output: ${path.relative(ROOT, OUT)}`);
  return failed === 0 ? 0 : 1;
}

process.exitCode = main();
