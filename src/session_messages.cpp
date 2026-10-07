// SPDX-License-Identifier: Apache-2.0
// Message shapes and constants follow the reference sender (pyatv 0.18.0 and
// robkochman/pyatv@8144c77c, MIT); no source is incorporated. See dependencies.md.
#include "session_messages.h"
#include "control_crypto.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
[[noreturn]] void invalid_body() {
    throw TransportException(TransportError::invalid_message);
}

std::string hex_digits(const std::uint8_t* data, std::size_t size, const char* separator) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    for (std::size_t index = 0; index < size; ++index) {
        if (index != 0) {
            output += separator;
        }
        output += digits[data[index] >> 4];
        output += digits[data[index] & 0x0f];
    }
    return output;
}

/// A copy with every dictionary's keys in byte order, at every depth, as
/// plistlib's sort_keys=True produces. Data payloads are left untouched.
PlistValue with_sorted_keys(const PlistValue& value) {
    if (value.kind() == PlistKind::array) {
        PlistArray elements;
        for (const auto& element : value.as_array()) {
            elements.push_back(with_sorted_keys(element));
        }
        return elements;
    }
    if (value.kind() != PlistKind::dictionary) {
        return value;
    }
    PlistDictionary entries;
    for (const auto& entry : value.as_dictionary()) {
        entries.push_back({entry.key, with_sorted_keys(entry.value)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const PlistEntry& left, const PlistEntry& right) { return left.key < right.key; });
    return entries;
}

/// RTSP bodies: the reference serializes dicts with sorted keys (byte order).
Bytes rtsp_plist(PlistDictionary entries) {
    return encode_binary_plist(with_sorted_keys(PlistValue(std::move(entries))));
}

/// Decode a receiver plist body, mapping codec errors to a transport category.
PlistValue decode_body(const Bytes& body) {
    try {
        return decode_binary_plist(body);
    } catch (const std::invalid_argument&) {
        invalid_body();
    }
}

const PlistValue& require(const PlistValue& container, const char* key, PlistKind kind) {
    const auto* value = container.find(key);
    if (value == nullptr || value->kind() != kind) {
        invalid_body();
    }
    return *value;
}

std::uint16_t require_port(const PlistValue& container, const char* key) {
    const auto port = require(container, key, PlistKind::integer).as_integer();
    if (port < 1 || port > std::numeric_limits<std::uint16_t>::max()) {
        invalid_body();
    }
    return static_cast<std::uint16_t>(port);
}

/// The inner command of a {"params": {"data": <plist>}} envelope. Receiver
/// events may also arrive as a bare dictionary with a "type" and no envelope.
PlistValue unwrap_envelope(const Bytes& body) {
    auto envelope = decode_body(body);
    if (envelope.kind() != PlistKind::dictionary) {
        invalid_body();
    }
    const auto* wrapped = envelope.find("params");
    const auto* data = wrapped != nullptr ? wrapped->find("data") : nullptr;
    if (data == nullptr && envelope.find("type") != nullptr) {
        return envelope;
    }
    const auto& params = require(envelope, "params", PlistKind::dictionary);
    const auto inner = decode_body(require(params, "data", PlistKind::data).as_data());
    if (inner.kind() != PlistKind::dictionary) {
        invalid_body();
    }
    return inner;
}

std::string lower_ascii(std::string text) {
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}
} // namespace

std::string random_device_id() {
    std::array<std::uint8_t, 6> bytes{};
    public_random_bytes(bytes.data(), bytes.size());
    // Locally administered (bit 1 set), unicast (bit 0 clear): never a vendor MAC.
    bytes[0] = static_cast<std::uint8_t>((bytes[0] & 0xfc) | 0x02);
    return hex_digits(bytes.data(), bytes.size(), ":");
}

std::string random_uuid() {
    std::array<std::uint8_t, 16> bytes{};
    public_random_bytes(bytes.data(), bytes.size());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x40); // Version 4.
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80); // RFC 4122 variant.
    return hex_digits(bytes.data(), 4, "") + "-" + hex_digits(bytes.data() + 4, 2, "") + "-" +
           hex_digits(bytes.data() + 6, 2, "") + "-" + hex_digits(bytes.data() + 8, 2, "") + "-" +
           hex_digits(bytes.data() + 10, 6, "");
}

std::uint64_t random_stream_seed() {
    std::array<std::uint8_t, 8> bytes{};
    public_random_bytes(bytes.data(), bytes.size());
    bytes[0] &= 0x7f; // Below 2^63.
    std::uint64_t seed = 0;
    for (const auto byte : bytes) {
        seed = (seed << 8) | byte;
    }
    return seed;
}

SessionHeaders SessionHeaders::random() {
    std::array<std::uint8_t, 8> dacp{};
    public_random_bytes(dacp.data(), dacp.size());
    std::array<std::uint8_t, 4> remote{};
    public_random_bytes(remote.data(), remote.size());
    SessionHeaders headers;
    headers.dacp_id = hex_digits(dacp.data(), dacp.size(), "");
    for (const auto byte : remote) {
        headers.active_remote = (headers.active_remote << 8) | byte;
    }
    return headers;
}

ReceiverHeaders SessionHeaders::rtsp(bool plist_body) const {
    ReceiverHeaders headers{{"DACP-ID", dacp_id},
                            {"Active-Remote", std::to_string(active_remote)},
                            {"Client-Instance", dacp_id},
                            {"User-Agent", session_protocol::rtsp_user_agent}};
    if (plist_body) {
        headers.emplace_back("Content-Type", session_protocol::plist_content_type);
    }
    return headers;
}

ReceiverHeaders SessionHeaders::command(const std::string& session_uuid,
                                        std::int64_t stream_id) const {
    auto headers = rtsp(true);
    for (auto& field : headers) {
        if (field.first == "User-Agent") {
            field.second = session_protocol::command_user_agent;
        }
    }
    headers.emplace_back("X-Apple-ProtocolVersion", "1");
    headers.emplace_back("X-Apple-Session-ID", session_uuid);
    headers.emplace_back("X-Apple-StreamID", std::to_string(stream_id));
    return headers;
}

Bytes base_setup_body(const SenderIdentity& identity, const std::string& session_uuid,
                      const std::string& correlation_uuid, std::uint16_t timing_port) {
    return rtsp_plist({
        {"deviceID", identity.device_id},
        {"sessionUUID", session_uuid},
        {"sessionCorrelationUUID", correlation_uuid},
        {"timingPort", static_cast<std::int64_t>(timing_port)},
        {"timingProtocol", "NTP"},
        {"isMultiSelectAirPlay", true},
        {"groupContainsGroupLeader", false},
        {"macAddress", identity.device_id},
        {"model", identity.model},
        {"name", identity.name},
        {"osBuildVersion", identity.os_build_version},
        {"osName", identity.os_name},
        {"osVersion", identity.os_version},
        {"senderSupportsRelay", false},
        {"sourceVersion", identity.source_version},
        {"statsCollectionEnabled", false},
    });
}

Bytes remote_control_setup_body(const SenderIdentity& identity, const std::string& session_uuid) {
    return rtsp_plist({
        {"isRemoteControlOnly", true},
        {"osName", identity.os_name},
        {"sourceVersion", session_protocol::remote_control_source_version},
        {"timingProtocol", "None"},
        {"model", identity.model},
        {"deviceID", identity.device_id},
        {"osVersion", identity.os_version},
        {"osBuildVersion", identity.os_build_version},
        {"macAddress", identity.device_id},
        {"sessionUUID", session_uuid},
        {"name", identity.name},
    });
}

Bytes url_stream_setup_body(const SenderIdentity& identity, const std::string& client_uuid) {
    PlistDictionary stream{
        {"clientUUID", client_uuid},
        {"clientTypeUUID", session_protocol::url_stream_client_type},
        {"channelID", identity.device_id + "-RCS-1"},
        {"controlType", session_protocol::url_control_type},
        {"type", session_protocol::stream_type},
    };
    return rtsp_plist({{"streams", PlistArray{PlistValue(std::move(stream))}}});
}

Bytes data_stream_setup_body(const std::string& channel_id, const std::string& client_uuid,
                             std::uint64_t seed) {
    if (seed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("Data stream seed must be below 2^63");
    }
    PlistDictionary stream{
        {"controlType", session_protocol::data_control_type},
        {"channelID", channel_id},
        {"seed", static_cast<std::int64_t>(seed)},
        {"clientUUID", client_uuid},
        {"type", session_protocol::stream_type},
        {"wantsDedicatedSocket", true},
        {"clientTypeUUID", session_protocol::data_stream_client_type},
    };
    return rtsp_plist({{"streams", PlistArray{PlistValue(std::move(stream))}}});
}

std::uint16_t parse_event_port(const Bytes& response_body) {
    const auto response = decode_body(response_body);
    if (response.kind() != PlistKind::dictionary) {
        invalid_body();
    }
    return require_port(response, "eventPort");
}

StreamSetup parse_stream_setup(const Bytes& response_body) {
    const auto response = decode_body(response_body);
    if (response.kind() != PlistKind::dictionary) {
        invalid_body();
    }
    const auto& streams = require(response, "streams", PlistKind::array).as_array();
    if (streams.size() != 1 || streams.front().kind() != PlistKind::dictionary) {
        invalid_body();
    }
    const auto& stream = streams.front();
    StreamSetup setup;
    setup.stream_id = require(stream, "streamID", PlistKind::integer).as_integer();
    if (stream.find("dataPort") != nullptr) {
        setup.data_port = require_port(stream, "dataPort");
    }
    return setup;
}

Bytes command_body(const PlistValue& command) {
    return rtsp_plist({{"params", PlistDictionary{{"data", encode_binary_plist(command)}}}});
}

PlistValue insert_play_queue_item(const std::string& item_uuid, const std::string& url,
                                  double start_seconds) {
    return PlistDictionary{
        {"type", "insertPlayQueueItem"},
        {"item",
         PlistDictionary{
             {"uuid", item_uuid},
             {"mediaType", "file"},
             {"Content-Location", url},
             {"Start-Position-Seconds", start_seconds},
         }},
    };
}

PlistValue set_interested_in_date_range(const std::string& item_uuid) {
    return PlistDictionary{
        {"type", "setProperty"},
        {"value", true},
        {"property", "isInterestedInDateRange"},
        {"item", PlistDictionary{{"uuid", item_uuid}}},
    };
}

PlistValue set_action_at_item_end() {
    return PlistDictionary{{"type", "setProperty"}, {"value", 1}, {"property", "actionAtItemEnd"}};
}

PlistValue set_rate(double rate) {
    return PlistDictionary{{"type", "setRate"}, {"rate", rate}};
}

namespace {
constexpr std::size_t max_described_paths = 48;
constexpr std::size_t max_described_depth = 4;
constexpr std::int64_t time_valid = 1, time_rounded = 2;

/// URL events can represent seconds as a number or as a CMTime dictionary.
/// A rational duration must have a positive timescale and a valid numeric
/// flag (bit 0); rounded values (bit 1) are allowed, infinities/indefinite are not.
std::optional<double> duration_seconds(const PlistValue& duration, bool allow_zero = false) {
    double seconds = -1;
    if (duration.kind() == PlistKind::real) {
        seconds = duration.as_real();
    } else if (duration.kind() == PlistKind::integer) {
        seconds = static_cast<double>(duration.as_integer());
    } else if (duration.kind() == PlistKind::dictionary) {
        const auto* value = duration.find("value");
        const auto* scale = duration.find("timescale");
        const auto* flags = duration.find("flags");
        if (!value || !scale || !flags || value->kind() != PlistKind::integer ||
            scale->kind() != PlistKind::integer || flags->kind() != PlistKind::integer ||
            scale->as_integer() <= 0 ||
            scale->as_integer() > std::numeric_limits<std::int32_t>::max() ||
            (flags->as_integer() != time_valid &&
             flags->as_integer() != (time_valid | time_rounded))) {
            return {};
        }
        seconds =
            static_cast<double>(value->as_integer()) / static_cast<double>(scale->as_integer());
    }
    return std::isfinite(seconds) && (seconds > 0 || (allow_zero && seconds == 0))
               ? std::optional<double>{seconds}
               : std::nullopt;
}

void describe_buffering_container(const PlistValue& container, const char* prefix,
                                  std::ostringstream& output) {
    constexpr double max_diagnostic_seconds = 1e9;
    for (const auto* key : {"rate", "position", "duration"}) {
        const auto* value = container.find(key);
        if (!value) {
            continue;
        }
        const auto number = duration_seconds(*value, true);
        if (number && *number <= max_diagnostic_seconds) {
            output << ' ' << prefix << key << '=' << *number;
        }
    }
    if (const auto* ready = container.find("readyToPlay");
        ready && ready->kind() == PlistKind::boolean) {
        output << ' ' << prefix << "readyToPlay=" << (ready->as_boolean() ? "yes" : "no");
    }
    if (const auto* stalls = container.find("stallCount");
        stalls && stalls->kind() == PlistKind::integer && stalls->as_integer() >= 0 &&
        stalls->as_integer() <= std::numeric_limits<std::uint32_t>::max()) {
        output << ' ' << prefix << "stallCount=" << stalls->as_integer();
    }
}

void collect_key_paths(const PlistValue& value, const std::string& prefix, std::size_t depth,
                       std::vector<std::string>& paths) {
    if (depth >= max_described_depth || paths.size() >= max_described_paths) {
        return;
    }
    if (value.kind() == PlistKind::array) {
        // Describe the first element's shape; arrays are usually homogeneous.
        if (!value.as_array().empty()) {
            collect_key_paths(value.as_array().front(), prefix + "[]", depth + 1, paths);
        }
        return;
    }
    if (value.kind() != PlistKind::dictionary) {
        return;
    }
    for (const auto& entry : value.as_dictionary()) {
        if (paths.size() >= max_described_paths) {
            return;
        }
        const auto path = prefix.empty() ? entry.key : prefix + "." + entry.key;
        paths.push_back(entry.value.kind() == PlistKind::array ? path + "[]" : path);
        collect_key_paths(entry.value, path, depth + 1, paths);
    }
}
} // namespace

std::string describe_event_structure(const Bytes& body) {
    const auto event = unwrap_envelope(body);
    const auto parsed = parse_session_event(body);
    std::string output = "type=" + parsed.type;
    if (parsed.playback_state) {
        output += " state=" + *parsed.playback_state;
    }
    std::vector<std::string> paths;
    collect_key_paths(event, "", 0, paths);
    output += " keys=";
    for (std::size_t index = 0; index < paths.size(); ++index) {
        output += (index == 0 ? "" : ",") + paths[index];
    }
    return output;
}

std::string describe_buffering_values(const Bytes& body) {
    const auto event = unwrap_envelope(body);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(3);
    describe_buffering_container(event, "root.", output);
    if (const auto* params = event.find("params")) {
        describe_buffering_container(*params, "params.", output);
    }
    if (const auto* value = event.find("value")) {
        describe_buffering_container(*value, "value.", output);
    }
    auto text = output.str();
    return text.empty() ? text : "buffer" + text;
}

std::string describe_remote_event(const Bytes& body) {
    auto event = decode_body(body);
    if (event.kind() != PlistKind::dictionary) {
        invalid_body();
    }
    if (const auto* params = event.find("params")) {
        if (const auto* data = params->find("data"); data && data->kind() == PlistKind::data) {
            // Finish decoding before replacing the dictionary that owns data.
            auto inner = decode_body(data->as_data());
            if (inner.kind() != PlistKind::dictionary) {
                invalid_body();
            }
            event = std::move(inner);
        }
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "remote type=";
    const auto* type = event.find("type");
    const char* label = type ? "other" : "none";
    if (type && type->kind() == PlistKind::string) {
        for (const auto* known : {"playbackState", "updateInfo", "notification", "didPlayToEndTime",
                                  "playbackEnded", "stop"}) {
            if (type->as_string() == known) {
                label = known;
                break;
            }
        }
    }
    output << label;
    const auto* params = event.find("params");
    const auto* state = params ? params->find("playbackState") : nullptr;
    if (!state) {
        state = event.find("name");
    }
    const char* state_label = state ? "other" : "none";
    if (state && state->kind() == PlistKind::string) {
        const auto normalized = lower_ascii(state->as_string());
        for (const auto* known : {"loading", "playing", "paused", "idle", "stopped", "ended"}) {
            if (normalized == known) {
                state_label = known;
                break;
            }
        }
    }
    output << " state=" << state_label;
    // Fixed scopes and a fixed key list bound output independently of peer input.
    for (const auto* scope : {"root", "params", "value"}) {
        const auto* container = std::string_view(scope) == "root" ? &event : event.find(scope);
        output << ' ' << scope << "_keys=";
        bool first = true;
        for (const auto* key : {"type", "name", "params", "value", "data", "playbackState",
                                "reason", "error", "status", "rate", "duration"}) {
            if (container && container->find(key)) {
                output << (first ? "" : ",") << key;
                first = false;
            }
        }
        if (first) {
            output << "none";
        }
        for (const auto* key : {"reason", "error", "status"}) {
            const auto* code = container ? container->find(key) : nullptr;
            if (code && code->kind() == PlistKind::integer &&
                code->as_integer() >= std::numeric_limits<std::int32_t>::min() &&
                code->as_integer() <= std::numeric_limits<std::int32_t>::max()) {
                output << ' ' << scope << '.' << key << '=' << code->as_integer();
            }
        }
    }
    return output.str();
}

SessionEvent parse_session_event(const Bytes& body) {
    const auto event = unwrap_envelope(body);
    SessionEvent output;
    output.type = require(event, "type", PlistKind::string).as_string();
    if (output.type != "playbackState") {
        return output;
    }
    // The state is in params.playbackState, or in "name" on some messages.
    const auto* params = event.find("params");
    const auto* state = params != nullptr ? params->find("playbackState") : nullptr;
    if (state == nullptr) {
        state = event.find("name");
    }
    if (state == nullptr || state->kind() != PlistKind::string) {
        invalid_body();
    }
    output.playback_state = lower_ascii(state->as_string());
    if (const auto* duration = params ? params->find("duration") : nullptr) {
        output.duration_seconds = duration_seconds(*duration);
    }
    const auto* rate = params ? params->find("rate") : nullptr;
    if (!rate) {
        rate = event.find("rate");
    }
    if (rate && (rate->kind() == PlistKind::integer || rate->kind() == PlistKind::real)) {
        const auto number = rate->kind() == PlistKind::integer
                                ? static_cast<double>(rate->as_integer())
                                : rate->as_real();
        if (std::isfinite(number)) {
            output.playback_rate = number;
        }
    }
    return output;
}
} // namespace send_airplay2::detail
