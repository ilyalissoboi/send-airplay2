// SPDX-License-Identifier: Apache-2.0
// Observe owned plaintext immediately before freeing it; never read freed storage.
#include "mrp_messages.h"
#include "event_channel.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
constexpr std::size_t synthetic_payload_size = 257;
struct AllocationObservation {
    void* payload = nullptr;
    bool capture = false;
    bool fail_after_capture = false;
    bool fail_next = false;
    unsigned releases = 0;
    bool erased = true;
} observation;

void observe_release(void* storage) noexcept {
    if (storage && storage == observation.payload) {
        const auto* bytes = static_cast<const unsigned char*>(storage);
        observation.erased = std::all_of(bytes, bytes + synthetic_payload_size,
                                         [](unsigned char byte) { return byte == 0; });
        ++observation.releases;
        observation.payload = nullptr;
    }
}
} // namespace

// Replacement allocation is confined to this single-threaded test executable.
// Only a known-size decoded payload is observed. All other allocations retain
// ordinary malloc/free behavior; one injected failure is consumed exactly once.
void* operator new(std::size_t size) {
    if (observation.fail_next) {
        observation.fail_next = false;
        throw std::bad_alloc();
    }
    auto* storage = std::malloc(size ? size : 1);
    if (!storage) {
        throw std::bad_alloc();
    }
    if (observation.capture && size == synthetic_payload_size) {
        observation.capture = false;
        observation.payload = storage;
        observation.fail_next = observation.fail_after_capture;
    }
    return storage;
}
void operator delete(void* storage) noexcept {
    observe_release(storage);
    std::free(storage);
}
void operator delete(void* storage, std::size_t) noexcept {
    ::operator delete(storage);
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete[](void* storage) noexcept {
    ::operator delete(storage);
}
void operator delete[](void* storage, std::size_t) noexcept {
    ::operator delete(storage);
}

using namespace send_airplay2::detail;
static_assert(!std::is_copy_constructible_v<MrpMessage> && !std::is_copy_assignable_v<MrpMessage>);
static_assert(std::is_nothrow_move_constructible_v<MrpMessage> &&
              std::is_nothrow_move_assignable_v<MrpMessage>);
namespace {
int failures = 0;
void check(bool value, const char* scenario) {
    if (!value) {
        std::cerr << "FAIL [plaintext ownership]: " << scenario << '\n';
        ++failures;
    }
}
Bytes synthetic_message() {
    // Literal ProtocolMessage: type=4, extension=9, length=257 (0x81 0x02).
    Bytes wire{0x08, 0x04, 0x4a, 0x81, 0x02};
    wire.insert(wire.end(), synthetic_payload_size, 0x5a);
    return wire;
}
Bytes synthetic_batch() {
    // Literal outer length=262 (five wire-header bytes plus 257 payload bytes).
    Bytes batch{0x86, 0x02};
    const auto message = synthetic_message();
    batch.insert(batch.end(), message.begin(), message.end());
    return batch;
}
void begin_observation(bool allocation_failure = false) {
    observation = {};
    observation.capture = true;
    observation.fail_after_capture = allocation_failure;
}
void check_erasure(const char* scenario) {
    observation.capture = false;
    observation.fail_next = false;
    check(observation.releases == 1 && observation.erased, scenario);
}
void lifetime_tests() {
    const auto wire = synthetic_message();
    begin_observation();
    {
        auto message = decode_mrp(protobuf_wire::view(wire));
        check(message.payload.size() == synthetic_payload_size && message.payload.front() == 0x5a,
              "decoded plaintext is intact while owned");
        auto moved = std::move(message);
        check(moved.payload.data() == observation.payload,
              "move transfers the original allocation");
    }
    check_erasure("destruction erases moved plaintext before freeing it");

    begin_observation();
    {
        auto destination = decode_mrp(protobuf_wire::view(wire));
        auto source = decode_mrp(protobuf_wire::view(wire));
        destination = std::move(source);
        check(observation.releases == 1 && observation.erased,
              "move assignment erases replaced plaintext before freeing it");
    }
    check_erasure("replaced payload has exactly one erased release");
}
void exception_tests() {
    auto malformed_batch = synthetic_batch();
    malformed_batch.push_back(0); // Forbidden zero-length second ProtocolMessage.
    begin_observation();
    try {
        (void)decode_mrp_batch(malformed_batch);
        check(false, "malformed second message accepted");
    } catch (const std::invalid_argument&) {
    }
    check_erasure("later malformed input erases the already decoded first payload");

    const auto valid_batch = synthetic_batch();
    begin_observation(true);
    try {
        (void)decode_mrp_batch(valid_batch);
        check(false, "injected batch allocation failure was not reached");
    } catch (const std::bad_alloc&) {
    }
    check_erasure("allocation failure after payload decoding erases its temporary owner");
}

/// A synthetic encrypted event arrives normally, but its reply fails. No sockets.
class ReplyFailureStream final : public ReceiverStream {
public:
    explicit ReplyFailureStream(Bytes wire) : wire_(std::move(wire)) {}
    std::size_t write_some(const std::uint8_t*, std::size_t,
                           const ReceiverOperation& operation) override {
        operation.check();
        throw TransportException(TransportError::network);
    }
    std::size_t read_some(std::uint8_t* output, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        operation.check();
        const auto count = std::min(capacity, wire_.size() - offset_);
        std::copy_n(wire_.begin() + static_cast<std::ptrdiff_t>(offset_), count, output);
        offset_ += count;
        return count;
    }
    void require_idle(const ReceiverOperation&) override {}
    void close() noexcept override {}

private:
    Bytes wire_;
    std::size_t offset_ = 0;
};

void event_reply_failure_tests() {
    const std::string header = "POST /command RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 257\r\n\r\n";
    Bytes request(header.begin(), header.end());
    request.insert(request.end(), synthetic_payload_size, 0x5a);
    Secret32 write_key, read_key; // Public zero-valued fixture keys, separate owners.
    ControlWriter receiver_writer(read_key.bytes);
    auto stream = std::make_unique<ReplyFailureStream>(receiver_writer.encrypt(request));
    EventChannel channel(std::move(stream), write_key, read_key);
    begin_observation();
    try {
        (void)channel.receive(ReceiverOperation::after(std::chrono::seconds{1}));
        check(false, "failed event acknowledgment was accepted");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::network, "event reply retains network category");
    }
    check_erasure("failed event acknowledgment erases decoded body before freeing it");
    check(channel.closed(), "failed event acknowledgment closes the channel");
}
} // namespace
int main() {
    lifetime_tests();
    exception_tests();
    event_reply_failure_tests();
    if (failures) {
        return 1;
    }
    std::cout << "Plaintext ownership tests passed\n";
    return 0;
}
