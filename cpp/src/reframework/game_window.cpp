#include <cameraunlock/reframework/game_window.h>
#include <cameraunlock/reframework/log_callback.h>

#include <cameraunlock/os/game_window.h>

namespace cameraunlock::reframework {

namespace {

void ForwardToReLog(os::WindowLogLevel level, const char* message) {
    switch (level) {
        case os::WindowLogLevel::Warning:
            Log(LogLevel::Warning, "%s", message);
            return;
        case os::WindowLogLevel::Info:
            Log(LogLevel::Info, "%s", message);
            return;
    }
}

}  // namespace

void CenterGameWindowOnce() {
    os::CenterGameWindowOnce(&ForwardToReLog);
}

} // namespace cameraunlock::reframework
