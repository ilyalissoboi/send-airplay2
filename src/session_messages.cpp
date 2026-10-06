// SPDX-License-Identifier: Apache-2.0
// Message shapes and constants follow the reference sender (pyatv 0.18.0 and
// robkochman/pyatv@8144c77c, MIT); no source is incorporated. See dependencies.md.
#include "session_messages.h"
#include "control_crypto.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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

/// The inner command of a {"params": {"data": <plist>}} envelope.
PlistValue unwrap_envelope(const Bytes& body) {
    const auto envelope = decode_body(body);
    if (envelope.kind() != PlistKind::dictionary) {
        invalid_body();
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
    return output;
}
} // namespace send_airplay2::detail
