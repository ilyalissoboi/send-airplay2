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
#include <limits>
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

void url_control_command_tests(const std::string& directory) {
    group = "URL control /command bodies (D62)";
    check_bytes("setRate 0 pauses", command_body(set_rate(0.0)),
                fixture(directory, "command-pause"));
    // A request with messageID 7; 12.5 s at the millisecond timescale: value
    // 12500, timescale 1000.
    check_bytes("seek request with a CMTime", command_body(seek_to(12.5, 7)),
                fixture(directory, "command-seek"));
    check_bytes("stop", command_body(stop_playback()), fixture(directory, "command-stop"));

    const auto seek_value = [](double seconds) {
        return seek_to(seconds, 1).find("time")->find("value")->as_integer();
    };
    check(seek_value(0) == 0, "seek to zero");
    check(seek_value(0.0004) == 0 && seek_value(0.0006) == 1, "seek rounds to the millisecond");
    check(seek_value(1e9) == 1'000'000'000'000, "largest seek, 1e9 s");
    for (const auto seconds : {-0.001, 1e9 + 1, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        try {
            (void)seek_to(seconds, 1);
            check(false, "seek out of range accepted: " + std::to_string(seconds));
        } catch (const std::invalid_argument&) {
        }
    }
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
    const auto timed = parse_session_event(fixture(directory, "event-state-duration"));
    check(timed.playback_state == "playing" && timed.duration_seconds == 131.6,
          "independent CMTime duration, value/timescale in seconds");
    const auto notification = parse_session_event(fixture(directory, "event-notification"));
    check(notification.type == "notification" && !notification.playback_state,
          "other event types carry no state");
    const auto bare = parse_session_event(fixture(directory, "event-bare"));
    check(bare.type == "updateInfo" && !bare.playback_state, "bare event without envelope");

    auto envelope = [](PlistValue inner) {
        return encode_binary_plist(
            PlistDictionary{{"params", PlistDictionary{{"data", encode_binary_plist(inner)}}}});
    };
    auto duration_event = [&](PlistValue duration) {
        return parse_session_event(envelope(
            PlistDictionary{{"type", "playbackState"},
                            {"params", PlistDictionary{{"playbackState", "playing"},
                                                       {"duration", std::move(duration)}}}}));
    };
    check(duration_event(30).duration_seconds == 30.0, "integer duration");
    check(duration_event(30.5).duration_seconds == 30.5, "real duration");
    for (const auto flags : {0, 4, 8, 16, -1}) {
        check(!duration_event(PlistDictionary{{"value", 100}, {"timescale", 10}, {"flags", flags}})
                   .duration_seconds,
              "invalid/non-numeric CMTime flags " + std::to_string(flags));
    }
    check(duration_event(PlistDictionary{{"value", 101}, {"timescale", 10}, {"flags", 3}})
                  .duration_seconds == 10.1,
          "rounded CMTime remains numeric");
    for (auto scale : {0, -1}) {
        check(!duration_event(PlistDictionary{{"value", 100}, {"timescale", scale}, {"flags", 1}})
                   .duration_seconds,
              "nonpositive timescale " + std::to_string(scale));
    }
    for (const auto value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        check(!duration_event(value).duration_seconds, "nonpositive/nonfinite duration");
    }
    check(!duration_event(PlistDictionary{{"value", 100}, {"flags", 1}}).duration_seconds,
          "missing timescale");
    check(!duration_event("100").duration_seconds, "text duration is not parsed");
    const auto rate_event = [&](PlistValue rate, bool root) {
        PlistDictionary fields{{"type", "playbackState"}, {"name", "Playing"}};
        if (root) {
            fields.push_back({"rate", std::move(rate)});
        } else {
            fields.push_back({"params", PlistDictionary{{"rate", std::move(rate)}}});
        }
        return parse_session_event(envelope(std::move(fields)));
    };
    check(rate_event(0, false).playback_rate == 0.0,
          "integer zero rate is explicit stationary telemetry");
    check(rate_event(1.0, true).playback_rate == 1.0, "root numeric rate supported");
    check(rate_event(-1.0, false).playback_rate == -1.0,
          "reverse rate retained without implying forward startup");
    for (const auto rate :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        check(!rate_event(rate, false).playback_rate, "nonfinite rate ignored");
    }
    check(!rate_event("private-rate", false).playback_rate,
          "rate text is not interpreted or retained");
    check(!rate_event(true, false).playback_rate, "boolean rate is not numeric");

    // macOS reports progress in playbackState params and at the root of
    // notifications (time jumps, rate changes), as CMTime dictionaries (D62).
    const auto cm_time = [](std::int64_t value) {
        return PlistDictionary{{"value", value}, {"timescale", 1000}, {"flags", 1}, {"epoch", 0}};
    };
    const auto state_with_position = parse_session_event(
        envelope(PlistDictionary{{"type", "playbackState"},
                                 {"params", PlistDictionary{{"playbackState", "playing"},
                                                            {"position", cm_time(124908)},
                                                            {"duration", cm_time(131567)},
                                                            {"rate", 1.0}}}}));
    check(state_with_position.position_seconds == 124.908 &&
              state_with_position.duration_seconds == 131.567 &&
              state_with_position.playback_rate == 1.0,
          "playbackState params position, duration and rate");
    const auto time_jump = parse_session_event(envelope(PlistDictionary{
        {"type", "notification"}, {"name", "private-name"}, {"position", cm_time(0)}}));
    check(time_jump.type == "notification" && !time_jump.playback_state &&
              time_jump.position_seconds == 0.0 && !time_jump.playback_rate,
          "notification root position, zero is valid");
    const auto rate_change = parse_session_event(envelope(
        PlistDictionary{{"type", "notification"}, {"rate", 0.0}, {"position", cm_time(60000)}}));
    check(rate_change.playback_rate == 0.0 && rate_change.position_seconds == 60.0,
          "notification root rate and position");
    check(!parse_session_event(
               envelope(PlistDictionary{{"type", "notification"}, {"position", "60"}}))
               .position_seconds,
          "text position is not parsed");
    check(!parse_session_event(
               envelope(PlistDictionary{
                   {"type", "notification"},
                   {"position", PlistDictionary{{"value", 1}, {"timescale", 1}, {"flags", 0}}}}))
               .position_seconds,
          "invalid CMTime position is unknown");
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

void url_diagnostic_redaction_tests() {
    group = "URL diagnostic redaction";
    const auto private_event = encode_binary_plist(
        PlistDictionary{{"type", "private-event\nforged-output"},
                        {"private-field", "private-value"},
                        {"params", PlistDictionary{{"private-key", "private-metadata"}}}});
    check(parse_session_event(private_event).type == "other", "unknown type becomes a fixed label");
    check(describe_event_structure(private_event) == "type=other keys=type,params",
          "unknown type text, keys and metadata never appear");
    const auto private_state =
        encode_binary_plist(PlistDictionary{{"type", "playbackState"},
                                            {"name", "PRIVATE-STATE\nforged-output"},
                                            {"private-key", "private-value"}});
    check(parse_session_event(private_state).playback_state == "other",
          "unknown state becomes a fixed label in public status");
    check(describe_event_structure(private_state) ==
              "type=playbackState state=other keys=type,name",
          "unknown state cannot inject output lines");
}
void buffering_diagnostic_tests() {
    group = "allowlisted buffering values";
    const auto diagnostics = [](PlistDictionary fields) {
        return describe_buffering_values(
            encode_binary_plist(PlistDictionary{{"type", "playbackState"},
                                                {"params", std::move(fields)},
                                                {"name", "private-player-name"},
                                                {"url", "http://private.example/media"}}));
    };
    check(diagnostics(
              {{"rate", 1.0},
               {"position", 0},
               {"duration", PlistDictionary{{"value", 1316}, {"timescale", 10}, {"flags", 1}}},
               {"readyToPlay", false},
               {"stallCount", 2},
               {"uuid", "private-id"}}) ==
              "buffer params.rate=1.000 params.position=0.000 params.duration=131.600 "
              "params.readyToPlay=no params.stallCount=2",
          "fixed numeric/boolean fields and valid CMTime; no metadata values or key names");
    check(diagnostics({{"rate", "private-rate"},
                       {"position", -1},
                       {"duration", std::numeric_limits<double>::infinity()},
                       {"readyToPlay", "private-ready"},
                       {"stallCount", -1}})
              .empty(),
          "wrong types, negative times/counters and nonfinite values are omitted");
    check(diagnostics({{"position", PlistDictionary{{"value", 10}, {"timescale", 1}, {"flags", 4}}},
                       {"rate", std::numeric_limits<double>::quiet_NaN()},
                       {"duration", 1e100},
                       {"stallCount", std::int64_t{4294967296LL}}})
              .empty(),
          "indefinite CMTime and unbounded numeric values are omitted");
    check(describe_buffering_values(encode_binary_plist(PlistDictionary{
              {"type", "private-unknown-type"},
              {"rate", 0},
              {"value", PlistDictionary{{"readyToPlay", true}, {"stallCount", 0}}}})) ==
              "buffer root.rate=0.000 value.readyToPlay=yes value.stallCount=0",
          "bare root/value dictionaries use fixed scope labels and never output event type");
}
void remote_diagnostic_tests() {
    group = "remote diagnostic redaction";
    const auto envelope = [](const PlistValue& inner) {
        return encode_binary_plist(
            PlistDictionary{{"params", PlistDictionary{{"data", encode_binary_plist(inner)}}}});
    };
    const PlistDictionary event{
        {"type", "private-type"},
        {"name", "private-state"},
        {"private-key", "private-value"},
        {"reason", std::int64_t{-2147483648LL}},
        {"error", std::int64_t{2147483647LL}},
        {"status", std::int64_t{2147483648LL}},
        {"params", PlistDictionary{{"reason", "private-reason"}, {"error", true}}}};
    const auto expected = "remote type=other state=other root_keys=type,name,params,reason,error,"
                          "status root.reason=-2147483648 root.error=2147483647 "
                          "params_keys=reason,error value_keys=none";
    check(describe_remote_event(encode_binary_plist(event)) == expected,
          "unknown strings/keys and wrong or out-of-range codes are never emitted");
    check(describe_remote_event(envelope(event)) == expected,
          "wrapped and bare remote observations have the same fixed output");
    check(describe_remote_event(encode_binary_plist(PlistDictionary{})) ==
              "remote type=none state=none root_keys=none params_keys=none value_keys=none",
          "generic remote dictionary does not require URL event type");
    expect_invalid("malformed remote plist", [] { (void)describe_remote_event(Bytes{0xff}); });
    expect_invalid("remote inner non-dictionary",
                   [&] { (void)describe_remote_event(envelope(PlistArray{})); });
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
        url_control_command_tests(directory);
        header_tests();
        response_tests(directory);
        event_tests(directory);
        url_diagnostic_redaction_tests();
        buffering_diagnostic_tests();
        remote_diagnostic_tests();
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
