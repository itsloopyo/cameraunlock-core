#pragma once

#include "cameraunlock/protocol/socket_types.h"
#include <cstdint>
#include <string>

namespace cameraunlock {

/// RAII wrapper for UDP socket setup/teardown.
/// Handles WSA init, socket creation, non-blocking mode, binding, and cleanup.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    // Non-copyable, non-movable
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&&) = delete;
    UdpSocket& operator=(UdpSocket&&) = delete;

    /// Creates and binds a non-blocking UDP socket on the given port.
    /// @return True if successful.
    bool Open(uint16_t port);

    /// Closes the socket and cleans up WSA if needed.
    void Close();

    /// Returns the raw socket handle.
    SOCKET GetHandle() const { return m_socket; }

    /// True if the socket is open and valid.
    bool IsOpen() const { return m_socket != INVALID_SOCKET; }

    /// What the OS said about the most recent failed Open(), in its own words:
    /// which step failed, the error code, and the system message text for it.
    /// Empty after a successful Open().
    ///
    /// Captured at the failure point. The cleanup that follows a failed bind
    /// (closesocket + WSACleanup) resets WSAGetLastError(), so a caller reading
    /// it afterwards gets 0 and has nothing to report but a guess - which is
    /// how "another app is holding the port" came to be logged for every
    /// failure, including the ones where no app holds anything (a port inside a
    /// Hyper-V/WSL reserved range refuses the bind with WSAEACCES, and the user
    /// then goes hunting an app that is not running).
    const std::string& LastError() const { return m_lastError; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    bool m_wsaInitialized = false;
    std::string m_lastError;
};

}  // namespace cameraunlock
