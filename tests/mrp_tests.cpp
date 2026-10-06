// SPDX-License-Identifier: Apache-2.0
// Public synthetic protobuf/plistlib oracle fixtures; no receiver captures.
#include "mrp_session.h"
#include "control_crypto.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
using namespace send_airplay2::detail;
using namespace std::chrono_literals;
namespace pb = send_airplay2::detail::protobuf_wire;
namespace {
int failures = 0;
const char* group = "fixtures";
void check(bool value, const std::string& scenario) {
    if (!value) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}
template <typename Function> void invalid(Function function, const std::string& scenario) {
    try {
        function();
        check(false, scenario + " accepted");
    } catch (const std::invalid_argument&) {
    } catch (const TransportException&) {
    }
}
template <typename Predicate> bool eventually(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}
Bytes load(const std::string& root, const char* name) {
    std::ifstream file(root + "/" + name + ".bin", std::ios::binary);
    if (!file) {
        throw std::runtime_error("Missing public fixture");
    }
    return Bytes(std::istreambuf_iterator<char>(file), {});
}
Bytes text(const char* input) {
    const std::string value(input);
    return Bytes(value.begin(), value.end());
}
Bytes envelope(std::uint32_t type, const Bytes& payload) {
    return encode_mrp(type, "SYNTHETIC-REQUEST", "SYNTHETIC-UNIQUE", payload);
}
void fixtures(const std::string& root) {
    auto reply_with_body = load(root, "reply-payload");
    const auto parsed_reply = take_mrp_frame(reply_with_body);
    check(parsed_reply && !parsed_reply->sync && mrp_frame_protobufs(parsed_reply->payload).empty(),
          "receiver reply with plist body is valid and needs no acknowledgment");
    const auto path = load(root, "path");
    check(envelope(mrp::device_info,
                   mrp_device_info(SenderIdentity{}, text("synthetic-controller"))) ==
              load(root, "device"),
          "device info matches generated protobuf bytes");
    check(envelope(mrp::connection_state, mrp_connection_state()) == load(root, "connection"),
          "connection state bytes");
    check(envelope(mrp::updates_config, mrp_updates_config()) == load(root, "updates"),
          "updates subscription bytes");
    const auto seek = envelope(mrp::send_command, mrp_command(PlaybackCommand::seek, path, 30.5));
    check(seek == load(root, "seek"),
          "seek uses independent field numbers, fixed64 and explicit path");
    check(mrp_command_succeeded(decode_mrp(pb::view(load(root, "result")))),
          "literal command success");
    const auto frame = encode_mrp_frame(true, 0x0102030405060708ULL, mrp_frame_payload(seek));
    check(frame == load(root, "sync"), "network-endian sync header and plistlib envelope");
    check(encode_mrp_frame(false, 0x0102030405060708ULL, {}) == load(root, "reply"),
          "empty reply header bytes");
    for (std::size_t split = 0; split <= frame.size(); ++split) {
        Bytes pending(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(split));
        auto first = take_mrp_frame(pending);
        check(first.has_value() == (split == frame.size()),
              "partial frame at split " + std::to_string(split));
        if (first) {
            continue;
        }
        pending.insert(pending.end(), frame.begin() + static_cast<std::ptrdiff_t>(split),
                       frame.end());
        const auto parsed = take_mrp_frame(pending);
        check(parsed && parsed->sequence == 0x0102030405060708ULL && parsed->sync &&
                  pending.empty(),
              "complete frame at split " + std::to_string(split));
    }
    Bytes coalesced = frame;
    coalesced.insert(coalesced.end(), frame.begin(), frame.end());
    check(take_mrp_frame(coalesced).has_value() && take_mrp_frame(coalesced).has_value() &&
              coalesced.empty(),
          "coalesced frames");
}
void malformed_inputs() {
    group = "bounds and malformed input";
    for (const Bytes& input : {Bytes{0}, Bytes{8, 0x80}, Bytes{0x0b}, Bytes{0x0d, 0},
                               Bytes{0x0a, 0x7f}, Bytes{0x09, 0}}) {
        invalid([&] { (void)pb::decode(pb::view(input)); }, "bad wire field");
    }
    Bytes overflow(10, 0xff);
    overflow.back() = 2;
    invalid(
        [&] {
            std::size_t offset = 0;
            (void)pb::varint(pb::view(overflow), offset);
        },
        "uint64 overflow");
    const auto duplicate = pb::decode(std::string_view("\x08\x01\x08\x02", 4));
    invalid([&] { (void)pb::find(duplicate, 1, 0); }, "duplicate singular field");
    invalid([&] { (void)pb::decode(std::string(pb::max_message_size + 1, 'x')); },
            "message size literal 65537");
    check(pb::max_message_size == 65536 && mrp_frame::max_size == 262144,
          "literal protocol budgets");
    for (Bytes prefix : {Bytes{0, 0, 0, 31}, Bytes{0, 4, 0, 1}, Bytes{0xff, 0xff, 0xff, 0xff}}) {
        invalid([&] { (void)take_mrp_frame(prefix); }, "frame size rejected from first four bytes");
    }
    Bytes empty;
    invalid([&] { (void)mrp_command(PlaybackCommand::play, empty); }, "no player path");
    invalid([&] { (void)mrp_command(PlaybackCommand::seek, Bytes{1}, -1); }, "negative seek");
    invalid(
        [&] {
            (void)mrp_command(PlaybackCommand::seek, Bytes{1},
                              std::numeric_limits<double>::infinity());
        },
        "infinite seek");
    auto unknown = encode_mrp(120, {}, "synthetic");
    check(decode_mrp_batch(unknown).front().type == 120,
          "unprefixed ConfigureConnection exception");
    check(decode_mrp_batch(encode_mrp(mrp::heartbeat, {}, "synthetic")).front().type ==
              mrp::heartbeat,
          "bounded unprefixed reference message accepted");
    Bytes skipped{8, 42, 0x98, 6, 127, 0xa5, 6, 1, 2, 3, 4};
    check(decode_mrp(pb::view(skipped)).type == 42, "unknown varint and fixed32 skipped");
    Bytes result;
    pb::integer(result, 1, 10);
    check(!mrp_command_succeeded(decode_mrp(pb::view(envelope(mrp::command_result, result)))),
          "send error rejected");
    result.clear();
    pb::integer(result, 2, 401);
    check(!mrp_command_succeeded(decode_mrp(pb::view(envelope(mrp::command_result, result)))),
          "handler error rejected");
}
void large_data_records(const std::string& root) {
    group = "AirPlay data record bounds";
    const auto wire = load(root, "large-record");
    const auto plaintext = load(root, "large-record-plain");
    ControlKey key{};
    key.fill(0x22);
    ControlReader strict(key);
    try {
        (void)strict.feed(wire);
        check(false, "default control reader accepted a 2363-byte record");
    } catch (const ControlException& error) {
        check(error.reason() == ControlError::invalid_length, "control bound unchanged");
    }
    ControlReader compatible(key, 16384);
    Bytes result;
    for (std::size_t offset = 0; offset < wire.size(); offset += 17) {
        const auto end = std::min(offset + 17, wire.size());
        const auto part = compatible.feed(Bytes(wire.begin() + static_cast<std::ptrdiff_t>(offset),
                                                wire.begin() + static_cast<std::ptrdiff_t>(end)));
        result.insert(result.end(), part.begin(), part.end());
    }
    check(result == plaintext && result.size() == 2363,
          "independent large-record AEAD and fragmented decoding");
    compatible.finish();
    ControlReader over_limit(key, 16384);
    try {
        (void)over_limit.feed({1, 64});
        check(false, "16385-byte advertised record accepted");
    } catch (const ControlException& error) {
        check(error.reason() == ControlError::invalid_length, "literal 16385 receive limit");
    }
    auto corrupted = wire;
    corrupted.back() ^= 1;
    ControlReader bad_tag(key, 16384);
    try {
        (void)bad_tag.feed(corrupted);
        check(false, "large-record bad tag accepted");
    } catch (const ControlException& error) {
        check(error.reason() == ControlError::authentication,
              "large-record authentication remains mandatory");
    }
}
void ownership(const std::string& root) {
    group = "ownership and telemetry";
    MrpPlaybackTracker tracker;
    tracker.expect_item("other-item", "http://192.0.2.10/synthetic.mp4");
    tracker.apply(decode_mrp(pb::view(load(root, "state"))));
    auto status = tracker.status();
    check(status.owned && status.state == "playing" && status.position_seconds == 17.0 &&
              status.duration_seconds == 131.6,
          "exact URL ownership and initial scalar telemetry");
    check(tracker.owned_player_path() == load(root, "path"),
          "original explicit playerPath retained");
    tracker.apply(decode_mrp(pb::view(load(root, "content-update"))));
    check(tracker.status().position_seconds == 30.5 && tracker.status().playback_rate == 0.0,
          "partial metadata merges without clearing duration");
    Bytes client;
    pb::data(client, 2, "unrelated.application");
    Bytes selection;
    pb::data(selection, 1, client);
    tracker.apply(decode_mrp(pb::view(envelope(mrp::now_playing_client, selection))));
    check(!tracker.status().owned && tracker.owned_player_path().empty(),
          "another active app invalidates controls");
    MrpPlaybackTracker exact_uuid;
    exact_uuid.expect_item("synthetic-item", "other-url");
    exact_uuid.apply(decode_mrp(pb::view(load(root, "state"))));
    check(exact_uuid.status().owned, "exact queue item UUID ownership");
    Bytes removed;
    pb::data(removed, 1, load(root, "path"));
    exact_uuid.apply(decode_mrp(pb::view(envelope(mrp::remove_player, removed))));
    check(!exact_uuid.status().owned, "removed player clears ownership");
    tracker.expect_item("unrelated-item", "other-url");
    tracker.apply(decode_mrp(pb::view(load(root, "state"))));
    check(!tracker.status().owned, "same bundle with unrelated content cannot establish ownership");
}
void startup_binding(const std::string& root) {
    group = "URL/MRP startup binding";
    const auto state = decode_mrp(pb::view(load(root, "unlinked-airplay-state")));
    const auto active = decode_mrp(pb::view(load(root, "active-airplay-player")));
    MrpPlaybackTracker fresh;
    fresh.expect_item("our-uuid", "our-url");
    fresh.apply(state);
    fresh.apply(active);
    check(!fresh.status().owned, "AirPlay app name alone cannot bind");
    fresh.confirm_url_playing(300);
    check(!fresh.status().owned, "different URL duration cannot bind");
    fresh.confirm_url_playing(131.6);
    check(fresh.status().owned && fresh.status().position_seconds == 17.0,
          "new active TVAirPlay item matches URL duration");
    fresh.apply(state);
    check(fresh.status().owned, "same bound item survives queue refresh");
    fresh.apply(decode_mrp(pb::view(load(root, "replacement-airplay-state"))));
    check(!fresh.status().owned, "replacement item revokes binding without automatic adoption");
    MrpPlaybackTracker stale;
    stale.apply(state);
    stale.apply(active);
    stale.expect_item("our-uuid", "our-url");
    stale.apply(state);
    stale.confirm_url_playing(131.6);
    check(!stale.status().owned, "pre-start item remains unowned despite matching duration");
    MrpPlaybackTracker other_app;
    other_app.expect_item("our-uuid", "our-url");
    other_app.apply(decode_mrp(pb::view(load(root, "state"))));
    other_app.confirm_url_playing(131.6);
    check(!other_app.status().owned, "unrelated app cannot use duration fallback");
}

/// Thread-safe peer facts. Only the worker accesses codec instances; the test
/// configures faults under mutex before issuing a request.
struct Peer {
    std::mutex mutex;
    Bytes incoming, pending;
    std::unique_ptr<ControlReader> decrypt;
    std::unique_ptr<ControlWriter> encrypt;
    std::vector<std::uint32_t> types;
    std::vector<std::uint64_t> sender_sequences;
    std::vector<Bytes> command_paths;
    Bytes state, result;
    bool closed = false, eof = false, silent = false, wrong_type = false, wrong_identifier = false;
    bool corrupt_tag = false, reject = false;
    bool missing_device_payload = false, malformed_result = false;
    std::size_t acknowledgements = 0;
    explicit Peer(const std::string& root)
        : state(load(root, "state")), result(load(root, "result")) {
        ControlKey write{}, read{};
        write.fill(0x11);
        read.fill(0x22);
        decrypt = std::make_unique<ControlReader>(write);
        encrypt = std::make_unique<ControlWriter>(read);
    }
    void push(const Bytes& message) {
        auto wire = encrypt->encrypt(encode_mrp_frame(true, 17, mrp_frame_payload(message)));
        if (corrupt_tag) {
            wire.back() ^= 1;
        }
        incoming.insert(incoming.end(), wire.begin(), wire.end());
    }
    void accept(const std::uint8_t* data, std::size_t size) {
        const auto plain = decrypt->feed(Bytes(data, data + size));
        pending.insert(pending.end(), plain.begin(), plain.end());
        while (auto frame = take_mrp_frame(pending)) {
            if (!frame->sync) {
                ++acknowledgements;
                continue;
            }
            sender_sequences.push_back(frame->sequence);
            const auto batch = decode_mrp_batch(mrp_frame_protobufs(frame->payload));
            for (const auto& message : batch) {
                types.push_back(message.type);
                if (message.type == mrp::connection_state) {
                    continue;
                }
                if (silent) {
                    continue;
                }
                if (message.type == mrp::device_info && missing_device_payload) {
                    Bytes reply;
                    pb::integer(reply, 1, 15);
                    pb::data(reply, 2, message.identifier);
                    push(reply);
                    continue;
                }
                auto type = message.type;
                if (type == mrp::updates_config || type == mrp::heartbeat) {
                    type = 0;
                }
                Bytes payload;
                if (message.type == mrp::send_command) {
                    const auto fields = pb::decode(pb::view(message.payload));
                    const auto* path = pb::find(fields, 3, 2);
                    command_paths.emplace_back(path->data.begin(), path->data.end());
                    type = mrp::command_result;
                    payload = decode_mrp(pb::view(result)).payload;
                    if (reject) {
                        payload.clear();
                        pb::integer(payload, 1, 10);
                    }
                    if (malformed_result) {
                        payload = {0x80}; // Truncated extension field, not a rejection.
                    }
                }
                push(encode_mrp(wrong_type ? mrp::heartbeat : type,
                                wrong_identifier ? "unrelated" : message.identifier,
                                "synthetic-peer", payload));
                if (message.type == mrp::updates_config) {
                    push(state);
                }
            }
        }
    }
};
class PeerStream final : public ReceiverStream {
public:
    explicit PeerStream(std::shared_ptr<Peer> peer) : peer_(std::move(peer)) {}
    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override {
        operation.check();
        std::lock_guard<std::mutex> lock(peer_->mutex);
        const auto count = std::min(size, std::size_t{7});
        peer_->accept(data, count);
        return count;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        for (;;) {
            operation.check();
            {
                std::lock_guard<std::mutex> lock(peer_->mutex);
                if (!peer_->incoming.empty()) {
                    const auto count =
                        std::min({capacity, peer_->incoming.size(), std::size_t{13}});
                    std::copy_n(peer_->incoming.begin(), count, data);
                    peer_->incoming.erase(peer_->incoming.begin(),
                                          peer_->incoming.begin() +
                                              static_cast<std::ptrdiff_t>(count));
                    return count;
                }
                if (peer_->eof) {
                    return 0;
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
                    if (!peer_->incoming.empty() || peer_->eof) {
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
    std::shared_ptr<Peer> peer_;
};
std::unique_ptr<MrpChannel> channel(std::shared_ptr<Peer> peer) {
    Secret32 write, read;
    write.bytes.fill(0x11);
    read.bytes.fill(0x22);
    return std::make_unique<MrpChannel>(std::make_unique<PeerStream>(std::move(peer)), write, read);
}
void session_success(const std::string& root) {
    group = "encrypted MRP session";
    auto peer = std::make_shared<Peer>(root);
    MrpSession session(channel(peer), 1s, 100ms);
    session.expect_item("synthetic-item", "http://192.0.2.10/synthetic.mp4");
    session.handshake(SenderIdentity{}, text("synthetic-controller"));
    check(eventually([&] { return session.status().owned; }),
          "subscription receives owned playback state");
    session.command(PlaybackCommand::pause);
    session.command(PlaybackCommand::play);
    session.command(PlaybackCommand::seek, 30.5);
    check(eventually([&] { return session.status().heartbeats >= 1; }),
          "correlated heartbeat acknowledged");
    {
        std::lock_guard<std::mutex> lock(peer->mutex);
        peer->reject = true;
    }
    try {
        session.command(PlaybackCommand::stop);
        check(false, "rejected stop accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::rejected, "command result rejection category");
    }
    check(!session.failed(), "command rejection does not invent a terminal network failure");
    session.stop();
    session.stop();
    std::lock_guard<std::mutex> lock(peer->mutex);
    check(peer->closed && peer->acknowledgements >= 4, "sync replies and ordered close");
    check(!peer->sender_sequences.empty() &&
              std::all_of(peer->sender_sequences.begin(), peer->sender_sequences.end(),
                          [&](std::uint64_t sequence) {
                              return sequence == peer->sender_sequences.front();
                          }),
          "data-frame sequence is fixed per channel, independently of advancing authenticated "
          "records");
    check(peer->types.size() >= 6 && peer->types[0] == 15 && peer->types[1] == 38 &&
              peer->types[2] == 16,
          "device info first, connection, then subscription");
    check(std::all_of(peer->command_paths.begin(), peer->command_paths.end(),
                      [&](const Bytes& path) { return path == load(root, "path"); }),
          "every command has our explicit player path");
}
void failures_and_cancel(const std::string& root) {
    group = "failure and cancellation";
    for (unsigned fault = 0; fault < 5; ++fault) {
        auto peer = std::make_shared<Peer>(root);
        if (fault == 0) {
            peer->silent = true;
        }
        if (fault == 1) {
            peer->wrong_type = true;
        }
        if (fault == 2) {
            peer->wrong_identifier = true;
        }
        if (fault == 3) {
            peer->corrupt_tag = true;
        }
        if (fault == 4) {
            peer->eof = true;
            peer->silent = true;
        }
        MrpSession session(channel(peer), 80ms, 1s);
        try {
            session.handshake(SenderIdentity{}, text("synthetic"));
            check(false, "fault accepted index " + std::to_string(fault));
        } catch (const MrpException& error) {
            const auto expected = fault == 0 || fault == 2 ? MrpError::timeout
                                  : fault == 1             ? MrpError::malformed
                                  : fault == 3             ? MrpError::authentication
                                                           : MrpError::disconnected;
            check(error.reason() == expected, "fault category index " + std::to_string(fault));
        }
        session.stop();
        std::lock_guard<std::mutex> lock(peer->mutex);
        check(peer->closed, "fault closes stream index " + std::to_string(fault));
    }
    auto peer = std::make_shared<Peer>(root);
    peer->silent = true;
    MrpSession session(channel(peer), 1s, 1s);
    std::atomic_bool cancelled{true};
    try {
        session.handshake(SenderIdentity{}, text("synthetic"), &cancelled);
        check(false, "cancellation accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::cancelled, "cancellation category");
    }
    session.stop();
    auto other = std::make_shared<Peer>(root);
    MrpSession unowned(channel(other), 1s, 1s);
    try {
        unowned.command(PlaybackCommand::pause);
        check(false, "unowned pause accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::not_owned, "unowned command refusal");
    }
}

void malformed_response_contracts(const std::string& root) {
    group = "missing device info payload";
    auto peer = std::make_shared<Peer>(root);
    peer->missing_device_payload = true;
    MrpSession handshake(channel(peer), 1s, 1s);
    try {
        handshake.handshake(SenderIdentity{}, text("synthetic"));
        check(false, "correct type and correlation without device payload accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::malformed && handshake.failed(),
              "missing required device extension terminates session");
    }
    handshake.stop();

    group = "malformed command result";
    peer = std::make_shared<Peer>(root);
    peer->malformed_result = true;
    MrpSession command(channel(peer), 1s, 1s);
    command.expect_item("synthetic-item", "http://192.0.2.10/synthetic.mp4");
    command.handshake(SenderIdentity{}, text("synthetic"));
    check(eventually([&] { return command.status().owned; }), "owned state received");
    try {
        command.command(PlaybackCommand::pause);
        check(false, "malformed correlated command extension accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::malformed && command.failed(),
              "malformed result maps to terminal protocol error");
    }
    command.stop();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Fixture directory required");
        }
        fixtures(argv[1]);
        malformed_inputs();
        large_data_records(argv[1]);
        ownership(argv[1]);
        startup_binding(argv[1]);
        session_success(argv[1]);
        failures_and_cancel(argv[1]);
        malformed_response_contracts(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: " << error.what() << '\n';
        ++failures;
    }
    return failures ? 1 : 0;
}
