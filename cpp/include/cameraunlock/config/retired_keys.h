// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/config/value_guards.h>

namespace cameraunlock::config {

/// Warns, once per process per key, that `[section] key` names a retired concept
/// and is ignored, with the advice data/config-schema.json carries for it.
/// Silent when the key is absent from the file, or when it names no retired
/// concept.
///
/// Once per process rather than once per load, because config is reloadable and
/// repeating this on every reload buries it.
///
/// A retired value is deliberately never migrated: each concept was retired
/// because what it meant changed, so carrying the number across would hand the
/// player a setting they never chose. The advice names what to set instead.
///
/// Not in value_guards.h, which declares the older, narrower
/// WarnRetiredSmoothingKey: that header and its .cpp are compiled by every mod
/// repo's frozen legacy import, and eight of them pin the pair's SHA-256 in
/// tests/config_differential/provenance.txt. Editing either file would rewrite
/// what those repos claim the published build compiled, for a function the
/// published build never called.
void WarnRetiredConfigKey(const IniReader& reader, const char* section, const char* key,
                          LogSink log);

}  // namespace cameraunlock::config
