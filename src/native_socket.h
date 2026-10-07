// SPDX-License-Identifier: Apache-2.0
// Private native socket helpers shared by the TCP receiver stream and the UDP
// timing responder. Include only from implementation files: it pulls in the
// platform socket headers.
#ifndef SEND_AIRPLAY2_NATIVE_SOCKET_H
#define SEND_AIRPLAY2_NATIVE_SOCKET_H

#include "receiver_stream.h"
#include <cstdint>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace send_airplay2::detail::native {
/// Longest single readiness wait, so cancellation is noticed within ~20 ms.
constexpr int poll_slice_ms = 20;

#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket invalid_socket = INVALID_SOCKET;
/// One Winsock reference per socket owner; released after the socket closes.
struct NetworkRuntime {
    NetworkRuntime();
    ~NetworkRuntime();
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
    NetworkRuntime(NetworkRuntime&&) = delete;
    NetworkRuntime& operator=(NetworkRuntime&&) = delete;
};
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket invalid_socket = -1;
struct NetworkRuntime {};
#endif

int socket_error();
/// Would-block or interrupted: retry the operation.
bool retryable(int error);
/// A nonblocking connect that is still in progress.
bool connecting(int error);
void close_socket(Socket socket);

/// Closes the socket on destruction or close(); noncopyable, nonmovable.
struct SocketOwner {
    Socket value = invalid_socket;
    SocketOwner() = default;
    ~SocketOwner() {
        close();
    }
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    SocketOwner(SocketOwner&&) = delete;
    SocketOwner& operator=(SocketOwner&&) = delete;
    void close() noexcept {
        if (value != invalid_socket) {
            close_socket(value);
            value = invalid_socket;
        }
    }
};

[[noreturn]] void network_failure();

struct NativeAddress {
    sockaddr_storage storage{};
    int size = 0;
    int family = 0;
};

/// Numeric IPv4/IPv6 socket address; port 0 is allowed (bind to an ephemeral
/// port). IPv4 rejects a scope ID. Throws TransportException(invalid_argument).
[[nodiscard]] NativeAddress numeric_socket_address(std::string_view address, std::uint16_t port,
                                                   std::uint32_t scope_id);
/// As numeric_socket_address for a peer endpoint, which must have a port.
[[nodiscard]] NativeAddress numeric_address(const ReceiverEndpoint& endpoint);
/// True when both addresses are the same host (family, address bytes and, for
/// IPv6, scope). Ports are ignored.
[[nodiscard]] bool same_host(const NativeAddress& left, const NativeAddress& right);

/// The local address the OS route uses to reach `receiver`, as numeric text.
/// Connects a UDP socket without sending anything. Throws
/// TransportException(invalid_argument or network).
[[nodiscard]] std::string route_local_address(const ReceiverEndpoint& receiver);

/// Nonblocking, close-on-exec and (macOS) no SIGPIPE.
void nonblocking(Socket socket);
/// Return readiness, without resetting the operation deadline on EINTR/progress.
[[nodiscard]] bool ready(Socket socket, bool writing, int timeout_ms);
/// The next readiness wait: at most poll_slice_ms and never past the deadline.
[[nodiscard]] int poll_slice(const ReceiverOperation& operation);
} // namespace send_airplay2::detail::native
#endif
