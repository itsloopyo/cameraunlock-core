#pragma once

namespace cameraunlock::reframework {

// Log severity levels
enum class LogLevel { Info = 0, Warning = 1, Error = 2 };

// Callback signature: (level, formatted message)
using LogCallbackFn = void(*)(LogLevel level, const char* message);

// Set the logging callback. Must be called before using any reframework utilities.
// Pass nullptr to disable logging.
void SetLogCallback(LogCallbackFn fn);

// Internal: call the registered callback (no-op if null).
void Log(LogLevel level, const char* fmt, ...);

} // namespace cameraunlock::reframework
