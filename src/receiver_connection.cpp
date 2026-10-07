// SPDX-License-Identifier: Apache-2.0
#include "receiver_connection.h"
#include "control_crypto.h"
#include <limits>
#include <utility>

namespace send_airplay2::detail {
namespace {
/// Wipe owned transient protocol plaintext on success and every exceptional exit.
template <class Value> struct Wiped {
    Value value;
    explicit Wiped(Value input) : value(std::move(input)) {}
    ~Wiped() {
        wipe(value);
    }
    Wiped(const Wiped&) = delete;
    Wiped& operator=(const Wiped&) = delete;
    Wiped(Wiped&&) = delete;
    Wiped& operator=(Wiped&&) = delete;

private:
    static void wipe(Bytes& bytes) {
        cleanse(bytes.data(), bytes.size());
    }
    static void wipe(ReceiverRequest& request) {
        wipe(request.body);
    }
    static void wipe(ReceiverResponse& response) {
        wipe(response.body);
    }
};
ReceiverRequest pairing_request(const char* path, Bytes body) {
    ReceiverRequest request;
    // Move secrets first so subsequent header allocations cannot leave unowned copies.
    Wiped<ReceiverRequest> owner{std::move(request)};
    owner.value.body = std::move(body);
    owner.value.target = path;
    owner.value.headers = {{"Content-Type", "application/octet-stream"},
                           {"User-Agent", "AirPlay/320.20"},
                           {"X-Apple-HKP", "3"}};
    return std::move(owner.value);
}
} // namespace

ReceiverConnection::ReceiverConnection(std::unique_ptr<ReceiverStream> stream,
                                       std::string authority)
    : stream_(std::move(stream)), authority_(std::move(authority)) {
    if (!stream_) {
        throw TransportException(TransportError::invalid_argument);
    }
}
ReceiverConnection::~ReceiverConnection() {
    close();
}
void ReceiverConnection::require_state(State expected) const {
    if (state_ != expected) {
        throw TransportException(TransportError::closed);
    }
}
void ReceiverConnection::close() noexcept {
    state_ = State::closed;
    if (stream_) {
        stream_->close();
    }
    writer_.reset();
    reader_.reset();
    channel_keys_.clear();
    setup_.reset();
    cleanse(pending_m2_.body.data(), pending_m2_.body.size());
    pending_m2_.body.clear();
    pending_m2_.headers.clear();
}
ReceiverResponse ReceiverConnection::exchange(const ReceiverRequest& request,
                                              std::size_t response_body_limit,
                                              const ReceiverOperation& operation) {
    if (state_ == State::closed) {
        throw TransportException(TransportError::closed);
    }
    operation.check();
    if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
        throw TransportException(TransportError::correlation);
    }
    // Validate/bound the complete response before writing any request bytes.
    ReceiverResponseParser parser(request.protocol, sequence_, response_body_limit);
    Wiped<Bytes> plaintext{encode_receiver_request(request, authority_, sequence_)};
    stream_->require_idle(operation);
    Wiped<Bytes> wire{writer_ ? writer_->encrypt(plaintext.value) : std::move(plaintext.value)};
    ++sequence_;
    // Encrypted bytes are retained exactly once; partial sends never re-encrypt.
    for (std::size_t offset = 0; offset < wire.value.size();) {
        operation.check();
        const auto remaining = wire.value.size() - offset;
        const auto written = stream_->write_some(wire.value.data() + offset, remaining, operation);
        if (!written || written > remaining) {
            throw TransportException(TransportError::network);
        }
        offset += written;
    }
    while (!parser.complete()) {
        Wiped<Bytes> input{Bytes(receiver_http::read_chunk)};
        operation.check();
        const auto received = stream_->read_some(input.value.data(), input.value.size(), operation);
        if (received > input.value.size()) {
            throw TransportException(TransportError::network);
        }
        if (!received) {
            if (reader_) {
                reader_->finish();
            }
            parser.finish();
            throw TransportException(TransportError::disconnected);
        }
        input.value.resize(received);
        if (reader_) {
            Wiped<Bytes> decoded{reader_->feed(input.value)};
            parser.feed(decoded.value);
        } else {
            parser.feed(input.value);
        }
    }
    operation.check();
    if (reader_ && !reader_->at_boundary()) {
        throw TransportException(TransportError::correlation);
    }
    return parser.take();
}
void ReceiverConnection::begin_pairing(Bytes controller_id, const ReceiverOperation& operation) {
    try {
        require_state(State::fresh);
        operation.check();
        setup_ = std::make_unique<PairSetup>(std::move(controller_id));
        Wiped<ReceiverRequest> start{pairing_request("/pair-pin-start", {})};
        Wiped<ReceiverResponse> acknowledgement{exchange(start.value, 0, operation)};
        if (acknowledgement.value.status != 200) {
            throw TransportException(TransportError::invalid_message);
        }
        Wiped<ReceiverRequest> m1{pairing_request("/pair-setup", setup_->start())};
        pending_m2_ = exchange(m1.value, pair_setup::max_body_size, operation);
        if (pending_m2_.status != 200) {
            throw TransportException(TransportError::invalid_message);
        }
        operation.check();
        state_ = State::pairing;
    } catch (...) {
        close();
        throw;
    }
}
std::unique_ptr<PairCredentials>
ReceiverConnection::finish_pairing(std::string_view pin, const ReceiverOperation& operation) {
    try {
        require_state(State::pairing);
        operation.check();
        Wiped<ReceiverRequest> m3{pairing_request(
            "/pair-setup", setup_->respond(pin, pending_m2_.status, pending_m2_.body))};
        cleanse(pending_m2_.body.data(), pending_m2_.body.size());
        pending_m2_.body.clear();
        Wiped<ReceiverResponse> m4{exchange(m3.value, pair_setup::max_body_size, operation)};
        Wiped<ReceiverRequest> m5{
            pairing_request("/pair-setup", setup_->confirm(m4.value.status, m4.value.body))};
        Wiped<ReceiverResponse> m6{exchange(m5.value, pair_setup::max_body_size, operation)};
        auto credentials = setup_->finish(m6.value.status, m6.value.body);
        operation.check();
        close();
        return credentials;
    } catch (...) {
        close();
        throw;
    }
}
void ReceiverConnection::verify(const PairCredentials& credentials,
                                const ReceiverOperation& operation) {
    try {
        require_state(State::fresh);
        operation.check();
        PairVerifier verifier(credentials);
        Wiped<ReceiverRequest> m1{pairing_request("/pair-verify", verifier.start())};
        Wiped<ReceiverResponse> m2{exchange(m1.value, pair_verify::max_body_size, operation)};
        Wiped<ReceiverRequest> m3{
            pairing_request("/pair-verify", verifier.respond(m2.value.status, m2.value.body))};
        Wiped<ReceiverResponse> m4{exchange(m3.value, pair_verify::max_body_size, operation)};
        verifier.finish(m4.value.status, m4.value.body);
        operation.check();
        stream_->require_idle(operation);
        Secret32 write_key;
        Secret32 read_key;
        verifier.take_session_keys(write_key, read_key, channel_keys_);
        writer_ = std::make_unique<ControlWriter>(write_key.bytes);
        reader_ = std::make_unique<ControlReader>(read_key.bytes);
        operation.check();
        state_ = State::verified;
    } catch (...) {
        close();
        throw;
    }
}
void ReceiverConnection::derive_channel_keys(const ChannelKeyLabels& labels, Secret32& sender_write,
                                             Secret32& sender_read) {
    try {
        require_state(State::verified);
        channel_keys_.derive(labels, sender_write, sender_read);
    } catch (...) {
        close();
        throw;
    }
}
ReceiverResponse ReceiverConnection::request(const ReceiverRequest& request,
                                             std::size_t response_body_limit,
                                             const ReceiverOperation& operation) {
    try {
        require_state(State::verified);
        return exchange(request, response_body_limit, operation);
    } catch (...) {
        close();
        throw;
    }
}
} // namespace send_airplay2::detail
