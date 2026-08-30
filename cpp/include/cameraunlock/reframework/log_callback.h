#pragma once

namespace cameraunlock::reframework {

// Log severity levels
enum class LogLevel { Info = 0, Warning = 1, Error = 2 };

// Callback signature: (level, formatted message)
using LogCallbackFn = void(*)(LogLevel level, const char* message);

// Set the logging callback. Must be called before using any reframework utilities.
// Pass nullptr to disable logging.
void SetLogCallback(LogCallbackFn fn);

// Signature of REFramework's log_info / log_warn / log_error plugin functions.
using ReframeworkLogFn = void (*)(const char* format, ...);

// Route every Log()/LogInfo()/LogWarning()/LogError() call to REFramework's own
// log functions, prefixing each line with "[tag] ". `tag` must outlive the
// process (a string literal). Installs itself through SetLogCallback, so a
// later SetLogCallback replaces it.
void InstallReframeworkLogSink(ReframeworkLogFn info, ReframeworkLogFn warn,
                               ReframeworkLogFn error, const char* tag);

// Internal: call the registered callback (no-op if null).
void Log(LogLevel level, const char* fmt, ...);

// Severity-named forms of Log(), which is what mod code reads better with.
void LogInfo(const char* fmt, ...);
void LogWarning(const char* fmt, ...);
void LogError(const char* fmt, ...);

} // namespace cameraunlock::reframework
