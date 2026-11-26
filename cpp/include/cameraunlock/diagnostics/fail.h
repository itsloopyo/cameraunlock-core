// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace cameraunlock::diagnostics {

// Writes the reason a check failed and returns false, so a step that matches
// code reads `if (...) return fail("no call to 0x%X", target);` and its caller
// reads the reason from the string it handed over. For a search that tries
// one shape after another and reports why the last did not match, where a
// throw would end the search.
class Fail {
public:
    explicit Fail(std::string& out) : out_(out) {}

    // Formats as printf does, cut at 511 characters, replacing what the string held.
    bool operator()(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        out_ = buf;
        return false;
    }

private:
    std::string& out_;
};

}  // namespace cameraunlock::diagnostics
