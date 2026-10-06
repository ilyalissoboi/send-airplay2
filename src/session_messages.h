// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_SESSION_MESSAGES_H
#define SEND_AIRPLAY2_SESSION_MESSAGES_H

#include "binary_plist.h"
#include "receiver_http.h"
#include <cstdint>
#include <optional>
#include <string>

namespace send_airplay2::detail {
/**
 * Bodies and headers of the URL playback session, as the reference sender
 * builds them on tvOS 26.6 (pyatv 0.18.0 and the unmerged fix
 * robkochman/pyatv@8144c77c; see session-design.md and dependencies.md).
 *
 * RTSP plist bodies are serialized with keys in byte order, because the
 * reference passes dict bodies to plistlib's default sort_keys=True. /command
 * payloads nest a second plist whose keys keep the reference's own order.
 *
 * Parsers throw TransportException(invalid_message) for any body that is not
 * the expected plist shape; they never return partial results.
 */
namespace session_protocol {
constexpr const char* plist_content_type = "application/x-apple-binary-plist";
/// User-Agent of RTSP requests, and of /command requests (reference values).
constexpr const char* rtsp_user_agent = "AirPlay/550.10";
constexpr const char* command_user_agent = "AirPlay/870.14.1";
/// `sourceVersion` the reference sends in a remote-control-only SETUP.
constexpr const char* remote_control_source_version = "550.10";
/// Fixed client-type identifiers of the two type-130 stream kinds.
constexpr const char* url_stream_client_type = "A6B27562-B43A-4F2D-B75F-82391E250194";
constexpr const char* data_stream_client_type = "1910A70F-DBC0-4242-AF95-115DB30604E1";
constexpr std::int64_t stream_type = 130;
constexpr std::int64_t url_control_type = 1;  // Commands over /command.
constexpr std::int64_t data_control_type = 2; // Dedicated data-stream socket.
} // namespace session_protocol

/**
 * How the sender describes itself in SETUP (decision D30). The model and OS
 * fields default to the reference values, the only ones known to work; the
 * display name is this project's own.
 */
struct SenderIdentity {
    std::string device_id; // Also sent as macAddress; never the host's real MAC.
    std::string name = "send-airplay2";
    std::string model = "iPhone14,3";
    std::string os_name = "iPhone OS";
    std::string os_version = "16.5";
    std::string os_build_version = "20F66";
    std::string source_version = "690.7.1";
};

/// Random identifiers for one session. Device IDs are locally administered
/// unicast MAC-style values ("02:..."), UUIDs are uppercase version 4.
[[nodiscard]] std::string random_device_id();
[[nodiscard]] std::string random_uuid();
/// A data-stream seed below 2^63, so it fits the plist integer model.
[[nodiscard]] std::uint64_t random_stream_seed();

/// Request headers shared by every request of one receiver connection.
struct SessionHeaders {
    std::string dacp_id; // 64-bit value in uppercase hex; also Client-Instance.
    std::uint32_t active_remote = 0;
    [[nodiscard]] static SessionHeaders random();
    /// DACP-ID, Active-Remote, Client-Instance and the RTSP User-Agent; plus
    /// Content-Type when the request carries a plist body.
    [[nodiscard]] ReceiverHeaders rtsp(bool plist_body) const;
    /// rtsp(true) with the /command User-Agent and session/stream identifiers.
    [[nodiscard]] ReceiverHeaders command(const std::string& session_uuid,
                                          std::int64_t stream_id) const;
};

// ---- SETUP bodies ----

/// Base SETUP of the URL playback session; the response names `eventPort`.
[[nodiscard]] Bytes base_setup_body(const SenderIdentity& identity, const std::string& session_uuid,
                                    const std::string& correlation_uuid, std::uint16_t timing_port);
/// Base SETUP of the remote-control-only session (no timing).
[[nodiscard]] Bytes remote_control_setup_body(const SenderIdentity& identity,
                                              const std::string& session_uuid);
/// Type-130 control stream for URL playback; the response names `streamID`.
[[nodiscard]] Bytes url_stream_setup_body(const SenderIdentity& identity,
                                          const std::string& client_uuid);
/// Type-130 remote-control data stream; the response names `dataPort`.
[[nodiscard]] Bytes data_stream_setup_body(const std::string& channel_id,
                                           const std::string& client_uuid, std::uint64_t seed);

// ---- SETUP responses ----

[[nodiscard]] std::uint16_t parse_event_port(const Bytes& response_body);
struct StreamSetup {
    std::int64_t stream_id = 0;
    std::optional<std::uint16_t> data_port; // Present for data streams.
};
[[nodiscard]] StreamSetup parse_stream_setup(const Bytes& response_body);

// ---- /command ----

/// The /command body: {"params": {"data": <command as a binary plist>}}.
[[nodiscard]] Bytes command_body(const PlistValue& command);
[[nodiscard]] PlistValue insert_play_queue_item(const std::string& item_uuid,
                                                const std::string& url, double start_seconds);
[[nodiscard]] PlistValue set_interested_in_date_range(const std::string& item_uuid);
[[nodiscard]] PlistValue set_action_at_item_end();
[[nodiscard]] PlistValue set_rate(double rate);

// ---- Receiver events ----

/// A decoded event-channel message. `playback_state` is lower-cased and set
/// only for "playbackState" events.
struct SessionEvent {
    std::string type;
    std::optional<std::string> playback_state;
    std::optional<double> duration_seconds;
};
/// Decode an event body: the same {"params": {"data": ...}} envelope, or a
/// bare dictionary with a "type" (some receiver events are not wrapped).
[[nodiscard]] SessionEvent parse_session_event(const Bytes& body);
/**
 * A diagnostic, value-free outline of an event body: "type=<type>", the
 * playback state when present, then the key paths of the inner plist
 * (arrays as "name[]"). No other values are included, so identifiers, URLs and
 * metadata never appear. At most 48 paths, 4 levels deep.
 */
[[nodiscard]] std::string describe_event_structure(const Bytes& body);
} // namespace send_airplay2::detail
#endif
