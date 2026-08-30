#!/usr/bin/env node
// Emits the C# and C++ alias tables from data/config-schema.json.
//
//   node scripts/generate-config-schema.mjs            write the generated files
//   node scripts/generate-config-schema.mjs --check    fail if either is stale
//
// The two languages resolve config keys through tables generated from ONE file, so a
// key added to one half cannot go missing from the other.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const schemaPath = join(repoRoot, 'data', 'config-schema.json');
const csharpPath = join(repoRoot, 'csharp', 'src', 'CameraUnlock.Core', 'Config', 'ConfigKeySchema.g.cs');
const cppPath = join(repoRoot, 'cpp', 'include', 'cameraunlock', 'config', 'config_key_schema.g.h');

const normalize = (key) => key.toLowerCase().replace(/[_-]/g, '');

// Every id becomes a C# `const` name and, prefixed with 'k', a C++ `constexpr` name.
const identifierPattern = /^[A-Za-z_][A-Za-z0-9_]*$/;
// Every key and alias is emitted verbatim into a C# and a C++ string literal. Nothing is
// escaped on the way, so the accepted alphabet is the one that needs no escaping.
const spellingPattern = /^[A-Za-z0-9_-]+$/;

const valueTypes = {
    int: (v) => Number.isInteger(v),
    float: (v) => typeof v === 'number' && Number.isFinite(v),
    bool: (v) => typeof v === 'boolean',
    string: (v) => typeof v === 'string',
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

function validateSchema(schema) {
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
        if (!schema.sections.includes(concept.section)) {
            schemaError(where, `names section '${concept.section}', which is not in sections[]`);
        }
        checkSpelling(where, 'key', concept.key);
        checkAliases(where, concept.aliases, 0);

        // type and default are the only published record of what a key accepts and what it
        // does when absent, and AGENTS.md makes a changed default a breaking change. They
        // are checked here so a malformed one is caught at build time; agreement with the
        // C# field initialisers is pinned by ConfigSchemaDefaultsTests.
        const check = valueTypes[concept.type];
        if (check === undefined) {
            schemaError(where, `type '${concept.type}' is not one of ${Object.keys(valueTypes).join(', ')}`);
        }
        if (!('default' in concept)) schemaError(where, `has no default (type '${concept.type}')`);
        if (!check(concept.default)) {
            schemaError(where, `default ${JSON.stringify(concept.default)} is not a valid '${concept.type}'`);
        }
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

const banner = (comment) => [
    `${comment} Generated by scripts/generate-config-schema.mjs from data/config-schema.json.`,
    `${comment} Do not edit. Edit the schema and re-run the generator; \`pixi run check-config-schema\``,
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

function renderCpp(schema, entries) {
    const rows = entries
        .map((e) => `    { "${e.normalized}", "${e.canonical}", ${e.retired ? 'true' : 'false'} },`)
        .join('\n');
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

function main() {
    const schema = JSON.parse(readFileSync(schemaPath, 'utf8'));
    validateSchema(schema);
    const entries = buildEntries(schema);

    const outputs = [
        { path: csharpPath, text: renderCSharp(schema, entries) },
        { path: cppPath, text: renderCpp(schema, entries) },
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
            console.error(`\n${stale} generated file(s) do not match data/config-schema.json.`);
            console.error('Run: node scripts/generate-config-schema.mjs');
            process.exit(1);
        }
        console.log(`config schema is in sync (${entries.length} aliases over ${schema.concepts.length} concepts)`);
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
