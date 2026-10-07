// SPDX-License-Identifier: Apache-2.0
// Scripted fake AirPlay receiver for session-level tests, shared by the
// UrlPlaybackSession and CastController tests. Public synthetic identities and
// secrets only; the fake derives its keys from literal labels. It implements
// the URL and remote-control SETUP/event sequence. By default it has no MRP
// data stream, so sessions must run with UrlPlaybackOptions::enable_mrp = false;
// Behavior::mrp_fixtures enables a scripted MRP peer (see FakeMrpPeer).
#ifndef SEND_AIRPLAY2_TESTS_FAKE_RECEIVER_H
#define SEND_AIRPLAY2_TESTS_FAKE_RECEIVER_H
#include "url_playback_session.h"
#include "binary_plist.h"
#include "control_crypto.h"
#include "control_records.h"
#include "identity_crypto.h"
#include "mrp_channel.h"
#include "mrp_messages.h"
#include "pairing_tlv.h"
#include "protobuf_wire.h"
#include "receiver_http.h"
#include "receiver_stream.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace send_airplay2::detail::testing {
using namespace std::chrono_literals;

inline Bytes text(std::string_view value) {
    return Bytes(value.begin(), value.end());
}
template <std::size_t Size> Bytes bytes(const std::array<std::uint8_t, Size>& input) {
    return Bytes(input.begin(), input.end());
}
template <typename Predicate> bool eventually(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

// ---- Independent request framing for the fake receiver ----

struct ParsedRequest {
    std::string method;
    std::string target;
    std::string protocol;
    std::vector<std::pair<std::string, std::string>> headers; // Lower-case names.
    Bytes body;
    [[nodiscard]] std::string header(std::string_view name) const {
        for (const auto& field : headers) {
            if (field.first == name) {
                return field.second;
            }
        }
        return {};
    }
};
inline std::optional<ParsedRequest> take_request(Bytes& buffer) {
    const std::string view(buffer.begin(), buffer.end());
    const auto end = view.find("\r\n\r\n");
    if (end == std::string::npos) {
        return std::nullopt;
    }
    ParsedRequest request;
    std::size_t line_start = 0;
    auto line_end = view.find("\r\n");
    const auto first = view.substr(0, line_end);
    const auto first_space = first.find(' ');
    const auto second_space = first.find(' ', first_space + 1);
    request.method = first.substr(0, first_space);
    request.target = first.substr(first_space + 1, second_space - first_space - 1);
    request.protocol = first.substr(second_space + 1);
    std::size_t length = 0;
    for (line_start = line_end + 2; line_start < end; line_start = line_end + 2) {
        line_end = view.find("\r\n", line_start);
        const auto line = view.substr(line_start, line_end - line_start);
        const auto colon = line.find(':');
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const auto value = line.substr(colon + 2);
        if (name == "content-length") {
            length = std::stoul(value);
        }
        request.headers.emplace_back(name, value);
    }
    const auto body_start = end + 4;
    if (buffer.size() < body_start + length) {
        return std::nullopt;
    }
    request.body.assign(buffer.begin() + static_cast<std::ptrdiff_t>(body_start),
                        buffer.begin() + static_cast<std::ptrdiff_t>(body_start + length));
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(body_start + length));
    return request;
}
inline Bytes response(const ParsedRequest& request, unsigned status, const Bytes& body = {}) {
    auto output = text(request.protocol + " " + std::to_string(status) +
                       " Synthetic\r\nCSeq: " + request.header("cseq") +
                       "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n");
    output.insert(output.end(), body.begin(), body.end());
    return output;
}
inline ControlNonce named_nonce(const char* label) {
    ControlNonce nonce{};
    std::copy_n(label, 8, nonce.begin() + 4); // Four zero bytes, then the label.
    return nonce;
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed(const Bytes& input) {
    std::array<std::uint8_t, Size> output{};
    std::copy_n(input.begin(), Size, output.begin());
    return output;
}
inline Bytes tlv_field(const std::vector<TlvField>& fields, std::uint8_t type) {
    for (const auto& field : fields) {
        if (field.type == type) {
            return field.value;
        }
    }
    throw std::runtime_error("Fake receiver: missing TLV field");
}

// ---- Fake event connection: a blocking, cancellable pipe ----

struct EventPipe {
    std::mutex mutex;
    std::condition_variable changed;
    Bytes inbound;  // Receiver to sender (encrypted).
    Bytes outbound; // Sender to receiver (encrypted replies).
    bool ended = false;
    bool closed = false;
};

class FakeEventStream final : public ReceiverStream {
public:
    explicit FakeEventStream(std::shared_ptr<EventPipe> pipe) : pipe_(std::move(pipe)) {}
    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override {
        operation.check();
        std::lock_guard<std::mutex> lock(pipe_->mutex);
        if (pipe_->closed) {
            throw TransportException(TransportError::closed);
        }
        pipe_->outbound.insert(pipe_->outbound.end(), data, data + size);
        return size;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        std::unique_lock<std::mutex> lock(pipe_->mutex);
        for (;;) {
            if (pipe_->closed) {
                throw TransportException(TransportError::closed);
            }
            if (!pipe_->inbound.empty()) {
                const auto count = std::min(capacity, pipe_->inbound.size());
                std::copy_n(pipe_->inbound.begin(), count, data);
                pipe_->inbound.erase(pipe_->inbound.begin(),
                                     pipe_->inbound.begin() + static_cast<std::ptrdiff_t>(count));
                return count;
            }
            if (pipe_->ended) {
                return 0;
            }
            pipe_->changed.wait_for(lock, 5ms);
            operation.check(); // Cancellation reaches a blocked reader here.
        }
    }
    void require_idle(const ReceiverOperation&) override {}
    void close() noexcept override {
        std::lock_guard<std::mutex> lock(pipe_->mutex);
        pipe_->closed = true;
        pipe_->changed.notify_all();
    }

private:
    std::shared_ptr<EventPipe> pipe_;
};

// ---- Fake MRP data stream ----

/// Public synthetic fixture from tests/fixtures/mrp (generate_mrp_fixtures.py).
inline Bytes load_mrp_fixture(const std::string& root, const char* name) {
    std::ifstream file(root + "/" + name + ".bin", std::ios::binary);
    if (!file) {
        throw std::runtime_error("Missing public MRP fixture");
    }
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

/// MRP playback states on the wire (SetStateMessage.playbackState).
inline constexpr std::uint64_t mrp_state_playing = 1;
inline constexpr std::uint64_t mrp_state_paused = 2;
/// Duration the fake reports for the URL item, matching the public fixtures.
inline constexpr double fake_item_duration_seconds = 131.6;
inline constexpr double fake_item_elapsed_seconds = 17.0;

/**
 * Receiver side of one encrypted MRP data stream, modeled on mrp_tests.cpp's
 * peer. It answers the handshake, replies to every SEND_COMMAND with the
 * fixture's success result, records each command, and pushes SET_STATE for the
 * URL item once the URL session has inserted it. The player path and command
 * result are independent fixture bytes; pushed SET_STATE messages are built
 * here because ownership must name the session's random item UUID.
 * All members are guarded by `mutex`; the stream adapter takes it per call.
 */
struct FakeMrpPeer {
    FakeMrpPeer(const std::string& fixtures, const ControlKey& read_key,
                const ControlKey& write_key)
        : decrypt(std::make_unique<ControlReader>(read_key)),
          encrypt(std::make_unique<ControlWriter>(write_key)),
          player_path(load_mrp_fixture(fixtures, "path")),
          command_result(
              decode_mrp(protobuf_wire::view(load_mrp_fixture(fixtures, "result"))).payload) {}

    /// Queue one receiver message as a sync frame (the sender acknowledges it).
    void push_locked(const Bytes& message) {
        constexpr std::uint64_t receiver_sequence = 17; // Fixed per stream, like the reference.
        const auto wire =
            encrypt->encrypt(encode_mrp_frame(true, receiver_sequence, mrp_frame_payload(message)));
        incoming.insert(incoming.end(), wire.begin(), wire.end());
    }
    /// SET_STATE for one queue item on the fixture player path. `item` defaults
    /// to the URL item, which is what makes the sender treat it as owned.
    void push_state_locked(std::uint64_t state, double elapsed_seconds,
                           const std::string& item = {}) {
        namespace pb = protobuf_wire;
        Bytes metadata;
        pb::real64(metadata, 14, fake_item_duration_seconds); // duration
        pb::real64(metadata, 35, elapsed_seconds);            // elapsedTime
        Bytes content;
        pb::data(content, 1, item.empty() ? item_uuid : item); // identifier
        pb::data(content, 2, metadata);
        Bytes queue;
        pb::integer(queue, 1, 0); // location: the first content item is current
        pb::data(queue, 2, content);
        Bytes payload;
        pb::data(payload, 3, queue);       // playbackQueue
        pb::integer(payload, 6, state);    // playbackState
        pb::data(payload, 9, player_path); // playerPath
        push_locked(encode_mrp(mrp::set_state, {}, "synthetic-peer", payload));
    }

    /// Decrypt sender bytes and answer each complete request.
    void accept_locked(const std::uint8_t* data, std::size_t size) {
        namespace pb = protobuf_wire;
        const auto plain = decrypt->feed(Bytes(data, data + size));
        pending.insert(pending.end(), plain.begin(), plain.end());
        while (auto frame = take_mrp_frame(pending)) {
            if (!frame->sync) {
                continue; // The sender's acknowledgement of a pushed frame.
            }
            for (const auto& message : decode_mrp_batch(mrp_frame_protobufs(frame->payload))) {
                if (message.type == mrp::connection_state) {
                    continue; // No response is defined.
                }
                auto type = message.type;
                Bytes reply;
                if (type == mrp::updates_config || type == mrp::heartbeat) {
                    type = 0; // Generic acknowledgement, as the reference peer sends.
                }
                if (message.type == mrp::send_command) {
                    const auto fields = pb::decode(pb::view(message.payload));
                    const auto command = pb::find(fields, 1, 0)->integer;
                    commands.push_back(static_cast<std::uint32_t>(command));
                    if (command == static_cast<std::uint64_t>(PlaybackCommand::seek)) {
                        const auto options = pb::decode(pb::find(fields, 2, 2)->data);
                        seek_positions.push_back(pb::real64(*pb::find(options, 9, 1)));
                    }
                    type = mrp::command_result;
                    reply = command_result;
                }
                push_locked(encode_mrp(type, message.identifier, "synthetic-peer", reply));
                // Reflect accepted transport commands in the reported state.
                if (message.type == mrp::send_command && !item_uuid.empty()) {
                    const auto command = commands.back();
                    if (command == static_cast<std::uint32_t>(PlaybackCommand::pause)) {
                        push_state_locked(mrp_state_paused, fake_item_elapsed_seconds);
                    } else if (command == static_cast<std::uint32_t>(PlaybackCommand::play)) {
                        push_state_locked(mrp_state_playing, fake_item_elapsed_seconds);
                    }
                }
            }
        }
    }

    std::mutex mutex;
    Bytes incoming, pending;
    std::unique_ptr<ControlReader> decrypt;
    std::unique_ptr<ControlWriter> encrypt;
    Bytes player_path, command_result;
    std::string item_uuid; // Set from the URL session's insertPlayQueueItem.
    std::vector<std::uint32_t> commands;
    std::vector<double> seek_positions;
    bool closed = false;
};

/// Byte stream over a FakeMrpPeer. Writes and reads are split into small
/// chunks so the sender's framing handles partial records, as in mrp_tests.
class FakeMrpStream final : public ReceiverStream {
public:
    explicit FakeMrpStream(std::shared_ptr<FakeMrpPeer> peer) : peer_(std::move(peer)) {}
    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override {
        operation.check();
        std::lock_guard<std::mutex> lock(peer_->mutex);
        if (peer_->closed) {
            throw TransportException(TransportError::closed);
        }
        const auto count = std::min(size, std::size_t{7});
        peer_->accept_locked(data, count);
        return count;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        for (;;) {
            operation.check();
            {
                std::lock_guard<std::mutex> lock(peer_->mutex);
                if (peer_->closed) {
                    throw TransportException(TransportError::closed);
                }
                if (!peer_->incoming.empty()) {
                    const auto count =
                        std::min({capacity, peer_->incoming.size(), std::size_t{13}});
                    std::copy_n(peer_->incoming.begin(), count, data);
                    peer_->incoming.erase(peer_->incoming.begin(),
                                          peer_->incoming.begin() +
                                              static_cast<std::ptrdiff_t>(count));
                    return count;
                }
            }
            std::this_thread::sleep_for(1ms);
        }
    }
    void require_idle(const ReceiverOperation&) override {}
    bool wait_readable(const ReceiverOperation& operation) override {
        try {
            for (;;) {
                operation.check();
                {
                    std::lock_guard<std::mutex> lock(peer_->mutex);
                    if (!peer_->incoming.empty() || peer_->closed) {
                        return true;
                    }
                }
                std::this_thread::sleep_for(1ms);
            }
        } catch (const TransportException& error) {
            if (error.reason() == TransportError::timeout) {
                return false;
            }
            throw;
        }
    }
    void close() noexcept override {
        std::lock_guard<std::mutex> lock(peer_->mutex);
        peer_->closed = true;
    }

private:
    std::shared_ptr<FakeMrpPeer> peer_;
};

// ---- Fake receiver ----

struct Behavior {
    unsigned base_setup_status = 200;
    int reject_command = -1;    // Index 0..3 of the /command to answer with 400.
    bool report_playing = true; // After setRate: "Loading", then "Playing".
    bool end_events_on_rate = false;
    bool remote_control_only = false;
    unsigned remote_setup_status = 200;
    bool reject_remote_event_connect = false;
    bool end_remote_events_on_rate = false;
    bool silent_feedback = false;
    unsigned zero_rate_events = 0;
    bool pause_after_zero_rate = false;
    bool remain_stationary = false;
    /// Directory of the public MRP fixtures. Non-empty enables the remote
    /// session's MRP data stream (FakeMrpPeer); empty keeps the minimum sequence.
    std::string mrp_fixtures;
};

inline constexpr std::uint16_t control_port = 7000;
inline constexpr std::uint16_t event_port = 49213;
inline constexpr std::uint16_t remote_event_port = 49214;
inline constexpr std::int64_t stream_id = 1;
inline constexpr std::uint16_t data_port = 49215; // MRP data stream, remote session only.

struct ControlCloseOrder {
    std::mutex mutex;
    std::vector<std::string> sessions;
};

class FakeReceiver;

class FakeControlStream final : public ReceiverStream {
public:
    explicit FakeControlStream(FakeReceiver& receiver) : receiver_(receiver) {}
    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override;
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override;
    void require_idle(const ReceiverOperation&) override {}
    void close() noexcept override;

private:
    FakeReceiver& receiver_;
};

/// Accessory side of pair-verify, then the URL playback request sequence.
class FakeReceiver {
public:
    explicit FakeReceiver(Behavior behavior) : behavior_(behavior) {
        for (std::size_t index = 0; index < 32; ++index) {
            receiver_seed_.bytes[index] = static_cast<std::uint8_t>(index + 32);
            server_ephemeral_.bytes[index] = static_cast<std::uint8_t>(index + 96);
            client_seed_.bytes[index] = static_cast<std::uint8_t>(index);
        }
        server_public_ = x25519_public(server_ephemeral_);
    }
    PairCredentials credentials() const {
        return PairCredentials(text("synthetic-receiver"), ed25519_public(receiver_seed_),
                               text("synthetic-controller"), client_seed_);
    }
    StreamConnector connector() {
        Behavior remote_behavior;
        remote_behavior.remote_control_only = true;
        remote_behavior.base_setup_status = behavior_.remote_setup_status;
        remote_behavior.mrp_fixtures = behavior_.mrp_fixtures;
        remote_receiver_ = std::make_unique<FakeReceiver>(remote_behavior);
        remote_receiver_->close_order_ = close_order_;
        // Different ephemeral secrets make accidental key sharing fail authentication.
        remote_receiver_->server_ephemeral_.bytes[0] ^= 0x40;
        remote_receiver_->server_public_ = x25519_public(remote_receiver_->server_ephemeral_);
        auto url_connector = direct_connector();
        auto remote_connector = remote_receiver_->direct_connector();
        return [this, url_connector, remote_connector, remote_connected = false](
                   const ReceiverEndpoint& endpoint,
                   const ReceiverOperation& operation) mutable -> std::unique_ptr<ReceiverStream> {
            if (endpoint.port == control_port && !remote_connected) {
                remote_connected = true;
                return remote_connector(endpoint, operation);
            }
            if (endpoint.port == data_port) {
                return remote_connector(endpoint, operation);
            }
            if (endpoint.port == remote_event_port) {
                if (behavior_.reject_remote_event_connect) {
                    throw TransportException(TransportError::network);
                }
                return remote_connector(endpoint, operation);
            }
            return url_connector(endpoint, operation);
        };
    }
    FakeReceiver& remote_control() {
        return *remote_receiver_;
    }
    std::vector<std::string> control_close_order() const {
        std::lock_guard<std::mutex> lock(close_order_->mutex);
        return close_order_->sessions;
    }

private:
    StreamConnector direct_connector() {
        return [this](const ReceiverEndpoint& endpoint,
                      const ReceiverOperation& operation) -> std::unique_ptr<ReceiverStream> {
            operation.check();
            std::lock_guard<std::mutex> lock(mutex_);
            if (endpoint.port == control_port) {
                return std::make_unique<FakeControlStream>(*this);
            }
            const auto session_event_port =
                behavior_.remote_control_only ? remote_event_port : event_port;
            if (endpoint.port == session_event_port && verified_) {
                // The receiver writes with the key the sender reads, and so on.
                const auto secret = bytes(shared_.bytes);
                event_writer_ = std::make_unique<ControlWriter>(
                    derive_control_key(secret, "Events-Salt", "Events-Write-Encryption-Key"));
                event_reader_ = std::make_unique<ControlReader>(
                    derive_control_key(secret, "Events-Salt", "Events-Read-Encryption-Key"));
                event_pipe_ = std::make_shared<EventPipe>();
                return std::make_unique<FakeEventStream>(event_pipe_);
            }
            if (endpoint.port == data_port && behavior_.remote_control_only &&
                !behavior_.mrp_fixtures.empty() && verified_ && data_seed_) {
                // The receiver reads what the sender writes ("Output") and
                // writes what the sender reads ("Input"), salted by the seed.
                const auto secret = bytes(shared_.bytes);
                const auto salt = "DataStream-Salt" + std::to_string(*data_seed_);
                mrp_peer_ = std::make_shared<FakeMrpPeer>(
                    behavior_.mrp_fixtures,
                    derive_control_key(secret, salt, "DataStream-Output-Encryption-Key"),
                    derive_control_key(secret, salt, "DataStream-Input-Encryption-Key"));
                return std::make_unique<FakeMrpStream>(mrp_peer_);
            }
            throw TransportException(TransportError::network);
        };
    }

public:
    // Called by FakeControlStream under the session's control mutex.
    void on_control_bytes(const std::uint8_t* data, std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        Bytes wire(data, data + size);
        auto plain = control_reader_ ? control_reader_->feed(wire) : wire;
        control_input_.insert(control_input_.end(), plain.begin(), plain.end());
        while (auto request = take_request(control_input_)) {
            const bool encrypted = control_writer_ != nullptr;
            auto reply = respond(*request);
            if (reply.empty()) {
                continue; // Scripted silent peer; the reader must honor its deadline.
            }
            if (encrypted) {
                reply = control_writer_->encrypt(reply);
            }
            control_output_.insert(control_output_.end(), reply.begin(), reply.end());
        }
    }
    std::size_t take_control_output(std::uint8_t* data, std::size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (control_output_.empty()) {
            return 0; // No queued bytes, rather than transport EOF.
        }
        const auto count = std::min(capacity, control_output_.size());
        std::copy_n(control_output_.begin(), count, data);
        control_output_.erase(control_output_.begin(),
                              control_output_.begin() + static_cast<std::ptrdiff_t>(count));
        return count;
    }
    void control_closed() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!control_closed_) {
            std::lock_guard<std::mutex> order_lock(close_order_->mutex);
            close_order_->sessions.push_back(behavior_.remote_control_only ? "remote" : "URL");
        }
        control_closed_ = true;
    }

    /// Send a playbackState event on the event channel.
    void push_state(const std::string& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        push_state_locked(state);
    }
    /// Synthetic remote notification, including malformed bodies.
    void push_event_body(const Bytes& body, const std::string& target = "/command") {
        std::lock_guard<std::mutex> lock(mutex_);
        push_event_body_locked(body, target);
    }
    /// Arm the fault after startup; return the count of earlier answered requests.
    int silence_feedback() {
        std::lock_guard<std::mutex> lock(mutex_);
        behavior_.silent_feedback = true;
        return feedback_;
    }
    void end_event_channel() {
        std::lock_guard<std::mutex> lock(mutex_);
        end_events_locked();
    }
    /// Hold the next /feedback request unanswered until release_held_feedback(),
    /// like a slow receiver; return the count of earlier feedback requests.
    int hold_feedback() {
        std::lock_guard<std::mutex> lock(mutex_);
        hold_feedback_ = true;
        return feedback_;
    }
    [[nodiscard]] bool feedback_held() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return held_feedback_.has_value();
    }
    /// Answer the held request with 200 and stop holding. Feedback is the only
    /// request in flight on this connection while held, so record order holds.
    bool release_held_feedback() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!held_feedback_) {
            return false;
        }
        auto reply = response(*held_feedback_, 200);
        if (control_writer_) {
            reply = control_writer_->encrypt(reply);
        }
        control_output_.insert(control_output_.end(), reply.begin(), reply.end());
        held_feedback_.reset();
        hold_feedback_ = false;
        return true;
    }
    /// Remote session only: push SET_STATE for the URL item, or for `item`.
    /// Returns false when no MRP data stream or URL item exists yet.
    bool push_mrp_state(std::uint64_t state, double elapsed_seconds, const std::string& item = {}) {
        const auto peer = mrp_peer();
        if (!peer) {
            return false;
        }
        std::lock_guard<std::mutex> lock(peer->mutex);
        if (peer->item_uuid.empty()) {
            return false;
        }
        peer->push_state_locked(state, elapsed_seconds, item);
        return true;
    }
    /// Remote session only: MRP command numbers received, oldest first.
    std::vector<std::uint32_t> mrp_commands() const {
        const auto peer = mrp_peer();
        if (!peer) {
            return {};
        }
        std::lock_guard<std::mutex> lock(peer->mutex);
        return peer->commands;
    }
    /// Remote session only: absolute seek positions received, oldest first.
    std::vector<double> mrp_seek_positions() const {
        const auto peer = mrp_peer();
        if (!peer) {
            return {};
        }
        std::lock_guard<std::mutex> lock(peer->mutex);
        return peer->seek_positions;
    }

    // ---- Inspection ----
    std::shared_ptr<FakeMrpPeer> mrp_peer() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return mrp_peer_;
    }
    std::vector<ParsedRequest> requests() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }
    int feedback_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return feedback_;
    }
    std::uint16_t announced_timing_port() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return timing_port_;
    }
    bool control_was_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return control_closed_;
    }
    bool event_channel_opened() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return event_pipe_ != nullptr;
    }
    bool event_was_closed() const {
        std::shared_ptr<EventPipe> pipe;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pipe = event_pipe_;
        }
        if (!pipe) {
            return false;
        }
        std::lock_guard<std::mutex> lock(pipe->mutex);
        return pipe->closed;
    }
    /// The sender's decrypted replies on the event channel.
    std::string event_replies() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!event_pipe_) {
            return {};
        }
        Bytes wire;
        {
            std::lock_guard<std::mutex> pipe_lock(event_pipe_->mutex);
            wire.swap(event_pipe_->outbound);
        }
        const auto plain = event_reader_->feed(wire);
        replies_ += std::string(plain.begin(), plain.end());
        return replies_;
    }

private:
    Bytes respond(const ParsedRequest& request) {
        if (!verified_) {
            return pair_verify(request);
        }
        requests_.push_back(request);
        if (request.method == "SETUP") {
            const auto body = decode_binary_plist(request.body);
            if (body.find("streams") != nullptr && behavior_.remote_control_only &&
                !behavior_.mrp_fixtures.empty()) {
                const auto& stream = body.find("streams")->as_array().front();
                data_seed_ = static_cast<std::uint64_t>(stream.find("seed")->as_integer());
                return response(
                    request, 200,
                    encode_binary_plist(PlistDictionary{
                        {"streams",
                         PlistArray{PlistDictionary{
                             {"type", 130}, {"streamID", stream_id}, {"dataPort", data_port}}}}}));
            }
            if (body.find("streams") != nullptr) {
                return response(request, 200,
                                encode_binary_plist(PlistDictionary{
                                    {"streams", PlistArray{PlistDictionary{
                                                    {"type", 130}, {"streamID", stream_id}}}}}));
            }
            if (const auto* timing_port = body.find("timingPort")) {
                timing_port_ = static_cast<std::uint16_t>(timing_port->as_integer());
            }
            if (behavior_.base_setup_status != 200) {
                return response(request, behavior_.base_setup_status);
            }
            return response(request, 200,
                            encode_binary_plist(PlistDictionary{
                                {"eventPort",
                                 behavior_.remote_control_only ? remote_event_port : event_port}}));
        }
        if (request.method == "GET" && request.target == "/info") {
            return response(request, 200, encode_binary_plist(PlistDictionary{{"name", "fake"}}));
        }
        if (request.method == "RECORD") {
            return response(request, 200);
        }
        if (request.method == "POST" && request.target == "/feedback") {
            ++feedback_;
            if (hold_feedback_) {
                held_feedback_ = request; // Answered later by release_held_feedback().
                return {};
            }
            if (behavior_.silent_feedback) {
                return {};
            }
            return response(request, 200);
        }
        if (request.method == "POST" && request.target == "/command") {
            const auto index = commands_++;
            if (index == behavior_.reject_command) {
                return response(request, 400);
            }
            const auto envelope = decode_binary_plist(request.body);
            const auto command =
                decode_binary_plist(envelope.find("params")->find("data")->as_data());
            if (command.find("type")->as_string() == "insertPlayQueueItem" && remote_receiver_) {
                remote_receiver_->set_mrp_item(command.find("item")->find("uuid")->as_string());
            }
            if (command.find("type")->as_string() == "setRate") {
                if (remote_receiver_) {
                    // The receiver reports our item playing on MRP as it starts.
                    (void)remote_receiver_->push_mrp_state(mrp_state_playing,
                                                           fake_item_elapsed_seconds);
                }
                if (behavior_.end_remote_events_on_rate) {
                    remote_receiver_->end_event_channel();
                }
                if (behavior_.end_events_on_rate) {
                    end_events_locked();
                } else if (behavior_.report_playing) {
                    push_state_locked("Loading");
                    for (unsigned event = 0; event < behavior_.zero_rate_events; ++event) {
                        push_state_locked("Playing", 0.0);
                    }
                    if (behavior_.pause_after_zero_rate) {
                        push_state_locked("Paused");
                    } else if (!behavior_.remain_stationary) {
                        push_state_locked("Playing", behavior_.zero_rate_events
                                                         ? std::optional<double>{1.0}
                                                         : std::nullopt);
                    }
                } else {
                    push_state_locked("Loading");
                }
            }
            return response(request, 200);
        }
        return response(request, 404);
    }

    Bytes pair_verify(const ParsedRequest& request) {
        const auto fields = decode_tlv(request.body);
        if (tlv_field(fields, 6) == Bytes{1}) {
            client_public_ = fixed<32>(tlv_field(fields, 3));
            x25519_shared(server_ephemeral_, client_public_, shared_);
            session_key_.bytes = derive_control_key(
                bytes(shared_.bytes), "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info");
            const auto identity = text("synthetic-receiver");
            auto transcript = bytes(server_public_);
            transcript.insert(transcript.end(), identity.begin(), identity.end());
            transcript.insert(transcript.end(), client_public_.begin(), client_public_.end());
            const auto sealed = seal_record(
                session_key_.bytes, named_nonce("PV-Msg02"), {},
                encode_tlv({{1, identity}, {10, bytes(ed25519_sign(receiver_seed_, transcript))}}));
            return response(request, 200,
                            encode_tlv({{6, {2}}, {3, bytes(server_public_)}, {5, sealed}}));
        }
        // M3: acknowledge in plaintext; later bytes use the control keys.
        const auto secret = bytes(shared_.bytes);
        control_reader_ = std::make_unique<ControlReader>(
            derive_control_key(secret, "Control-Salt", "Control-Write-Encryption-Key"));
        control_writer_ = std::make_unique<ControlWriter>(
            derive_control_key(secret, "Control-Salt", "Control-Read-Encryption-Key"));
        verified_ = true;
        auto reply = response(request, 200, encode_tlv({{6, {4}}}));
        return reply;
    }

    void set_mrp_item(const std::string& uuid) {
        const auto peer = mrp_peer();
        if (peer) {
            std::lock_guard<std::mutex> lock(peer->mutex);
            peer->item_uuid = uuid;
        }
    }

    void push_state_locked(const std::string& state, std::optional<double> rate = {}) {
        if (!event_pipe_) {
            return;
        }
        PlistDictionary params{{"playbackState", state}};
        if (rate) {
            params.push_back({"rate", *rate});
        }
        const auto inner = encode_binary_plist(
            PlistDictionary{{"type", "playbackState"}, {"params", std::move(params)}});
        const auto body =
            encode_binary_plist(PlistDictionary{{"params", PlistDictionary{{"data", inner}}}});
        push_event_body_locked(body);
    }
    void push_event_body_locked(const Bytes& body, const std::string& target = "/command") {
        auto event =
            text("POST " + target + " RTSP/1.0\r\nCSeq: " + std::to_string(++event_sequence_) +
                 "\r\nContent-Type: application/x-apple-binary-plist\r\n"
                 "Content-Length: " +
                 std::to_string(body.size()) + "\r\n\r\n");
        event.insert(event.end(), body.begin(), body.end());
        const auto wire = event_writer_->encrypt(event);
        std::lock_guard<std::mutex> lock(event_pipe_->mutex);
        event_pipe_->inbound.insert(event_pipe_->inbound.end(), wire.begin(), wire.end());
        event_pipe_->changed.notify_all();
    }
    void end_events_locked() {
        if (!event_pipe_) {
            return;
        }
        std::lock_guard<std::mutex> lock(event_pipe_->mutex);
        event_pipe_->ended = true;
        event_pipe_->changed.notify_all();
    }

    Behavior behavior_;
    std::unique_ptr<FakeReceiver> remote_receiver_;
    std::shared_ptr<ControlCloseOrder> close_order_ = std::make_shared<ControlCloseOrder>();
    mutable std::mutex mutex_;
    Secret32 receiver_seed_;
    Secret32 server_ephemeral_;
    Secret32 client_seed_;
    Secret32 shared_;
    Secret32 session_key_;
    PublicKey server_public_{};
    PublicKey client_public_{};
    bool verified_ = false;
    std::unique_ptr<ControlReader> control_reader_;
    std::unique_ptr<ControlWriter> control_writer_;
    Bytes control_input_;
    Bytes control_output_;
    bool control_closed_ = false;
    std::vector<ParsedRequest> requests_;
    int feedback_ = 0;
    bool hold_feedback_ = false;
    std::optional<ParsedRequest> held_feedback_;
    int commands_ = 0;
    std::uint16_t timing_port_ = 0;
    std::shared_ptr<EventPipe> event_pipe_;
    std::optional<std::uint64_t> data_seed_;
    std::shared_ptr<FakeMrpPeer> mrp_peer_;
    std::unique_ptr<ControlWriter> event_writer_;
    std::unique_ptr<ControlReader> event_reader_;
    unsigned event_sequence_ = 0;
    std::string replies_;
};

inline std::size_t FakeControlStream::write_some(const std::uint8_t* data, std::size_t size,
                                                 const ReceiverOperation& operation) {
    operation.check();
    receiver_.on_control_bytes(data, size);
    return size;
}
inline std::size_t FakeControlStream::read_some(std::uint8_t* data, std::size_t capacity,
                                                const ReceiverOperation& operation) {
    for (;;) {
        operation.check();
        if (const auto count = receiver_.take_control_output(data, capacity)) {
            return count;
        }
        std::this_thread::sleep_for(1ms);
    }
}
inline void FakeControlStream::close() noexcept {
    receiver_.control_closed();
}
} // namespace send_airplay2::detail::testing
#endif
