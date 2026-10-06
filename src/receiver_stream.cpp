// SPDX-License-Identifier: Apache-2.0
#include "receiver_stream.h"
#include "native_socket.h"
#include <algorithm>
#include <chrono>

namespace send_airplay2::detail {
ReceiverOperation ReceiverOperation::after(std::chrono::milliseconds timeout,
                                           const std::atomic_bool* cancelled) {
    if (timeout.count() < 1 || timeout.count() > 60000) {
        throw TransportException(TransportError::invalid_argument);
    }
    return {std::chrono::steady_clock::now() + timeout, cancelled};
}
ReceiverOperation ReceiverOperation::until_cancelled(const std::atomic_bool* cancelled) {
    if (cancelled == nullptr) {
        throw TransportException(TransportError::invalid_argument);
    }
    return {std::chrono::steady_clock::time_point::max(), cancelled};
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
        // The scope identifies a sender-local interface, not part of the peer's
        // HTTP authority. It is supplied only through sockaddr_in6::sin6_scope_id.
        return "[" + address + "]:" + std::to_string(port);
    }
    return address + ":" + std::to_string(port);
}
namespace {
using namespace native;
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
            if (ready(socket_.value, writing, poll_slice(operation))) {
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
