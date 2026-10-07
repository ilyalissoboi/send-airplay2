// SPDX-License-Identifier: Apache-2.0
// Wire schema facts: pinned MIT pyatv .proto definitions; dependencies.md.
#include "mrp_messages.h"
#include "control_crypto.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
namespace pb = protobuf_wire;
constexpr std::size_t max_identity_size = 1024;
constexpr std::size_t max_players = 32, max_queue_items = 256;
constexpr double cocoa_epoch_unix_seconds = 978307200.0;
[[noreturn]] void malformed() {
    throw std::invalid_argument("Invalid bounded MRP message");
}
std::uint32_t extension(std::uint32_t type) {
    switch (type) {
    case mrp::send_command:
        return 6;
    case mrp::command_result:
        return 7;
    case mrp::set_state:
        return 9;
    case mrp::device_info:
        return 20;
    case mrp::updates_config:
        return 21;
    case mrp::keyboard_session:
        return 29;
    case mrp::connection_state:
        return 42;
    case mrp::now_playing_client:
        return 50;
    case mrp::now_playing_player:
        return 51;
    case mrp::remove_client:
        return 57;
    case mrp::remove_player:
        return 58;
    case mrp::update_content:
        return 60;
    default:
        return 0;
    }
}
std::string string_field(const pb::Fields& fields, std::uint32_t number) {
    const auto* field = pb::find(fields, number, 2);
    if (!field) {
        return {};
    }
    if (field->data.size() > max_identity_size) {
        malformed();
    }
    return std::string(field->data);
}
pb::Fields nested(const pb::Fields& fields, std::uint32_t number) {
    const auto* field = pb::find(fields, number, 2);
    return field ? pb::decode(field->data) : pb::Fields{};
}
std::string client_key(const pb::Fields& path) {
    return string_field(nested(path, 2), 2);
}
std::string player_key(const pb::Fields& path) {
    const auto bundle = client_key(path);
    const auto player = string_field(nested(path, 3), 1);
    if (bundle.empty() || player.empty()) {
        return {};
    }
    const auto origin = nested(path, 1);
    const auto client = nested(path, 2);
    const auto* origin_id = pb::find(origin, 3, 0);
    const auto* process_id = pb::find(client, 1, 0);
    return bundle + '\0' + std::to_string(origin_id ? origin_id->integer : 0) + '\0' +
           std::to_string(process_id ? process_id->integer : 0) + '\0' + player;
}
std::optional<double> real(const pb::Fields& fields, std::uint32_t number, unsigned wire) {
    const auto* field = pb::find(fields, number, wire);
    if (!field) {
        return {};
    }
    const double value = wire == 1 ? pb::real64(*field) : pb::real32(*field);
    if (!std::isfinite(value)) {
        malformed();
    }
    return value;
}
bool nonzero(const pb::Fields& fields, std::uint32_t number) {
    const auto* field = pb::find(fields, number, 0);
    return field && field->integer != 0;
}
} // namespace
MrpMessage::~MrpMessage() {
    cleanse(payload.data(), payload.size());
}
MrpMessage::MrpMessage(MrpMessage&& other) noexcept {
    *this = std::move(other);
}
MrpMessage& MrpMessage::operator=(MrpMessage&& other) noexcept {
    if (this != &other) {
        cleanse(payload.data(), payload.size());
        type = other.type;
        identifier = std::move(other.identifier);
        error = other.error;
        has_payload = other.has_payload;
        payload = std::move(other.payload);
    }
    return *this;
}
Bytes encode_mrp(std::uint32_t type, const std::string& identifier,
                 const std::string& unique_identifier, const Bytes& payload) {
    Bytes output;
    pb::integer(output, 1, type);
    if (!identifier.empty()) {
        pb::data(output, 2, identifier);
    }
    pb::integer(output, 4, 0);
    // The independent protobuf oracle emits ordinary fields before extensions.
    pb::data(output, 85, unique_identifier);
    const auto field = extension(type);
    if (field != 0) {
        pb::data(output, field, payload);
    }
    return output;
}
MrpMessage decode_mrp(std::string_view bytes) {
    const auto fields = pb::decode(bytes);
    const auto* type = pb::find(fields, 1, 0);
    if (!type || type->integer > std::numeric_limits<std::uint32_t>::max()) {
        malformed();
    }
    MrpMessage message;
    message.type = static_cast<std::uint32_t>(type->integer);
    message.identifier = string_field(fields, 2);
    if (const auto* error = pb::find(fields, 4, 0)) {
        message.error = error->integer;
    }
    if (const auto field = extension(message.type)) {
        if (const auto* payload = pb::find(fields, field, 2)) {
            message.has_payload = true;
            message.payload.assign(payload->data.begin(), payload->data.end());
        }
    }
    return message;
}
std::vector<MrpMessage> decode_mrp_batch(const Bytes& bytes) {
    const auto input = pb::view(bytes);
    if (input.size() > pb::max_message_size) {
        malformed();
    }
    std::vector<MrpMessage> messages;
    if (!input.empty() && static_cast<unsigned char>(input[0]) == 8) {
        auto message = decode_mrp(input);
        // The pinned reference accepts one unprefixed ProtocolMessage when
        // the first byte is the type tag. ConfigureConnection is one known
        // example; other receiver responses can use the same shape.
        messages.push_back(std::move(message));
        return messages;
    }
    for (std::size_t offset = 0; offset < input.size();) {
        if (messages.size() == mrp::max_batch_messages) {
            malformed();
        }
        const auto length = pb::varint(input, offset);
        if (length == 0 || length > input.size() - offset) {
            malformed();
        }
        messages.push_back(decode_mrp(input.substr(offset, static_cast<std::size_t>(length))));
        offset += static_cast<std::size_t>(length);
    }
    return messages;
}
Bytes mrp_device_info(const SenderIdentity& identity, const Bytes& pairing_id) {
    Bytes output;
    pb::data(output, 1, pairing_id);
    pb::data(output, 2, identity.name);
    pb::data(output, 3, "iPhone");
    pb::data(output, 4, identity.os_build_version);
    pb::data(output, 5, "com.apple.TVRemote");
    pb::data(output, 6, "344.28");
    pb::integer(output, 7, 1);
    pb::integer(output, 8, 108);
    pb::integer(output, 9, 1);
    pb::integer(output, 10, 1);
    pb::data(output, 12, "com.apple.TVMusic");
    pb::integer(output, 13, 1);
    pb::integer(output, 14, 1);
    pb::integer(output, 15, 1);
    pb::integer(output, 17, 2);
    pb::integer(output, 21, 1);
    pb::integer(output, 22, 1);
    return output;
}
Bytes mrp_connection_state() {
    Bytes output;
    pb::integer(output, 1, 2);
    return output;
}
Bytes mrp_updates_config() {
    Bytes output;
    // Reference subscription values first. nowPlayingUpdates=false still
    // delivers playback state in the reference's validated control run.
    pb::integer(output, 1, 1);
    pb::integer(output, 2, 0);
    pb::integer(output, 3, 1);
    pb::integer(output, 4, 1);
    pb::integer(output, 5, 1);
    return output;
}
Bytes mrp_command(PlaybackCommand command, const Bytes& path, double position) {
    if (path.empty() || !std::isfinite(position) || position < 0) {
        throw std::invalid_argument(
            "Command needs an owned player and nonnegative finite position");
    }
    Bytes output;
    pb::integer(output, 1, static_cast<std::uint32_t>(command));
    if (command == PlaybackCommand::seek) {
        Bytes options;
        pb::real64(options, 9, position);
        pb::data(output, 2, options);
    }
    pb::data(output, 3, path);
    return output;
}
bool mrp_command_succeeded(const MrpMessage& message) {
    if (message.type != mrp::command_result || !message.has_payload) {
        malformed();
    }
    if (message.error != 0) {
        return false;
    }
    const auto fields = pb::decode(pb::view(message.payload));
    if (nonzero(fields, 1) || nonzero(fields, 2)) {
        return false;
    }
    const auto result = nested(fields, 6);
    if (nonzero(result, 2)) {
        return false;
    }
    for (const auto& field : result) {
        if (field.number == 3) {
            if (field.wire != 2 || nonzero(pb::decode(field.data), 1)) {
                return false;
            }
        }
    }
    return true;
}
void MrpPlaybackTracker::expect_item(std::string uuid, std::string url) {
    for (auto& player : players_) {
        player.baseline_item = player.item;
        player.owns_item = false;
        player.changed_item = false;
    }
    item_uuid_ = std::move(uuid);
    media_url_ = std::move(url);
    url_duration_.reset();
    bound_once_ = false;
    bound_key_.clear();
    bound_item_.clear();
}
void MrpPlaybackTracker::confirm_url_playing(double duration) {
    if (std::isfinite(duration) && duration > 0) {
        url_duration_ = duration;
        consider_binding();
    }
}
void MrpPlaybackTracker::consider_binding() {
    if (bound_once_ || !url_duration_ || !client_selected_ || !player_selected_ ||
        active_client_ != "com.apple.TVAirPlay") {
        return;
    }
    for (auto& player : players_) {
        if (player.key == active_player_ && player.changed_item && !player.item.empty() &&
            player.duration && std::abs(*player.duration - *url_duration_) <= 0.5) {
            bound_once_ = true;
            bound_key_ = player.key;
            bound_item_ = player.item;
            player.owns_item = true;
            return;
        }
    }
}
void MrpPlaybackTracker::apply_metadata(Player& player, const pb::Fields& fields) {
    if (auto value = real(fields, 14, 1)) {
        player.duration = value;
    }
    if (auto value = real(fields, 35, 1)) {
        player.elapsed = value;
    }
    if (auto value = real(fields, 39, 5)) {
        player.rate = value;
    }
    if (auto value = real(fields, 74, 1)) {
        player.timestamp = value;
    }
    const auto* url = pb::find(fields, 32, 2);
    const auto* content = pb::find(fields, 44, 2);
    if ((!media_url_.empty() && url && url->data == media_url_) ||
        (!item_uuid_.empty() && content && content->data == item_uuid_)) {
        player.owns_item = true;
    }
}
void MrpPlaybackTracker::apply(const MrpMessage& message) {
    if (message.type != mrp::set_state && message.type != mrp::update_content &&
        message.type != mrp::now_playing_client && message.type != mrp::now_playing_player &&
        message.type != mrp::remove_client && message.type != mrp::remove_player) {
        return;
    }
    const auto fields = pb::decode(pb::view(message.payload));
    if (message.type == mrp::now_playing_client || message.type == mrp::remove_client) {
        const auto client = string_field(nested(fields, 1), 2);
        if (message.type == mrp::now_playing_client) {
            active_client_ = client;
            client_selected_ = true;
            consider_binding();
        } else {
            players_.erase(std::remove_if(players_.begin(), players_.end(),
                                          [&](const Player& player) {
                                              return player.key.substr(0, player.key.find('\0')) ==
                                                     client;
                                          }),
                           players_.end());
        }
        return;
    }
    const auto path_number = message.type == mrp::set_state        ? 9u
                             : message.type == mrp::update_content ? 2u
                                                                   : 1u;
    const auto* path_field = pb::find(fields, path_number, 2);
    if (!path_field) {
        return;
    }
    const auto path = pb::decode(path_field->data);
    const auto key = player_key(path);
    if (message.type == mrp::now_playing_player) {
        active_client_ = client_key(path);
        client_selected_ = true;
        active_player_ = key;
        player_selected_ = true;
        consider_binding();
        return;
    }
    if (key.empty()) {
        return;
    }
    auto iterator = std::find_if(players_.begin(), players_.end(),
                                 [&](const Player& player) { return player.key == key; });
    if (message.type == mrp::remove_player) {
        if (iterator != players_.end()) {
            players_.erase(iterator);
        }
        return;
    }
    if (iterator == players_.end()) {
        if (players_.size() == max_players) {
            malformed();
        }
        players_.push_back(Player{});
        iterator = players_.end() - 1;
        iterator->key = key;
    }
    auto& player = *iterator;
    player.path.assign(path_field->data.begin(), path_field->data.end());
    if (message.type == mrp::set_state) {
        if (const auto* state = pb::find(fields, 6, 0)) {
            switch (state->integer) {
            case 1:
                player.state = "playing";
                break;
            case 2:
                player.state = "paused";
                break;
            case 3:
                player.state = "stopped";
                break;
            case 4:
                player.state = "interrupted";
                break;
            case 5:
                player.state = "seeking";
                break;
            default:
                player.state = "unknown";
            }
        }
    }
    const auto* queue_field = message.type == mrp::set_state ? pb::find(fields, 3, 2) : nullptr;
    const auto queue = queue_field ? pb::decode(queue_field->data) : fields;
    if (message.type == mrp::set_state && !queue_field) {
        return;
    }
    if (queue_field) {
        player.owns_item = false;
        player.item.clear();
        player.elapsed.reset();
        player.duration.reset();
        player.rate.reset();
        player.timestamp.reset();
    }
    const auto item_field_number = queue_field ? 2u : 1u;
    std::uint64_t location = 0;
    if (queue_field) {
        if (const auto* value = pb::find(queue, 1, 0)) {
            location = value->integer;
        }
    }
    std::size_t count = 0;
    for (const auto& field : queue) {
        if (field.number != item_field_number) {
            continue;
        }
        if (field.wire != 2 || count == max_queue_items) {
            malformed();
        }
        const auto item = pb::decode(field.data);
        const auto id = string_field(item, 1);
        const bool selected =
            queue_field ? count == location : !player.item.empty() && player.item == id;
        ++count;
        if (!selected) {
            continue;
        }
        if (queue_field) {
            player.item = id;
            player.owns_item = !item_uuid_.empty() && id == item_uuid_;
            player.changed_item = id != player.baseline_item;
            if (bound_once_ && bound_key_ == player.key && bound_item_ == id) {
                player.owns_item = true;
            }
        }
        apply_metadata(player, nested(item, 2));
    }
    consider_binding();
}
MrpPlaybackStatus MrpPlaybackTracker::status() const {
    MrpPlaybackStatus result;
    for (const auto& player : players_) {
        if (!player.owns_item ||
            (client_selected_ && player.key.substr(0, player.key.find('\0')) != active_client_) ||
            (player_selected_ && player.key != active_player_)) {
            continue;
        }
        if (result.owned) {
            return {};
        } // Ambiguous ownership must not control either.
        result.owned = true;
        result.state = player.state;
        result.duration_seconds = player.duration;
        result.playback_rate = player.rate;
        result.position_seconds = player.elapsed;
        result.reported_position_seconds = player.elapsed;
        result.at_end = player.elapsed && player.duration && *player.duration > 0 &&
                        *player.elapsed >= *player.duration &&
                        (player.state == "paused" || player.state == "stopped");
        if (result.position_seconds && player.timestamp && player.rate &&
            player.state == "playing") {
            const auto now =
                std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                    .count() -
                cocoa_epoch_unix_seconds;
            // Future/stale clocks cannot manufacture enormous positions.
            const auto age = std::clamp(now - *player.timestamp, 0.0, 3600.0);
            *result.position_seconds += age * *player.rate;
        }
        if (result.position_seconds) {
            *result.position_seconds = std::max(0.0, *result.position_seconds);
            if (result.duration_seconds) {
                *result.position_seconds =
                    std::min(*result.position_seconds, std::max(0.0, *result.duration_seconds));
            }
        }
    }
    return result;
}
Bytes MrpPlaybackTracker::owned_player_path() const {
    if (!status().owned) {
        return {};
    }
    for (const auto& player : players_) {
        if (player.owns_item &&
            (!client_selected_ || player.key.substr(0, player.key.find('\0')) == active_client_) &&
            (!player_selected_ || player.key == active_player_)) {
            return player.path;
        }
    }
    return {};
}
} // namespace send_airplay2::detail
