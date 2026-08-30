#!/usr/bin/env node
// Drives data/pipeline-conformance.json against a pipeline implementation.
//
// The implementation is a "harness": a program that reads a line-oriented
// command stream on stdin and prints one line of space-separated numbers for
// every step command. All assertion logic lives here, so a port only has to
// supply ~200 lines of dumb executor rather than reimplement the checks - and
// every port is then checked by the same code.
//
// Harness protocol (one command per line):
//   unit <name> <vector-id>  select the unit under test and reset its state. The
//                            vector id lets a harness skip one specific vector -
//                            e.g. a port that never inspects the HCAM trailer can
//                            still run the packet unit.
//   cfg <key> <value>        set one configuration value (floats; bools as 0/1)
//   begin                    configuration is complete; the harness answers with
//                            one line, either "ok" or "skip <reason>". A harness
//                            skips a unit it does not implement, and MUST skip on
//                            a cfg key it does not recognise - silently ignoring
//                            one runs a different test than the vector describes.
//   s <numbers...>           one step; prints the unit's output fields
//   q <hex>                  packet unit: parse one datagram
//   e <yaw> <pitch> <roll>   euler_roundtrip unit: compose then decompose
//   p <hex> <dt>             session units: a frame carrying a datagram
//   f <dt>                   session units: a frame with no datagram
//   end                      unit run complete
//   bye                      terminate
//
// Usage: node scripts/pipeline-vectors/run-vectors.mjs --harness "<command line>"
//        [--vectors <path>] [--only <id,id>] [--verbose]

import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');

function parseArgs(argv) {
    const out = { harness: null, vectors: resolve(repoRoot, 'data', 'pipeline-conformance.json'), only: null, verbose: false };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--harness') out.harness = argv[++i];
        else if (a === '--vectors') out.vectors = resolve(argv[++i]);
        else if (a === '--only') out.only = new Set(argv[++i].split(',').map((s) => s.trim()));
        else if (a === '--verbose') out.verbose = true;
        else throw new Error(`unknown argument: ${a}`);
    }
    if (!out.harness) throw new Error('--harness "<command line>" is required');
    return out;
}

// ---------------------------------------------------------------- step expansion

const UNIT_STEP_FIELDS = {
    pose_interpolator: ['yaw', 'pitch', 'roll'],
    position_interpolator: ['x', 'y', 'z'],
    tracking_processor: ['yaw', 'pitch', 'roll'],
    position_processor: ['x', 'y', 'z', 'rot_yaw', 'rot_pitch', 'rot_roll'],
    euler_roundtrip: ['yaw', 'pitch', 'roll'],
    packet: [],
    session_rot: ['yaw', 'pitch', 'roll'],
    session_pos: ['x', 'y', 'z'],
};

const UNIT_OUT_FIELDS = {
    pose_interpolator: ['yaw', 'pitch', 'roll'],
    position_interpolator: ['x', 'y', 'z'],
    tracking_processor: ['yaw', 'pitch', 'roll'],
    position_processor: ['x', 'y', 'z'],
    euler_roundtrip: ['yaw', 'pitch', 'roll'],
    packet: ['ok', 'yaw', 'pitch', 'roll', 'x', 'y', 'z', 'trailer_present', 'recenter_counter'],
    session_rot: ['yaw', 'pitch', 'roll'],
    session_pos: ['x', 'y', 'z'],
};

const INTERPOLATOR_UNITS = new Set(['pose_interpolator', 'position_interpolator']);
const SESSION_UNITS = new Set(['session_rot', 'session_pos']);

// Session ramps synthesise their own datagram from the step fields so the
// vector file does not have to carry hundreds of hex strings. Position is
// metres in the vector and centimetres on the wire; rotation is degrees in both.
const WIRE_FIELDS = [['x', 100], ['y', 100], ['z', 100], ['yaw', 1], ['pitch', 1], ['roll', 1]];

function packetHex(values) {
    const buf = Buffer.alloc(48);
    WIRE_FIELDS.forEach(([name, scale], i) => buf.writeDoubleLE((values[name] ?? 0) * scale, i * 8));
    return buf.toString('hex');
}

function expandSteps(vector) {
    const unit = vector.unit;
    const expanded = [];
    const labels = new Map();

    for (const raw of vector.steps) {
        const op = raw.op ?? 'step';
        const first = expanded.length;

        if (op === 'step') {
            const n = raw.repeat ?? 1;
            for (let i = 0; i < n; i++) expanded.push({ ...raw, op: 'step' });
        } else if (op === 'ramp') {
            const newEvery = raw.new_every ?? 1;
            for (let i = 0; i < raw.count; i++) {
                const sample = Math.floor(i / newEvery);
                const values = {};
                for (const f of UNIT_STEP_FIELDS[unit]) {
                    values[f] = (raw.from?.[f] ?? 0) + (raw.per_sample?.[f] ?? 0) * sample;
                }
                expanded.push({ op: 'step', dt: raw.dt, new: i % newEvery === 0, ...values });
            }
        } else if (op === 'grid') {
            const axis = (spec) => {
                const vals = [];
                // Inclusive of `to`, with a half-step guard so float accumulation
                // does not silently drop the final value.
                for (let v = spec.from; v <= spec.to + spec.step * 0.5; v += spec.step) vals.push(v);
                return vals;
            };
            for (const yaw of axis(raw.yaw)) {
                for (const pitch of axis(raw.pitch)) {
                    for (const roll of axis(raw.roll)) {
                        expanded.push({ op: 'step', yaw, pitch, roll });
                    }
                }
            }
        } else {
            throw new Error(`${vector.id}: unknown step op "${op}"`);
        }

        if (raw.label) {
            if (labels.has(raw.label)) throw new Error(`${vector.id}: duplicate label "${raw.label}"`);
            labels.set(raw.label, { first, last: expanded.length - 1 });
        }
    }

    if (SESSION_UNITS.has(unit)) {
        for (const step of expanded) {
            if (step.bytes === undefined && step.dt !== undefined && UNIT_STEP_FIELDS[unit].some((f) => step[f] !== undefined)) {
                step.bytes = packetHex(step);
            }
        }
    }

    return { expanded, labels };
}

function resolveRef(ref, labels, count) {
    if (typeof ref === 'number') {
        if (!Number.isInteger(ref) || ref < 0 || ref >= count) throw new Error(`step index ${ref} out of range (0..${count - 1})`);
        return ref;
    }
    const m = /^(.*?)(:last)?$/.exec(ref);
    const entry = labels.get(m[1]);
    if (!entry) throw new Error(`unknown step label "${ref}"`);
    return m[2] ? entry.last : entry.first;
}

// ---------------------------------------------------------------- command encoding

function num(v) {
    // 17 significant digits round-trips a double exactly, which matters for the
    // frame times: the hold/decay boundaries are only exact if dt is.
    return Number(v).toPrecision(17);
}

function encodeStep(unit, step) {
    if (unit === 'packet') return `q ${step.bytes}`;
    if (unit === 'euler_roundtrip') return `e ${num(step.yaw)} ${num(step.pitch)} ${num(step.roll)}`;
    if (SESSION_UNITS.has(unit)) {
        return step.bytes ? `p ${step.bytes} ${num(step.dt)}` : `f ${num(step.dt)}`;
    }
    const parts = UNIT_STEP_FIELDS[unit].map((f) => num(step[f] ?? 0));
    if (INTERPOLATOR_UNITS.has(unit)) parts.push(step.new ? '1' : '0');
    parts.push(num(step.dt));
    return `s ${parts.join(' ')}`;
}

function buildScript(vectors) {
    const lines = [];
    const plan = [];
    for (const v of vectors) {
        const { expanded, labels } = expandSteps(v);
        lines.push(`unit ${v.unit} ${v.id}`);
        for (const [k, val] of Object.entries(v.config ?? {})) lines.push(`cfg ${k} ${num(val)}`);
        lines.push('begin');
        for (const step of expanded) lines.push(encodeStep(v.unit, step));
        lines.push('end');
        plan.push({ vector: v, expanded, labels });
    }
    lines.push('bye');
    return { script: lines.join('\n') + '\n', plan };
}

// ---------------------------------------------------------------- assertions

const DEG = Math.PI / 180;

function normalizeAngle(a) {
    a = a % 360;
    if (a > 180) a -= 360;
    else if (a < -180) a += 360;
    return a;
}

function quatFromYawPitchRoll(yawDeg, pitchDeg, rollDeg) {
    const hy = yawDeg * DEG * 0.5, hp = pitchDeg * DEG * 0.5, hr = rollDeg * DEG * 0.5;
    const sy = Math.sin(hy), cy = Math.cos(hy);
    const sp = Math.sin(hp), cp = Math.cos(hp);
    const sr = Math.sin(hr), cr = Math.cos(hr);
    return [
        cy * sp * cr + sy * cp * sr,
        sy * cp * cr - cy * sp * sr,
        cy * cp * sr - sy * sp * cr,
        cy * cp * cr + sy * sp * sr,
    ];
}

function quatAngleDeg(a, b) {
    const dot = Math.abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2 * Math.acos(Math.min(1, dot)) / DEG;
}

function checkAssertion(a, ctx) {
    const { outputs, labels, steps, unit } = ctx;
    const count = outputs.length;
    const get = (i, field) => {
        const v = outputs[i][field];
        if (v === undefined) throw new Error(`unit ${unit} has no output field "${field}"`);
        return v;
    };
    const range = () => {
        const from = resolveRef(a.from, labels, count);
        const to = resolveRef(a.to, labels, count);
        if (to < from) throw new Error(`assertion range ${a.from}..${a.to} runs backwards`);
        return [from, to];
    };

    switch (a.kind) {
        case 'equals': {
            const i = resolveRef(a.at, labels, count);
            const got = get(i, a.field);
            const diff = Math.abs(got - a.value);
            if (!(diff <= a.tolerance)) {
                return `step ${i} ${a.field}: expected ${a.value} +/-${a.tolerance}, got ${got} (off by ${diff.toPrecision(4)})`;
            }
            return null;
        }
        case 'abs_at_most': {
            const [from, to] = range();
            for (let i = from; i <= to; i++) {
                const got = get(i, a.field);
                if (!(Math.abs(got) <= a.value)) return `step ${i} ${a.field}: |${got}| exceeds ${a.value}`;
            }
            return null;
        }
        case 'abs_at_least': {
            const [from, to] = range();
            for (let i = from; i <= to; i++) {
                const got = get(i, a.field);
                if (!(Math.abs(got) >= a.value)) return `step ${i} ${a.field}: |${got}| is below ${a.value}`;
            }
            return null;
        }
        case 'max_step': {
            const [from, to] = range();
            for (let i = from + 1; i <= to; i++) {
                const d = Math.abs(get(i, a.field) - get(i - 1, a.field));
                if (!(d <= a.value)) return `steps ${i - 1}->${i} ${a.field}: jumped ${d.toPrecision(6)}, limit ${a.value}`;
            }
            return null;
        }
        case 'min_step': {
            const [from, to] = range();
            for (let i = from + 1; i <= to; i++) {
                const d = Math.abs(get(i, a.field) - get(i - 1, a.field));
                if (!(d >= a.value)) return `steps ${i - 1}->${i} ${a.field}: moved only ${d.toPrecision(6)}, floor ${a.value}`;
            }
            return null;
        }
        case 'angular_travel_at_most': {
            const [from, to] = range();
            let total = 0;
            for (let i = from + 1; i <= to; i++) {
                total += Math.abs(normalizeAngle(get(i, a.field) - get(i - 1, a.field)));
            }
            if (!(total <= a.value)) return `steps ${from}..${to} ${a.field}: travelled ${total.toPrecision(6)} degrees, limit ${a.value}`;
            return null;
        }
        case 'constant': {
            const [from, to] = range();
            let lo = Infinity, hi = -Infinity;
            for (let i = from; i <= to; i++) {
                const v = get(i, a.field);
                lo = Math.min(lo, v);
                hi = Math.max(hi, v);
            }
            if (!(hi - lo <= a.tolerance)) return `steps ${from}..${to} ${a.field}: spread ${(hi - lo).toPrecision(4)} exceeds ${a.tolerance} (${lo} .. ${hi})`;
            return null;
        }
        case 'orientation_roundtrip_max_error': {
            let worst = 0, worstAt = -1;
            for (let i = 0; i < count; i++) {
                const inQ = quatFromYawPitchRoll(steps[i].yaw ?? 0, steps[i].pitch ?? 0, steps[i].roll ?? 0);
                const outQ = quatFromYawPitchRoll(outputs[i].yaw, outputs[i].pitch, outputs[i].roll);
                const err = quatAngleDeg(inQ, outQ);
                if (err > worst) { worst = err; worstAt = i; }
            }
            if (!(worst <= a.value)) {
                const s = steps[worstAt], o = outputs[worstAt];
                return `worst round-trip error ${worst.toPrecision(6)} degrees exceeds ${a.value} at step ${worstAt} `
                    + `(in yaw=${s.yaw} pitch=${s.pitch} roll=${s.roll} -> out yaw=${o.yaw} pitch=${o.pitch} roll=${o.roll})`;
            }
            return null;
        }
        default:
            throw new Error(`unknown assertion kind "${a.kind}"`);
    }
}

// ---------------------------------------------------------------- harness driver

function runHarness(commandLine, script) {
    return new Promise((res, rej) => {
        // The harness runs through a shell so a port can pass a full command
        // line ("lua tests/harness.lua", "cargo run -q --bin harness"). cmd.exe
        // rejects a leading path written with forward slashes, so the first
        // token - and only the first - is normalised.
        let command = commandLine;
        if (process.platform === 'win32') {
            const m = /^(\S+)([\s\S]*)$/.exec(commandLine);
            command = m[1].replace(/\//g, '\\') + m[2];
        }
        // The harness runs in the CALLER's directory, not this script's: a port
        // invokes the runner out of the cameraunlock-core submodule and its own
        // harness path is relative to its own repo root.
        const child = spawn(command, { shell: true });
        let stdout = '', stderr = '';
        child.stdout.on('data', (d) => { stdout += d; });
        child.stderr.on('data', (d) => { stderr += d; });
        child.on('error', rej);
        child.on('close', (code) => res({ code, stdout, stderr }));
        child.stdin.on('error', () => { /* harness exited early; the close handler reports it */ });
        child.stdin.end(script);
    });
}

// ---------------------------------------------------------------- main

const args = parseArgs(process.argv.slice(2));
const doc = JSON.parse(readFileSync(args.vectors, 'utf8'));
if (doc.schema_version !== 1) {
    console.error(`unsupported schema_version ${doc.schema_version}; this runner speaks 1`);
    process.exit(2);
}

const selected = doc.vectors.filter((v) => !args.only || args.only.has(v.id));
if (args.only) {
    for (const id of args.only) {
        if (!selected.some((v) => v.id === id)) {
            console.error(`no vector with id "${id}"`);
            process.exit(2);
        }
    }
}

const { script, plan } = buildScript(selected);
const { code, stdout, stderr } = await runHarness(args.harness, script);

if (code !== 0) {
    console.error(`harness exited ${code}`);
    if (stderr.trim()) console.error(stderr.trim());
    process.exit(2);
}
const resultLines = stdout.split('\n').map((l) => l.trim()).filter((l) => l.length > 0);

let cursor = 0;
let failed = 0;
const failures = [];
const skipped = [];

for (const { vector, expanded, labels } of plan) {
    const status = resultLines[cursor++];
    if (status === undefined) {
        console.error(`harness ended before reporting on ${vector.id}`);
        if (stderr.trim()) console.error(stderr.trim());
        process.exit(2);
    }
    if (status.startsWith('skip')) {
        skipped.push({ vector, reason: status.slice(4).trim() || '(no reason given)' });
        console.log(`  SKIP  ${vector.id}  (section ${vector.section})`);
        continue;
    }
    if (status !== 'ok') {
        console.error(`${vector.id}: expected "ok" or "skip <reason>" after begin, got "${status}"`);
        process.exit(2);
    }

    const fields = UNIT_OUT_FIELDS[vector.unit];
    const outputs = [];
    for (let i = 0; i < expanded.length; i++) {
        const raw = resultLines[cursor++];
        if (raw === undefined) {
            console.error(`${vector.id}: harness produced only ${i} of ${expanded.length} result lines`);
            if (stderr.trim()) console.error(stderr.trim());
            process.exit(2);
        }
        const parts = raw.split(/\s+/).map(Number);
        if (parts.length !== fields.length || parts.some(Number.isNaN)) {
            console.error(`${vector.id} step ${i}: malformed harness output "${raw}"`);
            process.exit(2);
        }
        outputs.push(Object.fromEntries(fields.map((f, k) => [f, parts[k]])));
    }

    const ctx = { outputs, labels, steps: expanded, unit: vector.unit };
    const problems = [];
    for (const a of vector.assert) {
        const problem = checkAssertion(a, ctx);
        if (problem) problems.push(`${a.kind}: ${problem}`);
    }

    if (problems.length === 0) {
        if (args.verbose) console.log(`  PASS  ${vector.id}  (section ${vector.section}, ${expanded.length} steps)`);
    } else {
        failed++;
        failures.push({ vector, problems });
        console.log(`  FAIL  ${vector.id}  (section ${vector.section})`);
    }
}

if (failures.length > 0) {
    console.log('');
    for (const { vector, problems } of failures) {
        console.log(`${vector.id} - ${vector.title}`);
        console.log(`  doctrine: docs/porting-the-pipeline.md section ${vector.section}`);
        console.log(`  catches:  ${vector.catches}`);
        for (const p of problems) console.log(`  ! ${p}`);
        console.log('');
    }
}

if (skipped.length > 0) {
    console.log('');
    console.log('Skipped by the harness:');
    for (const { vector, reason } of skipped) console.log(`  ${vector.id}: ${reason}`);
    console.log('');
}

if (cursor !== resultLines.length) {
    console.error(`harness produced ${resultLines.length - cursor} unread trailing lines`);
    process.exit(2);
}

const total = plan.length;
const ran = total - skipped.length;
console.log(`${ran - failed}/${ran} conformance vectors passed`
    + (skipped.length > 0 ? `, ${skipped.length} skipped` : ''));
process.exit(failed === 0 ? 0 : 1);
