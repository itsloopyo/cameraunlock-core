#include <cameraunlock/reframework/log_callback.h>
#include <cstdio>
#include <cstdarg>

namespace cameraunlock::reframework {

static LogCallbackFn g_logCallback = nullptr;

static ReframeworkLogFn g_refInfo = nullptr;
static ReframeworkLogFn g_refWarn = nullptr;
static ReframeworkLogFn g_refError = nullptr;
static const char* g_refTag = "";

void SetLogCallback(LogCallbackFn fn) {
    g_logCallback = fn;
}

static void ReframeworkSink(LogLevel level, const char* message) {
    ReframeworkLogFn fn = g_refInfo;
    if (level == LogLevel::Warning) fn = g_refWarn;
    else if (level == LogLevel::Error) fn = g_refError;
    if (!fn) return;
    fn("[%s] %s", g_refTag, message);
}

void InstallReframeworkLogSink(ReframeworkLogFn info, ReframeworkLogFn warn,
                               ReframeworkLogFn error, const char* tag) {
    g_refInfo = info;
    g_refWarn = warn;
    g_refError = error;
    g_refTag = tag;
    SetLogCallback(&ReframeworkSink);
}

static void Dispatch(LogLevel level, const char* fmt, va_list args) {
    if (!g_logCallback) return;
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    g_logCallback(level, buffer);
}

void Log(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Dispatch(level, fmt, args);
    va_end(args);
}

void LogInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Dispatch(LogLevel::Info, fmt, args);
    va_end(args);
}

void LogWarning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Dispatch(LogLevel::Warning, fmt, args);
    va_end(args);
}

void LogError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Dispatch(LogLevel::Error, fmt, args);
    va_end(args);
}

} // namespace cameraunlock::reframework
