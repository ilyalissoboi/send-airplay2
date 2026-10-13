// SPDX-License-Identifier: Apache-2.0
// C boundary for send_airplay2/playback.h and pairing.h: validation, type
// mapping and exception containment only. Behavior lives in CastController and
// host_credentials.cpp.
#include "send_airplay2/pairing.h"
#include "send_airplay2/playback.h"
#include "cast_controller.h"
#include "credential_store.h"
#include "host_credentials.h"
#include "media_track_inventory.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <new>
#include <optional>
#include <string>
#include <utility>

/// Borrowed view of one media read's stop conditions.
struct sap2_read_control {
    const send_airplay2::MediaReadContext* context;
    const std::atomic_bool* cast_stopped = nullptr;
    void* cancel_context = nullptr;
    int (*should_cancel)(void*) = nullptr;
    std::atomic_bool* probe_cancelled = nullptr;
};

struct sap2_track_list {
    send_airplay2::detail::MediaTrackInventory inventory;
};

struct sap2_cast {
    sap2_cast(send_airplay2::detail::CastSettings settings, send_airplay2::MediaSource source,
              send_airplay2::detail::CastDependencies dependencies)
        : controller(std::move(settings), std::move(source), std::move(dependencies)) {}
    send_airplay2::detail::CastController controller;
};

namespace {
using send_airplay2::MediaReadContext;
using send_airplay2::MediaSource;
using namespace send_airplay2::detail;

static_assert(static_cast<std::int32_t>(CastResult::ok) == SAP2_OK);
static_assert(static_cast<std::int32_t>(CastResult::invalid_argument) ==
              SAP2_ERROR_INVALID_ARGUMENT);
static_assert(static_cast<std::int32_t>(CastResult::invalid_state) == SAP2_ERROR_INVALID_STATE);
static_assert(static_cast<std::int32_t>(CastResult::cancelled) == SAP2_ERROR_CANCELLED);
static_assert(static_cast<std::int32_t>(CastResult::profile_not_found) ==
              SAP2_ERROR_PROFILE_NOT_FOUND);
static_assert(static_cast<std::int32_t>(CastResult::credential_store) ==
              SAP2_ERROR_CREDENTIAL_STORE);
static_assert(static_cast<std::int32_t>(CastResult::unsupported) == SAP2_ERROR_UNSUPPORTED);
static_assert(static_cast<std::int32_t>(CastResult::authentication) == SAP2_ERROR_AUTHENTICATION);
static_assert(static_cast<std::int32_t>(CastResult::receiver_rejected) ==
              SAP2_ERROR_RECEIVER_REJECTED);
static_assert(static_cast<std::int32_t>(CastResult::start_timeout) == SAP2_ERROR_START_TIMEOUT);
static_assert(static_cast<std::int32_t>(CastResult::connection) == SAP2_ERROR_CONNECTION);
static_assert(static_cast<std::int32_t>(CastResult::protocol) == SAP2_ERROR_PROTOCOL);
static_assert(static_cast<std::int32_t>(CastResult::media_server) == SAP2_ERROR_MEDIA_SERVER);
static_assert(static_cast<std::int32_t>(CastResult::not_owned) == SAP2_ERROR_NOT_OWNED);
static_assert(static_cast<std::int32_t>(CastResult::command_failed) == SAP2_ERROR_COMMAND_FAILED);
static_assert(static_cast<std::int32_t>(CastResult::ended) == SAP2_ERROR_ENDED);
static_assert(static_cast<std::int32_t>(CastResult::out_of_memory) == SAP2_ERROR_OUT_OF_MEMORY);
static_assert(static_cast<std::int32_t>(CastResult::internal) == SAP2_ERROR_INTERNAL);
static_assert(static_cast<std::int32_t>(CastResult::profile_exists) == SAP2_ERROR_PROFILE_EXISTS);
static_assert(static_cast<std::int32_t>(CastResult::pin_timeout) == SAP2_ERROR_PIN_TIMEOUT);
static_assert(static_cast<std::int32_t>(CastResult::media_unsupported) ==
              SAP2_ERROR_MEDIA_UNSUPPORTED);
static_assert(static_cast<std::int32_t>(CastResult::media_malformed) == SAP2_ERROR_MEDIA_MALFORMED);
static_assert(static_cast<std::int32_t>(CastResult::audio_selection_required) ==
              SAP2_ERROR_AUDIO_SELECTION_REQUIRED);
static_assert(static_cast<std::int32_t>(CastResult::audio_track_invalid) ==
              SAP2_ERROR_AUDIO_TRACK_INVALID);
static_assert(static_cast<std::int32_t>(CastResult::media_codec) == SAP2_ERROR_MEDIA_CODEC);
static_assert(static_cast<std::int32_t>(CastResult::media_cache) == SAP2_ERROR_MEDIA_CACHE);
static_assert(static_cast<std::int32_t>(CastResult::media_source) == SAP2_ERROR_MEDIA_SOURCE);
static_assert(static_cast<std::int32_t>(CastResult::media_limit) == SAP2_ERROR_MEDIA_LIMIT);
static_assert(static_cast<std::int32_t>(CastResult::media_generation_timeout) ==
              SAP2_ERROR_MEDIA_GENERATION_TIMEOUT);
static_assert(static_cast<std::uint32_t>(StereoAacHlsFailure::generation_timeout) ==
              SAP2_MEDIA_FAILURE_TIMEOUT);
static_assert(static_cast<std::uint32_t>(StereoAacHlsFailure::cache_error) ==
              SAP2_MEDIA_FAILURE_CACHE);
static_assert(authoring_audio_estimated == SAP2_AUTHORING_AUDIO_ESTIMATED);
static_assert(authoring_copied_sizes_estimated == SAP2_AUTHORING_COPIED_SIZES_ESTIMATED);
static_assert(authoring_subtitle_sizes_unmeasured == SAP2_AUTHORING_SUBTITLE_SIZES_UNMEASURED);

// Version 1 sap2_cast_options ended before credential_store (API version 2).
constexpr std::size_t cast_options_v1_size = offsetof(sap2_cast_options, credential_store);
constexpr std::size_t cast_options_v2_size =
    cast_options_v1_size + sizeof(sap2_cast_options::credential_store);
// The version 2 layout, for its full size: a version 2 host passes sizeof,
// which on 32-bit targets includes 4 bytes of tail padding (8-byte alignment).
struct CastOptionsV2 {
    std::uint32_t struct_size;
    const char* receiver_address;
    std::uint16_t receiver_port;
    const char* profile;
    const char* content_type;
    std::uint32_t start_timeout_ms;
    std::uint32_t media_connections;
    double start_position_seconds;
    const sap2_credential_store* credential_store;
};
static_assert(offsetof(CastOptionsV2, credential_store) ==
                  offsetof(sap2_cast_options, credential_store),
              "version 2 prefix of sap2_cast_options");
// Version 3 added delivery; it must start beyond any version 2 struct, or a
// version 2 host's padding would be read as delivery.
static_assert(offsetof(sap2_cast_options, delivery) >= sizeof(CastOptionsV2),
              "delivery overlaps a version 2 struct");
constexpr std::size_t cast_options_v3_size =
    offsetof(sap2_cast_options, delivery) + sizeof(sap2_cast_options::delivery);
struct CastOptionsV3 {
    std::uint32_t struct_size;
    const char* receiver_address;
    std::uint16_t receiver_port;
    const char* profile;
    const char* content_type;
    std::uint32_t start_timeout_ms;
    std::uint32_t media_connections;
    double start_position_seconds;
    const sap2_credential_store* credential_store;
    std::uint32_t reserved;
    std::uint32_t delivery;
};
static_assert(offsetof(CastOptionsV3, delivery) == offsetof(sap2_cast_options, delivery),
              "version 3 prefix of sap2_cast_options");
static_assert(offsetof(sap2_cast_options, audio_track_id) >= sizeof(CastOptionsV3),
              "audio ID overlaps a version 3 struct");
constexpr std::size_t cast_options_v4_size =
    offsetof(sap2_cast_options, fragment_store) + sizeof(sap2_cast_options::fragment_store);
constexpr std::size_t cast_status_v3_size = offsetof(sap2_cast_status, media_failure);

// Longest numeric IPv6 text form (INET6_ADDRSTRLEN without the terminator).
constexpr std::size_t max_address_length = 45;
// Generous bound for a plain type/subtype; the media server validates syntax.
constexpr std::size_t max_content_type_length = 255;
constexpr const char* default_content_type = "video/mp4";

std::int32_t to_c(CastResult result) noexcept {
    return static_cast<std::int32_t>(result);
}

/// Bounded strlen: returns limit + 1 when no terminator is found within limit.
std::size_t bounded_length(const char* text, std::size_t limit) noexcept {
    std::size_t length = 0;
    while (length <= limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

/**
 * Shared owner of the host media context. Every MediaSource copy holds it, so
 * `release` runs when the last copy is destroyed: after the media server has
 * joined its workers and the controller has dropped its own copy. It stays
 * disarmed until sap2_cast_create() succeeds, because on failure the caller
 * keeps ownership and must not see release().
 */
class HostSource {
public:
    explicit HostSource(const sap2_media_source& source) noexcept : source_(source) {}
    ~HostSource() {
        if (armed_ && source_.release) {
            source_.release(source_.context);
        }
    }
    HostSource(const HostSource&) = delete;
    HostSource& operator=(const HostSource&) = delete;
    HostSource(HostSource&&) = delete;
    HostSource& operator=(HostSource&&) = delete;

    void arm() noexcept {
        armed_ = true;
    }
    [[nodiscard]] std::uint64_t size() const noexcept {
        return source_.size;
    }
    std::size_t read_at(std::uint64_t offset, std::uint8_t* buffer, std::size_t capacity,
                        const MediaReadContext& context) const noexcept {
        const sap2_read_control control{&context};
        return source_.read_at(source_.context, offset, buffer, capacity, &control);
    }

private:
    sap2_media_source source_;
    bool armed_ = false; // Written before the handle is published to the host.
};

MediaSource media_source_for(const std::shared_ptr<HostSource>& host) {
    MediaSource source;
    source.size = [host] { return host->size(); };
    source.read_at = [host](std::uint64_t offset, std::uint8_t* buffer, std::size_t capacity,
                            const MediaReadContext& context) {
        return host->read_at(offset, buffer, capacity, context);
    };
    return source;
}

/** Cache callbacks retain this disarmed owner until successful create. Cleanup
 * drains reads first in the controller, and release follows all callback copies.
 */
class HostFragmentStore {
public:
    explicit HostFragmentStore(const sap2_aac_fragment_store& store) noexcept : store_(store) {}
    ~HostFragmentStore() {
        if (armed_) {
            clear();
            if (store_.release) {
                store_.release(store_.context);
            }
        }
    }
    HostFragmentStore(const HostFragmentStore&) = delete;
    HostFragmentStore& operator=(const HostFragmentStore&) = delete;
    HostFragmentStore(HostFragmentStore&&) = delete;
    HostFragmentStore& operator=(HostFragmentStore&&) = delete;
    void arm() noexcept {
        armed_ = true;
    }
    void clear() noexcept {
        if (armed_ && !cleared_.exchange(true)) {
            store_.clear(store_.context);
        }
    }
    void seal(std::size_t index, const Bytes& bytes, const AacCacheContext& context) const {
        const sap2_read_control control{&context.request, &context.cast_stopped};
        if (store_.seal(store_.context, index, bytes.data(), bytes.size(), &control) != SAP2_OK) {
            throw std::runtime_error("fragment storage failed");
        }
    }
    std::size_t read(std::size_t index, std::uint64_t offset, std::uint8_t* buffer,
                     std::size_t capacity, const AacCacheContext& context) const noexcept {
        const sap2_read_control control{&context.request, &context.cast_stopped};
        return store_.read_at(store_.context, index, offset, buffer, capacity, &control);
    }

private:
    sap2_aac_fragment_store store_;
    bool armed_ = false;
    std::atomic_bool cleared_{false};
};
AacFragmentStore fragment_store_for(const std::shared_ptr<HostFragmentStore>& host) {
    return {[host](std::size_t index, const Bytes& bytes, const AacCacheContext& context) {
                host->seal(index, bytes, context);
            },
            [host](std::size_t index, std::uint64_t offset, std::uint8_t* buffer,
                   std::size_t capacity, const AacCacheContext& context) {
                return host->read(index, offset, buffer, capacity, context);
            },
            [host] { host->clear(); }};
}

/// Returns ok and fills `settings`, or the argument error. May throw bad_alloc.
/// The host store a cast options struct carries: null for a version 1 struct or
/// a null field. Sets `valid` to false for a malformed table.
const sap2_credential_store* host_store_from(const sap2_cast_options& options, bool& valid) {
    valid = true;
    if (options.struct_size < cast_options_v2_size || !options.credential_store) {
        return nullptr;
    }
    valid = valid_credential_store(*options.credential_store);
    return options.credential_store;
}

/// Profiles longer than this are rejected before copying; the format check
/// itself is validate_credential_profile().
constexpr std::size_t max_profile_length = 64;

CastResult settings_from(const sap2_cast_options& options, CastSettings& settings) {
    if (options.struct_size < cast_options_v1_size || !options.receiver_address ||
        !options.profile || options.receiver_port == 0) {
        return CastResult::invalid_argument;
    }
    const auto address_length = bounded_length(options.receiver_address, max_address_length);
    if (address_length == 0 || address_length > max_address_length) {
        return CastResult::invalid_argument;
    }
    if (options.start_timeout_ms == 0 || options.start_timeout_ms > SAP2_MAX_START_TIMEOUT_MS ||
        options.media_connections == 0 || options.media_connections > SAP2_MAX_MEDIA_CONNECTIONS ||
        !std::isfinite(options.start_position_seconds) || options.start_position_seconds < 0) {
        return CastResult::invalid_argument;
    }
    const char* content_type = options.content_type ? options.content_type : default_content_type;
    const auto content_type_length = bounded_length(content_type, max_content_type_length);
    if (content_type_length == 0 || content_type_length > max_content_type_length) {
        return CastResult::invalid_argument;
    }
    const auto profile_length = bounded_length(options.profile, max_profile_length);
    if (profile_length > max_profile_length) {
        return CastResult::invalid_argument;
    }
    settings.profile.assign(options.profile, profile_length);
    validate_credential_profile(settings.profile); // Throws invalid_profile.
    settings.receiver_address.assign(options.receiver_address, address_length);
    settings.receiver_port = options.receiver_port;
    settings.content_type.assign(content_type, content_type_length);
    settings.start_timeout = std::chrono::milliseconds(options.start_timeout_ms);
    settings.media_connections = options.media_connections;
    settings.start_position_seconds = options.start_position_seconds;
    if (options.struct_size >= cast_options_v3_size) {
        if (options.delivery == SAP2_DELIVERY_HLS_REMUX) {
            settings.delivery = CastDelivery::hls_remux;
        } else if (options.delivery == SAP2_DELIVERY_STEREO_AAC_HLS) {
            if (options.struct_size < cast_options_v4_size || !options.fragment_store ||
                options.fragment_store->struct_size < sizeof(sap2_aac_fragment_store) ||
                !options.fragment_store->seal || !options.fragment_store->read_at ||
                !options.fragment_store->clear || options.cache_quota_bytes == 0) {
                return CastResult::invalid_argument;
            }
            if (!stereo_aac_available()) {
                return CastResult::unsupported;
            }
            settings.delivery = CastDelivery::stereo_aac_hls;
            if (options.audio_track_id) {
                settings.audio_track_id = options.audio_track_id;
            }
            settings.cache_quota_bytes = options.cache_quota_bytes;
        } else if (options.delivery != SAP2_DELIVERY_PROGRESSIVE) {
            return CastResult::invalid_argument;
        }
    }
    if (options.struct_size >= cast_options_v4_size &&
        settings.delivery != CastDelivery::stereo_aac_hls &&
        (options.audio_track_id || options.cache_quota_bytes || options.fragment_store)) {
        return CastResult::invalid_argument;
    }
    return CastResult::ok;
}

std::uint32_t phase_code(CastPhase phase) noexcept {
    switch (phase) {
    case CastPhase::created:
        return SAP2_PHASE_CREATED;
    case CastPhase::starting:
        return SAP2_PHASE_STARTING;
    case CastPhase::active:
        return SAP2_PHASE_ACTIVE;
    case CastPhase::ended:
        return SAP2_PHASE_ENDED;
    case CastPhase::start_failed:
        return SAP2_PHASE_START_FAILED;
    case CastPhase::stopped:
        return SAP2_PHASE_STOPPED;
    }
    return SAP2_PHASE_STOPPED;
}

/// Fixed URL state labels published by the session (session_messages.cpp).
struct StateLabel {
    std::uint32_t code;
    const char* label;
};
constexpr StateLabel state_labels[] = {
    {SAP2_STATE_NONE, ""},         {SAP2_STATE_LOADING, "loading"}, {SAP2_STATE_PLAYING, "playing"},
    {SAP2_STATE_PAUSED, "paused"}, {SAP2_STATE_IDLE, "idle"},       {SAP2_STATE_STOPPED, "stopped"},
    {SAP2_STATE_ENDED, "ended"},   {SAP2_STATE_OTHER, "other"},
};

std::uint32_t state_code(const std::string& label) noexcept {
    for (const auto& entry : state_labels) {
        if (label == entry.label) {
            return entry.code;
        }
    }
    return SAP2_STATE_OTHER;
}

const char* state_label(std::uint32_t code) noexcept {
    for (const auto& entry : state_labels) {
        if (code == entry.code) {
            return entry.label;
        }
    }
    return nullptr;
}

std::uint32_t end_code(SessionEnd reason) noexcept {
    switch (reason) {
    case SessionEnd::none:
        return SAP2_END_NONE;
    case SessionEnd::sender_stop:
        return SAP2_END_SENDER_STOP;
    case SessionEnd::media_end:
        return SAP2_END_MEDIA_END;
    case SessionEnd::receiver_stop:
        return SAP2_END_RECEIVER_STOP;
    case SessionEnd::ownership_lost:
        return SAP2_END_OWNERSHIP_LOST;
    case SessionEnd::connection_lost:
        return SAP2_END_CONNECTION_LOST;
    }
    return SAP2_END_NONE;
}

std::uint32_t failure_channel_code(SessionFailureChannel channel) noexcept {
    switch (channel) {
    case SessionFailureChannel::none:
        return SAP2_FAILURE_CHANNEL_NONE;
    case SessionFailureChannel::url_events:
        return SAP2_FAILURE_CHANNEL_URL_EVENTS;
    case SessionFailureChannel::remote_events:
        return SAP2_FAILURE_CHANNEL_REMOTE_EVENTS;
    case SessionFailureChannel::url_feedback:
        return SAP2_FAILURE_CHANNEL_URL_FEEDBACK;
    case SessionFailureChannel::remote_feedback:
        return SAP2_FAILURE_CHANNEL_REMOTE_FEEDBACK;
    case SessionFailureChannel::timing:
        return SAP2_FAILURE_CHANNEL_TIMING;
    case SessionFailureChannel::mrp:
        return SAP2_FAILURE_CHANNEL_MRP;
    case SessionFailureChannel::supervisor:
        return SAP2_FAILURE_CHANNEL_SUPERVISOR;
    }
    return SAP2_FAILURE_CHANNEL_NONE;
}

std::uint32_t failure_reason_code(SessionFailureReason reason) noexcept {
    switch (reason) {
    case SessionFailureReason::none:
        return SAP2_FAILURE_REASON_NONE;
    case SessionFailureReason::timeout:
        return SAP2_FAILURE_REASON_TIMEOUT;
    case SessionFailureReason::disconnected:
        return SAP2_FAILURE_REASON_DISCONNECTED;
    case SessionFailureReason::network:
        return SAP2_FAILURE_REASON_NETWORK;
    case SessionFailureReason::invalid_message:
        return SAP2_FAILURE_REASON_INVALID_MESSAGE;
    case SessionFailureReason::authentication:
        return SAP2_FAILURE_REASON_AUTHENTICATION;
    case SessionFailureReason::cancelled:
        return SAP2_FAILURE_REASON_CANCELLED;
    case SessionFailureReason::rejected:
        return SAP2_FAILURE_REASON_REJECTED;
    case SessionFailureReason::other:
        return SAP2_FAILURE_REASON_OTHER;
    }
    return SAP2_FAILURE_REASON_OTHER;
}

/// Write the version-1 fields; the caller's struct_size is preserved.
void write_status(const CastSnapshot& snapshot, sap2_cast_status& status) noexcept {
    status.phase = phase_code(snapshot.phase);
    status.start_result = to_c(snapshot.start_result);
    status.rejected_status = snapshot.rejected_status;
    status.playback_state = state_code(snapshot.session.playback_state);
    status.end_reason = end_code(snapshot.session.end_reason);
    if (snapshot.media_failure_first &&
        (status.end_reason == SAP2_END_NONE || status.end_reason == SAP2_END_SENDER_STOP)) {
        status.end_reason = SAP2_END_MEDIA_FAILURE;
    }
    status.failure_channel = failure_channel_code(snapshot.session.failure_channel);
    status.failure_reason = failure_reason_code(snapshot.session.failure_reason);
    status.owned = snapshot.playback.owned ? 1 : 0;
    status.at_end = snapshot.playback.at_end ? 1 : 0;
    status.cleaned_up = snapshot.session.cleaned_up ? 1 : 0;
    const auto scalar = [](const std::optional<double>& value, std::uint32_t& has, double& output) {
        has = value && std::isfinite(*value) ? 1 : 0;
        output = has ? *value : 0;
    };
    scalar(snapshot.playback.position_seconds, status.has_position, status.position_seconds);
    scalar(snapshot.playback.duration_seconds, status.has_duration, status.duration_seconds);
    scalar(snapshot.playback.playback_rate, status.has_playback_rate, status.playback_rate);
    if (status.struct_size >= sizeof(sap2_cast_status)) {
        status.media_failure = static_cast<std::uint32_t>(snapshot.media_failure);
        status.media_cleaned_up = snapshot.media_cleaned_up ? 1 : 0;
        status.authoring_flags = snapshot.authoring_flags;
        status.reserved_v4 = 0;
        status.selected_audio_id = snapshot.selected_audio_id;
    }
}

bool valid_status(const sap2_cast_status* status) noexcept {
    return status && status->struct_size >= cast_status_v3_size;
}

/// Map the in-flight exception with the shared categories, so a platform
/// without a built-in store reports SAP2_ERROR_UNSUPPORTED here too.
std::int32_t contained_failure() noexcept {
    unsigned rejected_status = 0;
    return to_c(cast_start_result(std::current_exception(), rejected_status));
}
} // namespace

extern "C" {

uint32_t sap2_playback_api_version(void) {
    return SAP2_PLAYBACK_API_VERSION;
}
int sap2_stereo_aac_available(void) {
    return stereo_aac_available() ? 1 : 0;
}

const char* sap2_result_name(int32_t result) {
    switch (result) {
    case SAP2_OK:
        return "ok";
    case SAP2_ERROR_INVALID_ARGUMENT:
        return "invalid_argument";
    case SAP2_ERROR_INVALID_STATE:
        return "invalid_state";
    case SAP2_ERROR_CANCELLED:
        return "cancelled";
    case SAP2_ERROR_PROFILE_NOT_FOUND:
        return "profile_not_found";
    case SAP2_ERROR_CREDENTIAL_STORE:
        return "credential_store";
    case SAP2_ERROR_UNSUPPORTED:
        return "unsupported";
    case SAP2_ERROR_AUTHENTICATION:
        return "authentication";
    case SAP2_ERROR_RECEIVER_REJECTED:
        return "receiver_rejected";
    case SAP2_ERROR_START_TIMEOUT:
        return "start_timeout";
    case SAP2_ERROR_CONNECTION:
        return "connection";
    case SAP2_ERROR_PROTOCOL:
        return "protocol";
    case SAP2_ERROR_MEDIA_SERVER:
        return "media_server";
    case SAP2_ERROR_NOT_OWNED:
        return "not_owned";
    case SAP2_ERROR_COMMAND_FAILED:
        return "command_failed";
    case SAP2_ERROR_ENDED:
        return "ended";
    case SAP2_ERROR_OUT_OF_MEMORY:
        return "out_of_memory";
    case SAP2_ERROR_INTERNAL:
        return "internal";
    case SAP2_ERROR_PROFILE_EXISTS:
        return "profile_exists";
    case SAP2_ERROR_MEDIA_UNSUPPORTED:
        return "media_unsupported";
    case SAP2_ERROR_MEDIA_MALFORMED:
        return "media_malformed";
    case SAP2_ERROR_PIN_TIMEOUT:
        return "pin_timeout";
    case SAP2_ERROR_AUDIO_SELECTION_REQUIRED:
        return "audio_selection_required";
    case SAP2_ERROR_AUDIO_TRACK_INVALID:
        return "audio_track_invalid";
    case SAP2_ERROR_MEDIA_CODEC:
        return "media_codec";
    case SAP2_ERROR_MEDIA_CACHE:
        return "media_cache";
    case SAP2_ERROR_MEDIA_SOURCE:
        return "media_source";
    case SAP2_ERROR_MEDIA_LIMIT:
        return "media_limit";
    case SAP2_ERROR_MEDIA_GENERATION_TIMEOUT:
        return "media_generation_timeout";
    default:
        return "unknown";
    }
}

int sap2_read_should_stop(const sap2_read_control* control) {
    if (!control || !control->context || control->context->should_stop() ||
        (control->cast_stopped && control->cast_stopped->load())) {
        return 1;
    }
    if (control->should_cancel && control->should_cancel(control->cancel_context)) {
        if (control->probe_cancelled) {
            control->probe_cancelled->store(true);
        }
        return 1;
    }
    return 0;
}

int32_t sap2_media_probe(const sap2_media_source* source, uint32_t timeout_ms, void* context,
                         int (*should_cancel)(void*), sap2_track_list** tracks) {
    if (!tracks) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    *tracks = nullptr;
    if (!source || source->struct_size < sizeof(sap2_media_source) || !source->read_at ||
        !timeout_ms || timeout_ms > SAP2_MAX_START_TIMEOUT_MS) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    std::atomic_bool cancelled{false};
    const MediaReadContext request{&cancelled, &cancelled,
                                   std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(timeout_ms)};
    const auto poll = [&] {
        if (should_cancel && should_cancel(context)) {
            cancelled = true;
        }
        return request.should_stop();
    };
    try {
        if (poll()) {
            return cancelled ? SAP2_ERROR_CANCELLED : SAP2_ERROR_MEDIA_GENERATION_TIMEOUT;
        }
        MediaSource borrowed;
        borrowed.size = [source] { return source->size; };
        borrowed.read_at = [&](std::uint64_t offset, std::uint8_t* buffer, std::size_t capacity,
                               const MediaReadContext& read_context) {
            if (poll()) {
                return std::size_t{0};
            }
            const sap2_read_control control{&read_context, nullptr, context, should_cancel,
                                            &cancelled};
            const auto count = source->read_at(source->context, offset, buffer, capacity, &control);
            if (poll()) {
                return std::size_t{0};
            }
            return count;
        };
        auto list = std::make_unique<sap2_track_list>();
        list->inventory = probe_media_tracks(borrowed, request);
        if (poll()) {
            return cancelled ? SAP2_ERROR_CANCELLED : SAP2_ERROR_MEDIA_GENERATION_TIMEOUT;
        }
        *tracks = list.release();
        return SAP2_OK;
    } catch (const RemuxException& error) {
        if (poll()) {
            return cancelled ? SAP2_ERROR_CANCELLED : SAP2_ERROR_MEDIA_GENERATION_TIMEOUT;
        }
        return error.reason() == RemuxFailure::too_large   ? SAP2_ERROR_MEDIA_LIMIT
               : error.reason() == RemuxFailure::malformed ? SAP2_ERROR_MEDIA_MALFORMED
                                                           : SAP2_ERROR_MEDIA_UNSUPPORTED;
    } catch (const std::bad_alloc&) {
        return SAP2_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        poll();
        return cancelled               ? SAP2_ERROR_CANCELLED
               : request.should_stop() ? SAP2_ERROR_MEDIA_GENERATION_TIMEOUT
                                       : SAP2_ERROR_MEDIA_SOURCE;
    }
}
size_t sap2_track_list_count(const sap2_track_list* tracks) {
    return tracks ? tracks->inventory.tracks.size() : 0;
}
uint32_t sap2_track_list_container(const sap2_track_list* tracks) {
    return !tracks                                               ? 0
           : tracks->inventory.container == SourceContainer::mp4 ? SAP2_CONTAINER_MP4
                                                                 : SAP2_CONTAINER_MATROSKA;
}
int32_t sap2_track_list_get(const sap2_track_list* tracks, size_t index, sap2_media_track* track) {
    if (!tracks || !track || track->struct_size < sizeof(sap2_media_track) ||
        index >= tracks->inventory.tracks.size()) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    const auto& source = tracks->inventory.tracks[index];
    track->kind = source.kind == SourceTrackKind::audio   ? SAP2_TRACK_AUDIO
                  : source.kind == SourceTrackKind::video ? SAP2_TRACK_VIDEO
                                                          : SAP2_TRACK_OTHER;
    track->id = source.id;
    track->enabled = source.enabled;
    track->default_track = source.default_track;
    track->encoded = source.encoded;
    track->standard_timeline = source.standard_timeline;
    const auto copy = [](const std::string& value, char* output, std::size_t capacity) {
        std::memset(output, 0, capacity);
        std::memcpy(output, value.data(), std::min(value.size(), capacity - 1));
    };
    copy(source.codec, track->codec, sizeof(track->codec));
    copy(source.language, track->language, sizeof(track->language));
    copy(source.name, track->name, sizeof(track->name));
    return SAP2_OK;
}
void sap2_track_list_free(sap2_track_list* tracks) {
    delete tracks;
}

void sap2_cast_options_init_sized(sap2_cast_options* options, size_t struct_size) {
    if (!options || struct_size < cast_options_v1_size) {
        return;
    }
    // Never write past the caller's struct: an older host's struct is smaller.
    const auto known = std::min(struct_size, sizeof(sap2_cast_options));
    std::memset(options, 0, known);
    options->struct_size = static_cast<uint32_t>(known);
    options->receiver_port = SAP2_DEFAULT_RECEIVER_PORT;
    options->start_timeout_ms = SAP2_DEFAULT_START_TIMEOUT_MS;
    options->media_connections = SAP2_DEFAULT_MEDIA_CONNECTIONS;
}

void sap2_cast_options_init(sap2_cast_options* options) {
    // The version 1 entry point: its callers allocated a version 1 struct.
    sap2_cast_options_init_sized(options, cast_options_v1_size);
}

int32_t sap2_cast_create(const sap2_cast_options* options, const sap2_media_source* source,
                         sap2_cast** cast) {
    if (!cast) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    *cast = nullptr;
    if (!options || !source || source->struct_size < sizeof(sap2_media_source) ||
        !source->read_at) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    try {
        CastSettings settings;
        const auto validated = settings_from(*options, settings);
        if (validated != CastResult::ok) {
            return to_c(validated);
        }
        bool valid_store = true;
        const auto* host_store = host_store_from(*options, valid_store);
        if (!valid_store) {
            return SAP2_ERROR_INVALID_ARGUMENT;
        }
        CastDependencies dependencies;
        if (host_store) {
            // Copied now; the host keeps its callbacks valid until destroy.
            auto store = std::make_shared<HostCredentialStore>(*host_store);
            dependencies.load_credentials = [store](std::string_view profile) {
                return store->load(profile);
            };
        }
        auto host = std::make_shared<HostSource>(*source);
        std::shared_ptr<HostFragmentStore> fragments;
        if (settings.delivery == CastDelivery::stereo_aac_hls) {
            fragments = std::make_shared<HostFragmentStore>(*options->fragment_store);
            settings.fragment_store = fragment_store_for(fragments);
        }
        auto handle = std::make_unique<sap2_cast>(std::move(settings), media_source_for(host),
                                                  std::move(dependencies));
        host->arm(); // Ownership transfers only now that nothing else can fail.
        if (fragments) {
            fragments->arm();
        }
        *cast = handle.release();
        return SAP2_OK;
    } catch (...) {
        return contained_failure();
    }
}

int32_t sap2_cast_start(sap2_cast* cast) {
    if (!cast) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    return to_c(cast->controller.start());
}

int32_t sap2_cast_get_status(sap2_cast* cast, sap2_cast_status* status) {
    if (!cast || !valid_status(status)) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    try {
        write_status(cast->controller.snapshot(), *status);
        return SAP2_OK;
    } catch (...) {
        return contained_failure();
    }
}

int32_t sap2_cast_wait_for_change(sap2_cast* cast, uint32_t previous_state, uint32_t timeout_ms,
                                  sap2_cast_status* status) {
    const char* previous = state_label(previous_state);
    if (!cast || !valid_status(status) || !previous) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    try {
        write_status(
            cast->controller.wait_for_change(previous, std::chrono::milliseconds(timeout_ms)),
            *status);
        return SAP2_OK;
    } catch (...) {
        return contained_failure();
    }
}

int32_t sap2_cast_command(sap2_cast* cast, uint32_t command, double position_seconds) {
    if (!cast) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    PlaybackCommand mapped = PlaybackCommand::play;
    switch (command) {
    case SAP2_COMMAND_PLAY:
        mapped = PlaybackCommand::play;
        break;
    case SAP2_COMMAND_PAUSE:
        mapped = PlaybackCommand::pause;
        break;
    case SAP2_COMMAND_STOP:
        mapped = PlaybackCommand::stop;
        break;
    case SAP2_COMMAND_SEEK:
        mapped = PlaybackCommand::seek;
        break;
    default:
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    return to_c(cast->controller.command(mapped, position_seconds));
}

int32_t sap2_cast_stop(sap2_cast* cast) {
    if (!cast) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    cast->controller.stop();
    return SAP2_OK;
}

void sap2_cast_destroy(sap2_cast* cast) {
    delete cast; // The controller destructor stops; the last source copy releases.
}
void sap2_pair_options_init(sap2_pair_options* options, size_t struct_size) {
    if (!options || struct_size < sizeof(sap2_pair_options)) {
        return; // Only one pairing struct version exists; nothing smaller is valid.
    }
    // A later, larger struct keeps its own extra fields; only known bytes are set.
    std::memset(options, 0, sizeof(sap2_pair_options));
    options->struct_size = sizeof(sap2_pair_options);
    options->receiver_port = SAP2_DEFAULT_RECEIVER_PORT;
    options->timeout_ms = SAP2_DEFAULT_PAIR_TIMEOUT_MS;
    options->pin_timeout_ms = SAP2_DEFAULT_PIN_TIMEOUT_MS;
}

int32_t sap2_pair(const sap2_pair_options* options) {
    if (!options || options->struct_size < sizeof(sap2_pair_options) ||
        !options->receiver_address || !options->profile || !options->read_pin ||
        options->receiver_port == 0 || options->timeout_ms == 0 ||
        options->timeout_ms > SAP2_MAX_PAIR_TIMEOUT_MS || options->pin_timeout_ms == 0 ||
        options->pin_timeout_ms > SAP2_MAX_PIN_TIMEOUT_MS ||
        (options->credential_store && !valid_credential_store(*options->credential_store))) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    const auto address_length = bounded_length(options->receiver_address, max_address_length);
    const auto profile_length = bounded_length(options->profile, max_profile_length);
    if (address_length == 0 || address_length > max_address_length ||
        profile_length > max_profile_length) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    try {
        PairSettings settings;
        settings.endpoint = {std::string(options->receiver_address, address_length),
                             options->receiver_port, 0};
        settings.profile.assign(options->profile, profile_length);
        settings.timeout = std::chrono::milliseconds(options->timeout_ms);
        settings.pin_timeout = std::chrono::milliseconds(options->pin_timeout_ms);
        auto store = select_credential_store(options->credential_store);
        auto sessions = native_auth_sessions();
        CallbackPinPrompt prompt(options->read_pin, options->pin_context);
        return to_c(pair_profile(settings, *store, *sessions, prompt));
    } catch (...) {
        return contained_failure();
    }
}

int32_t sap2_forget_profile(const char* profile, const sap2_credential_store* credential_store) {
    if (!profile || (credential_store && !valid_credential_store(*credential_store))) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    const auto profile_length = bounded_length(profile, max_profile_length);
    if (profile_length > max_profile_length) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    try {
        auto store = select_credential_store(credential_store);
        return to_c(forget_profile(std::string_view(profile, profile_length), *store));
    } catch (...) {
        return contained_failure();
    }
}
} // extern "C"
