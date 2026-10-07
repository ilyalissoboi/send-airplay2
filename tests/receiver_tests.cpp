// SPDX-License-Identifier: Apache-2.0
// Public synthetic identities/secrets only. Real receiver authentication is a separate gate.
#include "receiver_connection.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace send_airplay2::detail;
using namespace std::chrono_literals;
namespace send_airplay2::detail {
// Injection remains test-only and uses the existing independent SRP oracle.
struct PairSetupTestAccess {
    static void deterministic(PairSetup& setup) {
        (void)botan_rng_destroy(setup.srp_.rng_);
        setup.srp_.rng_ = nullptr;
        if (botan_rng_init_custom(&setup.srp_.rng_, "synthetic-test-only", nullptr, random_bytes,
                                  nullptr, nullptr)) {
            throw std::runtime_error("Test RNG initialization failed");
        }
        for (std::size_t byte = 0; byte < 32; ++byte) {
            setup.controller_seed_.bytes[byte] = static_cast<std::uint8_t>(byte);
        }
    }

private:
    static int random_bytes(void*, std::uint8_t* output, std::size_t length) {
        std::fill_n(output, length, std::uint8_t{0x42});
        return 0;
    }
};
struct ReceiverConnectionTestAccess {
    static void deterministic_setup(ReceiverConnection& connection) {
        PairSetupTestAccess::deterministic(*connection.setup_);
    }
    static bool closed(const ReceiverConnection& connection) {
        return connection.state_ == ReceiverConnection::State::closed && !connection.setup_ &&
               !connection.writer_ && !connection.reader_ && connection.pending_m2_.body.empty() &&
               !connection.channel_keys_.available();
    }
    static void exhaust_sequence(ReceiverConnection& connection) {
        connection.sequence_ = UINT32_MAX;
    }
};
} // namespace send_airplay2::detail
namespace {
int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}
template <class Action>
void reject(const std::string& scenario, TransportError expected, Action action) {
    try {
        action();
        check(false, scenario + ": accepted");
    } catch (const TransportException& error) {
        check(error.reason() == expected,
              scenario + ": category " + std::to_string(static_cast<int>(error.reason())));
    }
}
Bytes text(std::string_view value) {
    return Bytes(value.begin(), value.end());
}
Bytes fixture(const std::string& directory, const char* name) {
    std::ifstream input(directory + "/" + name + ".hex");
    std::string hex;
    if (!(input >> hex) || hex.size() % 2) {
        throw std::runtime_error("Missing synthetic fixture");
    }
    Bytes output;
    for (std::size_t offset = 0; offset < hex.size(); offset += 2) {
        output.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(offset, 2), nullptr, 16)));
    }
    return output;
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed(const Bytes& input) {
    if (input.size() != Size) {
        throw std::runtime_error("Wrong synthetic fixture length");
    }
    std::array<std::uint8_t, Size> result{};
    std::copy(input.begin(), input.end(), result.begin());
    return result;
}
template <std::size_t Size> Bytes bytes(const std::array<std::uint8_t, Size>& input) {
    return Bytes(input.begin(), input.end());
}
Bytes response(const Bytes& body, unsigned sequence, unsigned status = 200,
               const char* protocol = "HTTP/1.1") {
    auto result = text(std::string(protocol) + " " + std::to_string(status) +
                       " OK\r\nCSeq: " + std::to_string(sequence) +
                       "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n");
    result.insert(result.end(), body.begin(), body.end());
    return result;
}
void feed_chunks(ReceiverResponseParser& parser, const Bytes& wire, std::size_t chunk) {
    for (std::size_t offset = 0; offset < wire.size(); offset += chunk) {
        const auto end = std::min(offset + chunk, wire.size());
        parser.feed(Bytes(wire.begin() + static_cast<std::ptrdiff_t>(offset),
                          wire.begin() + static_cast<std::ptrdiff_t>(end)));
    }
}
void framing_tests() {
    group = "incremental response framing";
    const Bytes body{0, 255, '\r', '\n', 1};
    const auto wire = response(body, 7);
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        ReceiverResponseParser parser(ReceiverProtocol::http, 7, 5);
        parser.feed(Bytes(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(split)));
        parser.feed(Bytes(wire.begin() + static_cast<std::ptrdiff_t>(split), wire.end()));
        check(parser.complete(), "complete at split " + std::to_string(split));
        parser.finish();
        const auto result = parser.take();
        check(result.status == 200 && result.body == body,
              "binary body at split " + std::to_string(split));
        reject("single response release", TransportError::closed, [&] { (void)parser.take(); });
    }
    ReceiverResponseParser bytewise(ReceiverProtocol::http, 7, 5);
    feed_chunks(bytewise, wire, 1);
    check(bytewise.take().body == body, "one byte per call");
    ReceiverResponseParser ordered(ReceiverProtocol::http, 3, 0);
    ordered.feed(text("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"));
    check(ordered.take().status == 200, "HTTP ordered response without CSeq");
    ReceiverResponseParser rtsp(ReceiverProtocol::rtsp, 2, 0);
    rtsp.feed(response({}, 2, 200, "RTSP/1.0"));
    check(rtsp.take().status == 200, "RTSP correlated response");
    ReceiverResponseParser no_content(ReceiverProtocol::http, 1, 0);
    no_content.feed(text("HTTP/1.1 204 No Content\r\n\r\n"));
    check(no_content.take().body.empty(), "204 is bodyless without Content-Length");
    ReceiverResponseParser large(ReceiverProtocol::http, 1, 32768);
    feed_chunks(large, response(Bytes(32768, 0xaa), 1), 4096);
    check(large.take().body.size() == 32768, "literal maximum body size");
    for (std::size_t prefix = 0; prefix < wire.size(); ++prefix) {
        ReceiverResponseParser truncated(ReceiverProtocol::http, 7, 5);
        truncated.feed(Bytes(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(prefix)));
        reject("EOF prefix " + std::to_string(prefix), TransportError::disconnected,
               [&] { truncated.finish(); });
    }
}
void malformed_tests() {
    group = "strict framing and correlation";
    const std::vector<std::string> invalid_headers = {
        "Content-Length: 0\r\ncontent-length: 0\r\n",
        "Content-Length: 0, 0\r\n",
        "Content-Length: -1\r\n",
        "Content-Length: 4294967296\r\n",
        "Content-Length: 32769\r\n",
        "Content-Length : 0\r\n",
        " Content-Length: 0\r\n",
        "Content-Length: 0\nX: x\r\n",
        "Content-Length: 0\r\nTransfer-Encoding: chunked\r\n",
        "Transfer-Encoding: chunked\r\n",
        "Content-Length: 0\r\nUpgrade: test\r\n",
        "Content-Length: 0\r\nConnection: close\r\n",
        "Content-Length: 0\r\nX: a\r\nX: b\r\n",
        "X: body framing absent\r\n",
        "Content-Length: 0\r\nX: " + std::string(1024, 'a') + "\r\n",
        std::string("Content-Length: 0\r\nX: ") + char(0) + "\r\n"};
    for (std::size_t index = 0; index < invalid_headers.size(); ++index) {
        ReceiverResponseParser parser(ReceiverProtocol::http, 1, 32768);
        reject("header case " + std::to_string(index), TransportError::invalid_message,
               [&] { parser.feed(text("HTTP/1.1 200 OK\r\n" + invalid_headers[index] + "\r\n")); });
        reject("terminal parser case " + std::to_string(index), TransportError::closed,
               [&] { parser.feed({}); });
    }
    for (const auto* status : {"HTTP/1.0 200 OK", "HTTP/1.1 100 Continue", "HTTP/1.1 304 Cached",
                               "HTTP/1.1 600 Bad", "HTTP/1.1 20a Bad", "HTTP/1.1 200"}) {
        ReceiverResponseParser parser(ReceiverProtocol::http, 1, 0);
        reject(status, TransportError::invalid_message,
               [&] { parser.feed(text(std::string(status) + "\r\nContent-Length: 0\r\n\r\n")); });
    }
    ReceiverResponseParser wrong(ReceiverProtocol::http, 1, 0);
    reject("wrong CSeq", TransportError::correlation, [&] { wrong.feed(response({}, 2)); });
    ReceiverResponseParser missing(ReceiverProtocol::rtsp, 1, 0);
    reject("missing RTSP CSeq", TransportError::correlation,
           [&] { missing.feed(text("RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n")); });
    ReceiverResponseParser extra(ReceiverProtocol::http, 1, 0);
    auto coalesced = response({}, 1);
    const auto second = response({}, 2);
    coalesced.insert(coalesced.end(), second.begin(), second.end());
    reject("coalesced unsolicited response", TransportError::correlation,
           [&] { extra.feed(coalesced); });
    ReceiverResponseParser excessive(ReceiverProtocol::http, 1, 0);
    reject("header cap without delimiter", TransportError::invalid_message,
           [&] { feed_chunks(excessive, Bytes(8193, 'a'), 4096); });
    ReceiverResponseParser fields(ReceiverProtocol::http, 1, 0);
    std::string headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n";
    for (unsigned index = 0; index < 32; ++index) {
        headers += "X" + std::to_string(index) + ": v\r\n";
    }
    reject("field count cap", TransportError::invalid_message,
           [&] { fields.feed(text(headers + "\r\n")); });
    ReceiverResponseParser limited_body(ReceiverProtocol::http, 1, 4);
    reject("per exchange body cap before buffering", TransportError::invalid_message,
           [&] { limited_body.feed(response(Bytes(5), 1)); });
}
void request_tests() {
    group = "request bounds and injection";
    ReceiverRequest request;
    request.target = "/pair-verify";
    request.body = {6, 1, 1};
    const auto expected = text("POST /pair-verify HTTP/1.1\r\nHost: 127.0.0.1:7000\r\nCSeq: 1\r\n"
                               "Content-Length: 3\r\nConnection: keep-alive\r\n\r\n\x06\x01\x01");
    check(encode_receiver_request(request, "127.0.0.1:7000", 1) == expected,
          "literal request bytes");
    for (const auto* name : {"Content-Length", "Transfer-Encoding", "CSeq", "Host", "Connection",
                             "Upgrade", "Trailer", "bad name"}) {
        request.headers = {{name, "0"}};
        reject(name, TransportError::invalid_message,
               [&] { (void)encode_receiver_request(request, "127.0.0.1:1", 1); });
    }
    request.headers = {{"X", "v\r\nInjected: true"}};
    reject("header injection", TransportError::invalid_message,
           [&] { (void)encode_receiver_request(request, "127.0.0.1:1", 1); });
    request.headers.clear();
    request.target = "/x\r\n";
    reject("target injection", TransportError::invalid_message,
           [&] { (void)encode_receiver_request(request, "127.0.0.1:1", 1); });
    request.target = "/x";
    request.method = "HEAD";
    reject("HEAD unsupported framing", TransportError::invalid_message,
           [&] { (void)encode_receiver_request(request, "127.0.0.1:1", 1); });
    request.method = "POST";
    request.body.resize(32769);
    reject("request body cap", TransportError::invalid_message,
           [&] { (void)encode_receiver_request(request, "127.0.0.1:1", 1); });
}

// Independent test-server framing, intentionally separate from the response parser/encoder.
struct IncomingRequest {
    std::string first_line;
    Bytes body;
};
bool consume_request(Bytes& input, IncomingRequest& output) {
    const std::string view(input.begin(), input.end());
    const auto end = view.find("\r\n\r\n");
    if (end == std::string::npos) {
        return false;
    }
    const auto length_field = view.find("\r\nContent-Length: ");
    if (length_field == std::string::npos) {
        throw std::runtime_error("Test server missing length");
    }
    const auto body_length = std::stoul(view.substr(length_field + 18));
    constexpr std::size_t delimiter_size = 4;
    const auto body_start = end + delimiter_size;
    if (input.size() < body_start + body_length) {
        return false;
    }
    output.first_line = view.substr(0, view.find("\r\n"));
    output.body.assign(input.begin() + static_cast<std::ptrdiff_t>(body_start),
                       input.begin() + static_cast<std::ptrdiff_t>(body_start + body_length));
    input.erase(input.begin(),
                input.begin() + static_cast<std::ptrdiff_t>(body_start + body_length));
    return true;
}
struct ScriptStream final : ReceiverStream {
    std::function<Bytes(const IncomingRequest&, unsigned)> respond;
    Bytes inbound;
    Bytes accumulated;
    std::vector<IncomingRequest> requests;
    std::size_t write_fragment = 7;
    std::size_t read_fragment = 1;
    bool closed = false;
    bool eof = false;
    bool fail_write = false;
    bool fail_read = false;
    bool idle_failure = false;
    std::function<void()> before_write;
    std::unique_ptr<ControlReader> server_reader;
    std::unique_ptr<ControlWriter> server_writer;
    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override {
        operation.check();
        if (before_write) {
            before_write();
            operation.check();
        }
        if (fail_write) {
            throw TransportException(TransportError::network);
        }
        const auto count = std::min(size, write_fragment);
        Bytes wire(data, data + count);
        auto plain = server_reader ? server_reader->feed(wire) : std::move(wire);
        accumulated.insert(accumulated.end(), plain.begin(), plain.end());
        IncomingRequest request;
        if (consume_request(accumulated, request)) {
            requests.push_back(request);
            auto result = respond(request, static_cast<unsigned>(requests.size()));
            // The callback owns encryption to allow deliberate malformed/truncated records.
            inbound.insert(inbound.end(), result.begin(), result.end());
        }
        return count;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        operation.check();
        if (fail_read) {
            throw TransportException(TransportError::timeout);
        }
        if (inbound.empty()) {
            if (eof) {
                return 0;
            }
            throw std::runtime_error("Synthetic receiver response queue unexpectedly empty");
        }
        const auto count = std::min({capacity, inbound.size(), read_fragment});
        std::copy_n(inbound.begin(), count, data);
        inbound.erase(inbound.begin(), inbound.begin() + static_cast<std::ptrdiff_t>(count));
        return count;
    }
    void require_idle(const ReceiverOperation& operation) override {
        operation.check();
        if (idle_failure || !inbound.empty()) {
            throw TransportException(TransportError::correlation);
        }
    }
    void close() noexcept override {
        closed = true;
        cleanse(accumulated.data(), accumulated.size());
        accumulated.clear();
        inbound.clear();
        server_reader.reset();
        server_writer.reset();
    }
};

void pin_transport_tests(const std::string& directory) {
    group = "PIN setup over fragmented transport";
    auto stream = std::make_unique<ScriptStream>();
    auto* observed = stream.get();
    observed->respond = [&](const IncomingRequest& request, unsigned sequence) {
        if (sequence == 1) {
            check(request.first_line == "POST /pair-pin-start HTTP/1.1" && request.body.empty(),
                  "PIN display request");
            return response({}, sequence);
        }
        const auto phase = sequence == 2 ? "m1" : sequence == 3 ? "m3" : "m5";
        check(request.first_line == "POST /pair-setup HTTP/1.1",
              "setup endpoint sequence " + std::to_string(sequence));
        check(request.body == fixture(directory, phase),
              "independent outbound " + std::string(phase));
        return response(fixture(directory, sequence == 2   ? "m2"
                                           : sequence == 3 ? "m4"
                                                           : "m6"),
                        sequence);
    };
    ReceiverConnection connection(std::move(stream), "127.0.0.1:7000");
    connection.begin_pairing(text("synthetic-controller"), ReceiverOperation::after(5s));
    ReceiverConnectionTestAccess::deterministic_setup(connection);
    auto credentials = connection.finish_pairing("0123", ReceiverOperation::after(30s));
    check(credentials != nullptr && observed->requests.size() == 4, "credential released after M6");
    check(observed->closed && ReceiverConnectionTestAccess::closed(connection),
          "provisioning socket and secrets closed");
    reject("single credential release", TransportError::closed,
           [&] { (void)connection.finish_pairing("0123", ReceiverOperation::after(1s)); });
}

void pin_failure_tests(const std::string& directory) {
    group = "PIN transport failure gates";
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        auto stream = std::make_unique<ScriptStream>();
        auto* observed = stream.get();
        observed->respond = [&](const IncomingRequest&, unsigned seq) {
            if (seq == 1) {
                return response({}, seq);
            }
            auto body = fixture(directory, seq == 2 ? "m2" : seq == 3 ? "m4" : "bad-signature-m6");
            if (scenario == 1 && seq == 3) {
                body.back() ^= 1;
            }
            return response(body, seq);
        };
        ReceiverConnection connection(std::move(stream), "127.0.0.1:7000");
        connection.begin_pairing(text("synthetic-controller"), ReceiverOperation::after(5s));
        ReceiverConnectionTestAccess::deterministic_setup(connection);
        std::atomic_bool cancelled{scenario == 0};
        try {
            (void)connection.finish_pairing("0123", ReceiverOperation::after(30s, &cancelled));
            check(false, "failed enrollment released credentials");
        } catch (const TransportException& error) {
            check(scenario == 0 && error.reason() == TransportError::cancelled,
                  "cancelled PIN prompt");
        } catch (const PairSetupException& error) {
            check(scenario != 0 && error.reason() == PairSetupError::authentication,
                  "proof/signature failure propagated");
        }
        check(observed->requests.size() == scenario + 2,
              "failed gate prevents subsequent message, case " + std::to_string(scenario));
        check(observed->closed && ReceiverConnectionTestAccess::closed(connection),
              "failed enrollment cleanup");
    }
}

ControlNonce named_nonce(std::string_view name) {
    ControlNonce nonce{};
    std::copy(name.begin(), name.end(), nonce.begin() + 4);
    return nonce;
}
const Bytes& tlv_field(const std::vector<TlvField>& fields, std::uint8_t tag) {
    const auto it = std::find_if(fields.begin(), fields.end(),
                                 [tag](const auto& field) { return field.type == tag; });
    if (it == fields.end()) {
        throw std::runtime_error("Synthetic server missing TLV field");
    }
    return it->value;
}
/// A dynamic synthetic accessory authenticates the production client's fresh randomness.
/// Independent fixed transcripts still validate the primitives in the existing suites.
struct VerifyingReceiver {
    ScriptStream* stream;
    Secret32 server_ephemeral;
    Secret32 receiver_seed;
    Secret32 client_seed;
    Secret32 shared;
    Secret32 session_key;
    PublicKey client_public{};
    PublicKey server_public{};
    bool wrong_identity = false;
    bool bad_tag = false;
    bool replay = false;
    bool partial_extra = false;
    Bytes previous_record;
    explicit VerifyingReceiver(ScriptStream* input) : stream(input) {
        for (std::size_t index = 0; index < 32; ++index) {
            receiver_seed.bytes[index] = static_cast<std::uint8_t>(index + 32);
            server_ephemeral.bytes[index] = static_cast<std::uint8_t>(index + 96);
            client_seed.bytes[index] = static_cast<std::uint8_t>(index);
        }
        server_public = x25519_public(server_ephemeral);
    }
    Bytes respond(const IncomingRequest& request, unsigned sequence) {
        if (sequence == 1) {
            check(request.first_line == "POST /pair-verify HTTP/1.1", "verify endpoint M1");
            const auto fields = decode_tlv(request.body);
            check(tlv_field(fields, 6) == Bytes{1}, "verify M1 state");
            client_public = fixed<32>(tlv_field(fields, 3));
            x25519_shared(server_ephemeral, client_public, shared);
            session_key.bytes = derive_control_key(bytes(shared.bytes), "Pair-Verify-Encrypt-Salt",
                                                   "Pair-Verify-Encrypt-Info");
            const auto identity =
                text(wrong_identity ? "different-receiver" : "synthetic-receiver");
            auto transcript = bytes(server_public);
            transcript.insert(transcript.end(), identity.begin(), identity.end());
            transcript.insert(transcript.end(), client_public.begin(), client_public.end());
            const auto sealed = seal_record(
                session_key.bytes, named_nonce("PV-Msg02"), {},
                encode_tlv({{1, identity}, {10, bytes(ed25519_sign(receiver_seed, transcript))}}));
            return response(encode_tlv({{6, {2}}, {3, bytes(server_public)}, {5, sealed}}),
                            sequence);
        }
        if (sequence == 2) {
            const auto outer = decode_tlv(request.body);
            check(tlv_field(outer, 6) == Bytes{3}, "verify M3 state");
            const auto inner = decode_tlv(
                open_record(session_key.bytes, named_nonce("PV-Msg03"), {}, tlv_field(outer, 5)));
            const auto identity = text("synthetic-controller");
            auto transcript = bytes(client_public);
            transcript.insert(transcript.end(), identity.begin(), identity.end());
            transcript.insert(transcript.end(), server_public.begin(), server_public.end());
            check(tlv_field(inner, 1) == identity &&
                      ed25519_verify(ed25519_public(client_seed), transcript,
                                     fixed<64>(tlv_field(inner, 10))),
                  "accessory verifies controller signature");
            stream->server_reader = std::make_unique<ControlReader>(derive_control_key(
                bytes(shared.bytes), "Control-Salt", "Control-Write-Encryption-Key"));
            stream->server_writer = std::make_unique<ControlWriter>(derive_control_key(
                bytes(shared.bytes), "Control-Salt", "Control-Read-Encryption-Key"));
            return response(encode_tlv({{6, {4}}}), sequence);
        }
        check(request.first_line == "GET /info RTSP/1.0", "authenticated RTSP request decrypted");
        if (replay && !previous_record.empty()) {
            return previous_record;
        }
        auto wire = stream->server_writer->encrypt(
            response(text("synthetic-info"), sequence, 200, "RTSP/1.0"));
        if (bad_tag) {
            wire.back() ^= 1;
        }
        if (partial_extra) {
            wire.push_back(1);
        }
        previous_record = wire;
        return wire;
    }
    PairCredentials credentials() {
        return PairCredentials(text("synthetic-receiver"), ed25519_public(receiver_seed),
                               text("synthetic-controller"), client_seed);
    }
};
ReceiverRequest info_request() {
    ReceiverRequest request;
    request.method = "GET";
    request.target = "/info";
    request.protocol = ReceiverProtocol::rtsp;
    return request;
}
void verification_transport_tests() {
    group = "authenticated record transition";
    for (const auto chunk : {std::size_t{1}, std::size_t{4096}}) {
        auto stream = std::make_unique<ScriptStream>();
        auto* observed = stream.get();
        observed->read_fragment = chunk;
        VerifyingReceiver accessory(observed);
        observed->respond = [&](const IncomingRequest& request, unsigned seq) {
            return accessory.respond(request, seq);
        };
        ReceiverConnection connection(std::move(stream), "127.0.0.1:7000");
        auto credentials = accessory.credentials();
        connection.verify(credentials, ReceiverOperation::after(5s));
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            check(connection.request(info_request(), 32, ReceiverOperation::after(5s)).body ==
                      text("synthetic-info"),
                  "encrypted request/response, chunk " + std::to_string(chunk));
        }
        check(observed->requests.size() == 4 && !observed->closed,
              "one connection preserves counters across exchanges");
        // The accessory derives event keys from its side of the shared secret with
        // literal labels; the sender's write key is the receiver's "Read" key.
        Secret32 event_write, event_read;
        connection.derive_channel_keys(event_channel_labels(), event_write, event_read);
        const auto shared_secret = bytes(accessory.shared.bytes);
        check(event_write.bytes == derive_control_key(shared_secret, "Events-Salt",
                                                      "Events-Read-Encryption-Key") &&
                  event_read.bytes == derive_control_key(shared_secret, "Events-Salt",
                                                         "Events-Write-Encryption-Key"),
              "event channel keys match the accessory, chunk " + std::to_string(chunk));
        connection.close();
        check(observed->closed && ReceiverConnectionTestAccess::closed(connection),
              "close wipes both record directions and the channel secret");
        reject("channel keys after close", TransportError::closed, [&] {
            connection.derive_channel_keys(event_channel_labels(), event_write, event_read);
        });
    }
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        auto stream = std::make_unique<ScriptStream>();
        auto* observed = stream.get();
        observed->read_fragment = 4096;
        VerifyingReceiver accessory(observed);
        accessory.wrong_identity = scenario == 0;
        accessory.bad_tag = scenario == 1;
        accessory.replay = scenario == 2;
        accessory.partial_extra = scenario == 3;
        observed->respond = [&](const IncomingRequest& request, unsigned seq) {
            return accessory.respond(request, seq);
        };
        ReceiverConnection connection(std::move(stream), "127.0.0.1:7000");
        auto credentials = accessory.credentials();
        try {
            connection.verify(credentials, ReceiverOperation::after(5s));
            (void)connection.request(info_request(), 32, ReceiverOperation::after(5s));
            if (scenario == 2) {
                (void)connection.request(info_request(), 32, ReceiverOperation::after(5s));
            }
            check(false, "failure case accepted " + std::to_string(scenario));
        } catch (const PairVerifyException& error) {
            check(scenario == 0 && error.reason() == PairVerifyError::authentication,
                  "wrong identity rejected");
        } catch (const ControlException& error) {
            check((scenario == 1 || scenario == 2) &&
                      error.reason() == ControlError::authentication,
                  "tag/replay rejected");
        } catch (const TransportException& error) {
            check(scenario == 3 && error.reason() == TransportError::correlation,
                  "trailing partial unsolicited record rejected");
        }
        check(observed->closed && ReceiverConnectionTestAccess::closed(connection),
              "failure cleanup case " + std::to_string(scenario));
    }
}

void lifecycle_tests() {
    group = "deadline cancellation and failure cleanup";
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        auto stream = std::make_unique<ScriptStream>();
        auto* observed = stream.get();
        observed->fail_write = scenario == 2;
        observed->fail_read = scenario == 3;
        observed->eof = scenario == 4;
        observed->idle_failure = scenario == 5;
        observed->respond = [](const IncomingRequest&, unsigned seq) { return response({}, seq); };
        if (scenario == 4) {
            observed->respond = [](const IncomingRequest&, unsigned) { return Bytes{}; };
        }
        ReceiverConnection connection(std::move(stream), "127.0.0.1:7000");
        std::atomic_bool cancelled{scenario == 1};
        auto operation = ReceiverOperation::after(5s, &cancelled);
        if (scenario == 0) {
            operation.deadline = std::chrono::steady_clock::now() - 1ms;
        }
        if (scenario == 6) {
            ReceiverConnectionTestAccess::exhaust_sequence(connection);
        }
        const auto expected = scenario == 0 || scenario == 3 ? TransportError::timeout
                              : scenario == 1                ? TransportError::cancelled
                              : scenario == 2                ? TransportError::network
                              : scenario == 4                ? TransportError::disconnected
                                                             : TransportError::correlation;
        reject("lifecycle case " + std::to_string(scenario), expected,
               [&] { connection.begin_pairing(text("synthetic-controller"), operation); });
        check(observed->closed && ReceiverConnectionTestAccess::closed(connection),
              "terminal cleanup case " + std::to_string(scenario));
    }
    reject("zero timeout", TransportError::invalid_argument,
           [] { (void)ReceiverOperation::after(0ms); });
    reject("timeout cap", TransportError::invalid_argument,
           [] { (void)ReceiverOperation::after(60001ms); });
    auto stream = std::make_unique<ScriptStream>();
    auto* observed = stream.get();
    observed->before_write = [] { std::this_thread::sleep_for(2ms); };
    observed->respond = [](const IncomingRequest&, unsigned seq) { return response({}, seq); };
    ReceiverConnection connection(std::move(stream), "127.0.0.1:7000");
    reject("partial write progress does not renew deadline", TransportError::timeout, [&] {
        connection.begin_pairing(text("synthetic-controller"), ReceiverOperation::after(20ms));
    });
    check(observed->closed && observed->requests.empty(),
          "deadline expires while a request is partially written");
}

#ifdef _WIN32
using TestSocket = SOCKET;
constexpr TestSocket invalid_test_socket = INVALID_SOCKET;
void close_test_socket(TestSocket value) {
    closesocket(value);
}
#else
using TestSocket = int;
constexpr TestSocket invalid_test_socket = -1;
void close_test_socket(TestSocket value) {
    ::close(value);
}
#endif
struct TestNetworkRuntime {
    TestNetworkRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) {
            throw std::runtime_error("Test Winsock initialization failed");
        }
#endif
    }
    ~TestNetworkRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
    TestNetworkRuntime(const TestNetworkRuntime&) = delete;
    TestNetworkRuntime& operator=(const TestNetworkRuntime&) = delete;
    TestNetworkRuntime(TestNetworkRuntime&&) = delete;
    TestNetworkRuntime& operator=(TestNetworkRuntime&&) = delete;
};
struct TestSocketOwner {
    TestSocket value = invalid_test_socket;
    TestSocketOwner() = default;
    explicit TestSocketOwner(TestSocket input) : value(input) {
        if (value == invalid_test_socket) {
            throw std::runtime_error("Test accept failed");
        }
    }
    ~TestSocketOwner() {
        close();
    }
    TestSocketOwner(const TestSocketOwner&) = delete;
    TestSocketOwner& operator=(const TestSocketOwner&) = delete;
    TestSocketOwner(TestSocketOwner&&) = delete;
    TestSocketOwner& operator=(TestSocketOwner&&) = delete;
    void close() noexcept {
        if (value != invalid_test_socket) {
            close_test_socket(value);
            value = invalid_test_socket;
        }
    }
};
struct JoiningThread {
    std::thread value;
    explicit JoiningThread(std::function<void()> task) : value(std::move(task)) {}
    ~JoiningThread() {
        if (value.joinable()) {
            value.join();
        }
    }
    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;
    JoiningThread(JoiningThread&&) = delete;
    JoiningThread& operator=(JoiningThread&&) = delete;
};
/// Real loopback socket exercises the native adapter without involving LAN devices.
struct LoopbackListener {
    [[maybe_unused]] TestNetworkRuntime runtime;
    TestSocketOwner socket;
    ReceiverEndpoint endpoint;
    explicit LoopbackListener(bool ipv6 = false) {
        socket.value = ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_storage storage{};
        int size;
        if (ipv6) {
            sockaddr_in6 local{};
            local.sin6_family = AF_INET6;
            local.sin6_addr = in6addr_loopback;
            std::memcpy(&storage, &local, sizeof(local));
            size = sizeof(local);
            endpoint.address = "::1";
        } else {
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            std::memcpy(&storage, &local, sizeof(local));
            size = sizeof(local);
            endpoint.address = "127.0.0.1";
        }
        if (socket.value == invalid_test_socket ||
            bind(socket.value, reinterpret_cast<const sockaddr*>(&storage), size) ||
            listen(socket.value, 1)) {
            throw std::runtime_error("Test loopback bind failed");
        }
#ifdef _WIN32
        int length = sizeof(storage);
#else
        socklen_t length = sizeof(storage);
#endif
        if (getsockname(socket.value, reinterpret_cast<sockaddr*>(&storage), &length)) {
            throw std::runtime_error("Test getsockname failed");
        }
        endpoint.port = ntohs(ipv6 ? reinterpret_cast<const sockaddr_in6*>(&storage)->sin6_port
                                   : reinterpret_cast<const sockaddr_in*>(&storage)->sin_port);
    }
    ~LoopbackListener() = default;
    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;
    LoopbackListener(LoopbackListener&&) = delete;
    LoopbackListener& operator=(LoopbackListener&&) = delete;
};
void native_socket_tests() {
    group = "native IPv4/IPv6 loopback";
    for (bool ipv6 : {false, true}) {
        LoopbackListener listener(ipv6);
        auto stream = connect_receiver(listener.endpoint, ReceiverOperation::after(2s));
        TestSocketOwner peer(accept(listener.socket.value, nullptr, nullptr));
        stream->require_idle(ReceiverOperation::after(1s));
        check(!stream->wait_readable(ReceiverOperation::after(20ms)),
              "idle poll expires without closing native socket");
        stream->require_idle(ReceiverOperation::after(1s));
        const auto request = text("loopback");
        std::size_t sent = 0;
        while (sent < request.size()) {
            sent += stream->write_some(request.data() + sent, request.size() - sent,
                                       ReceiverOperation::after(1s));
        }
        std::array<char, 8> received{};
        std::size_t offset = 0;
        while (offset < received.size()) {
            const auto count = recv(peer.value, received.data() + offset,
                                    static_cast<int>(received.size() - offset), 0);
            if (count <= 0) {
                throw std::runtime_error("Test loopback recv failed");
            }
            offset += static_cast<std::size_t>(count);
        }
        check(std::string(received.data(), received.size()) == "loopback", "native outbound bytes");
        if (send(peer.value, "reply", 5, 0) != 5) {
            throw std::runtime_error("Test loopback send failed");
        }
        Bytes inbound(5);
        check(stream->wait_readable(ReceiverOperation::after(1s)),
              "native poll detects reply without consuming it");
        offset = 0;
        while (offset < inbound.size()) {
            offset += stream->read_some(inbound.data() + offset, inbound.size() - offset,
                                        ReceiverOperation::after(1s));
        }
        check(inbound == text("reply"), "native inbound bytes");
        reject("idle-read deadline", TransportError::timeout, [&] {
            (void)stream->read_some(inbound.data(), inbound.size(), ReceiverOperation::after(30ms));
        });
        reject("native timeout closes socket", TransportError::closed,
               [&] { stream->require_idle(ReceiverOperation::after(1s)); });
    }
    LoopbackListener listener;
    auto stream = connect_receiver(listener.endpoint, ReceiverOperation::after(2s));
    TestSocketOwner peer(accept(listener.socket.value, nullptr, nullptr));
    std::atomic_bool cancel{false};
    JoiningThread trigger([&] {
        std::this_thread::sleep_for(30ms);
        cancel.store(true);
    });
    Bytes input(1);
    reject("cancellation during native wait", TransportError::cancelled, [&] {
        (void)stream->read_some(input.data(), input.size(), ReceiverOperation::after(2s, &cancel));
    });
    for (const ReceiverEndpoint& endpoint :
         {ReceiverEndpoint{"localhost", 7000, 0}, ReceiverEndpoint{"127.0.0.1", 0, 0},
          ReceiverEndpoint{"127.0.0.1", 7000, 1}}) {
        reject("numeric endpoint validation", TransportError::invalid_argument,
               [&] { (void)connect_receiver(endpoint, ReceiverOperation::after(1s)); });
    }
    check(ReceiverEndpoint{"fe80::1", 7000, 24}.authority() == "[fe80::1]:7000",
          "IPv6 HTTP authority omits the sender-local routing scope");
}

void native_disconnect_tests() {
    group = "native disconnect and queued input";
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        LoopbackListener listener;
        auto stream = connect_receiver(listener.endpoint, ReceiverOperation::after(2s));
        TestSocketOwner peer(accept(listener.socket.value, nullptr, nullptr));
        if (scenario == 0) {
            peer.close();
            std::uint8_t byte{};
            check(stream->read_some(&byte, 1, ReceiverOperation::after(1s)) == 0,
                  "native clean EOF");
        } else {
            if (send(peer.value, "unsolicited", 11, 0) != 11) {
                throw std::runtime_error("Test send failed");
            }
            // Give the native TCP stack time to publish readiness; the assertion itself is
            // nonblocking.
            std::this_thread::sleep_for(20ms);
            reject("queued unsolicited bytes", TransportError::correlation,
                   [&] { stream->require_idle(ReceiverOperation::after(1s)); });
        }
        reject("terminal disconnect/correlation closes native socket", TransportError::closed,
               [&] { stream->require_idle(ReceiverOperation::after(1s)); });
    }
    LoopbackListener refused;
    const auto endpoint = refused.endpoint;
    refused.socket.close();
    reject("connection refused", TransportError::network,
           [&] { (void)connect_receiver(endpoint, ReceiverOperation::after(5s)); });
    std::atomic_bool cancelled{true};
    reject("cancelled connect", TransportError::cancelled,
           [&] { (void)connect_receiver(endpoint, ReceiverOperation::after(1s, &cancelled)); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected synthetic fixture directory");
        }
        framing_tests();
        malformed_tests();
        request_tests();
        pin_transport_tests(std::string(argv[1]) + "/pair-setup");
        pin_failure_tests(std::string(argv[1]) + "/pair-setup");
        verification_transport_tests();
        lifecycle_tests();
        native_socket_tests();
        native_disconnect_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
