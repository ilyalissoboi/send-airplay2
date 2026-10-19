// SPDX-License-Identifier: Apache-2.0
#include "prepared_automatic_media.h"
#include "mp3_audio.h"
#include "standalone_audio.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <new>
#include <utility>

namespace send_airplay2::detail {
namespace {
void check_preparation_context(const MediaReadContext& context) {
    if (!context.server_stopped || !context.request_cancelled) {
        throw MediaPreparationException(MediaPreparationFailure::source_error);
    }
    if (context.should_stop()) {
        throw MediaPreparationException(std::chrono::steady_clock::now() >= context.deadline
                                            ? MediaPreparationFailure::timeout
                                            : MediaPreparationFailure::cancelled);
    }
}

/** Borrowed exact reader for signature and progressive MP4 validation. Charge
 * every short-read retry; never retain the startup context in served resources. */
class AutomaticReader {
public:
    AutomaticReader(const MediaSource& source, std::uint64_t size, const MediaReadContext& context)
        : source_(source), size_(size), context_(context) {}
    void read(std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        if (offset > size_ || length > size_ - offset) {
            throw MediaPreparationException(MediaPreparationFailure::malformed);
        }
        while (length) {
            check_preparation_context(context_);
            constexpr std::uint64_t maximum_bytes = 256ULL << 20;
            constexpr std::size_t maximum_calls = 1'048'576;
            const auto wanted = std::min<std::size_t>(length, 65536);
            if (++calls_ > maximum_calls || wanted > maximum_bytes - requested_) {
                throw MediaPreparationException(MediaPreparationFailure::resource_limit);
            }
            requested_ += wanted;
            const auto count = source_.read_at(offset, output, wanted, context_);
            check_preparation_context(context_);
            if (!count || count > wanted) {
                throw MediaPreparationException(MediaPreparationFailure::source_error);
            }
            offset += count;
            output += count;
            length -= count;
        }
    }

private:
    const MediaSource& source_;
    const std::uint64_t size_;
    const MediaReadContext& context_;
    std::uint64_t requested_ = 0;
    std::size_t calls_ = 0;
};

AudioDeliveryPlan audio_only_plan(const MediaSource& source, std::uint64_t size,
                                  const MediaReadContext& context,
                                  std::optional<std::uint64_t> selected_id) {
    AutomaticReader reader(source, size, context);
    std::array<std::uint8_t, 3> signature{};
    reader.read(0, signature.data(), signature.size());
    AudioDeliveryPlan plan;
    plan.profile = AudioDeliveryProfile::apple_audio_v1;
    // Recognized MP3 candidates go through the complete framing probe. A failed
    // probe never falls back to container parsing or a different delivery route.
    const bool mp3 = (signature[0] == 'I' && signature[1] == 'D' && signature[2] == '3') ||
                     (signature[0] == 0xff && (signature[1] & 0xe0) == 0xe0);
    if (mp3) {
        if (selected_id && *selected_id != 1) {
            throw MediaPreparationException(MediaPreparationFailure::invalid_selection);
        }
        const auto audio = probe_mp3_audio(source, context);
        if ((audio.sample_rate != 44100 && audio.sample_rate != 48000) || audio.channels != 2) {
            throw MediaPreparationException(MediaPreparationFailure::unsupported);
        }
        plan.selection = AudioSelectionStatus::selected;
        plan.source_id = 1;
        plan.source_family = SourceAudioFamily::mp3;
        plan.output_channels = audio.channels;
    } else {
        const auto inventory = probe_audio_only_tracks(source, context);
        if (std::any_of(
                inventory.tracks.begin(), inventory.tracks.end(),
                [](const SourceTrack& track) { return track.kind == SourceTrackKind::video; })) {
            throw MediaPreparationException(MediaPreparationFailure::unsupported);
        }
        const auto selected = select_source_audio(inventory, selected_id);
        plan.selection = selected.status;
        if (selected.status != AudioSelectionStatus::selected) {
            if (selected.status == AudioSelectionStatus::video_only) {
                throw MediaPreparationException(MediaPreparationFailure::unsupported);
            }
            return plan;
        }
        const auto& track = inventory.tracks.at(selected.track_index);
        plan.source_id = track.id;
        if (is_standalone_audio(inventory.container)) {
            plan.source_family =
                inventory.container == SourceContainer::flac   ? SourceAudioFamily::flac
                : inventory.container == SourceContainer::wave ? SourceAudioFamily::pcm
                : track.codec == "OPUS"                        ? SourceAudioFamily::opus
                                                               : SourceAudioFamily::vorbis;
        } else {
            plan = plan_audio_delivery(inventory, AudioDeliveryProfile::apple_hls_v1, track.id);
            plan.profile = AudioDeliveryProfile::apple_audio_v1;
            if (plan.source_family == SourceAudioFamily::aac) {
                // Serving the original container cannot honor per-track filtering.
                // Require one track, an admitted AAC index and finite timeline.
                if (inventory.container != SourceContainer::mp4 || inventory.tracks.size() != 1 ||
                    plan.output_channels != 2) {
                    throw MediaPreparationException(MediaPreparationFailure::unsupported);
                }
                const RandomReader read = [&](std::uint64_t offset, std::uint8_t* output,
                                              std::size_t length) {
                    reader.read(offset, output, length);
                };
                const auto audio = read_mp4_source_audio(read, size, track.id);
                plan.output_channels = validate_profile_copy_audio(audio, read);
                (void)mp4_audio_presentation_samples(audio, inventory.movie_timescale);
            } else if (plan.source_family != SourceAudioFamily::flac &&
                       plan.source_family != SourceAudioFamily::pcm) {
                throw MediaPreparationException(MediaPreparationFailure::unsupported);
            }
        }
    }
    const bool direct = plan.source_family == SourceAudioFamily::aac ||
                        plan.source_family == SourceAudioFamily::mp3;
    plan.action = direct ? AudioDeliveryAction::progressive : AudioDeliveryAction::stereo_aac;
    plan.reason = direct ? AudioDeliveryReason::profile_progressive
                         : AudioDeliveryReason::profile_requires_stereo;
    plan.output_family = direct ? plan.source_family : SourceAudioFamily::aac;
    if (!direct) {
        plan.output_channels = 2;
    }
    return plan;
}
} // namespace
PreparedAutomaticMedia::~PreparedAutomaticMedia() {
    stop();
}
const AudioDeliveryPlan& PreparedAutomaticMedia::decision() const noexcept {
    return decision_;
}
const RemuxedHls& PreparedAutomaticMedia::presentation() const noexcept {
    return converted_ ? converted_->presentation() : copied_;
}
StereoAacHlsFailure PreparedAutomaticMedia::failure() const noexcept {
    return converted_ ? converted_->failure() : StereoAacHlsFailure::none;
}
std::uint32_t PreparedAutomaticMedia::authoring_flags() const noexcept {
    return converted_ ? converted_->authoring_flags()
                      : (copied_.segment_sizes_estimated ? authoring_copied_sizes_estimated : 0U);
}
void PreparedAutomaticMedia::stop() noexcept {
    if (converted_) {
        converted_->stop();
    }
    copied_.resources.clear();
}
std::unique_ptr<PreparedAutomaticMedia>
PreparedAutomaticMedia::open(MediaSource source, AacFragmentStore store, std::uint64_t quota,
                             const MediaReadContext& context, AudioDeliveryProfile profile,
                             AudioConversionPermission permission,
                             std::optional<std::uint64_t> selected_id) {
    try {
        check_preparation_context(context);
        if (permission != AudioConversionPermission::allow_stereo &&
            permission != AudioConversionPermission::preserve_original) {
            throw MediaPreparationException(MediaPreparationFailure::unsupported);
        }
        if (!source.size || !source.read_at) {
            throw MediaPreparationException(MediaPreparationFailure::source_error);
        }
        // Own one immutable size result across inventory and all preparation routes.
        const auto size = source.size();
        constexpr std::uint64_t maximum_automatic_source_bytes = 1ULL << 40;
        if (size > maximum_automatic_source_bytes) {
            throw MediaPreparationException(MediaPreparationFailure::resource_limit);
        }
        source.size = [size] { return size; };
        if (profile != AudioDeliveryProfile::apple_hls_v1 &&
            profile != AudioDeliveryProfile::apple_audio_v1) {
            throw MediaPreparationException(MediaPreparationFailure::unsupported);
        }
        auto decision =
            profile == AudioDeliveryProfile::apple_audio_v1
                ? audio_only_plan(source, size, context, selected_id)
                : plan_audio_delivery(probe_media_tracks(source, context), profile, selected_id);
        if (decision.selection == AudioSelectionStatus::invalid_selection) {
            throw MediaPreparationException(MediaPreparationFailure::invalid_selection);
        }
        if (decision.selection == AudioSelectionStatus::selection_required) {
            throw MediaPreparationException(MediaPreparationFailure::selection_required);
        }
        auto media = std::unique_ptr<PreparedAutomaticMedia>(new PreparedAutomaticMedia());
        if (decision.action == AudioDeliveryAction::stereo_aac) {
            if (permission == AudioConversionPermission::preserve_original) {
                throw MediaPreparationException(MediaPreparationFailure::unsupported);
            }
            if (!stereo_aac_available()) {
                throw MediaPreparationException(MediaPreparationFailure::backend_unavailable);
            }
            if (!store.seal || !store.read_at || !store.clear || quota == 0) {
                throw MediaPreparationException(MediaPreparationFailure::cache_error);
            }
            media->converted_ = profile == AudioDeliveryProfile::apple_audio_v1
                                    ? prepare_audio_only_media(std::move(source), std::move(store),
                                                               quota, context, decision.source_id)
                                    : prepare_stereo_media(std::move(source), std::move(store),
                                                           quota, context, decision.source_id);
        } else if (decision.action == AudioDeliveryAction::progressive) {
            const bool mp3 = decision.source_family == SourceAudioFamily::mp3;
            media->copied_.playlist_name = mp3 ? "audio.mp3" : "audio.m4a";
            media->copied_.resources.push_back({media->copied_.playlist_name,
                                                mp3 ? "audio/mpeg" : "audio/mp4",
                                                std::move(source),
                                                {}});
        } else if (decision.action == AudioDeliveryAction::copy) {
            media->copied_ = remux_selected_audio_to_hls(std::move(source), decision.source_id,
                                                         context, decision.output_channels);
        } else {
            media->copied_ =
                remux_to_hls(std::move(source), context.request_cancelled, true, &context);
        }
        check_preparation_context(context);
        media->decision_ = decision;
        return media;
    } catch (const MediaPreparationException&) {
        throw;
    } catch (const RemuxException& error) {
        throw MediaPreparationException(
            error.reason() == RemuxFailure::malformed   ? MediaPreparationFailure::malformed
            : error.reason() == RemuxFailure::too_large ? MediaPreparationFailure::resource_limit
                                                        : MediaPreparationFailure::unsupported);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        check_preparation_context(context);
        throw MediaPreparationException(MediaPreparationFailure::source_error);
    }
}
} // namespace send_airplay2::detail
