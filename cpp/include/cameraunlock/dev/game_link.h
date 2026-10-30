#pragma once

// A TCP link between a harness and a dev build of a mod, for a harness that
// plays the game as it goes: commands in, one a line, played as they arrive,
// and messages out, one JSON object a line. isolated_input.h starts it when the
// command file's folder holds CameraUnlockLink.txt with a port number in it.
//
// It listens on 127.0.0.1 only. A container on Docker Desktop reaches it
// through host.docker.internal.
//
// In:
//   a line of the isolated-input command language (input_script.h), played at
//     once. Not `wait`: the client times its own commands.
//   mod <text>    queued for the mod, which takes it with TakeModCommand on a
//                 thread of its own (a game's own input or scripting, which only
//                 the mod can reach)
// Out:
//   {"type":"hello","protocol":1,"pid":<the game's process id>}   on connecting
//   {"type":"error","line":"<the line>","message":"<why>"}        a line not played
//   whatever the mod publishes with PublishToGameLink
//
// One client at a time; the next is taken once the last has gone. Never in a
// release build.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace cameraunlock::dev {

inline constexpr int kGameLinkProtocol = 1;
inline constexpr std::size_t kGameLinkMaxLine = 4096;
inline constexpr std::size_t kGameLinkMaxQueued = 4096;
inline constexpr const wchar_t* kGameLinkPortFile = L"CameraUnlockLink.txt";

// A JSON string literal holding `text`, quotes included.
inline std::string JsonQuote(std::string_view text) {
    static const char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0xF];
                    out += kHex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

inline std::string GameLinkHello(unsigned long processId) {
    return "{\"type\":\"hello\",\"protocol\":" + std::to_string(kGameLinkProtocol) +
           ",\"pid\":" + std::to_string(processId) + "}";
}

inline std::string GameLinkError(std::string_view line, std::string_view message) {
    return "{\"type\":\"error\",\"line\":" + JsonQuote(line) + ",\"message\":" + JsonQuote(message) + "}";
}

// True for `mod <text>`, with `text` in `rest`.
inline bool IsModLine(std::string_view line, std::string_view& rest) {
    if (line.size() < 4 || line.substr(0, 4) != "mod ") return false;
    rest = line.substr(4);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    return true;
}

// What the mod may publish: one JSON object on one line. Only the shape is
// checked, not the JSON inside it.
inline bool IsPublishable(std::string_view json) {
    return json.size() >= 2 && json.front() == '{' && json.back() == '}' &&
           json.find_first_of("\r\n") == std::string_view::npos;
}

inline bool ParseGameLinkPort(std::string_view text, int& port, std::string& error) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    long value = 0;
    if (text.empty() || text.size() > 5) {
        error = "expected a port number from 1 to 65535";
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            error = "expected a port number from 1 to 65535";
            return false;
        }
        value = value * 10 + (c - '0');
    }
    if (value < 1 || value > 65535) {
        error = "expected a port number from 1 to 65535";
        return false;
    }
    port = static_cast<int>(value);
    return true;
}

// Cuts a byte stream into lines. A line longer than kGameLinkMaxLine stops the
// reader: the stream after it cannot be trusted to start on a line.
class LinkLineReader {
public:
    void Feed(const char* data, std::size_t size) {
        if (overflowed_) return;
        pending_.append(data, size);
        if (pending_.find('\n') == std::string::npos && pending_.size() > kGameLinkMaxLine) overflowed_ = true;
    }

    bool Next(std::string& line) {
        const std::size_t end = pending_.find('\n');
        if (end == std::string::npos) return false;
        if (end > kGameLinkMaxLine) {
            overflowed_ = true;
            pending_.clear();
            return false;
        }
        line.assign(pending_, 0, end);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pending_.erase(0, end + 1);
        return true;
    }

    bool Overflowed() const { return overflowed_; }

private:
    std::string pending_;
    bool overflowed_ = false;
};

// Plays one command-language line, or says why not.
using GameLinkPlayFn = std::function<bool(const std::string& line, std::string& error)>;
using GameLinkLogFn = void (*)(const char*);

// Listens on 127.0.0.1:port (0 takes a free one) and returns the port, or 0
// when it could not, with the reason logged. One link a process.
int StartGameLink(int port, GameLinkPlayFn play, GameLinkLogFn log);

// Sends one JSON object to the connected client, from any thread. False when
// nobody is connected, when the link is not running, or when `json` is not one
// line holding an object (logged).
bool PublishToGameLink(std::string_view json);

// The oldest `mod` command not yet taken, from any thread. False when none waits.
bool TakeModCommand(std::string& line);

// Closes the link and waits for its thread.
void StopGameLink();

}  // namespace cameraunlock::dev
