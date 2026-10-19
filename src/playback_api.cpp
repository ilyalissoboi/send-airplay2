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
#ifdef SAP2_RUNTIME_VALIDATION
#include "runtime_validation.h"
#include <future>
#include <vector>
#endif

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string_view>
#include <new>
#include <optional>
#include <string>
#include <stdexcept>
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
constexpr std::size_t cast_status_v4_size = offsetof(sap2_cast_status, audio_decision);
// Round the final v4 field to its ABI alignment, including x86 tail padding.
constexpr std::size_t cast_options_v4_full_size =
    (cast_options_v4_size + alignof(sap2_cast_options) - 1) / alignof(sap2_cast_options) *
    alignof(sap2_cast_options);
static_assert(offsetof(sap2_cast_options, audio_profile) >= cast_options_v4_full_size,
              "audio profile overlaps v4 tail padding");

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
 * Automatic stores clear only through their accepted conversion owner; forced
 * stereo also clears on release. Offline checks suppress context release and
 * borrow the optional cancellation callback/atomic until all workers drain.
 */
class HostFragmentStore {
public:
    explicit HostFragmentStore(const sap2_aac_fragment_store& store, bool clear_on_release = true,
                               void* cancel_context = nullptr,
                               int (*should_cancel)(void*) = nullptr,
                               std::atomic_bool* cancelled = nullptr) noexcept
        : store_(store), clear_on_release_(clear_on_release), cancel_context_(cancel_context),
          should_cancel_(should_cancel), cancelled_(cancelled) {}
    ~HostFragmentStore() {
        if (armed_) {
            if (clear_on_release_) {
                clear();
            }
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
        const sap2_read_control control{&context.request, &context.cast_stopped, cancel_context_,
                                        should_cancel_, cancelled_};
        if (store_.seal(store_.context, index, bytes.data(), bytes.size(), &control) != SAP2_OK) {
            throw std::runtime_error("fragment storage failed");
        }
    }
    std::size_t read(std::size_t index, std::uint64_t offset, std::uint8_t* buffer,
                     std::size_t capacity, const AacCacheContext& context) const noexcept {
        const sap2_read_control control{&context.request, &context.cast_stopped, cancel_context_,
                                        should_cancel_, cancelled_};
        return store_.read_at(store_.context, index, offset, buffer, capacity, &control);
    }

private:
    sap2_aac_fragment_store store_;
    bool armed_ = false;
    const bool clear_on_release_;
    void* const cancel_context_;
    int (*const should_cancel_)(void*);
    std::atomic_bool* const cancelled_;
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
        } else if (options.delivery == SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS) {
            if (options.struct_size < sizeof(sap2_cast_options) ||
                (options.audio_profile != SAP2_AUDIO_PROFILE_APPLE_HLS_V1 &&
                 options.audio_profile != SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1) ||
                options.conversion_permission > SAP2_AUDIO_PRESERVE_ORIGINAL) {
                return CastResult::invalid_argument;
            }
            if (options.fragment_store &&
                (options.fragment_store->struct_size < sizeof(sap2_aac_fragment_store) ||
                 !options.fragment_store->seal || !options.fragment_store->read_at ||
                 !options.fragment_store->clear)) {
                return CastResult::invalid_argument;
            }
            settings.delivery = CastDelivery::automatic_audio_hls;
            settings.audio_profile = options.audio_profile == SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1
                                         ? AudioDeliveryProfile::apple_audio_v1
                                         : AudioDeliveryProfile::apple_hls_v1;
            settings.audio_track_id = options.audio_track_id
                                          ? std::optional<std::uint64_t>(options.audio_track_id)
                                          : std::nullopt;
            settings.cache_quota_bytes = options.cache_quota_bytes;
            settings.conversion_permission =
                options.conversion_permission == SAP2_AUDIO_ALLOW_STEREO
                    ? AudioConversionPermission::allow_stereo
                    : AudioConversionPermission::preserve_original;
        } else if (options.delivery != SAP2_DELIVERY_PROGRESSIVE) {
            return CastResult::invalid_argument;
        }
    }
    if (options.struct_size >= cast_options_v4_size &&
        settings.delivery != CastDelivery::stereo_aac_hls &&
        settings.delivery != CastDelivery::automatic_audio_hls &&
        (options.audio_track_id || options.cache_quota_bytes || options.fragment_store)) {
        return CastResult::invalid_argument;
    }
    return CastResult::ok;
}

std::uint32_t audio_family_code(SourceAudioFamily family) noexcept {
    switch (family) {
    case SourceAudioFamily::none:
        return SAP2_AUDIO_FAMILY_NONE;
    case SourceAudioFamily::aac:
        return SAP2_AUDIO_FAMILY_AAC;
    case SourceAudioFamily::ac3:
        return SAP2_AUDIO_FAMILY_AC3;
    case SourceAudioFamily::eac3:
        return SAP2_AUDIO_FAMILY_EAC3;
    case SourceAudioFamily::dts:
        return SAP2_AUDIO_FAMILY_DTS;
    case SourceAudioFamily::flac:
        return SAP2_AUDIO_FAMILY_FLAC;
    case SourceAudioFamily::opus:
        return SAP2_AUDIO_FAMILY_OPUS;
    case SourceAudioFamily::vorbis:
        return SAP2_AUDIO_FAMILY_VORBIS;
    case SourceAudioFamily::pcm:
        return SAP2_AUDIO_FAMILY_PCM;
    case SourceAudioFamily::truehd:
        return SAP2_AUDIO_FAMILY_TRUEHD;
    case SourceAudioFamily::mp3:
        return SAP2_AUDIO_FAMILY_MP3;
    }
    return SAP2_AUDIO_FAMILY_NONE;
}
void write_audio_decision(const AudioDeliveryPlan& plan, sap2_audio_decision& decision) noexcept {
    decision = {};
    decision.struct_size = sizeof(decision);
    decision.profile = plan.profile == AudioDeliveryProfile::apple_audio_v1
                           ? SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1
                           : SAP2_AUDIO_PROFILE_APPLE_HLS_V1;
    decision.selected_audio_id = plan.source_id;
    decision.source_family = audio_family_code(plan.source_family);
    decision.output_family = audio_family_code(plan.output_family);
    decision.output_channels = plan.output_channels;
    switch (plan.action) {
    case AudioDeliveryAction::video_only:
        decision.action = SAP2_AUDIO_ACTION_VIDEO_ONLY;
        break;
    case AudioDeliveryAction::copy:
        decision.action = SAP2_AUDIO_ACTION_COPY;
        break;
    case AudioDeliveryAction::stereo_aac:
        decision.action = SAP2_AUDIO_ACTION_STEREO_AAC;
        break;
    case AudioDeliveryAction::progressive:
        decision.action = SAP2_AUDIO_ACTION_PROGRESSIVE;
        break;
    }
    switch (plan.reason) {
    case AudioDeliveryReason::no_enabled_audio:
        decision.reason = SAP2_AUDIO_REASON_NO_ENABLED_AUDIO;
        break;
    case AudioDeliveryReason::profile_copy:
        decision.reason = SAP2_AUDIO_REASON_PROFILE_COPY;
        break;
    case AudioDeliveryReason::profile_requires_stereo:
        decision.reason = SAP2_AUDIO_REASON_PROFILE_REQUIRES_STEREO;
        break;
    case AudioDeliveryReason::profile_progressive:
        decision.reason = SAP2_AUDIO_REASON_PROFILE_PROGRESSIVE;
        break;
    }
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
    if (status.struct_size >= cast_status_v4_size) {
        status.media_failure = static_cast<std::uint32_t>(snapshot.media_failure);
        status.media_cleaned_up = snapshot.media_cleaned_up ? 1 : 0;
        status.authoring_flags = snapshot.authoring_flags;
        status.reserved_v4 = 0;
        status.selected_audio_id = snapshot.selected_audio_id;
    }
    if (status.struct_size >= sizeof(sap2_cast_status)) {
        status.audio_decision = {};
        if (snapshot.audio_decision) {
            write_audio_decision(*snapshot.audio_decision, status.audio_decision);
        }
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

void sap2_audio_options_init(sap2_audio_options* options, size_t struct_size) {
    if (!options || struct_size < sizeof(sap2_audio_options)) {
        return;
    }
    *options = {};
    options->struct_size = sizeof(*options);
    options->profile = SAP2_AUDIO_PROFILE_APPLE_HLS_V1;
    options->timeout_ms = SAP2_MAX_START_TIMEOUT_MS;
}
int32_t sap2_media_check_audio(const sap2_media_source* source, const sap2_audio_options* options,
                               void* context, int (*should_cancel)(void*),
                               sap2_audio_decision* decision) {
    if (!decision || decision->struct_size < sizeof(sap2_audio_decision)) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    const auto output_size = decision->struct_size;
    *decision = {};
    decision->struct_size = output_size;
    if (!source || source->struct_size < sizeof(sap2_media_source) || !source->read_at ||
        !options || options->struct_size < sizeof(sap2_audio_options) ||
        (options->profile != SAP2_AUDIO_PROFILE_APPLE_HLS_V1 &&
         options->profile != SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1) ||
        options->conversion_permission > SAP2_AUDIO_PRESERVE_ORIGINAL || !options->timeout_ms ||
        options->timeout_ms > SAP2_MAX_START_TIMEOUT_MS) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    const auto* offered_store = options->fragment_store;
    if (offered_store &&
        (offered_store->struct_size < sizeof(sap2_aac_fragment_store) || !offered_store->seal ||
         !offered_store->read_at || !offered_store->clear)) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    std::atomic_bool cancelled{false};
    const MediaReadContext request{&cancelled, &cancelled,
                                   std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(options->timeout_ms)};
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
            return poll() ? std::size_t{0} : count;
        };
        AacFragmentStore store;
        if (offered_store) {
            auto table = *offered_store;
            table.release = nullptr; // Offline check borrows the host context.
            auto host = std::make_shared<HostFragmentStore>(table, false, context, should_cancel,
                                                            &cancelled);
            host->arm();
            store = fragment_store_for(host);
        }
        auto prepared = PreparedAutomaticMedia::open(
            std::move(borrowed), std::move(store), options->cache_quota_bytes, request,
            options->profile == SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1
                ? AudioDeliveryProfile::apple_audio_v1
                : AudioDeliveryProfile::apple_hls_v1,
            options->conversion_permission == SAP2_AUDIO_ALLOW_STEREO
                ? AudioConversionPermission::allow_stereo
                : AudioConversionPermission::preserve_original,
            options->audio_track_id ? std::optional<std::uint64_t>(options->audio_track_id)
                                    : std::nullopt);
        if (poll()) {
            return cancelled ? SAP2_ERROR_CANCELLED : SAP2_ERROR_MEDIA_GENERATION_TIMEOUT;
        }
        write_audio_decision(prepared->decision(), *decision);
        return SAP2_OK;
    } catch (...) {
        poll();
        return cancelled               ? SAP2_ERROR_CANCELLED
               : request.should_stop() ? SAP2_ERROR_MEDIA_GENERATION_TIMEOUT
                                       : contained_failure();
    }
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
    if (known >= sizeof(sap2_cast_options)) {
        options->audio_profile = SAP2_AUDIO_PROFILE_APPLE_HLS_V1;
    }
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
        if (settings.delivery == CastDelivery::stereo_aac_hls ||
            (settings.delivery == CastDelivery::automatic_audio_hls && options->fragment_store)) {
            fragments = std::make_shared<HostFragmentStore>(
                *options->fragment_store, settings.delivery == CastDelivery::stereo_aac_hls);
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

#ifdef SAP2_RUNTIME_VALIDATION
namespace {
/// Bounded offline fetch with the same resource callbacks used by HTTP workers.
Bytes validation_read(const send_airplay2::MediaResource& resource,
                      const MediaReadContext& context) {
    constexpr std::uint64_t max_validation_resource_bytes = 16U * 1024U * 1024U;
    constexpr std::size_t validation_read_chunk = 4093;
    const auto size = send_airplay2::media_resource_size(resource, context);
    if (!size || size > max_validation_resource_bytes) {
        throw std::runtime_error("validation resource bound");
    }
    Bytes bytes(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto capacity = std::min(validation_read_chunk, bytes.size() - offset);
        const auto count =
            resource.source.read_at(offset, bytes.data() + offset, capacity, context);
        if (!count || count > capacity || context.should_stop()) {
            throw std::runtime_error("validation resource read");
        }
        offset += count;
    }
    return bytes;
}
std::uint64_t validation_elapsed(std::chrono::steady_clock::time_point begin) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - begin)
                                          .count());
}
/// One production owner behind the two private runtime hooks. No conversion or
/// resource-serving logic is implemented here; it only adapts their accessors.
class RuntimePreparedMedia {
public:
    RuntimePreparedMedia(MediaSource source, AacFragmentStore store, std::uint64_t quota,
                         const MediaReadContext& context, std::uint64_t audio_id, bool automatic,
                         bool preserve) {
        const auto selected = audio_id ? std::optional<std::uint64_t>{audio_id} : std::nullopt;
        if (automatic) {
            automatic_ =
                PreparedAutomaticMedia::open(std::move(source), std::move(store), quota, context,
                                             AudioDeliveryProfile::apple_hls_v1,
                                             preserve ? AudioConversionPermission::preserve_original
                                                      : AudioConversionPermission::allow_stereo,
                                             selected);
        } else {
            stereo_ =
                prepare_stereo_media(std::move(source), std::move(store), quota, context, selected);
        }
    }
    RuntimePreparedMedia(const RuntimePreparedMedia&) = delete;
    RuntimePreparedMedia& operator=(const RuntimePreparedMedia&) = delete;
    RuntimePreparedMedia(RuntimePreparedMedia&&) = delete;
    RuntimePreparedMedia& operator=(RuntimePreparedMedia&&) = delete;
    [[nodiscard]] const send_airplay2::detail::RemuxedHls& presentation() const noexcept {
        return automatic_ ? automatic_->presentation() : stereo_->presentation();
    }
    [[nodiscard]] StereoAacHlsFailure failure() const noexcept {
        return automatic_ ? automatic_->failure() : stereo_->failure();
    }
    [[nodiscard]] std::uint64_t selected_audio_id() const noexcept {
        return automatic_ ? automatic_->decision().source_id : stereo_->selected_audio_id();
    }
    [[nodiscard]] bool converted() const noexcept {
        return !automatic_ || automatic_->decision().action == AudioDeliveryAction::stereo_aac;
    }
    void write_decision(sap2_audio_decision& decision) const noexcept {
        if (automatic_) {
            write_audio_decision(automatic_->decision(), decision);
        }
    }
    void stop() noexcept {
        if (automatic_) {
            automatic_->stop();
        } else {
            stereo_->stop();
        }
    }

private:
    std::unique_ptr<PreparedStereoMedia> stereo_;
    std::unique_ptr<PreparedAutomaticMedia> automatic_;
};

/// Counts actual host callback invocations/capacities, not logical parser reads.
/// The guard outlives prepared resources and records refusals as well as success.
class RuntimeReadMeasurements {
public:
    explicit RuntimeReadMeasurements(sap2_runtime_automatic_report* report) noexcept
        : report_(report) {}
    ~RuntimeReadMeasurements() {
        if (!report_) {
            return;
        }
        if (!prepared_) {
            finish_preparation();
        }
        report_->request_read_calls = calls_.load() - report_->preparation_read_calls;
        report_->request_requested_bytes = requested_.load() - report_->preparation_requested_bytes;
        report_->request_actual_bytes = actual_.load() - report_->preparation_actual_bytes;
    }
    RuntimeReadMeasurements(const RuntimeReadMeasurements&) = delete;
    RuntimeReadMeasurements& operator=(const RuntimeReadMeasurements&) = delete;
    RuntimeReadMeasurements(RuntimeReadMeasurements&&) = delete;
    RuntimeReadMeasurements& operator=(RuntimeReadMeasurements&&) = delete;
    void record(std::size_t capacity, std::size_t count) noexcept {
        if (report_) {
            ++calls_;
            requested_ += capacity;
            actual_ += count;
        }
    }
    void finish_preparation() noexcept {
        if (!report_) {
            return;
        }
        report_->preparation_read_calls = calls_.load();
        report_->preparation_requested_bytes = requested_.load();
        report_->preparation_actual_bytes = actual_.load();
        prepared_ = true;
    }

private:
    sap2_runtime_automatic_report* const report_;
    bool prepared_ = false;
    std::atomic<std::uint64_t> calls_{0}, requested_{0}, actual_{0};
};

/// Exercise request cancellation, cold/concurrent/cache reads and ordered Stop.
/// Copies are dropped after drain; only converted retained callbacks are probed.
int32_t validate_runtime_resources(RuntimePreparedMedia& prepared, const MediaReadContext& context,
                                   bool& cancel_request, std::atomic_bool& cancelled,
                                   std::uint32_t mode, sap2_runtime_report* report,
                                   sap2_runtime_automatic_report* automatic_report) {
    const auto& resources = prepared.presentation().resources;
    std::vector<const send_airplay2::MediaResource*> audio;
    for (const auto& resource : resources) {
        const char prefix = prepared.converted() ? 'a' : 's';
        if (resource.name.size() > 5 && resource.name.front() == prefix &&
            resource.name.substr(resource.name.size() - 4) == ".m4s") {
            audio.push_back(&resource);
        }
    }
    if (audio.empty()) {
        return SAP2_ERROR_INTERNAL;
    }
    report->audio_segments = audio.size();
    const auto request_begin = std::chrono::steady_clock::now();
    if (mode == 1) {
        cancel_request = true;
        bool refused = false;
        try {
            (void)validation_read(*audio.back(), context);
        } catch (...) {
            refused = true;
        }
        cancel_request = false;
        cancelled = false;
        if (!refused || prepared.failure() != StereoAacHlsFailure::none) {
            return SAP2_ERROR_INTERNAL;
        }
    }
    try {
        if (automatic_report && mode >= 9) {
            const auto index = mode == 9    ? std::size_t{0}
                               : mode == 10 ? audio.size() / 2
                                            : audio.size() - 1;
            const auto cold_begin = std::chrono::steady_clock::now();
            const auto cold = validation_read(*audio[index], context);
            automatic_report->cold_request_ms = validation_elapsed(cold_begin);
            const auto warm_begin = std::chrono::steady_clock::now();
            const auto warm = validation_read(*audio[index], context);
            automatic_report->warm_request_ms = validation_elapsed(warm_begin);
            if (cold != warm) {
                return SAP2_ERROR_INTERNAL;
            }
            report->resource_bytes = cold.size();
        } else {
            // Cold forward seek, sealed reread, backward seek, then all resources to EOF.
            const auto fetch_late = [&] { return validation_read(*audio.back(), context); };
            auto first = std::async(std::launch::async, fetch_late);
            auto second = std::async(std::launch::async, fetch_late);
            const auto late = first.get();
            if (late != second.get() || late != validation_read(*audio.back(), context)) {
                return SAP2_ERROR_INTERNAL;
            }
            for (const auto& resource : resources) {
                report->resource_bytes += validation_read(resource, context).size();
            }
        }
    } catch (...) {
        report->request_ms = validation_elapsed(request_begin);
        report->failure = static_cast<std::uint32_t>(prepared.failure());
        prepared.stop();
        prepared.stop();
        const auto expected =
            mode == 2 ? StereoAacHlsFailure::cache_error : StereoAacHlsFailure::source_error;
        if (!prepared.converted() && mode == 3) {
            // Copy read errors stay resource errors, without a conversion
            // worker/failure classification. No conversion fallback occurs.
            return report->failure == SAP2_MEDIA_FAILURE_NONE ? SAP2_OK : SAP2_ERROR_INTERNAL;
        }
        return (mode == 2 || mode == 3) && report->failure == static_cast<std::uint32_t>(expected)
                   ? SAP2_OK
                   : SAP2_ERROR_INTERNAL;
    }
    report->request_ms = validation_elapsed(request_begin);
    report->failure = static_cast<std::uint32_t>(prepared.failure());
    // Retained audio callbacks become inert after ordered stop; no further source reads.
    auto retained = *audio.front();
    if (!prepared.converted()) {
        retained = {};
    }
    prepared.stop();
    prepared.stop();
    bool refused = false;
    try {
        if (!prepared.converted()) {
            return (mode <= 1 || mode == 6 || mode >= 9) && !report->failure ? SAP2_OK
                                                                             : SAP2_ERROR_INTERNAL;
        }
        (void)validation_read(retained, context);
    } catch (...) {
        refused = true;
    }
    if (automatic_report) {
        automatic_report->post_stop_refused = refused ? 1U : 0U;
    }
    return (mode <= 1 || mode >= 9) && !report->failure && refused ? SAP2_OK : SAP2_ERROR_INTERNAL;
}

int32_t runtime_validate(const sap2_media_source* source, const sap2_aac_fragment_store* store,
                         std::uint64_t audio_id, std::uint64_t quota, std::uint32_t mode,
                         sap2_runtime_report* report,
                         sap2_runtime_automatic_report* automatic_report) {
    const bool automatic = automatic_report != nullptr;
    if (!source || source->struct_size < sizeof(*source) || !source->read_at || !source->release ||
        !store || store->struct_size < sizeof(*store) || !store->seal || !store->read_at ||
        !store->clear || !store->release || !report || report->struct_size != sizeof(*report) ||
        mode > (automatic ? 11U : 5U)) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    *report = {};
    report->struct_size = sizeof(*report);
    bool ownership_transferred = false;
    RuntimeReadMeasurements measurements(automatic_report);
    try {
        auto host_source = std::make_shared<HostSource>(*source);
        auto host_store = std::make_shared<HostFragmentStore>(*store, !automatic);
        host_source->arm();
        host_store->arm();
        ownership_transferred = true;
        std::atomic_bool stopped{false};
        std::atomic_bool cancelled{false};
        const MediaReadContext context{
            &stopped, &cancelled, std::chrono::steady_clock::now() + std::chrono::seconds(120)};
        auto media_source = media_source_for(host_source);
        bool generation_started = false;
        bool cancel_request = false;
        bool copied_presentation = false;
        const auto read_source = media_source.read_at;
        media_source.read_at = [&](std::uint64_t offset, std::uint8_t* bytes, std::size_t size,
                                   const MediaReadContext& read_context) {
            if (mode == 7 || (copied_presentation && cancel_request)) {
                cancelled = true;
                return std::size_t{0};
            }
            // Mode 3 refuses source reads only after preparation, not during inventory.
            if (mode == 3 && generation_started) {
                return std::size_t{0};
            }
            const auto count = read_source(offset, bytes, size, read_context);
            measurements.record(size, count);
            return count;
        };
        auto fragment_store = fragment_store_for(host_store);
        const auto read_fragment = fragment_store.read_at;
        fragment_store.read_at = [&](std::size_t index, std::uint64_t offset, std::uint8_t* bytes,
                                     std::size_t capacity, const AacCacheContext& cache_context) {
            if (cancel_request) {
                cancelled = true;
            }
            return read_fragment(index, offset, bytes, capacity, cache_context);
        };
        const auto begin = std::chrono::steady_clock::now();
        std::unique_ptr<RuntimePreparedMedia> prepared;
        try {
            prepared = std::make_unique<RuntimePreparedMedia>(
                std::move(media_source), std::move(fragment_store), quota, context, audio_id,
                automatic, mode == 6);
        } catch (const MediaPreparationException& error) {
            report->preparation_ms = validation_elapsed(begin);
            if (automatic_report) {
                automatic_report->preparation_result = contained_failure();
            }
            if (error.reason() == MediaPreparationFailure::resource_limit) {
                report->failure = SAP2_MEDIA_FAILURE_LIMIT;
            }
            return ((mode == 4 && error.reason() == MediaPreparationFailure::invalid_selection) ||
                    ((mode == 5 || mode == 8) &&
                     error.reason() == MediaPreparationFailure::resource_limit) ||
                    (mode == 6 && error.reason() == MediaPreparationFailure::unsupported) ||
                    (mode == 7 && error.reason() == MediaPreparationFailure::cancelled))
                       ? SAP2_OK
                       : SAP2_ERROR_INTERNAL;
        }
        if (mode == 4 || mode == 7 || mode == 8) {
            return SAP2_ERROR_INTERNAL;
        }
        report->preparation_ms = validation_elapsed(begin);
        report->selected_audio_id = prepared->selected_audio_id();
        measurements.finish_preparation();
        copied_presentation = !prepared->converted();
        if (automatic_report) {
            prepared->write_decision(automatic_report->decision);
        }
        generation_started = true;
        return validate_runtime_resources(*prepared, context, cancel_request, cancelled, mode,
                                          report, automatic_report);
    } catch (...) {
        if (!ownership_transferred) {
            if (!automatic) {
                store->clear(store->context);
            }
            store->release(store->context);
            source->release(source->context);
        }
        return SAP2_ERROR_INTERNAL;
    }
}
} // namespace
extern "C" int32_t sap2_runtime_validate(const sap2_media_source* source,
                                         const sap2_aac_fragment_store* store, uint64_t audio_id,
                                         uint32_t mode, sap2_runtime_report* report) {
    return runtime_validate(source, store, audio_id, 512ULL * 1024 * 1024, mode, report, nullptr);
}
extern "C" int32_t sap2_runtime_validate_automatic(const sap2_media_source* source,
                                                   const sap2_aac_fragment_store* store,
                                                   uint64_t audio_id, uint64_t quota_bytes,
                                                   uint32_t mode,
                                                   sap2_runtime_automatic_report* report) {
    if (!report || report->media.struct_size != sizeof(*report)) {
        return SAP2_ERROR_INVALID_ARGUMENT;
    }
    *report = {};
    report->media.struct_size = sizeof(report->media);
    const auto result =
        runtime_validate(source, store, audio_id, quota_bytes, mode, &report->media, report);
    report->media.struct_size = sizeof(*report);
    return result;
}
#endif
