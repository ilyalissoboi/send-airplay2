// SPDX-License-Identifier: Apache-2.0
#include "receiver_stream.h"
#include <algorithm>
#include <chrono>
#include <cstring>
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
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace send_airplay2::detail {
ReceiverOperation ReceiverOperation::after(std::chrono::milliseconds timeout,
                                           const std::atomic_bool* cancelled) {
    if (timeout.count() < 1 || timeout.count() > 60000) {
        throw TransportException(TransportError::invalid_argument);
    }
    return {std::chrono::steady_clock::now() + timeout, cancelled};
}
void ReceiverOperation::check() const {
    if (cancelled && cancelled->load(std::memory_order_relaxed)) {
        throw TransportException(TransportError::cancelled);
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        throw TransportException(TransportError::timeout);
    }
}
std::string ReceiverEndpoint::authority() const {
    if (address.find(':') != std::string::npos) {
        return "[" + address + (scope_id ? "%" + std::to_string(scope_id) : "") +
               "]:" + std::to_string(port);
    }
    return address + ":" + std::to_string(port);
}
namespace {
constexpr int poll_slice_ms = 20;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
struct NetworkRuntime {
    NetworkRuntime() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw TransportException(TransportError::network);
        }
    }
    ~NetworkRuntime() {
        WSACleanup();
    }
    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
    NetworkRuntime(NetworkRuntime&&) = delete;
    NetworkRuntime& operator=(NetworkRuntime&&) = delete;
};
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
using Socket = int;
constexpr Socket invalid_socket = -1;
struct NetworkRuntime {};
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
[[noreturn]] void network_failure() {
    throw TransportException(TransportError::network);
}
struct NativeAddress {
    sockaddr_storage storage{};
    int size = 0;
    int family = 0;
};
NativeAddress numeric_address(const ReceiverEndpoint& endpoint) {
    if (!endpoint.port || endpoint.address.empty() || endpoint.address.size() > 64 ||
        endpoint.address.find('\0') != std::string::npos) {
        throw TransportException(TransportError::invalid_argument);
    }
    NativeAddress output;
    sockaddr_in ipv4{};
    sockaddr_in6 ipv6{};
    if (inet_pton(AF_INET, endpoint.address.c_str(), &ipv4.sin_addr) == 1) {
        if (endpoint.scope_id) {
            throw TransportException(TransportError::invalid_argument);
        }
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(endpoint.port);
        // sockaddr_storage has native alignment; copy the complete initialized value.
        std::memcpy(&output.storage, &ipv4, sizeof(ipv4));
        output.size = sizeof(ipv4);
        output.family = AF_INET;
    } else if (inet_pton(AF_INET6, endpoint.address.c_str(), &ipv6.sin6_addr) == 1) {
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(endpoint.port);
        ipv6.sin6_scope_id = endpoint.scope_id;
        std::memcpy(&output.storage, &ipv6, sizeof(ipv6));
        output.size = sizeof(ipv6);
        output.family = AF_INET6;
    } else {
        throw TransportException(TransportError::invalid_argument);
    }
    return output;
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
/// Return readiness, without resetting the operation deadline on EINTR/progress.
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
class NativeReceiverStream final : public ReceiverStream {
    // Socket closes before the Winsock reference is released, even on construction failure.
    [[maybe_unused]] NetworkRuntime runtime_;
    SocketOwner socket_;
    void require_open() const {
        if (socket_.value == invalid_socket) {
            throw TransportException(TransportError::closed);
        }
    }
    void wait(bool writing, const ReceiverOperation& operation) {
        require_open();
        for (;;) {
            operation.check();
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       operation.deadline - std::chrono::steady_clock::now())
                                       .count();
            const auto slice = static_cast<int>(
                std::max<std::int64_t>(0, std::min<std::int64_t>(poll_slice_ms, remaining)));
            if (ready(socket_.value, writing, slice)) {
                operation.check();
                return;
            }
        }
    }

public:
    NativeReceiverStream(const ReceiverEndpoint& endpoint, const ReceiverOperation& operation) {
        operation.check();
        const auto address = numeric_address(endpoint);
        socket_.value = socket(address.family, SOCK_STREAM, IPPROTO_TCP);
        if (socket_.value == invalid_socket) {
            network_failure();
        }
        nonblocking(socket_.value);
        if (::connect(socket_.value, reinterpret_cast<const sockaddr*>(&address.storage),
                      address.size) != 0) {
            if (!connecting(socket_error())) {
                network_failure();
            }
            wait(true, operation);
            int error = 0;
#ifdef _WIN32
            int length = sizeof(error);
#else
            socklen_t length = sizeof(error);
#endif
            if (getsockopt(socket_.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error),
                           &length) != 0 ||
                error) {
                network_failure();
            }
        }
        operation.check();
    }
    ~NativeReceiverStream() override {
        close();
    }
    void close() noexcept override {
        socket_.close();
    }
    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override {
        try {
            if (!data || !size) {
                throw TransportException(TransportError::invalid_argument);
            }
            const auto length = static_cast<int>(std::min(size, receiver_http::read_chunk));
            for (;;) {
                wait(true, operation);
                int flags = 0;
#if !defined(_WIN32) && !defined(__APPLE__)
                flags = MSG_NOSIGNAL;
#endif
                const auto written =
                    ::send(socket_.value, reinterpret_cast<const char*>(data), length, flags);
                if (written > 0) {
                    operation.check();
                    return static_cast<std::size_t>(written);
                }
                if (written == 0) {
                    throw TransportException(TransportError::disconnected);
                }
                if (!retryable(socket_error())) {
                    network_failure();
                }
            }
        } catch (...) {
            close();
            throw;
        }
    }
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        try {
            if (!data || !capacity) {
                throw TransportException(TransportError::invalid_argument);
            }
            const auto length = static_cast<int>(std::min(capacity, receiver_http::read_chunk));
            for (;;) {
                wait(false, operation);
                const auto received =
                    ::recv(socket_.value, reinterpret_cast<char*>(data), length, 0);
                if (received >= 0) {
                    operation.check();
                    if (!received) {
                        close();
                    }
                    return static_cast<std::size_t>(received);
                }
                if (!retryable(socket_error())) {
                    network_failure();
                }
            }
        } catch (...) {
            close();
            throw;
        }
    }
    void require_idle(const ReceiverOperation& operation) override {
        try {
            require_open();
            operation.check();
            if (ready(socket_.value, false, 0)) {
                char byte{};
                const auto received = ::recv(socket_.value, &byte, 1, MSG_PEEK);
                if (received > 0) {
                    throw TransportException(TransportError::correlation);
                }
                if (!received) {
                    throw TransportException(TransportError::disconnected);
                }
                if (!retryable(socket_error())) {
                    network_failure();
                }
            }
            operation.check();
        } catch (...) {
            close();
            throw;
        }
    }
};
} // namespace
std::unique_ptr<ReceiverStream> connect_receiver(const ReceiverEndpoint& endpoint,
                                                 const ReceiverOperation& operation) {
    return std::make_unique<NativeReceiverStream>(endpoint, operation);
}
} // namespace send_airplay2::detail
