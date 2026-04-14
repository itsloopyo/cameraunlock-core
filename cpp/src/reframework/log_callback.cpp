#include <cameraunlock/reframework/log_callback.h>
#include <cstdio>
#include <cstdarg>

namespace cameraunlock::reframework {

static LogCallbackFn g_logCallback = nullptr;

void SetLogCallback(LogCallbackFn fn) {
    g_logCallback = fn;
}

void Log(LogLevel level, const char* fmt, ...) {
    if (!g_logCallback) return;
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    g_logCallback(level, buffer);
}

} // namespace cameraunlock::reframework
