// SPDX-License-Identifier: Apache-2.0
#include "native_socket.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace send_airplay2::detail::native {
#ifdef _WIN32
NetworkRuntime::NetworkRuntime() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw TransportException(TransportError::network);
    }
}
NetworkRuntime::~NetworkRuntime() {
    WSACleanup();
}
int socket_error() {
    return WSAGetLastError();
}
bool retryable(int error) {
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}
bool connecting(int error) {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}
void close_socket(Socket socket) {
    closesocket(socket);
}
#else
int socket_error() {
    return errno;
}
bool retryable(int error) {
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}
bool connecting(int error) {
    return error == EINPROGRESS || error == EINTR;
}
void close_socket(Socket socket) {
    ::close(socket);
}
#endif

void network_failure() {
    throw TransportException(TransportError::network);
}

NativeAddress numeric_socket_address(std::string_view address, std::uint16_t port,
                                     std::uint32_t scope_id) {
    if (address.empty() || address.size() > 64 || address.find('\0') != std::string_view::npos) {
        throw TransportException(TransportError::invalid_argument);
    }
    const std::string text(address);
    NativeAddress output;
    sockaddr_in ipv4{};
    sockaddr_in6 ipv6{};
    if (inet_pton(AF_INET, text.c_str(), &ipv4.sin_addr) == 1) {
        if (scope_id) {
            throw TransportException(TransportError::invalid_argument);
        }
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(port);
        // sockaddr_storage has native alignment; copy the complete initialized value.
        std::memcpy(&output.storage, &ipv4, sizeof(ipv4));
        output.size = sizeof(ipv4);
        output.family = AF_INET;
    } else if (inet_pton(AF_INET6, text.c_str(), &ipv6.sin6_addr) == 1) {
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(port);
        ipv6.sin6_scope_id = scope_id;
        std::memcpy(&output.storage, &ipv6, sizeof(ipv6));
        output.size = sizeof(ipv6);
        output.family = AF_INET6;
    } else {
        throw TransportException(TransportError::invalid_argument);
    }
    return output;
}

NativeAddress numeric_address(const ReceiverEndpoint& endpoint) {
    if (!endpoint.port) {
        throw TransportException(TransportError::invalid_argument);
    }
    return numeric_socket_address(endpoint.address, endpoint.port, endpoint.scope_id);
}

bool same_host(const NativeAddress& left, const NativeAddress& right) {
    if (left.family != right.family) {
        return false;
    }
    if (left.family == AF_INET) {
        sockaddr_in first{};
        sockaddr_in second{};
        std::memcpy(&first, &left.storage, sizeof(first));
        std::memcpy(&second, &right.storage, sizeof(second));
        return std::memcmp(&first.sin_addr, &second.sin_addr, sizeof(first.sin_addr)) == 0;
    }
    if (left.family == AF_INET6) {
        sockaddr_in6 first{};
        sockaddr_in6 second{};
        std::memcpy(&first, &left.storage, sizeof(first));
        std::memcpy(&second, &right.storage, sizeof(second));
        return std::memcmp(&first.sin6_addr, &second.sin6_addr, sizeof(first.sin6_addr)) == 0 &&
               first.sin6_scope_id == second.sin6_scope_id;
    }
    return false;
}

std::string route_local_address(const ReceiverEndpoint& receiver) {
    const auto remote = numeric_address(receiver);
    [[maybe_unused]] NetworkRuntime runtime; // Outlives the socket below.
    SocketOwner socket;
    socket.value = ::socket(remote.family, SOCK_DGRAM, IPPROTO_UDP);
    if (socket.value == invalid_socket) {
        network_failure();
    }
    // A UDP connect only selects the route; no datagram is sent.
    if (::connect(socket.value, reinterpret_cast<const sockaddr*>(&remote.storage), remote.size) !=
        0) {
        network_failure();
    }
    sockaddr_storage local{};
    SocketLength local_size = sizeof(local);
    if (::getsockname(socket.value, reinterpret_cast<sockaddr*>(&local), &local_size) != 0) {
        network_failure();
    }
    char text[INET6_ADDRSTRLEN] = {};
    const void* address = nullptr;
    if (local.ss_family == AF_INET) {
        address = &reinterpret_cast<const sockaddr_in*>(&local)->sin_addr;
    } else if (local.ss_family == AF_INET6) {
        address = &reinterpret_cast<const sockaddr_in6*>(&local)->sin6_addr;
    } else {
        network_failure();
    }
    if (inet_ntop(local.ss_family, address, text, sizeof(text)) == nullptr) {
        network_failure();
    }
    return text;
}

void nonblocking(Socket socket) {
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled)) {
        network_failure();
    }
#else
    const auto flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        network_failure();
    }
    const auto descriptor_flags = fcntl(socket, F_GETFD, 0);
    if (descriptor_flags < 0 || fcntl(socket, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        network_failure();
    }
#ifdef __APPLE__
    const int enabled = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled))) {
        network_failure();
    }
#endif
#endif
}

bool ready(Socket socket, bool writing, int timeout_ms) {
#ifdef _WIN32
    fd_set events;
    FD_ZERO(&events);
    FD_SET(socket, &events);
    fd_set errors = events;
    timeval timeout{0, timeout_ms * 1000};
    const auto result =
        select(0, writing ? nullptr : &events, writing ? &events : nullptr, &errors, &timeout);
#else
    pollfd descriptor{socket, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    const auto result = poll(&descriptor, 1, timeout_ms);
#endif
    if (result < 0) {
        if (retryable(socket_error())) {
            return false;
        }
        network_failure();
    }
    return result > 0;
}

int poll_slice(const ReceiverOperation& operation) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               operation.deadline - std::chrono::steady_clock::now())
                               .count();
    return static_cast<int>(
        std::max<std::int64_t>(0, std::min<std::int64_t>(poll_slice_ms, remaining)));
}
} // namespace send_airplay2::detail::native
