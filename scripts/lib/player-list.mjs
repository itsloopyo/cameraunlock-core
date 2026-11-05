// The player_list codec of data/config-schema.json, as cameraunlock::config::PlayerListCodec and
// CameraUnlock.Core.Config.PlayerListCodec read it: empty or only spaces and tabs is the empty
// list; otherwise items split at ',', each trimmed of spaces and tabs, ASCII digits for a number
// from 1 to 8, none twice, kept in the order written. Returns { players } or { error }.
export function parsePlayerList(text) {
  if (/^[ \t]*$/.test(text)) return { players: [] };
  const players = [];
  for (const raw of text.split(",")) {
    const item = raw.replace(/^[ \t]+|[ \t]+$/g, "");
    if (!/^[0-9]+$/.test(item) || Number(item) < 1 || Number(item) > 8) {
      return { error: `'${item}' is not a player number from 1 to 8` };
    }
    const n = Number(item);
    if (players.includes(n)) return { error: `player ${n} is listed twice` };
    players.push(n);
  }
  return { players };
}

// The canonical text of a list parsePlayerList read: the numbers joined by ", ".
export function formatPlayerList(players) {
  return players.join(", ");
}
