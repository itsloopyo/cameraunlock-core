#pragma once

#include <string>

#include "cameraunlock/config/ini_reader.h"

// Boundary validation for the numbers a user hands a mod in HeadTracking.ini.
// This is the one place raw text becomes a float that reaches the camera
// transform, so every hazard is caught here rather than anywhere downstream.
//
// Four of them, all reachable from a single typo:
//
//   - strtod accepts "nan" and "inf", and overflows a literal like 1e400 to
//     +inf. Nothing downstream catches it: every comparison against NaN is
//     false, so the range clamps are skipped, and sin/cos of an infinite angle
//     is NaN. The result is a NaN view matrix written every frame with nothing
//     in the log.
//   - strtod parses a PREFIX. "LocalSmoothing=0,15" - a European decimal comma,
//     and IniReader pins the C locale, so this is the expected user error -
//     yields 0.0, which is inside the valid range and passes every check
//     silently. Requiring the whole token to parse is what catches it.
//   - GetPrivateProfileStringA does NOT strip inline comments, so
//     "LocalSmoothing=0.15 ; settle" reaches the parser with the comment
//     attached. Numeric readers survive that by parsing a prefix, which is
//     exactly the behaviour the point above has to remove - so the comment has
//     to come off first instead.
//   - A negative position limit inverts the clamp bounds in PositionProcessor
//     (Clamp(v, -limit, limit) with limit < 0 returns the lower bound for every
//     input), pinning the lean at a fixed offset instead of freeing it.
//
// Every function takes the mod's own printf-style log sink so the diagnostic
// carries the mod's prefix. A null sink means no diagnostic; the value is still
// corrected.
namespace cameraunlock::config {

/// Printf-style sink, matching what every mod's logging macro already is.
using LogSink = void (*)(const char* fmt, ...);

/// TrackingProcessor multiplies a quaternion decomposition (never more than 180
/// degrees) by this, so the bound keeps the product finite with room to spare
/// while sitting orders of magnitude past any usable setting. The documented
/// tuning range is 0.1-3.0.
constexpr float kMaxSensitivity = 100.0f;

/// Travel limits are metres. A cockpit head has centimetres of travel, so this
/// is generous headroom that still catches a mistyped 10000 for 0.10.
constexpr float kMaxPositionLimit = 10.0f;

/// Smoothing must be finite and within [0,1] - the whole meaningful domain of
/// CalculateSmoothingFactor. Validation, never a floor: a configured 0.0 stays
/// 0.0. `fallback` is the shipped default of the key being read (0.0 for
/// LocalSmoothing, 0.15 for RemoteSmoothing), so a malformed RemoteSmoothing
/// lands on the remote default rather than silently handing a phone-over-WiFi
/// user the local "no smoothing at all".
float SanitizeSmoothing(const char* key, float value, float fallback, LogSink log);

/// Sensitivity: sign and magnitude are both legitimate tuning choices (boost,
/// or invert without touching the Invert flags), so the only values refused are
/// the ones that reach the camera matrix as garbage - non-finite, and a
/// magnitude past kMaxSensitivity.
float SanitizeSensitivity(const char* key, float value, float fallback, LogSink log);

/// Position limit in metres, clamped to [0, kMaxPositionLimit].
float SanitizePositionLimit(const char* key, float value, float fallback, LogSink log);

/// A virtual key code the hotkey poller can actually watch. GetAsyncKeyState
/// only defines 0x01..0xFE, so a typo like ToggleKey=0x230 registers a hotkey
/// that can never fire and the key silently does nothing.
///
/// Not the same question as input::IsValidHotkeyCode, and the two deliberately
/// disagree. This one is a DENY list - everything the OS can poll except the
/// modifiers - and answers "can this binding ever fire", which is what a config
/// reader needs. IsValidHotkeyCode is an ALLOW list of the keys the fleet's
/// binding convention actually uses (function keys, numpad, nav cluster,
/// letters, digits) and answers "is this a sensible thing to offer a user".
/// Every key the allow list admits passes here; the reverse does not hold.
///
/// The modifiers are refused for a second reason: Ctrl and Shift are what the
/// chord guard tests, so an action bound to one either never fires (a nav
/// binding is suppressed while the chord is held) or fires on every press of
/// any chord. Alt sits with them because it is the same class of key and a
/// binding on it reads as a modifier the user expects to combine, not press.
bool IsBindableVirtualKey(int vk);

/// True only when the WHOLE of `text` is a decimal number. "0,15", "1.5 scale",
/// "0x10" and anything with leading whitespace are rejected. "nan", "inf" and
/// "1e400" DO parse and are left for SanitizeFinite - that is the one point
/// where this and TryParseConfigFloat differ, and it is deliberate: the guards
/// want the non-finite value in hand so they can name it in the diagnostic.
bool ParseFloatStrict(const std::string& text, float& out);

/// The raw text of a key, truncated at the first ';' or '#' and then trimmed.
/// Empty means the key is absent, holds nothing, or is entirely a comment.
///
/// The truncation is unconditional, so this is a reader for the numeric keys the
/// guards below parse, not a general string accessor: a path value like
/// `D:\Games\Half#Life` comes back as `D:\Games\Half`. Read a value that may
/// legitimately contain ';' or '#' with IniReader::ReadString instead.
std::string ReadRawValue(const IniReader& ini, const char* section, const char* key);

/// Reads a float that must parse whole, then sanitizes it into [lo, hi] via
/// math::SanitizeFinite. An absent key yields `fallback` silently; a present
/// one that had to be corrected says so, because a silently ignored setting is
/// what sends a user looking for a mod fault.
float ReadFloatChecked(const IniReader& ini, const char* section, const char* key,
                       float fallback, float lo, float hi, LogSink log);

/// Warns, once per process, that the pre-split `[section] key` smoothing entry
/// is retired. Silent when the key is absent.
///
/// Once per process rather than once per load, because config is reloadable and
/// repeating this on every reload buries it.
///
/// The old value is deliberately NOT migrated into the new keys. The single
/// Smoothing value carried a hidden 0.15 floor, so the number in an existing
/// config does not mean what it used to: copying it across would hand a local
/// user smoothing they never chose under the new semantics, and copying it into
/// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const IniReader& reader, const char* section,
                             const char* key, LogSink log);

}  // namespace cameraunlock::config
