#!/usr/bin/env node
// Emits the C# and C++ alias tables, the C++ defaults table and the C++ test expectation for
// the concept ranges from data/config-schema.json, the C++ test expectation for
// data/pipeline-conformance.json's preference_modes, and the C# and C++ key-name tables from
// data/keys.json.
//
//   node scripts/generate-config-schema.mjs            write the generated files
//   node scripts/generate-config-schema.mjs --check    fail if any is stale
//
// The two languages resolve config keys through tables generated from ONE file, so a
// key added to one half cannot go missing from the other.
//
// preference_modes is the other way round: the runtime encode/decode is hand-written in
// both languages and only the TEST expectation is generated. C++ cannot read the JSON, so
// its test compares against this header, while the C# test reads the file directly. A
// generated runtime table would agree with the file by construction and prove nothing.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const schemaPath = join(repoRoot, 'data', 'config-schema.json');
const csharpPath = join(repoRoot, 'csharp', 'src', 'CameraUnlock.Core', 'Config', 'ConfigKeySchema.g.cs');
const cppPath = join(repoRoot, 'cpp', 'include', 'cameraunlock', 'config', 'config_key_schema.g.h');
const conformancePath = join(repoRoot, 'data', 'pipeline-conformance.json');
const preferenceModesTestPath = join(repoRoot, 'cpp', 'tests', 'preference_modes.g.h');
const conceptRangesTestPath = join(repoRoot, 'cpp', 'tests', 'concept_ranges.g.h');
const keysPath = join(repoRoot, 'data', 'keys.json');
const keyNamesCppPath = join(repoRoot, 'cpp', 'include', 'cameraunlock', 'input', 'key_names.g.h');
const keyNamesCsharpPath = join(repoRoot, 'csharp', 'src', 'CameraUnlock.Core', 'Input', 'KeyNames.g.cs');

const normalize = (key) => key.toLowerCase().replace(/[_-]/g, '');

// Every id becomes a C# `const` name and, prefixed with 'k', a C++ `constexpr` name.
const identifierPattern = /^[A-Za-z_][A-Za-z0-9_]*$/;
// Every key and alias is emitted verbatim into a C# and a C++ string literal. Nothing is
// escaped on the way, so the accepted alphabet is the one that needs no escaping.
const spellingPattern = /^[A-Za-z0-9_-]+$/;
// String defaults are emitted into a C++ string literal the same way, so the same rule.
const stringDefaultPattern = /^[A-Za-z0-9_-]*$/;

const conceptFields = new Set([
    'id', 'section', 'key', 'type', 'default', 'aliases', 'doc',
    'canonical', 'canonical_reason', 'file_comment', 'range', 'codec', 'canonical_default',
]);

const valueTypes = {
    // The lower bound is INT_MIN + 1: the literal -2147483648 is unary minus applied to
    // 2147483648, which does not fit in an int, so it narrows inside the C++ aggregate
    // initialiser and fails to compile.
    int: (v) => Number.isInteger(v) && v >= -2147483647 && v <= 2147483647,
    float: (v) => typeof v === 'number' && Number.isFinite(v),
    bool: (v) => typeof v === 'boolean',
    string: (v) => typeof v === 'string' && stringDefaultPattern.test(v),
    color: (v) => Array.isArray(v) && v.length === 4 &&
        v.every((c) => typeof c === 'number' && Number.isFinite(c) && c >= 0 && c <= 1),
};

class SchemaError extends Error {}

function schemaError(where, problem) {
    throw new SchemaError(`data/config-schema.json ${where}: ${problem}`);
}

function checkSpelling(where, label, spelling) {
    if (typeof spelling !== 'string') {
        schemaError(where, `${label} is ${JSON.stringify(spelling)}, expected a string`);
    }
    if (!spellingPattern.test(spelling)) {
        schemaError(where, `${label} '${spelling}' contains a character outside [A-Za-z0-9_-]. ` +
            'Keys and aliases are emitted verbatim into C# and C++ string literals and are not escaped');
    }
    if (normalize(spelling) === '') {
        schemaError(where, `${label} '${spelling}' normalizes to the empty string`);
    }
}

// Player text: canonical_reason and the file_comment lines. It goes into C# and C++ string
// literals unescaped, so it is printable ASCII without '"' and '\', the two characters either
// language would need escaped.
function checkPlayerText(where, label, text) {
    if (typeof text !== 'string' || text.length === 0) {
        schemaError(where, `${label} is ${JSON.stringify(text)}, expected a non-empty string`);
    }
    for (const c of text) {
        const code = c.charCodeAt(0);
        if (code < 0x20 || code > 0x7E || c === '"' || c === '\\') {
            schemaError(where, `${label} holds ${JSON.stringify(c)}. Player text is printable ASCII without '"' ` +
                "and '\\', because it is emitted unescaped into C# and C++ string literals");
        }
    }
    if (text !== text.trim()) schemaError(where, `${label} ${JSON.stringify(text)} starts or ends with a space`);
}

// A canonical_default must read in both dialects and be spelled the way the codecs write it,
// because it is written into canonical files as it stands: Ctrl, Shift and Alt in that order,
// the key's table name, ', ' between items.
function checkCanonicalBindings(where, text, keyTable) {
    if (text === '') return;
    const byName = new Map(keyTable.keys.map((k) => [k.name, k]));
    const seen = new Set();
    for (const item of text.split(', ')) {
        const tokens = item.split('+');
        const key = tokens.pop();
        let previous = -1;
        for (const token of tokens) {
            const index = modifierNames.indexOf(token);
            if (index <= previous) {
                schemaError(where, `canonical_default item '${item}' is not written as the codecs write it: ` +
                    `expected ${modifierNames.join(', ')} at most once each and in that order, then one key`);
            }
            previous = index;
        }
        const entry = byName.get(key);
        if (entry === undefined) {
            schemaError(where, `canonical_default item '${item}': '${key}' is not a key name as data/keys.json spells it`);
        }
        if (entry.vk === 0) schemaError(where, `canonical_default names '${key}', which has no Windows key code`);
        if (entry.unity === 0) schemaError(where, `canonical_default names '${key}', which has no Unity KeyCode`);
        if (seen.has(item)) schemaError(where, `canonical_default lists '${item}' twice`);
        seen.add(item);
    }
}

function checkRange(where, concept) {
    const { range } = concept;
    if (concept.type !== 'int' && concept.type !== 'float') {
        schemaError(where, `has a range, but ranges are for int and float concepts and this one is '${concept.type}'`);
    }
    if (range === null || typeof range !== 'object' || Array.isArray(range)) {
        schemaError(where, `range is ${JSON.stringify(range)}, expected {"min": ..., "max": ...}`);
    }
    for (const field of Object.keys(range)) {
        if (field !== 'min' && field !== 'max') schemaError(where, `range has unknown field '${field}'`);
    }
    if (!('min' in range) && !('max' in range)) schemaError(where, 'range names neither min nor max');
    for (const field of ['min', 'max']) {
        if (field in range && !valueTypes[concept.type](range[field])) {
            schemaError(where, `range ${field} ${JSON.stringify(range[field])} is not a valid '${concept.type}'`);
        }
    }
    if ('min' in range && 'max' in range && range.min > range.max) {
        schemaError(where, `range min ${range.min} is above max ${range.max}`);
    }
    if (('min' in range && concept.default < range.min) || ('max' in range && concept.default > range.max)) {
        schemaError(where, `default ${concept.default} is outside its own range ${JSON.stringify(range)}`);
    }
}

function checkCanonicalFields(where, concept, keyTable) {
    if (typeof concept.canonical !== 'boolean') {
        schemaError(where, `canonical is ${JSON.stringify(concept.canonical)}, expected true or false`);
    }

    if (!concept.canonical) {
        if (!('canonical_reason' in concept)) {
            schemaError(where, 'is not canonical and has no canonical_reason, the line a player is shown for its key');
        }
        checkPlayerText(where, 'canonical_reason', concept.canonical_reason);
        for (const field of ['file_comment', 'range', 'codec', 'canonical_default']) {
            if (field in concept) {
                schemaError(where, `is not canonical, so it has no ${field}: no canonical file writes its key`);
            }
        }
        return;
    }

    if ('canonical_reason' in concept) schemaError(where, 'is canonical, so it has no canonical_reason');

    const comment = concept.file_comment;
    if (!Array.isArray(comment) || comment.length < 1 || comment.length > 2) {
        schemaError(where, `file_comment is ${JSON.stringify(comment)}, expected one or two lines of player text`);
    }
    comment.forEach((line, i) => checkPlayerText(where, `file_comment[${i}]`, line));

    if ('range' in concept) checkRange(where, concept);

    if (concept.type === 'string') {
        if (concept.codec !== 'hotkey') {
            schemaError(where, `codec is ${JSON.stringify(concept.codec)}: a canonical string concept needs one, ` +
                "and 'hotkey' is the only one");
        }
    } else if ('codec' in concept) {
        schemaError(where, `has a codec, but only string concepts take one and this one is '${concept.type}'`);
    }

    if ('canonical_default' in concept) {
        if (concept.codec !== 'hotkey') schemaError(where, 'has a canonical_default, which only hotkey concepts take');
        if (typeof concept.canonical_default !== 'string') {
            schemaError(where, `canonical_default is ${JSON.stringify(concept.canonical_default)}, expected a string`);
        }
        checkCanonicalBindings(where, concept.canonical_default, keyTable);
    }
}

function validateSchema(schema, keyTable) {
    if (!Array.isArray(schema.sections)) schemaError('sections', 'missing, expected an array');
    if (!Array.isArray(schema.concepts)) schemaError('concepts', 'missing, expected an array');
    if (!Array.isArray(schema.retired)) schemaError('retired', 'missing, expected an array');

    // Both parsers dispatch the retired-key warning from one hardcoded branch
    // (HeadTrackingConfigData.ApplyValues 'case ConfigKeySchema.Keys.Smoothing',
    // head_tracking_config.cpp 'canonical == config_keys::kSmoothing'). A second retired
    // concept resolves through the alias table, matches no branch, and is dropped with
    // nothing in the log. Make both parsers test IsRetired / IsRetiredConfigKey ahead of
    // the switch, then this check goes.
    if (schema.retired.length > 1) {
        schemaError('retired', `${schema.retired.length} retired concepts, but the C# and C++ parsers ` +
            'each handle exactly one, named in a hardcoded branch. The extra concept would parse, ' +
            'resolve, match no branch and be ignored with no warning');
    }

    const ids = new Map();

    const claimId = (where, id) => {
        if (typeof id !== 'string') schemaError(where, `id is ${JSON.stringify(id)}, expected a string`);
        if (!identifierPattern.test(id)) {
            schemaError(where, `id '${id}' is not a valid identifier. It becomes a C# 'const' name ` +
                "and a C++ 'constexpr' name, so it must match [A-Za-z_][A-Za-z0-9_]*");
        }
        if (ids.has(id)) {
            schemaError(where, `id '${id}' is already used by ${ids.get(id)}. Two concepts sharing an id ` +
                'emit the same constant twice (C# CS0102)');
        }
        ids.set(id, where);
    };

    const checkAliases = (where, aliases, minimum) => {
        if (!Array.isArray(aliases)) {
            schemaError(where, `aliases is ${JSON.stringify(aliases)}, expected an array of strings`);
        }
        if (aliases.length < minimum) {
            schemaError(where, `aliases has ${aliases.length} entries, expected at least ${minimum}`);
        }
        aliases.forEach((alias, i) => checkSpelling(where, `aliases[${i}]`, alias));
    };

    schema.concepts.forEach((concept, i) => {
        const where = `concepts[${i}]${typeof concept?.id === 'string' ? ` ('${concept.id}')` : ''}`;
        if (concept === null || typeof concept !== 'object') schemaError(where, 'is not an object');
        claimId(where, concept.id);
        for (const field of Object.keys(concept)) {
            if (!conceptFields.has(field)) schemaError(where, `unknown field '${field}'`);
        }
        if (!schema.sections.includes(concept.section)) {
            schemaError(where, `names section '${concept.section}', which is not in sections[]`);
        }
        checkSpelling(where, 'key', concept.key);
        checkAliases(where, concept.aliases, 0);

        // type and default are the only published record of what a key accepts and what it
        // does when absent, and AGENTS.md makes a changed default a breaking change. They
        // are checked here so a malformed one is caught at build time; agreement with the
        // C# field initialisers is pinned by ConfigSchemaDefaultsTests, and with the C++
        // ones by config_schema_tests.cpp against kConfigConceptDefaults.
        const check = valueTypes[concept.type];
        if (check === undefined) {
            schemaError(where, `type '${concept.type}' is not one of ${Object.keys(valueTypes).join(', ')}`);
        }
        if (!('default' in concept)) schemaError(where, `has no default (type '${concept.type}')`);
        if (!check(concept.default)) {
            const alphabet = concept.type === 'string'
                ? '. String defaults are emitted verbatim into a C++ string literal, so they are limited to [A-Za-z0-9_-]'
                : '';
            schemaError(where, `default ${JSON.stringify(concept.default)} is not a valid '${concept.type}'${alphabet}`);
        }
        checkCanonicalFields(where, concept, keyTable);
    });

    schema.retired.forEach((concept, i) => {
        const where = `retired[${i}]${typeof concept?.id === 'string' ? ` ('${concept.id}')` : ''}`;
        if (concept === null || typeof concept !== 'object') schemaError(where, 'is not an object');
        claimId(where, concept.id);
        checkAliases(where, concept.aliases, 1);
    });
}

function buildEntries(schema) {
    const entries = [];
    const owner = new Map();

    function claim(alias, canonical, retired, where) {
        const normalized = normalize(alias);
        if (owner.has(normalized)) {
            schemaError(where,
                `alias '${alias}' normalizes to '${normalized}', already claimed by '${owner.get(normalized)}'`);
        }
        owner.set(normalized, canonical);
        entries.push({ normalized, canonical, retired });
    }

    schema.concepts.forEach((concept, i) => {
        const canonical = normalize(concept.key);
        for (const alias of [concept.key, ...concept.aliases]) {
            claim(alias, canonical, false, `concepts[${i}] ('${concept.id}')`);
        }
    });
    schema.retired.forEach((concept, i) => {
        const canonical = normalize(concept.aliases[0]);
        for (const alias of concept.aliases) {
            claim(alias, canonical, true, `retired[${i}] ('${concept.id}')`);
        }
    });

    entries.sort((a, b) => (a.normalized < b.normalized ? -1 : a.normalized > b.normalized ? 1 : 0));
    return entries;
}

const banner = (comment, source = 'data/config-schema.json', edit = 'the schema') => [
    `${comment} Generated by scripts/generate-config-schema.mjs from ${source}.`,
    `${comment} Do not edit. Edit ${edit} and re-run the generator; \`pixi run check-config-schema\``,
    `${comment} fails the build when this file is stale.`,
].join('\n');

function renderCSharp(schema, entries) {
    const rows = entries
        .map((e) => `            { "${e.normalized}", "${e.canonical}" },`)
        .join('\n');
    const canonicalConsts = schema.concepts
        .map((c) => `            public const string ${c.id} = "${normalize(c.key)}";`)
        .join('\n');
    const retiredConsts = schema.retired
        .map((c) => `            public const string ${c.id} = "${normalize(c.aliases[0])}";`)
        .join('\n');
    const retiredRows = schema.retired
        .map((c) => `            "${normalize(c.aliases[0])}",`)
        .join('\n');

    return `${banner('//')}

using System.Collections.Generic;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The canonical config-key vocabulary, shared with the C++ half of the library.
    /// Matching is section-less and case-insensitive: <see cref="Normalize"/> lowercases a
    /// key and strips '_' and '-', so Network.UDPPort, udp_port and Udp-Port all resolve to
    /// the same canonical name.
    /// </summary>
    public static class ConfigKeySchema
    {
        /// <summary>Canonical names, as returned by <see cref="Resolve"/>.</summary>
        public static class Keys
        {
${canonicalConsts}

${retiredConsts}
        }

        private static readonly Dictionary<string, string> Aliases = new Dictionary<string, string>
        {
${rows}
        };

        private static readonly HashSet<string> Retired = new HashSet<string>
        {
${retiredRows}
        };

        /// <summary>
        /// Strips '_' and '-' and folds ASCII A-Z to lower case. Applied to both sides of a
        /// lookup.
        /// <para>
        /// Deliberately not <c>ToLowerInvariant</c>: full Unicode case folding maps
        /// U+212A KELVIN SIGN onto 'k', so a key spelled with one bound here and was
        /// ignored by the C++ table, which folds A-Z and nothing else. Every spelling in
        /// the schema is ASCII, so nothing accepted by the table needs the wider fold.
        /// </para>
        /// </summary>
        public static string Normalize(string key)
        {
            if (key == null) return null;
            var normalized = new StringBuilder(key.Length);
            for (int i = 0; i < key.Length; i++)
            {
                char c = key[i];
                if (c == '_' || c == '-') continue;
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                normalized.Append(c);
            }
            return normalized.ToString();
        }

        /// <summary>
        /// Resolves any accepted spelling to its canonical normalized name, or null when the
        /// key belongs to no concept in the schema.
        /// </summary>
        public static string Resolve(string key)
        {
            string normalized = Normalize(key);
            if (normalized == null) return null;
            string canonical;
            return Aliases.TryGetValue(normalized, out canonical) ? canonical : null;
        }

        /// <summary>
        /// True when the canonical name names a retired concept: it parses, and is ignored.
        /// </summary>
        public static bool IsRetired(string canonicalKey)
        {
            return canonicalKey != null && Retired.Contains(canonicalKey);
        }
    }
}
`;
}

const cppFloat = (v) => `${Number.isInteger(v) ? v.toFixed(1) : String(v)}f`;

const cppValueTypes = { int: 'kInt', float: 'kFloat', bool: 'kBool', string: 'kString', color: 'kColor' };

function renderCppDefault(concept) {
    const cells = { int: '0', float: '0.0f', bool: 'false', string: 'nullptr', color: '{0.0f, 0.0f, 0.0f, 0.0f}' };
    const v = concept.default;
    switch (concept.type) {
        case 'int': cells.int = String(v); break;
        case 'float': cells.float = cppFloat(v); break;
        case 'bool': cells.bool = String(v); break;
        case 'string': cells.string = `"${v}"`; break;
        case 'color': cells.color = `{${v.map(cppFloat).join(', ')}}`; break;
    }
    return `    { "${concept.id}", config_keys::k${concept.id}, ConfigValueType::${cppValueTypes[concept.type]}, ` +
        `${cells.int}, ${cells.float}, ${cells.bool}, ${cells.string}, ${cells.color} },`;
}

function renderCpp(schema, entries) {
    const rows = entries
        .map((e) => `    { "${e.normalized}", "${e.canonical}", ${e.retired ? 'true' : 'false'} },`)
        .join('\n');
    const defaultRows = schema.concepts.map(renderCppDefault).join('\n');
    const canonicalConsts = schema.concepts
        .map((c) => `inline constexpr const char* k${c.id} = "${normalize(c.key)}";`)
        .join('\n');
    const retiredConsts = schema.retired
        .map((c) => `inline constexpr const char* k${c.id} = "${normalize(c.aliases[0])}";`)
        .join('\n');

    return `${banner('//')}

#pragma once

#include <cstddef>
#include <string>

namespace cameraunlock {
namespace config_keys {

${canonicalConsts}

${retiredConsts}

}  // namespace config_keys

/// One accepted spelling of a config key. \`normalized\` is the lookup form (lowercased,
/// '_' and '-' stripped); \`canonical\` is the name every spelling of the concept resolves
/// to, matching ConfigKeySchema.Keys on the C# side.
struct ConfigKeyAlias {
    const char* normalized;
    const char* canonical;
    bool retired;
};

inline constexpr ConfigKeyAlias kConfigKeyAliases[] = {
${rows}
};

inline constexpr size_t kConfigKeyAliasCount = sizeof(kConfigKeyAliases) / sizeof(kConfigKeyAliases[0]);

enum class ConfigValueType { kInt, kFloat, kBool, kString, kColor };

/// The default data/config-schema.json declares for one concept: what a key means when a
/// file leaves it out. Only the member \`type\` names carries the default; the rest are
/// zero, and \`string_value\` is nullptr unless \`type\` is kString.
struct ConfigConceptDefault {
    const char* id;
    const char* canonical;
    ConfigValueType type;
    int int_value;
    float float_value;
    bool bool_value;
    const char* string_value;
    float color_value[4];
};

inline constexpr ConfigConceptDefault kConfigConceptDefaults[] = {
${defaultRows}
};

inline constexpr size_t kConfigConceptDefaultCount =
    sizeof(kConfigConceptDefaults) / sizeof(kConfigConceptDefaults[0]);

/// Lowercases a key and strips '_' and '-'. Applied to both sides of a lookup.
inline std::string NormalizeConfigKey(const std::string& key) {
    std::string out;
    out.reserve(key.size());
    for (char c : key) {
        if (c == '_' || c == '-') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

/// Resolves any accepted spelling to its canonical normalized name, or nullptr when the
/// key belongs to no concept in the schema. The returned pointer is static storage.
inline const char* ResolveConfigKey(const std::string& key) {
    const std::string normalized = NormalizeConfigKey(key);
    for (size_t i = 0; i < kConfigKeyAliasCount; ++i) {
        if (normalized == kConfigKeyAliases[i].normalized) return kConfigKeyAliases[i].canonical;
    }
    return nullptr;
}

/// True when the canonical name names a retired concept: it parses, and is ignored.
inline bool IsRetiredConfigKey(const char* canonical_key) {
    if (canonical_key == nullptr) return false;
    for (size_t i = 0; i < kConfigKeyAliasCount; ++i) {
        if (kConfigKeyAliases[i].retired && std::string(canonical_key) == kConfigKeyAliases[i].canonical) {
            return true;
        }
    }
    return false;
}

}  // namespace cameraunlock
`;
}

function preferenceModesError(where, problem) {
    throw new SchemaError(`data/pipeline-conformance.json preference_modes.${where}: ${problem}`);
}

const modeNamePattern = /^[a-z][a-z0-9_]*$/;
const cppTrackingModePattern = /^TrackingMode::[A-Za-z][A-Za-z0-9]*$/;

// Checks the block is well formed. Whether the runtime agrees with it is left to the tests,
// which name the mode that disagrees and both values.
function validateTrackingModes(conformance, schema) {
    const block = conformance.preference_modes?.tracking_mode;
    if (block === null || typeof block !== 'object') preferenceModesError('tracking_mode', 'missing, expected an object');

    const { channels, modes } = block;
    if (!Array.isArray(channels) || channels.length === 0) {
        preferenceModesError('tracking_mode.channels', 'expected a non-empty array of config-schema concept ids');
    }
    channels.forEach((channel, i) => {
        const concept = schema.concepts.find((c) => c.id === channel);
        if (concept === undefined) {
            preferenceModesError(`tracking_mode.channels[${i}]`, `'${channel}' is not a concept id in data/config-schema.json`);
        }
        if (concept.type !== 'bool') {
            preferenceModesError(`tracking_mode.channels[${i}]`, `'${channel}' is a '${concept.type}' concept, expected 'bool'`);
        }
    });

    if (!Array.isArray(modes) || modes.length === 0) {
        preferenceModesError('tracking_mode.modes', 'expected a non-empty array');
    }
    const seen = { name: new Set(), cpp: new Set(), pair: new Set() };
    modes.forEach((mode, i) => {
        const where = `tracking_mode.modes[${i}]`;
        if (typeof mode?.name !== 'string' || !modeNamePattern.test(mode.name)) {
            preferenceModesError(where, `name ${JSON.stringify(mode?.name)} does not match ${modeNamePattern}`);
        }
        if (typeof mode.cpp !== 'string' || !cppTrackingModePattern.test(mode.cpp)) {
            preferenceModesError(where, `cpp ${JSON.stringify(mode.cpp)} does not match ${cppTrackingModePattern}. ` +
                'It is emitted verbatim into the C++ test expectation');
        }
        for (const channel of channels) {
            if (typeof mode[channel] !== 'boolean') {
                preferenceModesError(where, `${channel} is ${JSON.stringify(mode[channel])}, expected true or false`);
            }
        }
        const pair = channels.map((c) => mode[c]).join('/');
        for (const [kind, value] of [['name', mode.name], ['cpp', mode.cpp], ['pair', pair]]) {
            if (seen[kind].has(value)) preferenceModesError(where, `${kind} '${value}' is declared twice`);
            seen[kind].add(value);
        }
    });
    return block;
}

const snakeCase = (id) => id.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase();

function renderPreferenceModesTest(block) {
    const fields = block.channels.map((c) => `    bool ${snakeCase(c)};`).join('\n');
    const rows = block.modes
        .map((m) => `    { "${m.name}", cameraunlock::${m.cpp}, ${block.channels.map((c) => String(m[c])).join(', ')} },`)
        .join('\n');

    return `${banner('//', 'data/pipeline-conformance.json', 'preference_modes')}
//
// The EXPECTATION tracking_mode_tests.cpp holds cameraunlock/tracking/tracking_mode.h to.
// Nothing outside cpp/tests includes it: the runtime mapping is hand-written, so an edit
// to either side alone fails the test.

#pragma once

#include <cameraunlock/tracking/tracking_mode.h>

#include <cstddef>

namespace preference_modes {

struct TrackingModeExpectation {
    const char* name;
    cameraunlock::TrackingMode mode;
${fields}
};

// preference_modes.tracking_mode.modes in file order, which is the cycle order.
inline constexpr TrackingModeExpectation kTrackingModes[] = {
${rows}
};

inline constexpr size_t kTrackingModeCount = sizeof(kTrackingModes) / sizeof(kTrackingModes[0]);

}  // namespace preference_modes
`;
}

function renderConceptRangesTest(schema) {
    const bound = (range, field) => (field in range ? `true, ${range[field]}` : 'false, 0');
    const rows = schema.concepts
        .filter((c) => 'range' in c)
        .map((c) => `    { "${c.id}", ${bound(c.range, 'min')}, ${bound(c.range, 'max')} },`)
        .join('\n');

    return `${banner('//', 'data/config-schema.json', 'the schema')}
//
// The ranges data/config-schema.json declares, which config_schema_tests.cpp holds to the
// constants the flat readers guard with. Nothing outside cpp/tests includes it.

#pragma once

#include <cstddef>

namespace concept_ranges {

struct ConceptRange {
    const char* id;
    bool has_min;
    double min;
    bool has_max;
    double max;
};

inline constexpr ConceptRange kConceptRanges[] = {
${rows}
};

inline constexpr size_t kConceptRangeCount = sizeof(kConceptRanges) / sizeof(kConceptRanges[0]);

}  // namespace concept_ranges
`;
}

function keysError(where, problem) {
    throw new SchemaError(`data/keys.json ${where}: ${problem}`);
}

// A key name is written into config files and emitted verbatim into C# and C++ string
// literals, so it is ASCII letters and digits only.
const keyNamePattern = /^[A-Za-z0-9]+$/;
const vkPattern = /^0x[0-9A-F]{2}$/;
// The binding codecs hard-code these three, in this order, as the modifier flags 1, 2 and 4
// and as the order a binding writes them.
const modifierNames = ['Ctrl', 'Shift', 'Alt'];

function parseVk(where, text) {
    if (typeof text !== 'string' || !vkPattern.test(text)) {
        keysError(where, `vk ${JSON.stringify(text)} is not 0x and two upper-case hex digits`);
    }
    const vk = Number.parseInt(text.slice(2), 16);
    if (vk < 0x01 || vk > 0xFE) keysError(where, `vk ${text} is outside 0x01-0xFE`);
    return vk;
}

// Returns what the generated files carry: every key with its codes (0 for none), the
// aliases by key index, and the modifiers with their Unity codes resolved.
function validateKeys(doc) {
    if (doc.schema_version !== 1) keysError('schema_version', `is ${JSON.stringify(doc.schema_version)}, expected 1`);
    for (const field of ['about', 'unity_sources']) {
        if (!Array.isArray(doc[field]) || doc[field].length === 0 || !doc[field].every((l) => typeof l === 'string')) {
            keysError(field, 'expected a non-empty array of strings');
        }
    }
    if (!Array.isArray(doc.keys) || doc.keys.length === 0) keysError('keys', 'expected a non-empty array');
    if (!Array.isArray(doc.modifiers)) keysError('modifiers', 'expected an array');

    // Every spelling a value can use, compared ASCII case-insensitively as the codecs read
    // them. The modifier tokens are claimed first, so no key can take one.
    const spellings = new Map();
    const claim = (where, spelling) => {
        if (typeof spelling !== 'string' || !keyNamePattern.test(spelling)) {
            keysError(where, `${JSON.stringify(spelling)} is not ASCII letters and digits`);
        }
        const folded = spelling.toLowerCase();
        if (spellings.has(folded)) keysError(where, `'${spelling}' is already spelled by ${spellings.get(folded)}`);
        spellings.set(folded, where);
    };
    modifierNames.forEach((name) => claim(`modifier ${name}`, name));

    const byVk = new Map();
    const byUnity = new Map();
    const keys = [];
    const aliases = [];
    doc.keys.forEach((key, i) => {
        const where = `keys[${i}]${typeof key?.name === 'string' ? ` ('${key.name}')` : ''}`;
        if (key === null || typeof key !== 'object') keysError(where, 'is not an object');
        for (const field of Object.keys(key)) {
            if (!['name', 'vk', 'unity', 'aliases'].includes(field)) keysError(where, `unknown field '${field}'`);
        }
        claim(where, key.name);
        const vk = key.vk === undefined ? 0 : parseVk(where, key.vk);
        let unity = 0;
        if (key.unity !== undefined) {
            if (!Number.isInteger(key.unity) || key.unity < 1 || key.unity > 0x7FFFFFFF) {
                keysError(where, `unity ${JSON.stringify(key.unity)} is not a positive int`);
            }
            unity = key.unity;
        }
        if (vk === 0 && unity === 0) keysError(where, 'has neither a vk nor a unity value');
        if (vk !== 0) {
            if (byVk.has(vk)) keysError(where, `vk ${key.vk} is already the code of '${byVk.get(vk)}'`);
            byVk.set(vk, key.name);
        }
        if (unity !== 0) {
            if (byUnity.has(unity)) keysError(where, `unity ${unity} is already the value of '${byUnity.get(unity)}'`);
            byUnity.set(unity, key.name);
        }
        if (key.aliases !== undefined) {
            if (!Array.isArray(key.aliases) || key.aliases.length === 0) keysError(where, 'aliases must be a non-empty array');
            if (unity === 0) keysError(where, 'has aliases but no unity value; an alias is a second Unity name for one value');
            key.aliases.forEach((alias, j) => {
                claim(`${where} aliases[${j}]`, alias);
                aliases.push({ alias, key: i });
            });
        }
        keys.push({ name: key.name, vk, unity });
    });

    const unbound = doc.unity_unbound;
    if (unbound === null || typeof unbound !== 'object' || unbound.unity !== 0) {
        keysError('unity_unbound', 'expected {"name": ..., "unity": 0}, the KeyCode Unity uses for no key');
    }
    claim('unity_unbound', unbound.name);

    if (doc.modifiers.length !== modifierNames.length) {
        keysError('modifiers', `has ${doc.modifiers.length} entries, expected ${modifierNames.join(', ')}`);
    }
    const byName = new Map(keys.map((k) => [k.name, k]));
    const modifiers = doc.modifiers.map((modifier, i) => {
        const where = `modifiers[${i}]`;
        if (modifier?.name !== modifierNames[i]) {
            keysError(where, `is ${JSON.stringify(modifier?.name)}, expected '${modifierNames[i]}': ` +
                'the codecs write the modifiers in the order Ctrl, Shift, Alt');
        }
        const vk = parseVk(where, modifier.vk);
        if (byVk.has(vk)) {
            keysError(where, `vk ${modifier.vk} is also the code of key '${byVk.get(vk)}'. A modifier's own code has ` +
                'no key name, so a native value that binds it as a key writes it in hex');
        }
        if (!Array.isArray(modifier.unity) || modifier.unity.length !== 2) {
            keysError(where, 'unity must name the left and the right key, in that order');
        }
        const [left, right] = modifier.unity.map((name) => {
            const key = byName.get(name);
            if (key === undefined || key.unity === 0) keysError(where, `unity names '${name}', which is not a key with a unity value`);
            return key.unity;
        });
        return { name: modifier.name, vk, left, right };
    });

    return { keys, aliases, modifiers };
}

const hexVk = (vk) => (vk === 0 ? '0' : `0x${vk.toString(16).toUpperCase().padStart(2, '0')}`);

function renderKeyNamesCpp(table) {
    const keys = table.keys.map((k) => `    {"${k.name}", ${hexVk(k.vk)}, ${k.unity}},`).join('\n');
    const aliases = table.aliases.map((a) => `    {"${a.alias}", ${a.key}},`).join('\n');
    const modifiers = table.modifiers.map((m) => `    {"${m.name}", ${hexVk(m.vk)}, ${m.left}, ${m.right}},`).join('\n');

    return `${banner('//', 'data/keys.json', 'the key table')}

#pragma once

#include <cstddef>

namespace cameraunlock::input {

/// One key: the name a hotkey value writes it as, its Windows virtual-key code and its
/// UnityEngine.KeyCode value, each 0 where the key has none.
struct KeyNameEntry {
    const char* name;
    int vk;
    int unity;
};

inline constexpr KeyNameEntry kKeyNames[] = {
${keys}
};

inline constexpr std::size_t kKeyNameCount = sizeof(kKeyNames) / sizeof(kKeyNames[0]);

/// A second name Unity declares for a key's value. It reads as kKeyNames[key] and is
/// written as that key's name.
struct KeyNameAlias {
    const char* alias;
    std::size_t key;
};

inline constexpr KeyNameAlias kKeyNameAliases[] = {
${aliases}
};

inline constexpr std::size_t kKeyNameAliasCount = sizeof(kKeyNameAliases) / sizeof(kKeyNameAliases[0]);

/// The modifier tokens, in the order a binding writes them. \`vk\` is the code
/// GetAsyncKeyState reports for either side; \`unity_left\` and \`unity_right\` are the two
/// KeyCode values Unity reports.
struct KeyModifierEntry {
    const char* name;
    int vk;
    int unity_left;
    int unity_right;
};

inline constexpr KeyModifierEntry kKeyModifiers[] = {
${modifiers}
};

inline constexpr std::size_t kKeyModifierCount = sizeof(kKeyModifiers) / sizeof(kKeyModifiers[0]);

}  // namespace cameraunlock::input
`;
}

function renderKeyNamesCsharp(table) {
    const keys = table.keys.map((k) => `            new Key("${k.name}", ${hexVk(k.vk)}, ${k.unity}),`).join('\n');
    const aliases = table.aliases.map((a) => `            new Alias("${a.alias}", ${a.key}),`).join('\n');
    const modifiers = table.modifiers.map((m) => `            new Modifier("${m.name}", ${hexVk(m.vk)}, ${m.left}, ${m.right}),`).join('\n');

    return `${banner('//', 'data/keys.json', 'the key table')}

namespace CameraUnlock.Core.Input
{
    /// <summary>The key names hotkey values are written with, shared with the C++ half of the library.</summary>
    internal static class KeyNames
    {
        /// <summary>
        /// One key: the name a hotkey value writes it as, its Windows virtual-key code and its
        /// UnityEngine.KeyCode value, each 0 where the key has none.
        /// </summary>
        internal struct Key
        {
            internal readonly string Name;
            internal readonly int VirtualKey;
            internal readonly int UnityKeyCode;

            internal Key(string name, int virtualKey, int unityKeyCode)
            {
                Name = name;
                VirtualKey = virtualKey;
                UnityKeyCode = unityKeyCode;
            }
        }

        /// <summary>
        /// A second name Unity declares for a key's value. It reads as Keys[KeyIndex] and is
        /// written as that key's name.
        /// </summary>
        internal struct Alias
        {
            internal readonly string Name;
            internal readonly int KeyIndex;

            internal Alias(string name, int keyIndex)
            {
                Name = name;
                KeyIndex = keyIndex;
            }
        }

        /// <summary>
        /// A modifier token. VirtualKey is the code GetAsyncKeyState reports for either side;
        /// UnityLeft and UnityRight are the two KeyCode values Unity reports.
        /// </summary>
        internal struct Modifier
        {
            internal readonly string Name;
            internal readonly int VirtualKey;
            internal readonly int UnityLeft;
            internal readonly int UnityRight;

            internal Modifier(string name, int virtualKey, int unityLeft, int unityRight)
            {
                Name = name;
                VirtualKey = virtualKey;
                UnityLeft = unityLeft;
                UnityRight = unityRight;
            }
        }

        internal static readonly Key[] Keys =
        {
${keys}
        };

        internal static readonly Alias[] Aliases =
        {
${aliases}
        };

        /// <summary>In the order a binding writes them: Ctrl, Shift, Alt.</summary>
        internal static readonly Modifier[] Modifiers =
        {
${modifiers}
        };
    }
}
`;
}

function main() {
    const keyTable = validateKeys(JSON.parse(readFileSync(keysPath, 'utf8')));
    const schema = JSON.parse(readFileSync(schemaPath, 'utf8'));
    validateSchema(schema, keyTable);
    const entries = buildEntries(schema);
    const trackingModes = validateTrackingModes(JSON.parse(readFileSync(conformancePath, 'utf8')), schema);

    const outputs = [
        { path: csharpPath, text: renderCSharp(schema, entries) },
        { path: cppPath, text: renderCpp(schema, entries) },
        { path: preferenceModesTestPath, text: renderPreferenceModesTest(trackingModes) },
        { path: conceptRangesTestPath, text: renderConceptRangesTest(schema) },
        { path: keyNamesCppPath, text: renderKeyNamesCpp(keyTable) },
        { path: keyNamesCsharpPath, text: renderKeyNamesCsharp(keyTable) },
    ];

    const check = process.argv.includes('--check');
    let stale = 0;

    for (const { path, text } of outputs) {
        if (check) {
            let current = null;
            try {
                current = readFileSync(path, 'utf8');
            } catch {
                current = null;
            }
            if (current !== text) {
                console.error(`STALE: ${path}`);
                stale++;
            }
        } else {
            writeFileSync(path, text);
            console.log(`wrote ${path}`);
        }
    }

    if (check) {
        if (stale > 0) {
            console.error(`\n${stale} generated file(s) do not match data/config-schema.json, ` +
                'data/pipeline-conformance.json and data/keys.json.');
            console.error('Run: node scripts/generate-config-schema.mjs');
            process.exit(1);
        }
        console.log(`config schema is in sync (${entries.length} aliases over ${schema.concepts.length} concepts, ` +
            `${trackingModes.modes.length} tracking modes, ${keyTable.keys.length} key names)`);
    }
}

try {
    main();
} catch (err) {
    // A malformed schema is user input to a build-time tool: the message is the whole
    // diagnostic and a stack trace over it is noise. Anything else is a bug in here and
    // keeps its stack.
    if (!(err instanceof SchemaError)) throw err;
    console.error(err.message);
    process.exit(1);
}
