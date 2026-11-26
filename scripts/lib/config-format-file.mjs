import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

// The file the config tools read as data/config-format.json. CAMERAUNLOCK_CONFIG_FORMAT names
// another one in its place, so a test can hold the tools to an entry shape the fleet's own data
// may stop having, such as a converted repo whose committed file is not recorded yet.
// check-config-format.mjs validates data/config-format.json itself and never reads this.
export const CONFIG_FORMAT_FILE =
  process.env.CAMERAUNLOCK_CONFIG_FORMAT ??
  path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..", "data", "config-format.json");

export const readConfigFormat = () => JSON.parse(fs.readFileSync(CONFIG_FORMAT_FILE, "utf8"));
