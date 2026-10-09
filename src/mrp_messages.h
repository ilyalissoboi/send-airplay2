// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MRP_MESSAGES_H
#define SEND_AIRPLAY2_MRP_MESSAGES_H
#include "protobuf_wire.h"
#include "session_messages.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace send_airplay2::detail {
namespace mrp {
constexpr std::uint32_t send_command = 1, command_result = 2, set_state = 4;
constexpr std::uint32_t device_info = 15, updates_config = 16, connection_state = 38;
constexpr std::uint32_t device_info_update = 37;
/// WAKE_DEVICE_MESSAGE: no extension and no response (pyatv's MRP turn_on).
constexpr std::uint32_t wake_device = 41;
constexpr std::uint32_t keyboard_session = 24, heartbeat = 42;
constexpr std::uint32_t now_playing_client = 46, now_playing_player = 47, update_content = 56;
constexpr std::uint32_t remove_client = 53, remove_player = 54;
constexpr std::size_t max_batch_messages = 64;
} // namespace mrp
enum class PlaybackCommand : std::uint32_t { play = 1, pause = 2, stop = 4, seek = 45 };
/// Owns decoded extension plaintext. Move transfers ownership; replacement,
/// destruction and exception unwinding erase payload bytes before release.
/// Correlation identifiers are private tokens, never diagnostic text.
struct MrpMessage {
    MrpMessage() = default;
    ~MrpMessage();
    MrpMessage(const MrpMessage&) = delete;
    MrpMessage& operator=(const MrpMessage&) = delete;
    MrpMessage(MrpMessage&& other) noexcept;
    MrpMessage& operator=(MrpMessage&& other) noexcept;
    std::uint32_t type = 0;
    std::string identifier; // Private correlation token, never printed.
    std::uint64_t error = 0;
    bool has_payload = false;
    Bytes payload; // Mapped extension, empty for unknown types.
};
[[nodiscard]] Bytes encode_mrp(std::uint32_t type, const std::string& identifier,
                               const std::string& unique_identifier, const Bytes& payload = {});
[[nodiscard]] MrpMessage decode_mrp(std::string_view bytes);
/// Varint-length-prefixed protobufs, plus the reference's bounded single
/// unprefixed message when the first byte is the type tag (0x08).
[[nodiscard]] std::vector<MrpMessage> decode_mrp_batch(const Bytes& bytes);
[[nodiscard]] Bytes mrp_device_info(const SenderIdentity& identity, const Bytes& pairing_id);
[[nodiscard]] Bytes mrp_connection_state();
[[nodiscard]] Bytes mrp_updates_config();
/// Explicit playerPath prevents a command targeting an unrelated active app.
[[nodiscard]] Bytes mrp_command(PlaybackCommand command, const Bytes& player_path,
                                double position_seconds = 0);
/**
 * Receiver power report from DEVICE_INFO or DEVICE_INFO_UPDATE: the
 * DeviceInfoMessage logicalDeviceCount (field 22). pyatv treats one or more as
 * on and zero as off. Empty for other message types and when the field is
 * absent. Throws std::invalid_argument for a malformed payload or a count
 * outside uint32.
 */
[[nodiscard]] std::optional<std::uint32_t> mrp_logical_device_count(const MrpMessage& message);
/// False for any receiver protocol/send/handler/nested status error. Missing
/// command result extension is malformed, rather than an implicit success.
[[nodiscard]] bool mrp_command_succeeded(const MrpMessage& message);

/// Only scalar telemetry is exposed to the CLI; no metadata or identifiers.
struct MrpPlaybackStatus {
    bool owned = false;
    /// Receiver-reported paused/stopped position reaches its duration. Does
    /// not use our wall-clock extrapolation; a mid-item pause is not EOF.
    bool at_end = false;
    /// Receiver-reported paused/stopped position within 0.5 s of its duration.
    /// Not EOF by itself: a pause just before the end is still a pause. The
    /// session uses it only once the URL channel reports stopped/idle, because
    /// tvOS can stop HLS a frame or two short of the playlist duration (D60).
    bool near_end = false;
    std::string state;
    std::optional<double> position_seconds, duration_seconds, playback_rate;
    /// Last receiver elapsed-time scalar, without wall-clock extrapolation or
    /// duration clamping. Retained for diagnostics; never proves stop intent.
    std::optional<double> reported_position_seconds;
    std::uint64_t messages = 0;
    std::uint64_t heartbeats = 0;
};
/** Bounded tracker for our URL item. Prefer an exact media URL/UUID match;
 * otherwise use the documented cooperative startup binding (D32). An active
 * bundle alone never establishes ownership. Concurrent same-duration AirPlay
 * takeover is outside that fallback's guarantee; see docs/mrp-controls.md.
 * Keeps at most 32 paths and 256 items per queue. Serial use under caller lock.
 * Clears ownership on removal, queue replacement or another active player.
 */
class MrpPlaybackTracker {
public:
    void expect_item(std::string uuid, std::string url);
    /// Cooperative-receiver fallback when MRP omits URL/UUID: bind only a new
    /// item on the selected TVAirPlay path with matching URL-stream duration.
    /// Never bind a second item automatically during this cast.
    void confirm_url_playing(double duration_seconds);
    void apply(const MrpMessage& message);
    [[nodiscard]] MrpPlaybackStatus status() const;
    [[nodiscard]] Bytes owned_player_path() const;

private:
    struct Player {
        std::string key; // Bundle + origin/process/player identity, not a display name.
        Bytes path;
        std::string item;
        bool owns_item = false;
        std::string baseline_item;
        bool changed_item = false;
        std::string state;
        std::optional<double> elapsed, duration, rate, timestamp;
    };
    void apply_metadata(Player& player, const protobuf_wire::Fields& fields);
    void consider_binding();
    std::vector<Player> players_;
    std::string item_uuid_, media_url_, active_client_, active_player_;
    bool client_selected_ = false, player_selected_ = false;
    std::optional<double> url_duration_;
    bool bound_once_ = false;
    std::string bound_key_, bound_item_;
};
} // namespace send_airplay2::detail
#endif
