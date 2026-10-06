// SPDX-License-Identifier: Apache-2.0
// Session message builders and parsers against plistlib fixtures
// (fixtures/generate_session.py). Identifiers are public synthetic values.
#include "session_messages.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;

namespace {
int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}
Bytes unhex(const std::string& text) {
    Bytes output;
    for (std::size_t offset = 0; offset + 1 < text.size(); offset += 2) {
        output.push_back(
            static_cast<std::uint8_t>(std::stoul(text.substr(offset, 2), nullptr, 16)));
    }
    return output;
}
Bytes fixture(const std::string& directory, const std::string& name) {
    std::ifstream input(directory + '/' + name + ".hex");
    std::string hex;
    if (!(input >> hex)) {
        throw std::runtime_error("Missing session fixture: " + name);
    }
    return unhex(hex);
}
void check_bytes(const std::string& scenario, const Bytes& actual, const Bytes& expected) {
    if (actual == expected) {
        return;
    }
    std::size_t offset = 0;
    while (offset < actual.size() && offset < expected.size() &&
           actual[offset] == expected[offset]) {
        ++offset;
    }
    check(false, scenario + ": differs at byte " + std::to_string(offset) + " (" +
                     std::to_string(actual.size()) + " vs " + std::to_string(expected.size()) +
                     " bytes)");
}
template <typename Action> void expect_invalid(const std::string& scenario, Action action) {
    try {
        action();
        check(false, scenario + ": expected invalid_message");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::invalid_message,
              scenario + ": category " + std::to_string(static_cast<int>(error.reason())));
    }
}

// The fixture generator's synthetic values.
constexpr const char* device_id = "02:11:22:33:44:55";
constexpr const char* session_uuid = "00000000-0000-4000-8000-0000000000A1";
constexpr const char* correlation_uuid = "00000000-0000-4000-8000-0000000000A2";
constexpr const char* client_uuid = "00000000-0000-4000-8000-0000000000A3";
constexpr const char* channel_uuid = "00000000-0000-4000-8000-0000000000A4";
constexpr const char* item_uuid = "00000000-0000-4000-8000-0000000000A5";
constexpr std::uint16_t timing_port = 49152;
constexpr std::uint64_t data_stream_seed = 0x0123456789ABCDEF;
constexpr const char* media_url = "http://192.0.2.10:49153/synthetic-token/media";

SenderIdentity fixture_identity() {
    SenderIdentity identity;
    identity.device_id = device_id;
    return identity; // Reference model/OS values and this project's name.
}

void setup_body_tests(const std::string& directory) {
    group = "SETUP bodies";
    const auto identity = fixture_identity();
    check_bytes("base SETUP",
                base_setup_body(identity, session_uuid, correlation_uuid, timing_port),
                fixture(directory, "base-setup"));
    check_bytes("remote-control SETUP", remote_control_setup_body(identity, session_uuid),
                fixture(directory, "remote-control-setup"));
    check_bytes("URL stream SETUP", url_stream_setup_body(identity, client_uuid),
                fixture(directory, "url-stream-setup"));
    check_bytes("data stream SETUP",
                data_stream_setup_body(channel_uuid, client_uuid, data_stream_seed),
                fixture(directory, "data-stream-setup"));
    try {
        (void)data_stream_setup_body(channel_uuid, client_uuid, std::uint64_t{1} << 63);
        check(false, "seed of 2^63 rejected");
    } catch (const std::invalid_argument&) {
    }
}

void command_tests(const std::string& directory) {
    group = "/command bodies";
    check_bytes("insertPlayQueueItem",
                command_body(insert_play_queue_item(item_uuid, media_url, 0.0)),
                fixture(directory, "command-insert"));
    check_bytes("setProperty isInterestedInDateRange",
                command_body(set_interested_in_date_range(item_uuid)),
                fixture(directory, "command-date-range"));
    check_bytes("setProperty actionAtItemEnd", command_body(set_action_at_item_end()),
                fixture(directory, "command-item-end"));
    check_bytes("setRate", command_body(set_rate(1.0)), fixture(directory, "command-rate"));
}

void header_tests() {
    group = "request headers";
    SessionHeaders headers;
    headers.dacp_id = "0123456789ABCDEF";
    headers.active_remote = 42;
    check(headers.rtsp(false) == ReceiverHeaders{{"DACP-ID", "0123456789ABCDEF"},
                                                 {"Active-Remote", "42"},
                                                 {"Client-Instance", "0123456789ABCDEF"},
                                                 {"User-Agent", "AirPlay/550.10"}},
          "RTSP headers without a body");
    check(headers.command(session_uuid, 7) ==
              ReceiverHeaders{{"DACP-ID", "0123456789ABCDEF"},
                              {"Active-Remote", "42"},
                              {"Client-Instance", "0123456789ABCDEF"},
                              {"User-Agent", "AirPlay/870.14.1"},
                              {"Content-Type", "application/x-apple-binary-plist"},
                              {"X-Apple-ProtocolVersion", "1"},
                              {"X-Apple-Session-ID", session_uuid},
                              {"X-Apple-StreamID", "7"}},
          "/command headers");
}

void response_tests(const std::string& directory) {
    group = "SETUP responses";
    check(parse_event_port(fixture(directory, "base-setup-response")) == 49213, "eventPort");
    const auto url_stream = parse_stream_setup(fixture(directory, "url-stream-setup-response"));
    check(url_stream.stream_id == 1 && !url_stream.data_port, "URL stream ID, no data port");
    const auto data_stream = parse_stream_setup(fixture(directory, "data-stream-setup-response"));
    check(data_stream.stream_id == 2 && data_stream.data_port == 49214, "data stream port");

    auto body = [](PlistValue value) { return encode_binary_plist(value); };
    expect_invalid("not a plist", [] { (void)parse_event_port(Bytes{1, 2, 3}); });
    expect_invalid("array root", [&] { (void)parse_event_port(body(PlistArray{})); });
    expect_invalid("missing eventPort", [&] { (void)parse_event_port(body(PlistDictionary{})); });
    for (const std::int64_t port : {std::int64_t{0}, std::int64_t{65536}, std::int64_t{-1}}) {
        expect_invalid("eventPort " + std::to_string(port),
                       [&] { (void)parse_event_port(body(PlistDictionary{{"eventPort", port}})); });
    }
    expect_invalid("string eventPort",
                   [&] { (void)parse_event_port(body(PlistDictionary{{"eventPort", "49213"}})); });
    const PlistValue stream = PlistDictionary{{"type", 130}, {"streamID", 1}};
    expect_invalid("no streams", [&] {
        (void)parse_stream_setup(body(PlistDictionary{{"streams", PlistArray{}}}));
    });
    expect_invalid("two streams", [&] {
        (void)parse_stream_setup(body(PlistDictionary{{"streams", PlistArray{stream, stream}}}));
    });
    expect_invalid("missing streamID", [&] {
        (void)parse_stream_setup(
            body(PlistDictionary{{"streams", PlistArray{PlistDictionary{{"type", 130}}}}}));
    });
    expect_invalid("dataPort out of range", [&] {
        (void)parse_stream_setup(body(PlistDictionary{
            {"streams", PlistArray{PlistDictionary{{"streamID", 2}, {"dataPort", 70000}}}}}));
    });
}

void event_tests(const std::string& directory) {
    group = "receiver events";
    const auto playing = parse_session_event(fixture(directory, "event-state-params"));
    check(playing.type == "playbackState" && playing.playback_state == "playing",
          "params.playbackState, lower-cased");
    const auto loading = parse_session_event(fixture(directory, "event-state-name"));
    check(loading.playback_state == "loading", "state from the name field");
    const auto notification = parse_session_event(fixture(directory, "event-notification"));
    check(notification.type == "notification" && !notification.playback_state,
          "other event types carry no state");
    const auto bare = parse_session_event(fixture(directory, "event-bare"));
    check(bare.type == "updateInfo" && !bare.playback_state, "bare event without envelope");

    auto envelope = [](PlistValue inner) {
        return encode_binary_plist(
            PlistDictionary{{"params", PlistDictionary{{"data", encode_binary_plist(inner)}}}});
    };
    // Diagnostic outlines carry key names and the state, never other values.
    check(describe_event_structure(fixture(directory, "event-state-params")) ==
              "type=playbackState state=playing keys=type,params,params.playbackState",
          "event outline");
    check(describe_event_structure(fixture(directory, "event-notification")) ==
              "type=notification keys=type,params,params.kind",
          "notification outline omits the value");

    expect_invalid("neither envelope nor type", [] {
        (void)parse_session_event(encode_binary_plist(PlistDictionary{{"data", Bytes{}}}));
    });
    expect_invalid("params.data not data", [] {
        (void)parse_session_event(
            encode_binary_plist(PlistDictionary{{"params", PlistDictionary{{"data", "text"}}}}));
    });
    expect_invalid("inner plist is not a dictionary",
                   [&] { (void)parse_session_event(envelope(PlistArray{})); });
    expect_invalid("missing type",
                   [&] { (void)parse_session_event(envelope(PlistDictionary{{"name", "x"}})); });
    expect_invalid("playbackState without a state", [&] {
        (void)parse_session_event(envelope(PlistDictionary{{"type", "playbackState"}}));
    });
}

bool upper_hex(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
}
int hex_value(char ch) {
    return ch <= '9' ? ch - '0' : ch - 'A' + 10;
}

void random_identifier_tests() {
    group = "random identifiers";
    std::set<std::string> device_ids;
    std::set<std::string> uuids;
    for (int sample = 0; sample < 64; ++sample) {
        const auto id = random_device_id();
        bool shape = id.size() == 17;
        for (std::size_t index = 0; shape && index < id.size(); ++index) {
            shape = index % 3 == 2 ? id[index] == ':' : upper_hex(id[index]);
        }
        // Second hex digit of the first byte: bit 1 set (local), bit 0 clear (unicast).
        check(shape && (hex_value(id[1]) & 0x3) == 0x2,
              "device ID shape, sample " + std::to_string(sample));
        device_ids.insert(id);

        const auto uuid = random_uuid();
        bool uuid_shape = uuid.size() == 36;
        for (std::size_t index = 0; uuid_shape && index < uuid.size(); ++index) {
            const bool hyphen = index == 8 || index == 13 || index == 18 || index == 23;
            uuid_shape = hyphen ? uuid[index] == '-' : upper_hex(uuid[index]);
        }
        check(uuid_shape && uuid[14] == '4' &&
                  std::string_view("89AB").find(uuid[19]) != std::string_view::npos,
              "UUID version 4 shape, sample " + std::to_string(sample));
        uuids.insert(uuid);

        check(random_stream_seed() < (std::uint64_t{1} << 63), "seed below 2^63");
    }
    check(device_ids.size() == 64 && uuids.size() == 64, "identifiers do not repeat");
    const auto headers = SessionHeaders::random();
    check(headers.dacp_id.size() == 16 &&
              std::all_of(headers.dacp_id.begin(), headers.dacp_id.end(), upper_hex),
          "DACP-ID is 16 uppercase hex digits");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected session fixture directory");
        }
        const std::string directory = argv[1];
        setup_body_tests(directory);
        command_tests(directory);
        header_tests();
        response_tests(directory);
        event_tests(directory);
        random_identifier_tests();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " session message test failure(s)\n";
        return 1;
    }
    std::cout << "session message tests passed\n";
    return 0;
}
