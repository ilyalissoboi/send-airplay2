// SPDX-License-Identifier: Apache-2.0
// UrlPlaybackSession against a scripted fake receiver. Public synthetic
// identities and secrets only; the fake derives its keys from literal labels.
#include "url_playback_session.h"
#include "binary_plist.h"
#include "control_crypto.h"
#include "control_records.h"
#include "identity_crypto.h"
#include "native_socket.h"
#include "ntp_timing.h"
#include "pairing_tlv.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;
using namespace std::chrono_literals;

namespace {
int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}
Bytes text(std::string_view value) {
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
std::optional<ParsedRequest> take_request(Bytes& buffer) {
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
Bytes response(const ParsedRequest& request, unsigned status, const Bytes& body = {}) {
    auto output = text(request.protocol + " " + std::to_string(status) +
                       " Synthetic\r\nCSeq: " + request.header("cseq") +
                       "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n");
    output.insert(output.end(), body.begin(), body.end());
    return output;
}
ControlNonce named_nonce(const char* label) {
    ControlNonce nonce{};
    std::copy_n(label, 8, nonce.begin() + 4); // Four zero bytes, then the label.
    return nonce;
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed(const Bytes& input) {
    std::array<std::uint8_t, Size> output{};
    std::copy_n(input.begin(), Size, output.begin());
    return output;
}
Bytes tlv_field(const std::vector<TlvField>& fields, std::uint8_t type) {
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

// ---- Fake receiver ----

struct Behavior {
    unsigned base_setup_status = 200;
    int reject_command = -1;    // Index 0..3 of the /command to answer with 400.
    bool report_playing = true; // After setRate: "Loading", then "Playing".
    bool end_events_on_rate = false;
};

constexpr std::uint16_t control_port = 7000;
constexpr std::uint16_t event_port = 49213;
constexpr std::int64_t stream_id = 1;

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
        return [this](const ReceiverEndpoint& endpoint,
                      const ReceiverOperation& operation) -> std::unique_ptr<ReceiverStream> {
            operation.check();
            std::lock_guard<std::mutex> lock(mutex_);
            if (endpoint.port == control_port) {
                return std::make_unique<FakeControlStream>(*this);
            }
            if (endpoint.port == event_port && verified_) {
                // The receiver writes with the key the sender reads, and so on.
                const auto secret = bytes(shared_.bytes);
                event_writer_ = std::make_unique<ControlWriter>(
                    derive_control_key(secret, "Events-Salt", "Events-Write-Encryption-Key"));
                event_reader_ = std::make_unique<ControlReader>(
                    derive_control_key(secret, "Events-Salt", "Events-Read-Encryption-Key"));
                event_pipe_ = std::make_shared<EventPipe>();
                return std::make_unique<FakeEventStream>(event_pipe_);
            }
            throw TransportException(TransportError::network);
        };
    }

    // Called by FakeControlStream under the session's control mutex.
    void on_control_bytes(const std::uint8_t* data, std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        Bytes wire(data, data + size);
        auto plain = control_reader_ ? control_reader_->feed(wire) : wire;
        control_input_.insert(control_input_.end(), plain.begin(), plain.end());
        while (auto request = take_request(control_input_)) {
            const bool encrypted = control_writer_ != nullptr;
            auto reply = respond(*request);
            if (encrypted) {
                reply = control_writer_->encrypt(reply);
            }
            control_output_.insert(control_output_.end(), reply.begin(), reply.end());
        }
    }
    std::size_t take_control_output(std::uint8_t* data, std::size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (control_output_.empty()) {
            throw std::runtime_error("Fake receiver: no response queued");
        }
        const auto count = std::min(capacity, control_output_.size());
        std::copy_n(control_output_.begin(), count, data);
        control_output_.erase(control_output_.begin(),
                              control_output_.begin() + static_cast<std::ptrdiff_t>(count));
        return count;
    }
    void control_closed() {
        std::lock_guard<std::mutex> lock(mutex_);
        control_closed_ = true;
    }

    /// Send a playbackState event on the event channel.
    void push_state(const std::string& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        push_state_locked(state);
    }
    void end_event_channel() {
        std::lock_guard<std::mutex> lock(mutex_);
        end_events_locked();
    }

    // ---- Inspection ----
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
            if (body.find("streams") != nullptr) {
                return response(request, 200,
                                encode_binary_plist(PlistDictionary{
                                    {"streams", PlistArray{PlistDictionary{
                                                    {"type", 130}, {"streamID", stream_id}}}}}));
            }
            timing_port_ = static_cast<std::uint16_t>(body.find("timingPort")->as_integer());
            if (behavior_.base_setup_status != 200) {
                return response(request, behavior_.base_setup_status);
            }
            return response(request, 200,
                            encode_binary_plist(PlistDictionary{{"eventPort", event_port}}));
        }
        if (request.method == "GET" && request.target == "/info") {
            return response(request, 200, encode_binary_plist(PlistDictionary{{"name", "fake"}}));
        }
        if (request.method == "RECORD") {
            return response(request, 200);
        }
        if (request.method == "POST" && request.target == "/feedback") {
            ++feedback_;
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
            if (command.find("type")->as_string() == "setRate") {
                if (behavior_.end_events_on_rate) {
                    end_events_locked();
                } else if (behavior_.report_playing) {
                    push_state_locked("Loading");
                    push_state_locked("Playing");
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

    void push_state_locked(const std::string& state) {
        if (!event_pipe_) {
            return;
        }
        const auto inner = encode_binary_plist(PlistDictionary{
            {"type", "playbackState"}, {"params", PlistDictionary{{"playbackState", state}}}});
        const auto body =
            encode_binary_plist(PlistDictionary{{"params", PlistDictionary{{"data", inner}}}});
        auto event = text("POST /command RTSP/1.0\r\nCSeq: " + std::to_string(++event_sequence_) +
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
    int commands_ = 0;
    std::uint16_t timing_port_ = 0;
    std::shared_ptr<EventPipe> event_pipe_;
    std::unique_ptr<ControlWriter> event_writer_;
    std::unique_ptr<ControlReader> event_reader_;
    unsigned event_sequence_ = 0;
    std::string replies_;
};

std::size_t FakeControlStream::write_some(const std::uint8_t* data, std::size_t size,
                                          const ReceiverOperation& operation) {
    operation.check();
    receiver_.on_control_bytes(data, size);
    return size;
}
std::size_t FakeControlStream::read_some(std::uint8_t* data, std::size_t capacity,
                                         const ReceiverOperation& operation) {
    operation.check();
    return receiver_.take_control_output(data, capacity);
}
void FakeControlStream::close() noexcept {
    receiver_.control_closed();
}

// ---- Scenarios ----

constexpr const char* media_url = "http://127.0.0.1:49153/synthetic-token/media";

UrlPlaybackOptions options_for(FakeReceiver& receiver) {
    UrlPlaybackOptions options;
    options.receiver = {"127.0.0.1", control_port, 0};
    options.media_url = media_url;
    options.request_timeout = 2000ms;
    options.start_timeout = 3000ms;
    options.feedback_interval = 30ms;
    options.connect = receiver.connector();
    return options;
}

std::vector<std::string> sequence_without_feedback(const std::vector<ParsedRequest>& requests) {
    std::vector<std::string> sequence;
    for (const auto& request : requests) {
        if (request.target == "/feedback") {
            continue;
        }
        const bool rtsp_uri = request.target.rfind("rtsp://", 0) == 0;
        sequence.push_back(request.method + " " + (rtsp_uri ? "<uri>" : request.target) + " " +
                           request.protocol);
    }
    return sequence;
}

std::string command_type(const ParsedRequest& request) {
    const auto envelope = decode_binary_plist(request.body);
    return decode_binary_plist(envelope.find("params")->find("data")->as_data())
        .find("type")
        ->as_string();
}

/// Sends one timing request from loopback and returns whether a reply came.
bool timing_replies(std::uint16_t port) {
    native::NetworkRuntime runtime;
    native::SocketOwner socket;
    const auto local = native::numeric_socket_address("127.0.0.1", 0, 0);
    socket.value = ::socket(local.family, SOCK_DGRAM, IPPROTO_UDP);
    if (socket.value == native::invalid_socket) {
        return false;
    }
    const auto target = native::numeric_socket_address("127.0.0.1", port, 0);
    std::array<std::uint8_t, ntp_timing::packet_size> request{};
    request[0] = 0x80;
    request[1] = ntp_timing::request_type;
    if (::sendto(socket.value, reinterpret_cast<const char*>(request.data()),
                 static_cast<int>(request.size()), 0,
                 reinterpret_cast<const sockaddr*>(&target.storage), target.size) < 0) {
        return false;
    }
    if (!native::ready(socket.value, false, 2000)) {
        return false;
    }
    std::array<std::uint8_t, 64> reply{};
    const auto received = ::recv(socket.value, reinterpret_cast<char*>(reply.data()),
                                 static_cast<int>(reply.size()), 0);
    return received == static_cast<int>(ntp_timing::packet_size) &&
           reply[1] == ntp_timing::response_type;
}

void happy_path_tests() {
    group = "start, state and stop";
    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    auto session = UrlPlaybackSession::start(credentials, options_for(receiver));
    check(session->status().playback_state == "playing", "start returns once playing");

    const auto requests = receiver.requests();
    const std::vector<std::string> expected{"SETUP <uri> RTSP/1.0",   "GET /info RTSP/1.0",
                                            "RECORD <uri> RTSP/1.0",  "SETUP <uri> RTSP/1.0",
                                            "POST /command HTTP/1.1", "POST /command HTTP/1.1",
                                            "POST /command HTTP/1.1", "POST /command HTTP/1.1"};
    check(sequence_without_feedback(requests) == expected, "reference request order");

    const auto& base = requests.front();
    check(base.target.rfind("rtsp://127.0.0.1/", 0) == 0, "RTSP URI names the local address");
    check(!base.header("dacp-id").empty() &&
              base.header("client-instance") == base.header("dacp-id") &&
              !base.header("active-remote").empty() &&
              base.header("user-agent") == "AirPlay/550.10" &&
              base.header("content-type") == "application/x-apple-binary-plist",
          "RTSP session headers");
    const auto base_body = decode_binary_plist(base.body);
    const auto session_uuid = base_body.find("sessionUUID")->as_string();
    check(base_body.find("timingProtocol")->as_string() == "NTP", "base SETUP asks for NTP");

    std::vector<std::string> commands;
    for (const auto& request : requests) {
        if (request.target != "/command") {
            continue;
        }
        commands.push_back(command_type(request));
        check(request.header("x-apple-streamid") == std::to_string(stream_id) &&
                  request.header("x-apple-session-id") == session_uuid &&
                  request.header("user-agent") == "AirPlay/870.14.1",
              "/command headers");
    }
    check(commands == std::vector<std::string>{"insertPlayQueueItem", "setProperty", "setProperty",
                                               "setRate"},
          "start commands in order");
    for (const auto& request : requests) {
        if (request.target == "/command" && command_type(request) == "insertPlayQueueItem") {
            const auto envelope = decode_binary_plist(request.body);
            // Keep the decoded command alive: find() points into it.
            const auto command =
                decode_binary_plist(envelope.find("params")->find("data")->as_data());
            check(command.find("item")->find("Content-Location")->as_string() == media_url,
                  "the media URL is the queued item");
        }
    }

    check(timing_replies(receiver.announced_timing_port()), "announced timing port answers");
    check(eventually([&] { return receiver.feedback_count() >= 2; }), "periodic /feedback");
    check(eventually([&] {
              const auto replies = receiver.event_replies();
              return replies.find("CSeq: 1\r\n") != std::string::npos &&
                     replies.find("CSeq: 2\r\n") != std::string::npos;
          }),
          "each event answered");

    receiver.push_state("Paused");
    const auto paused = session->wait_for_change("playing", 2000ms);
    check(paused.playback_state == "paused" && paused.events == 3, "later state changes");
    check(session->status().timing_answered >= 1, "timing counted");

    session->stop();
    check(receiver.control_was_closed() && receiver.event_was_closed(),
          "stop closes control and event connections");
    session->stop(); // Idempotent.
    check(!session->status().failed, "a clean stop is not a failure");
}

template <typename Exception, typename Check>
void expect_start_failure(const std::string& scenario, Behavior behavior,
                          std::chrono::milliseconds start_timeout, Check matches) {
    FakeReceiver receiver(behavior);
    const auto credentials = receiver.credentials();
    auto options = options_for(receiver);
    options.start_timeout = start_timeout;
    try {
        (void)UrlPlaybackSession::start(credentials, std::move(options));
        check(false, scenario + ": start succeeded");
    } catch (const Exception& error) {
        check(matches(error), scenario + ": wrong category");
    }
    check(receiver.control_was_closed(), scenario + ": control closed");
    check(!receiver.event_channel_opened() || receiver.event_was_closed(),
          scenario + ": event channel closed");
}

void failure_tests() {
    group = "start failures";
    Behavior rejected_setup;
    rejected_setup.base_setup_status = 500;
    expect_start_failure<SessionException>(
        "base SETUP rejected", rejected_setup, 3000ms, [](const SessionException& error) {
            return error.reason() == SessionError::rejected && error.status() == 500;
        });
    Behavior rejected_insert;
    rejected_insert.reject_command = 0;
    expect_start_failure<SessionException>(
        "insertPlayQueueItem rejected", rejected_insert, 3000ms, [](const SessionException& error) {
            return error.reason() == SessionError::rejected && error.status() == 400;
        });
    Behavior loading_only;
    loading_only.report_playing = false;
    expect_start_failure<SessionException>("never playing", loading_only, 300ms,
                                           [](const SessionException& error) {
                                               return error.reason() == SessionError::start_timeout;
                                           });
    Behavior lost_events;
    lost_events.end_events_on_rate = true;
    expect_start_failure<SessionException>(
        "event channel ends during start", lost_events, 3000ms, [](const SessionException& error) {
            return error.reason() == SessionError::connection_lost;
        });

    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    std::atomic_bool cancelled{true};
    try {
        (void)UrlPlaybackSession::start(credentials, options_for(receiver), &cancelled);
        check(false, "cancelled start succeeded");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::cancelled, "cancelled start category");
    }
}

void failure_after_start_tests() {
    group = "failure after start";
    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    auto session = UrlPlaybackSession::start(credentials, options_for(receiver));
    receiver.end_event_channel();
    const auto status = session->wait_for_change("playing", 2000ms);
    check(status.failed, "a lost event channel marks the session failed");
    session->stop();
    check(receiver.control_was_closed(), "stop still closes the control connection");
}
} // namespace

int main() {
    try {
        happy_path_tests();
        failure_tests();
        failure_after_start_tests();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " URL playback session test failure(s)\n";
        return 1;
    }
    std::cout << "URL playback session tests passed\n";
    return 0;
}
