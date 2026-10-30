#include "cameraunlock/dev/game_link.h"

#include "cameraunlock/protocol/socket_types.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace cameraunlock::dev {
namespace {

struct Link {
    std::atomic<bool> running{false};
    std::atomic<bool> stopping{false};
    SOCKET listener = INVALID_SOCKET;
    // The client is written to from any thread, so every send and the swap of
    // the socket itself happen under this lock.
    std::mutex clientLock;
    SOCKET client = INVALID_SOCKET;
    std::mutex queueLock;
    std::deque<std::string> queue;
    GameLinkPlayFn play;
    GameLinkLogFn log = nullptr;
    std::thread thread;

    // A link still running when the process exits: Windows has ended the thread
    // already, and a joinable std::thread destroyed here would call std::terminate,
    // which ends the process with 0xC0000409 on its way out.
    ~Link() {
        if (thread.joinable()) thread.detach();
    }
};

Link& L() {
    static Link link;
    return link;
}

void Log(const std::string& text) {
    if (L().log) L().log(text.c_str());
}

bool SendLine(const std::string& text) {
    Link& link = L();
    std::lock_guard<std::mutex> lock(link.clientLock);
    if (link.client == INVALID_SOCKET) return false;
    const std::string line = text + "\n";
    std::size_t sent = 0;
    while (sent < line.size()) {
        const int n = send(link.client, line.data() + sent, static_cast<int>(line.size() - sent), 0);
        if (n == SOCKET_ERROR) {
            const int code = LastSocketError();
            // The reading loop sees the shutdown and lets the client go.
            shutdown(link.client, SD_BOTH);
            Log("game link: sending to the client failed with error " + std::to_string(code) + ", so it was let go");
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void Handle(const std::string& line) {
    std::string_view text = line;
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    if (text.empty() || text.front() == '#') return;
    std::string_view rest;
    if (IsModLine(text, rest)) {
        std::lock_guard<std::mutex> lock(L().queueLock);
        if (L().queue.size() >= kGameLinkMaxQueued) {
            SendLine(GameLinkError(line, "the mod has not taken the " + std::to_string(kGameLinkMaxQueued) +
                                             " mod commands before this one"));
            return;
        }
        L().queue.emplace_back(rest);
        return;
    }
    std::string error;
    if (!L().play(line, error)) SendLine(GameLinkError(line, error));
}

void Serve(SOCKET client) {
    Link& link = L();
    const BOOL noDelay = TRUE;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    // A client that stops reading must not hold the game thread that publishes.
    const DWORD sendTimeoutMs = 1000;
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeoutMs), sizeof(sendTimeoutMs));
    {
        std::lock_guard<std::mutex> lock(link.clientLock);
        link.client = client;
    }
    Log("game link: a client connected");
    SendLine(GameLinkHello(GetCurrentProcessId()));

    LinkLineReader reader;
    char buffer[4096];
    std::string line;
    for (;;) {
        const int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        reader.Feed(buffer, static_cast<std::size_t>(n));
        while (reader.Next(line)) Handle(line);
        if (reader.Overflowed()) {
            SendLine(GameLinkError("", "a line was longer than " + std::to_string(kGameLinkMaxLine) +
                                           " bytes, so the client was let go"));
            Log("game link: a line was too long, so the client was let go");
            // Closed with the rest of that line unread, the socket would be reset, and a
            // reset throws away the reply above before the client has read it.
            shutdown(client, SD_SEND);
            const DWORD drainMs = 2000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&drainMs), sizeof(drainMs));
            while (recv(client, buffer, sizeof(buffer), 0) > 0) {
            }
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(link.clientLock);
        closesocket(link.client);
        link.client = INVALID_SOCKET;
    }
    Log("game link: the client left");
}

void Run() {
    Link& link = L();
    while (!link.stopping.load()) {
        const SOCKET client = accept(link.listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!link.stopping.load()) {
                Log("game link: accept failed with error " + std::to_string(LastSocketError()) +
                    ", so the link has stopped");
            }
            return;
        }
        Serve(client);
    }
}

}  // namespace

int StartGameLink(int port, GameLinkPlayFn play, GameLinkLogFn log) {
    Link& link = L();
    if (link.running.load()) {
        Log("game link: asked to start a second time");
        return 0;
    }
    link.log = log;
    link.play = std::move(play);
    link.stopping = false;

    WSADATA wsa;
    const int started = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (started != 0) {
        Log("game link: WSAStartup failed with error " + std::to_string(started));
        return 0;
    }
    link.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (link.listener == INVALID_SOCKET) {
        Log("game link: socket failed with error " + std::to_string(LastSocketError()));
        WSACleanup();
        return 0;
    }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(port));
    if (bind(link.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(link.listener, 1) == SOCKET_ERROR) {
        Log("game link: listening on 127.0.0.1:" + std::to_string(port) + " failed with error " +
            std::to_string(LastSocketError()));
        closesocket(link.listener);
        link.listener = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }
    int length = sizeof(address);
    getsockname(link.listener, reinterpret_cast<sockaddr*>(&address), &length);
    const int bound = ntohs(address.sin_port);
    link.running = true;
    link.thread = std::thread(&Run);
    Log("game link: listening on 127.0.0.1:" + std::to_string(bound));
    return bound;
}

bool PublishToGameLink(std::string_view json) {
    if (!L().running.load()) return false;
    if (!IsPublishable(json)) {
        Log("game link: refused to publish what is not one line holding a JSON object");
        return false;
    }
    return SendLine(std::string(json));
}

bool TakeModCommand(std::string& line) {
    std::lock_guard<std::mutex> lock(L().queueLock);
    if (L().queue.empty()) return false;
    line = std::move(L().queue.front());
    L().queue.pop_front();
    return true;
}

void StopGameLink() {
    Link& link = L();
    if (!link.running.load()) return;
    link.stopping = true;
    closesocket(link.listener);
    link.listener = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(link.clientLock);
        if (link.client != INVALID_SOCKET) shutdown(link.client, SD_BOTH);
    }
    link.thread.join();
    {
        std::lock_guard<std::mutex> lock(link.queueLock);
        link.queue.clear();
    }
    link.running = false;
    WSACleanup();
    Log("game link: stopped");
}

}  // namespace cameraunlock::dev
