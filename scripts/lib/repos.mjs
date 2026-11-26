import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const CORE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
export const REPOS_ROOT = path.dirname(CORE_ROOT);

export const readCoreJson = (rel) => JSON.parse(fs.readFileSync(path.join(CORE_ROOT, rel), "utf8"));

// A repo named on the command line: a path, a folder under REPOS_ROOT, or that folder without -headtracking.
export function resolveRepoRoot(token) {
  for (const candidate of [token, path.join(REPOS_ROOT, token), path.join(REPOS_ROOT, `${token}-headtracking`)]) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) return path.resolve(candidate);
  }
  throw new Error(`no repo found for '${token}', as a path or under ${REPOS_ROOT}`);
}

export function git(root, args, encoding = "utf8") {
  const result = spawnSync("git", ["-C", root, ...args], { encoding });
  if (result.error) throw result.error;
  return result;
}
