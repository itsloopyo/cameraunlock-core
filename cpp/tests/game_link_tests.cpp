// Tests for the game link (cameraunlock/dev/game_link.h): the line protocol, and
// a real link on loopback with a client that connects, sends and reads.

#include <cameraunlock/dev/game_link.h>
#include <cameraunlock/protocol/socket_types.h>

#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

using namespace cameraunlock::dev;

void ProtocolTests() {
    Check(JsonQuote("a \"b\" \\ c\n\t\x01") == "\"a \\\"b\\\" \\\\ c\\n\\t\\u0001\"", "JsonQuote escapes quotes, backslashes and controls");
    Check(GameLinkHello(42) == "{\"type\":\"hello\",\"protocol\":1,\"pid\":42}", "hello names the protocol and the process");
    Check(GameLinkError("tap Q", "no \"Q\"") == "{\"type\":\"error\",\"line\":\"tap Q\",\"message\":\"no \\\"Q\\\"\"}",
          "error quotes its line and message");

    std::string_view rest;
    Check(IsModLine("mod pad 1 join", rest) && rest == "pad 1 join", "mod line hands on the rest");
    Check(IsModLine("mod   lua x()", rest) && rest == "lua x()", "mod line drops the spaces after mod");
    Check(!IsModLine("model", rest) && !IsModLine("tap mod", rest), "only a line starting `mod ` is the mod's");

    Check(IsPublishable("{\"a\":1}"), "an object on one line may be published");
    Check(!IsPublishable("{\"a\":\n1}") && !IsPublishable("[1]") && !IsPublishable("") && !IsPublishable("{"),
          "a line break, an array, nothing or half an object may not");

    int port = 0;
    std::string error;
    Check(ParseGameLinkPort(" 47001\r\n", port, error) && port == 47001, "port file with spaces and a line end");
    Check(!ParseGameLinkPort("0", port, error) && !ParseGameLinkPort("65536", port, error) &&
              !ParseGameLinkPort("47 001", port, error) && !ParseGameLinkPort("", port, error) && !error.empty(),
          "port out of range, split or missing is refused");

    LinkLineReader reader;
    std::string line;
    const std::string first = "tap A\r\nmod x", second = "y\n";
    reader.Feed(first.data(), first.size());
    Check(reader.Next(line) && line == "tap A", "reader gives a whole line without its CR");
    Check(!reader.Next(line), "reader keeps half a line");
    reader.Feed(second.data(), second.size());
    Check(reader.Next(line) && line == "mod xy", "reader joins a line sent in two parts");

    LinkLineReader flood;
    const std::string big(kGameLinkMaxLine + 1, 'x');
    flood.Feed(big.data(), big.size());
    Check(flood.Overflowed() && !flood.Next(line), "reader stops on a line longer than the limit");
}

struct Client {
    SOCKET socket = INVALID_SOCKET;
    std::string pending;

    bool Connect(int port) {
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<u_short>(port));
        const DWORD timeoutMs = 2000;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        return connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }

    void Send(const std::string& text) { send(socket, text.data(), static_cast<int>(text.size()), 0); }

    // The next line, or "" after two seconds with none.
    std::string Read() {
        for (;;) {
            const std::size_t end = pending.find('\n');
            if (end != std::string::npos) {
                std::string line = pending.substr(0, end);
                pending.erase(0, end + 1);
                return line;
            }
            char buffer[1024];
            const int n = recv(socket, buffer, sizeof(buffer), 0);
            if (n <= 0) return "";
            pending.append(buffer, static_cast<std::size_t>(n));
        }
    }

    void Close() { closesocket(socket); }
};

std::mutex g_playedLock;
std::vector<std::string> g_played;

bool RecordPlay(const std::string& line, std::string& error) {
    if (line == "bogus") {
        error = "no command 'bogus'";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_playedLock);
    g_played.push_back(line);
    return true;
}

bool WaitForMod(std::string& line) {
    for (int i = 0; i < 200; ++i) {
        if (TakeModCommand(line)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void LinkTests() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    const int port = StartGameLink(0, &RecordPlay, nullptr);
    Check(port > 0, "link listens on a free loopback port");
    Check(StartGameLink(0, &RecordPlay, nullptr) == 0, "a second link in the process is refused");
    Check(!PublishToGameLink("{\"type\":\"state\"}"), "nothing is published with no client");

    Client client;
    Check(client.Connect(port), "client connects");
    Check(client.Read().rfind("{\"type\":\"hello\",\"protocol\":1,\"pid\":", 0) == 0, "client is greeted with hello");

    client.Send("tap A\n# a note\n\nmod pad 1 join\nbogus\nmod lua print(1)\n");
    Check(client.Read() == GameLinkError("bogus", "no command 'bogus'"), "a line not played comes back as an error");
    std::string mod;
    Check(WaitForMod(mod) && mod == "pad 1 join", "first mod command is queued for the mod");
    Check(WaitForMod(mod) && mod == "lua print(1)", "second mod command follows it");
    Check(!TakeModCommand(mod), "nothing more is queued");
    {
        std::lock_guard<std::mutex> lock(g_playedLock);
        Check(g_played == std::vector<std::string>{"tap A"}, "only the command line is played; notes and blanks are not");
    }

    Check(PublishToGameLink("{\"type\":\"state\",\"x\":1}"), "the mod publishes to the client");
    Check(client.Read() == "{\"type\":\"state\",\"x\":1}", "client reads what was published");
    Check(!PublishToGameLink("not json"), "what is not an object is not published");

    client.Close();
    for (int i = 0; i < 200 && PublishToGameLink("{}"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Client second;
    Check(second.Connect(port), "a new client connects once the first has gone");
    Check(second.Read().rfind("{\"type\":\"hello\"", 0) == 0, "and is greeted too");

    const std::string flood(kGameLinkMaxLine + 10, 'x');
    second.Send(flood);
    second.Send("\n");
    Check(second.Read().find("longer than") != std::string::npos, "an overlong line is answered before the client is let go");
    Check(second.Read().empty(), "then the link closes it");
    second.Close();

    StopGameLink();
    Client refused;
    Check(!refused.Connect(port), "nothing listens once stopped");
    refused.Close();
    Check(StartGameLink(0, &RecordPlay, nullptr) > 0, "the link starts again after a stop");
    StopGameLink();
    WSACleanup();
}

}  // namespace

int RunGameLinkTests() {
    std::cout << "Game link:\n";
    g_failures = 0;
    ProtocolTests();
    LinkTests();
    return g_failures;
}
