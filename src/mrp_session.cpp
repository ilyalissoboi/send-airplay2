// SPDX-License-Identifier: Apache-2.0
#include "mrp_session.h"
#include "control_crypto.h"
#include <algorithm>
#include <utility>
namespace send_airplay2::detail {
namespace {
constexpr std::chrono::milliseconds poll_slice{20};
/// Transient plaintext/metadata is erased on exceptions as well as success.
class EraseOnExit {
public:
    explicit EraseOnExit(Bytes& bytes) : bytes_(bytes) {}
    ~EraseOnExit() {
        cleanse(bytes_.data(), bytes_.size());
    }
    EraseOnExit(const EraseOnExit&) = delete;
    EraseOnExit& operator=(const EraseOnExit&) = delete;
    EraseOnExit(EraseOnExit&&) = delete;
    EraseOnExit& operator=(EraseOnExit&&) = delete;

private:
    Bytes& bytes_;
};
const char* error_text(MrpError reason) {
    switch (reason) {
    case MrpError::malformed:
        return "Invalid MRP message";
    case MrpError::authentication:
        return "MRP record authentication failed";
    case MrpError::rejected:
        return "Receiver rejected MRP request";
    case MrpError::timeout:
        return "MRP response deadline expired";
    case MrpError::disconnected:
        return "MRP connection failed";
    case MrpError::cancelled:
        return "MRP operation cancelled";
    case MrpError::not_owned:
        return "MRP player ownership is not established";
    }
    return "MRP failure";
}
} // namespace
MrpException::MrpException(MrpError reason)
    : std::runtime_error(error_text(reason)), reason_(reason) {}
MrpSession::MrpSession(std::unique_ptr<MrpChannel> channel, std::chrono::milliseconds timeout,
                       std::chrono::milliseconds heartbeat)
    : channel_(std::move(channel)), request_timeout_(timeout), heartbeat_interval_(heartbeat) {
    if (!channel_ || timeout.count() <= 0 || heartbeat.count() <= 0) {
        throw std::invalid_argument("Invalid MRP session options");
    }
    worker_ = std::thread([this] { run(); });
}
MrpSession::~MrpSession() {
    stop();
}
void MrpSession::stop() noexcept {
    stop_ = true;
    changed_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    channel_->close();
    std::lock_guard<std::mutex> lock(mutex_);
    cleanse(outbound_.data(), outbound_.size());
    outbound_.clear();
}
void MrpSession::fail(MrpError reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failure_) {
            failure_ = reason;
        }
    }
    stop_ = true;
    changed_.notify_all();
}
void MrpSession::enqueue(std::uint32_t type, Bytes payload, const std::string& identifier) {
    const EraseOnExit erased(payload);
    outbound_ = encode_mrp(type, identifier, random_uuid(), payload);
    cleanse(payload.data(), payload.size());
}
MrpMessage MrpSession::request(std::uint32_t type, Bytes payload, std::uint32_t response_type,
                               const std::atomic_bool* cancelled) {
    const EraseOnExit erased_payload(payload);
    std::lock_guard<std::mutex> serial(request_mutex_);
    const auto deadline = std::chrono::steady_clock::now() + request_timeout_;
    std::unique_lock<std::mutex> lock(mutex_);
    if (failure_) {
        throw MrpException(*failure_);
    }
    if (stop_) {
        throw MrpException(MrpError::cancelled);
    }
    if (type == mrp::send_command) {
        const auto fields = protobuf_wire::decode(protobuf_wire::view(payload));
        const auto* path = protobuf_wire::find(fields, 3, 2);
        const auto current_path = tracker_.owned_player_path();
        if (!path || current_path.empty() || path->data != protobuf_wire::view(current_path)) {
            throw MrpException(MrpError::not_owned);
        }
    }
    waiting_identifier_ = random_uuid();
    waiting_type_ = response_type;
    response_.reset();
    enqueue(type, std::move(payload), waiting_identifier_);
    while (!response_) {
        if (failure_) {
            throw MrpException(*failure_);
        }
        if (stop_ || (cancelled && cancelled->load())) {
            lock.unlock();
            fail(MrpError::cancelled);
            throw MrpException(MrpError::cancelled);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            lock.unlock();
            fail(MrpError::timeout);
            throw MrpException(MrpError::timeout);
        }
        changed_.wait_until(lock,
                            std::min(deadline, std::chrono::steady_clock::now() + poll_slice));
    }
    auto response = std::move(*response_);
    response_.reset();
    waiting_identifier_.clear();
    if (response.error != 0) {
        throw MrpException(MrpError::rejected);
    }
    return response;
}
void MrpSession::handshake(const SenderIdentity& identity, const Bytes& pairing_id,
                           const std::atomic_bool* cancelled) {
    auto device = request(mrp::device_info, mrp_device_info(identity, pairing_id), mrp::device_info,
                          cancelled);
    const EraseOnExit erased_device(device.payload);
    if (!device.has_payload) {
        fail(MrpError::malformed);
        throw MrpException(MrpError::malformed);
    }
    {
        std::lock_guard<std::mutex> serial(request_mutex_);
        std::unique_lock<std::mutex> lock(mutex_);
        enqueue(mrp::connection_state, mrp_connection_state(), {});
        // Ensure this send precedes the subscription; the worker consumes one
        // bounded slot. No response is defined for SET_CONNECTION_STATE.
        while (!outbound_.empty()) {
            if (failure_) {
                throw MrpException(*failure_);
            }
            if (stop_ || (cancelled && cancelled->load())) {
                lock.unlock();
                fail(MrpError::cancelled);
                throw MrpException(MrpError::cancelled);
            }
            changed_.wait_for(lock, poll_slice);
        }
    }
    (void)request(mrp::updates_config, mrp_updates_config(), mrp::updates_config, cancelled);
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = true;
}
void MrpSession::expect_item(std::string uuid, std::string url) {
    std::lock_guard<std::mutex> lock(mutex_);
    tracker_.expect_item(std::move(uuid), std::move(url));
}
void MrpSession::confirm_url_playing(double duration_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    tracker_.confirm_url_playing(duration_seconds);
}
void MrpSession::command(PlaybackCommand command, double position,
                         const std::atomic_bool* cancelled) {
    Bytes path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = tracker_.owned_player_path();
    }
    if (path.empty()) {
        throw MrpException(MrpError::not_owned);
    }
    auto response = request(mrp::send_command, mrp_command(command, path, position),
                            mrp::command_result, cancelled);
    const EraseOnExit erased_response(response.payload);
    bool succeeded = false;
    try {
        succeeded = mrp_command_succeeded(response);
    } catch (const std::invalid_argument&) {
        fail(MrpError::malformed);
        throw MrpException(MrpError::malformed);
    }
    if (!succeeded) {
        throw MrpException(MrpError::rejected);
    }
}
MrpPlaybackStatus MrpSession::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = tracker_.status();
    result.messages = messages_;
    result.heartbeats = heartbeats_;
    return result;
}
bool MrpSession::failed() const {
    return failure().has_value();
}
std::optional<MrpError> MrpSession::failure() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failure_;
}
void MrpSession::run() {
    auto heartbeat_due = std::chrono::steady_clock::now() + heartbeat_interval_;
    std::string heartbeat_identifier;
    auto heartbeat_deadline = heartbeat_due;
    try {
        while (!stop_) {
            Bytes send;
            const EraseOnExit erased_send(send);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                send.swap(outbound_);
                changed_.notify_all();
                const auto now = std::chrono::steady_clock::now();
                if (!heartbeat_identifier.empty() && now >= heartbeat_deadline) {
                    throw MrpException(MrpError::timeout);
                }
                if (ready_ && send.empty() && heartbeat_identifier.empty() &&
                    now >= heartbeat_due) {
                    heartbeat_identifier = random_uuid();
                    send = encode_mrp(mrp::heartbeat, heartbeat_identifier, random_uuid());
                    heartbeat_deadline = now + request_timeout_;
                }
            }
            if (!send.empty()) {
                try {
                    channel_->send(send, ReceiverOperation::after(request_timeout_, &stop_));
                } catch (...) {
                    cleanse(send.data(), send.size());
                    throw;
                }
                cleanse(send.data(), send.size());
            }
            auto frame = channel_->receive(ReceiverOperation::after(poll_slice, &stop_),
                                           ReceiverOperation::after(request_timeout_, &stop_));
            if (!frame) {
                continue;
            }
            const EraseOnExit erased_frame(frame->payload);
            auto batch = mrp_frame_protobufs(frame->payload);
            const EraseOnExit erased_batch(batch);
            cleanse(frame->payload.data(), frame->payload.size());
            auto messages = decode_mrp_batch(batch);
            cleanse(batch.data(), batch.size());
            for (auto& message : messages) {
                const EraseOnExit erased_message(message.payload);
                std::lock_guard<std::mutex> lock(mutex_);
                ++messages_;
                if (!heartbeat_identifier.empty() && message.identifier == heartbeat_identifier) {
                    if ((message.type != mrp::heartbeat && message.type != 0) ||
                        message.error != 0) {
                        throw MrpException(MrpError::rejected);
                    }
                    ++heartbeats_;
                    heartbeat_identifier.clear();
                    heartbeat_due = std::chrono::steady_clock::now() + heartbeat_interval_;
                } else if (!waiting_identifier_.empty() &&
                           message.identifier == waiting_identifier_) {
                    // tvOS 26.6 acknowledges update configuration with type 0
                    // and the exact request identifier; commands still require
                    // SEND_COMMAND_RESULT, and DEVICE_INFO requires its payload.
                    const bool subscription_ack =
                        waiting_type_ == mrp::updates_config && message.type == 0;
                    if ((message.type != waiting_type_ && !subscription_ack) || response_) {
                        throw MrpException(MrpError::malformed);
                    }
                    response_ = std::move(message);
                    changed_.notify_all();
                } else {
                    tracker_.apply(message);
                }
                cleanse(message.payload.data(), message.payload.size());
            }
        }
    } catch (const MrpException& error) {
        if (!stop_) {
            fail(error.reason());
        }
    } catch (const std::invalid_argument&) {
        if (!stop_) {
            fail(MrpError::malformed);
        }
    } catch (const TransportException& error) {
        if (!stop_) {
            fail(error.reason() == TransportError::invalid_message ? MrpError::malformed
                                                                   : MrpError::disconnected);
        }
    } catch (const ControlException& error) {
        if (!stop_) {
            fail(error.reason() == ControlError::authentication ? MrpError::authentication
                                                                : MrpError::malformed);
        }
    } catch (...) {
        if (!stop_) {
            fail(MrpError::disconnected);
        }
    }
    channel_->close();
    changed_.notify_all();
}
} // namespace send_airplay2::detail
