// SPDX-License-Identifier: Apache-2.0
#include "prepared_automatic_media.h"
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
        const auto inventory = probe_media_tracks(source, context);
        auto decision = plan_audio_delivery(inventory, profile, selected_id);
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
            media->converted_ = prepare_stereo_media(std::move(source), std::move(store), quota,
                                                     context, decision.source_id);
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
