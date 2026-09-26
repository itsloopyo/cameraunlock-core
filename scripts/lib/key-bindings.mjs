// The hotkey binding codec (design 1.5) over data/keys.json, for core's scripts: the rules of
// C++ ParseKeyBindings / FormatKeyBindings (the native dialect) and C# KeyBindings.TryParse /
// Format (the unity dialect), held to data/fixtures/canonical-ini/keys/cases.tsv by
// scripts/test-canonical-config.mjs.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { equalsAsciiIgnoreCase, trimSpaceTab } from "./canonical-ini.mjs";

const KEYS_PATH = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "data", "keys.json");
const { modifiers: MODIFIERS, keys: KEYS } = JSON.parse(fs.readFileSync(KEYS_PATH, "utf8"));

export const DIALECTS = Object.freeze(["native", "unity"]);

// The codes of the Ctrl, Shift and Alt keys, which no binding holds as its key: each modifier's own
// virtual-key code and both of its sides.
const MODIFIER_SIDES = MODIFIERS.flatMap((m) => m.unity.map((name) => KEYS.find((k) => k.name === name)));
const MODIFIER_KEYS = {
  native: new Set([...MODIFIERS.map((m) => parseInt(m.vk, 16)), ...MODIFIER_SIDES.map((k) => parseInt(k.vk, 16))]),
  unity: new Set(MODIFIER_SIDES.map((k) => k.unity)),
};

const quote = (text) => `'${text}'`;

function modifierIndex(token) {
  return MODIFIERS.findIndex((m) => equalsAsciiIgnoreCase(token, m.name));
}

function keyNamed(token) {
  return (
    KEYS.find((k) => equalsAsciiIgnoreCase(token, k.name)) ??
    KEYS.find((k) => (k.aliases ?? []).some((a) => equalsAsciiIgnoreCase(token, a))) ??
    null
  );
}

// The code of one key token in the dialect, or { error }.
function readKey(token, dialect) {
  if (token.length >= 2 && token[0] === "0" && (token[1] === "x" || token[1] === "X")) {
    if (dialect === "unity") {
      return { error: `${quote(token)} is a key code: expected a key name such as End or F9, because a Unity mod reads key names only` };
    }
    const digits = token.slice(2);
    const code = /^[0-9A-Fa-f]{1,2}$/.test(digits) ? parseInt(digits, 16) : 0;
    if (code < 0x01 || code > 0xfe) {
      return { error: `${quote(token)} is not a code from 0x01 to 0xFE: expected 0x and one or two hex digits` };
    }
    return { code };
  }
  const key = keyNamed(token);
  if (dialect === "unity") {
    if (key === null) return { error: `${quote(token)} is not a key name: expected a name such as End, F9 or A` };
    if (key.unity === undefined) return { error: `${quote(token)} has no Unity key code: expected a key a Unity game can report` };
    return { code: key.unity };
  }
  if (key === null) {
    return { error: `${quote(token)} is not a key name: expected a name such as End, F9 or A, or a code from 0x01 to 0xFE` };
  }
  if (key.vk === undefined) {
    return { error: `${quote(token)} has no Windows key code: expected a keyboard key name, or a code from 0x01 to 0xFE` };
  }
  return { code: parseInt(key.vk, 16) };
}

// { bindings: [{ modifiers, code }] } or { error }. `modifiers` is a bit set over the
// modifiers in data/keys.json order; `code` is a Windows virtual-key code (native) or a Unity
// KeyCode value (unity).
export function parseKeyBindings(text, dialect) {
  if (!DIALECTS.includes(dialect)) throw new Error(`unknown hotkey dialect ${JSON.stringify(dialect)}`);
  const value = trimSpaceTab(text);
  const bindings = [];
  if (value === "") return { bindings };

  const items = text.split(",");
  for (let n = 0; n < items.length; n++) {
    const item = trimSpaceTab(items[n]);
    if (item === "") {
      return { error: `item ${n + 1} of ${quote(value)} is empty: expected a key such as End or Ctrl+Shift+Y between commas` };
    }
    const tokens = item.split("+");
    let modifiers = 0;
    for (let t = 0; t + 1 < tokens.length; t++) {
      const index = modifierIndex(trimSpaceTab(tokens[t]));
      if (index < 0) return { error: `${quote(item)}: expected Ctrl, Shift or Alt before each '+' and one key after the last` };
      if (modifiers & (1 << index)) {
        return { error: `${quote(item)} names ${MODIFIERS[index].name} twice: expected each modifier at most once` };
      }
      modifiers |= 1 << index;
    }
    const keyToken = trimSpaceTab(tokens[tokens.length - 1]);
    if (keyToken === "") return { error: `${quote(item)}: expected Ctrl, Shift or Alt before each '+' and one key after the last` };
    if (modifierIndex(keyToken) >= 0) return { error: `${quote(item)} has no key: expected a key after the modifiers` };
    const read = readKey(keyToken, dialect);
    if (read.error) return { error: read.error };
    if (MODIFIER_KEYS[dialect].has(read.code)) {
      return { error: `${quote(keyToken)} is a Ctrl, Shift or Alt key: expected a key such as End, F9 or A, with Ctrl, Shift or Alt before it` };
    }
    if (bindings.some((b) => b.modifiers === modifiers && b.code === read.code)) {
      return { error: `${quote(item)} is listed twice: expected each binding once` };
    }
    bindings.push({ modifiers, code: read.code });
  }
  return { bindings };
}

export function formatKeyBindings(bindings, dialect) {
  return bindings
    .map(({ modifiers, code }, n) => {
      if (MODIFIER_KEYS[dialect].has(code)) {
        throw new Error(`binding ${n + 1} binds ${keyName(code, dialect)}, a Ctrl, Shift or Alt key, which a binding names only before its key`);
      }
      const names = MODIFIERS.filter((_, i) => modifiers & (1 << i)).map((m) => `${m.name}+`);
      return names.join("") + keyName(code, dialect);
    })
    .join(", ");
}

function keyName(code, dialect) {
  const key = KEYS.find((k) => (dialect === "unity" ? k.unity === code : k.vk !== undefined && parseInt(k.vk, 16) === code));
  if (key) return key.name;
  if (dialect === "unity") throw new Error(`Unity key code ${code} has no name in data/keys.json`);
  return `0x${code.toString(16).toUpperCase()}`;
}
