#include "cameraunlock/protocol/udp_socket.h"
#include <cstring>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

namespace cameraunlock {

UdpSocket::~UdpSocket() {
    Close();
}

bool UdpSocket::Open(uint16_t port) {
    if (m_socket != INVALID_SOCKET) {
        return true;
    }

#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        return false;
    }
    m_wsaInitialized = true;
#endif

    // Create UDP socket
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
#ifdef _WIN32
        WSACleanup();
        m_wsaInitialized = false;
#endif
        return false;
    }

    // Deliberately NO SO_REUSEADDR. It was set here once to make a fast
    // relaunch skip the retry loop, on the assumption that we are the only
    // consumer of the port. That assumption is false: OpenTrack (or a second
    // game) legitimately binds the same tracker port. With SO_REUSEADDR the
    // bind then SUCCEEDS anyway, two sockets share the port, Windows delivers
    // each datagram to only one of them, and the loser is silently deaf
    // forever - while Start() reports success, so the retry loop that exists
    // precisely to wait the other consumer out never runs. Letting bind()
    // fail is what makes the conflict visible and recoverable; the retry
    // interval is short enough that a relaunch still reclaims the port
    // promptly (UDP has no TIME_WAIT, so a closed socket frees it at once).

    // Set non-blocking
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        WSACleanup();
        m_wsaInitialized = false;
        return false;
    }

    // Disable WSAECONNRESET on UDP: by default Windows surfaces ICMP
    // port-unreachable replies (from any prior sendto, or from intermediate
    // hops on the inbound path) as a recvfrom error, which silently kills
    // throughput for receive-only sockets. SIO_UDP_CONNRESET = 0x9800000C.
    {
        DWORD bytesReturned = 0;
        BOOL  enabled = FALSE;
        DWORD ioctl = 0x9800000C;  // SIO_UDP_CONNRESET
        WSAIoctl(m_socket, ioctl, &enabled, sizeof(enabled),
                 nullptr, 0, &bytesReturned, nullptr, nullptr);
    }
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }
#endif

    // Bind to all interfaces
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        closesocket(m_socket);
        WSACleanup();
        m_wsaInitialized = false;
#else
        close(m_socket);
#endif
        m_socket = INVALID_SOCKET;
        return false;
    }

    return true;
}

void UdpSocket::Close() {
    if (m_socket != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        close(m_socket);
#endif
        m_socket = INVALID_SOCKET;
    }

#ifdef _WIN32
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }
#endif
}

}  // namespace cameraunlock
