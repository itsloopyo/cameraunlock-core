// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <cameraunlock/config/retired_keys.h>

#include <cameraunlock/config/config_key_schema.g.h>

#include <set>
#include <string>

namespace cameraunlock::config {

void WarnRetiredConfigKey(const IniReader& reader, const char* section, const char* key,
                          LogSink log) {
    static std::set<std::string> warned;
    const char* canonical = ResolveConfigKey(key);
    if (canonical == nullptr || !IsRetiredConfigKey(canonical)) return;
    // Latched only once the warning is actually emitted. Latching first would
    // let a caller with no sink burn the one chance any later caller had of
    // seeing it.
    if (log == nullptr) return;
    if (ReadRawValue(reader, section, key).empty()) return;
    if (!warned.insert(canonical).second) return;
    log("Config key [%s] %s has been retired and is IGNORED. %s", section, key,
        RetiredConfigKeyAdvice(canonical));
}

}  // namespace cameraunlock::config
